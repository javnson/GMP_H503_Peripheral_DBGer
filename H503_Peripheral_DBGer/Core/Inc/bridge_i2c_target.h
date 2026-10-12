#ifndef BRIDGE_I2C_TARGET_H
#define BRIDGE_I2C_TARGET_H
#include <stdint.h>

/* Software target on board-wired PB5 SDA / PB6 SCL. Register bank port 5.
 * Controller writes one-byte register pointer, then optional data; a repeated
 * start and read returns bytes from that pointer. */
int BridgeI2cTarget_Config(uint8_t enable, uint8_t address);
/* profile 0: flat byte register bank; 1: HDC1080-compatible 16-bit words.
 * In profile 1, writable sample words are 0x00 temperature, 0x01 humidity,
 * 0x02 configuration. The requested word is set atomically outside the ISR. */
int BridgeI2cTarget_SetProfileWord(uint8_t profile, uint8_t reg, uint16_t value);
int BridgeI2cTarget_GetWord(uint8_t reg, uint16_t *value);
uint8_t BridgeI2cTarget_Active(void);
uint8_t BridgeI2cTarget_Address(void);
void BridgeI2cTarget_IrqSda(void);
void BridgeI2cTarget_IrqScl(void);
#endif
