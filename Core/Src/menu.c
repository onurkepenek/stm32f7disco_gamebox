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

/* Yeni oyunlar buraya eklenir */
static const Game games[] = {
  { "TETRIS", RGB(170, 60, 240), Tetris_DrawIcon, Tetris_Run, SCORE_TETRIS },
  { "SNAKE",  RGB(40, 220, 60),  Snake_DrawIcon,  Snake_Run,  SCORE_SNAKE  },
  { "BRICKS", RGB(255, 145, 0),  Bricks_DrawIcon, Bricks_Run, SCORE_BRICKS },
  { "FLAPPY", RGB(255, 210, 40), Flappy_DrawIcon, Flappy_Run, SCORE_FLAPPY },
  { "2048",   RGB(237, 194, 46), Game2048_DrawIcon, Game2048_Run, SCORE_2048 },
  { "MINES",  RGB(240, 50, 50),  Mines_DrawIcon,  Mines_Run,  SCORE_MINES  },
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
  [NAV_PREV] = { TILE_X0,                  250, 60, 20, 2, "<" },
  [NAV_NEXT] = { LCD_W - TILE_X0 - 60,     250, 60, 20, 2, ">" },
};

static int page;

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

  for (int slot = 0; slot < PER_PAGE; slot++)
    draw_tile(slot, false);

  if (PAGE_COUNT > 1)
  {
    char buf[8];
    snprintf(buf, sizeof buf, "%d/%d", page + 1, PAGE_COUNT);
    gfx_text_centered(LCD_W / 2, 253, buf, 2, COL_LABEL);
    for (int i = 0; i < NAV_COUNT; i++)
      gfx_button(&nav[i], NULL, false);
  }
}

void Menu_Run(void)
{
  int pressed_slot = -1;   /* parmak indiginde altindaki kart */
  bool prev_touch = false;

  Scores_Init();
  draw_menu();

  for (;;)
  {
    int tx = 0, ty = 0;
    bool touch = Touch_Read(&tx, &ty);
    int slot = touch ? hit_tile(tx, ty) : -1;

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
