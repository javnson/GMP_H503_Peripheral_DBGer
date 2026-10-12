# GMP Peripheral ISO Host 协议 v1

传输层为 USB CDC ACM 串口，端口波特率设置不影响 USB 实际速率。多字节整数均为小端；每次只允许一个待处理请求。CRC16 使用 CCITT-FALSE：初始值 `0xFFFF`，多项式 `0x1021`，覆盖从魔数到 payload 末字节。

| 字段 | 字节数 | 说明 |
| --- | ---: | --- |
| magic | 2 | `A5 5A` |
| version | 1 | `01` |
| command | 1 | 请求命令号；响应为原号 `| 0x80` |
| sequence | 1 | 请求与响应相同 |
| length | 2 | payload 长度，最多 256 |
| payload | 0..256 | 响应的第一个字节固定为结果码 |
| CRC16 | 2 | 低字节先发 |

结果码：`0=OK`、`1=INVALID`、`2=IO`、`3=UNSUPPORTED`、`4=INCOMPLETE`。损坏的请求帧无响应，主机端按超时处理。

| 命令 | 请求 payload | 成功响应 payload（结果码之后） |
| --- | --- | --- |
| `01 HELLO` | 空 | 协议主/次版本各 1 字节，能力位 `u32`，设备名 UTF-8 |
| `02 STATUS` | 空 | TPLD 启动结果、地址、镜像存在、自动加载各 1 字节；generation `u32`、CRC32 `u32` |
| `03 GPIO_READ` | port, pin | 电平 0/1；兼容命令，port=0，pin=0..5 |
| `04 GPIO_WRITE` | port, pin, value | 空；当前只接受 PA0，切换为开漏输出 |
| `05 GPIO_CONFIG` | port=0, pin, mode | 空；mode 0=输入、1=输出、2=ADC、3=DAC。PA0/PA1 支持输入/ADC，只有 PA0 可输出；PA4/PA5 与 TPLD 共享，不开放 DAC 输出 |
| `06 PIN_READ` | port=0, pin | mode `u8`，当前值 `u16`；数字模式返回 0/1，ADC 返回 0..4095 |
| `07 PIN_SET` | port=0, pin, value `u16` | 空；只有已配置的输出/DAC 可设置，当前板可用的是 PA0 数字输出 |
| `10 TPLD_BEGIN` | autoload 0/1 | 空；清空临时缓冲 |
| `11 TPLD_CHUNK` | offset `u16`, data | 空；offset+data 不得越过 512 字节 |
| `12 TPLD_COMMIT` | 512 字节镜像 CRC32 `u32` | 空；需所有字节已接收且 CRC 一致 |
| `13 RESET` | 空 | 空；设备应答后约 250 ms 复位 |
| `20 I2C_TRANSFER` | 7 位地址, 写长度, 读长度, 写数据 | 读数据；各方向最多 64 字节，支持写后重复起始读 |
| `21 I2C_TARGET_CONFIG` | enable 0/1, 7 位地址 | 空；PB5/PB6 软件从机启用/关闭，与主机命令互斥 |
| `22 I2C_TARGET_PROFILE` | profile、register、16 位大端值 | 空；profile=0 平坦字节寄存器，profile=1 HDC1080 仿真，后者可设温度 0x00、湿度 0x01、配置 0x02 |
| `23 I2C_TARGET_WORD` | 16 位寄存器地址的低 8 位 | 当前仿真寄存器值，大端 2 字节 |
| `30 SPI_CONFIG` | role, mode, divider | 空；role=0 主机、1 固定帧从机；mode=0..3。Mode 0 硬件主机使用 divider=0..7 对应 /2..../256；Mode 1/2/3 当前为低速 GPIO 主机，divider 暂不控制时钟。 |
| `31 SPI_TRANSFER` | 1..64 字节发送数据 | 等长接收数据；PA8 片选拉低期间全双工传输 |
| `40 QEP_START` | 空 | 空；TIM3 编码器 x4 初始化 |
| `41 QEP_READ` | 空 | 有符号位置 `i32`、I 电平、计数方向各 1 字节 |
| `42 QEP_RESET` | 空 | 空；位置清零 |
| `50 SERIAL_CONFIG` | port, role, baud `u32` | 空；port=0 UART、1 RS232、2 RS485；role=0 透明收发、1 寄存器应答；1200..2000000 baud，8N1 |
| `51 SERIAL_WRITE` | port, 1..64 字节数据 | 空；串口发送，RS485 自动控制 PB8 DE |
| `52 SERIAL_READ` | port, 最大 0..64 字节 | 当前收到的数据；仅透明收发模式可用 |
| `53 REGISTER_WRITE` | port, 地址, 1..32 字节数据 | 空；预置 256 字节易失性寄存器组；port 0 UART、1 RS232、2 RS485、3 SPI、4 I3C、5 I²C |
| `54 REGISTER_READ` | port, 地址, 长度 1..32 | 寄存器数据 |
| `60 I3C_CONFIG` | role 0/1 | 空；0 控制器、1 目标设备 |
| `61 I3C_DAA` | 起始动态地址、最大设备数 1..8 | 实际设备数，后跟依次分配的动态地址 |
| `62 I3C_TRANSFER` | 目标动态地址、写长度、读长度、写数据 | 读数据；各方向最多 64 字节，可写后读 |
| `70 INTERFACE_STATUS` | 空 | SPI ready/role/error `u32`，I3C ready/role/error `u32`，I²C 从机 active/address，串口就绪位掩码；共 15 字节 |
| `71 CAN_CONFIG` | 标称速率 `u32`，支持 125000/250000/500000/1000000 | 空；经典 CAN 正常模式 |
| `72 CAN_TX` | 标识符 `u32`、extended 0/1、长度 0..8、数据 | 空；帧进入发送 FIFO |
| `73 CAN_RX` | 空 | present=0，或 present=1、标识符 `u32`、extended、长度、数据 |
| `74 CAN_STATUS` | 空 | ready、TX FIFO 空位、RX FIFO 待取、bus-off、最近错误码、TX/RX 错误计数，各 1 字节 |

`HELLO` 版本 1.4 能力位：bit0 USB、bit1 GPIO、bit2 I²C 主机、bit3 TPLD 加载、bit4 Flash 暂存、bit5 SPI 主机、bit6 QEP、bit7 TTL UART、bit8 RS232、bit9 RS485、bit10 串口寄存器应答、bit11 SPI 从机、bit12 I3C 控制器、bit13 I3C 目标设备、bit14 I²C 软件从机。能力位表示软件路径已实现；当前双板已验证的模式和速率见 README。

版本 1.5 保留以上位，新增 bit15 HDC1080 I²C 目标仿真、bit16 经典 CAN 原始帧。CAN1 接 PB12/PB13 和板载 SN65HVD232，内核时钟选用已运行的 PLL1Q；两板在 125/250/500/1000 kbit/s 各完成 40 帧双向收发。HDC1080 仿真提供寄存器指针读和配置寄存器写，不模拟转换等待时间；详见[GUI 参数文档](ui-profiles.md)。

UART、RS232、RS485、SPI 和 I3C 目标端共享应用层寄存器帧：请求 `A5 op address length [data] crc8`，`op=0` 读（无 data），`op=1` 写（带 length 字节）；响应 `5A status length [data] crc8`。CRC8 初始 0、多项式 `0x07`，覆盖帧头至数据末字节。每次长度 1..32 字节，地址不允许越过 255。每个物理接口有独立的 256 字节 RAM 寄存器组，复位后清零。C API 的 `gmp_remote_register_read/write` 对 UART/RS232/RS485 也封装了发帧与等候回复；本机串口须配置为透明收发模式。

SPI 从机：每次 NSS 片选期间必须恰好发送 37 字节，请求不足部分补零。当前事务同时收到的是**上一事务**的回复；用第二个 37 字节全零事务取回本次回复。当前交叉线接法由从机启用 SPI I/O 交换；Mode 0 使用硬件主机 /256，Mode 1/2/3 使用低速 GPIO 主机，四种模式均已完成双板各 20 轮连续读写。I3C 目标端：由控制器先执行 ENTDAA，再发送固定 37 字节私有写和固定 37 字节私有读；读事务取得刚写入请求的回复。C API 的 `gmp_remote_register_read/write` 封装这两种时序。串口外部主机需保持帧内字节间隔小于 100 ms；每路透明收发缓冲区 128 字节，满时新字节丢弃。

I²C 软件从机：PB5 SDA、PB6 SCL 使用双边沿 EXTI，7 位地址由 `21` 命令设定。主机写入的首字节是寄存器指针，后续字节依次写入；重复起始后读取从该指针开始的数据。C API 的 `gmp_remote_register_read/write` 在 bus=5 时封装这一事务。该模式与 `20 I2C_TRANSFER` 互斥，也与 TPLD 配置总线共线。当前双板低速读写已通过；因 PB5 不是硬件 SDA，引脚时序上限尚未确认。

TPLD 镜像存储在 `0x0801C000`（bank2 sector6）及 `0x0801E000`（bank2 sector7），每槽头部 32 字节，后跟 512 字节配置。槽头包含 magic、generation、长度、CRC32、autoload。提交新镜像时只擦备用槽，写镜像后最后写 magic 头；启动时选有效且 generation 最新的槽。程序链接最大地址为 `0x0801BFFF`。

设计限制：当前 USB 接收环形缓冲区 1024 字节，在主循环执行 Flash 写入时可能产生背压或丢字节；主机库采用严格的请求/响应流，不并发发送。I²C 与 TPLD 共用总线，外部主机不应同时操作该总线。两板互联已验证本文所列基础帧；最高可靠速率、SPI 非零模式的硬件主机、NSS 异常恢复仍待处理。
