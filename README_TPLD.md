# H503 + TPLD2001：IO12 输入 10 Hz 方波

正式调试器固件及 USB 主机工具见 [GMP Peripheral ISO Host](README.md)；本文仅说明独立的 `led_direct` 演示配置。

另见 [六拍脉冲分配器例程](cpld/pulse_distributor/README.md)：同样使用 PA0 的 10 Hz 时钟，输出 A/B/C 三路相位并由板上按键控制方向。

固件上电后用 PB5/PB6 软件 I²C 将 `cpld/led_direct/program.hex` 的 512 字节配置加载到板载 TPLD2001，并逐字节回读验证。加载成功后，H503 的 TIM2 CH1 从 PA0 输出 10 Hz、50% 占空比方波，送入 TPLD IO12。`led_direct` 工程将 IO12 直通到 IO11；H503 从 PA1 采样回读，同时该工程还驱动板上的 RGB LED 逻辑。

## 硬件连接

- PA0 连接 TPLD IO12（输入），PA1 连接 TPLD IO11（输出）。
- PA0 还连接 SW2、5.1 kΩ 上拉和 100 nF 电容。10 Hz TIM2 输出采用开漏模式；SW2 会拉低时钟线，不是方向键。脉冲分配器的方向键为 SW3。
- J11 跳帽连接 PB6 与 TPLD IO6/SCL、PB5 与 TPLD IO7/SDA；I²C 上拉跳帽也需接通。
- H503 的 PB5 复用功能是 `I2C1_SMBA`，因此这里使用开漏 GPIO 模拟 I²C，而不调用 `MX_I2C1_Init()`。

## 构建和下载

1. 在 TI InterConnect Studio 中打开 `cpld/led_direct/led_test.syscfg`，修改设计后生成 `cpld/led_direct/program.hex`。仓库中的 `program.hex` 已对应当前 `led_direct` 设计。
2. 在仓库根目录运行 `python scripts/generate_tpld_image.py`，更新 `H503_Peripheral_DBGer/Core/Inc/tpld2001_image.h`。脚本检查配置地址 `0x200` 至 `0x3FF` 的全部 512 字节及 HEX 校验和。
3. 在 Keil 打开 `H503_Peripheral_DBGer/MDK-ARM/H503_Peripheral_DBGer.uvprojx` 构建，或使用 PowerShell 运行 `scripts/build_gcc.ps1`。GCC 脚本需要 `arm-none-eabi-gcc` 和本地 STM32Cube FW_H5 V1.6.0；CubeMX 生成的 `Drivers/` 不纳入 Git。
4. 将 `firmware/H503_TPLD_led_direct_10Hz.hex` 下载到 STM32H503CB。**更新 TPLD 镜像后需让整板 3.3 V 完全断电再上电**，因为仅复位 MCU 不会清除 TPLD 当前的易失性配置；当前设计激活后不再响应配置 I²C。

原先 `cpld/pa1_to_pa0/program.hex` 的镜像仍可用 `python scripts/generate_tpld_image.py cpld/pa1_to_pa0/program.hex` 转换，但随后需重新构建固件；`firmware/H503_TPLD_PA1_to_PA0.hex` 是此前版本。

## 运行状态

调试器可观察下列全局变量：

- `tpld_status == 0`：TPLD 配置写入、回读及激活成功，TIM2 方波已启动；错误值定义见 `Core/Inc/tpld2001.h`。`1` 常表示 MCU 单独复位后，已激活的 TPLD 不再响应 I²C；`7` 表示 PWM 初始化失败。
- `tpld_i2c_address`：检测到的 TPLD 7 位 I²C 地址。本板空白器件实测为 `0x79`。
- `tpld_loopback_passes` 和 `tpld_loopback_failures`：在 PWM 高、低电平中段采样 PA1 的累计通过和失败次数。`tpld_loopback_ok` 是最近一次采样结果。

TIM2 输入时钟为 250 MHz，预分频寄存器值 `24999`、自动重装值 `999`、比较值 `500`，对应 `250 MHz / 25000 / 1000 = 10 Hz` 和 50% 占空比。只有配置加载成功才将 PA0 切为 TIM2 输出；加载失败时 PA0/PA1 保持输入模式。

## 烧录范围

此固件执行**易失性配置加载**。TPLD 掉电后恢复原有 OTP 内容，重新上电时由 H503 再次加载配置。TPLD2001 的永久 OTP 烧录需要在 GPI 提供 7.5–8 V VPP；现有板上没有受 H503 控制的 VPP 电路，因此本工程不执行永久 OTP 烧录。

原 `.ioc` 是全外设规划文件；当前固件只初始化本功能需要的 GPIO 和 TIM2。若重新用 CubeMX 生成代码，应保留 `main.c` 的用户代码、TPLD 驱动，并核对 PB5 复用功能。
