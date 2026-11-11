/*
 * Map preview screen: sd:/maps/*.ltb -> map_view (MapViewV2).
 *
 * How the pieces fit together:
 *   - libs/map_view is hardware agnostic: files, memory and logging come from
 *     the mv_port hooks, so all this file has to do is point them at FatFs,
 *     RTT and the LVGL heap (which sits in the SDRAM, see lv_conf.h).
 *   - No task_create is injected, so the tile cache runs in "pump" mode:
 *     map_view's own lv_timer loads a couple of tiles per tick.
 *   - A tile is tile_px^2 * bytes_per_pixel (512px RGB565 = 512 KB), so the
 *     buffer has to come from the SDRAM-backed LVGL heap - the 64 KB of
 *     internal SRAM cannot hold a single tile.
 *   - The package header (city name) is read with libs/ltmb_parse directly;
 *     map_view only exposes the tile grid, not the metadata.
 */
#include "map_demo.h"

#include <stdio.h>
#include <string.h>

#include "SEGGER_RTT.h"
#include "ff.h"
#include "ltmb_parse.h"
#include "lvgl.h"

#include "board_lcd.h"
#include "board_touch.h"
#include "mv_port.h"
#include "mv_source.h"
#include "mv_tile_cache.h"
#include "mv_view.h"

#define RTT_CH        0
#define MAP_DIR       "sd:/maps"
#define MAP_MAX_FILES 8
#define MAP_TILE_BUDGET (5u * 1024u * 1024u + 512u * 1024u)  /* of the 6 MB LVGL heap */

/* Debug setup: only this package is opened (matched against the file name,
 * UTF-8 is fine), and instead of rotating through the maps the view walks a
 * diagonal from the north-west to the south-east corner.
 *
 * The walk is deliberately slow: a step has to be much smaller than one tile
 * (512 px here), otherwise every move invalidates the whole cache and the
 * screen shows empty tiles while the new ones are read from the card. */
#define MAP_ONLY_NAME  "衡阳"
#define MAP_PAN_MS      250   /* one small step every 250 ms */
#define MAP_PAN_PX      5    /* ~20 px per step = 80 px/s, 1/25 of a tile;
                               * slower costs less per step but the stutter
                               * comes from reading a 512 KB tile, so give the
                               * pump time to fetch the next one in advance */

/* Layout: the map is full screen and the metadata card floats on top of it,
 * docked to the right edge and slightly transparent. */
#define MAP_SIDE_W     (LCD_HDOT / 3)   /* floating card width: 1/3 of the screen */
#define MAP_CARD_H     180
#define MAP_CARD_OPA   LV_OPA_20
#define MAP_VIEW_W     LCD_HDOT
#define MAP_VIEW_H     LCD_VDOT

typedef struct {
    char path[160];    /* full FatFs path - UTF-8, can contain Chinese */
    char label[40];    /* what the screen can draw (8.3 short name) */
    char city[64];     /* place_name out of the package header */
} map_entry_t;

static map_entry_t s_maps[MAP_MAX_FILES];
static int         s_map_count;
static int         s_map_index;

#define MAP_BTN_SIZE   56   /* floating +/- zoom buttons */
#define MAP_MENU_W     420  /* map picker panel */
#define MAP_MENU_H     320

static lv_obj_t *s_map;
static lv_obj_t *s_lbl_name;      /* top bar: "MAP 2/3  <short name>" */
static lv_obj_t *s_lbl_city;
static lv_obj_t *s_lbl_level;
static lv_obj_t *s_lbl_tiles;
static lv_obj_t *s_lbl_bounds1;
static lv_obj_t *s_lbl_bounds2;
static lv_obj_t *s_lbl_view;

/* floating controls on top of the map */
static lv_obj_t *s_btn_zoom_in;
static lv_obj_t *s_btn_zoom_out;
static lv_obj_t *s_btn_menu;
static lv_obj_t *s_menu;          /* map picker panel (hidden by default) */
static lv_obj_t *s_menu_list;

/* touch probe: a dot + coordinates, so a wrong X/Y mirror is visible at once */
static lv_obj_t *s_touch_dot;
static lv_obj_t *s_touch_lbl;
static uint32_t  s_touch_last_ms;
static bool      s_touch_was_down;

/* diagonal walk: from/to in degrees, current step */
static double s_from_lat, s_from_lon, s_to_lat, s_to_lon;
static int    s_step;
static int    s_steps = 10;   /* recomputed from the map size in map_open() */
static int    s_have_range;
static int    s_user_took_over;   /* set on the first touch: stop the auto-pan */

/*---------------------------------------------------------------------------
 * mv_port: files -> FatFs, log -> RTT
 *-------------------------------------------------------------------------*/
static void *fs_open(const char *path, void *user)
{
    FIL *f;

    (void)user;
    f = lv_malloc(sizeof(FIL));
    if(f == NULL) return NULL;

    if(f_open(f, path, FA_READ) != FR_OK) {
        lv_free(f);
        return NULL;
    }
    return f;
}

static int fs_read(void *h, void *buf, int bytes, void *user)
{
    UINT br = 0;

    (void)user;
    if(f_read((FIL *)h, buf, (UINT)bytes, &br) != FR_OK) return -1;

    return (int)br;
}

static int fs_seek(void *h, long off, void *user)
{
    (void)user;
    return (f_lseek((FIL *)h, (FSIZE_t)off) == FR_OK) ? 0 : -1;
}

static void fs_close(void *h, void *user)
{
    (void)user;
    f_close((FIL *)h);
    lv_free(h);
}

static void *fs_opendir(const char *path, void *user)
{
    static DIR dir;

    (void)user;
    if(f_opendir(&dir, path) != FR_OK) return NULL;

    return &dir;
}

static const char *fs_readdir(void *d, void *user)
{
    static FILINFO fno;

    (void)user;
    if(f_readdir((DIR *)d, &fno) != FR_OK) return NULL;
    if(fno.fname[0] == '\0') return NULL;      /* end of the directory */

    return fno.fname;
}

static void fs_closedir(void *d, void *user)
{
    (void)user;
    f_closedir((DIR *)d);
}

static void map_log(int level, const char *tag, const char *fmt, va_list ap)
{
    static const char *lvl[4] = { "E", "W", "I", "D" };
    char buf[160];

    vsnprintf(buf, sizeof(buf), fmt, ap);
    SEGGER_RTT_printf(RTT_CH, "[map/%s/%s] %s\n", tag, lvl[level], buf);
}

void map_demo_init(void)
{
    mv_port_t port;

    memset(&port, 0, sizeof(port));
    port.fs.open     = fs_open;
    port.fs.read     = fs_read;
    port.fs.seek     = fs_seek;
    port.fs.close    = fs_close;
    port.fs.opendir  = fs_opendir;
    port.fs.readdir  = fs_readdir;
    port.fs.closedir = fs_closedir;
    port.log         = map_log;
    mv_port_set(&port);

    /* tiles are hundreds of KB each: they have to come out of the SDRAM heap */
    mv_port_use_lvgl_heap(true);

    /* no mv_os.task_create -> the cache falls back to pump mode */
    SEGGER_RTT_printf(RTT_CH, "map: port ready (FatFs + LVGL heap, %s)\n",
                      mv_port_has_thread() ? "thread" : "pump mode");
}

/*---------------------------------------------------------------------------
 * helpers
 *
 * Neither SEGGER_RTT_printf nor the nano-specs snprintf() can do %f, so a
 * degree value is split into an integer and a 5-digit fraction by hand.
 *-------------------------------------------------------------------------*/
static void fmt_deg(char *out, size_t n, double v)
{
    int   neg = v < 0.0;
    double a  = neg ? -v : v;
    int   ip  = (int)a;
    int   fp  = (int)((a - (double)ip) * 100000.0 + 0.5);

    snprintf(out, n, "%s%d.%05d", neg ? "-" : "", ip, fp);
}

static int is_ascii_printable(const char *s)
{
    while(*s != '\0') {
        unsigned char c = (unsigned char)*s++;
        if(c < 0x20 || c > 0x7E) return 0;
    }
    return 1;
}

static int ends_with_ltb(const char *name)
{
    size_t n = strlen(name);
    const char *ext;

    if(n < 4) return 0;
    ext = name + n - 4;

    return ext[0] == '.' && (ext[1] | 0x20) == 'l' && (ext[2] | 0x20) == 't' && (ext[3] | 0x20) == 'b';
}

/*---------------------------------------------------------------------------
 * the maps directory
 *-------------------------------------------------------------------------*/
static int maps_scan(void)
{
    static DIR dir;
    static FILINFO fno;
    FRESULT fr;

    s_map_count = 0;

    fr = f_opendir(&dir, MAP_DIR);
    if(fr != FR_OK) {
        SEGGER_RTT_printf(RTT_CH, "map: f_opendir(%s) -> %d\n", MAP_DIR, (int)fr);
        return 0;
    }

    while(s_map_count < MAP_MAX_FILES) {
        map_entry_t *e;

        fr = f_readdir(&dir, &fno);
        if(fr != FR_OK || fno.fname[0] == '\0') break;
        if(fno.fattrib & AM_DIR) continue;
        if(!ends_with_ltb(fno.fname)) continue;
        /* macOS drops a "._name" resource fork next to every file it copies
         * to a FAT card - it is not an LTMB package, skip it. */
        if(fno.fname[0] == '.' || strncmp(fno.fname, "._", 2) == 0) continue;

        e = &s_maps[s_map_count++];
        snprintf(e->path, sizeof(e->path), "%s/%s", MAP_DIR, fno.fname);

        /* No CJK glyphs in this firmware: show the 8.3 short name (ASCII),
         * the real name only goes to the RTT log. */
        snprintf(e->label, sizeof(e->label), "%s",
                 (fno.altname[0] != '\0') ? fno.altname : fno.fname);
        e->city[0] = '\0';

        SEGGER_RTT_printf(RTT_CH, "map: %d = %s (label %s)\n",
                          s_map_count, e->path, e->label);
    }
    f_closedir(&dir);

    SEGGER_RTT_printf(RTT_CH, "map: %d file(s) in %s\n", s_map_count, MAP_DIR);
    return s_map_count;
}

/*---------------------------------------------------------------------------
 * which package to open
 *-------------------------------------------------------------------------*/
static int maps_find(const char *needle)
{
    int i;

    for(i = 0; i < s_map_count; i++) {
        if(strstr(s_maps[i].path, needle) != NULL) return i;
    }
    return (s_map_count > 0) ? 0 : -1;
}

/*---------------------------------------------------------------------------
 * package metadata (city name) - read with ltmb_parse on the same FatFs ops
 *-------------------------------------------------------------------------*/
static void *lt_open(void *context, const char *path)
{
    (void)context;
    return fs_open(path, NULL);
}

static void lt_close(void *handle)
{
    fs_close(handle, NULL);
}

static int32_t lt_read(void *handle, uint8_t *buffer, uint32_t size)
{
    return (int32_t)fs_read(handle, buffer, (int)size, NULL);
}

static int32_t lt_seek(void *handle, uint64_t offset)
{
    /* FatFS 向后 seek 会重走 FAT 链（大文件上很慢），已经在目标位置就直接返回 */
    if(f_tell((FIL *)handle) == (FSIZE_t)offset) return 0;

    return (int32_t)fs_seek(handle, (long)offset, NULL);
}

static uint64_t lt_size(void *handle)
{
    return (uint64_t)f_size((FIL *)handle);
}

static const LtmbFileOps s_lt_ops = {
    lt_open, lt_close, lt_read, lt_seek, lt_size
};

/* v3: the handle carries a 2 KB header (levels[24] table) - never on the stack */
static LtmbHandle s_lt;

static void read_city(const char *path, char *out, size_t n)
{
    unsigned i;

    out[0] = '\0';

    ltmb_handle_init(&s_lt);
    LtmbError err = ltmb_open_with_ops(&s_lt, &s_lt_ops, path);
    if(err != LTMB_OK) {
        uint8_t hdr[12] = { 0 };
        UINT br = 0;
        void *probe = fs_open(path, NULL);

        if(probe != NULL) {
            f_read((FIL *)probe, hdr, sizeof(hdr), &br);
            fs_close(probe, NULL);
        }

        /* 新版解析器只认 v3（一个包里多个缩放级）。旧 v1/v2 包返回 -8。
         * 直接把前 12 字节打出来，一眼看清是哪个版本、是不是 .ltb。 */
        SEGGER_RTT_printf(RTT_CH,
                          "map: header read failed (%d, %s); got %u B: "
                          "magic=%c%c%c%c version=%u header_size=%u\n",
                          (int)err, ltmb_error_string(err), (unsigned)br,
                          hdr[0], hdr[1], hdr[2], hdr[3],
                          (unsigned)((uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                                     ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24)),
                          (unsigned)((uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8) |
                                     ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24)));
        SEGGER_RTT_printf(RTT_CH, "map: need LTMB v3 packages (re-export with the new tool)\n");
        return;
    }

    snprintf(out, n, "%s", s_lt.header.place_name);

    /* file size straight from FatFs, for comparison with the header */
    void *fh = fs_open(path, NULL);
    uint32_t fsize = 0;
    if(fh != NULL) {
        fsize = (uint32_t)f_size((FIL *)fh);
        fs_close(fh, NULL);
    }

    SEGGER_RTT_printf(RTT_CH, "map: place_name = \"%s\"\n", s_lt.header.place_name);
    SEGGER_RTT_printf(RTT_CH, "map: ver=%u fmt=%u levels=%u file=%u KB\n",
                      (unsigned)s_lt.header.version,
                      (unsigned)s_lt.header.format,
                      (unsigned)s_lt.header.level_count,
                      (unsigned)(fsize / 1024));

    /* one line per zoom level stored in the package (v3 multi-level format) */
    for(i = 0; i < (unsigned)s_lt.header.level_count && i < LTMB_MAX_ZOOM_LEVELS; i++) {
        const LtmbLevel *lv = &s_lt.header.levels[i];

        if(lv->data_size == 0) continue;   /* level not present */

        SEGGER_RTT_printf(RTT_CH,
                          "map:   z%u %ux%u @%upx tiles=%u unique=%u off=%u size=%u KB\n",
                          (unsigned)lv->zoom,
                          (unsigned)lv->tile_cols,
                          (unsigned)lv->tile_rows,
                          (unsigned)lv->tile_size,
                          (unsigned)lv->total_tiles,
                          (unsigned)lv->unique_tile_count,
                          (unsigned)lv->data_offset,
                          (unsigned)(lv->data_size / 1024));
    }

    ltmb_close(&s_lt);
}

/*---------------------------------------------------------------------------
 * open one package
 *-------------------------------------------------------------------------*/
static void pan_apply(void);
static void level_label_update(void);
static void menu_rebuild(void);

/* bring-up probes: is the tile data readable, does the cache fill up */
static void probe_tile(mv_source_t *src, const mv_grid_t *g)
{
    uint8_t *buf;
    uint32_t i;
    int n;

    buf = lv_malloc(g->bytes_per_tile);
    if(buf == NULL) {
        SEGGER_RTT_printf(RTT_CH, "map: probe: out of memory (%u B)\n",
                          (unsigned)g->bytes_per_tile);
        return;
    }

    /* corners + centre + a couple in between: is 0xFF the tile content or
     * are we reading the wrong place in the file? */
    const int cols[5] = { 0, g->cols / 2, g->cols - 1, 1, g->cols / 3 };
    const int rows[5] = { 0, g->rows / 2, g->rows - 1, 1, g->rows / 3 };

    for(n = 0; n < 5; n++) {
        uint32_t non_ff = 0, samples = 0;
        bool ok;

        memset(buf, 0, g->bytes_per_tile);
        ok = mv_source_read_tile(src, 0, cols[n], rows[n], buf, g->bytes_per_tile);

        for(i = 0; i < g->bytes_per_tile; i += 256) {
            samples++;
            if(buf[i] != 0xFF) non_ff++;
        }

        SEGGER_RTT_printf(RTT_CH,
                          "map: tile c%2d r%2d ok=%d cf=%d first=%02X%02X %02X%02X non-FF=%u/%u\n",
                          cols[n], rows[n], (int)ok, (int)g->cf,
                          buf[0], buf[1], buf[2], buf[3],
                          (unsigned)non_ff, (unsigned)samples);
    }
    lv_free(buf);
}

static void print_cache_stats(void)
{
    mv_tile_cache_t *cache = mv_view_get_tile_cache(s_map);
    mv_tile_cache_stats_t st;

    if(cache == NULL) {
        SEGGER_RTT_printf(RTT_CH, "map: cache: none\n");
        return;
    }
    mv_tile_cache_get_stats(cache, &st);
    SEGGER_RTT_printf(RTT_CH,
                      "map: cache cap=%d resident=%d loading=%d missing=%d "
                      "loads=%u hits=%u misses=%u evict=%u\n",
                      st.capacity, st.resident, st.loading, st.missing,
                      (unsigned)st.loads, (unsigned)st.hits,
                      (unsigned)st.misses, (unsigned)st.evictions);
}

static void map_open(int index)
{
    map_entry_t *e = &s_maps[index];
    mv_source_t *src;
    mv_grid_t grid;
    int capacity = 6;
    char buf[96];

    SEGGER_RTT_printf(RTT_CH, "map: open %s\n", e->path);

    read_city(e->path, e->city, sizeof(e->city));

    if(e->city[0] == '\0' || !is_ascii_printable(e->city)) {
        snprintf(buf, sizeof(buf), "city: %s", e->city[0] ? e->city : "(none)");
    }
    else {
        snprintf(buf, sizeof(buf), "city: %s", e->city);
    }
    lv_label_set_text(s_lbl_city, buf);

    src = mv_source_open_ltmb(e->path, NULL);
    if(src == NULL) {
        SEGGER_RTT_printf(RTT_CH, "map: ltmb open failed\n");
        lv_label_set_text(s_lbl_tiles, "open failed: need LTMB v3");
        lv_label_set_text(s_lbl_city, "re-export .ltb with the v3 tool");
        lv_label_set_text(s_lbl_level, "no map loaded");
        lv_label_set_text(s_lbl_view, "check RTT for the header dump");
        return;
    }

    if(mv_source_grid(src, 0, &grid)) {
        uint32_t per = grid.bytes_per_tile ? grid.bytes_per_tile : 1;

        capacity = (int)(MAP_TILE_BUDGET / per);
        if(capacity < 4) capacity = 4;
        if(capacity > 32) capacity = 32;

        snprintf(buf, sizeof(buf), "tiles: %dx%d @ %dpx = %u blocks, %u KB each, %d slots",
                 grid.cols, grid.rows, grid.tile_px,
                 (unsigned)(grid.cols * grid.rows),
                 (unsigned)(per / 1024), capacity);
        lv_label_set_text(s_lbl_tiles, buf);
        SEGGER_RTT_printf(RTT_CH, "map: %s\n", buf);
    }

    if(!mv_view_set_source(s_map, src, capacity)) {
        SEGGER_RTT_printf(RTT_CH, "map: set_source failed\n");
        mv_source_close(src);
        lv_label_set_text(s_lbl_tiles, "set_source failed");
        return;
    }

    probe_tile(src, &grid);
    print_cache_stats();
    level_label_update();

    /* show the area the package covers, and write it on screen */
    double north, south, east, west;
    if(mv_source_ltmb_bounds(src, &north, &south, &east, &west)) {
        char n[24], s[24], ee[24], w[24];

        fmt_deg(n, sizeof(n), north);
        fmt_deg(s, sizeof(s), south);
        fmt_deg(ee, sizeof(ee), east);
        fmt_deg(w, sizeof(w), west);

        snprintf(buf, sizeof(buf), "N %s   S %s", n, s);
        lv_label_set_text(s_lbl_bounds1, buf);
        snprintf(buf, sizeof(buf), "E %s   W %s", ee, w);
        lv_label_set_text(s_lbl_bounds2, buf);

        SEGGER_RTT_printf(RTT_CH, "map: bounds N%s S%s E%s W%s\n", n, s, ee, w);

        /* Diagonal walk: keep half a viewport away from the edges so the
         * screen stays filled with tiles at both ends of the line. */
        if(grid.rows > 0 && grid.cols > 0 && grid.tile_px > 0) {
            double h_px = (double)grid.rows * grid.tile_px;
            double w_px = (double)grid.cols * grid.tile_px;
            double dlat = (north - south) / h_px;
            double dlon = (east - west) / w_px;
            double inset_lat = dlat * (double)MAP_VIEW_H / 2.0;
            double inset_lon = dlon * (double)MAP_VIEW_W / 2.0;

            /* north-west -> south-east: lat goes down, lon goes up */
            s_from_lat = north - inset_lat;
            s_from_lon = west + inset_lon;
            s_to_lat   = south + inset_lat;
            s_to_lon   = east - inset_lon;

            /* one step is MAP_PAN_PX pixels: the horizontal span decides how
             * many steps the whole diagonal takes */
            double span_px = w_px - (double)LCD_HDOT;
            s_steps = (span_px > 0.0) ? (int)(span_px / (double)MAP_PAN_PX) + 1 : 2;
            if(s_steps < 2) s_steps = 2;

            s_have_range = 1;
            s_step = 0;
        }
    }
    else {
        lv_label_set_text(s_lbl_bounds1, "bounds: n/a");
        lv_label_set_text(s_lbl_bounds2, "");
        s_have_range = 0;
    }

    pan_apply();

    snprintf(buf, sizeof(buf), "MAP %d/%d  %s", index + 1, s_map_count, e->label);
    lv_label_set_text(s_lbl_name, buf);
}

/*---------------------------------------------------------------------------
 * the diagonal walk
 *-------------------------------------------------------------------------*/
static void pan_apply(void)
{
    char buf[96];
    char lat_s[24], lon_s[24];
    double t, lat, lon;

    if(!s_have_range) {
        lv_label_set_text(s_lbl_view, "view: no bounds");
        return;
    }

    t = (s_steps > 1) ? (double)s_step / (double)(s_steps - 1) : 0.0;
    lat = s_from_lat + (s_to_lat - s_from_lat) * t;
    lon = s_from_lon + (s_to_lon - s_from_lon) * t;

    mv_view_set_center(s_map, lat, lon);

    fmt_deg(lat_s, sizeof(lat_s), lat);
    fmt_deg(lon_s, sizeof(lon_s), lon);
    snprintf(buf, sizeof(buf), "view: step %d/%d  %s, %s",
             s_step + 1, s_steps, lat_s, lon_s);
    lv_label_set_text(s_lbl_view, buf);

    /* 200 ms 一步，日志每 5 秒打一次就够了 */
    if(s_step % (5000 / MAP_PAN_MS) == 0) {
        SEGGER_RTT_printf(RTT_CH, "map: %s\n", buf);
        print_cache_stats();   /* resident/loads should climb as tiles arrive */
    }
}

/* The first touch switches from the automatic walk to real dragging: from
 * then on the map only moves when the finger does. */
static void map_pressed_cb(lv_event_t *e)
{
    (void)e;

    /* 点按钮时不该出现这一行：出现就说明触点落到了地图上（坐标镜像不对） */
    SEGGER_RTT_printf(RTT_CH, "map: map object got PRESSED\n");

    if(!s_user_took_over) {
        s_user_took_over = 1;
        SEGGER_RTT_printf(RTT_CH, "map: touch -> drag mode, auto-pan stopped\n");
    }
}

static void pan_timer(lv_timer_t *timer)
{
    (void)timer;

    if(s_user_took_over) return;   /* the user is dragging, do not fight him */

    if(s_map_count == 0) {
        if(maps_scan() == 0) return;         /* card or directory missing */
        s_map_index = maps_find(MAP_ONLY_NAME);
        if(s_map_index < 0) return;
        map_open(s_map_index);
        return;
    }

    s_step = (s_step + 1) % s_steps;
    pan_apply();
}

/*---------------------------------------------------------------------------
 * zoom buttons + map picker menu
 *
 * The LTMB packages can carry several zoom levels in one file now, so the
 * view's level table is no longer a single entry: "+" / "-" walk it and keep
 * the centre coordinates. The picker lists every .ltb the scan found.
 *-------------------------------------------------------------------------*/
static void level_label_update(void)
{
    char buf[96];

    if(mv_view_get_source(s_map) == NULL) return;

    snprintf(buf, sizeof(buf), "level: z%d  L%d/%d",
             mv_view_get_zoom(s_map),
             mv_view_get_level(s_map) + 1,
             mv_view_get_level_count(s_map));
    lv_label_set_text(s_lbl_level, buf);
}

static void zoom_btn_cb(lv_event_t *e)
{
    bool in = (bool)(uintptr_t)lv_event_get_user_data(e);

    s_user_took_over = 1;          /* buttons mean the user is driving now */

    if(mv_view_get_source(s_map) == NULL) {
        SEGGER_RTT_printf(RTT_CH, "map: zoom %s pressed but no map is loaded\n",
                          in ? "in" : "out");
        lv_label_set_text(s_lbl_level, "no map - cannot zoom (need v3 .ltb)");
        return;
    }

    if(in) {
        mv_view_zoom_in(s_map);
    }
    else {
        mv_view_zoom_out(s_map);
    }
    level_label_update();
    print_cache_stats();
    SEGGER_RTT_printf(RTT_CH, "map: zoom %s -> L%d/%d z%d\n",
                      in ? "in" : "out",
                      mv_view_get_level(s_map) + 1,
                      mv_view_get_level_count(s_map),
                      mv_view_get_zoom(s_map));
}

static void menu_open(bool show)
{
    if(s_menu == NULL) return;

    if(show) {
        menu_rebuild();            /* pick up maps scanned after boot */
        lv_obj_remove_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_menu);
    }
    else {
        lv_obj_add_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
    }
    SEGGER_RTT_printf(RTT_CH, "map: menu %s\n", show ? "open" : "close");
}

static void menu_btn_cb(lv_event_t *e)
{
    (void)e;
    SEGGER_RTT_printf(RTT_CH, "map: MAPS clicked\n");
    menu_open(lv_obj_has_flag(s_menu, LV_OBJ_FLAG_HIDDEN));
}

static void menu_close_cb(lv_event_t *e)
{
    (void)e;
    menu_open(false);
}

static void mirror_btn_cb(lv_event_t *e)
{
    int mx, my;

    (void)e;
    board_touch_get_mirror(&mx, &my);

    /* 00 -> 01 -> 10 -> 11 -> 00，点着试，红点跟手就对了 */
    if(!mx && my)        { mx = 1; my = 0; }
    else if(mx && !my)   { mx = 1; my = 1; }
    else if(mx && my)    { mx = 0; my = 0; }
    else                 { mx = 0; my = 1; }

    board_touch_set_mirror(mx, my);
}

static void menu_pick_cb(lv_event_t *e)
{
    int index = (int)(uintptr_t)lv_event_get_user_data(e);

    menu_open(false);
    if(index < 0 || index >= s_map_count) return;

    SEGGER_RTT_printf(RTT_CH, "map: pick %d (%s)\n", index, s_maps[index].label);
    s_user_took_over = 1;          /* chosen by hand: no more auto-pan */
    s_map_index = index;
    map_open(index);
}

/* one list row per .ltb found in sd:/maps */
static void menu_rebuild(void)
{
    int i;
    char buf[64];

    if(s_menu_list == NULL) return;

    lv_obj_clean(s_menu_list);

    if(s_map_count == 0) {
        lv_obj_t *lbl = lv_label_create(s_menu_list);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x8fa0b3), 0);
        lv_label_set_text(lbl, "no .ltb in sd:/maps");
        return;
    }

    for(i = 0; i < s_map_count; i++) {
        lv_obj_t *btn = lv_button_create(s_menu_list);
        lv_obj_set_size(btn, MAP_MENU_W - 40, 44);
        lv_obj_set_style_bg_color(btn, (i == s_map_index) ? lv_color_hex(0x1c6dd0)
                                                          : lv_color_hex(0x1a2c44), 0);
        lv_obj_set_style_radius(btn, 8, 0);

        snprintf(buf, sizeof(buf), "%d  %s", i + 1, s_maps[i].label);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xe6edf3), 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_width(lbl, MAP_MENU_W - 60);
        lv_label_set_text(lbl, buf);
        lv_obj_center(lbl);

        /* the row index rides in user_data */
        lv_obj_add_event_cb(btn, menu_pick_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
}

/*---------------------------------------------------------------------------
 * touch probe
 *
 * 把 LVGL 实际收到的坐标画出来：红点不跟手 = 镜像方向错了（菜单里 FLIP
 * 按钮可现场切换），点按钮没反应 = CLICKED 没发或点到了别处。
 *-------------------------------------------------------------------------*/
static void touch_probe_timer(lv_timer_t *t)
{
    lv_indev_t *indev = NULL;
    lv_point_t p;
    bool down;
    char buf[48];
    static int32_t last_x = -1, last_y = -1;

    (void)t;
    while((indev = lv_indev_get_next(indev)) != NULL) {
        if(lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) break;
    }
    if(indev == NULL || s_touch_dot == NULL) return;

    lv_indev_get_point(indev, &p);
    down = (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);

    if(down) {
        s_touch_last_ms = lv_tick_get();
        if(!s_touch_was_down) {
            SEGGER_RTT_printf(RTT_CH, "map: LVGL PRESSED at %d,%d\n", (int)p.x, (int)p.y);
            s_touch_was_down = true;
            lv_obj_move_foreground(s_touch_dot);
            lv_obj_move_foreground(s_touch_lbl);
        }
        if(p.x != last_x || p.y != last_y) {
            last_x = p.x;
            last_y = p.y;
            lv_obj_set_pos(s_touch_dot, p.x - 7, p.y - 7);
            snprintf(buf, sizeof(buf), "touch %3d,%3d", (int)p.x, (int)p.y);
            lv_label_set_text(s_touch_lbl, buf);
        }
        lv_obj_remove_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_touch_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        if(s_touch_was_down) {
            SEGGER_RTT_printf(RTT_CH, "map: LVGL RELEASED at %d,%d\n", (int)p.x, (int)p.y);
            s_touch_was_down = false;
        }
        /* 松手后留 3 秒，便于看清最后一次落点 */
        if(lv_tick_get() - s_touch_last_ms > 3000) {
            lv_obj_add_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_touch_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/*---------------------------------------------------------------------------
 * screen
 *-------------------------------------------------------------------------*/
static lv_obj_t *info_label(lv_obj_t *parent, lv_obj_t *prev)
{
    lv_obj_t *lbl = lv_label_create(parent);

    lv_obj_set_width(lbl, MAP_SIDE_W - 20);
    /* 一行装不下的就横向滚动，不再换行占高度 */
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    if(prev == NULL) {
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 10, 8);
    }
    else {
        lv_obj_align_to(lbl, prev, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    }

    return lbl;
}

void map_demo_create(void)
{
    lv_obj_t *scr;
    lv_obj_t *bar;

    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f1b2a), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* the map: the whole screen */
    s_map = mv_view_create(scr);
    lv_obj_set_size(s_map, MAP_VIEW_W, MAP_VIEW_H);
    lv_obj_align(s_map, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_map, lv_color_hex(0x203040), 0);
    lv_obj_set_style_bg_opa(s_map, LV_OPA_COVER, 0);
    mv_view_set_debug_overlay(s_map, true);

    /* drag the map with the finger; the first press stops the auto-pan */
    lv_obj_add_event_cb(s_map, map_pressed_cb, LV_EVENT_PRESSED, NULL);

    /* touch panel (GT911 on I2C1) -> LVGL pointer device */
    if(board_touch_init() == 0) {
        lv_port_indev_init();
        SEGGER_RTT_printf(RTT_CH, "map: touch ready - drag to move the map\n");
    }
    else {
        SEGGER_RTT_printf(RTT_CH, "map: no touch panel, keeping the auto-pan\n");
    }

    /* floating card on top of the map: created after it, so it is drawn last.
     * Semi transparent, so the tiles stay visible underneath. */
    bar = lv_obj_create(scr);
    lv_obj_set_size(bar, MAP_SIDE_W, MAP_CARD_H);
    lv_obj_align(bar, LV_ALIGN_TOP_RIGHT, -10, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0b1626), 0);
    lv_obj_set_style_bg_opa(bar, MAP_CARD_OPA, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    /* not clickable, so a drag that starts over the card still reaches the
     * map underneath */
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_name = lv_label_create(bar);
    lv_obj_set_width(s_lbl_name, MAP_SIDE_W - 20);
    lv_obj_set_style_text_font(s_lbl_name, &lv_font_montserrat_20, 0);
    lv_label_set_long_mode(s_lbl_name, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_label_set_text(s_lbl_name, "MAP");
    lv_obj_align(s_lbl_name, LV_ALIGN_TOP_LEFT, 10, 8);

    s_lbl_city    = info_label(bar, s_lbl_name);
    s_lbl_level   = info_label(bar, s_lbl_city);
    s_lbl_tiles   = info_label(bar, s_lbl_level);
    s_lbl_bounds1 = info_label(bar, s_lbl_tiles);
    s_lbl_bounds2 = info_label(bar, s_lbl_bounds1);
    s_lbl_view    = info_label(bar, s_lbl_bounds2);

    lv_label_set_text(s_lbl_city, "city: -");
    lv_label_set_text(s_lbl_level, "level: -");
    lv_label_set_text(s_lbl_tiles, "tiles: -");
    lv_label_set_text(s_lbl_bounds1, "");
    lv_label_set_text(s_lbl_bounds2, "");
    lv_label_set_text(s_lbl_view, "view: -");

    /* zoom buttons: bottom-right corner, above the debug overlay */
    s_btn_zoom_in = lv_button_create(scr);
    lv_obj_set_size(s_btn_zoom_in, MAP_BTN_SIZE, MAP_BTN_SIZE);
    lv_obj_align(s_btn_zoom_in, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_set_style_bg_color(s_btn_zoom_in, lv_color_hex(0x1c6dd0), 0);
    lv_obj_set_style_radius(s_btn_zoom_in, 12, 0);
    lv_obj_add_event_cb(s_btn_zoom_in, zoom_btn_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)true);
    lv_obj_t *zi = lv_label_create(s_btn_zoom_in);
    lv_obj_set_style_text_font(zi, &lv_font_montserrat_24, 0);
    lv_label_set_text(zi, "+");
    lv_obj_center(zi);

    s_btn_zoom_out = lv_button_create(scr);
    lv_obj_set_size(s_btn_zoom_out, MAP_BTN_SIZE, MAP_BTN_SIZE);
    lv_obj_align(s_btn_zoom_out, LV_ALIGN_BOTTOM_RIGHT, -20, -20 - MAP_BTN_SIZE - 12);
    lv_obj_set_style_bg_color(s_btn_zoom_out, lv_color_hex(0x1c6dd0), 0);
    lv_obj_set_style_radius(s_btn_zoom_out, 12, 0);
    lv_obj_add_event_cb(s_btn_zoom_out, zoom_btn_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)false);
    lv_obj_t *zo = lv_label_create(s_btn_zoom_out);
    lv_obj_set_style_text_font(zo, &lv_font_montserrat_24, 0);
    lv_label_set_text(zo, "-");
    lv_obj_center(zo);

    /* "MAPS" button: top-left, opens the map picker */
    s_btn_menu = lv_button_create(scr);
    lv_obj_set_size(s_btn_menu, 96, 44);
    lv_obj_align(s_btn_menu, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_set_style_bg_color(s_btn_menu, lv_color_hex(0x1c6dd0), 0);
    lv_obj_set_style_bg_opa(s_btn_menu, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_btn_menu, 10, 0);
    lv_obj_add_event_cb(s_btn_menu, menu_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ml = lv_label_create(s_btn_menu);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_20, 0);
    lv_label_set_text(ml, "MAPS");
    lv_obj_center(ml);

    /* map picker panel: hidden until MAPS is tapped */
    s_menu = lv_obj_create(scr);
    lv_obj_set_size(s_menu, MAP_MENU_W, MAP_MENU_H);
    lv_obj_align(s_menu, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_menu, lv_color_hex(0x0b1626), 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_menu, lv_color_hex(0x1c6dd0), 0);
    lv_obj_set_style_border_width(s_menu, 1, 0);
    lv_obj_set_style_radius(s_menu, 12, 0);
    lv_obj_set_style_pad_all(s_menu, 12, 0);
    lv_obj_add_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_menu, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mtitle = lv_label_create(s_menu);
    lv_obj_set_style_text_font(mtitle, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(mtitle, lv_color_hex(0xe6edf3), 0);
    lv_label_set_text(mtitle, "Select map (sd:/maps)");
    lv_obj_align(mtitle, LV_ALIGN_TOP_LEFT, 4, 4);

    lv_obj_t *mclose = lv_button_create(s_menu);
    lv_obj_set_size(mclose, 44, 32);
    lv_obj_align(mclose, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(mclose, lv_color_hex(0x33415c), 0);
    lv_obj_set_style_radius(mclose, 8, 0);
    lv_obj_add_event_cb(mclose, menu_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mx = lv_label_create(mclose);
    lv_label_set_text(mx, "X");
    lv_obj_center(mx);

    /* 现场切换触摸 X/Y 镜像（00/01/10/11 循环），红点跟手就停 */
    lv_obj_t *mflip = lv_button_create(s_menu);
    lv_obj_set_size(mflip, 76, 32);
    lv_obj_align(mflip, LV_ALIGN_TOP_RIGHT, -52, 0);
    lv_obj_set_style_bg_color(mflip, lv_color_hex(0x33415c), 0);
    lv_obj_set_style_radius(mflip, 8, 0);
    lv_obj_add_event_cb(mflip, mirror_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mfl = lv_label_create(mflip);
    lv_label_set_text(mfl, "FLIP");
    lv_obj_center(mfl);

    /* scrollable list below the title row */
    s_menu_list = lv_obj_create(s_menu);
    lv_obj_set_size(s_menu_list, MAP_MENU_W - 24, MAP_MENU_H - 56);
    lv_obj_align(s_menu_list, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(s_menu_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_menu_list, 0, 0);
    lv_obj_set_style_pad_all(s_menu_list, 4, 0);
    lv_obj_set_flex_flow(s_menu_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_menu_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_menu_list, 8, 0);

    /* touch probe: sits above everything but is NOT clickable, so it can
     * never steal a press from the buttons */
    s_touch_dot = lv_obj_create(scr);
    lv_obj_set_size(s_touch_dot, 14, 14);
    lv_obj_set_style_radius(s_touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_touch_dot, lv_color_hex(0xff3b30), 0);
    lv_obj_set_style_border_width(s_touch_dot, 0, 0);
    lv_obj_set_style_pad_all(s_touch_dot, 0, 0);
    lv_obj_clear_flag(s_touch_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_touch_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);

    s_touch_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(s_touch_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_touch_lbl, lv_color_hex(0xff8a80), 0);
    lv_label_set_text(s_touch_lbl, "touch");
    lv_obj_align(s_touch_lbl, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_flag(s_touch_lbl, LV_OBJ_FLAG_HIDDEN);

    lv_screen_load(scr);

    if(maps_scan() > 0) {
        s_map_index = maps_find(MAP_ONLY_NAME);
        if(s_map_index >= 0) {
            map_open(s_map_index);
        }
        else {
            lv_label_set_text(s_lbl_name, "not found: " MAP_ONLY_NAME);
        }
    }
    else {
        lv_label_set_text(s_lbl_name, "no .ltb in sd:/maps");
        lv_label_set_text(s_lbl_city, "insert the card / check the path");
    }

    lv_timer_create(pan_timer, MAP_PAN_MS, NULL);
    lv_timer_create(touch_probe_timer, 30, NULL);
}
