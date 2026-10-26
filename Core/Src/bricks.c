/*
 * bricks.c - Dokunmatik Bricks Breaker
 *
 * Raket: oyun alaninda parmakla surukle ya da soldaki ok tuslari.
 * Kirilan tuglalardan rastgele bonus kapsulleri duser; raketle yakala.
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

#define BCOLS   10
#define BROWS   8
#define BW      29                      /* tugla + 1px bosluk */
#define BH      13
#define BY0     (FY + 24)               /* ilk tugla satiri */

#define PW_NORMAL 50                    /* raket */
#define PW_WIDE   80
#define PH      7
#define PY      (FY + FH - 16)

#define BALL    6

#define COL_PADDLE  RGB(230, 235, 255)
#define COL_BALL    RGB(255, 255, 255)
#define COL_FIREBALL RGB(255, 120, 30)
#define COL_STRONG  RGB(175, 180, 195)

#define OV_W    220
#define OV_H    132
#define OV_X0   (FX0 + (FW - OV_W) / 2)
#define OV_X    (OV_X0 + field_dx)
#define OV_Y    (FY + (FH - OV_H) / 2)
#define OV_CX   (OV_X + OV_W / 2)

enum { BTN_LEFT, BTN_RIGHT, BTN_PAUSE, BTN_EXIT, BTN_LV_DOWN, BTN_LV_UP, BTN_COUNT, BTN_NONE = -1 };

static const Button buttons_base[BTN_COUNT] = {
  [BTN_LEFT]    = {  4,  62, 80, 142, 2, "" },
  [BTN_RIGHT]   = { 88,  62, 80, 142, 2, "" },
  [BTN_PAUSE]   = {  4, 214, 80,  50, 2, "PAUSE" },
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

/* '.' bos, '1' normal, '2' iki vurusta kirilan */
#define NPATTERNS 4
static const char *const patterns[NPATTERNS][BROWS] = {
  { "..........", "1111111111", "1111111111", "1111111111",
    "1111111111", "1111111111", "..........", ".........." },
  { "2222222222", "1111111111", "1.1.1.1.1.", ".1.1.1.1.1",
    "1.1.1.1.1.", ".1.1.1.1.1", "..........", ".........." },
  { "....22....", "...1111...", "..111111..", ".11111111.",
    "1111111111", "2222222222", "..........", ".........." },
  { "2222222222", "2111111112", "21.1111.12", "2111111112",
    "21.1111.12", "2111111112", "2222222222", ".........." },
};

static const uint16_t row_color[BROWS] = {
  RGB(240, 50, 60), RGB(255, 145, 0), RGB(250, 220, 0), RGB(40, 220, 60),
  RGB(0, 220, 230), RGB(50, 110, 255), RGB(170, 70, 240), RGB(255, 90, 180),
};

#define MAX_LEVEL 10

/* ------------------------------------------------------------------------- */
/* Bonuslar                                                                  */
/* ------------------------------------------------------------------------- */

enum { BONUS_WIDE, BONUS_SLOW, BONUS_FIRE, BONUS_LIFE, BONUS_COUNT };

typedef struct
{
  const char *label;     /* kapsulde yazan harf */
  const char *name;      /* sol paneldeki gosterge */
  uint16_t    color;
  uint8_t     weight;    /* dusme olasiligi agirligi */
  uint16_t    seconds;   /* 0: anlik etki */
} BonusInfo;

static const BonusInfo bonus_info[BONUS_COUNT] = {
  [BONUS_WIDE] = { "W", "WIDE", RGB(50, 110, 255), 30, 15 },
  [BONUS_SLOW] = { "S", "SLOW", RGB(40, 190, 60),  30, 10 },
  [BONUS_FIRE] = { "F", "FIRE", RGB(255, 120, 30), 25, 7  },
  [BONUS_LIFE] = { "+", "LIFE", RGB(255, 80, 170), 15, 0  },
};

#define DROP_CHANCE     18      /* kirilan tugla basina % */
#define SLOW_FACTOR     0.65f
#define MAX_LIVES       9

#define CAP_W           20      /* kapsul */
#define CAP_H           9
#define CAP_SPEED       0.06f   /* px/ms */
#define MAX_CAPSULES    4

typedef struct
{
  bool  active;
  int   type;
  float x, y;
  int   drawn_x, drawn_y;       /* -1: cizilmedi */
} Capsule;

static Capsule capsules[MAX_CAPSULES];
static uint32_t bonus_until[BONUS_COUNT]; /* 0: pasif, yoksa bitis zamani (tick) */

/* Sol paneldeki gosterge satiri (sureli bonuslar) */
#define STATUS_Y  40
static const int status_x[BONUS_COUNT] = { 30, 86, 142, 0 };

/* ------------------------------------------------------------------------- */
/* Oyun durumu                                                               */
/* ------------------------------------------------------------------------- */

enum { ST_TITLE, ST_PLAYING, ST_OVER };

static uint8_t bricks[BROWS][BCOLS];         /* kalan vurus sayisi */
static int bricks_left;
static float ball_x, ball_y, vel_x, vel_y;   /* sol ust kose, px/ms */
static float pad_x;                          /* raket sol kenari */
static int pad_w = PW_NORMAL;
static bool stuck;                           /* top rakette bekliyor */
static uint32_t serve_at;
static int drawn_ball_x = -1, drawn_ball_y = -1;
static bool drawn_ball_fire;
static int drawn_pad_x = -1, drawn_pad_w;
static uint32_t score, best, lives, level;
static uint32_t start_level = 1;
static int state;
static bool paused;
static uint32_t paused_at;
static bool new_best;

static float fsqrt(float v)
{
  float r;
  __asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(v));
  return r;
}

static bool bonus_active(int b)
{
  return bonus_until[b] != 0;
}

/* Top hizi, px/ms. Seviye 1: 120 px/s, her seviyede %7 artar (10'da ~196 px/s). */
#define BALL_SPEED_BASE   0.12f
#define BALL_SPEED_STEP   0.07f

static float ball_speed(void)
{
  float s = BALL_SPEED_BASE * (1.0f + BALL_SPEED_STEP * (float)(level - 1));
  return bonus_active(BONUS_SLOW) ? s * SLOW_FACTOR : s;
}

/* Hiz degisince (SLOW basladi/bitti) yonu koruyup buyuklugu ayarla */
static void rescale_ball(void)
{
  float mag = fsqrt(vel_x * vel_x + vel_y * vel_y);
  if (stuck || mag <= 0.0f)
    return;
  float k = ball_speed() / mag;
  vel_x *= k;
  vel_y *= k;
}

/* ------------------------------------------------------------------------- */
/* Cizim                                                                     */
/* ------------------------------------------------------------------------- */

static void draw_button(int i, bool active)
{
  const Button *b = &buttons[i];

  gfx_button(b, (i == BTN_PAUSE && paused) ? "PLAY" : NULL, active);
  if (i == BTN_LEFT || i == BTN_RIGHT)
    gfx_arrow(b->x + b->w / 2, b->y + b->h / 2, 16, i == BTN_LEFT ? ARROW_LEFT : ARROW_RIGHT, COL_TEXT);
}

static void draw_brick(int r, int c)
{
  int x = FX + c * BW, y = BY0 + r * BH;
  int w = BW - 1, h = BH - 1;

  if (!bricks[r][c])
  {
    gfx_fill(x, y, BW, BH, COL_BOARD);
    return;
  }

  uint16_t col = bricks[r][c] > 1 ? COL_STRONG : row_color[r];
  gfx_fill(x, y, w, h, gfx_mix(col, 0x0000, 120));
  gfx_fill(x, y, w - 1, h - 1, gfx_mix(col, 0xFFFF, 110));
  gfx_fill(x + 1, y + 1, w - 2, h - 2, col);
}

/* Bir dikdortgen silindikten sonra altinda kalan tuglalari onar */
static void repair_bricks(int x, int y, int w, int h)
{
  int c0 = (x - FX) / BW, c1 = (x + w - 1 - FX) / BW;
  int r0 = (y - BY0) / BH, r1 = (y + h - 1 - BY0) / BH;

  if (y + h <= BY0 || y >= BY0 + BROWS * BH)
    return;
  if (r0 < 0) r0 = 0;
  if (c0 < 0) c0 = 0;
  for (int r = r0; r <= r1 && r < BROWS; r++)
    for (int c = c0; c <= c1 && c < BCOLS; c++)
      if (bricks[r][c])
        draw_brick(r, c);
}

static void draw_paddle(void)
{
  int x = (int)pad_x;

  if (drawn_pad_x >= 0 && (drawn_pad_x != x || drawn_pad_w != pad_w))
  {
    if (drawn_pad_w != pad_w)
      gfx_fill(drawn_pad_x, PY, drawn_pad_w, PH, COL_BOARD);
    else if (x > drawn_pad_x)   /* sadece acilan seridi sil, titreme olmasin */
      gfx_fill(drawn_pad_x, PY, x - drawn_pad_x, PH, COL_BOARD);
    else
      gfx_fill(x + pad_w, PY, drawn_pad_x - x, PH, COL_BOARD);
  }

  uint16_t col = bonus_active(BONUS_WIDE) ? gfx_mix(COL_PADDLE, bonus_info[BONUS_WIDE].color, 110) : COL_PADDLE;
  gfx_fill(x, PY, pad_w, PH, col);
  gfx_fill(x + 2, PY + PH - 2, pad_w - 4, 2, gfx_mix(col, 0x0000, 100));
  drawn_pad_x = x;
  drawn_pad_w = pad_w;
}

static void erase_ball(void)
{
  if (drawn_ball_x >= 0)
  {
    gfx_fill(drawn_ball_x, drawn_ball_y, BALL, BALL, COL_BOARD);
    repair_bricks(drawn_ball_x, drawn_ball_y, BALL, BALL);
  }
  drawn_ball_x = -1;
}

static void draw_ball(void)
{
  int x = (int)ball_x, y = (int)ball_y;
  bool fire = bonus_active(BONUS_FIRE);

  if (x == drawn_ball_x && y == drawn_ball_y && fire == drawn_ball_fire)
    return;
  erase_ball();
  uint16_t col = fire ? COL_FIREBALL : COL_BALL;
  gfx_fill(x + 1, y, BALL - 2, BALL, col);
  gfx_fill(x, y + 1, BALL, BALL - 2, col);
  drawn_ball_x = x;
  drawn_ball_y = y;
  drawn_ball_fire = fire;
}

static void draw_capsule_at(int x, int y, int type)
{
  const BonusInfo *bi = &bonus_info[type];
  gfx_fill(x + 1, y, CAP_W - 2, CAP_H, bi->color);
  gfx_fill(x, y + 1, CAP_W, CAP_H - 2, bi->color);
  gfx_text_centered(x + CAP_W / 2, y + 1, bi->label, 1, COL_TEXT);
}

static void erase_capsule(Capsule *cp)
{
  if (cp->drawn_x < 0)
    return;
  gfx_fill(cp->drawn_x, cp->drawn_y, CAP_W, CAP_H, COL_BOARD);
  repair_bricks(cp->drawn_x, cp->drawn_y, CAP_W, CAP_H);
  cp->drawn_x = -1;
}

static void draw_capsules(bool force)
{
  for (int i = 0; i < MAX_CAPSULES; i++)
  {
    Capsule *cp = &capsules[i];
    int x = (int)cp->x, y = (int)cp->y;

    if (!cp->active)
      continue;
    if (!force && x == cp->drawn_x && y == cp->drawn_y)
      continue;
    erase_capsule(cp);
    draw_capsule_at(x, y, cp->type);
    cp->drawn_x = x;
    cp->drawn_y = y;
  }
}

/* Sol paneldeki sureli bonus gostergeleri ("WIDE 12" gibi) */
static void update_bonus_status(bool force)
{
  static int shown_secs[BONUS_COUNT];
  uint32_t now = HAL_GetTick();

  for (int b = 0; b < BONUS_COUNT; b++)
  {
    if (!bonus_info[b].seconds)
      continue;

    /* Duraklatilmisken sayac donmus gorunsun */
    uint32_t ref = paused ? paused_at : now;
    int secs = 0;
    if (bonus_active(b) && (int32_t)(bonus_until[b] - ref) > 0)
      secs = (int)((bonus_until[b] - ref + 999) / 1000);
    if (!force && secs == shown_secs[b])
      continue;
    shown_secs[b] = secs;

    int cx = status_x[b] + panel_dx;
    int x = cx - 26;
    if (secs > 0)
    {
      char buf[12];
      snprintf(buf, sizeof buf, "%s %d", bonus_info[b].name, secs);
      gfx_fill(x, STATUS_Y, 52, 16, bonus_info[b].color);
      gfx_text_centered(cx, STATUS_Y + 4, buf, 1, COL_TEXT);
    }
    else
    {
      gfx_fill(x, STATUS_Y, 52, 16, COL_BG);
    }
  }
}

static void draw_field(void)
{
  gfx_fill(FX, FY, FW, FH, COL_BOARD);
  drawn_ball_x = drawn_pad_x = -1;
  for (int i = 0; i < MAX_CAPSULES; i++)
    capsules[i].drawn_x = -1;
  if (state == ST_TITLE)
    return;

  for (int r = 0; r < BROWS; r++)
    for (int c = 0; c < BCOLS; c++)
      if (bricks[r][c])
        draw_brick(r, c);
  draw_capsules(true);
  draw_paddle();
  draw_ball();
}

#define STAT_SCORE_X  (30 + panel_dx)
#define STAT_LIVES_X  (86 + panel_dx)
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
  static uint32_t s = UINT32_MAX, li = UINT32_MAX, lv = UINT32_MAX;

  if (force) { s = li = lv = UINT32_MAX; }
  if (score != s)  { draw_value(STAT_SCORE_X, score); s = score; }
  if (lives != li) { draw_value(STAT_LIVES_X, lives); li = lives; }
  if (level != lv) { draw_value(STAT_LEVEL_X, level); lv = level; }
}

static void draw_static(void)
{
  gfx_fill(0, 0, LCD_W, LCD_H, COL_BG);
  gfx_outline(FX - 2, FY - 2, FW + 4, FH + 4, COL_BORDER);
  gfx_outline(FX - 1, FY - 1, FW + 2, FH + 2, COL_BORDER);

  gfx_text_centered(STAT_SCORE_X, 6, "SCORE", 1, COL_LABEL);
  gfx_text_centered(STAT_LIVES_X, 6, "LIVES", 1, COL_LABEL);
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

  snprintf(buf, sizeof buf, "%lu", (unsigned long)start_level);
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
        snprintf(buf, sizeof buf, "NEW BEST");
      else
        snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)best);
      gfx_text_centered(OV_CX, OV_Y + 51, buf, 1, new_best ? COL_TITLE : COL_LABEL);
      gfx_text_centered(OV_CX, OV_Y + 110, "TAP TO RETRY", 1, COL_LABEL);
      break;
    default:
      gfx_text_centered(OV_CX, OV_Y + 14, "BRICKS", 3, COL_TITLE);
      snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)best);
      gfx_text_centered(OV_CX, OV_Y + 44, buf, 1, COL_LABEL);
      gfx_text_centered(OV_CX, OV_Y + 110, "TAP TO START", 1, COL_LABEL);
      break;
  }

  draw_button(BTN_LV_DOWN, false);
  draw_button(BTN_LV_UP, false);
  draw_level_selector();
}

/* Bolum gecisinde kisa "LEVEL n" yazisi */
static void show_level_banner(void)
{
  char buf[16];
  snprintf(buf, sizeof buf, "LEVEL %lu", (unsigned long)level);
  gfx_panel(OV_X, OV_Y + 36, OV_W, 60);
  gfx_text_centered(OV_CX, OV_Y + 55, buf, 3, COL_TITLE);
  osDelay(1200);
}

/* ------------------------------------------------------------------------- */
/* Bonus mantigi                                                             */
/* ------------------------------------------------------------------------- */

static void set_paddle_width(int w)
{
  float center = pad_x + pad_w / 2.0f;
  pad_w = w;
  pad_x = center - w / 2.0f;
  if (pad_x < FX) pad_x = FX;
  if (pad_x > FX + FW - pad_w) pad_x = FX + FW - pad_w;
}

static void clear_bonuses(void)
{
  for (int i = 0; i < MAX_CAPSULES; i++)
  {
    erase_capsule(&capsules[i]);
    capsules[i].active = false;
  }
  memset(bonus_until, 0, sizeof bonus_until);
  if (pad_w != PW_NORMAL)
    set_paddle_width(PW_NORMAL);
}

static void maybe_drop_capsule(int r, int c)
{
  if (rand_next() % 100 >= DROP_CHANCE)
    return;

  int total = 0, pick;
  for (int b = 0; b < BONUS_COUNT; b++)
    total += bonus_info[b].weight;
  pick = (int)(rand_next() % (uint32_t)total);

  int type = 0;
  while (pick >= bonus_info[type].weight)
    pick -= bonus_info[type++].weight;

  for (int i = 0; i < MAX_CAPSULES; i++)
    if (!capsules[i].active)
    {
      capsules[i].active = true;
      capsules[i].type = type;
      capsules[i].x = FX + c * BW + (BW - CAP_W) / 2.0f;
      capsules[i].y = BY0 + r * BH + 2;
      capsules[i].drawn_x = -1;
      return;
    }
}

static void apply_bonus(int type)
{
  uint32_t now = HAL_GetTick();

  score += 50;
  switch (type)
  {
    case BONUS_WIDE:
      set_paddle_width(PW_WIDE);
      break;
    case BONUS_LIFE:
      if (lives < MAX_LIVES)
        lives++;
      break;
    default:
      break;
  }
  if (bonus_info[type].seconds)
  {
    bonus_until[type] = now + bonus_info[type].seconds * 1000u;
    if (type == BONUS_SLOW)
      rescale_ball();
  }
}

/* Suresi dolan bonuslari kapat */
static void update_bonus_timers(uint32_t now)
{
  for (int b = 0; b < BONUS_COUNT; b++)
  {
    if (!bonus_active(b) || (int32_t)(now - bonus_until[b]) < 0)
      continue;
    bonus_until[b] = 0;
    if (b == BONUS_WIDE)
      set_paddle_width(PW_NORMAL);
    else if (b == BONUS_SLOW)
      rescale_ball();
  }
}

static void move_capsules(float dt)
{
  for (int i = 0; i < MAX_CAPSULES; i++)
  {
    Capsule *cp = &capsules[i];
    if (!cp->active)
      continue;

    cp->y += CAP_SPEED * dt;

    bool caught = cp->y + CAP_H >= PY && cp->y <= PY + PH &&
                  cp->x + CAP_W > pad_x && cp->x < pad_x + pad_w;
    if (caught || cp->y > FY + FH - CAP_H)
    {
      erase_capsule(cp);
      cp->active = false;
      if (caught)
        apply_bonus(cp->type);
    }
  }
}

/* ------------------------------------------------------------------------- */
/* Oyun mantigi                                                              */
/* ------------------------------------------------------------------------- */

static void build_bricks(void)
{
  const char *const *p = patterns[(level - 1) % NPATTERNS];

  bricks_left = 0;
  for (int r = 0; r < BROWS; r++)
    for (int c = 0; c < BCOLS; c++)
    {
      char ch = p[r][c];
      bricks[r][c] = (ch == '1') ? 1 : (ch == '2') ? 2 : 0;
      bricks_left += bricks[r][c] ? 1 : 0;
    }
}

static void place_on_paddle(void)
{
  ball_x = pad_x + (pad_w - BALL) / 2.0f;
  ball_y = PY - BALL;
}

static void serve(void)
{
  stuck = true;
  serve_at = HAL_GetTick() + 1000;
  place_on_paddle();
}

static void launch(void)
{
  float s = ball_speed();
  vel_x = s * ((rand_next() & 1) ? 0.45f : -0.45f);
  vel_y = -fsqrt(s * s - vel_x * vel_x);
  stuck = false;
}

static void game_over(void)
{
  state = ST_OVER;
  new_best = Scores_Submit(SCORE_BRICKS, score);
  best = Scores_Get(SCORE_BRICKS);
  draw_overlay(OV_OVER);
}

static void start_game(void)
{
  score = 0;
  lives = 3;
  level = start_level;
  pad_w = PW_NORMAL;
  pad_x = FX + (FW - pad_w) / 2.0f;
  memset(capsules, 0, sizeof capsules);
  memset(bonus_until, 0, sizeof bonus_until);
  state = ST_PLAYING;
  build_bricks();
  draw_field();
  update_bonus_status(true);
  serve();
}

static void next_level(void)
{
  if (level < 99)
    level++;
  clear_bonuses();
  build_bricks();
  update_stats(false);
  update_bonus_status(false);
  draw_field();
  show_level_banner();
  draw_field();
  serve();
}

static void lose_life(void)
{
  erase_ball();
  clear_bonuses();
  lives--;
  if (lives == 0)
  {
    update_stats(false);
    game_over();
    return;
  }
  serve();
}

/* Topun dikdortgeniyle cakisan ilk tugla; yoksa -1 */
static int brick_at(float fx, float fy)
{
  int x0 = (int)fx - FX, y0 = (int)fy - BY0;
  int x1 = x0 + BALL - 1, y1 = y0 + BALL - 1;

  if (y1 < 0 || y0 >= BROWS * BH || x1 < 0 || x0 >= BCOLS * BW)
    return -1;
  if (y0 < 0) y0 = 0;
  if (x0 < 0) x0 = 0;

  for (int r = y0 / BH; r <= y1 / BH && r < BROWS; r++)
    for (int c = x0 / BW; c <= x1 / BW && c < BCOLS; c++)
      if (bricks[r][c])
        return r * BCOLS + c;
  return -1;
}

static void hit_brick(int idx, bool smash)
{
  int r = idx / BCOLS, c = idx % BCOLS;

  bricks[r][c] = smash ? 0 : bricks[r][c] - 1;
  score += 10 * level;
  if (!bricks[r][c])
  {
    bricks_left--;
    maybe_drop_capsule(r, c);
  }
  draw_brick(r, c);
}

/* Ates topu: tuglalari sekmeden deler gecer */
static void smash_all_at(float x, float y)
{
  int b;
  while ((b = brick_at(x, y)) >= 0)
    hit_brick(b, true);
}

/* Topu dt ms ilerletir. Tunelleme olmasin diye en fazla 2px'lik adimlarla. */
static void move_ball(float dt)
{
  float dx = vel_x * dt, dy = vel_y * dt;
  float dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
  int n = (int)(dist / 2.0f) + 1;
  float sx = dx / n, sy = dy / n;
  bool fire = bonus_active(BONUS_FIRE);

  for (int i = 0; i < n; i++)
  {
    int b;

    /* Yatay */
    ball_x += sx;
    if (ball_x < FX)              { ball_x = FX;             vel_x = -vel_x; sx = -sx; }
    if (ball_x > FX + FW - BALL)  { ball_x = FX + FW - BALL; vel_x = -vel_x; sx = -sx; }
    if (fire)
    {
      smash_all_at(ball_x, ball_y);
    }
    else if ((b = brick_at(ball_x, ball_y)) >= 0)
    {
      ball_x -= sx; vel_x = -vel_x; sx = -sx;
      hit_brick(b, false);
    }

    /* Dikey */
    ball_y += sy;
    if (ball_y < FY) { ball_y = FY; vel_y = -vel_y; sy = -sy; }
    if (fire)
    {
      smash_all_at(ball_x, ball_y);
    }
    else if ((b = brick_at(ball_x, ball_y)) >= 0)
    {
      ball_y -= sy; vel_y = -vel_y; sy = -sy;
      hit_brick(b, false);
    }

    /* Raket: carpma noktasina gore aci ver */
    if (vel_y > 0 && ball_y + BALL >= PY && ball_y + BALL <= PY + PH &&
        ball_x + BALL > pad_x && ball_x < pad_x + pad_w)
    {
      float off = ((ball_x + BALL / 2.0f) - (pad_x + pad_w / 2.0f)) / (pad_w / 2.0f);
      if (off > 1.0f) off = 1.0f;
      if (off < -1.0f) off = -1.0f;

      float s = ball_speed();
      ball_y = PY - BALL;
      vel_x = s * off * 0.85f;
      vel_y = -fsqrt(s * s - vel_x * vel_x);
      sx = vel_x * dt / n;
      sy = vel_y * dt / n;
    }

    if (ball_y > FY + FH - BALL)
    {
      lose_life();
      return;
    }
    if (bricks_left == 0)
    {
      next_level();
      return;
    }
  }
}

static void move_paddle(float x)
{
  if (x < FX) x = FX;
  if (x > FX + FW - pad_w) x = FX + FW - pad_w;
  pad_x = x;
}

static void set_paused(bool p)
{
  paused = p;
  if (paused)
  {
    paused_at = HAL_GetTick();
    draw_overlay(OV_PAUSED);
  }
  else
  {
    /* Duraklatma suresi bonus surelerinden sayilmasin */
    uint32_t pause_len = HAL_GetTick() - paused_at;
    for (int b = 0; b < BONUS_COUNT; b++)
      if (bonus_active(b))
        bonus_until[b] += pause_len;
    draw_field();
    if (stuck)
      serve_at = HAL_GetTick() + 1000;
  }
}

static void change_level(int delta)
{
  if (delta < 0 && start_level > 1)
    start_level--;
  else if (delta > 0 && start_level < MAX_LEVEL)
    start_level++;
  level = start_level;
  draw_level_selector();
}

/* ------------------------------------------------------------------------- */
/* Disariya acik fonksiyonlar                                                */
/* ------------------------------------------------------------------------- */

void Bricks_DrawIcon(int cx, int cy)
{
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 5; c++)
      gfx_fill(cx - 30 + c * 12, cy - 18 + r * 7, 11, 6, row_color[r]);
  gfx_fill(cx + 4, cy + 6, 5, 5, COL_BALL);
  gfx_fill(cx - 14, cy + 16, 28, 4, COL_PADDLE);
}

#define FRAME_MS        10
#define PAD_BTN_SPEED   0.28f   /* px/ms, ok tuslariyla */
#define PAD_TOUCH_SPEED 0.40f   /* px/ms, parmagi takip ederken en fazla */

void Bricks_Run(void)
{
  int prev_btn = BTN_NONE;
  bool prev_touch = false;
  uint32_t last_frame = HAL_GetTick();

  layout_init();
  paused = false;
  state = ST_TITLE;
  score = 0;
  best = Scores_Get(SCORE_BRICKS);
  lives = 3;
  level = start_level;
  pad_w = PW_NORMAL;
  memset(capsules, 0, sizeof capsules);
  memset(bonus_until, 0, sizeof bonus_until);

  draw_static();
  draw_field();
  update_stats(true);
  update_bonus_status(true);
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
      /* Raket kontrolu */
      if (btn == BTN_LEFT)
        move_paddle(pad_x - PAD_BTN_SPEED * dt);
      else if (btn == BTN_RIGHT)
        move_paddle(pad_x + PAD_BTN_SPEED * dt);
      else if (touch && btn == BTN_NONE && tx >= FX - 10 && tx < FX + FW + 10)
      {
        /* Parmaga dogru sinirli hizla kay, aninda atlama */
        float delta = ((float)tx - pad_w / 2.0f) - pad_x;
        float max_step = PAD_TOUCH_SPEED * dt;
        if (delta > max_step)  delta = max_step;
        if (delta < -max_step) delta = -max_step;
        move_paddle(pad_x + delta);
      }

      update_bonus_timers(now);
      move_capsules(dt);

      if (stuck)
      {
        place_on_paddle();
        if ((int32_t)(now - serve_at) >= 0)
          launch();
      }
      else
      {
        move_ball(dt);
      }

      if (state == ST_PLAYING)
      {
        draw_capsules(false);
        draw_ball();
        draw_paddle();   /* topun silinmesinin raketi cizdigi durumu onarir */
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
    update_bonus_status(false);
    osDelay(FRAME_MS);
  }
}
