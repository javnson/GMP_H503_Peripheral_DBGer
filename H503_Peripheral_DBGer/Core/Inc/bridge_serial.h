#ifndef BRIDGE_SERIAL_H
#define BRIDGE_SERIAL_H
#include <stdint.h>

/* port: 0=TTL UART3, 1=RS232 UART1, 2=RS485 UART2.
 * role: 0=transparent, 1=register responder. */
int BridgeSerial_Config(uint8_t port, uint8_t role, uint32_t baud);
int BridgeSerial_Disable(uint8_t port);
int BridgeSerial_Write(uint8_t port, const uint8_t *data, uint8_t size);
int BridgeSerial_Read(uint8_t port, uint8_t *data, uint8_t capacity,
                      uint8_t *actual);
int BridgeSerial_RegWrite(uint8_t port, uint8_t address,
                          const uint8_t *data, uint8_t size);
int BridgeSerial_RegRead(uint8_t port, uint8_t address,
                         uint8_t *data, uint8_t size);
void BridgeSerial_Poll(void);
uint8_t BridgeSerial_ReadyMask(void);
#endif
