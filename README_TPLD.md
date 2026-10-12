# H503 板载 TPLD2001 直通验证

当前固件在 H503 上运行时，通过软件 I²C 将 `cpld/pa1_to_pa0/program.hex` 的 512 字节配置加载到 TPLD2001 的配置寄存器，逐字节回读，再验证 PA1 → TPLD IO11 → TPLD IO12 → PA0。上电后自动执行，无需上位机命令。

## 硬件连接

- J11 的 1–2 跳帽连接 PB6 / SCL 与 TPLD IO6；3–4 跳帽连接 PB5 / SDA 与 TPLD IO7。I²C 上拉跳帽需要接通。
- H503 的 PB5 在芯片数据手册中只有 `I2C1_SMBA`，并非 `I2C1_SDA`。因此固件使用 PB5/PB6 开漏 GPIO 实现软件 I²C，**不要在此板上调用 `MX_I2C1_Init()` 来访问 TPLD**。
- PA1 接 TPLD IO11，PA0 接 IO12。当前 CPLD 镜像让 IO11 为输入、IO12 为输出。

## 构建与观察

- Keil 工程：`H503_Peripheral_DBGer/MDK-ARM/H503_Peripheral_DBGer.uvprojx`。
- 无 Keil 编译器许可证时，可运行 `pwsh scripts/build_gcc.ps1`。该脚本使用 STM32Cube FW_H5 V1.6.0 自带的 H503 GCC 启动文件/链接脚本；工程的 `Drivers/` 由 CubeMX 从同一固件包生成，不纳入 Git。
- 已验证的 H503 固件：`firmware/H503_TPLD_PA1_to_PA0.hex`，可通过 J-Link 写入 STM32H503CB。
- 调试器观察 `tpld_status`：`0` 表示本次加载和回读成功；`7` 表示仅 MCU 复位而 TPLD 配置仍在，直通验证成功；其他值见 `Core/Inc/tpld2001.h`。
- `tpld_i2c_address` 是检测到的 7 位地址；`tpld_loopback_ok` 为最近一次高低电平检查结果；`tpld_loopback_passes` 与 `tpld_loopback_failures` 为累计次数。

在本板实测，空白器件地址为 `0x79`，首次加载返回 `tpld_status=0`，直通检查连续通过。仅复位 MCU 后返回 `tpld_status=7`，直通仍通过。

## 重新生成 CPLD 镜像

在 TI InterConnect Studio 1.8.0 中打开 `cpld/pa1_to_pa0/pa1_to_pa0.syscfg` 并生成 `program.hex`。随后运行 `python cpld/pa1_to_pa0/generate_image.py`，更新嵌入固件的 `Core/Inc/tpld2001_image.h`。转换脚本检查 0x200–0x3FF 共 512 字节及 TI 工具生成的校验和格式。

## 永久烧录的硬件条件

本固件执行的是**易失性配置加载**，掉电后 TPLD 恢复原有 OTP 内容。TI 对 TPLD2001 的 OTP 烧录要求先在 GPI 引脚施加 7.5–8 V 的 VPP，再发出烧录命令。当前原理图的 GPI 没有由 H503 控制的 VPP 电源/开关，只有按键和 3.3 V 上拉，因此仅靠现有 H503 与跳帽无法安全完成永久 OTP 烧录。本工程不会发出 OTP 烧录命令。

原 `.ioc` 是全外设规划文件；当前验证固件只初始化 GPIO，并在 `main.c` 中配置本验证用引脚。重新用 CubeMX 生成代码时，应保留这段用户代码及软件 I²C 驱动，并核对 PB5 的复用功能。
