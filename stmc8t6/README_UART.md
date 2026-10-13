# ESP32 与 STM32 串口协作说明

本工程已接入 ESP32 聊天“开发 ESP32 手机控制软件”的 v2 协议；ESP32/APK 由该聊天负责，实际 STM32 工程由本聊天负责。此次没有烧录，没有串口实物收发或电机运行证据。

## 已完成的能力与当前边界

- USART2 双向收发、CRC 校验、帧同步、会话握手、序号防重放、停止确认、周期状态与断链处理已接入真实 STM32 工程。
- 三个串口仍统一为 115200 bit/s、8 数据位、无校验、1 停止位、无流控。只有 USART2 承担 ESP32 通信；USART1 和 USART3 保持初始化与预留。
- 原有“每次复位后等待 1 秒，输出一次 22.5° 指令脉冲”保留。未连接 ESP32 时沿用原行为。**ESP32 启用 UART 后会发送启动 STOP；它优先取消等待或正在执行的上电动作。上电定量动作不会在主循环里重启，下一次复位才能重新执行。启用连续模式后，新的有效运动命令可启动连续输出。**
- 按协作聊天中最新需求，协议现在控制单电机：正转、反转、0–100%速度、持续运行、按设置的两段时长往复。已删除小车左右转圈含义。没有新增“开始22.5°”串口命令。
- 连续模式代码已实现，但 `motor_config.h` 的 `MOTOR_CONTINUOUS_ENABLED=0`，因此当前默认 READY=0，运动请求返回 NOT_READY；等待硬件确认后才设置为1。DEMO始终为0。STOP 成功且 APPLIED=1，需读取定时器停止的软件状态确认；运动 APPLIED=1 需连续脉冲计数已启动。两者均不表示测得了转角、机械运动或驱动器电流。
- 电池电压没有采样，返回 `0xFFFF`（未知）；没有编造电压、编码器位置、驱动器反馈或串口电机协议。

## 引脚与后续接线

| 功能 | STM32 引脚 | 另一端 / 用途 |
|---|---|---|
| ESP32 通信 TX | PA2 / USART2_TX | 接 ESP32 的 UART RX，GPIO 尚待按实际板确认 |
| ESP32 通信 RX | PA3 / USART2_RX | 接 ESP32 的 UART TX，GPIO 尚待按实际板确认 |
| 信号参考 | GND | 普通非隔离 3.3V TTL 串口时接 ESP32 GND |
| 电脑串口预留 | PA9 TX / PA10 RX，USART1 | 如使用 USB 转串口，需核对为 3.3V TTL 接口 |
| 第三串口预留 | PB10 TX / PB11 RX，USART3 | 仅初始化，无预定协议 |
| PUL / DIR | PA8 TIM1_CH1 / PB0 GPIO | 原有电机 MCU 侧输出；T60 接口电路仍待确认 |
| SWD | PA13 / PA14 | 保留调试接口 |

PA2/PA3 默认复用分别为 USART2_TX/RX，参见 [ST STM32F103x8/xB 数据手册](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf)。仍按外部 HSE 8 MHz、PLL ×9、HCLK 72 MHz、APB1 36 MHz、APB2 72 MHz运行；实际晶振需对照硬件。

接线仅用于**确认双方都是 3.3V TTL UART**后的短距离方案：TX 交叉接 RX；非隔离方案需公共信号地。供电由各板合适的电源输入提供，避免未核实便把两板 3.3V/5V 电源输出互接。RS-232/RS-485 接口必须使用相应收发器；隔离 UART 的地与供电按隔离器方案处理。这条串口连接不涉及 T60 动力电源或 PUL/DIR/ENA 端子。

ESP32 侧 UART 默认关闭、TX/RX GPIO 默认 -1；这是待硬件确认的占位值。后续要选择实际可用 GPIO、避开 USB、启动脚、Flash/PSRAM 与板载外设，再启用 UART。不能把 STM32 PA2 的编号当成 ESP32 GPIO2。

## CubeMX 与代码入口

1. USART2 → Asynchronous，115200 / 8N1 / TX_RX / 无流控，PA2 与 PA3，不重映射。
2. USART2 → NVIC Settings → USART2 global interrupt，抢占优先级 5、子优先级 0。USART1/3 没有新增通信中断。
3. SysTick 保持 HAL 毫秒时基；TIM1、DIR、SWD 和时钟保留原配置。
4. `main.c` USER CODE 2 先 `RobotUART_Init(&huart2)`，再 `Motor_StartOnce(&htim1)`；主循环先 `RobotUART_Poll()` 后 `Motor_Process()`，使已接收到的 STOP 优先于启动下一批脉冲。
5. USART2 IRQ 由 CubeMX 生成并调用 `HAL_UART_IRQHandler(&huart2)`。RX 完成回调仅入队并重开 1 字节接收，不在中断内解析协议或操作电机。
6. 自定义代码在 `motor_motion.c`、`robot_uart.c`、`robot_peer.c` 和 `robot_wire.c`；根 CMakeLists 已加入源文件。生成代码中的入口在 USER CODE 区域，CubeMX Keep User Code 保持开启。

## 固定帧协议

采用 ESP32 和 STM32 共用的 `robot_wire.h/.c`，**UART版本2，固定32字节**；所有多字节整数均为小端。旧版本1的24字节帧不再接纳，ESP固件和APK必须配套更新。

| 字节 | 含义 |
|---|---|
| 0–1 | `A5 5A` 帧头 |
| 2 | UART 协议版本 2 |
| 3 | 1=COMMAND，2=STATUS，3=PING |
| 4–7 | ESP32 会话号 session |
| 8–11 | 串口命令序号 token |
| 12–29 | 18 字节载荷 |
| 30–31 | 前30字节的 CRC16-CCITT-FALSE，poly=0x1021，init=0xFFFF，小端传输 |

COMMAND载荷：0=op，1=speed，2–3=lease_ms(uint16)，4–7=phoneSeq(uint32)，8=mode，9=保留零，10–13=正转ms(uint32)，14–17=反转ms(uint32)。op=0停止、1正转、2反转；speed=0–100，STOP只能为0、非STOP不能为0，APK的0%会转换成STOP。lease必须为100–1000ms，ESP32使用500ms。mode=0持续（忽略段时长），mode=1往复（各段100–3600000ms）。保留字段非零、范围错误或session=0是坏命令。

STATUS载荷0–9：motion / speed / flags / error / battery_mv(uint16) / phoneSeq(uint32)；10=mode，11=当前方向阶段（0停止、1正、2反），12–17保留零。session/token/phoneSeq回显已接纳命令标识，不把手机BLE写入完成当成电机执行确认。

flags：bit0 READY，bit1 DEMO，bit2 电压有效，bit3 APPLIED，bit4 LOCAL_BUSY（本地上电动作仍在等待/输出）。UART motion仅0–2；ESP32将LOCAL_BUSY转成手机状态motion=5，显示“局部动作”。UART版本2与BLE版本3是两个独立协议层。

error：0=OK，1=BAD_COMMAND，2=TIMEOUT，3=NOT_READY，4=DRIVER_ERROR。READY=0、error=0 的 STOP 是正常停止成功；READY=0 不等于串口离线。

## 重复包、停止与超时

- 新 session 先 STOP 握手；首个运动包不建立会话。token 使用 uint32 环绕比较；重复或旧运动命令不会重复应用，也不会续租。
- STOP 调用真正的 `Motor_Stop()`，取消等待或正在输出的动作；不通过 `apply(0,0)` 假装支持连续运动。
- 正常定量完成沿用 TIM1 RCR + OPM，按硬件周期数停止。主动 STOP 将 OC1 强制为无效电平；可能截短最后一个高脉冲。`completed_pulses` 只记录已确认整批，若打断未完成批次则 `pulse_count_exact=0`，不能用这个计数声称精确停止角度。
- 共享peer负责持续/往复调度；仅新token运动命令续租，同参数心跳不重置往复阶段。PING只查询状态，不续运动租约；不依赖手机不停重新下发换向指令。
- 已建立通信后，600 ms 未处理到本 session 的有效命令/心跳，就停止并回 TIMEOUT。尚未建立通信时不取消原上电动作。
- RX 环形缓冲溢出、UART 错误或接收重开失败：停止、丢弃缓存及半帧、清除 F1 ORE 标志并重开接收。无效 CRC 帧丢弃并重同步，不当作有效心跳。
- STATUS每100ms更新；TX采用中断，最多一帧在发送、一帧最新状态待发，不无限积压。发送100ms仍未完成则中止并报告错误。32字节在115200/8N1上的线时约2.78ms。
- 所有超时需主循环获得执行时间；不要在主循环加入长阻塞延时。本次没有增加独立硬件看门狗。

## 连续模式硬件参数接口

`Core/Inc/motor_config.h` 集中定义：

| 参数 | 默认值 | 性质 |
|---|---:|---|
| MOTOR_CONTINUOUS_ENABLED | 0 | 默认不允许串口连续运动；确认接口后才启用 |
| MOTOR_CONTINUOUS_MIN_HZ | 100 | 1%对应频率，软件示例待确认 |
| MOTOR_CONTINUOUS_MAX_HZ | 1000 | 100%对应频率，软件示例待确认 |
| MOTOR_DIRECTION_HOLD_MS | 2 | 停脉冲到改变DIR的等待，软件建议待核实 |
| MOTOR_DIRECTION_SETUP_MS | 10 | 改变DIR到恢复脉冲的等待，软件建议待核实 |

1–100%线性映射100–1000Hz，按1MHz计数时钟取最近整数周期，PWM2高低各约半周期；0%=STOP。示例边界是100Hz时高/低各5ms，1000Hz时各0.5ms。这是脉冲频率比例，不是电机最大额定转速或实时转速。

转速换算仍为 `指令rpm=脉冲Hz×60/每圈指令脉冲数N`。若假设N=1600，则100–1000Hz对应3.75–37.5指令rpm；N仍待拨码确认。未用编码器测速，不返回实测rpm。

换向先强制PUL无效，等待hold，再改变DIR，再等待setup，最后取消OPM、RCR=0并启用连续PWM。同方向频率变化由Motor接口用ARR/CCR预装载更新。往复阶段时长按peer方向请求的逻辑时间计算，**包含换向等待**；当前示例每次换向约12ms再恢复脉冲，不能将设置的段时长当成等长的实测脉冲列。没有实现机械减速到零/加速斜坡；实机带载连续往复前还需确认机械允许条件。

启用连续模式只改变软件能力开关，不会确认供电、接线、DIR物理方向或频率上限。当前项目与交付ELF保持开关为0；主机测试单独编译enabled=1分支。

## 验证与复现

```powershell
cd C:\Users\y\Desktop\c8t6\stmc8t6
.\tools\verify_project.ps1
.\tools\test_motor.ps1
.\tools\test_uart.ps1
.\tools\build_motor.ps1 -Configuration Debug
.\tools\build_motor.ps1 -Configuration Release
```

`test_uart.ps1` 用 HAL/定时器模型执行真实生产模块，还将 ESP32 `robot_bridge` 源码快照接入双端往返测试；参考文件位于 `verification/uart/esp_reference`，只用于桌面测试，不链接进 MCU。详细结果见 `verification/UART_REPORT.md`。

实物测试顺序：先核对两板供电及3.3V TTL引脚；仅连接两板串口，不接电机，验证115200/8N1、32字节帧、CRC和握手；再测试拔线超过600ms、ESP32/STM32分别复位、STOP重发与默认拒绝运动。确认T60型号版本、每圈脉冲数、接口电压电流、电路、DIR时序、频率范围及供电后，才启用连续模式，测PA8波形并进行低速空载电机测试。

STM32修改前备份：`backups/before_uart_20261013`。旧 `THREE_ROUND_REPORT.md` 是本次串口修改之前的记录，不能当作当前工程的构建结果。
