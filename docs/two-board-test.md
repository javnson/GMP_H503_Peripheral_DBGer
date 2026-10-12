# 双板联调步骤

以下命令把控制器板记为 A、目标板记为 B。将 `<A>`、`<B>` 换成两块板各自的 CDC 端口；本次 Windows 实测 A=#1/J-Link/`COM5`、B=#2/ST-Link V3/`COM4`，端口号可能随 USB 连接变化。Linux 为 `/dev/ttyACM*`。每块板分别通过 USB 连接电脑。对应总线还需要在同一逻辑电平域连接信号与参考地，接线和上拉/终端以板级原理图为准。RS232 电平不可直接接到 TTL UART。

本次 #2 的 TPLD 连接及其本地上拉跳线断开，#1 上拉保留，未向 #2 TPLD 烧录。两板固件均从芯片唯一 ID 生成 USB 序列号，可在设备管理器中区分。当前接线可用下列命令一次复测六条总线：

```powershell
.\scripts\two-board-smoke.ps1 -MasterPort COM5 -TargetPort COM4 -Iterations 20
```

脚本只操作 USB、板间总线和 RAM 寄存器，不改 TPLD Flash。2026-10-12 的两板结果为 UART、RS232、RS485、I²C、I3C 及 SPI 四种模式各 20 轮寄存器读写通过；脚本重复运行时 I3C 也能重新分配地址。SPI Mode 0 使用硬件主机 /256，Mode 1/2/3 使用低速 GPIO 主机。

先在两块板上执行：

```text
gmpctl <A> info
gmpctl <B> info
gmpctl <A> interfaces
gmpctl <B> interfaces
```

当前 #1 板报告协议 1.5、能力 `0x1FFFF`；#2 板报告协议 1.8、能力 `0xFFFFF`。各总线可保持连接，测试时分别配置其角色。

## SPI2

连接 A/B 的 NSS、SCK 与参考地，MOSI/MISO 按当前线束交叉连接。固件在 SPI 从机角色启用 I/O 交换，使 A 的输出接到 B 的输入。使用已验证的低速 Mode 0：

```text
gmpctl <B> reg spi set 0x10 AABBCC
gmpctl <B> spi target 0
gmpctl <A> spi config 0 7
gmpctl <A> remote spi 0 get 0x10 3
```

预期输出 `aabbcc`。再执行 `remote spi 0 set 0x10 112233`，并在 B 上 `reg spi get 0x10 3`，预期 `112233`。SPI 从机每次 NSS 帧固定 37 字节，主机库会自动发送第二帧取回上一次请求的结果。四种模式各经 20 轮通过；Mode 1/2/3 当前改用低速 GPIO 主机，其 divider 参数不改变实际时钟。

## I3C2

连接 PB3 SCL、PB4 SDA 与参考地，检查 I3C 上拉配置：

```text
gmpctl <B> reg i3c set 0x10 AABBCC
gmpctl <B> i3c target
gmpctl <A> i3c controller
gmpctl <A> i3c daa 0x30 1
gmpctl <A> remote i3c 0x30 get 0x10 3
```

地址分配预期出现 `0x30`，读取预期 `aabbcc`。若 DAA 报错，先读取两端 `interfaces` 的 I3C 错误码，核对 SDA/SCL 电平与上拉。目标端采用固定 37 字节私有写后读；重新配置角色后会复位 I3C 外设，清除旧动态地址。两板已验证写入及重新地址分配；异常恢复和多目标总线待测。

## I²C 软件从机

连接 PB5 SDA、PB6 SCL 与参考地，确认两板的跳线和上拉。该总线也连接 TPLD，联调期间避免对 TPLD 地址操作。先用低速时钟验证基本事务：

```text
gmpctl <B> reg i2c set 0x10 AABBCC
gmpctl <B> i2c-target 0x42
gmpctl <A> remote i2c 0x42 get 0x10 3
```

预期 `aabbcc`。再写入并从 B 的本机寄存器读取确认。B 的软件从机启用期间，B 上的 I²C 主机命令被拒绝；测试结束执行 `gmpctl <B> i2c-target off`。PB5 不是 H503 硬件 SDA，先确认低速数据和重复起始，之后逐级测量可靠速率。

## UART、RS232、RS485

每种接口单独接线。TTL UART 需交叉连接 A 的 TX 与 B 的 RX、A 的 RX 与 B 的 TX，并连接参考地；RS232/RS485 应从各自板载收发器对应的外部接口接线。以 UART 为例：

```text
gmpctl <B> reg uart set 0x10 AABBCC
gmpctl <B> uart config slave 115200
gmpctl <A> uart config stream 115200
gmpctl <A> remote uart 0 get 0x10 3
```

预期 `aabbcc`。RS232、RS485 将命令中的 `uart` 替换成 `rs232`、`rs485`。RS485 需确认总线端接和收发方向；主机库自动发送寄存器请求并轮询应答，超时为约 1.2 秒。随后测试写入、不同波特率、连续帧和异常 CRC。

## CAN 和 HDC1080 仿真

CANH/CANL 与参考地连接、终端配置就绪后，可先手工验证一帧：

```text
gmpctl <A> can config 500000
gmpctl <B> can config 500000
gmpctl <A> can send 0x123 11223344
gmpctl <B> can recv
gmpctl <A> can status
gmpctl <B> can status
```

应收到 `0x123 standard 11223344`。`can status` 给出 FIFO、bus-off、最近错误和 TX/RX 错误计数；最近错误码 7 表示自上次读取以来没有新错误。FDCAN1 的时钟来自 PLL1Q；若发送 FIFO 不动，先确认 FDCAN 内核时钟与控制器 INIT 位。扩展帧可用 `can send 0x1ABCDE12 01020304 extended`。批量验证：

```powershell
python scripts/two-board-profile-smoke.py COM5 COM4 --iterations 20
```

此脚本在 125/250/500/1000 kbit/s 对每种速率发送 40 帧双向数据，覆盖标准/扩展 ID 与 0/1/8 字节。还发送一帧 CANopen TPDO1 用参数预设解析，随后让 B 以 `0x40` 仿真 HDC1080，由 A 经 I²C 读取温度、湿度、配置和 ID。2026-10-12 实测这 160 帧 CAN、1 帧 TPDO1 解析和 5 项 I²C 查询全部通过；两板错误计数为零。

## EEPROM 与资源页（协议 1.7）

#2 板已运行协议 1.8，#1 板保持协议 1.5。小容量 EEPROM 压测命令：

```powershell
python scripts/two-board-eeprom-stress.py COM5 COM4 --iterations 1000
```

测试覆盖 256 B 全片初始化、8 B 页内写回绕、连续读地址 `0xFF→0x00`、1000 次随机写、每 100 次整片校验，以及 #2 板 MCU 复位后 SRAM 清空和电脑重新上传。实测 1034 次页写、2066 次写周期忙 NACK；该数据未写入 STM32 Flash。

大容量镜像开发压测使用 `python scripts/two-board-eeprom-host-stress.py COM5 COM4 --iterations 1000`。程序在 `tmp/` 创建 16 KiB 电脑镜像，绑定后 #2 板分配 144 B SRAM 页缓冲；随机读写目前偶发 I²C 事务失败，该脚本会以非零退出码报告。跨页首位时序已修正，但尚未达到可宣称可靠 EEPROM 仿真的程度。GUI 第一页连接 #2 板后每 1 秒采样资源：实测静态 SRAM 8728 B，16 KiB 页缓存绑定后按需分配 144 B，可分配 19800 B，栈预留 4096 B。曲线保留最近 60 个采样点。

## 记录与故障定位

每次测试保存两板 `info`、`status`、`interfaces`，并记录总线连线、供电、时钟/波特率、示波器波形和 CLI 原始输出。若某个从机 IRQ 出错，重新配置该接口后再试；若 TPLD 自动加载状态异常，让整板 3.3 V 完全断电再上电，单独复位 MCU 不保证清除 TPLD 易失性配置。
