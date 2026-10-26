#ifndef SCORES_H
#define SCORES_H

#include <stdint.h>
#include <stdbool.h>

/* Her oyunun kalici en yuksek skor yuvasi. Yeni oyun icin sona ekle
 * (sirayi degistirme: Flash'taki eski kayitlar bu indekslerle okunur). */
enum
{
  SCORE_TETRIS,
  SCORE_SNAKE,
  SCORE_BRICKS,
  SCORE_FLAPPY,
  SCORE_2048,
  SCORE_MINES,
  SCORE_BALLOONS,
  SETTING_FLAGS = 7,      /* skor degil: ayar bitleri (SETTING_xxx) */
  SCORE_NONE = -1,        /* skoru olmayan oyunlar (boyama gibi) */
  SCORE_SLOTS = 8      /* gelecekteki oyunlar icin yer */
};

/* Flash'tan en son kaydi okur. Menu acilirken bir kez cagrilir. */
void     Scores_Init(void);

uint32_t Scores_Get(int slot);

/* Skor rekor ise Flash'a yazar ve true doner. */
bool     Scores_Submit(int slot, uint32_t score);

/* Degeri kosulsuz yazar (ayarlar icin). Degismediyse Flash'a dokunmaz. */
void     Scores_Set(int slot, uint32_t value);

/* Kalici ayarlar (SETTING_FLAGS yuvasinda bit olarak). Varsayilan: hepsi 0. */
#define SETTING_ROTATED        (1u << 0)   /* ekran 180 derece donuk */
#define SETTING_CONTROLS_LEFT  (1u << 1)   /* oyun kontrolleri solda (varsayilan sag) */

bool     Settings_Get(uint32_t flag);
void     Settings_Set(uint32_t flag, bool on);

#endif /* SCORES_H */
