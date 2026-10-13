# STM32F103C8T6：上电执行一次 22.5° 指令运动

工程：`C:\Users\y\Desktop\c8t6\stmc8t6`。本次修改只覆盖 MCU 侧配置、代码与 CMake 构建；没有烧录，也没有连接或操作电机。

## 引脚分配与默认值

| 功能 | 引脚 | 配置 | 用途与复用边界 |
|---|---|---|---|
| PUL | PA8 | TIM1_CH1，PWM2，高有效，10 MHz GPIO 速度 | 无重映射；占用 USART1_CK，USART1 异步模式不需要该时钟脚 |
| DIR | PB0 | 推挽 GPIO，低速，初始低 | 固定输出低；实际顺/逆时针由接线与驱动器设置决定；占用 ADC_IN8/TIM3_CH3 |
| USART1 TX/RX | PA9/PA10 | 异步 TX/RX | 分配给电脑；与 TIM1_CH2/CH3 冲突，但本工程只用 CH1 |
| USART2 TX/RX | PA2/PA3 | 异步 TX/RX | 分配给 ESP32；占用 TIM2_CH3/CH4 |
| USART3 TX/RX | PB10/PB11 | 异步 TX/RX | 只初始化并预留，不含电机协议；占用 TIM2 重映射 CH3/CH4、I2C2 |
| SWD | PA13/PA14 | Serial Wire | 保留 ST-Link 调试 |
| HSE | PD0/PD1 | 外部无源晶振 | 软件按 8 MHz 配置，实物频率待确认 |
| ENA 候选 | PB1 | 当前未配置、未连接 | 后续确认输入极性与接口后再启用 |

三个串口统一为 **115200 bit/s、8 数据位、无校验、1 停止位、无硬件流控、16 倍过采样**。后续 ESP32 协作已为 USART2 加入停止命令与状态收发，USART1/3 仍只初始化预留；没有通过串口调试 T60。最新说明和验证以 [README_UART.md](README_UART.md) 为准。

可扩展引脚：PB1（ENA 候选）、PB6/PB7（I2C1 候选）、PB12（报警候选）、PB13/PB14（限位候选）、PA4（模拟调速候选），当前保持未分配。使用前仍需核对板载连接与排针引出；PB12/13/14 会占用相应 SPI2 功能。

没有假定 ENA 一定需要由 MCU 控制。参考 T60 手册默认 ENA 输入光耦断电时驱动电机、通电时释放，且极性可修改。因此本次不配置不确定的使能逻辑；未来接线前必须确认驱动器能响应 PUL、停止脉冲后是否需要保持锁轴。**停发脉冲不会自动切断电机电流。**

## 参数与执行行为

入口位于 `Core/Src/main.c` 的 USER CODE 2，先初始化 ESP32 USART2 通信，再调用 `Motor_StartOnce(&htim1)`；主循环 USER CODE 3 先处理通信，再调用 `Motor_Process()`。有效 STOP 会取消上电动作且不自动重启；ESP32 启用串口后的启动 STOP 握手也有这一优先权。

运动参数集中在 `Core/Inc/motor_config.h`：

| 参数 | 默认值 | 说明 |
|---|---:|---|
| MOTOR_DRIVER_PULSES_PER_REV | 1600 | **待确认的示例值**，必须匹配 T60 SW1–SW4 / 软件设置 |
| MOTOR_TARGET_ANGLE_MDEG | 22500 | 22.5°；单位为千分之一度 |
| MOTOR_DIRECTION_LEVEL | 0 | PB0 固定低，物理转向未确认 |
| MOTOR_STARTUP_WAIT_MS | 1000 | 建议启动等待；不是厂家规定的上电/方向时序 |
| MOTOR_TIMER_PRESCALER | 71 | 与 .ioc 一致，TIM1 计数时钟为 1 MHz |
| MOTOR_TIMER_PERIOD | 9999 | 与 .ioc 一致，100 Hz |
| MOTOR_TIMER_COMPARE | 5000 | 与 .ioc 一致，PWM2 低 5 ms、高 5 ms |

保留原始 HSE/PLL 时钟函数：8 MHz ÷1 ×9，HCLK 72 MHz、APB1 36 MHz、APB2 72 MHz。TIM1 在 APB2，分频为 1，因此输入时钟为 72 MHz。

频率：`72 MHz / (PSC+1) / (ARR+1) = 100 Hz`。

对于向上计数 PWM2，高电平时间为 `(ARR+1-CCR1)/1 MHz = 5 ms`，低电平时间为 `CCR1/1 MHz = 5 ms`。这是当前工程的慢速测试波形，不是对 T60 最小脉宽的确认。若以后改速度，请同步更新 .ioc 与 motor_config.h 的 PSC/ARR/CCR 参数，并检查输入脉宽、电路带宽与机械负载。

目标脉冲计算使用 64 位整数中间值，最近整数取整，恰好半脉冲时向上取整：

```text
理想脉冲数 = N × 22.5 / 360 = N / 16
目标脉冲数 = round(N / 16)
实际指令角度 = 目标脉冲数 × 360 / N
指令量化误差 = 实际指令角度 − 22.5°
```

示例 N=1600：100 脉冲，指令角度 22.5°，量化误差 0；100 Hz 对应 3.75 rpm，脉冲列约 1 秒。

示例 N=200：12.5 脉冲取整为 13，指令角度 23.4°，误差 +0.9°。这里 200 只用于取整示例，不能因为电机步距角为 1.8°就把驱动器设定认定为 200 脉冲/圈。一般最近整数取整的误差上限为半个指令脉冲，即 `180/N` 度。计算为 0 脉冲时进入 MOTOR_FAULT，不启动定时器。

## 按数量停止的机制

采用 TIM1 的 **8 位重复计数器 RCR + 单次模式 OPM + PWM2**：

1. 初始化时不计数，CNT=0，PWM2 输出保持低；先固定 DIR，再等待约 1 秒。
2. 对一批 K 个脉冲设置 `RCR=K-1`，先用 UG 将参数装入硬件，再清除状态标志，最后启动 CEN。
3. 每个计数周期在 CCR1 处产生上升沿，在溢出处产生下降沿。
4. 第 K 次溢出形成更新事件；OPM 硬件清除 CEN，CNT 回到 0，PA8 回到低，不依赖中断响应速度，也不通过 HAL_Delay 估算角度。
5. 主循环观察硬件停止与 UIF 标志，将状态设为完成。保持通道使能，让 PUL 持续为低；不会再次请求运动。

RCR 一批最多 256 个脉冲。目标超过 256 时分成若干完整批次，批次之间可有额外的低电平间隙，脉冲总数不受主循环延迟影响；这不是连续高速轨迹。当前 100 个脉冲只需一批。等待时间只用于启动和方向稳定，不参与目标脉冲数计算。

可以用 SWD 观察 `g_motor_status`：WAITING → RUNNING → DONE。`completed_pulses` 是已完成的定时器周期数量，**不是编码器测量或真实角度反馈**。同一次复位周期重复调用 Motor_StartOnce 也不会重启。

## CubeMX 再生成与构建

本次用安装的 STM32CubeMX 6.18.1 和 STM32Cube FW_F1 V1.8.7 实际生成代码。`ProjectManager.KeepUserCode=true`；运动模块是独立文件，两个调用和 include 位于 USER CODE 区域；根 CMakeLists.txt 添加 motor_motion.c（该文件的头部说明根文件不会反复生成）。

.ioc 描述基础 PWM2/PSC/ARR/CCR 和串口初始化；动态的 OPM、RCR 每批脉冲数由 motor_motion.c 设置，不把示例运动脉冲数写死进 CubeMX 的初始化 RCR。不要手工调用 HAL_TIM_PWM_Start 来重复启动这个专用定时器，也不要把 TIM1 分配给其他任务。

构建与检查命令（在本工程目录执行）：

```powershell
.\tools\verify_project.ps1
.\tools\test_motor.ps1
.\tools\build_motor.ps1
```

build_motor.ps1 定位现有 STM32 bundles 内的 CMake、Ninja、ARM GCC，创建 `build/MotorDebug` 并干净编译链接；不调用烧录工具。原始文件快照保存在 `backups/before_motor_20261013`。tools/configure_motor.ps1 是本次初始迁移脚本，有防覆盖检查，不要重复执行以替代日常 CubeMX 操作。旧 MDK-ARM 目录不属于本次 CMake 构建交付，不能用其旧文件证明新代码可用。

验证证据见 tools/cubemx_generate.log、tools/regeneration_verify.log、tools/verify_project.log、tools/host_tests.log、tools/build_debug.log。

主机测试直接编译运动模块，使用独立的定时器寄存器模型，覆盖取整、0/1/100/256/257/2500 脉冲、批次边界、SysTick 回绕、处理延迟、禁止重启、错误模式和异常停止。模型检查不替代 STM32 实物脉冲测量。

## 接线和测试前必须确认

1. 实物晶振确为 8 MHz；PA8、PB0 和串口引脚确实引出且无板载冲突。
2. T60 具体硬件版本、动力供电、接口电路的电压/电流与隔离方式、有效脉冲边沿及反相关系、方向建立/保持时间、使能逻辑和启动准备时间。当前 1 秒等待与 5 ms 脉宽只是候选参数，不能替代厂家时序要求。
3. 驱动器每圈指令脉冲数确实与参数 N 一致，使用 PUL+DIR 控制模式，闭环及编码器/绕组连接正确；固定方向的物理意义通过低速空载确认。
4. 先在 MCU 引脚测量：每次复位等待约 1 秒后，仅有 100 个 100 Hz 脉冲、高/低各约 5 ms，最后保持低，长期等待不重复。再检查接口输出，最后进行机械固定后的空载转角测量。

真实动作还受驱动器是否准备就绪、报警、编码器、负载、传动间隙等影响。自动动作每次 MCU 复位都会重新执行，后续连接硬件时应评估复位带来的动作。

资料依据：
- [ST RM0008：定时器重复计数器、单次模式与 PWM2](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [ST STM32F103x8/xB 数据手册：引脚复用与器件限制](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf)
- [锐特 T60 V2.0 产品页](https://www.rtelligent.com/t60-v2-0/)
- [锐特 T60 参考手册（存在版本差异）](https://www.rtelligent.com/uploads/锐特技术T60-2.0闭环步进驱动器用户手册.pdf)

## 上电单次工程历史验证（串口协作修改前）

以下构建体积及测试记录属于串口接入之前；当前工程包含通信与默认关闭的连续模式，请参阅 [当前串口验证报告](verification/UART_REPORT.md)。

2026-10-13 已完成：

- CubeMX 实际生成并再生成成功，保留自定义模块、USER CODE 调用与根 CMake 集成；时钟函数及 SWD 配置与原始备份一致。
- 配置检查通过：三个串口的引脚及 115200/8N1、TIM1 PWM2/PSC/ARR/CCR、单次启动调用与 CMake 模块。
- 9 个主机测试场景通过，包括 256/257 脉冲分批边界、2500 脉冲长批次、SysTick 回绕、低电平停止、禁止重启及故障分支；测试编译启用 -Wall -Wextra -Werror。
- 最终源文件以现有 ARM GCC 14.3.1、CMake 4.4.0、Ninja 1.13.2 完成 Debug 干净编译和链接；构建日志没有编译警告或错误。
- 最终 ELF：`build/MotorDebug/stmc8t6.elf`，链接脚本限制 Flash 64 KB、RAM 20 KB；FLASH 使用 10628 B（16.22%），RAM 1896 B（9.26%，含保留的堆栈空间）。

本项目尚无烧录、实际信号测量、串口通信或电机真实转角证据。22.5° 是与已确认 N 匹配后的目标指令角度，不是实测结论。

用户要求的追加三轮复核已完成，详见 [三轮复核报告](verification/THREE_ROUND_REPORT.md)：配置/初始化复核通过；主机测试扩展至 15 个场景并全部通过；CubeMX 再生成后 16 个生产文件完全一致；Debug/Release 均完成干净编译链接且无未解析符号。生产运动代码保持不变。
