/*
 * Skeleton lizard - port of examples/swm341-lizard/index.html to LVGL 9.5.
 *
 * The browser version draws with Canvas2D. On the MCU the same shapes are
 * rasterised by hand into one RGB565 buffer (an lv_canvas), then LVGL blits
 * that canvas into the LCD framebuffer. Only the primitives the drawing needs
 * are implemented: alpha-blended pixels, thick/thin lines, disks, rotated
 * ellipses, quadratic bezier sampling and a scanline polygon filler.
 *
 * Body motion is unchanged from the HTML: a 26 segment chain whose head chases
 * the target with a serpentine sway, every segment keeping SPACING from its
 * parent; four legs use a two-bone IK solution driven by a gait phase.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lizard.h"
#include "board_lcd.h"
#include "board_touch.h"

#define LIZ_W        LCD_HDOT
#define LIZ_H        LCD_VDOT
#define LIZ_N        26

/* Overall size: 2/3 of the original specimen. Applied to every geometric
 * dimension and to the step length so the proportions (and apparent gait)
 * stay identical, only the lizard gets smaller. */
#define LIZ_SCALE    (2.0f / 3.0f)
#define S(v)         ((v) * LIZ_SCALE)

#define LIZ_SPACING  S(12.5f)

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef LIZARD_TOUCH
#define LIZARD_TOUCH 1
#endif

/*---------------------------------------------------------------------------
 * RGB565 colours (same palette as index.html)
 *-------------------------------------------------------------------------*/
#define C565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | \
                                  (((g) & 0xFC) << 3) | \
                                  (((b) & 0xF8) >> 3)))

/* Background: flat warm-yellow plaster wall. The pale skeleton stays legible
 * against the ochre tone; the grid colour below is pulled toward brown so
 * nothing clashes with the warm palette. */
#define BG_R  0xAD
#define BG_G  0x96
#define BG_B  0x68

static const uint16_t COL_BG      = C565(BG_R, BG_G, BG_B);
static const uint16_t COL_GRID    = C565(0x8A, 0x72, 0x46);
static const uint16_t COL_BONE    = C565(0xDF, 0xE9, 0xDC);
static const uint16_t COL_BONE_D  = C565(0x8F, 0xAE, 0x9C);
static const uint16_t COL_EDGE    = C565(0x3C, 0x69, 0x55);
static const uint16_t COL_EYE_BG  = C565(0x07, 0x10, 0x0E);
static const uint16_t COL_EYE_R   = C565(0x78, 0xC8, 0xAF);
static const uint16_t COL_EYE_DOT = C565(0x96, 0xFF, 0xCD);
static const uint16_t COL_NOSE    = C565(0x0A, 0x17, 0x13);
static const uint16_t COL_TONGUE  = C565(0xFF, 0x78, 0x91);
static const uint16_t COL_SHADOW  = C565(0x00, 0x00, 0x00);
static const uint16_t COL_TARGET  = C565(0x82, 0xFF, 0xC8);

/*---------------------------------------------------------------------------
 * Minimal rasteriser
 *-------------------------------------------------------------------------*/
typedef struct {
    int32_t   w, h;
    uint16_t *buf;
} gfx_t;

typedef struct {
    float x, y;
} pt;

static gfx_t g_gfx;

static inline void g_px(int x, int y, uint16_t c, int a)
{
    uint16_t *p;
    int r0, g0, b0, r1, g1, b1;

    if(x < 0 || y < 0 || x >= g_gfx.w || y >= g_gfx.h || a <= 0) return;
    p = &g_gfx.buf[y * g_gfx.w + x];
    if(a >= 255) {
        *p = c;
        return;
    }
    r0 = (*p >> 11) & 0x1F;  g0 = (*p >> 5) & 0x3F;  b0 =  *p       & 0x1F;
    r1 = ( c >> 11) & 0x1F;  g1 = ( c >> 5) & 0x3F;  b1 =   c       & 0x1F;
    r0 += ((r1 - r0) * a) >> 8;
    g0 += ((g1 - g0) * a) >> 8;
    b0 += ((b1 - b0) * a) >> 8;
    *p = (uint16_t)((r0 << 11) | (g0 << 5) | b0);
}

static void g_fill(int x, int y, int w, int h, uint16_t c)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > g_gfx.w ? g_gfx.w : x + w;
    int y1 = y + h > g_gfx.h ? g_gfx.h : y + h;
    int yy, xx;

    for(yy = y0; yy < y1; yy++) {
        uint16_t *p = &g_gfx.buf[yy * g_gfx.w + x0];
        for(xx = x0; xx < x1; xx++) *p++ = c;
    }
}

static void g_line(int x0, int y0, int x1, int y1, uint16_t c, int a)
{
    int dx, dy, sx, sy, err, e2, guard;

    /* Clamp endpoints so a stray coordinate can't turn Bresenham into an
     * unbounded loop. */
    if(x0 < -8) x0 = -8;  if(x0 >= g_gfx.w + 8) x0 = g_gfx.w + 7;
    if(x1 < -8) x1 = -8;  if(x1 >= g_gfx.w + 8) x1 = g_gfx.w + 7;
    if(y0 < -8) y0 = -8;  if(y0 >= g_gfx.h + 8) y0 = g_gfx.h + 7;
    if(y1 < -8) y1 = -8;  if(y1 >= g_gfx.h + 8) y1 = g_gfx.h + 7;

    dx = abs(x1 - x0);  sx = x0 < x1 ? 1 : -1;
    dy = -abs(y1 - y0); sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    guard = 2 * (g_gfx.w + g_gfx.h);

    while(guard-- > 0) {
        g_px(x0, y0, c, a);
        if(x0 == x1 && y0 == y1) break;
        /* e2 MUST be sampled once per step: the first branch below updates
         * err, so re-reading 2*err in the second branch uses a different
         * value and the line never converges (it runs to the screen edge
         * producing the "flying lines"). */
        e2 = 2 * err;
        if(e2 >= dy) { err += dy; x0 += sx; }
        if(e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Everything below is integer-only. The board has no hardware FPU, so float
 * maths in the inner pixel loops made a frame take hundreds of ms. Geometry
 * (the chain/IK) is still float, but it is converted to int at this boundary. */
static void g_disk_i(int cx, int cy, int r, uint16_t c, int a)
{
    int x0, x1, y0, y1, x, y, rr;

    if(r < 1) r = 1;
    if(r > 500) return;
    rr = r * r;
    x0 = cx - r;  x1 = cx + r;
    y0 = cy - r;  y1 = cy + r;
    if(x0 < 0) x0 = 0;  if(x1 >= g_gfx.w) x1 = g_gfx.w - 1;
    if(y0 < 0) y0 = 0;  if(y1 >= g_gfx.h) y1 = g_gfx.h - 1;

    for(y = y0; y <= y1; y++) {
        int dy = y - cy, dy2 = dy * dy;
        for(x = x0; x <= x1; x++) {
            int dx = x - cx;
            if(dx * dx + dy2 <= rr) g_px(x, y, c, a);
        }
    }
}

static void g_disk(float cx, float cy, float r, uint16_t c, int a)
{
    if(!(r > 0.0f) || r > 500.0f) return;
    if(!(cx > -10000.0f && cx < 10000.0f)) return;
    if(!(cy > -10000.0f && cy < 10000.0f)) return;
    g_disk_i((int)cx, (int)cy, (int)r, c, a);
}

/* Thick line = swept disk. Integer DDA with a finite step count. */
static void g_thick(float fx0, float fy0, float fx1, float fy1,
                    float w, uint16_t c, int a)
{
    int x0, y0, x1, y1, dx, dy, mx, steps, r, i;

    if(!(fx0 > -10000.0f && fx0 < 10000.0f)) return;
    if(!(fx1 > -10000.0f && fx1 < 10000.0f)) return;
    if(!(fy0 > -10000.0f && fy0 < 10000.0f)) return;
    if(!(fy1 > -10000.0f && fy1 < 10000.0f)) return;

    x0 = (int)fx0; y0 = (int)fy0;
    x1 = (int)fx1; y1 = (int)fy1;
    dx = x1 - x0;  dy = y1 - y0;
    mx = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if(mx < 1) mx = 1;
    steps = mx;
    r = (int)(w * 0.5f);
    if(r < 1) r = 1;

    for(i = 0; i <= steps; i++) {
        int px = x0 + (dx * i) / steps;
        int py = y0 + (dy * i) / steps;
        g_disk_i(px, py, r, c, a);
    }
}

/* fill=1: solid; fill=0: outline band. Integer ellipse, rotation from a
 * fixed-point (Q8) sin/cos computed once outside the pixel loop. */
static void g_ellipse(float fcx, float fcy, float frx, float fry, float rot,
                      uint16_t c, int a, int fill)
{
    int cx, cy, rx, ry, rad, cs, sn, x0, x1, y0, y1, x, y;
    int rx2, ry2, thresh;

    if(!(frx > 0.0f) || !(fry > 0.0f) || frx > 500.0f || fry > 500.0f) return;
    if(!(fcx > -10000.0f && fcx < 10000.0f)) return;
    if(!(fcy > -10000.0f && fcy < 10000.0f)) return;

    cx = (int)fcx; cy = (int)fcy;
    rx = (int)frx; ry = (int)fry;
    if(rx < 1) rx = 1;
    if(ry < 1) ry = 1;
    cs = (int)(cosf(rot) * 256.0f);
    sn = (int)(sinf(rot) * 256.0f);
    rad = rx > ry ? rx : ry;
    x0 = cx - rad;  x1 = cx + rad;
    y0 = cy - rad;  y1 = cy + rad;
    if(x0 < 0) x0 = 0;  if(x1 >= g_gfx.w) x1 = g_gfx.w - 1;
    if(y0 < 0) y0 = 0;  if(y1 >= g_gfx.h) y1 = g_gfx.h - 1;
    rx2 = rx * rx;  ry2 = ry * ry;

    thresh = rx2 * ry2;
    for(y = y0; y <= y1; y++) {
        int py = (y - cy) << 8;             /* Q8 */
        int pycs = py * cs, pysn = py * sn; /* Q16 */
        for(x = x0; x <= x1; x++) {
            int px = (x - cx) << 8;         /* Q8 */
            int u = (px * cs + pysn) >> 16; /* rotate -> Q0 */
            int v = (pycs - px * sn) >> 16;
            /* ellipse test without division: (u/rx)^2 + (v/ry)^2 <= 1 */
            int e = (u * u) * ry2 + (v * v) * rx2;
            if(fill ? (e <= thresh) : (e >= (thresh * 72) / 100 &&
                                       e <= (thresh * 128) / 100)) {
                g_px(x, y, c, a);
            }
        }
    }
}

/* Scanline fill with integer edge interpolation (mostly-convex polygons). */
static void g_poly(const pt *p, int n, uint16_t c, int a)
{
    int ymin = 1 << 20, ymax = -(1 << 20);
    int xs[64];
    int y0, y1, y, i, j, px[64], py[64];

    for(i = 0; i < n; i++) {
        px[i] = (int)p[i].x;  py[i] = (int)p[i].y;
        if(py[i] < ymin) ymin = py[i];
        if(py[i] > ymax) ymax = py[i];
    }
    y0 = ymin;  y1 = ymax;
    if(y1 < 0 || y0 >= g_gfx.h) return;
    if(y0 < 0) y0 = 0;
    if(y1 >= g_gfx.h) y1 = g_gfx.h - 1;

    for(y = y0; y <= y1; y++) {
        int cnt = 0;
        for(i = 0; i < n; i++) {
            int ay = py[i], by = py[(i + 1) % n];
            int ax = px[i], bx = px[(i + 1) % n];
            if((ay <= y && by > y) || (by <= y && ay > y)) {
                if(cnt < 64) xs[cnt++] = ax + (y - ay) * (bx - ax) / (by - ay);
            }
        }
        for(i = 0; i < cnt; i++) {
            for(j = i + 1; j < cnt; j++) {
                if(xs[j] < xs[i]) { int t = xs[i]; xs[i] = xs[j]; xs[j] = t; }
            }
        }
        for(i = 0; i + 1 < cnt; i += 2) {
            int xa = xs[i], xb = xs[i + 1];
            if(xa < 0) xa = 0;
            if(xb >= g_gfx.w) xb = g_gfx.w - 1;
            for(j = xa; j <= xb; j++) g_px(j, y, c, a);
        }
    }
}

static void g_poly_line(const pt *p, int n, uint16_t c, int a, float w)
{
    int i;
    for(i = 0; i < n; i++) {
        pt a1 = p[i], b1 = p[(i + 1) % n];
        if(w <= 1.2f) g_line((int)a1.x, (int)a1.y, (int)b1.x, (int)b1.y, c, a);
        else          g_thick(a1.x, a1.y, b1.x, b1.y, w, c, a);
    }
}

static int g_quad(pt *out, int idx,
                  float x0, float y0, float cx, float cy, float x1, float y1,
                  int steps)
{
    int i;
    for(i = 1; i <= steps; i++) {
        float t = (float)i / steps, u = 1.0f - t;
        out[idx].x = u * u * x0 + 2.0f * u * t * cx + t * t * x1;
        out[idx].y = u * u * y0 + 2.0f * u * t * cy + t * t * y1;
        idx++;
    }
    return idx;
}

/*---------------------------------------------------------------------------
 * Skeleton state
 *-------------------------------------------------------------------------*/
typedef struct {
    float x, y;
} vec;

static vec     s_seg[LIZ_N];
static float   s_ptr_x, s_ptr_y;
static uint32_t s_ptr_last;
static int      s_ptr_active;
static float   s_gait, s_smooth;
static float   s_tx, s_ty;
static uint32_t s_last_ms;

static lv_obj_t  *s_canvas;
static uint16_t  *s_buf;
static uint32_t   s_frame_cnt;

static vec tangent(int i)
{
    vec a = s_seg[i > 0 ? i - 1 : 0];
    vec b = s_seg[i < LIZ_N - 1 ? i + 1 : LIZ_N - 1];
    float dx = a.x - b.x, dy = a.y - b.y;
    float d = sqrtf(dx * dx + dy * dy);
    vec t;

    if(d < 0.001f) d = 1.0f;
    t.x = dx / d;
    t.y = dy / d;
    return t;
}

/*---------------------------------------------------------------------------
 * Bones, joints, legs
 *-------------------------------------------------------------------------*/
static void g_bone(float x1, float y1, float x2, float y2, float w)
{
    float k = w * 0.62f;

    g_thick(x1, y1, x2, y2, w + 1.4f, COL_EDGE, 210);
    g_disk(x1, y1, k + 0.7f, COL_EDGE, 210);
    g_disk(x2, y2, k + 0.7f, COL_EDGE, 210);
    g_thick(x1, y1, x2, y2, w, COL_BONE, 255);
    g_disk(x1, y1, k, COL_BONE, 255);
    g_disk(x2, y2, k, COL_BONE, 255);
}

static void g_joint(float x, float y, float r)
{
    g_disk(x, y, r, COL_BONE_D, 255);
    g_ellipse(x, y, r, r, 0, COL_EDGE, 220, 0);
}

static void draw_leg(int idx, int side, float phase, float moving)
{
    vec root = s_seg[idx];
    vec t = tangent(idx);
    float nx = -t.y, ny = t.x;
    float hx = root.x + nx * side * S(9.0f);
    float hy = root.y + ny * side * S(9.0f);
    float stride = S(26.0f) + moving * S(10.0f);
    float cyc = fmodf(phase, 2.0f * M_PI);
    float lx, lift = 0.0f;
    float reach = S(31.0f);
    float fx, fy, vx, vy, d, cd, base, cos_a, diff, knee_a, kx, ky;
    const float a = S(25.0f), b = S(24.0f);
    int k;

    if(cyc < 0.0f) cyc += 2.0f * M_PI;
    if(cyc < M_PI * 1.25f) {
        float p = cyc / (M_PI * 1.25f);
        lx = stride * (0.5f - p);
    }
    else {
        float p = (cyc - M_PI * 1.25f) / (M_PI * 0.75f);
        lx = stride * (-0.5f + p);
        lift = sinf(p * M_PI) * S(13.0f);
    }

    fx = hx + t.x * lx + nx * side * reach;
    fy = hy + t.y * lx + ny * side * reach - lift;

    vx = fx - hx;
    vy = fy - hy;
    d = sqrtf(vx * vx + vy * vy);
    if(d < 0.001f) d = 0.001f;

    cd = d < fabsf(a - b) + 0.01f ? fabsf(a - b) + 0.01f : d;
    if(cd > a + b - 0.01f) cd = a + b - 0.01f;
    vx /= d;
    vy /= d;
    fx = hx + vx * cd;
    fy = hy + vy * cd;

    base = atan2f(vy, vx);
    cos_a = (a * a + cd * cd - b * b) / (2.0f * a * cd);
    if(cos_a > 1.0f) cos_a = 1.0f;
    if(cos_a < -1.0f) cos_a = -1.0f;
    diff = acosf(cos_a);
    knee_a = base + (side > 0 ? 1.0f : -1.0f) * diff;
    kx = hx + cosf(knee_a) * a;
    ky = hy + sinf(knee_a) * a;

    g_bone(hx, hy, kx, ky, S(5.2f));
    g_joint(kx, ky, S(4.0f));
    g_bone(kx, ky, fx, fy, S(3.8f));
    g_joint(fx, fy, S(2.6f));

    for(k = -1; k <= 1; k++) {
        g_line((int)fx, (int)fy,
               (int)(fx + vx * S(7.0f) + nx * k * S(4.0f)),
               (int)(fy + vy * S(7.0f) + ny * k * S(4.0f)), COL_BONE, 255);
    }
}

/*---------------------------------------------------------------------------
 * Skull (local coordinates are transformed into head space)
 *-------------------------------------------------------------------------*/
static float s_ox, s_oy, s_cos, s_sin;

static void xform(float lx, float ly, float *ox, float *oy)
{
    /* local coordinates are in the original specimen's units */
    lx *= LIZ_SCALE;
    ly *= LIZ_SCALE;
    *ox = s_ox + lx * s_cos - ly * s_sin;
    *oy = s_oy + lx * s_sin + ly * s_cos;
}

static void draw_skull(uint32_t now)
{
    vec h = s_seg[0], h1 = s_seg[1];
    pt p[48], lp[48], jaw[32], tri[3];
    int n = 0, jn = 0, i;
    float ex, ey, dx, dy, ang;
    uint32_t f;

    ang = atan2f(h.y - h1.y, h.x - h1.x);
    s_cos = cosf(ang);
    s_sin = sinf(ang);
    s_ox = h.x;
    s_oy = h.y;

    /* cranium: moveTo + the four quadratic curves from the HTML */
    lp[n].x = -7; lp[n].y = -13; n++;
    n = g_quad(lp, n, -7, -13, 9, -15, 20, -9, 6);
    lp[n].x = 35; lp[n].y = -4.5f; n++;
    n = g_quad(lp, n, 35, -4.5f, 39, 0, 35, 4.5f, 6);
    lp[n].x = 20; lp[n].y = 9; n++;
    n = g_quad(lp, n, 20, 9, 9, 15, -7, 13, 6);
    n = g_quad(lp, n, -7, 13, -13, 0, -7, -13, 6);
    for(i = 0; i < n; i++) xform(lp[i].x, lp[i].y, &p[i].x, &p[i].y);
    g_poly(p, n, COL_BONE, 255);
    g_poly_line(p, n, COL_EDGE, 210, 1.1f);

    /* eye socket + glint */
    xform(7, -5.5f, &ex, &ey);
    g_ellipse(ex, ey, S(6.4f), S(5.2f), ang - 0.25f, COL_EYE_BG, 255, 1);
    g_ellipse(ex, ey, S(6.4f), S(5.2f), ang - 0.25f, COL_EYE_R, 200, 0);
    xform(9, -7, &dx, &dy);
    g_disk(dx, dy, S(1.5f), COL_EYE_DOT, 190);

    /* nostril */
    xform(29, -2.2f, &dx, &dy);
    g_ellipse(dx, dy, S(2.4f), S(1.6f), ang, COL_NOSE, 255, 1);

    /* lower jaw */
    jaw[jn].x = -5; jaw[jn].y = 6; jn++;
    jn = g_quad(jaw, jn, -5, 6, 12, 14, 31, 6, 6);
    jn = g_quad(jaw, jn, 31, 6, 33, 4, 31, 3.5f, 4);
    jn = g_quad(jaw, jn, 31, 3.5f, 12, 10, -5, 4, 6);
    for(i = 0; i < jn; i++) xform(jaw[i].x, jaw[i].y, &p[i].x, &p[i].y);
    g_poly(p, jn, COL_BONE, 255);
    g_poly_line(p, jn, COL_EDGE, 210, 1.0f);

    /* teeth */
    for(i = 12; i <= 32; i += 5) {
        xform(i, 6.5f, &tri[0].x, &tri[0].y);
        xform(i + 2.4f, 6.8f, &tri[1].x, &tri[1].y);
        xform(i + 1.2f, 11, &tri[2].x, &tri[2].y);
        g_poly(tri, 3, COL_BONE, 255);
    }

    /* crest behind the head */
    xform(-6, -10, &tri[0].x, &tri[0].y);
    xform(-15, -16, &tri[1].x, &tri[1].y);
    xform(-9, -6, &tri[2].x, &tri[2].y);
    g_poly(tri, 3, COL_BONE, 255);
    g_poly_line(tri, 3, COL_EDGE, 210, 1.0f);

    /* occasional flick of the forked tongue */
    f = now % 4200;
    if(f < 420) {
        float k = sinf(f / 420.0f * M_PI);
        float len = 14.0f + 18.0f * k;
        float bx, by, tipx, tipy;

        xform(37, 0, &bx, &by);
        xform(37 + len, sinf((float)f * 0.05f) * 3.0f, &tipx, &tipy);
        g_thick(bx, by, tipx, tipy, S(1.6f), COL_TONGUE, 255);
        xform(43 + len, -4, &dx, &dy);
        g_line((int)tipx, (int)tipy, (int)dx, (int)dy, COL_TONGUE, 255);
        xform(43 + len, 4, &dx, &dy);
        g_line((int)tipx, (int)tipy, (int)dx, (int)dy, COL_TONGUE, 255);
    }
}

/*---------------------------------------------------------------------------
 * Physics + frame
 *-------------------------------------------------------------------------*/
static void lizard_update(uint32_t now)
{
    float dt = (float)(now - s_last_ms) / 16.67f;
    float tx, ty, dx, dy, d, sway, ax, ay, step, mv;
    int use_ptr = 0;
    int i;

    if(dt > 2.5f) dt = 2.5f;
    if(dt < 0.0f) dt = 0.0f;
    s_last_ms = now;

#if LIZARD_TOUCH
    {
        int16_t px, py;
        int pressed;
        board_touch_read(&px, &py, &pressed);
        if(pressed) {
            s_ptr_x = px;
            s_ptr_y = py;
            s_ptr_active = 1;
            s_ptr_last = now;
        }
    }
#endif
    if(s_ptr_active && (now - s_ptr_last) < 2600) use_ptr = 1;

    if(use_ptr) {
        tx = s_ptr_x;
        ty = s_ptr_y;
    }
    else {
        tx = LIZ_W / 2.0f + cosf((float)now * 0.00034f) * fminf(240, LIZ_W * 0.28f);
        ty = LIZ_H / 2.0f + sinf((float)now * 0.00049f) * fminf(170, LIZ_H * 0.28f);
    }
    s_tx = tx;
    s_ty = ty;

    /* head: chase the aim point, with a serpentine offset on the target */
    dx = tx - s_seg[0].x;
    dy = ty - s_seg[0].y;
    d = sqrtf(dx * dx + dy * dy);
    if(d < 0.001f) d = 0.001f;
    sway = sinf((float)now * 0.0042f) * fminf(20, d * 0.1f);
    ax = tx + (-dy / d) * sway;
    ay = ty + ( dx / d) * sway;
    dx = ax - s_seg[0].x;
    dy = ay - s_seg[0].y;
    d = sqrtf(dx * dx + dy * dy);
    if(d < 0.001f) d = 0.001f;
    step = fminf(d, S(3.6f) * dt * (1.0f + s_smooth * 0.5f));
    s_seg[0].x += dx / d * step;
    s_seg[0].y += dy / d * step;

    /* chain: pull each segment to SPACING behind its parent.
     * vec is a value type here (unlike the HTML's object references), so the
     * updated position must be written back into s_seg[i]. */
    for(i = 1; i < LIZ_N; i++) {
        vec p = s_seg[i - 1];
        float vx = p.x - s_seg[i].x, vy = p.y - s_seg[i].y;
        float dist = sqrtf(vx * vx + vy * vy);
        float diff;

        if(dist < 0.001f) dist = 0.001f;
        diff = (dist - LIZ_SPACING) / dist;
        s_seg[i].x += vx * diff;
        s_seg[i].y += vy * diff;
    }

    mv = step / S(4.0f);
    if(mv > 1.0f) mv = 1.0f;
    s_smooth += (mv - s_smooth) * 0.06f;
    s_gait += dt * (0.07f + s_smooth * 0.13f);
}

static void lizard_render(uint32_t now)
{
    float cx = 0.0f, cy = 0.0f, ring;
    int i, x;

    g_gfx.w = LIZ_W;
    g_gfx.h = LIZ_H;
    g_gfx.buf = s_buf;

    /* background + slowly drifting grid */
    g_fill(0, 0, LIZ_W, LIZ_H, COL_BG);
    x = (int)(((uint32_t)((float)now * 0.004f)) % 48);
    for(; x < LIZ_W; x += 48) g_line(x, 0, x, LIZ_H - 1, COL_GRID, 14);
    for(i = 0; i < LIZ_H; i += 48) g_line(0, i, LIZ_W - 1, i, COL_GRID, 14);

    /* marker at the current target */
    ring = 10.0f + sinf((float)now * 0.004f) * 2.0f;
    g_ellipse(s_tx, s_ty, ring, ring, 0, COL_TARGET, 60, 0);

    /* Soft ground shadow. The long axis follows the head-to-tail body line
     * instead of staying horizontal. The light comes from the top-left, so
     * the whole ellipse is offset toward the bottom-right. One layer only -
     * software float is expensive on this FPU-less board. */
    for(i = 0; i < LIZ_N; i++) { cx += s_seg[i].x; cy += s_seg[i].y; }
    cx /= LIZ_N;
    cy /= LIZ_N;
    {
        float bx = s_seg[0].x - s_seg[LIZ_N - 1].x;
        float by = s_seg[0].y - s_seg[LIZ_N - 1].y;
        float span = sqrtf(bx * bx + by * by);
        float ang = atan2f(by, bx);
        g_ellipse(cx + S(10.0f), cy + S(10.0f),
                  S(70.0f) + span * 0.28f, S(18.0f),
                  ang, COL_SHADOW, 42, 1);
    }

    /* far-side legs */
    draw_leg(6, -1, s_gait + M_PI, s_smooth);
    draw_leg(18, -1, s_gait, s_smooth);

    /* spine, ribs and tail, drawn tail-first so the head sits on top */
    for(i = LIZ_N - 1; i >= 1; i--) {
        vec s = s_seg[i], p = s_seg[i - 1];
        vec t = tangent(i);
        float nx = -t.y, ny = t.x;
        float r = S(5.2f) * (1.0f - i / (float)(LIZ_N - 1)) * 0.9f + S(1.2f);
        int side;

        if(r < S(1.6f)) r = S(1.6f);
        g_bone(p.x, p.y, s.x, s.y, r * 1.05f);

        if(i >= 2 && i <= 17) {
            float u = (i - 1) / 18.0f;
            float len = S(34.0f) * sinf(M_PI * u);

            for(side = -1; side <= 1; side += 2) {
                pt q[12];
                int qn = 0, k;

                q[qn].x = s.x; q[qn].y = s.y; qn++;
                qn = g_quad(q, qn,
                            s.x, s.y,
                            s.x + nx * side * len * 0.8f - t.x * S(2.0f),
                            s.y + ny * side * len * 0.8f - t.y * S(2.0f),
                            s.x + nx * side * len - t.x * S(11.0f),
                            s.y + ny * side * len - t.y * S(11.0f), 8);
                for(k = 0; k + 1 < qn; k++) {
                    g_thick(q[k].x, q[k].y, q[k + 1].x, q[k + 1].y, S(2.2f), COL_BONE, 255);
                }
            }
            /* dorsal spine */
            g_thick(s.x, s.y, s.x - nx * S(7.0f), s.y - ny * S(7.0f), S(2.6f), COL_BONE, 255);
        }
        g_joint(s.x, s.y, r * 0.82f);
    }

    /* near-side legs */
    draw_leg(6, 1, s_gait, s_smooth);
    draw_leg(18, 1, s_gait + M_PI, s_smooth);

    /* pelvis */
    {
        vec pel = s_seg[18];
        vec t = tangent(18);
        float a = atan2f(t.y, t.x);
        g_ellipse(pel.x, pel.y, S(9), S(10), a, COL_BONE, 255, 1);
        g_ellipse(pel.x, pel.y, S(9), S(10), a, COL_EDGE, 220, 0);
    }

    draw_skull(now);
}

static void lizard_timer(lv_timer_t *timer)
{
    uint32_t now = board_millis();
    (void)timer;

    lizard_update(now);
    lizard_render(now);
    lv_obj_invalidate(s_canvas);
    s_frame_cnt++;
}

/*---------------------------------------------------------------------------
 * Public API
 *-------------------------------------------------------------------------*/
lv_obj_t *lizard_create(void)
{
    int i;

    s_buf = (uint16_t *)lv_malloc(LIZ_W * LIZ_H * 2);
    LV_ASSERT_MALLOC(s_buf);
    if(s_buf == NULL) return NULL;
    memset(s_buf, 0, LIZ_W * LIZ_H * 2);

    s_canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_buffer(s_canvas, s_buf, LIZ_W, LIZ_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_set_size(s_canvas, LIZ_W, LIZ_H);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);

    for(i = 0; i < LIZ_N; i++) {
        s_seg[i].x = LIZ_W / 2.0f - i * LIZ_SPACING;
        s_seg[i].y = LIZ_H / 2.0f;
    }
    s_ptr_x = LIZ_W / 2.0f;
    s_ptr_y = LIZ_H / 2.0f;
    s_ptr_last = 0;
    s_ptr_active = 0;
    s_tx = s_ptr_x;
    s_ty = s_ptr_y;
    s_gait = 0.0f;
    s_smooth = 0.0f;
    s_last_ms = board_millis();

    lv_timer_create(lizard_timer, 33, NULL);
    return s_canvas;
}

void lizard_set_target(uint16_t x, uint16_t y)
{
    s_ptr_x = x;
    s_ptr_y = y;
    s_ptr_last = board_millis();
    s_ptr_active = 1;
}

void lizard_clear_target(void)
{
    s_ptr_active = 0;
    s_ptr_last = 0;
}
