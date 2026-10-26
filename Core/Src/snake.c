/*
 * snake.c - Dokunmatik Snake
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

#define CELL    12
#define COLS    24
#define ROWS    22
/* Konumlar "kontroller solda" duzeni icin; kontroller sagdayken (varsayilan)
 * panel saga, oyun alani sola kayar (layout_init). */
static int panel_dx, field_dx;

#define FX0     182
#define FX      (FX0 + field_dx)        /* oyun alani sol ust kosesi */
#define FY      4
#define MAXLEN  (COLS * ROWS)

#define COL_SNAKE   RGB(40, 200, 60)
#define COL_HEAD    RGB(140, 255, 120)
#define COL_FOOD    RGB(240, 50, 50)

/* Baslangic / game over kutusu (seviye secici bunun icinde) */
#define OV_W    220
#define OV_H    132
#define OV_X0   (FX0 + (COLS * CELL - OV_W) / 2)
#define OV_X    (OV_X0 + field_dx)
#define OV_Y    (FY + (ROWS * CELL - OV_H) / 2)
#define OV_CX   (OV_X + OV_W / 2)

enum { BTN_UP, BTN_LEFT, BTN_RIGHT, BTN_DOWN, BTN_PAUSE, BTN_EXIT,
       BTN_LV_DOWN, BTN_LV_UP, BTN_COUNT, BTN_NONE = -1 };

static const Button buttons_base[BTN_COUNT] = {
  [BTN_UP]      = {  60,  38, 52, 52, 3, "" },
  [BTN_LEFT]    = {   4,  94, 52, 52, 3, "" },
  [BTN_RIGHT]   = { 116,  94, 52, 52, 3, "" },
  [BTN_DOWN]    = {  60, 150, 52, 52, 3, "" },
  [BTN_PAUSE]   = {   4, 214, 80, 50, 2, "PAUSE" },
  [BTN_EXIT]    = {  88, 214, 80, 50, 2, "EXIT" },
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
  field_dx = right ? -176 : 0;   /* alan 182..470 -> 6..294 */

  for (int i = 0; i < BTN_COUNT; i++)
  {
    buttons[i] = buttons_base[i];
    buttons[i].x += is_level_button(i) ? field_dx : panel_dx;
  }
}

/* Yonler: 0 yukari, 1 sag, 2 asagi, 3 sol */
static const int8_t DX[4] = { 0, 1, 0, -1 };
static const int8_t DY[4] = { -1, 0, 1, 0 };

/* ------------------------------------------------------------------------- */
/* Oyun durumu                                                               */
/* ------------------------------------------------------------------------- */

enum { ST_TITLE, ST_PLAYING, ST_OVER };

static uint8_t seg_x[MAXLEN], seg_y[MAXLEN]; /* halka tampon, head en yeni */
static int head, len;
static uint8_t occupied[ROWS][COLS];
static int dir;
static int dir_queue[2], queued;             /* hizli ardisik donusler icin */
static int food_x, food_y;
static uint32_t score, best, step_ms, last_step;
static uint32_t level = 1;                   /* 1..MAX_LEVEL, oyunlar arasi korunur */
static int state;
static bool paused;
static bool new_best;

#define MAX_LEVEL   10

static void draw_button(int i, bool active)
{
  const Button *b = &buttons[i];
  int arrow = -1;

  switch (i)
  {
    case BTN_UP:    arrow = ARROW_UP;    break;
    case BTN_RIGHT: arrow = ARROW_RIGHT; break;
    case BTN_DOWN:  arrow = ARROW_DOWN;  break;
    case BTN_LEFT:  arrow = ARROW_LEFT;  break;
    default: break;
  }

  gfx_button(b, (i == BTN_PAUSE && paused) ? "PLAY" : NULL, active);
  if (arrow >= 0)
    gfx_arrow(b->x + b->w / 2, b->y + b->h / 2, 14, arrow, COL_TEXT);
}

/* ------------------------------------------------------------------------- */
/* Cizim                                                                     */
/* ------------------------------------------------------------------------- */

enum { CELL_EMPTY, CELL_BODY, CELL_HEAD, CELL_FOOD };

static void draw_cell(int cx, int cy, int kind)
{
  int x = FX + cx * CELL, y = FY + cy * CELL;

  gfx_fill(x, y, CELL, CELL, COL_BOARD);
  switch (kind)
  {
    case CELL_BODY:
      gfx_fill(x + 1, y + 1, CELL - 2, CELL - 2, COL_SNAKE);
      break;
    case CELL_HEAD:
      gfx_fill(x + 1, y + 1, CELL - 2, CELL - 2, COL_HEAD);
      break;
    case CELL_FOOD:
      /* yuvarlak gorunumlu elma */
      gfx_fill(x + 3, y + 1, CELL - 6, CELL - 2, COL_FOOD);
      gfx_fill(x + 1, y + 3, CELL - 2, CELL - 6, COL_FOOD);
      break;
    default:
      break;
  }
}

static int seg_index(int i)   /* i = 0 bas, len-1 kuyruk */
{
  return (head - i + MAXLEN) % MAXLEN;
}

static void draw_field(void)
{
  gfx_fill(FX, FY, COLS * CELL, ROWS * CELL, COL_BOARD);
  if (state == ST_TITLE)
    return;
  for (int i = len - 1; i >= 0; i--)
  {
    int s = seg_index(i);
    draw_cell(seg_x[s], seg_y[s], i == 0 ? CELL_HEAD : CELL_BODY);
  }
  if (food_x >= 0)
    draw_cell(food_x, food_y, CELL_FOOD);
}

/* Ust bilgi satiri: SCORE / BEST / LEVEL sutunlari */
#define STAT_SCORE_X  (30 + panel_dx)
#define STAT_BEST_X   (86 + panel_dx)
#define STAT_LEVEL_X  (142 + panel_dx)

static void draw_value(int cx, uint32_t v)
{
  char buf[12];
  snprintf(buf, sizeof buf, "%lu", (unsigned long)v);
  gfx_fill(cx - 27, 18, 54, 14, COL_BG);
  gfx_text_centered(cx, 18, buf, 2, COL_TEXT);
}

static void update_stats(bool force)
{
  static uint32_t s = UINT32_MAX, b = UINT32_MAX, l = UINT32_MAX;

  if (force) { s = b = l = UINT32_MAX; }
  if (score != s) { draw_value(STAT_SCORE_X, score); s = score; }
  if (best != b)  { draw_value(STAT_BEST_X, best);   b = best; }
  if (level != l) { draw_value(STAT_LEVEL_X, level); l = level; }
}

static void draw_static(void)
{
  gfx_fill(0, 0, LCD_W, LCD_H, COL_BG);
  gfx_outline(FX - 2, FY - 2, COLS * CELL + 4, ROWS * CELL + 4, COL_BORDER);
  gfx_outline(FX - 1, FY - 1, COLS * CELL + 2, ROWS * CELL + 2, COL_BORDER);

  gfx_text_centered(STAT_SCORE_X, 6, "SCORE", 1, COL_LABEL);
  gfx_text_centered(STAT_BEST_X, 6, "BEST", 1, COL_LABEL);
  gfx_text_centered(STAT_LEVEL_X, 6, "LEVEL", 1, COL_LABEL);

  for (int i = 0; i < BTN_COUNT; i++)
    if (!is_level_button(i))
      draw_button(i, false);
}

/* Kutudaki "< LEVEL n >" satirinin ortasi */
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

enum { OV_TITLE, OV_OVER, OV_PAUSED };

static void draw_overlay(int kind)
{
  char buf[24];

  gfx_panel(OV_X, OV_Y, OV_W, OV_H);
  switch (kind)
  {
    case OV_PAUSED:
      gfx_text_centered(OV_CX, OV_Y + 42, "PAUSED", 3, COL_TITLE);
      gfx_text_centered(OV_CX, OV_Y + 80, "PRESS PLAY", 1, COL_LABEL);
      return;
    case OV_OVER:
      gfx_text_centered(OV_CX, OV_Y + 10, "GAME OVER", 3, COL_TITLE);
      snprintf(buf, sizeof buf, "SCORE %lu", (unsigned long)score);
      gfx_text_centered(OV_CX, OV_Y + 36, buf, 2, COL_TEXT);
      if (new_best)
        gfx_text_centered(OV_CX, OV_Y + 53, "NEW BEST", 1, COL_TITLE);
      gfx_text_centered(OV_CX, OV_Y + 110, "TAP TO RETRY", 1, COL_LABEL);
      break;
    default:
      gfx_text_centered(OV_CX, OV_Y + 18, "SNAKE", 3, COL_TITLE);
      gfx_text_centered(OV_CX, OV_Y + 110, "TAP TO START", 1, COL_LABEL);
      break;
  }

  draw_button(BTN_LV_DOWN, false);
  draw_button(BTN_LV_UP, false);
  draw_level_selector();
}

static void change_level(int delta)
{
  if (delta < 0 && level > 1)
    level--;
  else if (delta > 0 && level < MAX_LEVEL)
    level++;
  draw_level_selector();
}

/* Seviye 1: 180 ms/adim, seviye 10: 54 ms/adim */
static uint32_t level_step_ms(void)
{
  return 180 - (level - 1) * 14;
}

/* ------------------------------------------------------------------------- */
/* Oyun mantigi                                                              */
/* ------------------------------------------------------------------------- */

static void game_over(void)
{
  state = ST_OVER;
  new_best = Scores_Submit(SCORE_SNAKE, score);
  best = Scores_Get(SCORE_SNAKE);
  draw_overlay(OV_OVER);
}

/* Bos hucrelerden rastgele birine yem koyar; yer yoksa false */
static bool place_food(void)
{
  int empty = COLS * ROWS - len;
  if (empty <= 0)
    return false;

  int k = (int)(rand_next() % (uint32_t)empty);
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++)
      if (!occupied[y][x] && k-- == 0)
      {
        food_x = x;
        food_y = y;
        draw_cell(x, y, CELL_FOOD);
        return true;
      }
  return false;
}

static void start_game(void)
{
  memset(occupied, 0, sizeof occupied);
  len = 3;
  head = 2;
  for (int i = 0; i < len; i++)
  {
    seg_x[i] = (uint8_t)(COLS / 4 + i);
    seg_y[i] = ROWS / 2;
    occupied[seg_y[i]][seg_x[i]] = 1;
  }
  dir = 1;
  queued = 0;
  score = 0;
  step_ms = level_step_ms();
  state = ST_PLAYING;

  food_x = food_y = -1;   /* onceki oyunun yemi cizilmesin */
  draw_field();
  place_food();
  last_step = HAL_GetTick();
}

static void queue_dir(int d)
{
  int last = queued ? dir_queue[queued - 1] : dir;
  if (d == last || d == (last + 2) % 4)   /* ayni yon ya da geri donus yok */
    return;
  if (queued < 2)
    dir_queue[queued++] = d;
}

static void step(void)
{
  if (queued)
  {
    dir = dir_queue[0];
    dir_queue[0] = dir_queue[1];
    queued--;
  }

  int hs = seg_index(0);
  int nx = seg_x[hs] + DX[dir];
  int ny = seg_y[hs] + DY[dir];

  if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS)
  {
    game_over();
    return;
  }

  bool grow = (nx == food_x && ny == food_y);
  if (!grow)
  {
    /* Kuyruk once cekilir, boylece bas kuyrugun eski yerine girebilir */
    int ts = seg_index(len - 1);
    occupied[seg_y[ts]][seg_x[ts]] = 0;
    draw_cell(seg_x[ts], seg_y[ts], CELL_EMPTY);
    len--;
  }

  if (occupied[ny][nx])
  {
    game_over();
    return;
  }

  draw_cell(seg_x[hs], seg_y[hs], CELL_BODY);
  head = (head + 1) % MAXLEN;
  seg_x[head] = (uint8_t)nx;
  seg_y[head] = (uint8_t)ny;
  occupied[ny][nx] = 1;
  len++;
  draw_cell(nx, ny, CELL_HEAD);

  if (grow)
  {
    score += 10 * level;
    if (step_ms > 40)
      step_ms -= 2;
    if (!place_food())
      game_over();   /* alan doldu: kazandin */
  }
}

static void set_paused(bool p)
{
  paused = p;
  if (paused)
  {
    draw_overlay(OV_PAUSED);
  }
  else
  {
    draw_field();
    last_step = HAL_GetTick();
  }
}

/* ------------------------------------------------------------------------- */
/* Disariya acik fonksiyonlar                                                */
/* ------------------------------------------------------------------------- */

void Snake_DrawIcon(int cx, int cy)
{
  static const int8_t body[][2] = { {-3,1}, {-2,1}, {-1,1}, {-1,0}, {0,0}, {1,0} };
  const int s = 10;
  int n = (int)(sizeof body / sizeof body[0]);

  for (int i = 0; i < n; i++)
    gfx_fill(cx + body[i][0] * s - s / 2 + 1, cy + body[i][1] * s - s + 1, s - 2, s - 2,
             i == n - 1 ? COL_HEAD : COL_SNAKE);
  /* yem */
  gfx_fill(cx + 3 * s - s / 2 + 3, cy - s + 1, s - 6, s - 2, COL_FOOD);
  gfx_fill(cx + 3 * s - s / 2 + 1, cy - s + 3, s - 2, s - 6, COL_FOOD);
}

#define FRAME_MS 10

void Snake_Run(void)
{
  int prev_btn = BTN_NONE;
  bool prev_touch = false;

  layout_init();
  paused = false;
  state = ST_TITLE;
  score = 0;
  best = Scores_Get(SCORE_SNAKE);

  draw_static();
  draw_field();
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

    if (pressed && btn == BTN_EXIT)
    {
      return;           /* ana menuye don */
    }
    else if (pressed && btn == BTN_PAUSE && state == ST_PLAYING)
    {
      set_paused(!paused);
    }
    else if (pressed && is_level_button(btn))
    {
      change_level(btn == BTN_LV_UP ? 1 : -1);
    }
    else if (state != ST_PLAYING)
    {
      if (touch && !prev_touch && btn != BTN_PAUSE && btn != BTN_EXIT && !is_level_button(btn))
        start_game();
    }
    else if (!paused)
    {
      if (pressed)
      {
        switch (btn)
        {
          case BTN_UP:    queue_dir(0); break;
          case BTN_RIGHT: queue_dir(1); break;
          case BTN_DOWN:  queue_dir(2); break;
          case BTN_LEFT:  queue_dir(3); break;
          default: break;
        }
      }

      if (now - last_step >= step_ms)
      {
        last_step = now;
        step();
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
