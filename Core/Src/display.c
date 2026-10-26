/*
 * display.c - STM32F746G-DISCO ekran ve dokunmatik donanimi
 *
 * Ekran : 480x272 RGB565, LTDC Layer0 framebuffer'i SDRAM'de (0xC0000000)
 * Giris : FT5336 kapasitif dokunmatik (I2C3)
 */
#include "display.h"
#include "gfx.h"
#include "main.h"
#include "cmsis_os.h"

extern SDRAM_HandleTypeDef hsdram1;
extern I2C_HandleTypeDef hi2c3;

#define SDRAM_TIMEOUT   0xFFFF
#define SDRAM_REFRESH   0x0603  /* 64ms/4096 satir @ 100MHz SDCLK */
#define SDRAM_MODE_REG  0x0230  /* Burst 1, sequential, CAS 3, single write burst */

#define TS_I2C_ADDR     0x70    /* FT5336, 7-bit 0x38 */
#define TS_SWAP_XY      1       /* Bu kartta X/Y registerlari ters */

/* CubeMX'in MPU_Config()'i 0x60000000-0xDFFFFFFF araligini erisime kapatiyor.
 * Bu aralikta FMC/QSPI kontrol registerlari (0xA0000000) ve SDRAM (0xC0000000)
 * var; ikisini de tekrar aciyoruz. MX_FMC_Init()'ten ONCE cagrilmali. */
void Display_MPU_Config(void)
{
  MPU_Region_InitTypeDef r = {0};

  HAL_MPU_Disable();

  /* FMC + QSPI kontrol registerlari: device bellek */
  r.Enable = MPU_REGION_ENABLE;
  r.Number = MPU_REGION_NUMBER1;
  r.BaseAddress = 0xA0000000UL;
  r.Size = MPU_REGION_SIZE_8KB;
  r.SubRegionDisable = 0x00;
  r.TypeExtField = MPU_TEX_LEVEL0;
  r.AccessPermission = MPU_REGION_FULL_ACCESS;
  r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  r.IsShareable = MPU_ACCESS_SHAREABLE;
  r.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  r.IsBufferable = MPU_ACCESS_BUFFERABLE;
  HAL_MPU_ConfigRegion(&r);

  /* SDRAM (8MB): normal, cache'siz bellek */
  r.Number = MPU_REGION_NUMBER2;
  r.BaseAddress = FB_ADDR;
  r.Size = MPU_REGION_SIZE_8MB;
  r.TypeExtField = MPU_TEX_LEVEL1;
  r.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  r.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  r.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&r);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/*
 * SDRAM acilisi
 *
 * SDRAM cipinin mode register'i guc verildiginde rastgele bir degerle baslar.
 * LOAD_MODE komutu tutmazsa cip "burst 4 + burst write" modunda kalabilir:
 * her yazma ardisik 4 hucreye birden gider ve ekranda ince cizgiler/yazilar
 * bozulur. Bu hata sadece guc kesilip verilince gorunur; reset ya da yeniden
 * programlama SDRAM'in gucunu kesmedigi icin onceki (dogru) mod korunur.
 *
 * Onlemler:
 *  - Acilis dizisi MX_FMC_Init() icinde, LTDC SDRAM'i okumaya baslamadan once
 *    calisir (araya LTDC okumalari girip bankalari acik birakamaz).
 *  - Bu HAL surumunde HAL_SDRAM_SendCommand() FMC'nin onceki komutu
 *    bitirmesini beklemiyor; her komuttan once/sonra BUSY beklenir.
 *  - LOAD_MODE'dan hemen once tum bankalar tekrar kapatilir (PALL).
 *  - Sonunda test deseni yazilip geri okunur; tutmadiysa dizi tekrarlanir.
 */
static void sdram_wait_ready(void)
{
  uint32_t start = HAL_GetTick();
  while (__FMC_SDRAM_GET_FLAG(hsdram1.Instance, FMC_SDRAM_FLAG_BUSY))
    if (HAL_GetTick() - start > 10)
      break;
}

static void sdram_command(uint32_t mode, uint32_t refresh_count, uint32_t mode_reg)
{
  FMC_SDRAM_CommandTypeDef cmd = {0};

  cmd.CommandMode = mode;
  cmd.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
  cmd.AutoRefreshNumber = refresh_count;
  cmd.ModeRegisterDefinition = mode_reg;

  sdram_wait_ready();
  HAL_SDRAM_SendCommand(&hsdram1, &cmd, SDRAM_TIMEOUT);
  sdram_wait_ready();
}

static void sdram_init_sequence(void)
{
  sdram_command(FMC_SDRAM_CMD_CLK_ENABLE, 1, 0);
  HAL_Delay(1);                                     /* >= 100 us bekleme */
  sdram_command(FMC_SDRAM_CMD_PALL, 1, 0);
  sdram_command(FMC_SDRAM_CMD_AUTOREFRESH_MODE, 8, 0);
  sdram_command(FMC_SDRAM_CMD_PALL, 1, 0);          /* LOAD_MODE bos banka ister */
  sdram_command(FMC_SDRAM_CMD_LOAD_MODE, 1, SDRAM_MODE_REG);

  HAL_SDRAM_ProgramRefreshRate(&hsdram1, SDRAM_REFRESH);
}

/* Ekranin disindaki son 128 byte'a ardisik farkli degerler yazip geri okur.
 * Yanlis burst modunda 4'lu gruplar ayni degeri gosterir. */
#define SDRAM_TEST_ADDR  (FB_ADDR + 0x007FFF80UL)

static bool sdram_selftest(void)
{
  volatile uint16_t *h = (volatile uint16_t *)SDRAM_TEST_ADDR;
  volatile uint32_t *w = (volatile uint32_t *)(SDRAM_TEST_ADDR + 64);

  for (int i = 0; i < 16; i++)
    h[i] = (uint16_t)(0xA55A ^ (i * 0x1111));
  for (int i = 0; i < 8; i++)
    w[i] = 0x13579BDFUL ^ (uint32_t)(i * 0x01010101UL);
  __DSB();

  for (int i = 0; i < 16; i++)
    if (h[i] != (uint16_t)(0xA55A ^ (i * 0x1111)))
      return false;
  for (int i = 0; i < 8; i++)
    if (w[i] != (0x13579BDFUL ^ (uint32_t)(i * 0x01010101UL)))
      return false;
  return true;
}

#define SDRAM_INIT_TRIES 5

void Display_SDRAM_Init(void)
{
  for (int i = 0; i < SDRAM_INIT_TRIES; i++)
  {
    sdram_init_sequence();
    if (sdram_selftest())
      return;
    HAL_Delay(2);
  }
}

void Display_Init(void)
{
  SCB_EnableICache();

  /* Son kontrol: LTDC acildiktan sonra da SDRAM saglam mi? Degilse LTDC'yi
   * durdurup acilis dizisini tekrarla. */
  if (!sdram_selftest())
  {
    LTDC->GCR &= ~LTDC_GCR_LTDCEN;
    Display_SDRAM_Init();
    LTDC->GCR |= LTDC_GCR_LTDCEN;
  }

  gfx_fill(0, 0, LCD_W, LCD_H, COL_BG);
}

bool Touch_Read(int *x, int *y)
{
  uint8_t b[5];

  if (HAL_I2C_Mem_Read(&hi2c3, TS_I2C_ADDR, 0x02, I2C_MEMADD_SIZE_8BIT, b, sizeof b, 5) != HAL_OK)
    return false;

  uint8_t n = b[0] & 0x0F;
  if (n == 0 || n > 5)
    return false;

  int rx = ((b[1] & 0x0F) << 8) | b[2];
  int ry = ((b[3] & 0x0F) << 8) | b[4];
#if TS_SWAP_XY
  *x = ry;
  *y = rx;
#else
  *x = rx;
  *y = ry;
#endif
  if (*x >= LCD_W || *y >= LCD_H)
    return false;
  if (gfx_get_rotation())
  {
    *x = LCD_W - 1 - *x;
    *y = LCD_H - 1 - *y;
  }
  return true;
}

void Touch_WaitRelease(void)
{
  int x, y;
  while (Touch_Read(&x, &y))
    osDelay(10);
}
