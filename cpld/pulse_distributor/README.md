# 六拍脉冲分配器例程

本例由 STM32H503 的 TIM2/PA0 产生 10 Hz、50% 占空比方波，经 TPLD2001 IO12 作为 CP 时钟。TPLD 在每个 CP 上升沿切换一次输出状态，方向输入 X 使用板上 GPI 按键：松开时外部 5.1 kΩ 上拉使 X=1，按下时 X=0。X 在时钟上升沿被采样；改变方向后，下一个上升沿起反向步进。

**SW3 是方向按键。SW2 接在 PA0/CP 上，按下会把时钟线拉低，不控制方向。** PA0 使用 TIM2 开漏复用输出和板上 R12 的 5.1 kΩ 上拉；误按 SW2 时不会与 MCU 的推挽高电平短接。SW1 是 MCU 复位键；仅按 SW1 不会清除 TPLD 的易失性配置，若复位后 `tpld_status=1`，请让整板 3.3 V 完全掉电后重新上电。

| 信号 | TPLD 引脚 | 板上连接 | 用途 |
| --- | --- | --- | --- |
| CP | IO12 | H503 PA0 | 10 Hz 方波输入 |
| X | GPI/IN0 | SW3 按键，上拉高 | 高电平正转，低电平反转 |
| A | IO11 | H503 PA1 | 高电平有效的相位输出 |
| B | IO10 | H503 PA4 / DAC_1 | 高电平有效的相位输出 |
| C | IO9 | H503 PA5 / DAC_2 | 高电平有效的相位输出 |
| A/B/C LED | IO4/IO5/IO8 | LED3/LED4/LED5 | 与相位同亮；LED 支路在 3.3 V 侧，TPLD 输出经过反相 |

状态按 `(A B C)` 顺序表示。X=1 时：`100 → 110 → 010 → 011 → 001 → 101 → 100`；X=0 时顺序相反：`100 → 101 → 001 → 011 → 010 → 110 → 100`。每拍 100 ms，六拍周期 600 ms。三个触发器保存 A/B/C；其初始状态为 `100`。组合逻辑为：

| X | A 下一拍 | B 下一拍 | C 下一拍 |
| --- | --- | --- | --- |
| 1 | ¬B | ¬C | ¬A |
| 0 | ¬C | ¬A | ¬B |

## 构建与观察

在仓库根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/generate_pulse_distributor.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_gcc.ps1 -Example pulse_distributor
```

第一个脚本调用 TI InterConnect Studio 1.8.0 CLI 校验 `pulse_distributor.syscfg` 并生成 `program.hex`，再转换为固件内的 512 字节配置头文件。第二个脚本生成 `firmware/H503_TPLD_pulse_distributor_10Hz.hex`。Keil 工程默认编译 `led_direct`；若用 Keil 构建本例，需在 C 编译器预定义宏中加入 `TPLD_PULSE_DISTRIBUTOR`。

下载新固件后，整板 3.3 V 需完全断电再上电，让 TPLD 清除上一例的易失性配置。调试器中 `tpld_status=0` 表示新配置加载成功；`tpld_phase_abc` 的 bit2/bit1/bit0 分别为 A/B/C；`tpld_phase_passes` 和 `tpld_phase_failures` 每个时钟周期检查一次状态是否沿六拍环相邻跳转。可用示波器分别测 PA0、PA1、PA4、PA5，按键后观察输出顺序反向。

本板在确认目标电压降至 0 V 后重新上电，实测 `tpld_status=0`、I²C 地址 `0x79`，连续 63 次相邻状态跳转检查通过、失败 0 次。仅复位 MCU 或某些调试器重新连接后，TPLD 仍保留已激活配置，MCU 可能返回 `tpld_status=1` 并保持 PA0 高阻；此时需再次整板掉电重上电。

PA0 改用开漏输出并重新写入固件后，已再次整板掉电上电，板上 SW3 控制方向切换得到确认。

本例只产生三路逻辑信号，没有电机功率驱动级。不要直接将电机接到 TPLD 引脚。
