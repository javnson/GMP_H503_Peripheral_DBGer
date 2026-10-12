#ifndef BRIDGE_CAN_H
#define BRIDGE_CAN_H
#include <stdint.h>

int BridgeCan_Config(uint32_t bitrate);
int BridgeCan_Disable(void);
int BridgeCan_Send(uint32_t identifier, uint8_t extended,
                   const uint8_t *data, uint8_t length);
int BridgeCan_Receive(uint32_t *identifier, uint8_t *extended,
                      uint8_t *data, uint8_t *length);
int BridgeCan_Status(uint8_t out[7]);
#endif
