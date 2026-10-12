# GMP Peripheral Host

STM32H503CBU6 板载隔离式 USB 外设调试器。此仓库同时保留已验证的 TPLD `led_direct` 和 `pulse_distributor` 演示；调试器固件使用独立的 `bridge` 构建配置。

## 当前可用功能

| 功能 | 状态 | 说明 |
| --- | --- | --- |
| USB CDC ACM | 两板实测 | 设备描述符名称为 **GMP Peripheral Host**，序列号由 STM32 唯一 ID 生成；Windows 使用系统 `usbser.sys`，Linux 使用 `cdc_acm`。 |
| 握手、版本、能力和运行状态 | 已实现 | 命令帧含序号、长度和 CRC16。 |
| GPIO / ADC | 已在当前板卡验证 | GUI 固定显示 PA0、PA1 两行；可选输入/ADC，PA0 还可选开漏输出，低/高实测回读为 0/1。PA1 输出和 DAC 因与 TPLD 共享信号而禁用。PA0 上拉电平实测 ADC 原始值 4093；精度校准待验证。 |
| I²C 主机 | 两板实测 | PB5/PB6 软件 I²C，共享 TPLD 配置总线，最多 64 字节写、64 字节读。保留 TPLD 地址 0x79。 |
| TPLD 镜像上传 | 已在当前板卡验证 | 验证 512 字节 HEX，在 STM32 Flash 的两个 8 KiB 槽之间交替提交，CRC32 校验；可设置重启自动加载。 |
| SPI2 主机与从机 | 四种模式两板实测 | Mode 0 使用硬件主机，Mode 1/2/3 使用低速 GPIO 主机；从机使用 NSS 硬件输入和 37 字节中断帧，下一事务返回上一请求的寄存器回复。交叉接线使用从机 I/O 交换。 |
| QEP | 命令已在板上运行 | TIM3 对 PA6/PA7 正交计数、PB7 索引电平，支持启动/读取/清零；外部编码器待验证。 |
| UART / RS232 / RS485 | 两板 115200 baud 实测 | 三路独立接口，支持透明收发和外部请求自动回复 256 字节寄存器组。每路完成远端寄存器读写。 |
| I3C2 控制器与目标端 | 两板实测 | 控制器支持 ENTDAA、私有读写；目标端通过事件中断提供 37 字节寄存器应答，重复配置可重新分配动态地址。 |
| I²C 软件从机 | 两板实测 | PB5 是 I2C1_SMBA 而不是 SDA；用 PB5/PB6 边沿中断处理 7 位地址、寄存器指针、写入及重复起始读取，与软件主机互斥。最高可靠速率尚未测量。 |
| HDC1080 I²C 目标仿真 | 两板实测 | 协议 1.5 支持 16 位温湿度、配置、厂商/器件 ID；#1 主机读取 #2 仿真值通过。 |
| 24LC02B 类 EEPROM 仿真 | 两板压力测试通过 | 协议 1.7；绑定时分配 264 B SRAM，256 B 内容可从电脑镜像载入，GUI 每秒将外部 I²C 修改同步回电脑文件；1000 次随机页写、整片校验及复位后重新载入通过。EEPROM 数据不写入 STM32 Flash。 |
| 16/32/64 KiB EEPROM 页服务 | 实验中 | 镜像保存在电脑，STM32 只分配 144/144/272 B 页缓存。16 KiB 跨页读取已验证，但随机读写压力测试仍有间歇性 I²C 事务失败，暂不作为可靠的通用 EEPROM 从机使用。 |
| 设备资源监控 | #2 板实测 | GUI 第一页有任务管理器式表格和 60 秒曲线，每 1 秒采样一次；显示主循环忙碌率、静态 SRAM、按需分配、可用空间、栈预留及电脑镜像容量。 |
| 经典 CAN | 两板实测 | FDCAN1 与板载 SN65HVD232 支持 125/250/500/1000 kbit/s、标准/扩展 ID 和 0..8 字节，四种速率共 160 帧双向通过；CANopen 目前提供帧解析。 |
| TPLD OTP 烧录 | 不支持 | 当前实现只加载易失性配置；OTP 需要独立的 VPP 条件。 |

## 构建与使用

固件依赖已存在的 STM32Cube H5 HAL 源树，以及 `arm-none-eabi-gcc`：

```powershell
.\scripts\build_gcc.ps1 -Example bridge
```

Windows 也可执行 `scripts\build-all.bat`，一次完成桥接固件、主机 DLL/静态库/CLI 和主机协议测试。Linux 主机脚本同样在安装前运行测试。

输出为 `firmware/GMP_Peripheral_ISO_Host.hex`。`firmware/bridge_flash.ld` 将程序区限制为 112 KiB，留出最后两个 8 KiB 扇区供 TPLD 镜像存储。用此链接脚本构建的固件才允许执行镜像上传。此前两个演示构建配置保持可用：`-Example led_direct` 和 `-Example pulse_distributor`。

Windows 编译主机库和 CLI：

```powershell
.\host\build-windows.bat
.\host\dist\bin\gmpctl.exe COM5 info
.\host\dist\bin\gmpctl.exe COM5 status
.\host\dist\bin\gmpctl.exe COM5 interfaces
.\host\dist\bin\gmpctl.exe COM5 pin PA0 read
.\host\dist\bin\gmpctl.exe COM5 spi config 0 7
.\host\dist\bin\gmpctl.exe COM5 qep start
.\host\dist\bin\gmpctl.exe COM5 uart config slave 115200
.\host\dist\bin\gmpctl.exe COM5 uart reg-set 0x10 AABBCC
.\host\dist\bin\gmpctl.exe COM5 uart reg-get 0x10 3
.\host\dist\bin\gmpctl.exe COM5 spi target 0
.\host\dist\bin\gmpctl.exe COM5 i3c controller
.\host\dist\bin\gmpctl.exe COM5 i3c daa 0x30 1
.\host\dist\bin\gmpctl.exe COM5 i2c-target 0x42
.\host\dist\bin\gmpctl.exe COM5 reg i2c set 0x10 AABBCC
.\host\dist\bin\gmpctl.exe COM5 tpld upload .\cpld\pulse_distributor\program.hex --reset
```

标准 CLI 入口已经加入，保留上面的旧命令以便双板回归：

```powershell
.\host\dist\bin\gmpctl.exe list
.\host\dist\bin\gmpctl.exe --port COM4 i2c config role=slave address=0x50 profile=hdc1080
.\host\dist\bin\gmpctl.exe --port COM4 i2c bind
.\host\dist\bin\gmpctl.exe --port COM4 save config > config.json
```

完整参数、JSON 格式及尚未提供统一 RX 队列的接口见 [CLI 标准](docs/cli-standard.md)。协议 1.8 提供所有九类外设的确认式解绑。

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

GUI 第一页可连接、校验设备、选择 InterConnect Studio `program.hex`、上传并校验镜像、查看启动状态和重启。资源表格与曲线在连接协议 1.7 设备后每秒刷新；CPU 数值是主循环忙碌率，不包括中断执行时间。GPIO 页仅列 PA0/PA1，列出模式、设定和当前值。I²C、I3C、SPI、QEP、UART、RS232、RS485 各有独立标签页，可配置各自支持的角色；SPI 页标明各模式的主机实现。GUI 数据输入使用十六进制字节（空格分隔）。

新版 GUI 还将各物理接口分为参数配置与原始操作页，提供可编辑参数绑定、逐项/批量查询、读写初值以及主机/从机 JSON 导入导出。内置 HDC1080 I²C 仿真、EEPROM、Modbus RTU、AT、CAN 原始帧和 CANopen 解析示例；具体能在硬件上执行的范围见[参数配置说明](docs/ui-profiles.md)。#1 仍运行协议 1.5，#2 已更新到协议 1.8；经典 CAN 四种速率共 160 帧双向实测通过，HDC1080 仿真查询通过。

双板连接后，主机端可用 `gmpctl <port> remote spi 0 get 0x10 3`、`remote i3c 0x30 get 0x10 3`、`remote i2c 0x42 get 0x10 3` 或 `remote uart 0 get 0x10 3` 读另一板的寄存器；对应的 `set` 命令写入。SPI 主机需先 `spi config`，I3C 控制器需先 `i3c controller`、`i3c daa`，串口主机需先配置为 `stream`。GUI 的六个通信页面都提供远端寄存器操作。

具体接线、命令和预期结果见[双板联调步骤](docs/two-board-test.md)。

## TPLD 自动加载及供电

上传的 HEX 必须覆盖地址 `0x0200..0x03FF` 的每个字节。主机校验记录、地址与 checksum，然后把 512 字节和 CRC32 分块发送；设备只有收齐所有字节且 CRC32 一致才擦写备用扇区。设备先写数据，再写提交头；意外断电时上一个有效槽保留。

开启 `autoload` 后，STM32 启动时尝试把镜像写入 TPLD **易失性配置**，再启动 USB。若 TPLD 仍保留旧配置，单独复位 STM32 不一定能重新进入空白配置模式；更换 TPLD 逻辑后，应让整板 3.3 V 完全断电再上电，并通过 `status` 查看启动结果。`TPLD boot status=0` 表示本次加载成功，`255` 表示未尝试；其他值见 [TPLD 状态枚举](H503_Peripheral_DBGer/Core/Inc/tpld2001.h)。

## USB CDC 与未来 HID

首版使用 CDC ACM，设备描述符类/子类为 `02/02`。Windows 系统可按兼容 ID 自动绑定 `usbser.sys`，不必附带专用 INF；Linux 的常规 CDC ACM 驱动提供 `/dev/ttyACM*`。参见 [Microsoft USB serial driver 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-driver-installation-based-on-compatible-ids)。

Windows 设备管理器的串口友好名称当前仍显示为“USB 串行设备 (COMx)”；#2 板 USB 产品字符串和协议 HELLO 已改为“GMP Peripheral Host”，#1 板保留原固件。CLI 和 GUI 读取并显示握手名称。要让 Windows 端口友好名称也一致，需要匹配硬件 ID 的签名 INF；当前原型仍使用系统 `usbser.sys` 自动绑定。

CDC+HID 复合描述符、64 字节 HID 报告和 Windows 签名 INF 的具体方案见 [USB 传输设计](docs/usb-transport.md)。FreeRTOS 任务划分与 32 KiB SRAM 预算见 [任务模型](docs/rtos-architecture.md)；目前运行固件仍是已验证的裸机主循环。

| 选择 | 优点 | 代价 |
| --- | --- | --- |
| CDC ACM（当前） | 系统串口 API 即可在 C/C++/Python 上实现；连续数据和变长协议帧直观；单一主机库可跨平台。 | 系统分配的 COM 号或 `/dev/ttyACM*` 可能变化；Windows 串口通常只能由一个进程独占。 |
| HID（可行的后续方案） | Windows 和 Linux 都有系统 HID 类驱动；可通过 VID/PID 和设备路径定位，而非 COM 号。 | 需要 HID report descriptor、定长报告及分片/重组；512 字节 TPLD 镜像和未来 SPI/I3C 流量的协议实现更复杂。 |

Windows 的系统 HID 驱动和应用接口见 [Microsoft HID 类驱动列表](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/supported-usb-classes) 与 [HID API](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/hid-api)。如果后续确实需要 HID，可以保留现有命令层，替换传输层并规定固定 report 长度。当前描述符中的 VID/PID `CAFE:5030` 仅供内部开发测试；产品发布前需替换为有使用权的 VID/PID。固件已从芯片唯一 ID 生成每板独立的 USB 序列号。

## 硬件接口规划

| 接口 | 主机/控制器 | 从机/目标端 | 当前下一步 |
| --- | --- | --- | --- |
| I²C | 软件主机已实现 | 边沿中断软件从机已实现 | 低速双板寄存器读写通过；高速上限未测。如需保证高速从机时序，需把 SDA 改接支持硬件 I²C SDA 的引脚。 |
| I3C | 控制器 ENTDAA/私有传输已实现 | 动态地址通知与中断应答已实现 | 双板寄存器读写与重新分配地址通过；异常恢复和多目标总线待测。 |
| SPI | Mode 0 硬件主机、Mode 1/2/3 低速 GPIO 主机 | 固定 37 字节中断帧已实现 | 四种模式双板各 20 轮通过；非零模式的时钟分频参数暂不控制 GPIO 时钟，短帧/异常恢复和高速模式待开发。 |
| UART / RS232 / RS485 | 透明收发已实现 | 寄存器应答已实现 | 三条板间链路在 115200 baud 读写通过；其他波特率和连续流量待测。 |
| QEP | 编码器输入已实现 | 不适用 | 用编码器源确认计数方向、索引和溢出。 |
| CAN | 经典 CAN 原始收发已实现 | 对等总线 | 四种速率两板各 40 帧通过；CAN FD 和完整 CANopen 状态机尚未实现。 |

UART 是异步点对点链路，表中的“从机”指应用层收到寄存器访问帧后自动应答；无需像 I²C/I3C 那样分配总线地址。GUI 用“透明收发”和“从机寄存器应答”区分这两种工作方式。

| 接口 | 板级引脚/关系 | 后续实现重点 |
| --- | --- | --- |
| USB FS | PA11/PA12，隔离 USB-C | 设备枚举、连接恢复和吞吐测试。 |
| I²C / TPLD | PB5 SDA、PB6 SCL，共用跳线/上拉 | PB5 在 H503 上是 I2C1_SMBA，不能按 `.ioc` 中的硬件 I2C1_SDA 使用；当前为 GPIO 开漏软件 I²C。 |
| SPI2 | PA8 NSS、PB2 SCK、PB14 MISO、PB15 MOSI | 当前板间 MOSI/MISO 交叉，因此从机启用 SPI I/O swap；四种模式双板读写通过。 |
| I3C2 | PB3 SCL、PB4 SDA | 双板 ENTDAA 与私有读写通过。 |
| QEP | PA6/PA7 TIM3 CH1/CH2，PB7 索引 | 正交计数已实现；外部信号与索引边沿锁存待验证/开发。 |
| RS232 | USART1 PA9/PA10，经板载电平转换器 | MSP 已按板级映射修正，透明收发/寄存器模式已实现。 |
| RS485 | USART2 PB0/PB1 + PB8 DE，经板载收发器 | 透明收发、DE 控制和寄存器模式已实现。 |
| TTL UART | USART3 PB10/PA15 | 透明收发和寄存器模式已实现。 |
| CAN | FDCAN1 PB12/PB13，经 SN65HVD232 到 CANH/CANL | 时钟使用 PLL1Q；两板经典 CAN 双向通信已验证。 |

引脚表以板级手册及实际原理图为准。外部寄存器协议和 SPI/I3C 固定帧要求见[协议文档](docs/protocol.md)。上述双板结果仅覆盖当前接线与测试速率，不代表最高可靠速率。

仓库现有 Cube 生成的 `stm32h5xx_hal_msp.c` 原始映射与板级 SPI/QEP/RS232 网名不符；`H503_BRIDGE` 编译分支已修正实际引脚，其他演示工程的原始生成分支保持不变。后续修改 `.ioc` 时须同步这些板级覆盖。

## 协议与扩展

详见 [协议文档](docs/protocol.md)。协议版本 1 使用一个请求对应一个响应；不支持的命令返回 `UNSUPPORTED`。后续外设通过新的命令号、能力位和文档版本加入，旧主机程序可以先查询 `HELLO` 的能力位。

双板验证：#1 由 J-Link 调试，CDC 为 `COM5`，协议 1.5；#2 由 ST-Link V3 调试，CDC 为 `COM4`，协议 1.8。`scripts/two-board-smoke.ps1 -MasterPort COM5 -TargetPort COM4 -Iterations 20` 对 UART、RS232、RS485、I²C、SPI 四种模式、I3C 各完成 20 轮远端读写，全部通过。`python scripts/two-board-profile-smoke.py COM5 COM4 --iterations 20` 对经典 CAN 四种速率共完成 160 帧双向收发，并验证 HDC1080 仿真。协议 1.7 起的 256 B EEPROM 又经 1000 次随机页写、整片校验、复位重载通过；16 KiB 页服务仍有间歇性 I²C 失败。#1 TPLD 镜像槽更新前后 SHA-256 相同，generation `2`、CRC32 `B032BD7E`；#2 没有 TPLD 镜像，未执行 TPLD 烧录。Linux USB 联机、QEP 外部信号和 CAN FD 仍待验证。

USB 设备栈使用 ST 官方 `stm32-mw-usb-device` 的 Core/CDC 文件，来源修订 `2df324bd60d4b0bb27404fd70b1c089b467f0e09`，授权文本见 [LICENSE](third_party/stm32_usb_device/LICENSE.md)。
