/*
 * flappy.c - Dokunmatik Flappy Bird
 *
 * Oyun alanina ya da FLAP tusuna dokununca kus zipler.
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

/* Konumlar "kontroller solda" duzeni icin; kontroller sagdayken (varsayilan)
 * panel saga, oyun alani sola kayar (layout_init). */
static int panel_dx, field_dx;

#define FX0     180
#define FX      (FX0 + field_dx)        /* oyun alani */
#define FY      4
#define FW      290
#define FH      264
#define GROUND_H 14
#define GROUND_Y (FY + FH - GROUND_H)

#define BIRD_X  (FX + 60)
#define BIRD_W  16
#define BIRD_H  10

#define PIPE_W   36
#define LIP_OUT  3                      /* agiz kismi govdeden tasar */
#define LIP_H    10
#define PIPE_SPACING 150                /* iki boru arasi yatay mesafe */
#define MAX_PIPES 4

#define COL_SKY      RGB(24, 34, 66)
#define COL_PIPE     RGB(60, 190, 70)
#define COL_PIPE_HI  RGB(130, 230, 120)
#define COL_PIPE_LO  RGB(30, 120, 40)
#define COL_GROUND   RGB(150, 110, 60)
#define COL_GRASS    RGB(90, 200, 70)
#define COL_BIRD     RGB(255, 210, 40)
#define COL_WING     RGB(255, 240, 160)
#define COL_BEAK     RGB(255, 120, 30)
#define COL_EYE      RGB(255, 255, 255)
#define COL_PUPIL    RGB(0, 0, 0)

#define OV_W    220
#define OV_H    132
#define OV_X0   (FX0 + (FW - OV_W) / 2)
#define OV_X    (OV_X0 + field_dx)
#define OV_Y    (FY + (GROUND_Y - FY - OV_H) / 2)
#define OV_CX   (OV_X + OV_W / 2)

enum { BTN_FLAP, BTN_PAUSE, BTN_EXIT, BTN_LV_DOWN, BTN_LV_UP, BTN_COUNT, BTN_NONE = -1 };

static const Button buttons_base[BTN_COUNT] = {
  [BTN_FLAP]    = { 4,  40, 164, 164, 3, "FLAP" },
  [BTN_PAUSE]   = { 4, 214,  80,  50, 2, "PAUSE" },
  [BTN_EXIT]    = { 88, 214, 80,  50, 2, "EXIT" },
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
  field_dx = right ? -174 : 0;   /* alan 180..470 -> 6..296 */

  for (int i = 0; i < BTN_COUNT; i++)
  {
    buttons[i] = buttons_base[i];
    buttons[i].x += is_level_button(i) ? field_dx : panel_dx;
  }
}

/* Fizik (px, ms) */
#define GRAVITY     0.0012f
#define FLAP_VEL    (-0.32f)
#define MAX_FALL    0.45f

#define MAX_LEVEL   10

/* ------------------------------------------------------------------------- */
/* Oyun durumu                                                               */
/* ------------------------------------------------------------------------- */

enum { ST_TITLE, ST_PLAYING, ST_OVER };

typedef struct
{
  bool  active;
  float x;            /* govdenin sol kenari */
  int   gap_y;        /* boslugun ust kenari */
  bool  scored;
  int   drawn_x;      /* INT32_MIN: cizilmedi */
} Pipe;

static Pipe pipes[MAX_PIPES];
static float bird_y, bird_vy;
static int drawn_bird_y = -1;
static bool drawn_wing_up;
static uint32_t score, best, level = 1;
static int gap_h;
static float pipe_speed;
static int state;
static bool paused;
static bool new_best;

static int level_gap(void)    { return 96 - ((int)level - 1) * 3; }
static float level_speed(void){ return 0.07f * (1.0f + 0.08f * (float)(level - 1)); }

/* ------------------------------------------------------------------------- */
/* Cizim                                                                     */
/* ------------------------------------------------------------------------- */

static void draw_button(int i, bool active)
{
  gfx_button(&buttons[i], (i == BTN_PAUSE && paused) ? "PLAY" : NULL, active);
}

/* Oyun alaninin (zemin haric) yatay sinirlarina kirpilmis dikdortgen */
static void fill_field(int x, int y, int w, int h, uint16_t c)
{
  if (x < FX) { w -= FX - x; x = FX; }
  if (x + w > FX + FW) w = FX + FW - x;
  if (w > 0 && h > 0)
    gfx_fill(x, y, w, h, c);
}

static void draw_ground(void)
{
  gfx_fill(FX, GROUND_Y, FW, 3, COL_GRASS);
  gfx_fill(FX, GROUND_Y + 3, FW, GROUND_H - 3, COL_GROUND);
  for (int x = FX + 6; x < FX + FW; x += 18)
    gfx_fill(x, GROUND_Y + 7, 8, 2, gfx_mix(COL_GROUND, 0x0000, 60));
}

/* Borunun x konumundaki dikey parcalari: govde ve aginin y araliklari */
static void pipe_rows(const Pipe *p, int *top_body_h, int *top_lip_y, int *bot_lip_y, int *bot_body_y)
{
  *top_lip_y  = p->gap_y - LIP_H;
  *top_body_h = *top_lip_y - FY;
  *bot_lip_y  = p->gap_y + gap_h;
  *bot_body_y = *bot_lip_y + LIP_H;
}

static void draw_pipe_body(int x, int y, int h)
{
  fill_field(x, y, PIPE_W, h, COL_PIPE);
  fill_field(x + 4, y, 5, h, COL_PIPE_HI);
  fill_field(x + PIPE_W - 6, y, 6, h, COL_PIPE_LO);
}

static void draw_pipe_lip(int x, int y)
{
  int lx = x - LIP_OUT, lw = PIPE_W + 2 * LIP_OUT;
  fill_field(lx, y, lw, LIP_H, COL_PIPE);
  fill_field(lx + 3, y + 1, 5, LIP_H - 2, COL_PIPE_HI);
  fill_field(lx + lw - 6, y, 6, LIP_H, COL_PIPE_LO);
  fill_field(lx, y, lw, 1, COL_PIPE_LO);
  fill_field(lx, y + LIP_H - 1, lw, 1, COL_PIPE_LO);
}

/* Boruyu yeni konumunda cizer, sadece geride acilan seridi gokyuzu yapar */
static void draw_pipe(Pipe *p, bool force)
{
  int x = (int)p->x;
  int tbh, tly, bly, bby;

  if (!force && x == p->drawn_x)
    return;
  pipe_rows(p, &tbh, &tly, &bly, &bby);

  draw_pipe_body(x, FY, tbh);
  draw_pipe_lip(x, tly);
  draw_pipe_lip(x, bly);
  draw_pipe_body(x, bby, GROUND_Y - bby);

  if (!force && p->drawn_x != INT32_MIN && p->drawn_x > x)
  {
    int d = p->drawn_x - x;
    fill_field(x + PIPE_W, FY, d, tbh, COL_SKY);
    fill_field(x + PIPE_W, bby, d, GROUND_Y - bby, COL_SKY);
    fill_field(x + PIPE_W + LIP_OUT, tly, d, LIP_H, COL_SKY);
    fill_field(x + PIPE_W + LIP_OUT, bly, d, LIP_H, COL_SKY);
  }
  p->drawn_x = x;
}

static void draw_bird(bool force)
{
  int y = (int)bird_y;
  bool wing_up = bird_vy < 0;

  if (!force && y == drawn_bird_y && wing_up == drawn_wing_up)
    return;
  if (drawn_bird_y >= 0)
    gfx_fill(BIRD_X, drawn_bird_y, BIRD_W, BIRD_H, COL_SKY);

  int x = BIRD_X;
  gfx_fill(x + 1, y, 12, BIRD_H, COL_BIRD);                 /* govde */
  gfx_fill(x, y + 1, 14, BIRD_H - 2, COL_BIRD);
  gfx_fill(x + 9, y + 1, 4, 4, COL_EYE);                    /* goz */
  gfx_fill(x + 11, y + 2, 2, 2, COL_PUPIL);
  gfx_fill(x + 12, y + 5, 4, 3, COL_BEAK);                  /* gaga */
  gfx_fill(x + 2, wing_up ? y + 2 : y + 5, 6, 3, COL_WING); /* kanat */

  drawn_bird_y = y;
  drawn_wing_up = wing_up;
}

static void draw_field(void)
{
  gfx_fill(FX, FY, FW, GROUND_Y - FY, COL_SKY);
  draw_ground();
  drawn_bird_y = -1;
  if (state == ST_TITLE)
    return;
  for (int i = 0; i < MAX_PIPES; i++)
    if (pipes[i].active)
      draw_pipe(&pipes[i], true);
  draw_bird(true);
}

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
  gfx_outline(FX - 2, FY - 2, FW + 4, FH + 4, COL_BORDER);
  gfx_outline(FX - 1, FY - 1, FW + 2, FH + 2, COL_BORDER);

  gfx_text_centered(STAT_SCORE_X, 6, "SCORE", 1, COL_LABEL);
  gfx_text_centered(STAT_BEST_X, 6, "BEST", 1, COL_LABEL);
  gfx_text_centered(STAT_LEVEL_X, 6, "LEVEL", 1, COL_LABEL);

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
      gfx_text_centered(OV_CX, OV_Y + 8, "GAME OVER", 3, COL_TITLE);
      snprintf(buf, sizeof buf, "SCORE %lu", (unsigned long)score);
      gfx_text_centered(OV_CX, OV_Y + 33, buf, 2, COL_TEXT);
      if (new_best)
        gfx_text_centered(OV_CX, OV_Y + 51, "NEW BEST", 1, COL_TITLE);
      gfx_text_centered(OV_CX, OV_Y + 110, "TAP TO RETRY", 1, COL_LABEL);
      break;
    default:
      gfx_text_centered(OV_CX, OV_Y + 14, "FLAPPY", 3, COL_TITLE);
      gfx_text_centered(OV_CX, OV_Y + 44, "TAP TO FLY", 1, COL_LABEL);
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

static void spawn_pipe(float x)
{
  for (int i = 0; i < MAX_PIPES; i++)
    if (!pipes[i].active)
    {
      int lo = FY + 24 + LIP_H;
      int hi = GROUND_Y - 24 - LIP_H - gap_h;
      pipes[i].active = true;
      pipes[i].x = x;
      pipes[i].gap_y = lo + (int)(rand_next() % (uint32_t)(hi - lo + 1));
      pipes[i].scored = false;
      pipes[i].drawn_x = INT32_MIN;
      return;
    }
}

static void game_over(void)
{
  state = ST_OVER;
  new_best = Scores_Submit(SCORE_FLAPPY, score);
  best = Scores_Get(SCORE_FLAPPY);
  update_stats(false);
  draw_overlay(OV_OVER);
}

static void start_game(void)
{
  memset(pipes, 0, sizeof pipes);
  score = 0;
  gap_h = level_gap();
  pipe_speed = level_speed();
  bird_y = (FY + GROUND_Y) / 2.0f - BIRD_H;
  bird_vy = FLAP_VEL;          /* baslatan dokunus ilk kanat cirpisi */
  state = ST_PLAYING;
  spawn_pipe(FX + FW);
  draw_field();
}

static bool hits_pipe(const Pipe *p)
{
  /* Kusun kutusunu 1px iceriden al: adil carpisma */
  int bx0 = BIRD_X + 1, bx1 = BIRD_X + BIRD_W - 2;
  int by0 = (int)bird_y + 1, by1 = (int)bird_y + BIRD_H - 2;
  int px0 = (int)p->x - LIP_OUT, px1 = (int)p->x + PIPE_W + LIP_OUT - 1;

  if (bx1 < px0 || bx0 > px1)
    return false;
  return by0 < p->gap_y || by1 >= p->gap_y + gap_h;
}

static void update_game(float dt)
{
  /* Kus */
  bird_vy += GRAVITY * dt;
  if (bird_vy > MAX_FALL)
    bird_vy = MAX_FALL;
  bird_y += bird_vy * dt;
  if (bird_y < FY)
  {
    bird_y = FY;
    bird_vy = 0;
  }
  if (bird_y + BIRD_H >= GROUND_Y)
  {
    bird_y = GROUND_Y - BIRD_H;
    draw_bird(false);
    game_over();
    return;
  }

  /* Borular */
  float rightmost = -1e9f;
  for (int i = 0; i < MAX_PIPES; i++)
  {
    Pipe *p = &pipes[i];
    if (!p->active)
      continue;

    p->x -= pipe_speed * dt;
    if (p->x + PIPE_W + LIP_OUT < FX)
    {
      /* Alanin solundan cikti: kenarda kalan son seridi sil */
      if (p->drawn_x != INT32_MIN)
        fill_field(p->drawn_x - LIP_OUT, FY, PIPE_W + 2 * LIP_OUT, GROUND_Y - FY, COL_SKY);
      p->active = false;
      continue;
    }
    if (p->x > rightmost)
      rightmost = p->x;
    if (!p->scored && p->x + PIPE_W < BIRD_X)
    {
      p->scored = true;
      score++;
    }
  }
  if (rightmost < FX + FW - PIPE_SPACING)
    spawn_pipe(FX + FW);

  /* Ciz, sonra carpismayi kontrol et (son kare ekranda gorunsun) */
  for (int i = 0; i < MAX_PIPES; i++)
    if (pipes[i].active)
      draw_pipe(&pipes[i], false);
  draw_bird(false);

  for (int i = 0; i < MAX_PIPES; i++)
    if (pipes[i].active && hits_pipe(&pipes[i]))
    {
      game_over();
      return;
    }
}

static void set_paused(bool p)
{
  paused = p;
  if (paused)
    draw_overlay(OV_PAUSED);
  else
    draw_field();
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

void Flappy_DrawIcon(int cx, int cy)
{
  /* iki boru ve aradaki kus */
  gfx_fill(cx - 30, cy - 22, 12, 14, COL_PIPE);
  gfx_fill(cx - 32, cy - 10, 16, 4, COL_PIPE);
  gfx_fill(cx - 32, cy + 8, 16, 4, COL_PIPE);
  gfx_fill(cx - 30, cy + 12, 12, 10, COL_PIPE);
  gfx_fill(cx + 18, cy - 22, 12, 6, COL_PIPE);
  gfx_fill(cx + 16, cy - 16, 16, 4, COL_PIPE);
  gfx_fill(cx + 16, cy + 2, 16, 4, COL_PIPE);
  gfx_fill(cx + 18, cy + 6, 12, 16, COL_PIPE);

  int x = cx - 8, y = cy - 6;
  gfx_fill(x + 1, y, 12, 10, COL_BIRD);
  gfx_fill(x, y + 1, 14, 8, COL_BIRD);
  gfx_fill(x + 9, y + 1, 4, 4, COL_EYE);
  gfx_fill(x + 11, y + 2, 2, 2, COL_PUPIL);
  gfx_fill(x + 12, y + 5, 4, 3, COL_BEAK);
  gfx_fill(x + 2, y + 2, 6, 3, COL_WING);
}

#define FRAME_MS 10

void Flappy_Run(void)
{
  int prev_btn = BTN_NONE;
  bool prev_touch = false;
  uint32_t last_frame = HAL_GetTick();

  layout_init();
  paused = false;
  state = ST_TITLE;
  score = 0;
  best = Scores_Get(SCORE_FLAPPY);

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
    float dt = (float)(now - last_frame);
    last_frame = now;
    if (dt > 30.0f)
      dt = 30.0f;

    if (state == ST_PLAYING && is_level_button(btn))
      btn = BTN_NONE;   /* oyun sirasinda gizliler */
    bool pressed = (btn != prev_btn);
    bool tap = touch && !prev_touch;

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
      if (tap && btn != BTN_PAUSE && btn != BTN_EXIT && !is_level_button(btn))
        start_game();
    }
    else if (!paused)
    {
      /* FLAP tusu ya da oyun alanina dokunus */
      if (tap && (btn == BTN_FLAP || (btn == BTN_NONE && tx >= FX - 10 && tx < FX + FW + 10)))
        bird_vy = FLAP_VEL;
      update_game(dt);
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
