#ifndef BRIDGE_PERIPHERAL_H
#define BRIDGE_PERIPHERAL_H
#include <stdint.h>

int BridgeSpi_Config(uint8_t role, uint8_t mode, uint8_t divider);
int BridgeSpi_Disable(void);
int BridgeSpi_Transfer(const uint8_t *tx, uint8_t *rx, uint8_t size);
void BridgeSpi_IrqHandler(void);
void BridgeSpi_GetStatus(uint8_t *ready, uint8_t *role, uint32_t *error);
/* SPI slave uses fixed 37-byte requests and returns the previous reply in the
 * next 37-byte transaction; host can preset bank 3 via register commands. */
int BridgeQep_Start(void);
int BridgeQep_Disable(void);
int BridgeQep_Read(int32_t *position, uint8_t *index_level, uint8_t *direction);
int BridgeQep_Reset(void);
void BridgeQep_Poll(void);

#endif
