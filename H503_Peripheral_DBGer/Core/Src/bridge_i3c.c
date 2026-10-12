#include "bridge_i3c.h"
#include "bridge_registers.h"
#include "stm32h5xx_hal.h"
#include <string.h>

static I3C_HandleTypeDef bus;
static uint8_t ready, role, target_phase;
static volatile uint8_t target_needs_listen;
/* The HAL word-FIFO path accesses four bytes even for a short final word. */
static uint8_t target_rx[BRIDGE_REG_FRAME_SIZE + 3U];
static uint8_t target_tx[BRIDGE_REG_FRAME_SIZE + 3U];
static I3C_XferTypeDef target_context;

static int target_listen(void)
{
  target_context.RxBuf.pBuffer = target_rx;
  target_context.RxBuf.Size = BRIDGE_REG_FRAME_SIZE;
  target_context.TxBuf.pBuffer = target_tx;
  target_context.TxBuf.Size = BRIDGE_REG_FRAME_SIZE;
  target_phase = 0U;
  return HAL_I3C_Tgt_Receive_IT(&bus, &target_context) == HAL_OK;
}

int BridgeI3c_Config(uint8_t new_role)
{
  I3C_FifoConfTypeDef fifo = {0};
  I3C_CtrlConfTypeDef controller = {0};
  I3C_TgtConfTypeDef target = {0};
  if (new_role > 1U) return 0;
  if (bus.Instance == I3C2 && bus.State != HAL_I3C_STATE_RESET) {
    HAL_NVIC_DisableIRQ(I3C2_EV_IRQn);
    HAL_NVIC_DisableIRQ(I3C2_ER_IRQn);
    (void)HAL_I3C_DeInit(&bus);
  }
  /* DeInit leaves the target's assigned dynamic address in peripheral state.
   * A fresh controller has no record of it, so reset hardware before DAA. */
  __HAL_RCC_I3C2_FORCE_RESET();
  __HAL_RCC_I3C2_RELEASE_RESET();
  ready = 0U;
  target_needs_listen = 0U;
  memset(&bus, 0, sizeof(bus));
  bus.Instance = I3C2;
  bus.Mode = new_role == 0U ? HAL_I3C_MODE_CONTROLLER : HAL_I3C_MODE_TARGET;
  if (new_role == 0U) {
    bus.Init.CtrlBusCharacteristic.SDAHoldTime = HAL_I3C_SDA_HOLD_TIME_0_5;
    bus.Init.CtrlBusCharacteristic.WaitTime = HAL_I3C_OWN_ACTIVITY_STATE_0;
    bus.Init.CtrlBusCharacteristic.SCLPPLowDuration = 0x77U;
    bus.Init.CtrlBusCharacteristic.SCLI3CHighDuration = 0x4FU;
    bus.Init.CtrlBusCharacteristic.SCLODLowDuration = 0x77U;
    bus.Init.CtrlBusCharacteristic.SCLI2CHighDuration = 0U;
    bus.Init.CtrlBusCharacteristic.BusFreeDuration = 0x31U;
    bus.Init.CtrlBusCharacteristic.BusIdleDuration = 0xF8U;
  } else {
    bus.Init.TgtBusCharacteristic.BusAvailableDuration = 0xF8U;
  }
  if (HAL_I3C_Init(&bus) != HAL_OK) return 0;
  fifo.RxFifoThreshold = HAL_I3C_RXFIFO_THRESHOLD_1_4;
  fifo.TxFifoThreshold = HAL_I3C_TXFIFO_THRESHOLD_1_4;
  fifo.ControlFifo = HAL_I3C_CONTROLFIFO_DISABLE;
  fifo.StatusFifo = HAL_I3C_STATUSFIFO_DISABLE;
  if (HAL_I3C_SetConfigFifo(&bus, &fifo) != HAL_OK) return 0;
  if (new_role == 0U) {
    controller.DynamicAddr = 0U;
    controller.HotJoinAllowed = DISABLE;
    controller.ACKStallState = DISABLE;
    controller.CCCStallState = DISABLE;
    controller.TxStallState = DISABLE;
    controller.RxStallState = DISABLE;
    controller.HighKeeperSDA = DISABLE;
    if (HAL_I3C_Ctrl_Config(&bus, &controller) != HAL_OK) return 0;
  } else {
    target.Identifier = 0xC6U;
    target.MIPIIdentifier = 0x50U;
    target.CtrlRoleRequest = DISABLE;
    target.HotJoinRequest = DISABLE;
    target.IBIRequest = DISABLE;
    target.IBIPayload = DISABLE;
    target.IBIPayloadSize = HAL_I3C_PAYLOAD_EMPTY;
    target.MaxReadDataSize = BRIDGE_REG_FRAME_SIZE;
    target.MaxWriteDataSize = BRIDGE_REG_FRAME_SIZE;
    target.CtrlCapability = DISABLE;
    target.GroupAddrCapability = DISABLE;
    target.DataTurnAroundDuration = HAL_I3C_TURNAROUND_TIME_TSCO_LESS_12NS;
    target.MaxDataSpeed = HAL_I3C_GETMXDS_FORMAT_1;
    target.MaxSpeedLimitation = DISABLE;
    target.HandOffActivityState = HAL_I3C_HANDOFF_ACTIVITY_STATE_0;
    target.HandOffDelay = DISABLE;
    target.PendingReadMDB = DISABLE;
    if (HAL_I3C_Tgt_Config(&bus, &target) != HAL_OK) return 0;
    memset(&target_context, 0, sizeof(target_context));
    if (HAL_I3C_ActivateNotification(&bus, NULL,
                                     HAL_I3C_IT_DAUPDIE) != HAL_OK) return 0;
  }
  role = new_role;
  ready = 1U;
  return 1;
}

int BridgeI3c_Disable(void)
{
  if (!ready) return 1;
  ready = 0U;
  target_needs_listen = 0U;
  HAL_NVIC_DisableIRQ(I3C2_EV_IRQn);
  HAL_NVIC_DisableIRQ(I3C2_ER_IRQn);
  if (HAL_I3C_DeInit(&bus) != HAL_OK) return 0;
  __HAL_RCC_I3C2_FORCE_RESET();
  __HAL_RCC_I3C2_RELEASE_RESET();
  return 1;
}

int BridgeI3c_Assign(uint8_t base_address, uint8_t maximum,
                     uint8_t *addresses, uint8_t *actual)
{
  HAL_StatusTypeDef status;
  uint64_t payload = 0U;
  if (!ready || role != 0U || base_address < 0x08U ||
      maximum == 0U || maximum > 8U ||
      base_address + maximum > 0x78U || addresses == NULL || actual == NULL)
    return 0;
  *actual = 0U;
  for (;;) {
    payload = 0U;
    status = HAL_I3C_Ctrl_DynAddrAssign(&bus, &payload,
                                        I3C_ONLY_ENTDAA, 50U);
    if (status == HAL_OK) return 1;
    if (status != HAL_BUSY || *actual >= maximum) return 0;
    uint8_t address = (uint8_t)(base_address + *actual);
    if (HAL_I3C_Ctrl_SetDynAddr(&bus, address) != HAL_OK) return 0;
    addresses[(*actual)++] = address;
  }
}

static int private_transfer(uint8_t address, uint8_t *buffer,
                            uint8_t length, uint32_t direction)
{
  I3C_PrivateTypeDef descriptor = {0};
  I3C_XferTypeDef transfer = {0};
  uint32_t control[2] = {0};
  descriptor.TargetAddr = address;
  descriptor.Direction = direction;
  if (direction == HAL_I3C_DIRECTION_WRITE) {
    descriptor.TxBuf.pBuffer = buffer;
    descriptor.TxBuf.Size = length;
    transfer.TxBuf = descriptor.TxBuf;
  } else {
    descriptor.RxBuf.pBuffer = buffer;
    descriptor.RxBuf.Size = length;
    transfer.RxBuf = descriptor.RxBuf;
  }
  transfer.CtrlBuf.pBuffer = control;
  transfer.CtrlBuf.Size = 1U;
  if (HAL_I3C_AddDescToFrame(&bus, NULL, &descriptor, &transfer,
                             1U, I3C_PRIVATE_WITH_ARB_STOP) != HAL_OK)
    return 0;
  if (direction == HAL_I3C_DIRECTION_WRITE)
    return HAL_I3C_Ctrl_Transmit(&bus, &transfer, 100U) == HAL_OK;
  return HAL_I3C_Ctrl_Receive(&bus, &transfer, 100U) == HAL_OK;
}

int BridgeI3c_Transfer(uint8_t address, const uint8_t *tx, uint8_t tx_length,
                       uint8_t *rx, uint8_t rx_length)
{
  if (!ready || role != 0U || address < 0x08U || address > 0x77U ||
      tx_length > 64U || rx_length > 64U ||
      (!tx_length && !rx_length) ||
      (tx_length && tx == NULL) || (rx_length && rx == NULL)) return 0;
  if (tx_length && !private_transfer(address, (uint8_t *)tx,
                                      tx_length, HAL_I3C_DIRECTION_WRITE)) return 0;
  if (rx_length && !private_transfer(address, rx,
                                      rx_length, HAL_I3C_DIRECTION_READ)) return 0;
  return 1;
}

void BridgeI3c_EventIrq(void) { HAL_I3C_EV_IRQHandler(&bus); }
void BridgeI3c_ErrorIrq(void) { HAL_I3C_ER_IRQHandler(&bus); }

void BridgeI3c_Poll(void)
{
  if (ready && role == 1U && target_needs_listen &&
      HAL_I3C_GetState(&bus) == HAL_I3C_STATE_LISTEN) {
    target_needs_listen = 0U;
    if (!target_listen()) ready = 0U;
  }
}

void BridgeI3c_GetStatus(uint8_t *is_ready, uint8_t *current_role,
                         uint32_t *error)
{
  *is_ready = ready;
  *current_role = role;
  *error = bus.Instance == I3C2 ? HAL_I3C_GetError(&bus) : 0U;
}

void HAL_I3C_NotifyCallback(I3C_HandleTypeDef *handle, uint32_t event)
{
  if (handle == &bus && ready && role == 1U && (event & EVENT_ID_DAU))
    /* The HAL changes BUSY_RX back to LISTEN after this callback returns. */
    target_needs_listen = 1U;
}

void HAL_I3C_TgtRxCpltCallback(I3C_HandleTypeDef *handle)
{
  uint8_t expected, reply_size = 0U;
  if (handle != &bus || !ready || role != 1U) return;
  memset(target_tx, 0, sizeof(target_tx));
  expected = target_rx[1] == 1U ?
             (uint8_t)(5U + target_rx[3]) : 5U;
  if (expected > BRIDGE_REG_FRAME_SIZE ||
      !BridgeRegs_Execute(4U, target_rx, expected,
                          target_tx, &reply_size)) {
    target_tx[0] = 0x5AU; target_tx[1] = 1U;
    target_tx[2] = 0U; target_tx[3] = 0xB6U;
  }
  target_phase = 1U;
  if (HAL_I3C_Tgt_Transmit_IT(&bus, &target_context) != HAL_OK)
    ready = 0U;
}

void HAL_I3C_TgtTxCpltCallback(I3C_HandleTypeDef *handle)
{
  if (handle == &bus && ready && role == 1U && target_phase == 1U)
    if (!target_listen()) ready = 0U;
}

void HAL_I3C_ErrorCallback(I3C_HandleTypeDef *handle)
{
  if (handle == &bus) ready = 0U;
}
