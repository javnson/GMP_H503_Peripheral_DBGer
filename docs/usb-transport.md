# USB CDC + HID 传输设计

当前固件枚举为**单一 CDC ACM 功能**，Windows 使用系统 `usbser.sys`，Linux 使用 `cdc_acm`。#2 板实测产品字符串、HELLO 为 `GMP Peripheral Host`；Windows 端口列表仍为“USB 串行设备 (COM4)”。921600 8N1 是 CDC 线编码默认值，与 USB Full Speed 链路的 12 Mbit/s 速率和总线占用无直接关系。

下一描述符版本计划使用 USB 复合设备：设备类 `EF/02/01`；接口 0、1 由 IAD 组成 CDC ACM 控制和数据功能；接口 2 为 HID，带 64 字节 Interrupt IN/OUT，按 1 ms 轮询。CDC 继续承载配置、固件镜像上传和大块数据；HID 承载设备事件、短实时请求和中断通知。Windows 的复合父设备交给系统 `Usbccgp.sys`，CDC 子功能继续绑定 `usbser.sys`，HID 子功能绑定系统 HID 类驱动。IAD 和复合硬件 ID 规则见 [Microsoft IAD 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-interface-association-descriptor) 和 [标准 USB 标识符](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/standard-usb-identifiers)。切换复合描述符时需要重新检查 Windows 端口的 `MI_00` 硬件 ID、Linux `/dev/ttyACM*` 和两板序列号；#1 板不参与这一步固件试验。

## HID 64 字节报告草案

HID 设备报告不使用非零 Report ID；报告在线上恰好 64 字节。Windows HID API 某些调用需要应用在缓冲区前面额外写一个值为 0 的 Report ID 字节，这个前缀不计入设备报告。字段均为小端。

| 偏移 | 长度 | 内容 |
| ---: | ---: | --- |
| 0 | 1 | 格式版本，起始为 1 |
| 1 | 1 | `kind`：事件、请求、响应、状态 |
| 2 | 1 | 外设标记编号：0 系统、1 GPIO、2 I²C、3 I3C、4 SPI、5 QEP、6 UART、7 RS232、8 RS485、9 CAN |
| 3 | 1 | flags：bit0 overflow、bit1 error、bit2 more、bit3 direction |
| 4 | 1 | sequence，按方向独立递增，回包必须照抄请求号 |
| 5 | 1 | fragment index，从 0 开始 |
| 6 | 1 | fragment count，至少 1 |
| 7 | 1 | payload length，0..54 |
| 8..61 | 54 | payload，未使用字节必须为 0 |
| 62..63 | 2 | CRC16 CCITT-FALSE，覆盖 0..61 |

CAN 经典帧可以在一份报告中包含 ID、DLC 和 8 字节数据；I²C/I3C/SPI 64 字节交易、寄存器快照和 TPLD 镜像需要分片。接收端必须在固定超时内收齐且按 `sequence + fragment index` 验证；丢片则整笔事务失败，不把半包写入设备。HID 消息队列满时设置 sticky overflow 位并增加计数，`STATUS` 返回后不清除计数，仅显式 ACK 才重置本次告警。

## Windows 友好名称

自动类驱动绑定不要求安装任何 INF，但系统 `usbser.inf` 的端口描述为通用 USB Serial Device。工程提供 [签名 INF 模板](../drivers/windows/gmp_peripheral_host.inf.in)，其设备描述字符串为 `GMP Peripheral Host` 并继续引用系统 `usbser.sys`。该模板尚无签名 `.cat`，**不能作为已交付驱动安装**；产品部署需使用有权使用的 VID/PID、为准确硬件 ID 生成并签名 catalog，再验证 Windows 10/11 显示和复合 CDC 子功能。现用 `CAFE:5030` 只是开发原型 ID。Windows 会对驱动包的 INF 与 catalog 签名进行校验，见 [Microsoft catalog 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/catalog-files)。

| 传输 | 适合 | 限制 |
| --- | --- | --- |
| CDC | 命令/响应、变长数据、512 B TPLD 镜像、PC EEPROM 页服务 | 一个端口一般由一个进程独占；设备端必须处理背压和队列溢出。 |
| HID | 短事件、低延迟状态变化、系统驱动可直接枚举 | 64 B 报告需要分片；轮询间隔不保证严格的实时截止时间。 |

复合设备及 HID 还处于设计阶段；当前 CLI 的 `rx` 使用 CDC 查询 CAN FIFO/串口缓冲区，尚未通过 HID 收事件。
