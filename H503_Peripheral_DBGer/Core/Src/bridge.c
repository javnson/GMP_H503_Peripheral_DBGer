#include "bridge.h"
#include "bridge_store.h"
#include "bridge_usb.h"
#include "bridge_gpio.h"
#include "bridge_peripheral.h"
#include "bridge_serial.h"
#include "bridge_registers.h"
#include "bridge_i3c.h"
#include "bridge_i2c_target.h"
#include "bridge_eeprom.h"
#include "bridge_metrics.h"
#include "bridge_can.h"
#include "tpld2001.h"
#include "stm32h5xx_hal.h"
#include <string.h>

/* Protocol v1: A5 5A, version, command, sequence, length LE, payload, CRC16 LE.
 * Responses set command bit 7 and prefix the payload with a status byte. */
#define MAX_PAYLOAD 256U
#define FRAME_MAX (MAX_PAYLOAD + 9U)
#define CMD_HELLO 0x01U
#define CMD_STATUS 0x02U
#define CMD_GPIO_READ 0x03U
#define CMD_GPIO_WRITE 0x04U
#define CMD_GPIO_CONFIG 0x05U
#define CMD_PIN_READ 0x06U
#define CMD_PIN_SET 0x07U
#define CMD_TPLD_BEGIN 0x10U
#define CMD_TPLD_CHUNK 0x11U
#define CMD_TPLD_COMMIT 0x12U
#define CMD_RESET 0x13U
#define CMD_I2C_TRANSFER 0x20U
#define CMD_I2C_TARGET_CONFIG 0x21U
#define CMD_I2C_TARGET_PROFILE 0x22U
#define CMD_I2C_TARGET_WORD 0x23U
#define CMD_I2C_EEPROM_STATUS 0x24U
#define CMD_I2C_EEPROM_READ 0x25U
#define CMD_I2C_EEPROM_WRITE 0x26U
#define CMD_I2C_EEPROM_CONFIG 0x27U
#define CMD_I2C_EEPROM_EVENT 0x28U
#define CMD_I2C_EEPROM_REPLY 0x29U
#define CMD_I2C_EEPROM_INVALIDATE 0x2AU
#define CMD_SPI_CONFIG 0x30U
#define CMD_SPI_TRANSFER 0x31U
#define CMD_QEP_START 0x40U
#define CMD_QEP_READ 0x41U
#define CMD_QEP_RESET 0x42U
#define CMD_SERIAL_CONFIG 0x50U
#define CMD_SERIAL_WRITE 0x51U
#define CMD_SERIAL_READ 0x52U
#define CMD_REGISTER_WRITE 0x53U
#define CMD_REGISTER_READ 0x54U
#define CMD_I3C_CONFIG 0x60U
#define CMD_I3C_DAA 0x61U
#define CMD_I3C_TRANSFER 0x62U
#define CMD_INTERFACE_STATUS 0x70U
#define CMD_CAN_CONFIG 0x71U
#define CMD_CAN_TX 0x72U
#define CMD_CAN_RX 0x73U
#define CMD_CAN_STATUS 0x74U
#define CMD_METRICS 0x75U
#define CMD_INTERFACE_UNBIND 0x76U
#define RESULT_OK 0U
#define RESULT_INVALID 1U
#define RESULT_IO 2U
#define RESULT_UNSUPPORTED 3U
#define RESULT_INCOMPLETE 4U

static uint8_t request[FRAME_MAX], response[FRAME_MAX];
static uint16_t request_size, response_size;
static uint8_t pending_response, pending_reset;
static uint32_t reset_tick;
static uint8_t staged[BRIDGE_IMAGE_SIZE], received[BRIDGE_IMAGE_SIZE / 8U];
static uint8_t stage_active, stage_autoload;
static uint8_t boot_status, boot_address;

static uint16_t crc16(const uint8_t *data, uint16_t size)
{
  uint16_t crc = 0xFFFFU;
  for (uint16_t i = 0; i < size; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8U; ++b)
      crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
  }
  return crc;
}
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put32(uint8_t *p, uint32_t value)
{
  for (uint8_t i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (i * 8U));
}
static int pin(uint8_t port, uint8_t number, GPIO_TypeDef **gpio,
               uint16_t *mask)
{
  if (port != 0U || number > 5U) return 0;
  *gpio = GPIOA;
  *mask = (uint16_t)(1U << number);
  return 1;
}

static void handle(void)
{
  const uint8_t cmd = request[3], seq = request[4];
  const uint16_t size = (uint16_t)request[5] | ((uint16_t)request[6] << 8);
  const uint8_t *p = request + 7;
  uint8_t *out = response + 8;
  uint16_t n = 0;
  uint8_t result = RESULT_OK;
  BridgeImage image;
  GPIO_TypeDef *gpio;
  uint16_t mask;
  uint8_t pin_mode;
  uint16_t pin_value;
  switch (cmd) {
  case CMD_HELLO:
    if (size) { result = RESULT_INVALID; break; }
    out[n++] = 1; out[n++] = 8; /* protocol major/minor */
    put32(out + n, 0x000FFFFFUL); n += 4; /* + confirmed interface unbind */
    static const char device_name[] = "GMP Peripheral Host";
    memcpy(out + n, device_name, sizeof(device_name) - 1U);
    n += sizeof(device_name) - 1U;
    break;
  case CMD_STATUS:
    if (size) { result = RESULT_INVALID; break; }
    out[n++] = boot_status;
    out[n++] = boot_address;
    out[n++] = BridgeStore_Read(&image) ? 1U : 0U;
    out[n++] = out[2] ? image.autoload : 0U;
    put32(out + n, out[2] ? image.generation : 0U); n += 4;
    put32(out + n, out[2] ? image.crc32 : 0U); n += 4;
    break;
  case CMD_GPIO_READ:
    if (size != 2U || !pin(p[0], p[1], &gpio, &mask)) {
      result = RESULT_INVALID; break;
    }
    if (!BridgeGpio_Get(p[1], &pin_mode, &pin_value)) {
      result = RESULT_IO; break;
    }
    out[n++] = pin_value != 0U;
    break;
  case CMD_GPIO_WRITE: {
    if (size != 3U || p[2] > 1U || p[0] != 0U || p[1] != 0U ||
        !pin(p[0], p[1], &gpio, &mask)) {
      result = RESULT_INVALID; break;
    }
    if (!BridgeGpio_Configure(0U, BRIDGE_GPIO_OUTPUT, 1U) ||
        !BridgeGpio_Set(0U, p[2])) result = RESULT_IO;
    break;
  }
  case CMD_GPIO_CONFIG:
    if (size != 3U || p[0] != 0U ||
        !BridgeGpio_Configure(p[1], p[2], boot_status != 0xFFU))
      result = RESULT_INVALID;
    break;
  case CMD_PIN_READ:
    if (size != 2U || p[0] != 0U ||
        !BridgeGpio_Get(p[1], &pin_mode, &pin_value)) {
      result = RESULT_INVALID; break;
    }
    out[n++] = pin_mode;
    out[n++] = (uint8_t)pin_value;
    out[n++] = (uint8_t)(pin_value >> 8);
    break;
  case CMD_PIN_SET:
    if (size != 4U || p[0] != 0U ||
        !BridgeGpio_Set(p[1], (uint16_t)p[2] | ((uint16_t)p[3] << 8)))
      result = RESULT_INVALID;
    break;
  case CMD_TPLD_BEGIN:
    if (size != 1U || p[0] > 1U) { result = RESULT_INVALID; break; }
    memset(received, 0, sizeof(received));
    memset(staged, 0, sizeof(staged));
    stage_autoload = p[0]; stage_active = 1U;
    break;
  case CMD_TPLD_CHUNK: {
    uint16_t offset;
    if (!stage_active || size < 3U) { result = RESULT_INVALID; break; }
    offset = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    if (offset >= BRIDGE_IMAGE_SIZE || size - 2U > BRIDGE_IMAGE_SIZE - offset) {
      result = RESULT_INVALID; break;
    }
    for (uint16_t i = 0; i < size - 2U; ++i) {
      staged[offset + i] = p[2U + i];
      received[(offset + i) >> 3] |= (uint8_t)(1U << ((offset + i) & 7U));
    }
    break;
  }
  case CMD_TPLD_COMMIT:
    if (!stage_active || size != 4U) { result = RESULT_INVALID; break; }
    for (uint16_t i = 0; i < sizeof(received); ++i)
      if (received[i] != 0xFFU) { result = RESULT_INCOMPLETE; break; }
    if (result != RESULT_OK) break;
    if (get32(p) != Bridge_Crc32(staged, BRIDGE_IMAGE_SIZE)) {
      result = RESULT_INVALID; break;
    }
    if (!BridgeStore_Write(staged, stage_autoload)) { result = RESULT_IO; break; }
    stage_active = 0U;
    break;
  case CMD_I2C_TRANSFER:
    if (size < 3U || p[1] > 64U || p[2] > 64U ||
        size != (uint16_t)(3U + p[1]) || p[0] == 0x79U ||
        BridgeI2cTarget_Active()) {
      result = RESULT_INVALID; break;
    }
    if (!TPLD2001_I2CTransfer(p[0], p + 3, p[1], out, p[2])) {
      result = RESULT_IO; break;
    }
    n = p[2];
    break;
  case CMD_I2C_TARGET_CONFIG:
    if (size != 2U || !BridgeI2cTarget_Config(p[0], p[1]))
      result = RESULT_INVALID;
    break;
  case CMD_I2C_TARGET_PROFILE:
    if (size != 4U || !BridgeI2cTarget_SetProfileWord(
        p[0], p[1], (uint16_t)((uint16_t)p[2] << 8U) | p[3]))
      result = RESULT_INVALID;
    break;
  case CMD_I2C_TARGET_WORD: {
    uint16_t value;
    if (size != 1U || !BridgeI2cTarget_GetWord(p[0], &value)) {
      result = RESULT_INVALID; break;
    }
    out[n++] = (uint8_t)(value >> 8U);
    out[n++] = (uint8_t)value;
    break;
  }
  case CMD_I2C_EEPROM_STATUS:
    if (size) { result = RESULT_INVALID; break; }
    out[n++] = BridgeEeprom_Busy();
    out[n++] = BridgeEeprom_Error();
    break;
  case CMD_I2C_EEPROM_READ:
    if (size != 2U || !BridgeEeprom_LocalRead(p[0], out, p[1]))
      result = RESULT_INVALID;
    else n = p[1];
    break;
  case CMD_I2C_EEPROM_WRITE:
    if (size < 2U || size > 33U ||
        !BridgeEeprom_LocalWrite(p[0], p + 1U, (uint8_t)(size - 1U)))
      result = RESULT_IO;
    break;
  case CMD_I2C_EEPROM_CONFIG:
    if (size != 2U ||
        !((p[0] == 8U && p[1] == 8U) ||
          ((p[0] == 14U || p[0] == 15U) && p[1] == 64U) ||
          (p[0] == 16U && p[1] == 128U))) {
      result = RESULT_INVALID; break;
    }
    /* Release any HDC1080 profile allocation before reusing the one-block
     * SRAM arena for the EEPROM. The host selects profile 2 afterward. */
    if (!BridgeI2cTarget_SetProfileWord(0U, 0U, 0U) ||
        !BridgeEeprom_Config(p[0], p[1])) result = RESULT_INVALID;
    break;
  case CMD_I2C_EEPROM_EVENT:
    if (size) result = RESULT_INVALID;
    else n = BridgeEeprom_Event(out);
    break;
  case CMD_I2C_EEPROM_REPLY:
    if (!BridgeEeprom_Reply(p, size)) result = RESULT_INVALID;
    break;
  case CMD_I2C_EEPROM_INVALIDATE:
    if (size || !BridgeEeprom_InvalidateCache()) result = RESULT_INVALID;
    break;
  case CMD_SPI_CONFIG:
    if (size != 3U || !BridgeSpi_Config(p[0], p[1], p[2]))
      result = RESULT_INVALID;
    break;
  case CMD_SPI_TRANSFER:
    if (size == 0U || size > 64U ||
        !BridgeSpi_Transfer(p, out, (uint8_t)size)) result = RESULT_IO;
    else n = size;
    break;
  case CMD_QEP_START:
    if (size || !BridgeQep_Start()) result = RESULT_IO;
    break;
  case CMD_QEP_READ: {
    int32_t position;
    uint8_t index, direction;
    if (size || !BridgeQep_Read(&position, &index, &direction)) {
      result = RESULT_IO; break;
    }
    put32(out, (uint32_t)position);
    out[4] = index;
    out[5] = direction;
    n = 6U;
    break;
  }
  case CMD_QEP_RESET:
    if (size || !BridgeQep_Reset()) result = RESULT_IO;
    break;
  case CMD_SERIAL_CONFIG:
    if (size != 6U || !BridgeSerial_Config(p[0], p[1], get32(p + 2U)))
      result = RESULT_INVALID;
    break;
  case CMD_SERIAL_WRITE:
    if (size < 2U || size > 65U ||
        !BridgeSerial_Write(p[0], p + 1U, (uint8_t)(size - 1U)))
      result = RESULT_IO;
    break;
  case CMD_SERIAL_READ: {
    uint8_t actual = 0U;
    if (size != 2U || !BridgeSerial_Read(p[0], out, p[1], &actual)) {
      result = RESULT_INVALID; break;
    }
    n = actual;
    break;
  }
  case CMD_REGISTER_WRITE:
    if (size < 3U || size > 34U ||
        !BridgeRegs_Write(p[0], p[1], p + 2U,
                          (uint8_t)(size - 2U))) result = RESULT_INVALID;
    break;
  case CMD_REGISTER_READ:
    if (size != 3U || !BridgeRegs_Read(p[0], p[1], out, p[2]))
      result = RESULT_INVALID;
    else n = p[2];
    break;
  case CMD_I3C_CONFIG:
    if (size != 1U || !BridgeI3c_Config(p[0])) result = RESULT_INVALID;
    break;
  case CMD_I3C_DAA: {
    uint8_t actual = 0U;
    if (size != 2U || !BridgeI3c_Assign(p[0], p[1], out + 1U, &actual)) {
      result = RESULT_IO; break;
    }
    out[0] = actual; n = (uint16_t)(actual + 1U);
    break;
  }
  case CMD_I3C_TRANSFER:
    if (size < 3U || p[1] > 64U || p[2] > 64U ||
        size != (uint16_t)(3U + p[1]) ||
        !BridgeI3c_Transfer(p[0], p + 3U, p[1], out, p[2]))
      result = RESULT_IO;
    else n = p[2];
    break;
  case CMD_INTERFACE_STATUS: {
    uint8_t ready, role;
    uint32_t error;
    if (size != 0U) { result = RESULT_INVALID; break; }
    BridgeSpi_GetStatus(&ready, &role, &error);
    out[n++] = ready; out[n++] = role;
    put32(out + n, error); n += 4U;
    BridgeI3c_GetStatus(&ready, &role, &error);
    out[n++] = ready; out[n++] = role;
    put32(out + n, error); n += 4U;
    out[n++] = BridgeI2cTarget_Active();
    out[n++] = BridgeI2cTarget_Address();
    out[n++] = BridgeSerial_ReadyMask();
    break;
  }
  case CMD_CAN_CONFIG:
    if (size != 4U || !BridgeCan_Config(get32(p))) result = RESULT_INVALID;
    break;
  case CMD_CAN_TX:
    if (size < 6U || p[5] > 8U || size != (uint16_t)(6U + p[5]) ||
        !BridgeCan_Send(get32(p), p[4], p + 6U, p[5])) result = RESULT_IO;
    break;
  case CMD_CAN_RX: {
    uint32_t identifier;
    uint8_t extended, length;
    int received;
    if (size) { result = RESULT_INVALID; break; }
    received = BridgeCan_Receive(&identifier, &extended, out + 7U, &length);
    if (received < 0) { result = RESULT_IO; break; }
    out[0] = (uint8_t)(received != 0);
    if (received) {
      put32(out + 1U, identifier);
      out[5] = extended;
      out[6] = length;
      n = (uint16_t)(7U + length);
    } else n = 1U;
    break;
  }
  case CMD_CAN_STATUS:
    if (size || !BridgeCan_Status(out)) result = RESULT_INVALID;
    else n = 7U;
    break;
  case CMD_METRICS: {
    uint16_t fixed, allocated, available, stack;
    uint8_t cpu;
    if (size) { result = RESULT_INVALID; break; }
    BridgeMetrics_Get(&fixed, &allocated, &available, &stack, &cpu);
    out[n++] = (uint8_t)fixed; out[n++] = (uint8_t)(fixed >> 8U);
    out[n++] = (uint8_t)allocated; out[n++] = (uint8_t)(allocated >> 8U);
    out[n++] = (uint8_t)available; out[n++] = (uint8_t)(available >> 8U);
    out[n++] = (uint8_t)stack; out[n++] = (uint8_t)(stack >> 8U);
    out[n++] = cpu;
    break;
  }
  case CMD_INTERFACE_UNBIND:
    if (size != 1U) { result = RESULT_INVALID; break; }
    switch (p[0]) {
    case 1U: result = BridgeGpio_Configure(0U, BRIDGE_GPIO_INPUT, 1U) &&
                       BridgeGpio_Configure(1U, BRIDGE_GPIO_INPUT, 1U) ? RESULT_OK : RESULT_IO; break;
    case 2U: result = BridgeI2cTarget_Config(0U, 0U) ? RESULT_OK : RESULT_IO; break;
    case 3U: result = BridgeI3c_Disable() ? RESULT_OK : RESULT_IO; break;
    case 4U: result = BridgeSpi_Disable() ? RESULT_OK : RESULT_IO; break;
    case 5U: result = BridgeQep_Disable() ? RESULT_OK : RESULT_IO; break;
    case 6U: case 7U: case 8U:
      result = BridgeSerial_Disable((uint8_t)(p[0] - 6U)) ? RESULT_OK : RESULT_IO; break;
    case 9U: result = BridgeCan_Disable() ? RESULT_OK : RESULT_IO; break;
    default: result = RESULT_INVALID; break;
    }
    break;
  case CMD_RESET:
    if (size) { result = RESULT_INVALID; break; }
    pending_reset = 1U; reset_tick = HAL_GetTick();
    break;
  default:
    result = RESULT_UNSUPPORTED;
    break;
  }
  response[0] = 0xA5; response[1] = 0x5A;
  response[2] = 1U; response[3] = cmd | 0x80U; response[4] = seq;
  response[5] = (uint8_t)(n + 1U);
  response[6] = (uint8_t)((n + 1U) >> 8);
  response[7] = result;
  uint16_t crc = crc16(response, (uint16_t)(8U + n));
  response[8U + n] = (uint8_t)crc;
  response[9U + n] = (uint8_t)(crc >> 8);
  response_size = (uint16_t)(10U + n);
  pending_response = 1U;
}

void Bridge_Init(void)
{
  BridgeImage image;
  BridgeRegs_Init();
  BridgeMetrics_Init();
  BridgeEeprom_Init();
  BridgeGpio_Init();
  boot_status = 0xFFU;
  if (BridgeStore_Read(&image) && image.autoload)
    boot_status = (uint8_t)TPLD2001_LoadImage(image.image, BRIDGE_IMAGE_SIZE,
                                               &boot_address);
  (void)BridgeUsb_Init();
}

void Bridge_Poll(void)
{
  uint8_t byte;
  BridgeQep_Poll();
  BridgeSerial_Poll();
  BridgeI3c_Poll();
  BridgeEeprom_Poll();
  BridgeI2cTarget_Poll();
  if (pending_response) {
    if (BridgeUsb_Send(response, response_size)) pending_response = 0U;
    return;
  }
  if (pending_reset && HAL_GetTick() - reset_tick > 250U) NVIC_SystemReset();
  while (BridgeUsb_GetByte(&byte)) {
    if (request_size == 0U && byte != 0xA5U) continue;
    if (request_size == 1U && byte != 0x5AU) { request_size = 0U; continue; }
    request[request_size++] = byte;
    if (request_size == 7U) {
      uint16_t length = (uint16_t)request[5] | ((uint16_t)request[6] << 8);
      if (request[2] != 1U || length > MAX_PAYLOAD) { request_size = 0U; continue; }
    }
    if (request_size >= 9U) {
      uint16_t length = (uint16_t)request[5] | ((uint16_t)request[6] << 8);
      if (request_size == length + 9U) {
        uint16_t crc = crc16(request, (uint16_t)(length + 7U));
        if (request[length + 7U] == (uint8_t)crc &&
            request[length + 8U] == (uint8_t)(crc >> 8)) handle();
        request_size = 0U;
        break;
      }
    }
  }
}
