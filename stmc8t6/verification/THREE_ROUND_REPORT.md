# 三轮复核报告

日期：2026-10-13。范围：现有 STM32F103C8T6/CMake 工程的配置、MCU 运动代码、软件模型测试、CubeMX 再生成及编译链接。没有烧录或操作硬件。

**结论：三轮均通过，未发现新的引脚冲突、初始化不一致或单次定量运动逻辑问题。本次增强了检查脚本和测试，没有改变生产运动代码。**

## 第一轮：配置与初始化

- 原 HSE/PLL、总线频率及 SWD 配置和原始备份一致，生成的 SystemClock_Config 函数完全一致。
- PA8/TIM1_CH1、PB0/DIR；USART1 PA9/PA10、USART2 PA2/PA3、USART3 PB10/PB11，无实际使用的引脚冲突。
- 检查 .ioc 引脚、MSP 端口/时钟/引脚模式、main.h 引脚别名、HAL 模块、HSE_VALUE=8 MHz、SysTick 计时入口。
- 三个串口为 115200/8N1/TX_RX/无流控；没有串口运动命令或驱动器调试通信。
- TIM1 为 PWM2、高有效、PSC=71、ARR=9999、CCR1=5000；自定义模块参数与 .ioc 一致。
- 启动函数只调用一次，主循环只处理状态；没有额外 HAL_TIM_PWM_Start 绕过定量控制。

证据：[round1_config.log](three_rounds/round1_config.log)。

## 第二轮：脉冲与状态逻辑

重新阅读运动模块并核对 ST RM0008 的 PWM2、预装载、UG、重复计数器和 OPM 行为。UG 在启动计数前装入参数，清除生成的更新标志；RCR=K−1，每批最多 256 个周期；完成后 CNT=0，PWM2 输出无效电平，不依赖主循环及时停止脉冲。

直接用本机 GCC 编译当前运动模块，开启 -Wall -Wextra -Werror，运行 15 个场景：

| 场景 | 结果 |
|---|---|
| 每圈 7 脉冲，角度取整为 0 | 拒绝启动 |
| 每圈 16 脉冲，目标 1 | 通过 |
| 每圈 200 脉冲，目标 13 | 通过，取整结果符合 23.4° 指令角度 |
| 每圈 1600 脉冲，目标 100 | 通过 |
| 目标 255、256、257 | 全部通过，批次分别为 1、1、2 |
| 目标 511、512 | 全部通过，均为 2 批 |
| 每圈 40000 脉冲，目标 2500 | 通过，10 批 |
| 错误 PWM 模式 | 拒绝启动 |
| 高电平期间发生异常停止 | 进入故障，强制计数器为 0，输出无效 |
| NULL 定时器句柄 | 拒绝启动，故障后不能再请求运动 |
| 非 TIM1 句柄 | 拒绝启动 |
| PWM 极性反相 | 拒绝启动 |

正常场景还同时核查：启动等待的 SysTick 回绕；等待期间重复调用不能重置期限；运行期间不能重复启动或提前计数完成；每周期一个上升沿；每个脉冲高/低各 5000 个计数；主循环延迟 1 秒时硬件已停止；最终不重启且输出保持无效电平。

证据：[round2_tests.log](three_rounds/round2_tests.log)。这是软件模型验证，不是 GPIO 波形实测。

## 第三轮：再生成与最终构建

1. 用 ARM GCC 对真实 ST CMSIS/HAL 头文件执行静态断言：核对测试模型使用的寄存器位、TIM1 RCR 地址偏移及 8 位范围、引脚号和 100 Hz 公式。
2. 实际运行本机 CubeMX 再生成，重新检查配置；包含 .ioc、两个 CMake 文件和 Core 代码/头文件的共 16 个生产文件，与生成前 SHA-256 校验值全部一致。
3. 用现有 CMake/Ninja/ARM GCC 对当前源文件分别执行 Debug 和 Release 干净编译链接。
4. 检查两个 ELF：没有未解析符号，Motor_StartOnce、Motor_Process、g_motor_status、htim1 及三个串口句柄均存在。
5. 检查构建日志：没有编译或 CMake 警告/错误。构建后生产文件仍与再生成前校验值一致。

| 构建 | Flash | RAM | 文件 |
|---|---:|---:|---|
| Debug | 10628 B / 64 KB | 1896 B / 20 KB | build/MotorDebug/stmc8t6.elf |
| Release | 5516 B / 64 KB | 1896 B / 20 KB | build/MotorRelease/stmc8t6.elf |

ELF SHA-256：

```text
Debug   746BC1B57FC0988CB64CEA2AC79277E1E998E88E7EF877457CB2E5BF90DFF670
Release 0807B0F162D7E5A2459E6DFD782C9FE7764115EB45B866ACA5852A0A4B4B0306
```

证据：[真实寄存器定义](three_rounds/register_constants.log)、[CubeMX 日志](three_rounds/round3_cubemx.log)、[再生成一致性](three_rounds/round3_regeneration.log)、[Debug 构建](three_rounds/round3_debug_build.log)、[Release 构建](three_rounds/round3_release_build.log)、[ELF 检查](three_rounds/round3_elf_check.log)、[最终诊断](three_rounds/diagnostics.log)。

## 仍需硬件确认的条件

- 实物晶振为 8 MHz，所选引脚已引出且无板载占用。
- MOTOR_DRIVER_PULSES_PER_REV=1600 仍是待确认示例；100 个脉冲对应 22.5° 的前提是驱动器确实为 1600 指令脉冲/圈。
- 驱动器具体版本、接口电平/电流/隔离、脉冲有效边沿、方向时序、使能逻辑、供电和启动就绪时间。
- 后续先测每次复位的 GPIO 脉冲列，再测接口输出，最后做电机空载转角测试；本次没有真实脉冲、串口或电机运动证据。

资料：[ST RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)。
