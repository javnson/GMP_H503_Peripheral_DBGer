#include "bridge_store.h"
#include "stm32h5xx_hal.h"
#include <string.h>

#define SLOT_MAGIC 0x31444C54UL /* TLD1, little endian */
#define IMAGE_OFFSET 32U

typedef struct {
  uint32_t magic;
  uint32_t generation;
  uint32_t length;
  uint32_t crc32;
  uint32_t autoload;
  uint32_t reserved[3];
} SlotHeader;

uint32_t Bridge_Crc32(const uint8_t *data, uint32_t length)
{
  uint32_t crc = 0xFFFFFFFFUL;
  for (uint32_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint32_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
  }
  return ~crc;
}

static int valid(uint32_t address, BridgeImage *out)
{
  const SlotHeader *h = (const SlotHeader *)address;
  const uint8_t *image = (const uint8_t *)(address + IMAGE_OFFSET);
  if (h->magic != SLOT_MAGIC || h->length != BRIDGE_IMAGE_SIZE ||
      h->autoload > 1U || Bridge_Crc32(image, BRIDGE_IMAGE_SIZE) != h->crc32)
    return 0;
  if (out != NULL) {
    out->image = image;
    out->generation = h->generation;
    out->crc32 = h->crc32;
    out->autoload = (uint8_t)h->autoload;
  }
  return 1;
}

int BridgeStore_Read(BridgeImage *out)
{
  BridgeImage a, b;
  int va = valid(BRIDGE_SLOT_A, &a);
  int vb = valid(BRIDGE_SLOT_B, &b);
  if (!va && !vb) return 0;
  *out = (!vb || (va && (int32_t)(a.generation - b.generation) > 0)) ? a : b;
  return 1;
}

int BridgeStore_Write(const uint8_t image[BRIDGE_IMAGE_SIZE], uint8_t autoload)
{
  BridgeImage current;
  uint32_t target, generation, sector, error = 0;
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t words[4];
  SlotHeader header = {0};
  if (image == NULL || autoload > 1U) return 0;
  if (BridgeStore_Read(&current)) {
    target = ((uint32_t)current.image == BRIDGE_SLOT_A + IMAGE_OFFSET)
             ? BRIDGE_SLOT_B : BRIDGE_SLOT_A;
    generation = current.generation + 1U;
  } else {
    target = BRIDGE_SLOT_A;
    generation = 1U;
  }
  sector = (target == BRIDGE_SLOT_A) ? FLASH_SECTOR_6 : FLASH_SECTOR_7;
  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Banks = FLASH_BANK_2;
  erase.Sector = sector;
  erase.NbSectors = 1U;
  if (HAL_FLASH_Unlock() != HAL_OK) return 0;
  if (HAL_FLASHEx_Erase(&erase, &error) != HAL_OK) goto fail;
  /* Write the data first; the magic header is the commit marker. */
  for (uint32_t offset = 0; offset < BRIDGE_IMAGE_SIZE; offset += 16U) {
    memcpy(words, image + offset, 16U);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                          target + IMAGE_OFFSET + offset,
                          (uint32_t)words) != HAL_OK) goto fail;
  }
  header.magic = SLOT_MAGIC;
  header.generation = generation;
  header.length = BRIDGE_IMAGE_SIZE;
  header.crc32 = Bridge_Crc32(image, BRIDGE_IMAGE_SIZE);
  header.autoload = autoload;
  memcpy(words, (const uint8_t *)&header + 16U, 16U);
  if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, target + 16U,
                        (uint32_t)words) != HAL_OK) goto fail;
  memcpy(words, &header, 16U);
  if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, target,
                        (uint32_t)words) != HAL_OK) goto fail;
  HAL_FLASH_Lock();
  BridgeImage check;
  return valid(target, &check) && check.crc32 == header.crc32;
fail:
  HAL_FLASH_Lock();
  return 0;
}
