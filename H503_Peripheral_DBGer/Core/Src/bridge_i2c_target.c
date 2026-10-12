#include "bridge_i2c_target.h"
#include "bridge_registers.h"
#include "bridge_eeprom.h"
#include "bridge_metrics.h"
#include "stm32h5xx_hal.h"

#define SDA GPIO_PIN_5
#define SCL GPIO_PIN_6

typedef enum {
  WAIT_START, RECEIVE, ACK_FALL, ACK_RISE, ACK_RELEASE,
  SEND, MASTER_ACK_FALL, MASTER_ACK_RISE, MASTER_ACK_RELEASE, IGNORE
} Phase;

static volatile uint8_t active, address7;
static volatile Phase phase, after_ack;
static volatile uint8_t bits, shift, read_direction, pointer,
                        first_write_byte, acknowledge, send_byte;
static volatile uint8_t profile, word_offset, write_high;
static volatile uint16_t eeprom_pointer;
static volatile uint8_t eeprom_address_high, eeprom_address_count, waiting_cache;
static uint32_t stretch_tick;
static volatile uint16_t *words;

int BridgeI2cTarget_GetWord(uint8_t reg, uint16_t *value)
{
  if (profile != 1U || !value) return 0;
  *value = words[reg];
  return 1;
}

int BridgeI2cTarget_SetProfileWord(uint8_t selected, uint8_t reg, uint16_t value)
{
  if (selected > 2U || (selected == 1U && reg != 0U && reg != 1U && reg != 2U) ||
      (selected == 2U && (reg != 0U || value != 0U)))
    return 0;
  HAL_NVIC_DisableIRQ(EXTI5_IRQn);
  HAL_NVIC_DisableIRQ(EXTI6_IRQn);
  if (selected == 1U && (profile != 1U || !words)) {
    BridgeEeprom_Release();
    words = BridgeMemory_Allocate(512U);
    if (!words) goto fail;
  } else if (selected == 2U && !BridgeEeprom_Ready()) {
    words = 0;
    if (!BridgeEeprom_Config(8U, 8U)) goto fail;
  }
  if (selected != profile && selected == 1U) {
    words[0] = 0x64A0U; /* 24.88 C */
    words[1] = 0x8000U; /* 50% RH */
    words[2] = 0x1000U;
    words[0xFE] = 0x5449U;
    words[0xFF] = 0x1050U;
  }
  profile = selected;
  if (selected == 0U) { BridgeEeprom_Release(); words = 0; }
  if (selected == 1U) words[reg] = reg < 2U ? (value & 0xFFFCU) : value;
  if (active) {
    HAL_NVIC_EnableIRQ(EXTI5_IRQn);
    HAL_NVIC_EnableIRQ(EXTI6_IRQn);
  }
  return 1;
fail:
  if (active) {
    HAL_NVIC_EnableIRQ(EXTI5_IRQn);
    HAL_NVIC_EnableIRQ(EXTI6_IRQn);
  }
  return 0;
}

static void release_sda(void) { GPIOB->BSRR = SDA; }
static void drive_sda(uint8_t bit)
{ GPIOB->BSRR = bit ? SDA : ((uint32_t)SDA << 16U); }
static uint8_t sample_sda(void) { return (GPIOB->IDR & SDA) != 0U; }

uint8_t BridgeI2cTarget_Active(void) { return active; }
uint8_t BridgeI2cTarget_Address(void) { return active ? address7 : 0U; }

int BridgeI2cTarget_Config(uint8_t enable, uint8_t address)
{
  GPIO_InitTypeDef gpio = {0};
  if (enable > 1U || (enable && (address < 0x08U || address > 0x77U)))
    return 0;
  HAL_NVIC_DisableIRQ(EXTI5_IRQn);
  HAL_NVIC_DisableIRQ(EXTI6_IRQn);
  active = 0U;
  waiting_cache = 0U;
  profile = 0U; /* Legacy target configuration always selects flat registers. */
  BridgeEeprom_Release();
  words = 0;
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIOB->BSRR = SDA | SCL;
  if (!enable) {
    HAL_GPIO_WritePin(GPIOB, SDA | SCL, GPIO_PIN_SET);
    gpio.Pin = SDA | SCL;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
    return 1;
  }
  address7 = address;
  phase = WAIT_START;
  pointer = 0U;
  word_offset = 0U;
  release_sda();
  gpio.Pin = SDA | SCL;
  gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);
  /* Keep EXTI edge routing but enable the PB5 open-drain output driver. */
  GPIOB->OTYPER |= SDA | SCL;
  GPIOB->MODER = (GPIOB->MODER & ~((3UL << (5U * 2U)) |
                                     (3UL << (6U * 2U)))) |
                  (1UL << (5U * 2U)) | (1UL << (6U * 2U));
  release_sda();
  __HAL_GPIO_EXTI_CLEAR_RISING_IT(SDA | SCL);
  __HAL_GPIO_EXTI_CLEAR_FALLING_IT(SDA | SCL);
  HAL_NVIC_SetPriority(EXTI5_IRQn, 3U, 0U);
  HAL_NVIC_SetPriority(EXTI6_IRQn, 3U, 0U);
  active = 1U;
  HAL_NVIC_EnableIRQ(EXTI5_IRQn);
  HAL_NVIC_EnableIRQ(EXTI6_IRQn);
  return 1;
}

static void start_condition(void)
{
  release_sda();
  phase = RECEIVE;
  bits = 0U;
  shift = 0U;
  read_direction = 0U;
}

static void scl_rising(void)
{
  if (phase == RECEIVE) {
    shift = (uint8_t)((shift << 1U) | sample_sda());
    if (++bits == 8U) {
      if (read_direction == 0U) {
        acknowledge = (uint8_t)((shift >> 1U) == address7 &&
                                 (profile != 2U || !BridgeEeprom_Busy()));
        read_direction = (uint8_t)((shift & 1U) ? 2U : 1U);
        first_write_byte = 1U;
        eeprom_address_count = 0U;
        after_ack = acknowledge ?
                    (read_direction == 2U ? SEND : RECEIVE) : IGNORE;
      } else {
        if (first_write_byte) {
          if (profile == 2U && BridgeEeprom_AddressBytes() == 2U &&
              eeprom_address_count == 0U) {
            eeprom_address_high = shift;
            eeprom_address_count = 1U;
          } else {
            if (profile == 2U) {
              eeprom_pointer = BridgeEeprom_NormalizeAddress(
                  (uint16_t)(((uint16_t)eeprom_address_high << 8U) | shift));
              if (BridgeEeprom_AddressBytes() == 1U) eeprom_pointer = shift;
              BridgeEeprom_BeginWrite(eeprom_pointer);
            } else pointer = shift;
            word_offset = 0U;
            first_write_byte = 0U;
          }
        } else {
          uint8_t value = shift;
          if (profile == 2U) {
            BridgeEeprom_WriteByte(value);
            eeprom_pointer = BridgeEeprom_PageNext(eeprom_pointer);
          } else if (profile == 1U) {
            if (pointer == 2U) {
              if (word_offset == 0U) write_high = value;
              else words[2] = (uint16_t)(((uint16_t)write_high << 8U) | value);
              word_offset ^= 1U;
            }
          } else {
            (void)BridgeRegs_Write(5U, pointer++, &value, 1U);
          }
        }
        acknowledge = 1U;
        after_ack = RECEIVE;
      }
      phase = ACK_FALL;
    }
  } else if (phase == ACK_RISE) {
    phase = ACK_RELEASE;
  } else if (phase == SEND) {
    if (++bits == 8U) phase = MASTER_ACK_FALL;
  } else if (phase == MASTER_ACK_RISE) {
    if (profile == 2U) eeprom_pointer = BridgeEeprom_NextAddress(eeprom_pointer);
    after_ack = sample_sda() ? IGNORE : SEND;
    phase = MASTER_ACK_RELEASE;
  }
}

static void prepare_send(void)
{
  uint8_t value = 0U;
  if (profile == 2U) {
    if (!BridgeEeprom_ReadAt(eeprom_pointer, &value)) {
      waiting_cache = 1U;
      stretch_tick = HAL_GetTick();
      GPIOB->BSRR = (uint32_t)SCL << 16U;
      return;
    }
    waiting_cache = 0U;
  }
  else if (profile == 1U)
    value = word_offset ? (uint8_t)words[pointer] : (uint8_t)(words[pointer] >> 8U);
  else
    (void)BridgeRegs_Read(5U, pointer, &value, 1U);
  send_byte = value;
  bits = 0U;
  drive_sda((uint8_t)((send_byte & 0x80U) != 0U));
}

static void scl_falling(void)
{
  if (phase == ACK_FALL) {
    drive_sda(!acknowledge);
    phase = ACK_RISE;
  } else if (phase == ACK_RELEASE) {
    release_sda();
    phase = after_ack;
    bits = 0U;
    shift = 0U;
    if (phase == SEND) prepare_send();
  } else if (phase == SEND && bits < 8U) {
    drive_sda((uint8_t)((send_byte & (0x80U >> bits)) != 0U));
  } else if (phase == MASTER_ACK_FALL) {
    release_sda();
    phase = MASTER_ACK_RISE;
  } else if (phase == MASTER_ACK_RELEASE) {
    phase = after_ack;
    if (phase == SEND) {
      if (profile == 2U) {
        /* Already advanced after the controller sampled this byte. */
      } else if (profile == 1U) {
        word_offset ^= 1U;
        if (word_offset == 0U && pointer == 0U && (words[2] & 0x1000U)) pointer = 1U;
      } else ++pointer;
      prepare_send();
    }
  }
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
  if (!active) return;
  if (pin == SCL) scl_rising();
  else if (pin == SDA && (GPIOB->IDR & SCL)) {
    if (profile == 2U) BridgeEeprom_Stop();
    release_sda();
    phase = WAIT_START;
  }
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin)
{
  if (!active) return;
  if (pin == SCL) scl_falling();
  else if (pin == SDA && (GPIOB->IDR & SCL)) start_condition();
}

void BridgeI2cTarget_IrqSda(void) { HAL_GPIO_EXTI_IRQHandler(SDA); }
void BridgeI2cTarget_IrqScl(void) { HAL_GPIO_EXTI_IRQHandler(SCL); }
void BridgeI2cTarget_Poll(void)
{
  uint8_t value;
  if (!active || !waiting_cache) return;
  if (HAL_GetTick() - stretch_tick > 200U) BridgeEeprom_AbortRead();
  if (!BridgeEeprom_ReadAt(eeprom_pointer, &value)) return;
  HAL_NVIC_DisableIRQ(EXTI5_IRQn);
  HAL_NVIC_DisableIRQ(EXTI6_IRQn);
  if (waiting_cache) {
    prepare_send();
    /* Preparing bit 7 may toggle SDA while SCL is held low. EXTI5 was
     * masked, so discard that edge before SCL rises; otherwise the delayed
     * callback sees SCL high and mistakes it for a new START condition. */
    __HAL_GPIO_EXTI_CLEAR_RISING_IT(SDA);
    __HAL_GPIO_EXTI_CLEAR_FALLING_IT(SDA);
    GPIOB->BSRR = SCL; /* Release stretch after first data bit is valid. */
  }
  HAL_NVIC_EnableIRQ(EXTI5_IRQn);
  HAL_NVIC_EnableIRQ(EXTI6_IRQn);
}
