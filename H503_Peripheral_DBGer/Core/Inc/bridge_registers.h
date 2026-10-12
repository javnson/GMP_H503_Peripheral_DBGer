#ifndef BRIDGE_REGISTERS_H
#define BRIDGE_REGISTERS_H
#include <stdint.h>

/* 0=UART, 1=RS232, 2=RS485, 3=SPI, 4=I3C, 5=I2C. */
#define BRIDGE_REG_PORTS 6U
#define BRIDGE_REG_MAX_TRANSFER 32U
#define BRIDGE_REG_FRAME_SIZE 37U

void BridgeRegs_Init(void);
int BridgeRegs_Write(uint8_t port, uint8_t address,
                     const uint8_t *data, uint8_t size);
int BridgeRegs_Read(uint8_t port, uint8_t address,
                    uint8_t *data, uint8_t size);
/* Request A5 op address length [data] crc8; reply 5A status length [data] crc8.
 * The caller supplies exactly the received frame length and a 36-byte reply.
 * SPI pads both directions to 37 bytes; UART/I3C use the returned length. */
int BridgeRegs_Execute(uint8_t port, const uint8_t *request, uint8_t size,
                       uint8_t *reply, uint8_t *reply_size);
#endif
