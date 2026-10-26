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
  SCORE_SLOTS = 8        /* gelecekteki oyunlar icin yer */
};

/* Flash'tan en son kaydi okur. Menu acilirken bir kez cagrilir. */
void     Scores_Init(void);

uint32_t Scores_Get(int slot);

/* Skor rekor ise Flash'a yazar ve true doner. */
bool     Scores_Submit(int slot, uint32_t score);

#endif /* SCORES_H */
