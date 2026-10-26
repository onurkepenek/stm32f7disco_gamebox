/*
 * mines.c - Dokunmatik Mayin Tarlasi
 *
 * Dokun: hucreyi ac. Basili tut: bayrak koy/kaldir (ya da FLAG modunu ac).
 * Acik bir sayiya dokun: etrafindaki bayrak sayisi tutuyorsa komsulari acar.
 * Ilk dokunus her zaman guvenlidir.
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

#define CELL    22
#define MCOLS   13
#define MROWS   12
/* Konumlar "kontroller solda" duzeni icin; kontroller sagdayken (varsayilan)
 * panel saga, tahta sola kayar (layout_init). */
static int panel_dx, field_dx;

#define FX0     182
#define FX      (FX0 + field_dx)        /* tahta */
#define FY      4
#define FW      (MCOLS * CELL)
#define FH      (MROWS * CELL)

#define HOLD_MS 400                     /* basili tutma = bayrak */

#define OV_W    220
#define OV_H    132
#define OV_X0   (FX0 + (FW - OV_W) / 2)
#define OV_X    (OV_X0 + field_dx)
#define OV_Y    (FY + (FH - OV_H) / 2)
#define OV_CX   (OV_X + OV_W / 2)

#define COL_HIDDEN  RGB(70, 82, 135)
#define COL_FLAG    RGB(240, 50, 50)
#define COL_MINE    RGB(25, 25, 30)
#define COL_BOOM    RGB(200, 30, 30)

enum { BTN_FLAG, BTN_NEW, BTN_EXIT, BTN_LV_DOWN, BTN_LV_UP, BTN_COUNT, BTN_NONE = -1 };

static const Button buttons_base[BTN_COUNT] = {
  [BTN_FLAG]    = {  4,  40, 164, 64, 2, "FLAG OFF" },
  [BTN_NEW]     = {  4, 214,  80, 50, 2, "NEW" },
  [BTN_EXIT]    = { 88, 214,  80, 50, 2, "EXIT" },
  /* Sadece oyun disinda, kutunun icinde gorunur */
  [BTN_LV_DOWN] = { OV_X0 + 24,        OV_Y + 62, 36, 28, 2, "<" },
  [BTN_LV_UP]   = { OV_X0 + OV_W - 60, OV_Y + 62, 36, 28, 2, ">" },
};
static Button buttons[BTN_COUNT];

static bool is_level_button(int i)
{
  return i == BTN_LV_DOWN || i == BTN_LV_UP;
}

static void layout_init(void)
{
  bool right = !Settings_Get(SETTING_CONTROLS_LEFT);
  panel_dx = right ? 304 : 0;    /* panel 4..168 -> 308..472 */
  field_dx = right ? -176 : 0;   /* tahta 182..468 -> 6..292 */

  for (int i = 0; i < BTN_COUNT; i++)
  {
    buttons[i] = buttons_base[i];
    buttons[i].x += is_level_button(i) ? field_dx : panel_dx;
  }
}

#define MAX_LEVEL 5

/* ------------------------------------------------------------------------- */
/* Oyun durumu                                                               */
/* ------------------------------------------------------------------------- */

enum { ST_TITLE, ST_PLAYING, ST_LOST, ST_WON };

static uint8_t is_mine[MROWS][MCOLS];
static uint8_t is_open[MROWS][MCOLS];
static uint8_t is_flag[MROWS][MCOLS];
static uint8_t adjacent[MROWS][MCOLS];
static int mine_count, flags, opened;
static bool mines_placed;
static bool flag_mode;
static uint32_t start_tick, elapsed;    /* saniye */
static uint32_t level = 1, score, best;
static bool new_best;
static int state;

static int level_mines(void) { return 10 + ((int)level - 1) * 6; }   /* 10..34 */

/* ------------------------------------------------------------------------- */
/* Cizim                                                                     */
/* ------------------------------------------------------------------------- */

static const uint16_t number_color[9] = {
  0, RGB(80, 140, 255), RGB(60, 200, 80), RGB(240, 70, 70), RGB(170, 90, 255),
  RGB(255, 150, 40), RGB(40, 210, 210), RGB(240, 240, 240), RGB(160, 160, 160),
};

static void draw_button(int i, bool active)
{
  const char *label = NULL;
  if (i == BTN_FLAG)
    label = flag_mode ? "FLAG ON" : "FLAG OFF";
  gfx_button(&buttons[i], label, active || (i == BTN_FLAG && flag_mode));
}

static void draw_flag(int x, int y)
{
  gfx_fill(x + 12, y + 4, 2, 13, COL_TEXT);                 /* direk */
  gfx_fill(x + 6, y + 4, 6, 6, COL_FLAG);                   /* bayrak */
  gfx_fill(x + 8, y + 17, 10, 2, COL_TEXT);                 /* taban */
}

static void draw_mine(int x, int y, uint16_t bg)
{
  gfx_fill(x, y, CELL, CELL, bg);
  gfx_fill(x + 7, y + 5, 8, 12, COL_MINE);
  gfx_fill(x + 5, y + 7, 12, 8, COL_MINE);
  gfx_fill(x + 10, y + 3, 2, 16, COL_MINE);                 /* dikenler */
  gfx_fill(x + 3, y + 10, 16, 2, COL_MINE);
  gfx_fill(x + 8, y + 8, 2, 2, COL_TEXT);                   /* parlama */
}

static void draw_cell(int r, int c)
{
  int x = FX + c * CELL, y = FY + r * CELL;

  if (!is_open[r][c])
  {
    gfx_fill(x, y, CELL, CELL, gfx_mix(COL_HIDDEN, 0x0000, 110));
    gfx_fill(x, y, CELL - 1, CELL - 1, gfx_mix(COL_HIDDEN, 0xFFFF, 70));
    gfx_fill(x + 1, y + 1, CELL - 2, CELL - 2, COL_HIDDEN);
    if (is_flag[r][c])
      draw_flag(x, y);
    return;
  }

  if (is_mine[r][c])
  {
    draw_mine(x, y, COL_BOARD);
    return;
  }

  gfx_fill(x, y, CELL, CELL, COL_BOARD);
  gfx_fill(x + CELL - 1, y, 1, CELL, COL_GRID);
  gfx_fill(x, y + CELL - 1, CELL, 1, COL_GRID);
  if (adjacent[r][c])
  {
    char buf[2] = { (char)('0' + adjacent[r][c]), 0 };
    gfx_text(x + 6, y + 4, buf, 2, number_color[adjacent[r][c]]);
  }
}

static void draw_board(void)
{
  for (int r = 0; r < MROWS; r++)
    for (int c = 0; c < MCOLS; c++)
      draw_cell(r, c);
}

#define STAT_MINES_X  (30 + panel_dx)
#define STAT_TIME_X   (86 + panel_dx)
#define STAT_BEST_X   (142 + panel_dx)

static void draw_value(int cx, uint32_t v)
{
  char buf[12];
  snprintf(buf, sizeof buf, "%lu", (unsigned long)v);
  gfx_fill(cx - 27, 18, 54, 14, COL_BG);
  gfx_text_centered(cx, 18, buf, 2, COL_TEXT);
}

static void update_stats(bool force)
{
  static uint32_t m = UINT32_MAX, t = UINT32_MAX, b = UINT32_MAX;
  uint32_t left = (uint32_t)(mine_count > flags ? mine_count - flags : 0);

  if (force) { m = t = b = UINT32_MAX; }
  if (state == ST_PLAYING && mines_placed)
  {
    elapsed = (HAL_GetTick() - start_tick) / 1000;
    if (elapsed > 999)
      elapsed = 999;
  }
  if (left != m)    { draw_value(STAT_MINES_X, left);   m = left; }
  if (elapsed != t) { draw_value(STAT_TIME_X, elapsed); t = elapsed; }
  if (best != b)    { draw_value(STAT_BEST_X, best);    b = best; }
}

static void draw_static(void)
{
  gfx_fill(0, 0, LCD_W, LCD_H, COL_BG);
  gfx_outline(FX - 2, FY - 2, FW + 4, FH + 4, COL_BORDER);
  gfx_outline(FX - 1, FY - 1, FW + 2, FH + 2, COL_BORDER);

  gfx_text_centered(STAT_MINES_X, 6, "MINES", 1, COL_LABEL);
  gfx_text_centered(STAT_TIME_X, 6, "TIME", 1, COL_LABEL);
  gfx_text_centered(STAT_BEST_X, 6, "BEST", 1, COL_LABEL);

  gfx_text_centered(86 + panel_dx, 128, "TAP TO OPEN", 1, COL_LABEL);
  gfx_text_centered(86 + panel_dx, 146, "HOLD TO FLAG", 1, COL_LABEL);
  gfx_text_centered(86 + panel_dx, 172, "TAP A NUMBER TO", 1, COL_LABEL);
  gfx_text_centered(86 + panel_dx, 184, "OPEN AROUND IT", 1, COL_LABEL);

  for (int i = 0; i < BTN_COUNT; i++)
    if (!is_level_button(i))
      draw_button(i, false);
}

static void draw_level_selector(void)
{
  char buf[4];
  int x0 = buttons[BTN_LV_DOWN].x + buttons[BTN_LV_DOWN].w;
  int x1 = buttons[BTN_LV_UP].x;
  int y = buttons[BTN_LV_DOWN].y;

  snprintf(buf, sizeof buf, "%lu", (unsigned long)level);
  gfx_fill(x0, y, x1 - x0, buttons[BTN_LV_DOWN].h, COL_PANEL);
  gfx_text_centered(OV_CX, y, "LEVEL", 1, COL_LABEL);
  gfx_text_centered(OV_CX, y + 11, buf, 2, COL_TEXT);
}

enum { OV_TITLE, OV_LOST, OV_WON };

static void draw_overlay(int kind)
{
  char buf[24];

  gfx_panel(OV_X, OV_Y, OV_W, OV_H);
  switch (kind)
  {
    case OV_LOST:
      gfx_text_centered(OV_CX, OV_Y + 10, "BOOM", 3, COL_FLAG);
      gfx_text_centered(OV_CX, OV_Y + 40, "GAME OVER", 2, COL_TEXT);
      gfx_text_centered(OV_CX, OV_Y + 110, "TAP TO RETRY", 1, COL_LABEL);
      break;
    case OV_WON:
      gfx_text_centered(OV_CX, OV_Y + 8, "YOU WIN", 3, COL_TITLE);
      snprintf(buf, sizeof buf, "SCORE %lu", (unsigned long)score);
      gfx_text_centered(OV_CX, OV_Y + 33, buf, 2, COL_TEXT);
      if (new_best)
        snprintf(buf, sizeof buf, "NEW BEST");
      else
        snprintf(buf, sizeof buf, "TIME %lu", (unsigned long)elapsed);
      gfx_text_centered(OV_CX, OV_Y + 51, buf, 1, new_best ? COL_TITLE : COL_LABEL);
      gfx_text_centered(OV_CX, OV_Y + 110, "TAP TO PLAY AGAIN", 1, COL_LABEL);
      break;
    default:
      gfx_text_centered(OV_CX, OV_Y + 14, "MINES", 3, COL_TITLE);
      gfx_text_centered(OV_CX, OV_Y + 44, "FIRST TAP IS SAFE", 1, COL_LABEL);
      gfx_text_centered(OV_CX, OV_Y + 110, "TAP TO START", 1, COL_LABEL);
      break;
  }

  draw_button(BTN_LV_DOWN, false);
  draw_button(BTN_LV_UP, false);
  draw_level_selector();
}

/* ------------------------------------------------------------------------- */
/* Oyun mantigi                                                              */
/* ------------------------------------------------------------------------- */

static bool in_board(int r, int c)
{
  return r >= 0 && r < MROWS && c >= 0 && c < MCOLS;
}

/* Ilk acilan hucre ve komsulari mayinsiz kalir */
static void place_mines(int safe_r, int safe_c)
{
  int placed = 0;
  while (placed < mine_count)
  {
    int r = (int)(rand_next() % MROWS), c = (int)(rand_next() % MCOLS);
    int dr = r - safe_r, dc = c - safe_c;
    if (is_mine[r][c] || (dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1))
      continue;
    is_mine[r][c] = 1;
    placed++;
  }

  for (int r = 0; r < MROWS; r++)
    for (int c = 0; c < MCOLS; c++)
    {
      int n = 0;
      for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
          if (in_board(r + dr, c + dc) && is_mine[r + dr][c + dc])
            n++;
      adjacent[r][c] = (uint8_t)n;
    }

  mines_placed = true;
  start_tick = HAL_GetTick();
}

static void start_game(void)
{
  memset(is_mine, 0, sizeof is_mine);
  memset(is_open, 0, sizeof is_open);
  memset(is_flag, 0, sizeof is_flag);
  memset(adjacent, 0, sizeof adjacent);
  mine_count = level_mines();
  flags = opened = 0;
  mines_placed = false;
  elapsed = 0;
  new_best = false;
  state = ST_PLAYING;
  draw_board();
}

static void lose(int boom_r, int boom_c)
{
  state = ST_LOST;
  for (int r = 0; r < MROWS; r++)
    for (int c = 0; c < MCOLS; c++)
      if (is_mine[r][c] && !is_flag[r][c])
      {
        is_open[r][c] = 1;
        draw_cell(r, c);
      }
  draw_mine(FX + boom_c * CELL, FY + boom_r * CELL, COL_BOOM);
  update_stats(false);
  osDelay(1500);      /* tahtayi gorebilsin */
  draw_overlay(OV_LOST);
}

static void win(void)
{
  state = ST_WON;
  /* Seviye her zaman hizdan onemli: L*1000 + (999 - sure) */
  score = level * 1000 + (elapsed < 999 ? 999 - elapsed : 0);
  new_best = Scores_Submit(SCORE_MINES, score);
  best = Scores_Get(SCORE_MINES);

  for (int r = 0; r < MROWS; r++)
    for (int c = 0; c < MCOLS; c++)
      if (is_mine[r][c] && !is_flag[r][c])
      {
        is_flag[r][c] = 1;
        flags++;
        draw_cell(r, c);
      }
  update_stats(false);
  osDelay(800);
  draw_overlay(OV_WON);
}

/* Bos hucrelerden yayilarak acar (ozyinelemesiz) */
static void flood_open(int r0, int c0)
{
  static int16_t stack[MROWS * MCOLS];
  int sp = 0;

  stack[sp++] = (int16_t)(r0 * MCOLS + c0);
  while (sp > 0)
  {
    int idx = stack[--sp];
    int r = idx / MCOLS, c = idx % MCOLS;

    if (is_open[r][c] || is_flag[r][c] || is_mine[r][c])
      continue;
    is_open[r][c] = 1;
    opened++;
    draw_cell(r, c);

    if (adjacent[r][c] == 0)
      for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
        {
          int nr = r + dr, nc = c + dc;
          if (in_board(nr, nc) && !is_open[nr][nc] && sp < MROWS * MCOLS)
            stack[sp++] = (int16_t)(nr * MCOLS + nc);
        }
  }
}

static void check_win(void)
{
  if (state == ST_PLAYING && opened == MROWS * MCOLS - mine_count)
    win();
}

static void open_cell(int r, int c)
{
  if (is_flag[r][c] || is_open[r][c])
    return;
  if (!mines_placed)
    place_mines(r, c);
  if (is_mine[r][c])
  {
    lose(r, c);
    return;
  }
  flood_open(r, c);
  check_win();
}

/* Acik sayiya dokunus: bayraklar tutuyorsa kapali komsulari ac */
static void chord(int r, int c)
{
  int f = 0;
  for (int dr = -1; dr <= 1; dr++)
    for (int dc = -1; dc <= 1; dc++)
      if (in_board(r + dr, c + dc) && is_flag[r + dr][c + dc])
        f++;
  if (f != adjacent[r][c])
    return;

  for (int dr = -1; dr <= 1 && state == ST_PLAYING; dr++)
    for (int dc = -1; dc <= 1 && state == ST_PLAYING; dc++)
      if (in_board(r + dr, c + dc))
        open_cell(r + dr, c + dc);
}

static void toggle_flag(int r, int c)
{
  if (is_open[r][c])
    return;
  is_flag[r][c] ^= 1;
  flags += is_flag[r][c] ? 1 : -1;
  draw_cell(r, c);
}

static void tap_cell(int r, int c)
{
  if (is_open[r][c])
    chord(r, c);
  else if (flag_mode)
    toggle_flag(r, c);
  else
    open_cell(r, c);
}

static int cell_at(int x, int y)
{
  if (x < FX || x >= FX + FW || y < FY || y >= FY + FH)
    return -1;
  return ((y - FY) / CELL) * MCOLS + (x - FX) / CELL;
}

static void change_level(int delta)
{
  if (delta < 0 && level > 1)
    level--;
  else if (delta > 0 && level < MAX_LEVEL)
    level++;
  draw_level_selector();
}

/* ------------------------------------------------------------------------- */
/* Disariya acik fonksiyonlar                                                */
/* ------------------------------------------------------------------------- */

void Mines_DrawIcon(int cx, int cy)
{
  const int s = 16;
  int x0 = cx - 24, y0 = cy - 24;

  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++)
    {
      int x = x0 + c * s, y = y0 + r * s;
      bool hidden = (r == 0 && c == 2) || (r == 2);
      gfx_fill(x, y, s - 1, s - 1, hidden ? COL_HIDDEN : COL_BOARD);
    }
  gfx_text(x0 + s + 3, y0 + 4, "1", 1, number_color[1]);
  gfx_text(x0 + 3, y0 + s + 4, "2", 1, number_color[2]);
  gfx_text(x0 + 2 * s + 3, y0 + s + 4, "1", 1, number_color[1]);
  /* bayrak */
  gfx_fill(x0 + 2 * s + 8, y0 + 3, 2, 9, COL_TEXT);
  gfx_fill(x0 + 2 * s + 4, y0 + 3, 4, 4, COL_FLAG);
  /* mayin */
  gfx_fill(x0 + s + 4, y0 + 2 * s + 4, 7, 7, COL_MINE);
  gfx_fill(x0 + s + 6, y0 + 2 * s + 6, 2, 2, COL_TEXT);
}

#define FRAME_MS 10

void Mines_Run(void)
{
  int prev_btn = BTN_NONE;
  bool prev_touch = false;
  int press_cell = -1, prev_cell = -1;
  uint32_t press_tick = 0;
  bool hold_done = false;

  layout_init();
  state = ST_TITLE;
  flag_mode = false;
  mine_count = level_mines();
  flags = 0;
  elapsed = 0;
  best = Scores_Get(SCORE_MINES);
  memset(is_open, 0, sizeof is_open);
  memset(is_flag, 0, sizeof is_flag);

  draw_static();
  draw_board();
  update_stats(true);
  draw_overlay(OV_TITLE);

  for (;;)
  {
    int tx = 0, ty = 0;
    bool touch = Touch_Read(&tx, &ty);
    int btn = touch ? gfx_hit(buttons, BTN_COUNT, tx, ty) : BTN_NONE;
    uint32_t now = HAL_GetTick();

    if (state == ST_PLAYING && is_level_button(btn))
      btn = BTN_NONE;   /* oyun sirasinda gizliler */
    bool pressed = (btn != prev_btn);
    int cell = (touch && btn == BTN_NONE) ? cell_at(tx, ty) : -1;

    if (pressed && btn == BTN_EXIT)
    {
      return;           /* ana menuye don */
    }
    else if (pressed && btn == BTN_NEW)
    {
      start_game();
    }
    else if (pressed && btn == BTN_FLAG)
    {
      flag_mode = !flag_mode;
    }
    else if (pressed && is_level_button(btn))
    {
      change_level(btn == BTN_LV_UP ? 1 : -1);
    }
    else if (state != ST_PLAYING)
    {
      if (touch && !prev_touch && btn == BTN_NONE)
      {
        start_game();
        press_cell = -1;  /* baslatan dokunus hucre acmasin */
      }
    }
    else
    {
      if (touch && !prev_touch && cell >= 0)
      {
        press_cell = cell;
        press_tick = now;
        hold_done = false;
      }
      else if (touch && press_cell >= 0 && !hold_done && cell == press_cell &&
               now - press_tick >= HOLD_MS)
      {
        toggle_flag(press_cell / MCOLS, press_cell % MCOLS);
        hold_done = true;
      }
      else if (!touch && prev_touch && press_cell >= 0)
      {
        if (!hold_done && prev_cell == press_cell)
          tap_cell(press_cell / MCOLS, press_cell % MCOLS);
        press_cell = -1;
      }
    }

    if (pressed)
    {
      if (prev_btn != BTN_NONE) draw_button(prev_btn, false);
      if (btn != BTN_NONE)      draw_button(btn, true);
    }
    prev_btn = btn;
    prev_touch = touch;
    prev_cell = cell;

    update_stats(false);
    osDelay(FRAME_MS);
  }
}
