#include "bridge_peripheral.h"
#include "bridge_registers.h"
#include "stm32h5xx_hal.h"
#include <string.h>

static SPI_HandleTypeDef spi;
static TIM_HandleTypeDef qep;
static uint8_t spi_ready, spi_role, spi_mode, qep_ready;
static uint8_t spi_slave_rx[BRIDGE_REG_FRAME_SIZE];
static uint8_t spi_slave_tx[BRIDGE_REG_FRAME_SIZE];
static int32_t qep_position;
static uint16_t qep_previous;

static void spi_gpio_delay(void)
{
  for (volatile uint32_t n = 0U; n < 100U; ++n) __NOP();
}

int BridgeSpi_Config(uint8_t role, uint8_t mode, uint8_t divider)
{
  static const uint32_t baud[] = {
    SPI_BAUDRATEPRESCALER_2, SPI_BAUDRATEPRESCALER_4,
    SPI_BAUDRATEPRESCALER_8, SPI_BAUDRATEPRESCALER_16,
    SPI_BAUDRATEPRESCALER_32, SPI_BAUDRATEPRESCALER_64,
    SPI_BAUDRATEPRESCALER_128, SPI_BAUDRATEPRESCALER_256
  };
  GPIO_InitTypeDef select = {0};
  if (role > 1U || mode > 3U || divider > 7U) return 0;
  if (spi.Instance == SPI2 && spi.State != HAL_SPI_STATE_RESET) {
    HAL_NVIC_DisableIRQ(SPI2_IRQn);
    (void)HAL_SPI_DeInit(&spi);
  }
  spi_ready = 0U;
  memset(&spi, 0, sizeof(spi));
  spi.Instance = SPI2;
  spi.Init.Mode = role == 0U ? SPI_MODE_MASTER : SPI_MODE_SLAVE;
  spi.Init.Direction = SPI_DIRECTION_2LINES;
  spi.Init.DataSize = SPI_DATASIZE_8BIT;
  spi.Init.CLKPolarity = (mode & 2U) ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
  spi.Init.CLKPhase = (mode & 1U) ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
  spi.Init.NSS = role == 0U ? SPI_NSS_SOFT : SPI_NSS_HARD_INPUT;
  spi.Init.BaudRatePrescaler = baud[divider];
  spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
  spi.Init.TIMode = SPI_TIMODE_DISABLE;
  spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  spi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  spi.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  /* Refill the slave TX FIFO while eight slots remain; a one-byte threshold
   * leaves too little time between clocked bytes during continuous frames. */
  spi.Init.FifoThreshold = SPI_FIFO_THRESHOLD_08DATA;
  spi.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  /* The target is serviced by interrupt; leave time to refill its FIFO. */
  spi.Init.MasterInterDataIdleness = role == 0U ?
      SPI_MASTER_INTERDATA_IDLENESS_15CYCLE :
      SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  spi.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  spi.Init.MasterKeepIOState = role == 0U ?
      SPI_MASTER_KEEP_IO_STATE_ENABLE : SPI_MASTER_KEEP_IO_STATE_DISABLE;
  /* The board-to-board harness crosses MOSI/MISO. Swap the target's pins so
   * the controller output reaches the target input and vice versa. */
  spi.Init.IOSwap = role == 1U ? SPI_IO_SWAP_ENABLE : SPI_IO_SWAP_DISABLE;
  spi.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  spi.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
  if (HAL_SPI_Init(&spi) != HAL_OK) return 0;
  __HAL_RCC_GPIOA_CLK_ENABLE();
  select.Pin = GPIO_PIN_8;
  select.Pull = GPIO_NOPULL;
  select.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  if (role == 0U) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
    select.Mode = GPIO_MODE_OUTPUT_PP;
  } else {
    select.Mode = GPIO_MODE_AF_PP;
    select.Alternate = GPIO_AF11_SPI2;
  }
  HAL_GPIO_Init(GPIOA, &select);
  if (role == 0U && mode != 0U) {
    GPIO_InitTypeDef io = {0};
    /* On this harness the hardware controller loses bit phase during FIFO
     * refill in nonzero modes. Use a slow GPIO clock for Modes 1/2/3. */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2,
                      (mode & 2U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);
    io.Pin = GPIO_PIN_2 | GPIO_PIN_15;
    io.Mode = GPIO_MODE_OUTPUT_PP;
    io.Pull = GPIO_NOPULL;
    io.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOB, &io);
    io.Pin = GPIO_PIN_14;
    io.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(GPIOB, &io);
  }
  spi_role = role;
  spi_mode = mode;
  spi_ready = 1U;
  if (role == 1U) {
    memset(spi_slave_tx, 0, sizeof(spi_slave_tx));
    spi_slave_tx[0] = 0x5AU;
    spi_slave_tx[1] = 2U; /* no prior request */
    /* CRC8 over 5A 02 00 */
    spi_slave_tx[3] = 0x89U;
    if (HAL_SPI_TransmitReceive_IT(&spi, spi_slave_tx, spi_slave_rx,
                                   BRIDGE_REG_FRAME_SIZE) != HAL_OK) {
      spi_ready = 0U;
      return 0;
    }
  }
  return 1;
}

int BridgeSpi_Disable(void)
{
  GPIO_InitTypeDef gpio = {0};
  if (!spi_ready) return 1;
  spi_ready = 0U;
  HAL_NVIC_DisableIRQ(SPI2_IRQn);
  if (HAL_SPI_DeInit(&spi) != HAL_OK) return 0;
  gpio.Pin = GPIO_PIN_8;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_2 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOB, &gpio);
  return 1;
}

int BridgeSpi_Transfer(const uint8_t *tx, uint8_t *rx, uint8_t size)
{
  HAL_StatusTypeDef status;
  if (!spi_ready || spi_role != 0U || tx == NULL || rx == NULL ||
      !size || size > 64U) return 0;
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
  for (volatile uint32_t n = 0U; n < 1000U; ++n) __NOP();
  if (spi_mode != 0U) {
    const GPIO_PinState idle = (spi_mode & 2U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    const GPIO_PinState active = idle == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET;
    for (uint8_t i = 0U; i < size; ++i) {
      uint8_t input = 0U;
      for (uint8_t bit = 0U; bit < 8U; ++bit) {
        const GPIO_PinState output =
            (tx[i] & (0x80U >> bit)) ? GPIO_PIN_SET : GPIO_PIN_RESET;
        if ((spi_mode & 1U) == 0U)
          HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, output);
        spi_gpio_delay();
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, active);
        if (spi_mode & 1U)
          HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, output);
        spi_gpio_delay();
        if ((spi_mode & 1U) == 0U)
          input = (uint8_t)((input << 1) |
                            (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14) == GPIO_PIN_SET));
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, idle);
        spi_gpio_delay();
        if (spi_mode & 1U)
          input = (uint8_t)((input << 1) |
                            (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14) == GPIO_PIN_SET));
      }
      rx[i] = input;
    }
    for (volatile uint32_t n = 0U; n < 1000U; ++n) __NOP();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
    return 1;
  }
  status = HAL_SPI_TransmitReceive(&spi, (uint8_t *)tx, rx, size, 100U);
  for (volatile uint32_t n = 0U; n < 1000U; ++n) __NOP();
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
  return status == HAL_OK;
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *handle)
{
  uint8_t length, reply_size = 0U;
  if (handle != &spi || !spi_ready || spi_role != 1U) return;
  memset(spi_slave_tx, 0, sizeof(spi_slave_tx));
  length = spi_slave_rx[1] == 1U ?
           (uint8_t)(5U + spi_slave_rx[3]) : 5U;
  if (length > BRIDGE_REG_FRAME_SIZE ||
      !BridgeRegs_Execute(3U, spi_slave_rx, length,
                          spi_slave_tx, &reply_size)) {
    spi_slave_tx[0] = 0x5AU;
    spi_slave_tx[1] = 1U;
    spi_slave_tx[2] = 0U;
    spi_slave_tx[3] = 0xB6U;
  }
  if (HAL_SPI_TransmitReceive_IT(&spi, spi_slave_tx, spi_slave_rx,
                                 BRIDGE_REG_FRAME_SIZE) != HAL_OK)
    spi_ready = 0U;
}

void BridgeSpi_IrqHandler(void) { HAL_SPI_IRQHandler(&spi); }

void BridgeSpi_GetStatus(uint8_t *is_ready, uint8_t *role, uint32_t *error)
{
  *is_ready = spi_ready;
  *role = spi_role;
  *error = spi.Instance == SPI2 ? HAL_SPI_GetError(&spi) : 0U;
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *handle)
{
  if (handle == &spi) spi_ready = 0U;
}

int BridgeQep_Start(void)
{
  TIM_Encoder_InitTypeDef encoder = {0};
  if (qep_ready) return 1;
  memset(&qep, 0, sizeof(qep));
  qep.Instance = TIM3;
  qep.Init.Prescaler = 0U;
  qep.Init.CounterMode = TIM_COUNTERMODE_UP;
  qep.Init.Period = 65535U;
  qep.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  qep.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  encoder.EncoderMode = TIM_ENCODERMODE_TI12;
  encoder.IC1Polarity = TIM_ICPOLARITY_RISING;
  encoder.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  encoder.IC1Prescaler = TIM_ICPSC_DIV1;
  encoder.IC1Filter = 2U;
  encoder.IC2Polarity = TIM_ICPOLARITY_RISING;
  encoder.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  encoder.IC2Prescaler = TIM_ICPSC_DIV1;
  encoder.IC2Filter = 2U;
  if (HAL_TIM_Encoder_Init(&qep, &encoder) != HAL_OK ||
      HAL_TIM_Encoder_Start(&qep, TIM_CHANNEL_ALL) != HAL_OK) return 0;
  __HAL_TIM_SET_COUNTER(&qep, 0U);
  qep_position = 0;
  qep_previous = 0;
  qep_ready = 1U;
  return 1;
}

int BridgeQep_Disable(void)
{
  if (!qep_ready) return 1;
  qep_ready = 0U;
  if (HAL_TIM_Encoder_Stop(&qep, TIM_CHANNEL_ALL) != HAL_OK) return 0;
  return HAL_TIM_Encoder_DeInit(&qep) == HAL_OK;
}

void BridgeQep_Poll(void)
{
  uint16_t count;
  if (!qep_ready) return;
  count = (uint16_t)__HAL_TIM_GET_COUNTER(&qep);
  qep_position += (int16_t)(count - qep_previous);
  qep_previous = count;
}

int BridgeQep_Read(int32_t *position, uint8_t *index_level, uint8_t *direction)
{
  if (!qep_ready || position == NULL || index_level == NULL || direction == NULL)
    return 0;
  BridgeQep_Poll();
  *position = qep_position;
  *index_level = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET;
  *direction = __HAL_TIM_IS_TIM_COUNTING_DOWN(&qep) ? 1U : 0U;
  return 1;
}

int BridgeQep_Reset(void)
{
  if (!qep_ready) return 0;
  __HAL_TIM_SET_COUNTER(&qep, 0U);
  qep_position = 0;
  qep_previous = 0;
  return 1;
}
