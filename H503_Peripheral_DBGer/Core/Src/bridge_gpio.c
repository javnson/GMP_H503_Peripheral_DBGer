#include "bridge_gpio.h"
#include "stm32h5xx_hal.h"
#include <string.h>

static ADC_HandleTypeDef adc;
static DAC_HandleTypeDef dac;
static uint8_t adc_ready, dac_ready;
static uint8_t modes[6];
static uint16_t values[6];

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
  RCC_PeriphCLKInitTypeDef clock = {0};
  if (hadc->Instance != ADC1) return;
  clock.PeriphClockSelection = RCC_PERIPHCLK_ADCDAC;
  clock.AdcDacClockSelection = RCC_ADCDACCLKSOURCE_HCLK;
  (void)HAL_RCCEx_PeriphCLKConfig(&clock);
  __HAL_RCC_ADC_CLK_ENABLE();
}

static int adc_init(void)
{
  if (adc_ready) return 1;
  memset(&adc, 0, sizeof(adc));
  adc.Instance = ADC1;
  adc.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV8;
  adc.Init.Resolution = ADC_RESOLUTION_12B;
  adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  adc.Init.ScanConvMode = ADC_SCAN_DISABLE;
  adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  adc.Init.LowPowerAutoWait = DISABLE;
  adc.Init.ContinuousConvMode = DISABLE;
  adc.Init.NbrOfConversion = 1;
  adc.Init.DiscontinuousConvMode = DISABLE;
  adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  adc.Init.DMAContinuousRequests = DISABLE;
  adc.Init.SamplingMode = ADC_SAMPLING_MODE_NORMAL;
  adc.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  adc.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&adc) != HAL_OK ||
      HAL_ADCEx_Calibration_Start(&adc, ADC_SINGLE_ENDED) != HAL_OK)
    return 0;
  adc_ready = 1U;
  return 1;
}

static int dac_init(void)
{
  DAC_ChannelConfTypeDef channel = {0};
  if (dac_ready) return 1;
  memset(&dac, 0, sizeof(dac));
  dac.Instance = DAC1;
  if (HAL_DAC_Init(&dac) != HAL_OK) return 0;
  channel.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_DISABLE;
  channel.DAC_DMADoubleDataMode = DISABLE;
  channel.DAC_SignedFormat = DISABLE;
  channel.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  channel.DAC_Trigger = DAC_TRIGGER_NONE;
  channel.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  channel.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
  channel.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&dac, &channel, DAC_CHANNEL_1) != HAL_OK ||
      HAL_DAC_ConfigChannel(&dac, &channel, DAC_CHANNEL_2) != HAL_OK)
    return 0;
  dac_ready = 1U;
  return 1;
}

void BridgeGpio_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
             GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio);
  memset(modes, 0, sizeof(modes));
  memset(values, 0, sizeof(values));
}

int BridgeGpio_Configure(uint8_t pin, uint8_t mode, uint8_t tpld_active)
{
  GPIO_InitTypeDef gpio = {0};
  uint32_t channel;
  if (pin > 5U || mode > BRIDGE_GPIO_DAC) return 0;
  /* PA2/PA3 have external ADC nets; PA4/PA5 share TPLD output nets. */
  if (mode == BRIDGE_GPIO_OUTPUT && pin > 1U) return 0;
  if (mode == BRIDGE_GPIO_ADC && pin > 3U) return 0;
  if (mode == BRIDGE_GPIO_DAC && pin < 4U) return 0;
  /* PA1 is wired to TPLD IO11: a retained volatile configuration can still
   * drive it after an MCU-only reset even when autoload was disabled. */
  if (pin == 1U && mode == BRIDGE_GPIO_OUTPUT) return 0;
  /* PA4/PA5 likewise share TPLD output nets. No board isolation control is
   * available, so the DAC mode remains unavailable. */
  if (pin >= 4U && mode == BRIDGE_GPIO_DAC) return 0;
  (void)tpld_active;
  if (modes[pin] == BRIDGE_GPIO_DAC && mode != BRIDGE_GPIO_DAC)
    (void)HAL_DAC_Stop(&dac, pin == 4U ? DAC_CHANNEL_1 : DAC_CHANNEL_2);
  if (mode == BRIDGE_GPIO_ADC && !adc_init()) return 0;
  if (mode == BRIDGE_GPIO_DAC && !dac_init()) return 0;
  gpio.Pin = 1UL << pin;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Mode = mode == BRIDGE_GPIO_INPUT ? GPIO_MODE_INPUT :
              mode == BRIDGE_GPIO_OUTPUT ?
                (pin == 0U ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT_PP) :
              GPIO_MODE_ANALOG;
  if (mode == BRIDGE_GPIO_OUTPUT) HAL_GPIO_WritePin(GPIOA, gpio.Pin, GPIO_PIN_RESET);
  HAL_GPIO_Init(GPIOA, &gpio);
  if (mode == BRIDGE_GPIO_DAC) {
    channel = pin == 4U ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
    if (HAL_DAC_SetValue(&dac, channel, DAC_ALIGN_12B_R, 0U) != HAL_OK ||
        HAL_DAC_Start(&dac, channel) != HAL_OK) return 0;
  }
  modes[pin] = mode;
  values[pin] = 0U;
  return 1;
}

int BridgeGpio_Set(uint8_t pin, uint16_t value)
{
  if (pin > 5U) return 0;
  if (modes[pin] == BRIDGE_GPIO_OUTPUT && value <= 1U) {
    HAL_GPIO_WritePin(GPIOA, 1UL << pin, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
    values[pin] = value;
    return 1;
  }
  if (modes[pin] == BRIDGE_GPIO_DAC && value <= 4095U) {
    uint32_t channel = pin == 4U ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
    if (HAL_DAC_SetValue(&dac, channel, DAC_ALIGN_12B_R, value) != HAL_OK)
      return 0;
    values[pin] = value;
    return 1;
  }
  return 0;
}

int BridgeGpio_Get(uint8_t pin, uint8_t *mode, uint16_t *value)
{
  ADC_ChannelConfTypeDef config = {0};
  static const uint32_t adc_channels[4] = {
    ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_14, ADC_CHANNEL_15
  };
  if (pin > 5U || mode == NULL || value == NULL) return 0;
  *mode = modes[pin];
  if (*mode == BRIDGE_GPIO_ADC) {
    config.Channel = adc_channels[pin];
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
    config.SingleDiff = ADC_SINGLE_ENDED;
    config.OffsetNumber = ADC_OFFSET_NONE;
    if (HAL_ADC_ConfigChannel(&adc, &config) != HAL_OK ||
        HAL_ADC_Start(&adc) != HAL_OK ||
        HAL_ADC_PollForConversion(&adc, 20U) != HAL_OK) return 0;
    *value = (uint16_t)HAL_ADC_GetValue(&adc);
    (void)HAL_ADC_Stop(&adc);
  } else if (*mode == BRIDGE_GPIO_DAC) {
    *value = values[pin];
  } else {
    *value = HAL_GPIO_ReadPin(GPIOA, 1UL << pin) == GPIO_PIN_SET;
  }
  return 1;
}
