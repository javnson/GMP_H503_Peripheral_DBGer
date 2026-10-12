#include "bridge_can.h"
#include "stm32h5xx_hal.h"
#include <string.h>

static FDCAN_HandleTypeDef can;
static uint8_t ready;

int BridgeCan_Config(uint32_t bitrate)
{
  uint32_t prescaler, seg1, seg2;
  /* PLL1Q is 250 MHz in the bridge clock tree (CSI 4 MHz x125 /2). */
  if (bitrate == 125000U) { prescaler = 40U; seg1 = 39U; seg2 = 10U; }
  else if (bitrate == 250000U) { prescaler = 20U; seg1 = 39U; seg2 = 10U; }
  else if (bitrate == 500000U) { prescaler = 10U; seg1 = 39U; seg2 = 10U; }
  else if (bitrate == 1000000U) { prescaler = 10U; seg1 = 19U; seg2 = 5U; }
  else return 0;
  if (ready) {
    (void)HAL_FDCAN_Stop(&can);
    (void)HAL_FDCAN_DeInit(&can);
    ready = 0U;
  }
  memset(&can, 0, sizeof(can));
  can.Instance = FDCAN1;
  can.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  can.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  can.Init.Mode = FDCAN_MODE_NORMAL;
  can.Init.AutoRetransmission = ENABLE;
  can.Init.TransmitPause = DISABLE;
  can.Init.ProtocolException = DISABLE;
  can.Init.NominalPrescaler = prescaler;
  can.Init.NominalSyncJumpWidth = seg2;
  can.Init.NominalTimeSeg1 = seg1;
  can.Init.NominalTimeSeg2 = seg2;
  can.Init.DataPrescaler = prescaler;
  can.Init.DataSyncJumpWidth = seg2;
  can.Init.DataTimeSeg1 = seg1;
  can.Init.DataTimeSeg2 = seg2;
  can.Init.StdFiltersNbr = 0U;
  can.Init.ExtFiltersNbr = 0U;
  can.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&can) != HAL_OK) return 0;
  if (HAL_FDCAN_ConfigGlobalFilter(&can, FDCAN_ACCEPT_IN_RX_FIFO0,
       FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK ||
      HAL_FDCAN_Start(&can) != HAL_OK) {
    (void)HAL_FDCAN_DeInit(&can); return 0;
  }
  ready = 1U;
  return 1;
}

int BridgeCan_Send(uint32_t identifier, uint8_t extended,
                   const uint8_t *data, uint8_t length)
{
  FDCAN_TxHeaderTypeDef header = {0};
  static const uint32_t dlc[9] = {FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1,
      FDCAN_DLC_BYTES_2, FDCAN_DLC_BYTES_3, FDCAN_DLC_BYTES_4,
      FDCAN_DLC_BYTES_5, FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7, FDCAN_DLC_BYTES_8};
  if (!ready || extended > 1U || length > 8U ||
      identifier > (extended ? 0x1FFFFFFFUL : 0x7FFUL) ||
      (length && data == NULL) || HAL_FDCAN_GetTxFifoFreeLevel(&can) == 0U)
    return 0;
  header.Identifier = identifier;
  header.IdType = extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
  header.TxFrameType = FDCAN_DATA_FRAME;
  header.DataLength = dlc[length];
  header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  header.BitRateSwitch = FDCAN_BRS_OFF;
  header.FDFormat = FDCAN_CLASSIC_CAN;
  header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  header.MessageMarker = 0U;
  return HAL_FDCAN_AddMessageToTxFifoQ(&can, &header, (uint8_t *)data) == HAL_OK;
}

int BridgeCan_Receive(uint32_t *identifier, uint8_t *extended,
                      uint8_t *data, uint8_t *length)
{
  FDCAN_RxHeaderTypeDef header;
  uint8_t frame[64];
  if (!ready || !identifier || !extended || !data || !length) return -1;
  if (HAL_FDCAN_GetRxFifoFillLevel(&can, FDCAN_RX_FIFO0) == 0U) {
    *length = 0U; return 0;
  }
  if (HAL_FDCAN_GetRxMessage(&can, FDCAN_RX_FIFO0, &header, frame) != HAL_OK)
    return -1;
  *identifier = header.Identifier;
  *extended = header.IdType == FDCAN_EXTENDED_ID;
  *length = (uint8_t)header.DataLength;
  if (*length > 8U || header.RxFrameType != FDCAN_DATA_FRAME) return -1;
  memcpy(data, frame, *length);
  return 1;
}

int BridgeCan_Status(uint8_t out[7])
{
  FDCAN_ProtocolStatusTypeDef protocol;
  FDCAN_ErrorCountersTypeDef errors;
  if (!out) return 0;
  out[0] = ready;
  if (!ready) { memset(out + 1U, 0, 6U); return 1; }
  if (HAL_FDCAN_GetProtocolStatus(&can, &protocol) != HAL_OK ||
      HAL_FDCAN_GetErrorCounters(&can, &errors) != HAL_OK) return 0;
  out[1] = (uint8_t)HAL_FDCAN_GetTxFifoFreeLevel(&can);
  out[2] = (uint8_t)HAL_FDCAN_GetRxFifoFillLevel(&can, FDCAN_RX_FIFO0);
  out[3] = (uint8_t)protocol.BusOff;
  out[4] = (uint8_t)protocol.LastErrorCode;
  out[5] = (uint8_t)errors.TxErrorCnt;
  out[6] = (uint8_t)errors.RxErrorCnt;
  return 1;
}
