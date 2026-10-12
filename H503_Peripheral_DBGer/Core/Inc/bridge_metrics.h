#ifndef BRIDGE_METRICS_H
#define BRIDGE_METRICS_H
#include <stdint.h>

void BridgeMetrics_Init(void);
void *BridgeMemory_Allocate(uint16_t size);
void BridgeMemory_Release(void);
void BridgeMetrics_PollBegin(void);
void BridgeMetrics_PollEnd(void);
void BridgeMetrics_Idle(void);
void BridgeMetrics_Get(uint16_t *static_bytes, uint16_t *allocated_bytes,
                       uint16_t *available_bytes, uint16_t *stack_reserve,
                       uint8_t *cpu_percent);
#endif
