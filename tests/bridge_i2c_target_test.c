#include "bridge_i2c_target.h"
#include "bridge_registers.h"
#include "stm32h5xx_hal.h"
#include <stdio.h>

#define CHECK(test) do { if (!(test)) { \
  fprintf(stderr, "line %d: %s\n", __LINE__, #test); return 1; \
} } while (0)

GPIO_TypeDef mock_gpio_b;
static int master_low;

void HAL_GPIO_EXTI_IRQHandler(uint16_t pin) { (void)pin; }

static void update_sda(void)
{
  int was_high = (mock_gpio_b.IDR & GPIO_PIN_5) != 0U;
  int target_low = mock_gpio_b.BSRR == ((uint32_t)GPIO_PIN_5 << 16U);
  int is_high = !master_low && !target_low;
  if (is_high) mock_gpio_b.IDR |= GPIO_PIN_5;
  else mock_gpio_b.IDR &= ~GPIO_PIN_5;
  if (is_high != was_high) {
    if (is_high) HAL_GPIO_EXTI_Rising_Callback(GPIO_PIN_5);
    else HAL_GPIO_EXTI_Falling_Callback(GPIO_PIN_5);
  }
}

static void set_scl(int high)
{
  if (high) {
    mock_gpio_b.IDR |= GPIO_PIN_6;
    HAL_GPIO_EXTI_Rising_Callback(GPIO_PIN_6);
  } else {
    mock_gpio_b.IDR &= ~GPIO_PIN_6;
    HAL_GPIO_EXTI_Falling_Callback(GPIO_PIN_6);
  }
  update_sda();
}

static void set_master_sda(int low) { master_low = low; update_sda(); }

static void start(void)
{
  set_master_sda(0);
  set_scl(1);
  set_master_sda(1);
  set_scl(0);
}

static int send_byte(uint8_t value)
{
  for (unsigned b = 0; b < 8; ++b) {
    set_master_sda((value & (0x80U >> b)) == 0U);
    set_scl(1); set_scl(0);
  }
  set_master_sda(0);
  set_scl(1);
  int ack = (mock_gpio_b.IDR & GPIO_PIN_5) == 0U;
  set_scl(0);
  return ack;
}

static uint8_t read_byte(void)
{
  uint8_t value = 0U;
  set_master_sda(0);
  for (unsigned b = 0; b < 8; ++b) {
    set_scl(1);
    value = (uint8_t)((value << 1U) | ((mock_gpio_b.IDR & GPIO_PIN_5) != 0U));
    set_scl(0);
  }
  /* NACK the only byte. */
  set_scl(1); set_scl(0);
  return value;
}

static void stop(void)
{
  set_master_sda(1);
  set_scl(1);
  set_master_sda(0);
}

int main(void)
{
  uint8_t value = 0;
  BridgeRegs_Init();
  mock_gpio_b.IDR = GPIO_PIN_5 | GPIO_PIN_6;
  mock_gpio_b.BSRR = GPIO_PIN_5;
  CHECK(BridgeI2cTarget_Config(1U, 0x42U));
  CHECK(BridgeI2cTarget_Active());
  start();
  CHECK(send_byte(0x84U));
  CHECK(send_byte(0x10U));
  CHECK(send_byte(0xABU));
  stop();
  CHECK(BridgeRegs_Read(5U, 0x10U, &value, 1U) && value == 0xABU);
  start();
  CHECK(send_byte(0x84U));
  CHECK(send_byte(0x10U));
  /* Repeated START, address + read. */
  start();
  CHECK(send_byte(0x85U));
  CHECK(read_byte() == 0xABU);
  stop();
  CHECK(BridgeI2cTarget_SetProfileWord(1U, 0U, 0x64A0U));
  CHECK(BridgeI2cTarget_SetProfileWord(1U, 1U, 0x8000U));
  start();
  CHECK(send_byte(0x84U));
  CHECK(send_byte(0xFEU));
  start();
  CHECK(send_byte(0x85U));
  CHECK(read_byte() == 0x54U);
  stop();
  start();
  CHECK(send_byte(0x84U));
  CHECK(send_byte(0x00U));
  start();
  CHECK(send_byte(0x85U));
  CHECK(read_byte() == 0x64U);
  stop();
  CHECK(!BridgeI2cTarget_SetProfileWord(1U, 0x03U, 0U));
  CHECK(BridgeI2cTarget_Config(1U, 0x42U));
  start();
  CHECK(send_byte(0x84U));
  CHECK(send_byte(0x10U));
  start();
  CHECK(send_byte(0x85U));
  CHECK(read_byte() == 0xABU);
  stop();
  CHECK(BridgeI2cTarget_Config(0U, 0U));
  CHECK(!BridgeI2cTarget_Active());
  return 0;
}
