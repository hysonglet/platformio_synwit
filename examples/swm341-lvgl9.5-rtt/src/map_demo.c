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
#define MAP_CARD_OPA   LV_OPA_70
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

static lv_obj_t *s_map;
static lv_obj_t *s_lbl_name;      /* top bar: "MAP 2/3  <short name>" */
static lv_obj_t *s_lbl_city;
static lv_obj_t *s_lbl_level;
static lv_obj_t *s_lbl_tiles;
static lv_obj_t *s_lbl_bounds1;
static lv_obj_t *s_lbl_bounds2;
static lv_obj_t *s_lbl_view;

/* diagonal walk: from/to in degrees, current step */
static double s_from_lat, s_from_lon, s_to_lat, s_to_lon;
static int    s_step;
static int    s_steps = 10;   /* recomputed from the map size in map_open() */
static int    s_have_range;

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

static int32_t lt_seek(void *handle, uint32_t offset)
{
    /* FatFS 向后 seek 会重走 FAT 链（大文件上很慢），已经在目标位置就直接返回 */
    if(f_tell((FIL *)handle) == (FSIZE_t)offset) return 0;

    return (int32_t)fs_seek(handle, (long)offset, NULL);
}

static uint32_t lt_size(void *handle)
{
    return (uint32_t)f_size((FIL *)handle);
}

static const LtmbFileOps s_lt_ops = {
    lt_open, lt_close, lt_read, lt_seek, lt_size
};

/* the handle carries a 1 KB header - never on the stack */
static LtmbHandle s_lt;

static void read_city(const char *path, char *out, size_t n)
{
    out[0] = '\0';

    ltmb_handle_init(&s_lt);
    if(ltmb_open_with_ops(&s_lt, &s_lt_ops, path) != LTMB_OK) {
        SEGGER_RTT_printf(RTT_CH, "map: header read failed\n");
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

    SEGGER_RTT_printf(RTT_CH, "map: place_name = \"%s\", zoom = %u, %ux%u @ %upx\n",
                      s_lt.header.place_name,
                      (unsigned)s_lt.header.zoom,
                      (unsigned)s_lt.header.tile_cols,
                      (unsigned)s_lt.header.tile_rows,
                      (unsigned)s_lt.header.tile_size);
    SEGGER_RTT_printf(RTT_CH,
                      "map: ver=%u fmt=%u bpp=%u tiles=%u data_off=%u data_size=%u "
                      "unique=%u file=%u KB (%u KB expected)\n",
                      (unsigned)s_lt.header.version,
                      (unsigned)s_lt.header.format,
                      (unsigned)s_lt.header.bytes_per_pixel,
                      (unsigned)s_lt.header.total_tiles,
                      (unsigned)s_lt.header.data_offset,
                      (unsigned)s_lt.header.data_size,
                      (unsigned)s_lt.header.unique_tile_count,
                      (unsigned)(fsize / 1024),
                      (unsigned)(s_lt.header.total_tiles * 512 * 512 * 2 / 1024));

    ltmb_close(&s_lt);
}

/*---------------------------------------------------------------------------
 * open one package
 *-------------------------------------------------------------------------*/
static void pan_apply(void);

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
        lv_label_set_text(s_lbl_tiles, "open failed");
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

        snprintf(buf, sizeof(buf), "level: z%d (%d of %d)", grid.zoom, 1,
                 mv_source_level_count(src));
        lv_label_set_text(s_lbl_level, buf);
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

static void pan_timer(lv_timer_t *timer)
{
    (void)timer;

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
}
