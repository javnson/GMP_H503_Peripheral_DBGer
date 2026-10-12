# GUI 参数绑定与配置文件

GUI 的每个物理接口有独立标签。GPIO/QEP 提供参数查询；I²C、I3C、SPI、UART、RS232、RS485、CAN 分为主机和从机配置页。原有字节级操作保留在“原始操作”。配置页可添加、删除、查询参数，修改“初值”列后可写入允许写入的参数。导入 JSON 只更新界面；点击“应用接口配置”才操作设备。

## JSON 格式

内置示例位于 [`gui/presets`](../gui/presets)。所有配置使用 UTF-8、`schema: 1`，包含 `name`、`interface`、`role`、`protocol`、`settings` 和 `parameters`。`role` 为 `master` 或 `slave`；`interface` 为 `gpio/i2c/i3c/spi/qep/uart/rs232/rs485/can`。导入时会校验接口、角色、地址、类型、长度和参数数量；JSON 中没有可执行脚本。

```json
{
  "schema": 1,
  "name": "I2C 寄存器示例",
  "interface": "i2c",
  "role": "master",
  "protocol": "bridge-register",
  "settings": {"address": 66},
  "parameters": [
    {"name": "状态", "register": 16, "type": "u8", "access": "r"},
    {"name": "阈值", "register": 17, "type": "u16", "endian": "big", "scale": 0.1,
     "offset": 0, "unit": "V", "access": "rw", "value": 12.5}
  ]
}
```

`type` 支持 `u8/i8/u16/i16/u32/i32`。原始整数按 `raw × scale + offset` 显示；写入时执行反向换算和范围检查。`endian` 为 `big` 或 `little`，默认大端。`access` 为 `r/rw`。`value` 是可选的目标初值或主机待写值。CAN 绑定使用 `can_id` 和 `byte_offset`，其中 `offset` 仍专用于数值换算；QEP 使用 `source: position/index/direction`，GPIO 使用 `pin: 0/1`。AT 参数用 `command` 和可选的 `pattern` 正则表达式提取响应。

## 当前执行能力

| 协议/接口 | 主机参数查询 | 从机加载/查询 | 说明 |
| --- | --- | --- | --- |
| 桥接寄存器 I²C/I3C/SPI/串口 | 已实现 | 已实现 | I3C 主机需先在原始操作页 ENTDAA；SPI 和 I3C 使用固定 37 字节帧。 |
| HDC1080 I²C | 已实现 | 固件 1.5 起提供仿真 | 目标地址默认 `0x40`；温度、湿度、配置和 ID 按 16 位寄存器返回。目标侧可修改模拟温湿度；通过 I²C 总线读温湿度为只读。当前仿真没有实际传感器的转换延时和 NACK 忙状态。 |
| Modbus RTU 串口 | 功能码 03 查询及 06 写单寄存器 | 待实现 | 主机用现有透明串口，CRC16 校验；没有 Modbus 从机固件。 |
| AT 串口 | 发送命令、读取响应、可按正则提取 | 待实现 | 设备透明串口作为 AT 主机。 |
| CAN 原始帧 | 固件 1.5 起支持经典 CAN 配置/收发 | CAN 为对等总线，两个页均可配置/收发 | 支持 125/250/500/1000 kbit/s、11/29 位标识符、0..8 字节。发送调用表示帧已进入发送队列，不保证总线已经 ACK。CAN FD 尚未启用。 |
| CANopen | 常见 COB-ID 分类和 PDO 参数映射解析 | 协议栈待实现 | 当前可对手动输入或收到的经典 CAN 帧解析；尚无 NMT 状态机、SDO/PDO 通信管理。 |
| I3C 引脚上的 I²C 兼容模式 | UI 可选择 | UI 可选择 | PB3/PB4 的 I²C 主机/从机驱动待实现；点击应用会明确提示。纯 I3C 控制器/目标端已可用。 |
| GPIO/QEP | 端口当前值查询 | 不适用 | GPIO 保留 PA0/PA1 两行原始控制；QEP 查询位置、索引电平与方向。 |

HDC1080 示例依据 [TI 数据手册](https://www.ti.com/lit/ds/symlink/hdc1080.pdf) 的寄存器表、温湿度换算和 `0x5449/0x1050` ID。仿真示例用于调试 I²C 软件路径；它不是完整的时序/功耗模型。

## 使用示例

1. 在目标板的 I²C“从机配置 / 参数”导入 `hdc1080-slave.json`，点击“应用接口配置”。目标板固件必须为协议 1.5 或更新。
2. 在另一板的 I²C“主机配置 / 参数”导入 `hdc1080-master.json`，应用主机模式，点击“查询全部”。预期约 25 °C、50 %RH，厂商 ID 为 `0x5449`、器件 ID 为 `0x1050`。
3. 对其他接口先导入相应预设，配置角色、地址/波特率，随后查询或写入参数。跨板通信的物理接线与旧版测试步骤见 [`two-board-test.md`](two-board-test.md)。

## 实物验证（2026-10-12）

两块板已更新到协议 1.5，CANH/CANL 已互连。`python scripts/two-board-profile-smoke.py COM5 COM4 --iterations 20` 在 125/250/500/1000 kbit/s 各完成 40 帧双向收发，共 160 帧，覆盖标准/扩展 ID 与 0/1/8 字节数据；两板均无 bus-off 或错误计数。HDC1080 仿真从 #2 经 I²C 被 #1 读取到约 25 °C、50 %RH、厂商 ID `0x5449` 和器件 ID `0x1050`。GUI 绑定表的导入、应用和批量查询也经过这两块板验证。既有两板回归脚本对 UART、RS232、RS485、I²C、SPI 四模式和 I3C 各完成 20 轮读写，通过。#1 的 TPLD 镜像槽前后 SHA-256 相同。
