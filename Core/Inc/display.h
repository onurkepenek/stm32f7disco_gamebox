#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>

#define FB_ADDR  0xC0000000UL   /* LTDC Layer0 framebuffer (SDRAM) */

/* FMC registerlarina ve SDRAM'e erisim izni. MPU_Config()'ten hemen sonra,
 * MX_FMC_Init()'ten once cagrilir (USER CODE SysInit). */
void Display_MPU_Config(void);

/* SDRAM cipinin acilis komutlari + dogrulama. MX_FMC_Init() icinde,
 * HAL_SDRAM_Init()'ten hemen sonra cagrilir (USER CODE FMC_Init 2):
 * LTDC SDRAM'i okumaya baslamadan once calismali. */
void Display_SDRAM_Init(void);

/* Ekran hazirligi. main() icinde, RTOS baslamadan once cagrilir. */
void Display_Init(void);

/* Dokunmatik: basiliysa true doner ve ekran koordinatlarini yazar. */
bool Touch_Read(int *x, int *y);

/* Parmak ekrandan kalkana kadar bekler (oyunlar arasi geciste). */
void Touch_WaitRelease(void);

#endif /* DISPLAY_H */
