/*
 * scores.c - En yuksek skorlarin dahili Flash'ta kalici saklanmasi
 *
 * Son Flash sektoru (sektor 7, 256KB, 0x080C0000) bir kayit gunlugu olarak
 * kullanilir: her rekor, sektordeki bir sonraki bos yere yeni bir kayit olarak
 * eklenir; okurken en son gecerli kayit alinir. Sektor ancak doldugunda
 * (~6500 rekordan sonra) silinir. Boylece Flash yipranmaz ve normal bir
 * kayit birkac mikrosaniye surer (silme ~1-2 sn boyunca CPU'yu durdurur).
 *
 * Program yuklenirken CubeIDE sadece kodun kapladigi sektorleri sildigi icin
 * skorlar yeni build'lerden sonra da korunur ("full chip erase" haric).
 */
#include "scores.h"
#include "main.h"
#include <string.h>

#define STORE_ADDR    0x080C0000UL
#define STORE_SIZE    (256UL * 1024UL)
#define STORE_SECTOR  FLASH_SECTOR_7
#define RECORD_MAGIC  0x53435231UL      /* "SCR1" */

typedef struct
{
  uint32_t magic;
  uint32_t scores[SCORE_SLOTS];
  uint32_t check;
} Record;

#define RECORD_WORDS  (sizeof(Record) / 4)
#define RECORD_COUNT  (STORE_SIZE / sizeof(Record))

/* Linker script sembolleri: programin Flash'taki sonu */
extern uint32_t _sidata, _sdata, _edata;

static uint32_t best[SCORE_SLOTS];
static int next_slot;          /* bir sonraki bos kayit yeri */
static bool usable;

static uint32_t checksum(const Record *r)
{
  uint32_t c = 0xA5A5A5A5UL ^ r->magic;
  for (int i = 0; i < SCORE_SLOTS; i++)
    c = (c << 5 | c >> 27) ^ r->scores[i];
  return c;
}

static const Record *record_at(int i)
{
  return (const Record *)(STORE_ADDR + (uint32_t)i * sizeof(Record));
}

static bool record_is_erased(const Record *r)
{
  const uint32_t *w = (const uint32_t *)r;
  for (unsigned i = 0; i < RECORD_WORDS; i++)
    if (w[i] != 0xFFFFFFFFUL)
      return false;
  return true;
}

void Scores_Init(void)
{
  /* Program buyuyup bu sektore tasarsa kalici kaydi kapat: kodu ezmeyelim */
  uint32_t image_end = (uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata);
  usable = image_end <= STORE_ADDR;

  memset(best, 0, sizeof best);
  next_slot = 0;
  if (!usable)
    return;

  for (int i = 0; i < (int)RECORD_COUNT; i++)
  {
    const Record *r = record_at(i);
    if (record_is_erased(r))
      break;
    if (r->magic == RECORD_MAGIC && r->check == checksum(r))
      memcpy(best, r->scores, sizeof best);
    next_slot = i + 1;
  }
}

uint32_t Scores_Get(int slot)
{
  return (slot >= 0 && slot < SCORE_SLOTS) ? best[slot] : 0;
}

static bool write_record(int i, const Record *r)
{
  const uint32_t *w = (const uint32_t *)r;
  uint32_t addr = STORE_ADDR + (uint32_t)i * sizeof(Record);

  for (unsigned k = 0; k < RECORD_WORDS; k++)
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + k * 4, w[k]) != HAL_OK)
      return false;
  return true;
}

static bool erase_store(void)
{
  FLASH_EraseInitTypeDef er = {0};
  uint32_t bad_sector = 0;

  er.TypeErase = FLASH_TYPEERASE_SECTORS;
  er.Sector = STORE_SECTOR;
  er.NbSectors = 1;
  er.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  return HAL_FLASHEx_Erase(&er, &bad_sector) == HAL_OK;
}

bool Scores_Submit(int slot, uint32_t score)
{
  Record r;

  if (slot < 0 || slot >= SCORE_SLOTS || score <= best[slot])
    return false;

  best[slot] = score;
  if (!usable)
    return true;   /* rekor, ama sadece bu oturum icin */

  r.magic = RECORD_MAGIC;
  memcpy(r.scores, best, sizeof best);
  r.check = checksum(&r);

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_ALL_ERRORS);

  /* Yer kalmadiysa ya da hedef yer bos degilse sektoru sil, bastan yaz */
  if (next_slot >= (int)RECORD_COUNT || !record_is_erased(record_at(next_slot)))
  {
    if (erase_store())
      next_slot = 0;
    else
      next_slot = (int)RECORD_COUNT;   /* yazmayi atla */
  }
  if (next_slot < (int)RECORD_COUNT && write_record(next_slot, &r))
    next_slot++;

  HAL_FLASH_Lock();
  return true;
}
