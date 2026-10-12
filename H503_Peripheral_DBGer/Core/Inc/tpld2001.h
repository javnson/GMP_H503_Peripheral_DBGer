#ifndef TPLD2001_H
#define TPLD2001_H

#include "stm32h5xx_hal.h"

typedef enum {
  TPLD_OK = 0,
  TPLD_NO_DEVICE,
  TPLD_UNLOCK_FAILED,
  TPLD_MODE_FAILED,
  TPLD_WRITE_FAILED,
  TPLD_VERIFY_FAILED,
  TPLD_ACTIVATE_FAILED,
  TPLD_PWM_FAILED
} TPLD_Status;

/* Loads a volatile configuration using PB5 (SDA) and PB6 (SCL).
 * Permanent OTP programming needs an external 7.5-8 V VPP supply on GPI. */
TPLD_Status TPLD2001_Load(uint8_t *found_address);
TPLD_Status TPLD2001_LoadImage(const uint8_t *image, uint16_t size,
                              uint8_t *found_address);
/* Shared open-drain PB5/PB6 bus. Returns 1 on ACK and successful transfer. */
int TPLD2001_I2CTransfer(uint8_t address, const uint8_t *tx, uint8_t tx_size,
                         uint8_t *rx, uint8_t rx_size);

#endif
