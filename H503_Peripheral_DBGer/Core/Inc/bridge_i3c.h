#ifndef BRIDGE_I3C_H
#define BRIDGE_I3C_H
#include <stdint.h>

/* role 0=controller, 1=target. A target register request uses a fixed
 * 37-byte private write followed by a 37-byte private read. */
int BridgeI3c_Config(uint8_t role);
int BridgeI3c_Assign(uint8_t base_address, uint8_t maximum,
                     uint8_t *addresses, uint8_t *actual);
int BridgeI3c_Transfer(uint8_t address, const uint8_t *tx, uint8_t tx_length,
                       uint8_t *rx, uint8_t rx_length);
void BridgeI3c_EventIrq(void);
void BridgeI3c_ErrorIrq(void);
void BridgeI3c_Poll(void);
void BridgeI3c_GetStatus(uint8_t *ready, uint8_t *role, uint32_t *error);
#endif
