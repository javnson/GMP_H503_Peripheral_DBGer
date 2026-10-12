#ifndef BRIDGE_USB_H
#define BRIDGE_USB_H
#include <stdint.h>
int BridgeUsb_Init(void);
int BridgeUsb_GetByte(uint8_t *value);
int BridgeUsb_Send(const uint8_t *data, uint16_t length);
#endif
