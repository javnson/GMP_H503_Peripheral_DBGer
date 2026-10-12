# STM32H503 任务模型与 FreeRTOS 迁移设计

FreeRTOS 的执行单元是**任务**，在 H503 上共享一个地址空间，不提供 PC 操作系统那样隔离的进程。当前固件仍运行单一主循环和中断；本文件规定任务边界和迁移门槛，避免改变已验证的两板时序。

## 资源边界

H503 有 32 KiB SRAM，当前桥接镜像链接结果为 `data=260 B, bss=10004 B`，静态占用约 10.3 KiB；主循环资源查询按 4 KiB 栈预留估计可分配内存。引入 FreeRTOS 后，不能继续让现有 `BridgeMetrics_Alloc` 与内核堆互相重叠。优先使用 `xTaskCreateStatic`、`xQueueCreateStatic`，并给 EEPROM 页缓存单独的有界内存池。初步预算：协议/USB 任务 3 KiB 栈、总线服务 2 KiB、EEPROM 页服务 1.5 KiB、监控 1 KiB、异常/中断栈 2 KiB、队列与内核对象约 2 KiB；剩余空间需以 map 文件和任务栈最低余量实测，不以估算替代验收。FreeRTOS 支持由应用提供任务、队列等静态存储；任务栈最低余量可通过 `uxTaskGetStackHighWaterMark` 观测。[FreeRTOS 静态内存分配](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/03-Static-vs-Dynamic-memory-allocation)、[栈余量说明](https://www.freertos.org/media/2018/FreeRTOS_Reference_Manual_V10.0.0.pdf)。

## 标准任务和所有权

| 任务 | 优先级（相对） | 唯一拥有的资源 | 输入/输出 |
| --- | ---: | --- | --- |
| `usb_protocol` | 高 | CDC 请求帧、将来的 HID 控制报告、命令序号与响应 | USB 中断只投递字节/事件；该任务校验并提交命令。 |
| `bus_service` | 高 | I²C 主机、SPI/I3C、串口、CAN、QEP 和 GPIO 配置 | 从协议任务接收有界请求；完成后投递结果及异步事件。一个物理外设在任一时刻只有一个所有者。 |
| `image_service` | 中 | TPLD Flash 提交、EEPROM 页缓存元数据 | 接收显式上传/页请求。电脑 `.bin` 仍是大容量 EEPROM 权威副本；Flash 只存 TPLD 镜像。 |
| `telemetry` | 低 | CPU/内存/栈/队列统计 | 每秒采样一次，生成 GUI 首页数据；不得阻塞总线。 |

不为每个外设建独立任务：任务栈会吃掉有限 SRAM。目标端 I²C 边沿、I3C 事件、SPI NSS、CAN RX 和 USB EP 回调仍走中断，但中断只能把定长事件写入相应环形队列并唤醒任务，不解析 JSON、不写 Flash、不等待 USB。队列满时累加饱和的 overflow 计数，丢弃策略按协议明确。禁止在 ISR 与任务同时修改寄存器仿真区；寄存器区由 `bus_service` 拥有，ISR 只使用快照或经保护的双缓冲。

目前 PB5/PB6 软件 I²C 从机依赖边沿中断和时钟拉伸，16 KiB 页服务的跨页时序仍有偶发失败。迁移前必须把等待电脑页数据的路径从 ISR 中移出，明确最长时钟拉伸与超时行为。三路 UART 目前由 `BridgeSerial_Poll` 连续轮询；若任务开始阻塞/休眠，必须先改为中断或 DMA 接收并确认连续流量无丢字节。TPLD 自动加载应保持在调度器启动前的安全启动阶段，确认启动状态后再枚举 USB；Flash 提交需先应答/背压主机流，再独占镜像槽。中断优先级须重新检查 FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY`，高于该阈值的中断不得调用 `...FromISR` API。

## 标准消息契约

内部消息固定头：`kind u8, interface u8, flags u8, sequence u8, timestamp_us u32, length u16`，后接有界数据。每条消息有所有者；跨任务只传队列中拷贝的短数据或固定池块索引，禁止传栈地址。命令队列至少区分 `CONFIG/BIND/UNBIND/TX/RX/STATUS/IMAGE_PAGE`；优先级不能由上位机请求改变。超时、校验失败、队列满、硬件故障使用独立状态码；消息队列 overflow 可查询且不会导致系统停止。

## 迁移验收顺序

1. 在当前裸机版本补齐统一队列和 overflow 统计；协议 1.8 的确认式停用接口已完成并在 #2 板逐项验证。
2. 把 `Bridge_Poll` 分解为可调用服务；保持现有固件协议与两板压测作为基线。
3. 添加静态 FreeRTOS 任务及单独链接分区，运行时检查每个栈最低余量、ISR 延迟、队列高水位和总 SRAM 上界。
4. 重测 USB 枚举、全部双板总线、256 B EEPROM 压测、TPLD 自动加载和断电重启；大容量 EEPROM 需先解决现有跨页失败。

FreeRTOS 不会使 CDC 更快，亦不能单独修复软件 I²C 的时序问题。若迁移后的 ISR 延迟或 SRAM 余量不达标，继续使用当前主循环固件直到对应模块重构完成。
