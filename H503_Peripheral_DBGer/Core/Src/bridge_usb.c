#include "bridge_usb.h"
#include "usbd_core.h"
#include "usbd_cdc.h"
#include "stm32h5xx_hal_pcd_ex.h"
#include <string.h>

static USBD_HandleTypeDef usb;
static PCD_HandleTypeDef pcd;
static USBD_CDC_HandleTypeDef cdc_memory;
static uint8_t rx_packet[64], tx_packet[320];
static volatile uint16_t rx_head, rx_tail;
static uint8_t rx_ring[1024];
static uint8_t string_buffer[128];

void *USBD_static_malloc(uint32_t size)
{
  return size <= sizeof(cdc_memory) ? &cdc_memory : NULL;
}
void USBD_static_free(void *p) { (void)p; }

static uint8_t *device_desc(USBD_SpeedTypeDef speed, uint16_t *length)
{
  static uint8_t desc[] = {
    18, 1, 0x00, 0x02, 0x02, 0x02, 0x00, 64,
    0xFE, 0xCA, 0x30, 0x50, 0x00, 0x01, 1, 2, 3, 1
  };
  (void)speed; *length = sizeof(desc); return desc;
}
static uint8_t *lang_desc(USBD_SpeedTypeDef speed, uint16_t *length)
{
  static uint8_t desc[] = {4, 3, 0x09, 0x04};
  (void)speed; *length = sizeof(desc); return desc;
}
static uint8_t *make_string(const char *s, uint16_t *length)
{
  uint16_t n = 0;
  while (s[n] && n < 63U) ++n;
  string_buffer[0] = (uint8_t)(2U + n * 2U);
  string_buffer[1] = 3U;
  for (uint16_t i = 0; i < n; ++i) {
    string_buffer[2U + i * 2U] = (uint8_t)s[i];
    string_buffer[3U + i * 2U] = 0;
  }
  *length = string_buffer[0]; return string_buffer;
}
static uint8_t *manufacturer(USBD_SpeedTypeDef s, uint16_t *n)
{ (void)s; return make_string("GMP", n); }
static uint8_t *product(USBD_SpeedTypeDef s, uint16_t *n)
{ (void)s; return make_string("GMP Peripheral ISO Host", n); }
static uint8_t *serial(USBD_SpeedTypeDef s, uint16_t *n)
{
  static const char hex[] = "0123456789ABCDEF";
  char id[30] = "H503-";
  uint32_t words[3] = {HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()};
  (void)s;
  for (uint8_t word = 0; word < 3U; ++word)
    for (uint8_t digit = 0; digit < 8U; ++digit)
      id[5U + word * 8U + digit] = hex[(words[word] >> (28U - digit * 4U)) & 0xFU];
  id[29] = '\0';
  return make_string(id, n);
}
static uint8_t *configuration(USBD_SpeedTypeDef s, uint16_t *n)
{ (void)s; return make_string("Bridge CDC", n); }
static uint8_t *interface(USBD_SpeedTypeDef s, uint16_t *n)
{ (void)s; return make_string("Bridge Control", n); }
static USBD_DescriptorsTypeDef descriptors = {
  device_desc, lang_desc, manufacturer, product, serial,
  configuration, interface
};

static int8_t cdc_init(void)
{
  USBD_CDC_SetTxBuffer(&usb, tx_packet, 0);
  USBD_CDC_SetRxBuffer(&usb, rx_packet);
  return USBD_OK;
}
static int8_t cdc_deinit(void) { return USBD_OK; }
static int8_t cdc_control(uint8_t cmd, uint8_t *buf, uint16_t len)
{
  static uint8_t line_coding[7] = {0x00, 0xC2, 0x01, 0, 0, 0, 8};
  if (cmd == CDC_SET_LINE_CODING && len == 7U) memcpy(line_coding, buf, 7);
  if (cmd == CDC_GET_LINE_CODING && len == 7U) memcpy(buf, line_coding, 7);
  return USBD_OK;
}
static int8_t cdc_receive(uint8_t *buf, uint32_t *length)
{
  for (uint32_t i = 0; i < *length; ++i) {
    uint16_t next = (uint16_t)((rx_head + 1U) & 1023U);
    if (next != rx_tail) { rx_ring[rx_head] = buf[i]; rx_head = next; }
  }
  USBD_CDC_SetRxBuffer(&usb, rx_packet);
  USBD_CDC_ReceivePacket(&usb);
  return USBD_OK;
}
static int8_t cdc_sent(uint8_t *buf, uint32_t *length, uint8_t ep)
{ (void)buf; (void)length; (void)ep; return USBD_OK; }
static USBD_CDC_ItfTypeDef cdc_interface = {
  cdc_init, cdc_deinit, cdc_control, cdc_receive, cdc_sent
};

int BridgeUsb_GetByte(uint8_t *value)
{
  if (rx_tail == rx_head) return 0;
  *value = rx_ring[rx_tail];
  rx_tail = (uint16_t)((rx_tail + 1U) & 1023U);
  return 1;
}
int BridgeUsb_Send(const uint8_t *data, uint16_t length)
{
  if (length > sizeof(tx_packet) || usb.dev_state != USBD_STATE_CONFIGURED ||
      cdc_memory.TxState != 0U) return 0;
  memcpy(tx_packet, data, length);
  USBD_CDC_SetTxBuffer(&usb, tx_packet, length);
  return USBD_CDC_TransmitPacket(&usb) == USBD_OK;
}

int BridgeUsb_Init(void)
{
  RCC_OscInitTypeDef osc = {0};
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
  osc.HSI48State = RCC_HSI48_ON;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) return 0;
  if (USBD_Init(&usb, &descriptors, 0) != USBD_OK ||
      USBD_RegisterClass(&usb, USBD_CDC_CLASS) != USBD_OK ||
      USBD_CDC_RegisterInterface(&usb, &cdc_interface) != USBD_OK ||
      USBD_Start(&usb) != USBD_OK) return 0;
  return 1;
}

/* ST USB device stack to H5 HAL PCD adapter. */
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *dev)
{
  pcd.Instance = USB_DRD_FS;
  pcd.Init.dev_endpoints = 8;
  pcd.Init.speed = USBD_FS_SPEED;
  pcd.Init.phy_itface = PCD_PHY_EMBEDDED;
  pcd.Init.Sof_enable = DISABLE;
  pcd.Init.low_power_enable = DISABLE;
  pcd.Init.lpm_enable = DISABLE;
  pcd.Init.battery_charging_enable = DISABLE;
  pcd.Init.vbus_sensing_enable = DISABLE;
  pcd.Init.bulk_doublebuffer_enable = DISABLE;
  pcd.Init.iso_singlebuffer_enable = DISABLE;
  pcd.pData = dev;
  dev->pData = &pcd;
  if (HAL_PCD_Init(&pcd) != HAL_OK) return USBD_FAIL;
  /* H503 USB DRD uses PMA, not OTG FIFOs. Match the ST H503 CDC example. */
  HAL_PCDEx_PMAConfig(&pcd, 0x00, PCD_SNG_BUF, 0x14);
  HAL_PCDEx_PMAConfig(&pcd, 0x80, PCD_SNG_BUF, 0x54);
  HAL_PCDEx_PMAConfig(&pcd, 0x81, PCD_SNG_BUF, 0x94);
  HAL_PCDEx_PMAConfig(&pcd, 0x01, PCD_SNG_BUF, 0xD4);
  HAL_PCDEx_PMAConfig(&pcd, 0x82, PCD_SNG_BUF, 0x114);
  HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *d)
{ return HAL_PCD_DeInit((PCD_HandleTypeDef *)d->pData) == HAL_OK ? USBD_OK : USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *d)
{ return HAL_PCD_Start((PCD_HandleTypeDef *)d->pData) == HAL_OK ? USBD_OK : USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *d)
{ return HAL_PCD_Stop((PCD_HandleTypeDef *)d->pData) == HAL_OK ? USBD_OK : USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *d, uint8_t ep, uint8_t type, uint16_t size)
{ return HAL_PCD_EP_Open((PCD_HandleTypeDef *)d->pData, ep, size, type) == HAL_OK ? USBD_OK : USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *d, uint8_t ep)
{ return HAL_PCD_EP_Close((PCD_HandleTypeDef *)d->pData, ep) == HAL_OK ? USBD_OK : USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *d, uint8_t ep)
{ return HAL_PCD_EP_Flush((PCD_HandleTypeDef *)d->pData, ep) == HAL_OK ? USBD_OK : USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *d, uint8_t ep)
{ return HAL_PCD_EP_SetStall((PCD_HandleTypeDef *)d->pData, ep) == HAL_OK ? USBD_OK : USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *d, uint8_t ep)
{ return HAL_PCD_EP_ClrStall((PCD_HandleTypeDef *)d->pData, ep) == HAL_OK ? USBD_OK : USBD_FAIL; }
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *d, uint8_t ep)
{ PCD_HandleTypeDef *h = (PCD_HandleTypeDef *)d->pData;
  return (ep & 0x80U) ? h->IN_ep[ep & 0x7FU].is_stall : h->OUT_ep[ep & 0x7FU].is_stall; }
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *d, uint8_t address)
{ return HAL_PCD_SetAddress((PCD_HandleTypeDef *)d->pData, address) == HAL_OK ? USBD_OK : USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *d, uint8_t ep, uint8_t *buf, uint32_t n)
{ return HAL_PCD_EP_Transmit((PCD_HandleTypeDef *)d->pData, ep, buf, n) == HAL_OK ? USBD_OK : USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *d, uint8_t ep, uint8_t *buf, uint32_t n)
{ return HAL_PCD_EP_Receive((PCD_HandleTypeDef *)d->pData, ep, buf, n) == HAL_OK ? USBD_OK : USBD_FAIL; }
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *d, uint8_t ep)
{ return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef *)d->pData, ep); }
void USBD_LL_Delay(uint32_t ms) { HAL_Delay(ms); }

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *h)
{ USBD_LL_SetupStage((USBD_HandleTypeDef *)h->pData, (uint8_t *)h->Setup); }
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *h, uint8_t ep)
{ USBD_LL_DataOutStage((USBD_HandleTypeDef *)h->pData, ep, h->OUT_ep[ep].xfer_buff); }
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *h, uint8_t ep)
{ USBD_LL_DataInStage((USBD_HandleTypeDef *)h->pData, ep, h->IN_ep[ep].xfer_buff); }
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *h)
{ USBD_LL_SOF((USBD_HandleTypeDef *)h->pData); }
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *h)
{ USBD_LL_SetSpeed((USBD_HandleTypeDef *)h->pData, USBD_SPEED_FULL);
  USBD_LL_Reset((USBD_HandleTypeDef *)h->pData); }
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *h)
{ USBD_LL_Suspend((USBD_HandleTypeDef *)h->pData); }
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *h)
{ USBD_LL_Resume((USBD_HandleTypeDef *)h->pData); }
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *h)
{ USBD_LL_DevConnected((USBD_HandleTypeDef *)h->pData); }
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *h)
{ USBD_LL_DevDisconnected((USBD_HandleTypeDef *)h->pData); }
void USB_DRD_FS_IRQHandler(void) { HAL_PCD_IRQHandler(&pcd); }
