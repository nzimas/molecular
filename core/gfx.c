/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: drawing into a 128 x 160 RGB565 frame, sent to the screen whole. Large type only: a 5 x 7 font drawn
 * two to four times its size, bright on black. */

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define C_BLACK RGB(0, 0, 0)
#define C_WHITE RGB(255, 255, 255)
#define C_GREY RGB(110, 110, 110)

static uint16_t gfx_fb[SCR_H][SCR_W];

/* the font: 5 columns a glyph, bit 0 the top row; space, 0-9, A-Z, then ? % - + . : */
static const char GFX_CHARS[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ?%-+.:";
static const uint8_t GFX_FONT[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E},
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
    {0x02, 0x01, 0x51, 0x09, 0x06}, {0x23, 0x13, 0x08, 0x64, 0x62}, {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x08, 0x08, 0x3E, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00}, {0x00, 0x36, 0x36, 0x00, 0x00},
};

AINL void gfx_px(int32_t x, int32_t y, uint16_t c)
{
    if ((uint32_t)x < SCR_W && (uint32_t)y < SCR_H)
        gfx_fb[y][x] = c;
}
static void gfx_clear(uint16_t c)
{
    int32_t x, y;
    for (y = 0; y < SCR_H; y++)
        for (x = 0; x < SCR_W; x++)
            gfx_fb[y][x] = c;
}
static void gfx_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            gfx_px(i, j, c);
}
static void gfx_frame(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint16_t c)
{
    gfx_rect(x, y, w, t, c);
    gfx_rect(x, y + h - t, w, t, c);
    gfx_rect(x, y, t, h, c);
    gfx_rect(x + w - t, y, t, h, c);
}
static void gfx_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                gfx_px(cx + x, cy + y, c);
}
/* a ring from radius r0 to r1; gap: skip the top's +-gap pixels across (the power sign's opening), 0 for none */
static void gfx_ring(int32_t cx, int32_t cy, int32_t r0, int32_t r1, int32_t gap, uint16_t c)
{
    int32_t x, y, d;
    for (y = -r1; y <= r1; y++)
        for (x = -r1; x <= r1; x++) {
            d = x * x + y * y;
            if (d >= r0 * r0 && d <= r1 * r1 + r1 && !(gap && y < 0 && x > -gap && x < gap))
                gfx_px(cx + x, cy + y, c);
        }
}
/* a line t pixels thick (a square brush along Bresenham's path) */
static void gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t t, uint16_t c)
{
    int32_t dx = absi(x1 - x0), dy = -absi(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy, h = t / 2;
    for (;;) {
        gfx_rect(x0 - h, y0 - h, t, t, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (2 * e >= dy) {
            e += dy;
            x0 += sx;
        }
        if (2 * e <= dx) {
            e += dx;
            y0 += sy;
        }
    }
}

static int32_t gfx_text_w(const char *s, int32_t scale)
{
    int32_t n = 0;
    while (s[n])
        n++;
    return n ? n * 6 * scale - scale : 0;
}
static void gfx_text(int32_t x, int32_t y, const char *s, int32_t scale, uint16_t c)
{
    for (; *s; s++, x += 6 * scale) {
        const char *f = GFX_CHARS;
        int32_t g = 0, col, row;
        char ch = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
        while (f[g] && f[g] != ch)
            g++;
        if (!f[g])
            g = 0;
        for (col = 0; col < 5; col++)
            for (row = 0; row < 7; row++)
                if ((GFX_FONT[g][col] >> row) & 1u)
                    gfx_rect(x + col * scale, y + row * scale, scale, scale, c);
    }
}
static void gfx_text_c(int32_t cx, int32_t y, const char *s, int32_t scale, uint16_t c)
{
    gfx_text(cx - gfx_text_w(s, scale) / 2, y, s, scale, c);
}
/* the largest scale, up to max, at which s fits w pixels */
static int32_t gfx_fit(const char *s, int32_t w, int32_t max)
{
    while (max > 1 && gfx_text_w(s, max) > w)
        max--;
    return max;
}
/* the power sign: a ring open at the top and a bar through the opening */
static void gfx_power(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t t = r / 4 > 2 ? r / 4 : 2;
    gfx_ring(cx, cy, r - t, r, r / 2, c);
    gfx_rect(cx - t / 2, cy - r - t / 2, t, r + t / 2, c);
}
