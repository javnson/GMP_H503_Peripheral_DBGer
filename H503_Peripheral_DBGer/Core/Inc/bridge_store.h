#ifndef BRIDGE_STORE_H
#define BRIDGE_STORE_H
#include <stdint.h>

#define BRIDGE_IMAGE_SIZE 512U
/* Two independent 8 KiB sectors at the end of the 128 KiB H503 flash. */
#define BRIDGE_SLOT_A 0x0801C000UL
#define BRIDGE_SLOT_B 0x0801E000UL

typedef struct {
  const uint8_t *image;
  uint32_t generation;
  uint32_t crc32;
  uint8_t autoload;
} BridgeImage;

uint32_t Bridge_Crc32(const uint8_t *data, uint32_t length);
int BridgeStore_Read(BridgeImage *result);
int BridgeStore_Write(const uint8_t image[BRIDGE_IMAGE_SIZE], uint8_t autoload);

#endif
