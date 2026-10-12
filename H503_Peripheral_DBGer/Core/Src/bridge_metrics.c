#include "bridge_metrics.h"
#include "bridge_serial.h"
#include "stm32h5xx_hal.h"
#include <stdint.h>

extern uint8_t _end, _estack;
#define STACK_RESERVE 4096U
static uint16_t allocated;
static uint32_t poll_start, active_cycles, window_tick;
static uint8_t sampled_cpu;

void BridgeMetrics_Init(void)
{
  allocated = 0U;
  active_cycles = 0U;
  window_tick = HAL_GetTick();
  sampled_cpu = 0xFFU;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void *BridgeMemory_Allocate(uint16_t size)
{
  uintptr_t base = ((uintptr_t)&_end + 7U) & ~(uintptr_t)7U;
  uintptr_t limit = (uintptr_t)&_estack - STACK_RESERVE;
  uint16_t aligned = (uint16_t)((size + 7U) & ~7U);
  if (!size || base + aligned > limit) return 0;
  allocated = aligned;
  return (void *)base;
}

void BridgeMemory_Release(void) { allocated = 0U; }

void BridgeMetrics_PollBegin(void) { poll_start = DWT->CYCCNT; }
void BridgeMetrics_PollEnd(void) { active_cycles += DWT->CYCCNT - poll_start; }

void BridgeMetrics_Idle(void)
{
  /* The serial RX path is polled, so sleeping until the 1 ms SysTick would
   * overrun 115200-baud bytes. Keep polling whenever any UART is enabled. */
  if (!BridgeSerial_ReadyMask()) __WFI();
  uint32_t elapsed = HAL_GetTick() - window_tick;
  if (elapsed >= 1000U) {
    uint64_t possible = ((uint64_t)SystemCoreClock * elapsed) / 1000U;
    uint32_t percent = possible ? (uint32_t)((100ULL * active_cycles) / possible) : 0U;
    sampled_cpu = (uint8_t)(percent > 100U ? 100U : percent);
    active_cycles = 0U;
    window_tick = HAL_GetTick();
  }
}

void BridgeMetrics_Get(uint16_t *static_bytes, uint16_t *allocated_bytes,
                       uint16_t *available_bytes, uint16_t *stack_reserve,
                       uint8_t *cpu_percent)
{
  uint32_t base = (uint32_t)(uintptr_t)&_end - 0x20000000UL;
  uint32_t available = base + allocated + STACK_RESERVE < 32768U ?
                       32768U - base - allocated - STACK_RESERVE : 0U;
  *static_bytes = (uint16_t)base;
  *allocated_bytes = allocated;
  *available_bytes = (uint16_t)available;
  *stack_reserve = STACK_RESERVE;
  *cpu_percent = sampled_cpu;
}
