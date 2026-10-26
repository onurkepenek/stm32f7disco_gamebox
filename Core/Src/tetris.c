/*
 * tetris.c - Dokunmatik Tetris
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

#define COLS    10
#define ROWS    20
#define CELL    13
#define BX      175                     /* tahta sol ust kosesi */
#define BY      6

/* Asagidaki konumlar "kontroller solda" duzeni icin. Kontroller sagdayken
 * (varsayilan) kontrol tuslari ile bilgi paneli yer degistirir; tahta ortada
 * oldugu icin yerinde kalir. Kaydirmalar layout_init()'te hesaplanir. */
static int ctrl_dx, info_dx;

#define PANEL_X0 313
#define PANEL_X (PANEL_X0 + info_dx)
#define PANEL_W 160
#define PANEL_CX (PANEL_X + PANEL_W / 2)

#define SCORE_Y 118
#define LINES_Y 168
#define LEVEL_Y 218

enum { BTN_ROT, BTN_LEFT, BTN_RIGHT, BTN_DOWN, BTN_DROP, BTN_PAUSE, BTN_EXIT,
       BTN_LV_DOWN, BTN_LV_UP, BTN_NEW, BTN_COUNT, BTN_NONE = -1 };

static const Button buttons_base[BTN_COUNT] = {
  [BTN_ROT]   = {  8,   8, 160,  70, 2, "ROTATE" },
  [BTN_LEFT]  = {  8,  86,  76,  80, 4, "<" },
  [BTN_RIGHT] = { 92,  86,  76,  80, 4, ">" },
  [BTN_DOWN]  = {  8, 174,  76,  92, 2, "DOWN" },
  [BTN_DROP]  = { 92, 174,  76,  92, 2, "DROP" },
  [BTN_PAUSE] = { PANEL_X0 + 4,  238, 74, 26, 2, "PAUSE" },
  [BTN_EXIT]  = { PANEL_X0 + 82, 238, 74, 26, 2, "EXIT" },
  /* Baslangic seviyesi secimi, sadece oyun disinda gorunur */
  [BTN_LV_DOWN] = { PANEL_X0 + 6,           LEVEL_Y - 4, 32, 22, 2, "<" },
  [BTN_LV_UP]   = { PANEL_X0 + PANEL_W - 38, LEVEL_Y - 4, 32, 22, 2, ">" },
  /* Sadece duraklatma kutusunda (tahtanin ortasinda) gorunur */
  [BTN_NEW]     = { BX + 20, BY + 80 + 44, 90, 30, 2, "NEW" },
};
static Button buttons[BTN_COUNT];

static bool is_level_button(int i)
{
  return i == BTN_LV_DOWN || i == BTN_LV_UP;
}

static void layout_init(void)
{
  bool right = !Settings_Get(SETTING_CONTROLS_LEFT);
  ctrl_dx = right ? 304 : 0;     /* kontroller 8..168 -> 312..472 */
  info_dx = right ? -306 : 0;    /* bilgi paneli 313..473 -> 7..167 */

  for (int i = 0; i < BTN_COUNT; i++)
  {
    buttons[i] = buttons_base[i];
    if (i <= BTN_DROP)
      buttons[i].x += ctrl_dx;
    else if (i != BTN_NEW)       /* NEW tahtanin ustunde, tahta kaymiyor */
      buttons[i].x += info_dx;
  }
}

static bool paused;
static bool new_best;

static void draw_button(int i, bool active)
{
  gfx_button(&buttons[i], (i == BTN_PAUSE && paused) ? "PLAY" : NULL, active);
}

/* ------------------------------------------------------------------------- */
/* Parcalar                                                                  */
/* ------------------------------------------------------------------------- */

#define NPIECES 7

/* (satir, sutun) - n x n kutu icinde, 0. rotasyon */
static const int8_t piece_base[NPIECES][4][2] = {
  {{1,0},{1,1},{1,2},{1,3}},   /* I */
  {{0,0},{0,1},{1,0},{1,1}},   /* O */
  {{0,1},{1,0},{1,1},{1,2}},   /* T */
  {{0,1},{0,2},{1,0},{1,1}},   /* S */
  {{0,0},{0,1},{1,1},{1,2}},   /* Z */
  {{0,0},{1,0},{1,1},{1,2}},   /* J */
  {{0,2},{1,0},{1,1},{1,2}},   /* L */
};
static const uint8_t piece_box[NPIECES] = { 4, 2, 3, 3, 3, 3, 3 };
static const uint16_t piece_color[NPIECES] = {
  RGB(0, 230, 240),    /* I */
  RGB(250, 220, 0),    /* O */
  RGB(170, 60, 240),   /* T */
  RGB(40, 220, 60),    /* S */
  RGB(240, 40, 50),    /* Z */
  RGB(50, 100, 255),   /* J */
  RGB(255, 145, 0),    /* L */
};

static int8_t shapes[NPIECES][4][4][2];

static void build_shapes(void)
{
  for (int p = 0; p < NPIECES; p++)
  {
    int n = piece_box[p];
    memcpy(shapes[p][0], piece_base[p], sizeof piece_base[p]);
    for (int r = 1; r < 4; r++)
      for (int i = 0; i < 4; i++)
      {
        /* saat yonunde: (satir, sutun) -> (sutun, n-1-satir) */
        shapes[p][r][i][0] = shapes[p][r - 1][i][1];
        shapes[p][r][i][1] = (int8_t)(n - 1 - shapes[p][r - 1][i][0]);
      }
  }
}

/* ------------------------------------------------------------------------- */
/* Oyun durumu                                                               */
/* ------------------------------------------------------------------------- */

enum { ST_TITLE, ST_PLAYING, ST_OVER };

static uint16_t board[ROWS][COLS];
static int state;
static int cur, rot, px, py, next;
static uint32_t score, lines, level;
static uint32_t start_level;          /* 0 tabanli; ekranda +1 gosterilir */
static uint32_t last_fall;

#define MAX_START_LEVEL 14            /* ekranda 15 */

static uint8_t bag[NPIECES];
static int bag_pos = NPIECES;

/* 7-bag: her 7 parcada her sekilden bir tane */
static int bag_next(void)
{
  if (bag_pos >= NPIECES)
  {
    for (int i = 0; i < NPIECES; i++)
      bag[i] = (uint8_t)i;
    for (int i = NPIECES - 1; i > 0; i--)
    {
      int j = (int)(rand_next() % (uint32_t)(i + 1));
      uint8_t t = bag[i]; bag[i] = bag[j]; bag[j] = t;
    }
    bag_pos = 0;
  }
  return bag[bag_pos++];
}

static bool collides(int p, int r, int x, int y)
{
  for (int i = 0; i < 4; i++)
  {
    int c = x + shapes[p][r][i][1];
    int rr = y + shapes[p][r][i][0];
    if (c < 0 || c >= COLS || rr >= ROWS)
      return true;
    if (rr >= 0 && board[rr][c])
      return true;
  }
  return false;
}

static uint32_t fall_interval(void)
{
  static const uint16_t ms[] = { 800, 717, 633, 550, 467, 383, 300, 217, 133, 100, 83, 83, 83, 67, 67, 67, 50 };
  uint32_t n = sizeof ms / sizeof ms[0];
  return ms[level < n ? level : n - 1];
}

/* ------------------------------------------------------------------------- */
/* Cizim                                                                     */
/* ------------------------------------------------------------------------- */

#define V_SOLID   (1UL << 16)
#define V_GHOST   (1UL << 17)
#define V_INVALID 0xFFFFFFFFUL

static uint32_t shown[ROWS][COLS];

static void draw_cell(int x, int y, uint32_t v)
{
  uint16_t c = (uint16_t)(v & 0xFFFF);

  if (v & V_SOLID)
  {
    gfx_block(x, y, CELL, c);
    return;
  }

  gfx_fill(x, y, CELL, CELL, COL_BOARD);
  gfx_fill(x + CELL - 1, y, 1, CELL, COL_GRID);
  gfx_fill(x, y + CELL - 1, CELL, 1, COL_GRID);
  if (v & V_GHOST)
    gfx_outline(x + 1, y + 1, CELL - 3, CELL - 3, gfx_mix(c, 0x0000, 90));
}

static void invalidate_board(void)
{
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      shown[r][c] = V_INVALID;
}

/* Sadece degisen hucreleri cizer, boylece titreme olmaz */
static void render_board(void)
{
  static uint32_t target[ROWS][COLS];

  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      target[r][c] = board[r][c] ? (V_SOLID | board[r][c]) : 0;

  if (state == ST_PLAYING)
  {
    int gy = py;
    while (!collides(cur, rot, px, gy + 1))
      gy++;
    for (int i = 0; i < 4; i++)
    {
      int r = gy + shapes[cur][rot][i][0], c = px + shapes[cur][rot][i][1];
      if (r >= 0 && !target[r][c])
        target[r][c] = V_GHOST | piece_color[cur];
    }
    for (int i = 0; i < 4; i++)
    {
      int r = py + shapes[cur][rot][i][0], c = px + shapes[cur][rot][i][1];
      if (r >= 0)
        target[r][c] = V_SOLID | piece_color[cur];
    }
  }

  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (target[r][c] != shown[r][c])
      {
        draw_cell(BX + c * CELL, BY + r * CELL, target[r][c]);
        shown[r][c] = target[r][c];
      }
}

#define NEXT_X  (PANEL_CX - 40)
#define NEXT_Y  28
#define NEXT_W  80
#define NEXT_H  56

static void draw_next(void)
{
  gfx_fill(NEXT_X, NEXT_Y, NEXT_W, NEXT_H, COL_BOARD);
  gfx_outline(NEXT_X, NEXT_Y, NEXT_W, NEXT_H, COL_GRID);
  if (state != ST_PLAYING)
    return;

  int minr = 9, maxr = -1, minc = 9, maxc = -1;
  for (int i = 0; i < 4; i++)
  {
    int r = shapes[next][0][i][0], c = shapes[next][0][i][1];
    if (r < minr) minr = r;
    if (r > maxr) maxr = r;
    if (c < minc) minc = c;
    if (c > maxc) maxc = c;
  }
  int ox = NEXT_X + (NEXT_W - (maxc - minc + 1) * CELL) / 2;
  int oy = NEXT_Y + (NEXT_H - (maxr - minr + 1) * CELL) / 2;
  for (int i = 0; i < 4; i++)
    gfx_block(ox + (shapes[next][0][i][1] - minc) * CELL,
              oy + (shapes[next][0][i][0] - minr) * CELL,
              CELL, piece_color[next]);
}

/* clear_w: silinecek alan genisligi (LEVEL'da yandaki butonlara dokunmamak icin dar) */
static void draw_value(int y, uint32_t v, int clear_w)
{
  char buf[12];
  snprintf(buf, sizeof buf, "%lu", (unsigned long)v);
  gfx_fill(PANEL_CX - clear_w / 2, y, clear_w, 14, COL_PANEL);
  gfx_text_centered(PANEL_CX, y, buf, 2, COL_TEXT);
}

static void show_level_buttons(bool visible)
{
  for (int i = 0; i < BTN_COUNT; i++)
  {
    if (!is_level_button(i))
      continue;
    if (visible)
      draw_button(i, false);
    else
      gfx_fill(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, COL_PANEL);
  }
}

static void update_stats(bool force)
{
  static uint32_t s = UINT32_MAX, l = UINT32_MAX, lv = UINT32_MAX;
  static int n = -1;

  if (force) { s = l = lv = UINT32_MAX; n = -1; }
  if (score != s)  { draw_value(SCORE_Y, score, PANEL_W - 4); s = score; }
  if (lines != l)  { draw_value(LINES_Y, lines, PANEL_W - 4); l = lines; }
  if (level != lv) { draw_value(LEVEL_Y, level + 1, 76); lv = level; }
  if (next != n)   { draw_next(); n = next; }
}

static void draw_static(void)
{
  gfx_fill(0, 0, LCD_W, LCD_H, COL_BG);

  gfx_outline(BX - 2, BY - 2, COLS * CELL + 4, ROWS * CELL + 4, COL_BORDER);
  gfx_outline(BX - 1, BY - 1, COLS * CELL + 2, ROWS * CELL + 2, COL_BORDER);

  gfx_panel(PANEL_X, 4, PANEL_W, 264);
  gfx_text_centered(PANEL_CX, 10, "NEXT", 2, COL_LABEL);
  gfx_text_centered(PANEL_CX, SCORE_Y - 20, "SCORE", 2, COL_LABEL);
  gfx_text_centered(PANEL_CX, LINES_Y - 20, "LINES", 2, COL_LABEL);
  gfx_text_centered(PANEL_CX, LEVEL_Y - 20, "LEVEL", 2, COL_LABEL);

  for (int i = 0; i < BTN_COUNT; i++)
    if (i != BTN_NEW)
      draw_button(i, false);

  invalidate_board();
}

enum { OV_TITLE, OV_OVER, OV_PAUSED };

static void draw_overlay(int kind)
{
  int x = BX + 5, y = BY + 80, w = COLS * CELL - 10, h = 100;
  int cx = BX + COLS * CELL / 2;

  char buf[20];

  gfx_panel(x, y, w, h);
  snprintf(buf, sizeof buf, "BEST %lu", (unsigned long)Scores_Get(SCORE_TETRIS));

  switch (kind)
  {
    case OV_OVER:
      gfx_text_centered(cx, y + 8, "GAME", 3, COL_TITLE);
      gfx_text_centered(cx, y + 34, "OVER", 3, COL_TITLE);
      if (new_best)
        gfx_text_centered(cx, y + 64, "NEW BEST", 1, COL_TITLE);
      else
        gfx_text_centered(cx, y + 64, buf, 1, COL_LABEL);
      gfx_text_centered(cx, y + 82, "TAP TO RETRY", 1, COL_LABEL);
      break;
    case OV_PAUSED:
      gfx_text_centered(cx, y + 10, "PAUSED", 3, COL_TITLE);
      draw_button(BTN_NEW, false);
      gfx_text_centered(cx, y + 84, "PRESS PLAY", 1, COL_LABEL);
      break;
    default:
      gfx_text_centered(cx, y + 10, "TETRIS", 3, COL_TITLE);
      gfx_text_centered(cx, y + 38, buf, 1, COL_LABEL);
      gfx_text_centered(cx, y + 56, "TAP TO", 2, COL_TEXT);
      gfx_text_centered(cx, y + 76, "START", 2, COL_TEXT);
      break;
  }

  /* Overlay'in kapattigi hucreler bir sonraki render'da yeniden cizilsin */
  invalidate_board();
}

/* ------------------------------------------------------------------------- */
/* Oyun mantigi                                                              */
/* ------------------------------------------------------------------------- */

static void game_over(void)
{
  state = ST_OVER;
  new_best = Scores_Submit(SCORE_TETRIS, score);
  render_board();
  draw_overlay(OV_OVER);
  show_level_buttons(true);
}

static void spawn(void)
{
  cur = next;
  next = bag_next();
  rot = 0;
  px = (COLS - piece_box[cur]) / 2;
  py = (cur == 0) ? -1 : 0;   /* I parcasi kutusunun 2. satirinda */
  last_fall = HAL_GetTick();

  if (collides(cur, rot, px, py))
    game_over();
}

static void start_game(void)
{
  memset(board, 0, sizeof board);
  score = lines = 0;
  level = start_level;
  bag_pos = NPIECES;
  state = ST_PLAYING;
  show_level_buttons(false);
  next = bag_next();
  spawn();
  invalidate_board();
}

static int clear_lines(void)
{
  bool full[ROWS];
  int n = 0;

  for (int r = 0; r < ROWS; r++)
  {
    full[r] = true;
    for (int c = 0; c < COLS && full[r]; c++)
      full[r] = board[r][c] != 0;
    n += full[r];
  }
  if (n == 0)
    return 0;

  /* Kisa beyaz flas */
  for (int r = 0; r < ROWS; r++)
    if (full[r])
      for (int c = 0; c < COLS; c++)
      {
        draw_cell(BX + c * CELL, BY + r * CELL, V_SOLID | COL_TEXT);
        shown[r][c] = V_INVALID;
      }
  osDelay(150);

  int dst = ROWS - 1;
  for (int src = ROWS - 1; src >= 0; src--)
    if (!full[src])
    {
      if (dst != src)
        memcpy(board[dst], board[src], sizeof board[0]);
      dst--;
    }
  for (; dst >= 0; dst--)
    memset(board[dst], 0, sizeof board[0]);

  return n;
}

static void lock_piece(void)
{
  static const uint16_t points[5] = { 0, 100, 300, 500, 800 };

  for (int i = 0; i < 4; i++)
  {
    int r = py + shapes[cur][rot][i][0], c = px + shapes[cur][rot][i][1];
    if (r < 0)
    {
      game_over();
      return;
    }
    board[r][c] = piece_color[cur];
  }

  render_board();
  int n = clear_lines();
  score += points[n] * (level + 1);
  lines += (uint32_t)n;
  level = start_level + lines / 10;
  spawn();
}

static bool try_move(int dx, int dy)
{
  if (collides(cur, rot, px + dx, py + dy))
    return false;
  px += dx;
  py += dy;
  return true;
}

static void try_rotate(void)
{
  /* Basit duvar tekmeleri: once yerinde, sonra yanlara, en son yukari */
  static const int8_t kicks[][2] = { {0,0}, {-1,0}, {1,0}, {-2,0}, {2,0}, {0,-1} };
  int nr = (rot + 1) & 3;

  for (unsigned i = 0; i < sizeof kicks / sizeof kicks[0]; i++)
    if (!collides(cur, nr, px + kicks[i][0], py + kicks[i][1]))
    {
      rot = nr;
      px += kicks[i][0];
      py += kicks[i][1];
      return;
    }
}

static void hard_drop(void)
{
  while (try_move(0, 1))
    score += 2;
  lock_piece();
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
    invalidate_board();
    last_fall = HAL_GetTick();  /* devam edince parca hemen dusmesin */
  }
}

static void change_start_level(int delta)
{
  if (delta < 0 && start_level > 0)
    start_level--;
  else if (delta > 0 && start_level < MAX_START_LEVEL)
    start_level++;
  level = start_level;
}

/* ------------------------------------------------------------------------- */
/* Disariya acik fonksiyonlar                                                */
/* ------------------------------------------------------------------------- */

void Tetris_DrawIcon(int cx, int cy)
{
  const int s = 12;
  /* Ustte T, altta yatay I */
  gfx_block(cx - 18, cy - 14, s, piece_color[2]);
  gfx_block(cx - 6,  cy - 14, s, piece_color[2]);
  gfx_block(cx + 6,  cy - 14, s, piece_color[2]);
  gfx_block(cx - 6,  cy - 2,  s, piece_color[2]);
  for (int i = 0; i < 4; i++)
    gfx_block(cx - 24 + i * s, cy + 10, s, piece_color[0]);
}

#define FRAME_MS       10
#define DAS_DELAY_MS   180   /* sola/saga basili tutunca tekrar baslama suresi */
#define DAS_REPEAT_MS  60
#define SOFT_DROP_MS   35

void Tetris_Run(void)
{
  int prev_btn = BTN_NONE;
  bool prev_touch = false;
  uint32_t repeat_at = 0;

  layout_init();
  build_shapes();

  if (state == ST_PLAYING)
  {
    /* Yarida birakilan oyun: oldugu gibi ciz, duraklatilmis olarak goster */
    paused = true;
    draw_static();
    show_level_buttons(false);
    render_board();
    update_stats(true);
    draw_overlay(OV_PAUSED);
  }
  else
  {
    memset(board, 0, sizeof board);
    paused = false;
    state = ST_TITLE;
    score = lines = 0;
    level = start_level;

    draw_static();
    render_board();
    update_stats(true);
    draw_overlay(OV_TITLE);
  }

  for (;;)
  {
    int tx = 0, ty = 0;
    bool touch = Touch_Read(&tx, &ty);
    int btn = touch ? gfx_hit(buttons, BTN_COUNT, tx, ty) : BTN_NONE;
    uint32_t now = HAL_GetTick();

    if (state == ST_PLAYING && is_level_button(btn))
      btn = BTN_NONE;   /* oyun sirasinda gizliler */
    if (btn == BTN_NEW && !(state == ST_PLAYING && paused))
      btn = BTN_NONE;   /* sadece duraklatma kutusunda */
    bool pressed = (btn != prev_btn);

    if (pressed && btn == BTN_EXIT)
    {
      if (state == ST_PLAYING)
        paused = true;  /* oyun saklanir, donunce kaldigi yerden */
      return;           /* ana menuye don */
    }
    else if (pressed && btn == BTN_NEW)
    {
      paused = false;
      draw_button(BTN_PAUSE, false);
      start_game();
    }
    else if (pressed && btn == BTN_PAUSE && state == ST_PLAYING)
    {
      set_paused(!paused);
    }
    else if (pressed && is_level_button(btn))
    {
      change_start_level(btn == BTN_LV_UP ? 1 : -1);
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
        repeat_at = now + DAS_DELAY_MS;
        switch (btn)
        {
          case BTN_ROT:   try_rotate();      break;
          case BTN_LEFT:  try_move(-1, 0);   break;
          case BTN_RIGHT: try_move(1, 0);    break;
          case BTN_DROP:  hard_drop();       break;
          default: break;
        }
      }
      else if ((btn == BTN_LEFT || btn == BTN_RIGHT) && (int32_t)(now - repeat_at) >= 0)
      {
        try_move(btn == BTN_LEFT ? -1 : 1, 0);
        repeat_at = now + DAS_REPEAT_MS;
      }

      if (state == ST_PLAYING)
      {
        bool soft = (btn == BTN_DOWN);
        if (now - last_fall >= (soft ? SOFT_DROP_MS : fall_interval()))
        {
          last_fall = now;
          if (try_move(0, 1))
            score += soft ? 1 : 0;
          else
            lock_piece();
        }
      }
    }

    /* NEW tahtanin ustunde: yeni oyun basladiktan sonra yeniden cizilmesin */
    if (pressed)
    {
      if (prev_btn != BTN_NONE && prev_btn != BTN_NEW) draw_button(prev_btn, false);
      if (btn != BTN_NONE && btn != BTN_NEW)           draw_button(btn, true);
    }
    prev_btn = btn;
    prev_touch = touch;

    if (state == ST_PLAYING && !paused)
      render_board();
    update_stats(false);

    osDelay(FRAME_MS);
  }
}
