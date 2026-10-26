/*
 * paint.c - Parmakla Boyama (kucuk cocuklar icin)
 *
 * Soldan renk sec, ortada parmakla ciz. Sagda 3 firca kalinligi,
 * basili tutunca calisan CLEAR ve EXIT. Son renk kutusu gokkusagi kalemi.
 * Resim cikista SDRAM'de saklanir, geri gelince oldugu gibi durur
 * (kart kapanana kadar).
 */
#include "games.h"
#include "gfx.h"
#include "display.h"
#include "main.h"
#include "cmsis_os.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Yerlesim                                                                  */
/* ------------------------------------------------------------------------- */

#define CX0     80                      /* tuval */
#define CY0     4
#define CW      340
#define CH      264

#define SW_SIZE 34                      /* renk kutusu */
#define SW_GAP  4
#define SW_X0   4
#define SW_Y0   4

#define COL_CANVAS  RGB(255, 255, 255)

/* Ekranin disinda, kullanilmayan SDRAM bolgesi: tuvalin yedegi.
 * Mantiksal koordinatla saklanir; ekran dondurulse de resim dogru doner. */
#define BACKUP      ((volatile uint16_t *)(FB_ADDR + 0x00100000UL))

#define RAINBOW     0x0001              /* ozel renk: gokkusagi kalemi */

static const uint16_t palette[12] = {
  RGB(230, 30, 40),   RGB(255, 140, 0),   RGB(255, 220, 0),   RGB(40, 190, 60),
  RGB(0, 190, 220),   RGB(30, 80, 230),   RGB(140, 60, 220),  RGB(255, 100, 180),
  RGB(130, 80, 40),   RGB(0, 0, 0),       RGB(255, 255, 255), RAINBOW,
};

static const int brush_radius[3] = { 3, 7, 12 };

enum { SIZE_BTN_COUNT = 3 };
static const Button size_buttons[SIZE_BTN_COUNT] = {
  { 424,   4, 52, 44, 1, "" },
  { 424,  52, 52, 44, 1, "" },
  { 424, 100, 52, 44, 1, "" },
};

static HoldButton clear_btn = { { 424, 156, 52, 52, 1, "CLEAR" }, 1200, 0, 0 };
static HoldButton exit_btn  = { { 424, 216, 52, 52, 1, "EXIT" },  1500, 0, 0 };

/* ------------------------------------------------------------------------- */
/* Durum                                                                     */
/* ------------------------------------------------------------------------- */

static int color_idx = 0;
static int size_idx = 1;
static bool saved;          /* tuval yedegi var mi */
static uint32_t hue;        /* gokkusagi kalemi icin */

/* ------------------------------------------------------------------------- */
/* Cizim                                                                     */
/* ------------------------------------------------------------------------- */

/* 0..1535 arasi ton -> RGB565, tam doygun */
static uint16_t hue_color(uint32_t h)
{
  int seg = (int)(h / 256) % 6, f = (int)(h % 256);
  int r, g, b;
  switch (seg)
  {
    case 0:  r = 255;     g = f;       b = 0;       break;
    case 1:  r = 255 - f; g = 255;     b = 0;       break;
    case 2:  r = 0;       g = 255;     b = f;       break;
    case 3:  r = 0;       g = 255 - f; b = 255;     break;
    case 4:  r = f;       g = 0;       b = 255;     break;
    default: r = 255;     g = 0;       b = 255 - f; break;
  }
  return RGB(r, g, b);
}

static void swatch_rect(int i, int *x, int *y)
{
  *x = SW_X0 + (i % 2) * (SW_SIZE + SW_GAP);
  *y = SW_Y0 + (i / 2) * (SW_SIZE + SW_GAP);
}

static void draw_swatch(int i)
{
  int x, y;
  swatch_rect(i, &x, &y);

  gfx_fill(x - 2, y - 2, SW_SIZE + 4, SW_SIZE + 4, COL_BG);
  if (palette[i] == RAINBOW)
  {
    for (int k = 0; k < SW_SIZE; k++)
      gfx_fill(x, y + k, SW_SIZE, 1, hue_color((uint32_t)k * 1536 / SW_SIZE));
  }
  else
  {
    gfx_fill(x, y, SW_SIZE, SW_SIZE, palette[i]);
  }

  if (i == color_idx)
  {
    /* secili: kalin sari cerceve */
    gfx_outline(x - 2, y - 2, SW_SIZE + 4, SW_SIZE + 4, COL_TITLE);
    gfx_outline(x - 1, y - 1, SW_SIZE + 2, SW_SIZE + 2, COL_TITLE);
  }
  else
  {
    gfx_outline(x, y, SW_SIZE, SW_SIZE, COL_BORDER);
  }
}

/* Tuvale kirpilmis dolu daire */
static void brush_dot(int cx, int cy, int r, uint16_t c)
{
  for (int dy = -r; dy <= r; dy++)
  {
    int y = cy + dy;
    if (y < CY0 || y >= CY0 + CH)
      continue;
    int w = gfx_isqrt(r * r - dy * dy);
    int x0 = cx - w, x1 = cx + w;
    if (x0 < CX0) x0 = CX0;
    if (x1 > CX0 + CW - 1) x1 = CX0 + CW - 1;
    if (x1 >= x0)
      gfx_fill(x0, y, x1 - x0 + 1, 1, c);
  }
}

static void draw_size_button(int i)
{
  const Button *b = &size_buttons[i];
  gfx_button(b, NULL, i == size_idx);
  int r = brush_radius[i];
  int cx = b->x + b->w / 2, cy = b->y + b->h / 2;
  for (int dy = -r; dy <= r; dy++)
  {
    int w = gfx_isqrt(r * r - dy * dy);
    gfx_fill(cx - w, cy + dy, 2 * w + 1, 1, COL_TEXT);
  }
}

static void clear_canvas(void)
{
  gfx_fill(CX0, CY0, CW, CH, COL_CANVAS);
}

static void save_canvas(void)
{
  for (int y = 0; y < CH; y++)
    for (int x = 0; x < CW; x++)
      BACKUP[y * CW + x] = gfx_get_pixel(CX0 + x, CY0 + y);
  saved = true;
}

static void restore_canvas(void)
{
  for (int y = 0; y < CH; y++)
    for (int x = 0; x < CW; x++)
      gfx_put_pixel(CX0 + x, CY0 + y, BACKUP[y * CW + x]);
}

static void draw_static(void)
{
  gfx_fill(0, 0, LCD_W, LCD_H, COL_BG);
  gfx_outline(CX0 - 2, CY0 - 2, CW + 4, CH + 4, COL_BORDER);
  gfx_outline(CX0 - 1, CY0 - 1, CW + 2, CH + 2, COL_BORDER);

  for (int i = 0; i < 12; i++)
    draw_swatch(i);
  for (int i = 0; i < SIZE_BTN_COUNT; i++)
    draw_size_button(i);
  gfx_hold_draw(&clear_btn);
  gfx_hold_draw(&exit_btn);

  if (saved)
    restore_canvas();
  else
    clear_canvas();
}

/* ------------------------------------------------------------------------- */
/* Boyama                                                                    */
/* ------------------------------------------------------------------------- */

static uint16_t current_color(void)
{
  if (palette[color_idx] != RAINBOW)
    return palette[color_idx];
  hue = (hue + 6) % 1536;
  return hue_color(hue);
}

/* Iki nokta arasini firca darbeleriyle doldur: hizli cizgide bosluk kalmasin */
static void stroke(int x0, int y0, int x1, int y1)
{
  int r = brush_radius[size_idx];
  int dx = x1 - x0, dy = y1 - y0;
  int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
  int dist = adx > ady ? adx : ady;
  int step = r / 2 > 0 ? r / 2 : 1;
  int n = dist / step;

  for (int i = 1; i <= n; i++)
    brush_dot(x0 + dx * i / n, y0 + dy * i / n, r, current_color());
  brush_dot(x1, y1, r, current_color());
}

static int swatch_at(int tx, int ty)
{
  for (int i = 0; i < 12; i++)
  {
    int x, y;
    swatch_rect(i, &x, &y);
    if (tx >= x - SW_GAP / 2 && tx < x + SW_SIZE + SW_GAP / 2 &&
        ty >= y - SW_GAP / 2 && ty < y + SW_SIZE + SW_GAP / 2)
      return i;
  }
  return -1;
}

static bool in_canvas(int x, int y)
{
  return x >= CX0 && x < CX0 + CW && y >= CY0 && y < CY0 + CH;
}

/* ------------------------------------------------------------------------- */
/* Disariya acik fonksiyonlar                                                */
/* ------------------------------------------------------------------------- */

void Paint_DrawIcon(int cx, int cy)
{
  /* fircayla cizilmis dalgali gokkusagi cizgisi */
  for (int x = -30; x <= 30; x++)
  {
    int y = cy + ((x + 30) / 10 % 2 ? (x + 30) % 10 : 10 - (x + 30) % 10) - 8;
    gfx_fill(cx + x, y, 2, 6, hue_color((uint32_t)(x + 30) * 1536 / 61));
  }
  /* boya damlalari */
  gfx_fill(cx - 24, cy + 10, 10, 10, palette[0]);
  gfx_fill(cx - 6, cy + 12, 10, 10, palette[2]);
  gfx_fill(cx + 12, cy + 10, 10, 10, palette[5]);
}

#define FRAME_MS 5

void Paint_Run(void)
{
  bool drawing = false;
  bool prev_touch = false;
  int last_x = 0, last_y = 0;

  draw_static();

  for (;;)
  {
    int tx = 0, ty = 0;
    bool touch = Touch_Read(&tx, &ty);
    uint32_t now = HAL_GetTick();

    bool on_exit = touch && gfx_hit(&exit_btn.btn, 1, tx, ty) == 0;
    bool on_clear = touch && gfx_hit(&clear_btn.btn, 1, tx, ty) == 0;

    if (gfx_hold_update(&exit_btn, on_exit, now))
    {
      save_canvas();
      return;               /* ana menuye don */
    }
    if (gfx_hold_update(&clear_btn, on_clear, now))
      clear_canvas();

    if (touch && in_canvas(tx, ty))
    {
      if (drawing)
        stroke(last_x, last_y, tx, ty);
      else
        brush_dot(tx, ty, brush_radius[size_idx], current_color());
      drawing = true;
      last_x = tx;
      last_y = ty;
    }
    else
    {
      drawing = false;
    }

    if (touch && !prev_touch)
    {
      int s = swatch_at(tx, ty);
      if (s >= 0 && s != color_idx)
      {
        int old = color_idx;
        color_idx = s;
        draw_swatch(old);
        draw_swatch(s);
      }

      int b = gfx_hit(size_buttons, SIZE_BTN_COUNT, tx, ty);
      if (b >= 0 && b != size_idx)
      {
        int old = size_idx;
        size_idx = b;
        draw_size_button(old);
        draw_size_button(b);
      }
    }

    prev_touch = touch;
    osDelay(FRAME_MS);
  }
}
