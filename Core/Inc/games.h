#ifndef GAMES_H
#define GAMES_H

#include <stdint.h>

/*
 * Yeni oyun eklemek icin:
 *  1. Core/Src/<oyun>.c icinde XXX_Run() ve XXX_DrawIcon() yaz.
 *     XXX_Run(), oyuncu EXIT'e basinca geri donmeli.
 *  2. Asagiya prototiplerini ekle.
 *  3. scores.h'deki listenin sonuna bir SCORE_XXX yuvasi ekle.
 *  4. menu.c'deki games[] listesine bir satir ekle.
 */
typedef struct
{
  const char *name;
  uint16_t    color;                      /* menu kartindaki vurgu rengi */
  void      (*draw_icon)(int cx, int cy); /* yaklasik 60x40 alana sigmali */
  void      (*run)(void);
  int         score_slot;                 /* scores.h'deki SCORE_xxx */
} Game;

/* Ana menu. Bir FreeRTOS task'i icinden cagrilir, geri donmez. */
void Menu_Run(void);

void Tetris_Run(void);
void Tetris_DrawIcon(int cx, int cy);

void Snake_Run(void);
void Snake_DrawIcon(int cx, int cy);

void Bricks_Run(void);
void Bricks_DrawIcon(int cx, int cy);

void Flappy_Run(void);
void Flappy_DrawIcon(int cx, int cy);

void Game2048_Run(void);
void Game2048_DrawIcon(int cx, int cy);

void Mines_Run(void);
void Mines_DrawIcon(int cx, int cy);

void Balloons_Run(void);
void Balloons_DrawIcon(int cx, int cy);

void Paint_Run(void);
void Paint_DrawIcon(int cx, int cy);

void Memory_Run(void);
void Memory_DrawIcon(int cx, int cy);

#endif /* GAMES_H */
