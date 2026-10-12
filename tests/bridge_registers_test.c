#include "bridge_registers.h"
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
  fprintf(stderr, "line %d: %s\n", __LINE__, #test); return 1; \
} } while (0)

static uint8_t crc8(const uint8_t *data, uint8_t size)
{
  uint8_t crc = 0U;
  for (uint8_t i = 0U; i < size; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0U; b < 8U; ++b)
      crc = (uint8_t)((crc << 1) ^ ((crc & 0x80U) ? 7U : 0U));
  }
  return crc;
}

int main(void)
{
  uint8_t reply[36], reply_size = 0U, data[3] = {0};
  uint8_t write[] = {0xA5U, 1U, 0x10U, 3U, 0xAAU, 0xBBU, 0xCCU, 0U};
  uint8_t read[] = {0xA5U, 0U, 0x10U, 3U, 0U};
  BridgeRegs_Init();
  CHECK(BridgeRegs_Read(3U, 0x10U, data, 3U));
  CHECK(data[0] == 0U && data[1] == 0U && data[2] == 0U);
  write[7] = crc8(write, 7U);
  CHECK(BridgeRegs_Execute(3U, write, sizeof(write), reply, &reply_size));
  CHECK(reply_size == 4U && reply[0] == 0x5AU && reply[1] == 0U);
  CHECK(reply[3] == crc8(reply, 3U));
  read[4] = crc8(read, 4U);
  CHECK(BridgeRegs_Execute(3U, read, sizeof(read), reply, &reply_size));
  CHECK(reply_size == 7U && reply[2] == 3U);
  CHECK(memcmp(reply + 3U, write + 4U, 3U) == 0);
  CHECK(reply[6] == crc8(reply, 6U));
  write[7] ^= 1U;
  CHECK(BridgeRegs_Execute(3U, write, sizeof(write), reply, &reply_size));
  CHECK(reply[1] == 1U && reply_size == 4U);
  CHECK(!BridgeRegs_Write(3U, 255U, data, 3U));
  CHECK(!BridgeRegs_Read(6U, 0U, data, 1U));
  CHECK(!BridgeRegs_Execute(3U, read, 4U, reply, &reply_size));
  return 0;
}
