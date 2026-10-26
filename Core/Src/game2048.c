/*
 * game2048.c - Dokunmatik 2048
 *
 * Tahtada parmagi kaydir ya da soldaki yon tuslarini kullan.
 */
#include "games.h"
#include "gfx.h"
#include "display.h"
#include "scores.h"
#include "main.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------------- */
/* Yerlesim                                                                  */
/* ------------------------------------------------------------------------- */

#define N        4
#define TILE     58
#define GAP      6
#define BOARD_W  (N * TILE + (N + 1) * GAP)     /* 262 */
#define BX       (LCD_W - BOARD_W - 6)
#define BY       ((LCD_H - BOARD_W) / 2)

#define SWIPE_MIN 25                            /* px */

#define OV_W    210
#define OV_H    110
#define OV_X    (BX + (BOARD_W - OV_W) / 2)
#define OV_Y    (BY + (BOARD_W - OV_H) / 2)
#define OV_CX   (OV_X + OV_W / 2)

#define COL_EMPTY   RGB(44, 50, 84)
#define COL_DARKTXT RGB(119, 110, 101)

enum { BTN_UP, BTN_RIGHT, BTN_DOWN, BTN_LEFT, BTN_NEW, BTN_EXIT, BTN_COUNT, BTN_NONE = -1 };

/* Yon tuslari ARROW_xxx sirasiyla ayni: indeks = yon */
static const Button buttons[BTN_COUNT] = {
  [BTN_UP]    = {  72,  44, 58, 56, 2, "" },
  [BTN_RIGHT] = { 136, 104, 58, 56, 2, "" },
  [BTN_DOWN]  = {  72, 164, 58, 56, 2, "" },
  [BTN_LEFT]  = {   8, 104, 58, 56, 2, "" },
  [BTN_NEW]   = {   4, 226, 96, 40, 2, "NEW" },
  [BTN_EXIT]  = { 104, 226, 96, 40, 2, "EXIT" },
};

#define STAT_SCORE_X  52
#define STAT_BEST_X   152

/* ------------------------------------------------------------------------- */
/* Oyun durumu                                                               */
/* ------------------------------------------------------------------------- */

enum { ST_PLAYING, ST_WON, ST_OVER };

static uint32_t grid[N][N];
static uint32_t shown[N][N];
static uint32_t score, best;
static int state;
static bool reached_2048;
static bool new_best;

/* ------------------------------------------------------------------------- */
/* Cizim                                                                     */
/* ------------------------------------------------------------------------- */

static void tile_colors(uint32_t v, uint16_t *bg, uint16_t *fg)
{
  *fg = COL_TEXT;
  switch (v)
  {
    case 0:    *bg = COL_EMPTY; break;
    case 2:    *bg = RGB(238, 228, 218); *fg = COL_DARKTXT; break;
    case 4:    *bg = RGB(237, 224, 200); *fg = COL_DARKTXT; break;
    case 8:    *bg = RGB(242, 177, 121); break;
    case 16:   *bg = RGB(245, 149, 99);  break;
    case 32:   *bg = RGB(246, 124, 95);  break;
    case 64:   *bg = RGB(246, 94, 59);   break;
    case 128:  *bg = RGB(237, 207, 114); break;
    case 256:  *bg = RGB(237, 204, 97);  break;
    case 512:  *bg = RGB(237, 200, 80);  break;
    case 1024: *bg = RGB(237, 197, 63);  break;
    case 2048: *bg = RGB(237, 194, 46);  break;
    default:   *bg = RGB(60, 58, 50);    break;
  }
}

static void draw_tile_at(int x, int y, int size, uint32_t v, int max_scale)
{
  uint16_t bg, fg;
  tile_colors(v, &bg, &fg);

  gfx_fill(x + 1, y, size - 2, size, bg);
  gfx_fill(x, y + 1, size, size - 2, bg);
  if (!v)
    return;

  char buf[12];
  snprintf(buf, sizeof buf, "%lu", (unsigned long)v);
  int s = max_scale;
  while (s > 1 && gfx_text_width(buf, s) > size - 6)
    s--;
  gfx_text_centered(x + size / 2, y + (size - 7 * s) / 2, buf, s, fg);
}

static void draw_tile(int r, int c)
{
  draw_tile_at(BX + GAP + c * (TILE + GAP), BY + GAP + r * (TILE + GAP), TILE, grid[r][c], 3);
}

static void render(bool force)
{
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++)
      if (force || grid[r][c] != shown[r][c])
      {
        draw_tile(r, c);
        shown[r][c] = grid[r][c];
      }
}

static void draw_button(int i, bool active)
{
  const Button *b = &buttons[i];
  gfx_button(b, NULL, active);
  if (i <= BTN_LEFT)   /* yon tuslari: indeks = ARROW_xxx */
    gfx_arrow(b->x + b->w / 2, b->y + b->h / 2, 14, i, COL_TEXT);
}

static void draw_value(int cx, uint32_t v)
{
  char buf[12];
  snprintf(buf, sizeof buf, "%lu", (unsigned long)v);
  gfx_fill(cx - 48, 20, 96, 14, COL_BG);
  gfx_text_centered(cx, 20, buf, 2, COL_TEXT);
}

static void update_stats(bool force)
{
  static uint32_t s = UINT32_MAX, b = UINT32_MAX;

  if (force) { s = b = UINT32_MAX; }
  if (score != s) { draw_value(STAT_SCORE_X, score); s = score; }
  if (best != b)  { draw_value(STAT_BEST_X, best);   b = best; }
}

static void draw_static(void)
{
  gfx_fill(0, 0, LCD_W, LCD_H, COL_BG);
  gfx_fill(BX, BY, BOARD_W, BOARD_W, COL_PANEL);
  gfx_outline(BX, BY, BOARD_W, BOARD_W, COL_BORDER);

  gfx_text_centered(STAT_SCORE_X, 7, "SCORE", 1, COL_LABEL);
  gfx_text_centered(STAT_BEST_X, 7, "BEST", 1, COL_LABEL);

  for (int i = 0; i < BTN_COUNT; i++)
    draw_button(i, false);
}

enum { OV_OVER, OV_WON };

static void draw_overlay(int kind)
{
  char buf[24];

  gfx_panel(OV_X, OV_Y, OV_W, OV_H);
  if (kind == OV_WON)
  {
    gfx_text_centered(OV_CX, OV_Y + 16, "2048", 4, COL_TITLE);
    gfx_text_centered(OV_CX, OV_Y + 58, "YOU WIN", 2, COL_TEXT);
    gfx_text_centered(OV_CX, OV_Y + 88, "TAP TO CONTINUE", 1, COL_LABEL);
  }
  else
  {
    gfx_text_centered(OV_CX, OV_Y + 12, "GAME OVER", 3, COL_TITLE);
    snprintf(buf, sizeof buf, "SCORE %lu", (unsigned long)score);
    gfx_text_centered(OV_CX, OV_Y + 42, buf, 2, COL_TEXT);
    if (new_best)
      gfx_text_centered(OV_CX, OV_Y + 64, "NEW BEST", 1, COL_TITLE);
    gfx_text_centered(OV_CX, OV_Y + 88, "TAP TO RETRY", 1, COL_LABEL);
  }
}

/* ------------------------------------------------------------------------- */
/* Oyun mantigi                                                              */
/* ------------------------------------------------------------------------- */

static void spawn_tile(void)
{
  int empty = 0;
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++)
      empty += grid[r][c] == 0;
  if (!empty)
    return;

  int k = (int)(rand_next() % (uint32_t)empty);
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++)
      if (!grid[r][c] && k-- == 0)
        grid[r][c] = (rand_next() % 10 == 0) ? 4 : 2;
}

static bool can_move(void)
{
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++)
    {
      if (!grid[r][c])
        return true;
      if (c + 1 < N && grid[r][c] == grid[r][c + 1])
        return true;
      if (r + 1 < N && grid[r][c] == grid[r + 1][c])
        return true;
    }
  return false;
}

/* dir: ARROW_UP/RIGHT/DOWN/LEFT. Herhangi bir tas kaydiysa true. */
static bool slide(int dir)
{
  bool moved = false;

  for (int line = 0; line < N; line++)
  {
    /* Hareket yonundeki kenardan baslayarak bu hattin hucreleri */
    int rr[N], cc[N];
    for (int k = 0; k < N; k++)
    {
      switch (dir)
      {
        case ARROW_UP:    rr[k] = k;         cc[k] = line;      break;
        case ARROW_DOWN:  rr[k] = N - 1 - k; cc[k] = line;      break;
        case ARROW_LEFT:  rr[k] = line;      cc[k] = k;         break;
        default:          rr[k] = line;      cc[k] = N - 1 - k; break;
      }
    }

    uint32_t in[N], out[N] = {0};
    int n = 0, o = 0;
    for (int k = 0; k < N; k++)
      if (grid[rr[k]][cc[k]])
        in[n++] = grid[rr[k]][cc[k]];

    for (int i = 0; i < n; i++)
    {
      if (i + 1 < n && in[i] == in[i + 1])
      {
        out[o] = in[i] * 2;
        score += out[o];
        if (out[o] == 2048)
          reached_2048 = true;
        o++;
        i++;   /* birlesen tas bir kez daha birlesmez */
      }
      else
      {
        out[o++] = in[i];
      }
    }

    for (int k = 0; k < N; k++)
      if (grid[rr[k]][cc[k]] != out[k])
      {
        grid[rr[k]][cc[k]] = out[k];
        moved = true;
      }
  }
  return moved;
}

static void submit_score(void)
{
  new_best = Scores_Submit(SCORE_2048, score);
  best = Scores_Get(SCORE_2048);
}

static void new_game(void)
{
  memset(grid, 0, sizeof grid);
  score = 0;
  reached_2048 = false;
  new_best = false;
  state = ST_PLAYING;
  spawn_tile();
  spawn_tile();
  render(true);
}

static void do_move(int dir)
{
  bool was_2048 = reached_2048;

  if (!slide(dir))
    return;
  spawn_tile();
  render(false);

  if (reached_2048 && !was_2048)
  {
    state = ST_WON;
    draw_overlay(OV_WON);
  }
  else if (!can_move())
  {
    state = ST_OVER;
    submit_score();
    draw_overlay(OV_OVER);
  }
}

/* ------------------------------------------------------------------------- */
/* Disariya acik fonksiyonlar                                                */
/* ------------------------------------------------------------------------- */

void Game2048_DrawIcon(int cx, int cy)
{
  draw_tile_at(cx - 25, cy - 23, 24, 2, 2);
  draw_tile_at(cx + 1,  cy - 23, 24, 4, 2);
  draw_tile_at(cx - 25, cy + 1,  24, 8, 2);
  draw_tile_at(cx + 1,  cy + 1,  24, 16, 2);
}

#define FRAME_MS 10

void Game2048_Run(void)
{
  int prev_btn = BTN_NONE;
  bool prev_touch = false;
  bool swiping = false;
  int sx = 0, sy = 0, lx = 0, ly = 0;

  best = Scores_Get(SCORE_2048);
  draw_static();
  new_game();
  update_stats(true);

  for (;;)
  {
    int tx = 0, ty = 0;
    bool touch = Touch_Read(&tx, &ty);
    int btn = touch ? gfx_hit(buttons, BTN_COUNT, tx, ty) : BTN_NONE;
    bool pressed = (btn != prev_btn);
    bool tap = touch && !prev_touch;

    if (pressed && btn == BTN_EXIT)
    {
      if (state == ST_PLAYING)
        submit_score();          /* yarida birakilan oyunun skoru da sayilir */
      return;                    /* ana menuye don */
    }
    else if (pressed && btn == BTN_NEW)
    {
      if (state == ST_PLAYING)
        submit_score();
      new_game();
    }
    else if (state != ST_PLAYING)
    {
      if (tap && btn == BTN_NONE)
      {
        if (state == ST_WON)
        {
          state = ST_PLAYING;    /* 2048'den sonra devam */
          render(true);
          if (!can_move())
          {
            state = ST_OVER;
            submit_score();
            draw_overlay(OV_OVER);
          }
        }
        else
        {
          new_game();
        }
      }
    }
    else
    {
      if (pressed && btn >= BTN_UP && btn <= BTN_LEFT)
        do_move(btn);

      /* Tahtada kaydirma */
      if (tap && btn == BTN_NONE && tx >= BX)
      {
        swiping = true;
        sx = lx = tx;
        sy = ly = ty;
      }
      else if (touch && swiping)
      {
        lx = tx;
        ly = ty;
      }
      else if (!touch && swiping)
      {
        int dx = lx - sx, dy = ly - sy;
        int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        swiping = false;
        if (adx >= ady && adx >= SWIPE_MIN)
          do_move(dx > 0 ? ARROW_RIGHT : ARROW_LEFT);
        else if (ady > adx && ady >= SWIPE_MIN)
          do_move(dy > 0 ? ARROW_DOWN : ARROW_UP);
      }
    }

    if (pressed)
    {
      if (prev_btn != BTN_NONE) draw_button(prev_btn, false);
      if (btn != BTN_NONE)      draw_button(btn, true);
    }
    prev_btn = btn;
    prev_touch = touch;

    update_stats(false);
    osDelay(FRAME_MS);
  }
}
