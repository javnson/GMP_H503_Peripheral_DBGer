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
| `03 GPIO_READ` | port, pin | 电平 0/1；当前只接受 port=0 (GPIOA)，pin=0/1/4/5 |
| `04 GPIO_WRITE` | port, pin, value | 空；当前只接受 PA0，切换为开漏输出 |
| `10 TPLD_BEGIN` | autoload 0/1 | 空；清空临时缓冲 |
| `11 TPLD_CHUNK` | offset `u16`, data | 空；offset+data 不得越过 512 字节 |
| `12 TPLD_COMMIT` | 512 字节镜像 CRC32 `u32` | 空；需所有字节已接收且 CRC 一致 |
| `13 RESET` | 空 | 空；设备应答后约 250 ms 复位 |
| `20 I2C_TRANSFER` | 7 位地址, 写长度, 读长度, 写数据 | 读数据；各方向最多 64 字节，支持写后重复起始读 |

`HELLO` 能力位从低到高：bit0 USB 通道，bit1 GPIO，bit2 I²C，bit3 TPLD 加载，bit4 Flash 暂存。SPI、I3C、QEP、UART/RS232/RS485 的能力位尚未分配。

TPLD 镜像存储在 `0x0801C000`（bank2 sector6）及 `0x0801E000`（bank2 sector7），每槽头部 32 字节，后跟 512 字节配置。槽头包含 magic、generation、长度、CRC32、autoload。提交新镜像时只擦备用槽，写镜像后最后写 magic 头；启动时选有效且 generation 最新的槽。程序链接最大地址为 `0x0801BFFF`。

设计限制：当前 USB 接收环形缓冲区 1024 字节，在主循环执行 Flash 写入时可能产生背压或丢字节；主机库采用严格的请求/响应流，不并发发送。I²C 与 TPLD 共用总线，外部主机不应同时操作该总线。未来从机模式需独立事件队列和寄存器映射命令。
