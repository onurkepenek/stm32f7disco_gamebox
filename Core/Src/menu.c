/*
 * menu.c - Ana menu: oyun kartlari, sayfalama
 */
#include "games.h"
#include "gfx.h"
#include "display.h"
#include "scores.h"
#include "main.h"
#include "cmsis_os.h"
#include <stdio.h>

/* Yeni oyunlar buraya eklenir. Menu bu sirayla gosterir (sayfa basina 6). */
static const Game games[] = {
  /* Kucuk cocuklar icin */
  { "BALLOONS", RGB(70, 150, 235),  Balloons_DrawIcon, Balloons_Run, SCORE_BALLOONS },
  { "PAINT",    RGB(255, 100, 180), Paint_DrawIcon,    Paint_Run,    SCORE_NONE     },
  { "MEMORY",   RGB(50, 90, 200),   Memory_DrawIcon,   Memory_Run,   SCORE_NONE     },
  /* Diger oyunlar */
  { "TETRIS",   RGB(170, 60, 240),  Tetris_DrawIcon,   Tetris_Run,   SCORE_TETRIS   },
  { "SNAKE",    RGB(40, 220, 60),   Snake_DrawIcon,    Snake_Run,    SCORE_SNAKE    },
  { "BRICKS",   RGB(255, 145, 0),   Bricks_DrawIcon,   Bricks_Run,   SCORE_BRICKS   },
  { "FLAPPY",   RGB(255, 210, 40),  Flappy_DrawIcon,   Flappy_Run,   SCORE_FLAPPY   },
  { "2048",     RGB(237, 194, 46),  Game2048_DrawIcon, Game2048_Run, SCORE_2048     },
  { "MINES",    RGB(240, 50, 50),   Mines_DrawIcon,    Mines_Run,    SCORE_MINES    },
};

#define GAME_COUNT  ((int)(sizeof games / sizeof games[0]))

/* 3x2 kart izgarasi */
#define TILE_COLS   3
#define TILE_ROWS   2
#define PER_PAGE    (TILE_COLS * TILE_ROWS)
#define TILE_W      140
#define TILE_H      88
#define TILE_GAP    14
#define TILE_X0     ((LCD_W - (TILE_COLS * TILE_W + (TILE_COLS - 1) * TILE_GAP)) / 2)
#define TILE_Y0     62
#define PAGE_COUNT  ((GAME_COUNT + PER_PAGE - 1) / PER_PAGE)

enum { NAV_PREV, NAV_NEXT, NAV_COUNT };
static const Button nav[NAV_COUNT] = {
  [NAV_PREV] = { TILE_X0,                  254, 60, 18, 2, "<" },
  [NAV_NEXT] = { LCD_W - TILE_X0 - 60,     254, 60, 18, 2, ">" },
};

/* Sag ustte, ikisi de basili tutularak: ekrani 180 derece dondur,
 * oyun kontrollerini saga/sola al */
static void draw_rotate_icon(const Button *b, bool active);
static void draw_side_icon(const Button *b, bool active);
static HoldButton rotate_btn = { { LCD_W - 56,  6, 48, 46, 1, "" }, 1200, 0, 0, draw_rotate_icon };
static HoldButton side_btn   = { { LCD_W - 110, 6, 48, 46, 1, "" }, 1200, 0, 0, draw_side_icon };

static int page;

/* Simge: kucuk bir ekran; kontrollerin oldugu taraf dolu serit */
static void draw_side_icon(const Button *b, bool active)
{
  int x = b->x + 9, y = b->y + 10, w = 30, h = 22;
  bool left = Settings_Get(SETTING_CONTROLS_LEFT);

  (void)active;
  gfx_outline(x, y, w, h, COL_TEXT);
  gfx_fill(left ? x + 2 : x + w - 11, y + 2, 9, h - 4, COL_TITLE);
}

static void draw_rotate_icon(const Button *b, bool active)
{
  uint16_t bg = active ? COL_BTN_ACTIVE : COL_BTN;
  int cx = b->x + b->w / 2, cy = b->y + b->h / 2 - 2;

  /* donen ok: halka, sag ustunde bosluk ve ok ucu */
  gfx_circle(cx, cy, 14, COL_TEXT);
  gfx_circle(cx, cy, 10, bg);
  gfx_fill(cx + 1, cy - 15, 15, 12, bg);
  gfx_arrow(cx + 12, cy - 6, 8, ARROW_DOWN, COL_TEXT);
}

static void tile_rect(int slot, int *x, int *y)
{
  *x = TILE_X0 + (slot % TILE_COLS) * (TILE_W + TILE_GAP);
  *y = TILE_Y0 + (slot / TILE_COLS) * (TILE_H + TILE_GAP);
}

static void draw_tile(int slot, bool active)
{
  int idx = page * PER_PAGE + slot;
  int x, y;

  if (idx >= GAME_COUNT)
    return;
  tile_rect(slot, &x, &y);

  const Game *g = &games[idx];
  gfx_fill(x, y, TILE_W, TILE_H, active ? COL_BTN_ACTIVE : COL_PANEL);
  gfx_outline(x, y, TILE_W, TILE_H, active ? g->color : COL_BORDER);
  gfx_fill(x + 1, y + 1, TILE_W - 2, 4, g->color);

  uint32_t best = Scores_Get(g->score_slot);
  if (best)
  {
    char buf[20];
    snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)best);
    gfx_text(x + TILE_W - 6 - gfx_text_width(buf, 1), y + 9, buf, 1, COL_LABEL);
  }

  if (g->draw_icon)
    g->draw_icon(x + TILE_W / 2, y + 36);
  gfx_text_centered(x + TILE_W / 2, y + TILE_H - 22, g->name, 2, COL_TEXT);
}

/* Dokunulan kartin slot numarasi, yoksa -1 */
static int hit_tile(int tx, int ty)
{
  for (int slot = 0; slot < PER_PAGE; slot++)
  {
    int x, y;
    if (page * PER_PAGE + slot >= GAME_COUNT)
      break;
    tile_rect(slot, &x, &y);
    if (tx >= x && tx < x + TILE_W && ty >= y && ty < y + TILE_H)
      return slot;
  }
  return -1;
}

static void draw_menu(void)
{
  gfx_fill(0, 0, LCD_W, LCD_H, COL_BG);
  gfx_text_centered(LCD_W / 2, 14, "GAME BOX", 3, COL_TITLE);
  gfx_text_centered(LCD_W / 2, 42, "SELECT A GAME", 1, COL_LABEL);
  gfx_hold_draw(&rotate_btn);
  gfx_hold_draw(&side_btn);

  for (int slot = 0; slot < PER_PAGE; slot++)
    draw_tile(slot, false);

  if (PAGE_COUNT > 1)
  {
    char buf[24];
    snprintf(buf, sizeof buf, "%d/%d", page + 1, PAGE_COUNT);
    gfx_text_centered(LCD_W / 2, 256, buf, 2, COL_LABEL);
    for (int i = 0; i < NAV_COUNT; i++)
      gfx_button(&nav[i], NULL, false);
  }
}

void Menu_Run(void)
{
  int pressed_slot = -1;   /* parmak indiginde altindaki kart */
  bool prev_touch = false;

  Scores_Init();
  gfx_set_rotation(Settings_Get(SETTING_ROTATED));
  draw_menu();

  for (;;)
  {
    int tx = 0, ty = 0;
    bool touch = Touch_Read(&tx, &ty);
    int slot = touch ? hit_tile(tx, ty) : -1;

    bool on_rotate = touch && gfx_hit(&rotate_btn.btn, 1, tx, ty) == 0;
    if (gfx_hold_update(&rotate_btn, on_rotate, HAL_GetTick()))
    {
      /* Basili tutuldu: ekrani dondur, ayari kalici kaydet */
      gfx_set_rotation(!gfx_get_rotation());
      Settings_Set(SETTING_ROTATED, gfx_get_rotation());
      Touch_WaitRelease();
      pressed_slot = -1;
      prev_touch = false;
      draw_menu();
      continue;
    }

    bool on_side = touch && gfx_hit(&side_btn.btn, 1, tx, ty) == 0;
    if (gfx_hold_update(&side_btn, on_side, HAL_GetTick()))
    {
      /* Basili tutuldu: oyun kontrollerini saga/sola al */
      Settings_Set(SETTING_CONTROLS_LEFT, !Settings_Get(SETTING_CONTROLS_LEFT));
      gfx_hold_draw(&side_btn);      /* simge yeni tarafi gostersin */
      Touch_WaitRelease();
      pressed_slot = -1;
      prev_touch = false;
      continue;
    }

    if (touch && !prev_touch)
    {
      pressed_slot = slot;
      if (slot >= 0)
        draw_tile(slot, true);

      int n = (PAGE_COUNT > 1) ? gfx_hit(nav, NAV_COUNT, tx, ty) : -1;
      if (n >= 0)
      {
        page = (page + (n == NAV_NEXT ? 1 : PAGE_COUNT - 1)) % PAGE_COUNT;
        draw_menu();
      }
    }
    else if (!touch && prev_touch && pressed_slot >= 0)
    {
      /* Parmak kalkinca oyunu baslat: dokunus oyuna tasinmasin */
      const Game *g = &games[page * PER_PAGE + pressed_slot];
      rand_seed(HAL_GetTick() * 2654435761UL ^ SysTick->VAL);
      g->run();
      Touch_WaitRelease();
      pressed_slot = -1;
      draw_menu();
    }
    else if (touch && pressed_slot >= 0 && slot != pressed_slot)
    {
      /* Parmak karttan kaydi: secimi iptal et */
      draw_tile(pressed_slot, false);
      pressed_slot = -1;
    }

    prev_touch = touch;
    osDelay(10);
  }
}
