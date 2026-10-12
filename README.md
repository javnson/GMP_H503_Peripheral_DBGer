# GMP Peripheral ISO Host

STM32H503CBU6 板载隔离式 USB 外设调试器。此仓库同时保留已验证的 TPLD `led_direct` 和 `pulse_distributor` 演示；调试器固件使用独立的 `bridge` 构建配置。

## 当前可用功能

| 功能 | 状态 | 说明 |
| --- | --- | --- |
| USB CDC ACM | 已在当前板卡验证 | 设备描述符名称为 **GMP Peripheral ISO Host**；Windows 使用系统 `usbser.sys`，Linux 使用 `cdc_acm`。 |
| 握手、版本、能力和运行状态 | 已实现 | 命令帧含序号、长度和 CRC16。 |
| GPIO | 已实现 | 可读取 PA0、PA1、PA4、PA5；只允许 PA0 开漏输出（板上 5.1 kΩ 上拉），避免与 TPLD 驱动的 PA1/PA4/PA5 对驱。 |
| I²C 主机 | 已实现 | PB5/PB6 软件 I²C，共享 TPLD 配置总线，最多 64 字节写、64 字节读。保留 TPLD 地址 0x79。 |
| TPLD 镜像上传 | 已在当前板卡验证 | 验证 512 字节 HEX，在 STM32 Flash 的两个 8 KiB 槽之间交替提交，CRC32 校验；可设置重启自动加载。 |
| SPI / I3C / QEP / UART / RS232 / RS485 | 已规划接口 | 引脚及电气关系已记录，协议命令和主从机/寄存器服务待实现；不会在握手能力位中声称已可用。 |
| TPLD OTP 烧录 | 不支持 | 当前实现只加载易失性配置；OTP 需要独立的 VPP 条件。 |

## 构建与使用

固件依赖已存在的 STM32Cube H5 HAL 源树，以及 `arm-none-eabi-gcc`：

```powershell
.\scripts\build_gcc.ps1 -Example bridge
```

输出为 `firmware/GMP_Peripheral_ISO_Host.hex`。`firmware/bridge_flash.ld` 将程序区限制为 112 KiB，留出最后两个 8 KiB 扇区供 TPLD 镜像存储。用此链接脚本构建的固件才允许执行镜像上传。此前两个演示构建配置保持可用：`-Example led_direct` 和 `-Example pulse_distributor`。

Windows 编译主机库和 CLI：

```powershell
.\host\build-windows.bat
.\host\dist\bin\gmpctl.exe COM3 info
.\host\dist\bin\gmpctl.exe COM3 status
.\host\dist\bin\gmpctl.exe COM3 tpld upload .\cpld\pulse_distributor\program.hex --reset
```

Linux：

```sh
sh host/build-linux.sh
host/dist-linux/bin/gmpctl /dev/ttyACM0 info
```

主机库同时生成共享库 `gmp_host.dll` / `libgmp_host.so` 和静态库，公开 [C ABI](host/include/gmp_host.h) 与 `gmp::Device` C++ 类。静态链接时定义 `GMP_HOST_STATIC`。CLI 仅通过共享库通信。GUI 依赖 PySide6：

```powershell
python -m pip install -r gui/requirements.txt
python gui/app.py
```

GUI 第一页可连接、校验设备、选择 InterConnect Studio `program.hex`、上传并校验镜像、查看启动状态和重启。GPIO 页可操作四个已开放脚位。其余标签页保留入口，功能将按协议版本逐步加入。

## TPLD 自动加载及供电

上传的 HEX 必须覆盖地址 `0x0200..0x03FF` 的每个字节。主机校验记录、地址与 checksum，然后把 512 字节和 CRC32 分块发送；设备只有收齐所有字节且 CRC32 一致才擦写备用扇区。设备先写数据，再写提交头；意外断电时上一个有效槽保留。

开启 `autoload` 后，STM32 启动时尝试把镜像写入 TPLD **易失性配置**，再启动 USB。若 TPLD 仍保留旧配置，单独复位 STM32 不一定能重新进入空白配置模式；更换 TPLD 逻辑后，应让整板 3.3 V 完全断电再上电，并通过 `status` 查看启动结果。`TPLD boot status=0` 表示本次加载成功，`255` 表示未尝试；其他值见 [TPLD 状态枚举](H503_Peripheral_DBGer/Core/Inc/tpld2001.h)。

## USB CDC 与未来 HID

首版使用 CDC ACM，设备描述符类/子类为 `02/02`。Windows 系统可按兼容 ID 自动绑定 `usbser.sys`，不必附带专用 INF；Linux 的常规 CDC ACM 驱动提供 `/dev/ttyACM*`。参见 [Microsoft USB serial driver 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-driver-installation-based-on-compatible-ids)。

Windows 设备管理器的串口友好名称通常仍显示为“USB 串行设备 (COMx)”；USB 总线报告的产品字符串是“GMP Peripheral ISO Host”，CLI 和 GUI 握手也校验此名称。若产品化时要求设备管理器友好名称也一致，可以提供仅指定名称和系统 `usbser.sys` 的签名 INF。

| 选择 | 优点 | 代价 |
| --- | --- | --- |
| CDC ACM（当前） | 系统串口 API 即可在 C/C++/Python 上实现；连续数据和变长协议帧直观；单一主机库可跨平台。 | 系统分配的 COM 号或 `/dev/ttyACM*` 可能变化；Windows 串口通常只能由一个进程独占。 |
| HID（可行的后续方案） | Windows 和 Linux 都有系统 HID 类驱动；可通过 VID/PID 和设备路径定位，而非 COM 号。 | 需要 HID report descriptor、定长报告及分片/重组；512 字节 TPLD 镜像和未来 SPI/I3C 流量的协议实现更复杂。 |

Windows 的系统 HID 驱动和应用接口见 [Microsoft HID 类驱动列表](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/supported-usb-classes) 与 [HID API](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/hid-api)。如果后续确实需要 HID，可以保留现有命令层，替换传输层并规定固定 report 长度。当前描述符中的 VID/PID `CAFE:5030` 仅供内部开发测试；产品发布前需替换为有使用权的 VID/PID 和唯一序列号。

## 硬件接口规划

| 接口 | 板级引脚/关系 | 后续实现重点 |
| --- | --- | --- |
| USB FS | PA11/PA12，隔离 USB-C | 设备枚举、连接恢复和吞吐测试。 |
| I²C / TPLD | PB5 SDA、PB6 SCL，共用跳线/上拉 | PB5 在 H503 上是 I2C1_SMBA，不能按 `.ioc` 中的硬件 I2C1_SDA 使用；当前为 GPIO 开漏软件 I²C。 |
| SPI2 | PA8 NSS、PB2 SCK、PB14 MISO、PB15 MOSI | 主机传输、从机寄存器映射、片选时序。 |
| I3C2 | PB3 SCL、PB4 SDA | 动态地址与主/从角色。 |
| QEP | PA6/PA7 TIM3 CH1/CH2，PB7 ETR | 正交计数、索引和事件时间戳。 |
| RS232 | USART1，经板载电平转换器 | 需纠正当前 Cube 生成的 USART1 MSP 引脚（与 TPLD PA1 冲突），再启用。 |
| RS485 | USART2 + PB8 DE，经板载收发器 | 收发方向控制及从机寄存器服务。 |
| TTL UART | USART3 | 串口数据队列及主/从协议适配。 |

引脚表以板级手册及实际原理图为准。对于 SPI/UART 的“从机自动回复”，下一阶段应建立统一寄存器映射和访问回调，避免在中断中直接运行 USB 命令解释器。

仓库现有 Cube 生成的 `stm32h5xx_hal_msp.c` 还把 SPI2 映射到 PA7/PB1/PB2、TIM3 编码器映射到 PA0/PB5，与板级 SPI 和 QEP 网名不符；因此 `bridge` 当前没有调用这些生成的初始化函数。实现这些外设时需先按原理图修正 MSP 与 `.ioc`，并逐项做硬件回环/外部设备验证。

## 协议与扩展

详见 [协议文档](docs/protocol.md)。协议版本 1 使用一个请求对应一个响应；不支持的命令返回 `UNSUPPORTED`。后续外设通过新的命令号、能力位和文档版本加入，旧主机程序可以先查询 `HELLO` 的能力位。

当前板卡验证：Windows `COM3` 握手得到协议 `1.0`、功能位 `0x1F`；连续两次上传后 Flash 槽 generation 为 `2`、CRC32 为 `B032BD7E`。整板断电重上电后 `status` 返回 TPLD 启动结果 `0`、器件地址 `0x79`，说明自动加载成功。Linux 编译通过，实际 Linux USB 联机仍待验证。

USB 设备栈使用 ST 官方 `stm32-mw-usb-device` 的 Core/CDC 文件，来源修订 `2df324bd60d4b0bb27404fd70b1c089b467f0e09`，授权文本见 [LICENSE](third_party/stm32_usb_device/LICENSE.md)。
