#include "bridge_registers.h"
#include <string.h>

static uint8_t banks[BRIDGE_REG_PORTS][256];

static uint8_t crc8(const uint8_t *data, uint8_t length)
{
  uint8_t crc = 0U;
  for (uint8_t i = 0U; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0U; bit < 8U; ++bit)
      crc = (uint8_t)((crc << 1) ^ ((crc & 0x80U) ? 0x07U : 0U));
  }
  return crc;
}

void BridgeRegs_Init(void) { memset(banks, 0, sizeof(banks)); }

int BridgeRegs_Write(uint8_t port, uint8_t address,
                     const uint8_t *data, uint8_t size)
{
  if (port >= BRIDGE_REG_PORTS || data == NULL || size == 0U ||
      size > BRIDGE_REG_MAX_TRANSFER || (uint16_t)address + size > 256U)
    return 0;
  memcpy(banks[port] + address, data, size);
  return 1;
}

int BridgeRegs_Read(uint8_t port, uint8_t address,
                    uint8_t *data, uint8_t size)
{
  if (port >= BRIDGE_REG_PORTS || data == NULL || size == 0U ||
      size > BRIDGE_REG_MAX_TRANSFER || (uint16_t)address + size > 256U)
    return 0;
  memcpy(data, banks[port] + address, size);
  return 1;
}

int BridgeRegs_Execute(uint8_t port, const uint8_t *request, uint8_t size,
                       uint8_t *reply, uint8_t *reply_size)
{
  uint8_t length, opcode, expected, n = 3U;
  if (port >= BRIDGE_REG_PORTS || request == NULL || reply == NULL ||
      reply_size == NULL || size < 5U || request[0] != 0xA5U) return 0;
  opcode = request[1]; length = request[3];
  expected = (uint8_t)(5U + (opcode == 1U ? length : 0U));
  reply[0] = 0x5AU; reply[1] = 0U; reply[2] = 0U;
  if (opcode > 1U || length == 0U || length > BRIDGE_REG_MAX_TRANSFER ||
      size != expected || (uint16_t)request[2] + length > 256U ||
      crc8(request, (uint8_t)(size - 1U)) != request[size - 1U]) {
    reply[1] = 1U;
  } else if (opcode == 0U) {
    reply[2] = length;
    (void)BridgeRegs_Read(port, request[2], reply + 3U, length);
    n = (uint8_t)(n + length);
  } else {
    (void)BridgeRegs_Write(port, request[2], request + 4U, length);
  }
  reply[n] = crc8(reply, n);
  *reply_size = (uint8_t)(n + 1U);
  return 1;
}
