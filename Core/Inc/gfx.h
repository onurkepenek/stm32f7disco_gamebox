#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include <stdbool.h>

#define LCD_W  480
#define LCD_H  272

#define RGB(r, g, b)  ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* Ortak renk paleti */
#define COL_BG          RGB(10, 12, 24)
#define COL_PANEL       RGB(24, 28, 50)
#define COL_BORDER      RGB(90, 100, 170)
#define COL_BOARD       RGB(6, 8, 16)
#define COL_GRID        RGB(20, 24, 42)
#define COL_TEXT        RGB(255, 255, 255)
#define COL_LABEL       RGB(150, 160, 215)
#define COL_BTN         RGB(38, 44, 80)
#define COL_BTN_ACTIVE  RGB(85, 95, 165)
#define COL_TITLE       RGB(255, 200, 40)

/* Font: 0-9, A-Z, '<' '>' '/' '+' */

enum { ARROW_UP, ARROW_RIGHT, ARROW_DOWN, ARROW_LEFT };

/* Ekrani 180 derece dondur (cizim ve dokunmatik birlikte) */
void     gfx_set_rotation(bool on);
bool     gfx_get_rotation(void);

void     gfx_fill(int x, int y, int w, int h, uint16_t c);
uint16_t gfx_get_pixel(int x, int y);
void     gfx_put_pixel(int x, int y, uint16_t c);
void     gfx_outline(int x, int y, int w, int h, uint16_t c);
void     gfx_panel(int x, int y, int w, int h);
void     gfx_block(int x, int y, int size, uint16_t c);   /* kabartmali kare */
uint16_t gfx_mix(uint16_t c, uint16_t target, int amount); /* amount: 0..256 */
/* (cx, cy) merkezli dolu ucgen ok; size: ucun tabana uzakligi */
void     gfx_arrow(int cx, int cy, int size, int dir, uint16_t c);

int  gfx_text_width(const char *s, int scale);
void gfx_text(int x, int y, const char *s, int scale, uint16_t c);
void gfx_text_centered(int cx, int y, const char *s, int scale, uint16_t c);

typedef struct { int x, y, w, h, scale; const char *label; } Button;

/* label NULL ise b->label kullanilir */
void gfx_button(const Button *b, const char *label, bool active);
/* Dokunulan butonun indeksi, yoksa -1 */
int  gfx_hit(const Button *buttons, int count, int x, int y);

/* Basili tutulunca calisan buton (kucuk cocuklar kazara basmasin diye).
 * Tutarken alt kenarda bir dolum cubugu ilerler. */
typedef struct
{
  Button   btn;
  uint32_t hold_ms;
  uint32_t start;      /* 0: basili degil */
  int      shown;      /* cizilmis dolum genisligi */
  /* Istege bagli: butonun uzerine simge cizer (etiket yerine) */
  void   (*decorate)(const Button *b, bool active);
} HoldButton;

void gfx_hold_draw(HoldButton *h);
/* Her karede cagrilir; pressed: parmak butonun ustunde. Sure dolunca true. */
bool gfx_hold_update(HoldButton *h, bool pressed, uint32_t now);

int  gfx_isqrt(int v);

/* Sekiller (cocuk oyunlari icin) */
void gfx_circle(int cx, int cy, int r, uint16_t c);
void gfx_star(int x, int y, int scale, uint16_t c);   /* 11x11 desen, sol ust kose */

void     rand_seed(uint32_t s);
uint32_t rand_next(void);

#endif /* GFX_H */
