# GMP Peripheral Host CLI 标准（v0.1）

Windows 使用 `host/dist/bin/gmpctl.exe`，Linux 使用 `host/dist-linux/bin/gmpctl`。下文的 `tool` 指该程序。两块板并存时用 `tool --port COM4 ...`（Linux 为 `/dev/ttyACM0`）指定目标，或预先设置 `GMP_PORT`。旧版 `tool COM4 ...` 命令继续可用。

## 标准命令

```text
tool list
tool --port COM4 <interface> config [key=value ...] [-e:profile.json]
tool --port COM4 <interface> bind
tool --port COM4 <interface> unbind
tool --port COM4 <interface> tx <hex> [--address=N] [--read=N] [--id=N] [--extended]
tool --port COM4 <interface> tx --json '{"data":"aabb", "address":80, "read":2}'
tool --port COM4 <interface> tx -e:packet.json
tool --port COM4 <interface> rx [--json] [-o:received.json]
tool --port COM4 [<interface>] save config
tool --port COM4 [<interface>] load config [-e:profile.json | profile.json | stdin]
```

`config` 参数顺序任意，省略参数时仍打印完整有效配置。`-e:` 读取 JSON 中与外设标记同名的段；`{}` 和缺少该段时用当前配置或默认值。命令行 `key=value` 覆盖文件值；未知字段、类型错误、语法错误均返回非零退出码。已绑定的接口拒绝重新配置，并打印当前配置。`save config` 始终输出 JSON 到 stdout，故可以用 `> config.json`；`load config` 可以用 `< config.json`。载入时先验证全部涉及的接口，再写入主机状态。

`bind` 把配置发送到 STM32，成功后才记录绑定；配置与绑定标记存于电脑用户目录的 `GMP_Peripheral_Host/<端口>.json`，可用 `GMP_STATE_DIR` 改位置。设备复位、COM 号复用或其它程序改动配置后，主机状态可能失效；重新绑定前应核对设备状态。协议 1.8 的 `unbind` 会先让 STM32 关闭相应外设，成功后才清除本机配置；旧版固件拒绝此命令，本机状态保持原样。GPIO 解绑会把 PA0/PA1 都设为输入。

`tx` 的原始数据是连续十六进制字节，`--json` 则将命令行实参解析为包对象。`-e:packet.json` 也接受包对象。SPI 全双工及 I²C/I3C 写后读在 `tx` 的返回中给出接收数据。`rx` 从设备现有 UART/RS232/RS485 缓冲区或 CAN RX FIFO 取一条；空返回 `empty`，JSON 模式返回 `null`。`-o:` 写 JSON 文件。其它外设的异步 RX 队列及统一 overflow 位还没有固件实现，因此 `rx` 会报不支持，不能把空值当作无消息。串口缓冲区满时当前固件丢弃新字节，尚无可查询的溢出计数。

## 默认值和固件约束

| 标记 | 默认配置 | 当前约束 |
| --- | --- | --- |
| `gpio` | `pin=PA0 mode=input value=0` | PA1 仅输入/ADC；不能输出。 |
| `i2c` | `role=master address=80 baud=100000 profile=generic capacity=256 page_size=8` | 软件总线时钟目前固定，`baud` 除 100000 之外不能绑定；16 KiB 及以上 EEPROM 需持续的 PC 页服务进程。 |
| `i3c` | `role=i3c-master address=48` | 可配置 `i3c-master`、`i3c-slave`；`i2c-master` / `i2c-slave` 保留配置项，但固件尚不能绑定到 I3C 引脚。 |
| `spi` | `role=master mode=0 divider=7` | slave 使用固定 37 字节寄存器帧；mode 1/2/3 主机为 GPIO 低速实现。 |
| `qep` | `role=counter` | 仅输入计数。 |
| `uart` / `rs232` / `rs485` | `role=master baud=115200 protocol=raw` | `slave` 表示应用层寄存器自动应答；`modbus-rtu`、`at` 是主机预设，尚无对应固件状态机。 |
| `can` | `role=peer baud=500000 protocol=raw` | CANopen 当前仅主机解析，不是设备端协议栈。 |

各速率单位为 bit/s 或 Hz，命令行可写 `baud=100k`、`baud=1M`；JSON 用整数。配置只在对应接口 `bind` 时下发，不会为未绑定的接口分配仿真内存。USB CDC 线编码默认 921600 8N1；这一数值不改变 USB Full Speed 的物理带宽。

## 标准 JSON

```json
{
  "i2c": {"role":"slave", "address":80, "baud":100000, "profile":"eeprom", "capacity":256, "page_size":8},
  "spi": {"role":"master", "mode":0, "divider":7},
  "uart": {"role":"master", "baud":115200, "protocol":"raw"}
}
```

其他段可以省略，`{}` 有效。也接受外层 `{"interfaces":{...}}`。`save config` 输出所有或选定接口的完整参数。I²C 16/32/64 KiB EEPROM 镜像保存在电脑 `.bin`，使用现有 `tool COM4 eeprom serve image.bin` 提供页服务；标准 `bind` 在页服务尚未整合前拒绝这种配置。配置 JSON 不含 EEPROM 内容，避免误把配置文件当作数据持久化。

## 建议的后续协议版本

下一固件版本将增加统一循环消息队列：包头包含接口号、方向、时间戳、长度、序号；队列满时覆盖最旧包、递增 `overflow_count`，查询后保留计数。届时 `rx` 才能对 I²C/I3C/SPI 目标端提供统一行为。队列读出必须由固件确认，CLI 才报告消费成功。
