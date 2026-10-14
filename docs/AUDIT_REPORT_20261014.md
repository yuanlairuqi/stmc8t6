# 10号蓝板工程检查、修复与证据

日期：2026-10-14。工程目录：`C:\Users\y\Desktop\靶车项目\10_电机控制完整项目_STM32_ESP32_安卓`。最初只读检查，用户随后明确授权“修复已确认的问题后，再验证和发布”。本次不烧录、不驱动电机，未迁移框架或升级依赖。11号野火副本由另一协作聊天独立适配，不纳入本分支。

## 总体结论与实际架构

当前实现是安卓BLE v3 → ESP32-S3 UART v2 → STM32 TIM1 PUL/PB0 DIR → T60 → 57A1ED。CAN、ESP-NOW、T60 RS232运动控制、驱动器报警输入以及编码器到MCU的反馈均没有加入这条链路；目录或SDK组件出现名字不代表已实现。

未发现当前激活的STM时钟/引脚/定时器/串口资源冲突或两端生产协议不一致。已确认的缺陷主要是归并后的工具路径、缓存和Keil构建完整性，及在实际中文目录无法调用ESP/Android原生工具。下面各项已做保持现有框架的最小修复。软件构建与模型检查通过，实机运行仍未验证。

STM连续能力、ESP UART/ADC默认关闭，但STM上电一次22.5°逻辑仍会执行，这是原功能而不是完全无输出的安全模式；没有擅自删除该功能。接线前必须参见[硬件连接方案](HARDWARE_CONNECTIONS.md)。

## 已确认缺陷，按影响排序

行号中的“修复前”指检查时文件，原文备份在`backups/audit-20261014-before-repair`；修复后可在Git差异查看。

### P1：CubeMX脚本仍指向旧工程

- 证据：修复前`stmc8t6/tools/generate_cube.txt:1–4`硬编码`C:\Users\y\Desktop\c8t6\stmc8t6`，与当前10号工程不一致。
- 触发：在当前工程执行旧生成脚本；影响：可能生成/修改旧工程，当前工程配置没有同步，破坏副本隔离。
- 最小修复：原txt改为当前工作目录的相对输入；新增`regenerate_cube.ps1`从脚本位置确定当前`.ioc`，用绝对输入文件加载，不依赖旧路径。检查CubeMX的KO及实际main.c生成目录，避免只依据Java退出0判断成功。
- 验证：真实CubeMX 6.18.1 / FW_F1 1.8.7再生成；12个选定生产文件SHA保持一致，时钟/SWD/初始化检查通过。没有切换工具链。

### P1：归并携带的CMake缓存绑定旧目录

- 证据：`esp32_robot/build/CMakeCache.txt:560`及`stmc8t6/build/MotorDebug/CMakeCache.txt:347`、Release同位置，`CMAKE_HOME_DIRECTORY`仍指向旧`Desktop/c8t6`。
- 触发：按原脚本重用复制来的build；影响：CMake源目录不匹配，不能用旧生成物证明当前源码成功构建。
- 最小修复：两个工程的`tools/prepare_build.ps1`检查缓存归属；仅将当前工程内的异源生成目录移到`backups/relocated-builds`，保留内容后重新构建。拒绝搬动工程外目录/链接目录，不递归删除源文件。
- 验证：实际CMake Debug/Release重新构建；独立烟雾检查确认旧缓存保留、同源缓存不动、工程外搬动被拒绝。

### P1：ESP-IDF和安卓工具不能从现有中文路径构建

- 证据：`esp32_robot/tools/firmware.ps1`、`android/build-apk.ps1`修复前直接给原生工具传入中文路径；本轮实际ESP编译失败于`cannot read spec file ... picolibc.specs: Invalid argument`；aapt2报资源目录`failed to open directory`。同源码ASCII快照基线通过。
- 触发：按文档直接在当前10号中文目录构建；影响：交付脚本不可复现，重新安装框架不能解决源路径编码问题。
- 最小修复：`tools/native_workspace.ps1`为不同源目录创建独立、带来源标识的ASCII临时工作区，同步当前源码，再用既有IDF/CMake或aapt2/javac/D8/zipalign/apksigner构建，成功后回收产物至原工程。menuconfig配置会回写当前工程，签名使用现有测试密钥，不改变包名/业务。没有用旧缓存或另一个工程的固件充当成功结果。
- 验证：原中文工程的修复脚本成功构建ESP32-S3固件、链接ELF并回收BIN；APK成功构建、对齐、v1/v2/v3验签。构建工作区不是新的业务工程；源码快照与当前生产文件一致。

### P1：Keil项目不完整且ARMCC5不支持原断言

- 证据：修复前`stmc8t6/MDK-ARM/stmc8t6.uvprojx:375–475`缺motor_motion/robot_uart/robot_peer/robot_wire及HAL_TIM/TIM_EX/UART源文件；`OnChipMemories`选中的Flash/RAM范围全零。`Core/Src/motor_motion.c:11–27`的`_Static_assert`在现有ARMCC5.06实际编译20错误。
- 触发：使用保留的Keil工程；影响：缺符号链接失败、目标存储边界不完整，以及断言无法编译。CMake通过不能证明此工程通过。
- 最小修复：补7个源条目及IROM 0x08000000/64KB、IRAM 0x20000000/20KB；新增`motor_assert.h`，ARMCC5用编译期负数组断言，其余工具链保留C11断言。只替换断言宏，不改脉冲/方向算法，不升级编译器。
- 验证：现有Keil5.24/ARMCC5.06完整编译链接及HEX生成，0错误/0警告；原中文目录同样通过。CMake与38组回归通过。

### P3：启动日志显示旧BLE版本

- 证据：修复前`esp32_robot/main/main.c:185`打印BLE v2，而生产命令和状态为BLE v3。
- 触发：查看控制台；影响：容易把版本与UART v2混淆，但不影响线上字节。
- 最小修复：日志明确BLE v3 / UART v2；协议定义没有变化。

## 待核实风险，未当作已确认软件缺陷

| 风险/证据 | 触发与影响 | 最小处理建议 |
|---|---|---|
| T60照片DC18–68V，V2.0手册第5/9/10页DC24–50V | 套错版本选择电源或输入电路可能损坏硬件 | 匹配实物型号/版本；24V交集仅作候选，最终供电与电流核实 |
| 参考手册第9–12/20页3.3–24V兼容及7–16mA故障建议，但无本机实测 | GPIO直连可能电流不足或电平不合格 | 使用已验证接口电路并测差分电压/光耦电流，不能只靠“3.3V兼容” |
| `motor_config.h:7–10,27–30` N=1600、100–1000Hz、2/10ms均示例 | 拨码/模式不匹配造成角度速度错误；换向/滤波影响脉冲 | 核对N、SW7、DIR/ENA逻辑与时序；不以1.8°或编码器4000替代N |
| `main.c:105–106`及`motor_motion.c`保留自动一次动作 | MCU复位或ESP尚未STOP时即发脉冲，关闭连续能力并不关闭它 | 接驱动前先不接PUL测量；如需取消自动动作，应明确产品策略后再改 |
| 没有机械加减速斜坡；往复从逻辑方向请求计时，含换向等待 | 重载/高惯量换向可能失步、报警、机械冲击 | 空载验证后评估斜坡与急停，不能当作本轮已测缺陷 |
| APPLIED仅检查TIM CEN/CC1E/MOE，ALM/编码器未接入MCU | 显示输出已应用不等于电机实际转动或T60无报警 | 保持文档/界面区别；如需真运行反馈另接隔离报警/位置接口 |
| 蓝色最小板具体原理图、HSE实物频率和排针负载缺失 | 错晶振或板载负载会改变时钟/串口/脉冲表现 | 对照实板8MHz与排针；不把野火RGB/USB接线套用蓝板 |
| Stamp N8R8实际8MB/8MB，固件只用4MB/no PSRAM | 不利用全部资源，当前程序容量够用；本身不是故障 | 可选后续板级配置8MB，勿无依据启用PSRAM或占用G47/G48 |
| 当前CAN没有初始化/ID/波特率/异常处理 | 若把SN65HVD230当作已工作的控制链路，将无法通信 | 当前保持UART；若需要CAN另定义双端控制器、帧和故障策略，不直接塞32字节到Classic CAN单帧 |

## STM资源核对

- `.ioc`与main/MSP/IRQ一致：HSE8MHz×9、SYSCLK/HCLK72MHz、APB1 36MHz、APB2 72MHz，Flash 2 wait states；实际晶振未测。
- TIM1在APB2、分频1，输入72MHz；APB1分频2时APB1定时器仍72MHz，不能用USART2的PCLK1 36MHz替代定时器时钟。
- TIM1 CH1 PA8 / PWM2：PSC71、ARR9999、CCR5000 → 100Hz，高/低5ms；GPIO中速10MHz是翻转驱动速率等级，不是PWM频率。
- DIR PB0推挽低速、初始低；SWD PA13/PA14；HSE PD0/PD1保留。USART1 PA9/PA10、USART2 PA2/PA3、USART3 PB10/PB11均115200/8N1。
- 唯一串口业务USART2，中断优先级5，IRQ调用HAL_UART_IRQHandler，强RX/Error回调实际链接；SysTick仍更新HAL毫秒时间。TIM1用RCR/OPM硬件停批次，不需要定时器ISR计数。
- PA8占用TIM1_CH1但USART1异步不使用PA8 CK；PA9/10的TIM1 CH2/3未用。PB10/11与I2C2/TIM2重映射冲突功能均未启用。USB/CAN/STM ADC未启用，不以其目录/宏模板判定功能存在。
- 基础GPIO初始状态和自定义入口位于USER CODE或独立模块，实测再生成保留。没有配置ENA/ALM，也没有假定它们有效极性。

## 一条命令的完整路径

以正转50%、往复正300ms/反200ms为协议示例，当前默认硬件开关关闭时会明确拒绝或仅演示，不会声称已转动。

1. Android `Protocol.command()`生成16字节：版本3/op1/speed50/mode1/phoneSeq/300/200，uint32小端；STOP固定op0、speed0、mode0、时长0。
2. `CommandChannel`等待首次STOP同步，GATT串行写；运动心跳每200ms，写入/确认超时锁定并停止，后台或断连清空运行。
3. ESP `robot_command()`核版本、长度、参数、序号和STM就绪，ACK只代表ESP接受。`robot_bridge`加随机非零session、token和500ms租约，CRC覆盖前30字节，形成32字节UART v2。
4. STM USART2 IRQ只入环形缓冲，主循环`wire_feed → peer_receive`解析。新session先STOP；重复token不续租；同参数新token只续租；反向阶段调速保留当前方向/阶段。
5. 默认`MOTOR_CONTINUOUS_ENABLED=0`返回NOT_READY；启用且参数核实后，`Motor_SetContinuous`在hold/setup等待后启用TIM1，频率由百分比映射。`peer_tick`执行往复与租约。
6. STM返回32字节STATUS，回显session/token/phoneSeq、逻辑方向/阶段/速度、APPLIED、error；ESP匹配确认后生成20字节BLE状态。Android分别显示ESP ACK和STM ACK，并显示输出未确认/已应用。
7. CRC坏帧丢弃重同步；STM RX溢出/错误停输出并重开接收。ESP运动租约800ms、STM500ms、双端UART在线600ms；PING及相同token重试不续运动。模型覆盖STOP优先、丢ACK、重启及序号回绕。

## 构建与验证证据

| 层次 | 本轮结果 | 证据边界 |
|---|---|---|
| 静态/配置 | 通过，未发现激活资源冲突，4个共享文件完全一致 | 不能代替晶振/接线测量 |
| 生产C/Java主机模型 | 38组通过，修改前后均通过 | 模型与字节往返，不是实物UART/BLE |
| CubeMX再生成 | 正确当前目录，保留12个选定生产文件 | 不烧录；脚本检查KO与目标目录 |
| CMake ARM Debug | FLASH19396B，RAM2392B，编译链接通过 | RAM为链接分配，不是运行峰值 |
| CMake ARM Release | FLASH9992B，RAM2376B，编译链接通过 | 当前连续开关仍0 |
| Keil ARMCC5 | Code10104、RO344、RW76、ZI2372；0错0警告 | 既有Keil框架，未升级 |
| ESP-IDF6.1 | ESP32-S3 ELF/BIN通过，app456736B/1MiB分区 | 默认UART/ADC关闭；ASCII原码快照构建并回收 |
| Android SDK35/JDK17 | APK构建、对齐、v1/v2/v3验签通过 | 测试签名；没有安卓真机安装或屏幕验证 |
| 真实通信/电机 | 未进行 | 无UART波形、BLE实机、输入电流、转角/RPM/负载证据 |

构建日志位于本报告同级的`verification/20261014`；因原生SDK日志有中文/弃用提示，不能把“没有C编译错误”扩写为所有工具均无任何提示。

## 可选改进

- 硬件确认后引入ALM输入与实际位置/速度反馈，独立于“脉冲输出已启动”。
- 明确产品是否需要保留复位自动动作；若改成待机，应增加显式模式开关和测试，而不是悄悄删除原要求。
- 板载M5PM1电池/电源管理、Flash8MB/PSRAM或CAN可作为后续需求，不属当前已实现功能，也不作为本轮强制升级。
- 带载场景增加机械允许的加减速和硬件急停。不能仅靠软件租约或ENA释放当作所有机械风险的解决方案。
