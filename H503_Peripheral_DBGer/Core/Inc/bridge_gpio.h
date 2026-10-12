#ifndef BRIDGE_GPIO_H
#define BRIDGE_GPIO_H
#include <stdint.h>

enum { BRIDGE_GPIO_INPUT = 0, BRIDGE_GPIO_OUTPUT = 1,
       BRIDGE_GPIO_ADC = 2, BRIDGE_GPIO_DAC = 3 };
void BridgeGpio_Init(void);
/* TPLD outputs may drive PA1/PA4/PA5. */
int BridgeGpio_Configure(uint8_t pin, uint8_t mode, uint8_t tpld_active);
int BridgeGpio_Set(uint8_t pin, uint16_t value);
int BridgeGpio_Get(uint8_t pin, uint8_t *mode, uint16_t *value);
#endif
