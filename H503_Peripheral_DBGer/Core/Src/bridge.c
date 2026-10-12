#include "bridge.h"
#include "bridge_store.h"
#include "bridge_usb.h"
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
#define CMD_TPLD_BEGIN 0x10U
#define CMD_TPLD_CHUNK 0x11U
#define CMD_TPLD_COMMIT 0x12U
#define CMD_RESET 0x13U
#define CMD_I2C_TRANSFER 0x20U
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
  if (port != 0U || (number != 0U && number != 1U &&
                     number != 4U && number != 5U)) return 0;
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
  switch (cmd) {
  case CMD_HELLO:
    if (size) { result = RESULT_INVALID; break; }
    out[n++] = 1; out[n++] = 0; /* protocol major/minor */
    put32(out + n, 0x0000001FUL); n += 4; /* USB, GPIO, I2C, TPLD, Flash */
    memcpy(out + n, "GMP Peripheral ISO Host", 23); n += 23;
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
    out[n++] = HAL_GPIO_ReadPin(gpio, mask) == GPIO_PIN_SET;
    break;
  case CMD_GPIO_WRITE: {
    GPIO_InitTypeDef config = {0};
    if (size != 3U || p[2] > 1U || p[0] != 0U || p[1] != 0U ||
        !pin(p[0], p[1], &gpio, &mask)) {
      result = RESULT_INVALID; break;
    }
    HAL_GPIO_WritePin(gpio, mask, p[2] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    /* PA0 is TPLD IO12 / SW2. Open drain uses the board's 5.1 kOhm pull-up. */
    config.Pin = mask; config.Mode = GPIO_MODE_OUTPUT_OD;
    config.Pull = GPIO_NOPULL; config.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(gpio, &config);
    break;
  }
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
        size != (uint16_t)(3U + p[1]) || p[0] == 0x79U) {
      result = RESULT_INVALID; break;
    }
    if (!TPLD2001_I2CTransfer(p[0], p + 3, p[1], out, p[2])) {
      result = RESULT_IO; break;
    }
    n = p[2];
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
  GPIO_InitTypeDef config = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  config.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
  config.Mode = GPIO_MODE_INPUT; config.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &config);
  boot_status = 0xFFU;
  if (BridgeStore_Read(&image) && image.autoload)
    boot_status = (uint8_t)TPLD2001_LoadImage(image.image, BRIDGE_IMAGE_SIZE,
                                               &boot_address);
  (void)BridgeUsb_Init();
}

void Bridge_Poll(void)
{
  uint8_t byte;
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
