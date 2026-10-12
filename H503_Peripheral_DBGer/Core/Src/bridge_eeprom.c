#include "bridge_eeprom.h"
#include "stm32h5xx_hal.h"
#include "bridge_metrics.h"
#include <string.h>

static uint8_t *memory, *page_buffer, *page_mask, *cache;
static uint8_t page_size, capacity_log2, offset, cache_valid;
static uint8_t dirty, pending, error, event_kind, event_sequence;
static uint16_t page, cache_page;
static uint32_t busy_until;

void BridgeEeprom_Init(void)
{
  BridgeMemory_Release();
  memory = page_buffer = page_mask = cache = 0;
  page_size = 8U;
  capacity_log2 = 8U;
  dirty = pending = error = event_kind = cache_valid = 0U;
  event_sequence = 0U;
  busy_until = 0U;
}

void BridgeEeprom_Release(void) { BridgeEeprom_Init(); }
uint8_t BridgeEeprom_Ready(void) { return page_buffer != 0; }

int BridgeEeprom_Config(uint8_t bits, uint8_t size)
{
  uint8_t *block;
  uint16_t needed;
  if (!((bits == 8U && size == 8U) ||
        ((bits == 14U || bits == 15U) && size == 64U) ||
        (bits == 16U && size == 128U))) return 0;
  BridgeEeprom_Init();
  needed = bits == 8U ? 264U : (uint16_t)(2U * size + 16U);
  block = BridgeMemory_Allocate(needed);
  if (!block) return 0;
  capacity_log2 = bits;
  page_size = size;
  if (bits == 8U) {
    memory = block;
    page_buffer = block + 256U;
    memset(memory, 0xFF, 256U);
  } else {
    page_buffer = block;
    page_mask = block + size;
    cache = block + size + 16U;
  }
  return 1;
}

uint8_t BridgeEeprom_AddressBytes(void) { return capacity_log2 == 8U ? 1U : 2U; }
uint16_t BridgeEeprom_NormalizeAddress(uint16_t address)
{
  return capacity_log2 == 16U ? address :
         (uint16_t)(address & ((1UL << capacity_log2) - 1U));
}
uint16_t BridgeEeprom_PageNext(uint16_t address)
{
  return (uint16_t)((address & ~(uint16_t)(page_size - 1U)) |
                    ((address + 1U) & (page_size - 1U)));
}
uint16_t BridgeEeprom_NextAddress(uint16_t address)
{
  if (capacity_log2 == 16U) return (uint16_t)(address + 1U);
  return (uint16_t)((address + 1U) & ((1UL << capacity_log2) - 1U));
}

void BridgeEeprom_BeginWrite(uint16_t address)
{
  page = address / page_size;
  offset = (uint8_t)(address % page_size);
  if (capacity_log2 == 8U)
    memcpy(page_buffer, memory + page * 8U, 8U);
  else {
    memset(page_buffer, 0, page_size);
    memset(page_mask, 0, 16U);
  }
  dirty = 0U;
}

void BridgeEeprom_WriteByte(uint8_t value)
{
  page_buffer[offset] = value;
  if (capacity_log2 != 8U)
    page_mask[offset >> 3] |= (uint8_t)(1U << (offset & 7U));
  offset = (uint8_t)((offset + 1U) % page_size);
  dirty = 1U;
}

void BridgeEeprom_Stop(void)
{
  if (!dirty) return;
  dirty = 0U;
  busy_until = HAL_GetTick() + 5U;
  if (capacity_log2 == 8U) pending = 1U;
  else { event_kind = 2U; ++event_sequence; }
}

void BridgeEeprom_Poll(void)
{
  if (!pending) return;
  memcpy(memory + page * 8U, page_buffer, 8U);
  pending = 0U;
}

uint8_t BridgeEeprom_Busy(void)
{
  return pending || error || event_kind == 2U ||
         (int32_t)(HAL_GetTick() - busy_until) < 0;
}

int BridgeEeprom_ReadAt(uint16_t address, uint8_t *value)
{
  uint16_t selected;
  if (!value || !page_buffer) return 0;
  if (capacity_log2 == 8U) { *value = memory[(uint8_t)address]; return 1; }
  selected = address / page_size;
  if (cache_valid && cache_page == selected) {
    *value = cache[address % page_size];
    return 1;
  }
  if (!event_kind) {
    page = selected;
    event_kind = 1U;
    ++event_sequence;
  }
  return 0;
}

uint8_t BridgeEeprom_Event(uint8_t *out)
{
  if (!out) return 0U;
  out[0] = event_kind;
  out[1] = event_sequence;
  out[2] = (uint8_t)page;
  out[3] = (uint8_t)(page >> 8);
  out[4] = page_size;
  if (event_kind == 2U) {
    memcpy(out + 5U, page_mask, 16U);
    memcpy(out + 21U, page_buffer, page_size);
    return (uint8_t)(21U + page_size);
  }
  return 5U;
}

int BridgeEeprom_Reply(const uint8_t *payload, uint16_t size)
{
  uint16_t selected;
  if (!payload || size < 4U || payload[0] != event_kind ||
      payload[1] != event_sequence || !event_kind) return 0;
  selected = (uint16_t)payload[2] | ((uint16_t)payload[3] << 8);
  if (selected != page) return 0;
  if (event_kind == 1U) {
    if (size != (uint16_t)(4U + page_size)) return 0;
    memcpy(cache, payload + 4U, page_size);
    cache_page = page;
    cache_valid = 1U;
  } else {
    if (size != 4U) return 0;
    if (cache_valid && cache_page == page)
      for (uint8_t i = 0U; i < page_size; ++i)
        if (page_mask[i >> 3] & (1U << (i & 7U))) cache[i] = page_buffer[i];
    busy_until = HAL_GetTick() + 5U;
  }
  event_kind = 0U;
  return 1;
}

void BridgeEeprom_AbortRead(void)
{
  if (event_kind != 1U) return;
  memset(cache, 0xFF, page_size);
  cache_page = page;
  cache_valid = 1U;
  event_kind = 0U;
  error = 1U;
}

int BridgeEeprom_InvalidateCache(void)
{
  if (capacity_log2 == 8U || event_kind || pending) return 0;
  cache_valid = 0U;
  return 1;
}

int BridgeEeprom_LocalRead(uint8_t address, uint8_t *data, uint8_t count)
{
  if (capacity_log2 != 8U || !data || !count || count > 64U ||
      (uint16_t)address + count > 256U) return 0;
  memcpy(data, memory + address, count);
  return 1;
}

int BridgeEeprom_LocalWrite(uint8_t address, const uint8_t *data, uint8_t count)
{
  if (capacity_log2 != 8U || !data || !count || count > 32U ||
      (uint16_t)address + count > 256U || pending || error) return 0;
  pending = 1U;
  memcpy(memory + address, data, count);
  busy_until = HAL_GetTick() + 5U;
  pending = 0U;
  return 1;
}

uint8_t BridgeEeprom_Error(void) { return error; }
