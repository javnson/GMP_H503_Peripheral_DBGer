#include "tpld2001.h"
#ifdef TPLD_PULSE_DISTRIBUTOR
#include "tpld2001_pulse_distributor_image.h"
#else
#include "tpld2001_image.h"
#endif

#define TPLD_SDA             GPIO_PIN_5
#define TPLD_SCL             GPIO_PIN_6
#define TPLD_CONFIG_START    0x0200U
#define TPLD_MODE_REGISTER   0x0400U

/* PB5 is I2C1_SMBA, not I2C1_SDA on STM32H503. The board wires PB5 to the
 * TPLD SDA net, so these two pins must be used as open-drain GPIO instead. */
static void i2c_delay(void)
{
  for (volatile uint32_t n = 0; n < 300U; ++n) __NOP();
}

static void sda(uint8_t high)
{
  HAL_GPIO_WritePin(GPIOB, TPLD_SDA, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void scl(uint8_t high)
{
  HAL_GPIO_WritePin(GPIOB, TPLD_SCL, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t clock_high(void)
{
  uint32_t start = HAL_GetTick();
  scl(1);
  while (HAL_GPIO_ReadPin(GPIOB, TPLD_SCL) == GPIO_PIN_RESET) {
    if ((HAL_GetTick() - start) > 2U) return 0;
  }
  i2c_delay();
  return 1;
}

static void i2c_init(void)
{
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_GPIOB_CLK_ENABLE();
  HAL_GPIO_WritePin(GPIOB, TPLD_SDA | TPLD_SCL, GPIO_PIN_SET);
  gpio.Pin = TPLD_SDA | TPLD_SCL;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);
  i2c_delay();
}

static uint8_t i2c_start(void)
{
  sda(1);
  i2c_delay();
  if (!clock_high()) return 0;
  if (HAL_GPIO_ReadPin(GPIOB, TPLD_SDA) == GPIO_PIN_RESET) return 0;
  sda(0);
  i2c_delay();
  scl(0);
  return 1;
}

static void i2c_stop(void)
{
  sda(0);
  i2c_delay();
  (void)clock_high();
  sda(1);
  i2c_delay();
}

static uint8_t write_bit(uint8_t bit)
{
  sda(bit);
  i2c_delay();
  if (!clock_high()) return 0;
  scl(0);
  i2c_delay();
  return 1;
}

static uint8_t read_bit(uint8_t *bit)
{
  sda(1);
  i2c_delay();
  if (!clock_high()) return 0;
  *bit = (HAL_GPIO_ReadPin(GPIOB, TPLD_SDA) == GPIO_PIN_SET);
  scl(0);
  i2c_delay();
  return 1;
}

static uint8_t write_byte(uint8_t value)
{
  uint8_t nack;
  for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1) {
    if (!write_bit((value & mask) != 0U)) return 0;
  }
  if (!read_bit(&nack)) return 0;
  return !nack;
}

static uint8_t read_byte(uint8_t *value)
{
  uint8_t bit;
  *value = 0;
  for (uint32_t i = 0; i < 8U; ++i) {
    if (!read_bit(&bit)) return 0;
    *value = (uint8_t)((*value << 1) | bit);
  }
  return write_bit(1); /* NACK the last byte. */
}

static uint8_t read_register(uint8_t address, uint16_t reg, uint8_t *value)
{
  uint8_t ok = i2c_start();
  if (ok) ok = write_byte((uint8_t)(address << 1));
  if (ok) ok = write_byte((uint8_t)(reg >> 8));
  if (ok) ok = write_byte((uint8_t)reg);
  if (ok) ok = i2c_start();
  if (ok) ok = write_byte((uint8_t)((address << 1) | 1U));
  if (ok) ok = read_byte(value);
  i2c_stop();
  return ok;
}

static uint8_t write_register(uint8_t address, uint16_t reg, uint8_t value)
{
  uint8_t ok = i2c_start();
  if (ok) ok = write_byte((uint8_t)(address << 1));
  if (ok) ok = write_byte((uint8_t)(reg >> 8));
  if (ok) ok = write_byte((uint8_t)reg);
  if (ok) ok = write_byte(value);
  i2c_stop();
  return ok;
}

static uint8_t unlock_key(uint8_t address, uint8_t key)
{
  uint8_t ok = i2c_start();
  if (ok) ok = write_byte((uint8_t)(address << 1));
  if (ok) ok = write_byte(0x01U);
  if (ok) ok = write_byte(key);
  i2c_stop();
  return ok;
}

int TPLD2001_I2CTransfer(uint8_t address, const uint8_t *tx, uint8_t tx_size,
                         uint8_t *rx, uint8_t rx_size)
{
  uint8_t ok = 1;
  if (address < 0x08U || address > 0x77U || tx_size > 64U || rx_size > 64U ||
      (tx_size && tx == NULL) || (rx_size && rx == NULL)) return 0;
  i2c_init();
  if (tx_size || !rx_size) {
    ok = i2c_start() && write_byte((uint8_t)(address << 1));
    for (uint8_t i = 0; i < tx_size && ok; ++i) ok = write_byte(tx[i]);
  }
  if (rx_size && ok) {
    ok = i2c_start() && write_byte((uint8_t)((address << 1) | 1U));
    for (uint8_t i = 0; i < rx_size && ok; ++i) {
      uint8_t bit;
      rx[i] = 0;
      for (uint8_t b = 0; b < 8U && ok; ++b) {
        ok = read_bit(&bit);
        rx[i] = (uint8_t)((rx[i] << 1) | bit);
      }
      if (ok) ok = write_bit((uint8_t)(i + 1U == rx_size));
    }
  }
  i2c_stop();
  return ok;
}

TPLD_Status TPLD2001_LoadImage(const uint8_t *image, uint16_t size,
                              uint8_t *found_address)
{
  static const uint8_t keys[4] = {0xB9, 0x3E, 0xAF, 0x58};
  uint8_t address = 0, msb, lsb, value;
  if (image == NULL || size != TPLD2001_IMAGE_SIZE) return TPLD_WRITE_FAILED;
  if (found_address != NULL) *found_address = 0;
  i2c_init();

  /* Blank TPLD2001: ADDR = [A6 A5 A4 A3 0 0 1]. */
  for (uint8_t hardware_bits = 0; hardware_bits < 16U; ++hardware_bits) {
    uint8_t candidate = (uint8_t)((hardware_bits << 3) | 1U);
    if (read_register(candidate, 0x0000U, &msb) &&
        read_register(candidate, 0x0001U, &lsb) &&
        msb == 0x20U && lsb == 0x01U) {
      address = candidate;
      if (found_address != NULL) *found_address = address;
      break;
    }
  }
  if (address == 0U) return TPLD_NO_DEVICE;

  for (uint32_t i = 0; i < sizeof(keys); ++i) {
    if (!unlock_key(address, keys[i])) return TPLD_UNLOCK_FAILED;
    HAL_Delay(1U); /* TI requires at least 500 us between transactions. */
  }
  HAL_Delay(1U);
  if (!read_register(address, TPLD_MODE_REGISTER, &value) || value != 0x10U)
    return TPLD_MODE_FAILED;
  if (!write_register(address, TPLD_MODE_REGISTER, 0x02U))
    return TPLD_MODE_FAILED;

  /* Single-byte accesses avoid dependence on the auto-increment setting. */
  for (uint16_t i = 0; i < size; ++i) {
    uint16_t reg = TPLD_CONFIG_START + i;
    if (!write_register(address, reg, image[i]))
      return TPLD_WRITE_FAILED;
    if (!read_register(address, reg, &value) || value != image[i])
      return TPLD_VERIFY_FAILED;
  }

  if (!unlock_key(address, 0x4BU)) return TPLD_ACTIVATE_FAILED;
  HAL_Delay(1U);
  if (!write_register(address, TPLD_MODE_REGISTER, 0x00U))
    return TPLD_ACTIVATE_FAILED;
  return TPLD_OK;
}

TPLD_Status TPLD2001_Load(uint8_t *found_address)
{
  return TPLD2001_LoadImage(tpld2001_image, TPLD2001_IMAGE_SIZE,
                            found_address);
}
