/*
 * gfx.c - Framebuffer'a dogrudan cizim, 5x7 font, butonlar
 */
#include "gfx.h"
#include "display.h"
#include <string.h>

#define FB ((volatile uint16_t *)FB_ADDR)

/* Ekran 180 derece donuk mu. Tum cizim gfx_fill'den gectigi icin donusum
 * sadece burada (ve dokunmatikte) yapilir; oyunlar bundan habersizdir. */
static bool rotated;

void gfx_set_rotation(bool on) { rotated = on; }
bool gfx_get_rotation(void)    { return rotated; }

void gfx_fill(int x, int y, int w, int h, uint16_t c)
{
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0)
    return;

  if (rotated)
  {
    x = LCD_W - x - w;
    y = LCD_H - y - h;
  }

  for (int j = 0; j < h; j++)
  {
    volatile uint16_t *p = FB + (y + j) * LCD_W + x;
    for (int i = 0; i < w; i++)
      p[i] = c;
  }
}

/* Tek piksel okuma/yazma (mantiksal koordinat, donusum dahil) */
static volatile uint16_t *pixel_addr(int x, int y)
{
  if (rotated)
  {
    x = LCD_W - 1 - x;
    y = LCD_H - 1 - y;
  }
  return FB + y * LCD_W + x;
}

uint16_t gfx_get_pixel(int x, int y)          { return *pixel_addr(x, y); }
void     gfx_put_pixel(int x, int y, uint16_t c) { *pixel_addr(x, y) = c; }

void gfx_outline(int x, int y, int w, int h, uint16_t c)
{
  gfx_fill(x, y, w, 1, c);
  gfx_fill(x, y + h - 1, w, 1, c);
  gfx_fill(x, y, 1, h, c);
  gfx_fill(x + w - 1, y, 1, h, c);
}

void gfx_panel(int x, int y, int w, int h)
{
  gfx_fill(x, y, w, h, COL_PANEL);
  gfx_outline(x, y, w, h, COL_BORDER);
}

uint16_t gfx_mix(uint16_t c, uint16_t t, int a)
{
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  int tr = (t >> 11) & 31, tg = (t >> 5) & 63, tb = t & 31;
  r += ((tr - r) * a) >> 8;
  g += ((tg - g) * a) >> 8;
  b += ((tb - b) * a) >> 8;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

void gfx_arrow(int cx, int cy, int size, int dir, uint16_t c)
{
  int half = size / 2;

  for (int i = 0; i <= size; i++)
  {
    /* i: uctan itibaren mesafe, o noktadaki yari genislik i */
    switch (dir)
    {
      case ARROW_UP:    gfx_fill(cx - i, cy - half + i, 2 * i + 1, 1, c); break;
      case ARROW_DOWN:  gfx_fill(cx - i, cy + half - i, 2 * i + 1, 1, c); break;
      case ARROW_LEFT:  gfx_fill(cx - half + i, cy - i, 1, 2 * i + 1, c); break;
      case ARROW_RIGHT: gfx_fill(cx + half - i, cy - i, 1, 2 * i + 1, c); break;
      default: break;
    }
  }
}

void gfx_block(int x, int y, int size, uint16_t c)
{
  gfx_fill(x, y, size, size, gfx_mix(c, 0x0000, 130));
  gfx_fill(x, y, size - 1, size - 1, gfx_mix(c, 0xFFFF, 110));
  gfx_fill(x + 1, y + 1, size - 2, size - 2, c);
}

/* 5x7 font, sutun bazli, bit0 = ust satir */
static const uint8_t font_digits[10][5] = {
  {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46},
  {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
  {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, {0x36,0x49,0x49,0x49,0x36},
  {0x06,0x49,0x49,0x29,0x1E},
};
static const uint8_t font_alpha[26][5] = {
  {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
};
static const uint8_t font_lt[5]   = {0x08,0x14,0x22,0x41,0x00};
static const uint8_t font_gt[5]   = {0x00,0x41,0x22,0x14,0x08};
static const uint8_t font_slash[5] = {0x20,0x10,0x08,0x04,0x02};
static const uint8_t font_plus[5]  = {0x08,0x08,0x3E,0x08,0x08};

static const uint8_t *glyph(char ch)
{
  if (ch >= '0' && ch <= '9') return font_digits[ch - '0'];
  if (ch >= 'A' && ch <= 'Z') return font_alpha[ch - 'A'];
  switch (ch)
  {
    case '<': return font_lt;
    case '>': return font_gt;
    case '/': return font_slash;
    case '+': return font_plus;
    default:  return NULL;
  }
}

static void draw_char(int x, int y, char ch, int s, uint16_t c)
{
  const uint8_t *g = glyph(ch);
  if (!g)
    return;
  for (int col = 0; col < 5; col++)
    for (int row = 0; row < 7; row++)
      if (g[col] & (1 << row))
        gfx_fill(x + col * s, y + row * s, s, s, c);
}

int gfx_text_width(const char *str, int s)
{
  int n = (int)strlen(str);
  return n ? n * 6 * s - s : 0;
}

void gfx_text(int x, int y, const char *str, int s, uint16_t c)
{
  for (; *str; str++, x += 6 * s)
    draw_char(x, y, *str, s, c);
}

void gfx_text_centered(int cx, int y, const char *str, int s, uint16_t c)
{
  gfx_text(cx - gfx_text_width(str, s) / 2, y, str, s, c);
}

void gfx_button(const Button *b, const char *label, bool active)
{
  if (!label)
    label = b->label;
  gfx_fill(b->x, b->y, b->w, b->h, active ? COL_BTN_ACTIVE : COL_BTN);
  gfx_outline(b->x, b->y, b->w, b->h, COL_BORDER);
  gfx_text_centered(b->x + b->w / 2, b->y + (b->h - 7 * b->scale) / 2, label, b->scale, COL_TEXT);
}

int gfx_hit(const Button *buttons, int count, int x, int y)
{
  for (int i = 0; i < count; i++)
  {
    const Button *b = &buttons[i];
    if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h)
      return i;
  }
  return -1;
}

void gfx_hold_draw(HoldButton *h)
{
  h->start = 0;
  h->shown = 0;
  gfx_button(&h->btn, NULL, false);
}

bool gfx_hold_update(HoldButton *h, bool pressed, uint32_t now)
{
  const Button *b = &h->btn;
  int bar_w = b->w - 4;

  if (!pressed)
  {
    if (h->start)
      gfx_hold_draw(h);      /* erken birakildi: cubugu sil */
    return false;
  }

  if (!h->start)
  {
    h->start = now | 1;      /* 0 "basili degil" anlamina geliyor */
    gfx_button(b, NULL, true);
  }

  uint32_t held = now - h->start;
  int w = (int)((uint64_t)held * (uint32_t)bar_w / h->hold_ms);
  if (w > bar_w)
    w = bar_w;
  if (w > h->shown)
  {
    gfx_fill(b->x + 2 + h->shown, b->y + b->h - 6, w - h->shown, 4, COL_TITLE);
    h->shown = w;
  }

  if (held >= h->hold_ms)
  {
    gfx_hold_draw(h);
    return true;
  }
  return false;
}

int gfx_isqrt(int v)
{
  int r = 0;
  if (v <= 0)
    return 0;
  while ((r + 1) * (r + 1) <= v)
    r++;
  return r;
}

void gfx_circle(int cx, int cy, int r, uint16_t c)
{
  for (int dy = -r; dy <= r; dy++)
  {
    int w = gfx_isqrt(r * r - dy * dy);
    gfx_fill(cx - w, cy + dy, 2 * w + 1, 1, c);
  }
}

static const char *const star_rows[11] = {
  ".....#.....", "....###....", "....###....", "###########", ".#########.",
  "..#######..", "...#####...", "..#######..", "..###.###..", ".##.....##.",
  ".#.......#.",
};

void gfx_star(int x, int y, int s, uint16_t c)
{
  for (int r = 0; r < 11; r++)
  {
    /* ardisik '#' gruplarini tek dikdortgenle ciz */
    int k = 0;
    while (k < 11)
    {
      if (star_rows[r][k] != '#') { k++; continue; }
      int k0 = k;
      while (k < 11 && star_rows[r][k] == '#')
        k++;
      gfx_fill(x + k0 * s, y + r * s, (k - k0) * s, s, c);
    }
  }
}

static uint32_t rng_state = 0x1234567;

void rand_seed(uint32_t s)
{
  rng_state ^= s;
  if (rng_state == 0)
    rng_state = 1;
}

uint32_t rand_next(void)
{
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return rng_state = x;
}
