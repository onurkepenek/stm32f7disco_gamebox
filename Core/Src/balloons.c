/*
 * balloons.c - Balon Patlatma (kucuk cocuklar icin)
 *
 * Balonlar asagidan yukari suzulur, dokununca konfetiyle patlar.
 * Kaybetmek yok; patlatilan her balon ustteki yildiz sayacini artirir.
 * Cikis icin EXIT basili tutulur (kazara cikilmasin).
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

#define TOP         36                  /* ustte sayac cubugu, altinda gokyuzu */
#define LANES       6                   /* balonlar seritlerde: ust uste binmesinler */
#define LANE_W      (LCD_W / LANES)
#define MAX_BURSTS  4

#define KNOT_H      3
#define STRING_H    20
#define SWAY        5                   /* saga sola salinim, px */
#define SWAY_MS     2400

#define COL_STRING  RGB(230, 230, 240)
#define COL_STAR    RGB(255, 210, 40)
#define COL_BAR     RGB(20, 40, 90)

static const uint16_t balloon_colors[] = {
  RGB(240, 50, 60), RGB(255, 145, 0), RGB(250, 220, 0), RGB(50, 200, 70),
  RGB(40, 170, 255), RGB(160, 80, 240), RGB(255, 100, 180),
};
#define NCOLORS ((int)(sizeof balloon_colors / sizeof balloon_colors[0]))

static HoldButton exit_btn = { { LCD_W - 80, 4, 76, 28, 2, "EXIT" }, 1500, 0, 0 };

/* ------------------------------------------------------------------------- */
/* Durum                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct
{
  bool     active;
  float    cy;                /* govdenin merkezi */
  float    speed;             /* px/ms */
  int      rx, ry;
  uint16_t color;
  uint32_t phase;
  int      drawn_cx, drawn_cy;  /* drawn_cy == INT32_MIN: cizilmedi */
} Balloon;

typedef struct
{
  bool     active;
  int      cx, cy;
  uint16_t color;
  uint32_t start;
  int      drawn_r;           /* -1: cizilmedi */
} Burst;

static Balloon balloons[LANES];
static Burst bursts[MAX_BURSTS];
static uint16_t sky[LCD_H];
static uint32_t pops, next_spawn;

/* ------------------------------------------------------------------------- */
/* Cizim                                                                     */
/* ------------------------------------------------------------------------- */

static void build_sky(void)
{
  for (int y = 0; y < LCD_H; y++)
  {
    int a = (y - TOP) * 256 / (LCD_H - TOP);
    if (a < 0) a = 0;
    sky[y] = gfx_mix(RGB(70, 150, 235), RGB(190, 230, 255), a);
  }
}

/* Gokyuzu rengiyle (satira gore degisen) doldur */
static void fill_sky(int x, int y, int w, int h)
{
  if (x < 0) { w += x; x = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  for (int j = 0; j < h; j++)
    if (y + j >= TOP && y + j < LCD_H && w > 0)
      gfx_fill(x, y + j, w, 1, sky[y + j]);
}

/* Balon seklinin y satirindaki yatay parcasi */
static bool balloon_span(const Balloon *b, int cx, int cy, int y, int *x0, int *x1, uint16_t *col)
{
  int dy = y - cy;

  if (dy >= -b->ry && dy <= b->ry)                         /* govde (elips) */
  {
    int half = b->rx * gfx_isqrt(b->ry * b->ry - dy * dy) / b->ry;
    *x0 = cx - half;
    *x1 = cx + half;
    *col = b->color;
    return true;
  }
  dy -= b->ry;
  if (dy >= 1 && dy <= KNOT_H)                              /* dugum */
  {
    *x0 = cx - (KNOT_H - dy + 1);
    *x1 = cx + (KNOT_H - dy + 1);
    *col = b->color;
    return true;
  }
  if (dy > KNOT_H && dy <= KNOT_H + STRING_H)               /* ip */
  {
    *x0 = *x1 = cx + ((dy / 6) & 1);                        /* hafif kivrim */
    *col = COL_STRING;
    return true;
  }
  return false;
}

static int balloon_top(const Balloon *b, int cy)    { return cy - b->ry; }
static int balloon_bottom(const Balloon *b, int cy) { return cy + b->ry + KNOT_H + STRING_H; }

/* Balonu yeni konumuna tasir: yeni parcayi ciz, eskisinden aciga cikani sil */
static void draw_balloon(Balloon *b, int cx, int cy)
{
  bool had = b->drawn_cy != INT32_MIN;
  int y0 = balloon_top(b, cy), y1 = balloon_bottom(b, cy);

  if (had)
  {
    if (balloon_top(b, b->drawn_cy) < y0)    y0 = balloon_top(b, b->drawn_cy);
    if (balloon_bottom(b, b->drawn_cy) > y1) y1 = balloon_bottom(b, b->drawn_cy);
  }
  if (y0 < TOP) y0 = TOP;
  if (y1 > LCD_H - 1) y1 = LCD_H - 1;

  for (int y = y0; y <= y1; y++)
  {
    int nx0, nx1, ox0, ox1;
    uint16_t ncol, ocol;
    bool has_new = balloon_span(b, cx, cy, y, &nx0, &nx1, &ncol);
    bool has_old = had && balloon_span(b, b->drawn_cx, b->drawn_cy, y, &ox0, &ox1, &ocol);

    if (has_new)
      gfx_fill(nx0, y, nx1 - nx0 + 1, 1, ncol);
    if (has_old)
    {
      if (!has_new)
      {
        fill_sky(ox0, y, ox1 - ox0 + 1, 1);
      }
      else
      {
        if (ox0 < nx0) fill_sky(ox0, y, (ox1 < nx0 ? ox1 + 1 : nx0) - ox0, 1);
        if (ox1 > nx1) { int s = ox0 > nx1 ? ox0 : nx1 + 1; fill_sky(s, y, ox1 - s + 1, 1); }
      }
    }
  }

  /* Parlama */
  int hx = cx - b->rx / 2, hy = cy - b->ry / 2;
  if (hy - 3 >= TOP && hy + 4 < LCD_H)
  {
    uint16_t hl = gfx_mix(b->color, 0xFFFF, 150);
    gfx_fill(hx - 1, hy - 3, 3, 8, hl);
    gfx_fill(hx - 2, hy - 1, 5, 4, hl);
  }

  b->drawn_cx = cx;
  b->drawn_cy = cy;
}

static void erase_balloon(Balloon *b)
{
  if (b->drawn_cy == INT32_MIN)
    return;
  for (int y = balloon_top(b, b->drawn_cy); y <= balloon_bottom(b, b->drawn_cy); y++)
  {
    int x0, x1;
    uint16_t col;
    if (y >= TOP && y < LCD_H && balloon_span(b, b->drawn_cx, b->drawn_cy, y, &x0, &x1, &col))
      fill_sky(x0, y, x1 - x0 + 1, 1);
  }
  b->drawn_cy = INT32_MIN;
}

static void draw_counter(void)
{
  char buf[12];
  snprintf(buf, sizeof buf, "%lu", (unsigned long)pops);
  gfx_fill(36, 6, 200, 24, COL_BAR);
  gfx_text(40, 8, buf, 3, COL_TEXT);
}

static void draw_static(void)
{
  gfx_fill(0, 0, LCD_W, TOP, COL_BAR);
  for (int y = TOP; y < LCD_H; y++)
    gfx_fill(0, y, LCD_W, 1, sky[y]);
  gfx_star(6, 7, 2, COL_STAR);
  draw_counter();
  gfx_hold_draw(&exit_btn);
}

/* ------------------------------------------------------------------------- */
/* Patlama efekti                                                            */
/* ------------------------------------------------------------------------- */

#define BURST_MS     350
#define BURST_MAX_R  32
#define NPARTS       12

/* 12 yon icin cos/sin * 256 */
static const int16_t dir_x[NPARTS] = { 256, 222, 128, 0, -128, -222, -256, -222, -128, 0, 128, 222 };
static const int16_t dir_y[NPARTS] = { 0, 128, 222, 256, 222, 128, 0, -128, -222, -256, -222, -128 };

static void draw_particles(const Burst *p, int r, bool erase)
{
  for (int i = 0; i < NPARTS; i++)
  {
    int x = p->cx + dir_x[i] * r / 256 - 2;
    int y = p->cy + dir_y[i] * r / 256 - 2;
    if (erase)
      fill_sky(x, y, 5, 5);
    else if (y >= TOP)
      gfx_fill(x, y, 5, 5, (i & 1) ? p->color : COL_STAR);
  }
}

static void start_burst(int cx, int cy, uint16_t color)
{
  for (int i = 0; i < MAX_BURSTS; i++)
    if (!bursts[i].active)
    {
      bursts[i] = (Burst){ true, cx, cy, color, HAL_GetTick(), -1 };
      return;
    }
}

static void update_bursts(uint32_t now)
{
  for (int i = 0; i < MAX_BURSTS; i++)
  {
    Burst *p = &bursts[i];
    if (!p->active)
      continue;

    uint32_t t = now - p->start;
    int r = (int)(t * BURST_MAX_R / BURST_MS);
    if (t >= BURST_MS)
    {
      if (p->drawn_r >= 0)
        draw_particles(p, p->drawn_r, true);
      p->active = false;
      continue;
    }
    if (p->drawn_r >= 0 && p->drawn_r != r)
      draw_particles(p, p->drawn_r, true);
    if (p->drawn_r != r)
    {
      draw_particles(p, r, false);
      p->drawn_r = r;
    }
  }
}

/* ------------------------------------------------------------------------- */
/* Oyun mantigi                                                              */
/* ------------------------------------------------------------------------- */

static int lane_center(int lane) { return lane * LANE_W + LANE_W / 2; }

/* Ucgen dalga ile saga sola salinim */
static int sway(const Balloon *b, uint32_t now)
{
  uint32_t t = (now + b->phase) % SWAY_MS;
  int v = (int)(t < SWAY_MS / 2 ? t : SWAY_MS - t);
  return v * 2 * SWAY / (SWAY_MS / 2) - SWAY;
}

static void spawn_balloon(uint32_t now)
{
  int free_lanes[LANES], n = 0;
  for (int i = 0; i < LANES; i++)
    if (!balloons[i].active)
      free_lanes[n++] = i;
  if (!n)
    return;

  Balloon *b = &balloons[free_lanes[rand_next() % (uint32_t)n]];
  b->active = true;
  b->rx = 22 + (int)(rand_next() % 7);
  b->ry = b->rx * 6 / 5;
  b->cy = (float)(LCD_H + b->ry);
  b->speed = 0.030f + (float)(rand_next() % 25) * 0.001f;
  b->color = balloon_colors[rand_next() % NCOLORS];
  b->phase = rand_next() % SWAY_MS;
  b->drawn_cy = INT32_MIN;
  (void)now;
}

static void update_balloons(float dt, uint32_t now)
{
  for (int i = 0; i < LANES; i++)
  {
    Balloon *b = &balloons[i];
    if (!b->active)
      continue;

    b->cy -= b->speed * dt;
    int cy = (int)b->cy;
    int cx = lane_center(i) + sway(b, now);

    if (balloon_bottom(b, cy) < TOP)
    {
      erase_balloon(b);     /* ucup gitti, ceza yok */
      b->active = false;
      continue;
    }
    if (cx != b->drawn_cx || cy != b->drawn_cy)
      draw_balloon(b, cx, cy);
  }
}

/* Dokunulan balonu patlat. Kucuk parmaklar icin genis dokunma alani. */
static void try_pop(int tx, int ty)
{
  for (int i = 0; i < LANES; i++)
  {
    Balloon *b = &balloons[i];
    if (!b->active || b->drawn_cy == INT32_MIN)
      continue;

    int dx = tx - b->drawn_cx, dy = ty - b->drawn_cy;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx <= b->rx + 12 && dy <= b->ry + 12)
    {
      int cx = b->drawn_cx, cy = b->drawn_cy;
      erase_balloon(b);
      b->active = false;
      start_burst(cx, cy, b->color);
      pops++;
      draw_counter();
      return;
    }
  }
}

/* ------------------------------------------------------------------------- */
/* Disariya acik fonksiyonlar                                                */
/* ------------------------------------------------------------------------- */

static void icon_balloon(int cx, int cy, int rx, uint16_t c)
{
  int ry = rx * 6 / 5;
  for (int dy = -ry; dy <= ry; dy++)
  {
    int half = rx * gfx_isqrt(ry * ry - dy * dy) / ry;
    gfx_fill(cx - half, cy + dy, 2 * half + 1, 1, c);
  }
  gfx_fill(cx - 1, cy + ry + 1, 3, 2, c);
  gfx_fill(cx, cy + ry + 3, 1, 8, COL_STRING);
  gfx_fill(cx - rx / 2, cy - ry / 2, 2, 4, gfx_mix(c, 0xFFFF, 150));
}

void Balloons_DrawIcon(int cx, int cy)
{
  icon_balloon(cx - 22, cy - 2, 10, balloon_colors[0]);
  icon_balloon(cx + 22, cy - 4, 10, balloon_colors[4]);
  icon_balloon(cx, cy - 10, 11, balloon_colors[2]);
}

#define FRAME_MS 10

void Balloons_Run(void)
{
  bool prev_touch = false;
  uint32_t last_frame = HAL_GetTick();

  memset(balloons, 0, sizeof balloons);
  memset(bursts, 0, sizeof bursts);
  for (int i = 0; i < LANES; i++)
    balloons[i].drawn_cy = INT32_MIN;
  pops = 0;
  next_spawn = last_frame;

  build_sky();
  draw_static();

  for (;;)
  {
    int tx = 0, ty = 0;
    bool touch = Touch_Read(&tx, &ty);
    uint32_t now = HAL_GetTick();
    float dt = (float)(now - last_frame);
    last_frame = now;
    if (dt > 30.0f)
      dt = 30.0f;

    bool on_exit = touch && gfx_hit(&exit_btn.btn, 1, tx, ty) == 0;
    if (gfx_hold_update(&exit_btn, on_exit, now))
    {
      Scores_Submit(SCORE_BALLOONS, pops);
      return;               /* ana menuye don */
    }

    /* Her yeni dokunus bir patlatma denemesi */
    if (touch && !prev_touch && !on_exit && ty >= TOP - 10)
      try_pop(tx, ty);

    if ((int32_t)(now - next_spawn) >= 0)
    {
      spawn_balloon(now);
      next_spawn = now + 600 + rand_next() % 700;
    }

    update_balloons(dt, now);
    update_bursts(now);

    prev_touch = touch;
    osDelay(FRAME_MS);
  }
}
