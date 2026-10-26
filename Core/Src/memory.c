/*
 * memory.c - Hafiza Kartlari (kucuk cocuklar icin)
 *
 * Kapali kartlari cevirip ayni sekilleri esle. Okuma gerektirmez: kartlarda
 * renkli sekiller var. Ilk tur 3 ciftle baslar, her kazanilan turda zorlasir
 * (4, 6, 8 cift). Kaybetmek yok; yanlis eslesen kartlar sessizce geri doner.
 * Cikis icin EXIT basili tutulur.
 */
#include "games.h"
#include "gfx.h"
#include "display.h"
#include "main.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------------- */
/* Yerlesim                                                                  */
/* ------------------------------------------------------------------------- */

#define TOP       36                    /* ustte yildiz sayaci ve EXIT */
#define GAP       8
#define MARGIN    8
#define MAX_CARDS 16

#define COL_BAR     RGB(20, 40, 90)
#define COL_TABLE   RGB(30, 90, 70)     /* yesil masa ortusu */
#define COL_BACK    RGB(50, 90, 200)
#define COL_FACE    RGB(255, 255, 255)
#define COL_STAR    RGB(255, 210, 40)
#define COL_MATCHED RGB(255, 200, 40)

static HoldButton exit_btn = { { LCD_W - 80, 4, 76, 28, 2, "EXIT" }, 1500, 0, 0, NULL };

/* Tur: kac cift, kac sutun x satir */
typedef struct { uint8_t pairs, cols, rows; } Round;
static const Round rounds[] = { { 3, 3, 2 }, { 4, 4, 2 }, { 6, 4, 3 }, { 8, 4, 4 } };
#define NROUNDS ((int)(sizeof rounds / sizeof rounds[0]))

#define MISMATCH_MS 1000

/* ------------------------------------------------------------------------- */
/* Durum                                                                     */
/* ------------------------------------------------------------------------- */

enum { CARD_HIDDEN, CARD_UP, CARD_MATCHED };

static uint8_t symbol[MAX_CARDS];
static uint8_t card_state[MAX_CARDS];
static int ncards, cols, rows, card_w, card_h, grid_x, grid_y;
static int first = -1, second = -1;
static uint32_t flip_back_at;
static int pairs_left;
static int round_idx;
static uint32_t stars;

/* ------------------------------------------------------------------------- */
/* Sekiller                                                                  */
/* ------------------------------------------------------------------------- */

enum { SYM_CIRCLE, SYM_STAR, SYM_TRIANGLE, SYM_SQUARE, SYM_HEART, SYM_DIAMOND,
       SYM_FLOWER, SYM_MOON, NSYMBOLS };

static const uint16_t symbol_color[NSYMBOLS] = {
  RGB(230, 40, 50), RGB(250, 200, 0), RGB(40, 180, 60), RGB(40, 100, 230),
  RGB(150, 60, 220), RGB(255, 130, 0), RGB(255, 100, 180), RGB(0, 180, 200),
};

/* (cx, cy) merkezli, yaricapi yaklasik s olan sekil */
static void draw_symbol(int sym, int cx, int cy, int s, uint16_t bg)
{
  uint16_t c = symbol_color[sym];

  switch (sym)
  {
    case SYM_CIRCLE:
      gfx_circle(cx, cy, s, c);
      break;

    case SYM_STAR:
    {
      int sc = (2 * s + 10) / 11;
      gfx_star(cx - 11 * sc / 2, cy - 11 * sc / 2, sc, c);
      break;
    }

    case SYM_TRIANGLE:
      for (int i = 0; i <= 2 * s; i++)
        gfx_fill(cx - i / 2, cy - s + i, i + 1, 1, c);
      break;

    case SYM_SQUARE:
      gfx_fill(cx - s * 4 / 5, cy - s * 4 / 5, s * 8 / 5, s * 8 / 5, c);
      break;

    case SYM_HEART:
    {
      int r = s / 2, top = cy - s / 3;
      gfx_circle(cx - r, top, r, c);
      gfx_circle(cx + r, top, r, c);
      for (int y = top; y <= cy + s; y++)
      {
        int half = s * (cy + s - y) / (cy + s - top);
        gfx_fill(cx - half, y, 2 * half + 1, 1, c);
      }
      break;
    }

    case SYM_DIAMOND:
      for (int dy = -s; dy <= s; dy++)
      {
        int half = (s - (dy < 0 ? -dy : dy)) * 3 / 4;
        gfx_fill(cx - half, cy + dy, 2 * half + 1, 1, c);
      }
      break;

    case SYM_FLOWER:
    {
      int r = s * 9 / 20, d = s / 2;
      gfx_circle(cx - d, cy, r, c);
      gfx_circle(cx + d, cy, r, c);
      gfx_circle(cx, cy - d, r, c);
      gfx_circle(cx, cy + d, r, c);
      gfx_circle(cx, cy, s / 3, RGB(255, 220, 0));
      break;
    }

    default: /* SYM_MOON: dolu daireden kaydirilmis daireyi cikar */
      gfx_circle(cx, cy, s, c);
      gfx_circle(cx + s / 2, cy - s / 4, s * 4 / 5, bg);
      break;
  }
}

/* ------------------------------------------------------------------------- */
/* Kartlar                                                                   */
/* ------------------------------------------------------------------------- */

static void card_rect(int i, int *x, int *y)
{
  *x = grid_x + (i % cols) * (card_w + GAP);
  *y = grid_y + (i / cols) * (card_h + GAP);
}

static void rounded_fill(int x, int y, int w, int h, uint16_t c)
{
  gfx_fill(x + 3, y, w - 6, h, c);
  gfx_fill(x + 1, y + 1, w - 2, h - 2, c);
  gfx_fill(x, y + 3, w, h - 6, c);
}

static void draw_card(int i)
{
  int x, y;
  card_rect(i, &x, &y);

  gfx_fill(x, y, card_w, card_h, COL_TABLE);
  if (card_state[i] == CARD_HIDDEN)
  {
    rounded_fill(x, y, card_w, card_h, COL_BACK);
    gfx_outline(x + 4, y + 4, card_w - 8, card_h - 8, gfx_mix(COL_BACK, 0xFFFF, 90));
    int s = (card_w < card_h ? card_w : card_h) / 6;
    gfx_circle(x + card_w / 2, y + card_h / 2, s, gfx_mix(COL_BACK, 0xFFFF, 60));
    return;
  }

  if (card_state[i] == CARD_MATCHED)
  {
    rounded_fill(x, y, card_w, card_h, COL_MATCHED);       /* altin cerceve */
    rounded_fill(x + 4, y + 4, card_w - 8, card_h - 8, COL_FACE);
  }
  else
  {
    rounded_fill(x, y, card_w, card_h, COL_FACE);
  }
  int s = (card_w < card_h ? card_w : card_h) * 3 / 10;
  draw_symbol(symbol[i], x + card_w / 2, y + card_h / 2, s, COL_FACE);
}

/* Kart cevirme: kenarlardan ortaya kapanir, sonra yeni yuzle cizilir */
static void flip_card(int i, int new_state)
{
  int x, y;
  card_rect(i, &x, &y);
  for (int step = 1; step <= 4; step++)
  {
    int w = card_w * step / 8;
    gfx_fill(x, y, w, card_h, COL_TABLE);
    gfx_fill(x + card_w - w, y, w, card_h, COL_TABLE);
    osDelay(12);
  }
  card_state[i] = (uint8_t)new_state;
  draw_card(i);
}

static int card_at(int tx, int ty)
{
  for (int i = 0; i < ncards; i++)
  {
    int x, y;
    card_rect(i, &x, &y);
    if (tx >= x - GAP / 2 && tx < x + card_w + GAP / 2 &&
        ty >= y - GAP / 2 && ty < y + card_h + GAP / 2)
      return i;
  }
  return -1;
}

/* ------------------------------------------------------------------------- */
/* Tur ve sayac                                                              */
/* ------------------------------------------------------------------------- */

static void draw_counter(void)
{
  char buf[12];
  snprintf(buf, sizeof buf, "%lu", (unsigned long)stars);
  gfx_fill(36, 6, 200, 24, COL_BAR);
  gfx_text(40, 8, buf, 3, COL_TEXT);
}

static void start_round(void)
{
  const Round *rd = &rounds[round_idx];
  int avail_w = LCD_W - 2 * MARGIN;
  int avail_h = LCD_H - TOP - 2 * MARGIN;

  cols = rd->cols;
  rows = rd->rows;
  ncards = rd->pairs * 2;
  card_w = (avail_w - (cols - 1) * GAP) / cols;
  card_h = (avail_h - (rows - 1) * GAP) / rows;
  if (card_w > card_h * 5 / 4)          /* cok genis olmasin */
    card_w = card_h * 5 / 4;
  grid_x = (LCD_W - (cols * card_w + (cols - 1) * GAP)) / 2;
  grid_y = TOP + MARGIN + (avail_h - (rows * card_h + (rows - 1) * GAP)) / 2;

  /* Her turda farkli sekiller: tum sekilleri karistirip ilk "pairs" tanesini al */
  uint8_t pool[NSYMBOLS];
  for (int i = 0; i < NSYMBOLS; i++)
    pool[i] = (uint8_t)i;
  for (int i = NSYMBOLS - 1; i > 0; i--)
  {
    int j = (int)(rand_next() % (uint32_t)(i + 1));
    uint8_t t = pool[i]; pool[i] = pool[j]; pool[j] = t;
  }
  for (int i = 0; i < ncards; i++)
    symbol[i] = pool[i / 2];
  for (int i = ncards - 1; i > 0; i--)
  {
    int j = (int)(rand_next() % (uint32_t)(i + 1));
    uint8_t t = symbol[i]; symbol[i] = symbol[j]; symbol[j] = t;
  }

  memset(card_state, CARD_HIDDEN, sizeof card_state);
  first = second = -1;
  pairs_left = rd->pairs;

  gfx_fill(0, TOP, LCD_W, LCD_H - TOP, COL_TABLE);
  for (int i = 0; i < ncards; i++)
    draw_card(i);
}

/* Tur bitti: buyuk yildiz ve konfeti, sonra bir sonraki tur */
static bool celebrate(void)
{
  const int sc = 9;
  int sx = LCD_W / 2 - 11 * sc / 2, sy = TOP + (LCD_H - TOP) / 2 - 11 * sc / 2;
  uint32_t start = HAL_GetTick();

  stars++;
  draw_counter();
  gfx_star(sx, sy, sc, COL_STAR);

  while (HAL_GetTick() - start < 2500)
  {
    int tx, ty;
    bool touch = Touch_Read(&tx, &ty);
    bool on_exit = touch && gfx_hit(&exit_btn.btn, 1, tx, ty) == 0;
    if (gfx_hold_update(&exit_btn, on_exit, HAL_GetTick()))
      return false;

    /* rastgele renkli konfeti (yildizin disinda) */
    int x = (int)(rand_next() % (LCD_W - 8)), y = TOP + (int)(rand_next() % (LCD_H - TOP - 8));
    if (x + 8 < sx || x > sx + 11 * sc || y + 8 < sy || y > sy + 11 * sc)
      gfx_fill(x, y, 8, 8, symbol_color[rand_next() % NSYMBOLS]);
    osDelay(15);
  }

  if (round_idx < NROUNDS - 1)
    round_idx++;
  start_round();
  return true;
}

static void tap_card(int i)
{
  if (card_state[i] != CARD_HIDDEN || second >= 0)
    return;

  flip_card(i, CARD_UP);
  if (first < 0)
  {
    first = i;
    return;
  }

  second = i;
  if (symbol[first] == symbol[second])
  {
    osDelay(250);
    card_state[first] = card_state[second] = CARD_MATCHED;
    draw_card(first);
    draw_card(second);
    first = second = -1;
    pairs_left--;
  }
  else
  {
    flip_back_at = HAL_GetTick() + MISMATCH_MS;
  }
}

/* ------------------------------------------------------------------------- */
/* Disariya acik fonksiyonlar                                                */
/* ------------------------------------------------------------------------- */

void Memory_DrawIcon(int cx, int cy)
{
  /* bir kapali kart, iki acik kart */
  rounded_fill(cx - 34, cy - 18, 20, 28, COL_BACK);
  rounded_fill(cx - 10, cy - 18, 20, 28, COL_FACE);
  draw_symbol(SYM_HEART, cx, cy - 4, 6, COL_FACE);
  rounded_fill(cx + 14, cy - 18, 20, 28, COL_FACE);
  draw_symbol(SYM_HEART, cx + 24, cy - 4, 6, COL_FACE);
}

#define FRAME_MS 10

void Memory_Run(void)
{
  bool prev_touch = false;

  round_idx = 0;
  stars = 0;

  gfx_fill(0, 0, LCD_W, TOP, COL_BAR);
  gfx_star(6, 7, 2, COL_STAR);
  draw_counter();
  gfx_hold_draw(&exit_btn);
  start_round();

  for (;;)
  {
    int tx = 0, ty = 0;
    bool touch = Touch_Read(&tx, &ty);
    uint32_t now = HAL_GetTick();

    bool on_exit = touch && gfx_hit(&exit_btn.btn, 1, tx, ty) == 0;
    if (gfx_hold_update(&exit_btn, on_exit, now))
      return;               /* ana menuye don */

    /* Yanlis eslesme: bekleme bitince ikisini de geri cevir */
    if (second >= 0 && (int32_t)(now - flip_back_at) >= 0)
    {
      flip_card(first, CARD_HIDDEN);
      flip_card(second, CARD_HIDDEN);
      first = second = -1;
    }

    if (touch && !prev_touch && !on_exit)
    {
      int i = card_at(tx, ty);
      if (i >= 0)
        tap_card(i);
    }

    if (pairs_left == 0 && !celebrate())
      return;

    prev_touch = touch;
    osDelay(FRAME_MS);
  }
}
