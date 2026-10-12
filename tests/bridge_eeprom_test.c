#include "bridge_eeprom.h"
#include "bridge_metrics.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static uint32_t tick;
static uint8_t heap[1024];
void *BridgeMemory_Allocate(uint16_t size) { return size <= sizeof(heap) ? heap : 0; }
void BridgeMemory_Release(void) {}
uint32_t HAL_GetTick(void) { return tick; }

int main(void)
{
  uint8_t event[160], reply[132], value;
  uint8_t image[16384];
  memset(image, 0xFF, sizeof(image));
  BridgeEeprom_Init();
  CHECK(BridgeEeprom_Config(14U, 64U));
  CHECK(BridgeEeprom_AddressBytes() == 2U);
  CHECK(BridgeEeprom_NextAddress(16383U) == 0U);
  CHECK(!BridgeEeprom_ReadAt(0x1234U, &value));
  CHECK(BridgeEeprom_Event(event) == 5U && event[0] == 1U);
  CHECK(event[2] == (0x1234U / 64U));
  reply[0] = event[0]; reply[1] = event[1];
  reply[2] = event[2]; reply[3] = event[3];
  memset(reply + 4U, 0xA5, 64U);
  CHECK(BridgeEeprom_Reply(reply, 68U));
  CHECK(BridgeEeprom_ReadAt(0x1234U, &value) && value == 0xA5U);
  CHECK(!BridgeEeprom_ReadAt(0x127FU, &value));
  BridgeEeprom_AbortRead();
  CHECK(BridgeEeprom_Error());
  CHECK(BridgeEeprom_Config(14U, 64U));
  BridgeEeprom_BeginWrite(0x123EU);
  BridgeEeprom_WriteByte(0x11U);
  BridgeEeprom_WriteByte(0x22U);
  BridgeEeprom_WriteByte(0x33U); /* Rolls over within the 64-byte page. */
  BridgeEeprom_Stop();
  CHECK(BridgeEeprom_Busy());
  CHECK(BridgeEeprom_Event(event) == 85U && event[0] == 2U);
  CHECK((event[5U + 7U] & 0xC0U) == 0xC0U && (event[5U] & 1U));
  CHECK(event[21U + 62U] == 0x11U && event[21U + 63U] == 0x22U && event[21U] == 0x33U);
  for (unsigned i = 0; i < 64U; ++i)
    if (event[5U + i / 8U] & (1U << (i % 8U)))
      image[0x1200U + i] = event[21U + i];
  reply[0] = event[0]; reply[1] = event[1]; reply[2] = event[2]; reply[3] = event[3];
  CHECK(BridgeEeprom_Reply(reply, 4U));
  tick += 5U;
  CHECK(!BridgeEeprom_Busy());
  CHECK(image[0x123EU] == 0x11U && image[0x123FU] == 0x22U && image[0x1200U] == 0x33U);
  return 0;
}
