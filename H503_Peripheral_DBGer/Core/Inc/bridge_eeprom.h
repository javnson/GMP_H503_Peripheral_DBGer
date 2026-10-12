#ifndef BRIDGE_EEPROM_H
#define BRIDGE_EEPROM_H
#include <stdint.h>

/* 24LC02B in SRAM, or host-backed 24LC128/256/512 with one page cache. */
void BridgeEeprom_Init(void);
void BridgeEeprom_Release(void);
uint8_t BridgeEeprom_Ready(void);
int BridgeEeprom_Config(uint8_t capacity_log2, uint8_t page_size);
uint8_t BridgeEeprom_AddressBytes(void);
uint16_t BridgeEeprom_NormalizeAddress(uint16_t address);
uint16_t BridgeEeprom_PageNext(uint16_t address);
uint16_t BridgeEeprom_NextAddress(uint16_t address);
void BridgeEeprom_BeginWrite(uint16_t address);
void BridgeEeprom_WriteByte(uint8_t value);
void BridgeEeprom_Stop(void);
void BridgeEeprom_Poll(void);
uint8_t BridgeEeprom_Busy(void);
/* Zero means cache miss: stretch SCL until the host replies. */
int BridgeEeprom_ReadAt(uint16_t address, uint8_t *value);
int BridgeEeprom_LocalRead(uint8_t address, uint8_t *data, uint8_t count);
int BridgeEeprom_LocalWrite(uint8_t address, const uint8_t *data, uint8_t count);
uint8_t BridgeEeprom_Error(void);
/* Event kind 1=load page, 2=commit changed bytes. */
uint8_t BridgeEeprom_Event(uint8_t *out);
int BridgeEeprom_Reply(const uint8_t *payload, uint16_t size);
void BridgeEeprom_AbortRead(void);
int BridgeEeprom_InvalidateCache(void);
#endif
