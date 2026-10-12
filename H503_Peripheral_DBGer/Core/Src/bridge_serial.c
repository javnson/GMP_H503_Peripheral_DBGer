#include "bridge_serial.h"
#include "bridge_registers.h"
#include "stm32h5xx_hal.h"
#include <string.h>

#define SERIAL_PORTS 3U
#define RX_CAPACITY 128U
#define MAX_REGISTER_TRANSFER BRIDGE_REG_MAX_TRANSFER

typedef struct {
  UART_HandleTypeDef uart;
  uint8_t ready, role;
  uint8_t ring[RX_CAPACITY], head, tail;
  uint8_t frame[4U + MAX_REGISTER_TRANSFER + 1U];
  uint8_t frame_size;
  uint32_t last_byte_tick;
} SerialPort;

static SerialPort ports[SERIAL_PORTS];

static int send_bytes(uint8_t port, uint8_t *data, uint8_t size)
{
  HAL_StatusTypeDef result;
  if (port == 2U) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
    for (volatile uint32_t n = 0; n < 100U; ++n) __NOP();
  }
  result = HAL_UART_Transmit(&ports[port].uart, data, size, 1000U);
  if (port == 2U) HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
  return result == HAL_OK;
}

int BridgeSerial_Config(uint8_t port, uint8_t role, uint32_t baud)
{
  static USART_TypeDef *const instance[SERIAL_PORTS] = {
    USART3, USART1, USART2
  };
  SerialPort *p;
  if (port >= SERIAL_PORTS || role > 1U || baud < 1200U || baud > 2000000U)
    return 0;
  p = &ports[port];
  if (p->ready) (void)HAL_UART_DeInit(&p->uart);
  p->ready = 0U;
  memset(&p->uart, 0, sizeof(p->uart));
  p->uart.Instance = instance[port];
  p->uart.Init.BaudRate = baud;
  p->uart.Init.WordLength = UART_WORDLENGTH_8B;
  p->uart.Init.StopBits = UART_STOPBITS_1;
  p->uart.Init.Parity = UART_PARITY_NONE;
  p->uart.Init.Mode = UART_MODE_TX_RX;
  p->uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  p->uart.Init.OverSampling = UART_OVERSAMPLING_16;
  p->uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  p->uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  p->uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&p->uart) != HAL_OK ||
      HAL_UARTEx_DisableFifoMode(&p->uart) != HAL_OK) return 0;
  p->role = role;
  p->head = p->tail = p->frame_size = 0U;
  p->ready = 1U;
  return 1;
}

int BridgeSerial_Disable(uint8_t port)
{
  SerialPort *p;
  if (port >= SERIAL_PORTS) return 0;
  p = &ports[port];
  if (!p->ready) return 1;
  p->ready = 0U;
  if (port == 2U) HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
  if (HAL_UART_DeInit(&p->uart) != HAL_OK) return 0;
  p->head = p->tail = p->frame_size = 0U;
  return 1;
}

int BridgeSerial_Write(uint8_t port, const uint8_t *data, uint8_t size)
{
  if (port >= SERIAL_PORTS || !ports[port].ready || data == NULL ||
      !size || size > 64U) return 0;
  return send_bytes(port, (uint8_t *)data, size);
}

int BridgeSerial_Read(uint8_t port, uint8_t *data, uint8_t capacity,
                      uint8_t *actual)
{
  SerialPort *p;
  if (port >= SERIAL_PORTS || data == NULL || actual == NULL || capacity > 64U)
    return 0;
  p = &ports[port];
  if (!p->ready || p->role != 0U) return 0;
  *actual = 0U;
  while (*actual < capacity && p->tail != p->head) {
    data[(*actual)++] = p->ring[p->tail];
    p->tail = (uint8_t)((p->tail + 1U) & (RX_CAPACITY - 1U));
  }
  return 1;
}

int BridgeSerial_RegWrite(uint8_t port, uint8_t address,
                          const uint8_t *data, uint8_t size)
{
  return port < SERIAL_PORTS && BridgeRegs_Write(port, address, data, size);
}

int BridgeSerial_RegRead(uint8_t port, uint8_t address,
                         uint8_t *data, uint8_t size)
{
  return port < SERIAL_PORTS && BridgeRegs_Read(port, address, data, size);
}

static void process_register_frame(uint8_t port)
{
  SerialPort *p = &ports[port];
  uint8_t reply[3U + MAX_REGISTER_TRANSFER + 1U], reply_size = 0U;
  if (BridgeRegs_Execute(port, p->frame, p->frame_size, reply, &reply_size))
    (void)send_bytes(port, reply, reply_size);
  p->frame_size = 0U;
}

static void receive_register_byte(uint8_t port, uint8_t byte)
{
  SerialPort *p = &ports[port];
  if (p->frame_size && HAL_GetTick() - p->last_byte_tick > 100U)
    p->frame_size = 0U;
  p->last_byte_tick = HAL_GetTick();
  if (!p->frame_size && byte != 0xA5U) return;
  p->frame[p->frame_size++] = byte;
  if (p->frame_size == 4U &&
      (p->frame[3] == 0U || p->frame[3] > MAX_REGISTER_TRANSFER)) {
    p->frame_size = 0U;
    return;
  }
  if (p->frame_size >= 4U) {
    uint8_t expected = (uint8_t)(5U + (p->frame[1] == 1U ? p->frame[3] : 0U));
    if (p->frame_size == expected) process_register_frame(port);
  }
}

void BridgeSerial_Poll(void)
{
  for (uint8_t port = 0U; port < SERIAL_PORTS; ++port) {
    SerialPort *p = &ports[port];
    if (!p->ready) continue;
    if (__HAL_UART_GET_FLAG(&p->uart, UART_FLAG_ORE))
      __HAL_UART_CLEAR_OREFLAG(&p->uart);
    while (__HAL_UART_GET_FLAG(&p->uart, UART_FLAG_RXNE)) {
      uint8_t byte = (uint8_t)p->uart.Instance->RDR;
      if (p->role == 1U) receive_register_byte(port, byte);
      else {
        uint8_t next = (uint8_t)((p->head + 1U) & (RX_CAPACITY - 1U));
        if (next != p->tail) { p->ring[p->head] = byte; p->head = next; }
      }
    }
  }
}

uint8_t BridgeSerial_ReadyMask(void)
{
  uint8_t mask = 0U;
  for (uint8_t i = 0U; i < SERIAL_PORTS; ++i)
    if (ports[i].ready) mask |= (uint8_t)(1U << i);
  return mask;
}
