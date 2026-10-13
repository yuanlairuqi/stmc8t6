# STM32 / ESP32 UART v2 协作验证

日期：2026-10-13。工程：`C:\Users\y\Desktop\c8t6\stmc8t6`。

协作对象：聊天“开发 ESP32 手机控制软件”（01a134b4-a0aa-74f1-a25e-0dbe3eba5541）。ESP32/APK由该聊天维护，STM32实际工程由本聊天修改。双方共用UART v2固定32字节协议；电脑USART1与预留USART3未添加通信任务。此次未烧录、未操作硬件。

## 第1轮：配置和协议一致性

通过，日志：[configuration.log](uart/configuration.log)、[protocol-manifest.json](uart/protocol-manifest.json)。

- 原HSE/PLL时钟函数与最初备份逐字一致；SWD保留PA13/PA14。
- USART2 PA2 TX / PA3 RX，115200/8N1；CubeMX启用IRQ优先级5并生成HAL IRQ入口。
- PA8 TIM1_CH1 PWM2与PB0 DIR保留，三个串口无新增复用冲突。
- 四个共享/peer文件哈希与协作聊天冻结版本一致；ESP桥接源码测试快照另存manifest。
- 连续模式默认`MOTOR_CONTINUOUS_ENABLED=0`，ARM编译命令没有覆盖此值。运动返回NOT_READY，停止和状态可用；没有DEMO或假电压。
- main USER CODE先初始化通信再申请上电动作；主循环先处理停止/通信再处理定时器状态。

最终共享文件SHA256：

| 文件 | SHA256 |
|---|---|
| robot_wire.h | 619E120F1273B954325671E41CA36E2B8B6336CCF8A20835BE96564F18E6EE6F |
| robot_wire.c | 3868C735A5FE46279231DC9E7252D1ABE8E0DC57839F991FFDE5B1164B6DD575 |
| robot_peer.h | B9E40E578D1C3BAB6B018348992C55BE42D4ECAD9872843421BD3C2AB228DB09 |
| robot_peer.c | 076605A5A9BE0394DABD5D341ABE1F7C23E5D2C3F22B136125EF47F8534E9D6F |

## 第2轮：主机软件行为

34个测试组均通过：原运动15组、串口/协议/连续19组。主机编译启用`-Wall -Wextra -Werror`。日志：[host-motor.log](uart/host-motor.log)、[host-uart.log](uart/host-uart.log)。

串口19组包含：15个HAL模拟场景；1个共享协议套件；连续开关0/1各1组；开启连续分支的完整往复1组。套件中还包含多个CRC、序号与模式断言；“34组”不等于34次实物测试。

- 拆包、连续帧、随机噪声、CRC错误及重新同步；CRC标准检查向量123456789→0x29B1。
- 新会话先STOP、序号环绕、防重放、STOP重复确认、ESP重启恢复。
- RX溢出、UART错误、接收重开失败、临界区恢复原PRIMASK、缓存丢弃与恢复。
- 中断TX的在途缓冲不被新帧覆盖；新状态有界合并；启动失败与100ms发送超时。
- MCU独立600ms链路超时、运动500ms租约；PING不续租。
- 单次动作等待/运行/整批完成边界停止；部分脉冲中断不声称精确计数；不连接ESP32时保留原上电一次动作。
- 将ESP32桥接代码快照和STM32实际UART、motor、peer模块放入同一主机程序，以真实字节帧走接收回调和状态解码，核验两端STOP握手与运动拒绝。
- enabled=1仅用于桌面模型：连续PWM、1%/100%频率边界、DIR保持/建立等待、同方向调速不重置CNT、输出故障停止。
- 完整往复模型：300/200ms不等时长；等待期间APPLIED=0、计数启动后APPLIED=1；新token同参数不重置阶段；反向阶段调速保持方向及阶段；PING在线仍在运动租约到期后停止。

这些是源代码模型证据。模型不证明芯片输出实际波形、输入电路带宽、驱动器脉冲接受或机械运动。

## 第3轮：重生成、真实ARM编译链接

通过。使用已安装STM32CubeMX 6.18.1 / FW_F1 1.8.7；ARM GCC 14.3.1、CMake 4.4.0、Ninja 1.13.2。

- CubeMX实际重新生成后，15个最终生产文件SHA256完全不变，包括独立模块、USER CODE入口、IOC、MSP/IRQ与根CMake。日志：[regenerate-check.log](uart/regenerate-check.log)。
- 使用真实ST头文件编译寄存器断言，确认PWM2、强制无效模式、RCR与输出使能位。日志：[register-constants.log](uart/register-constants.log)。
- Debug/Release均干净构建并链接成功，日志没有编译警告或错误；两个ELF无未解析符号。
- nm确认USART2_IRQHandler、Rx/Error回调为实际强符号（T），未落入默认弱处理函数。日志：[link-symbols-Debug.log](uart/link-symbols-Debug.log)、[link-symbols-Release.log](uart/link-symbols-Release.log)。

| 构建 | FLASH占用 | RAM链接分配（含堆栈保留） | 结果 |
|---|---:|---:|---|
| Debug | 19396 B / 64 KB | 2392 B / 20 KB | 编译、链接通过 |
| Release | 9992 B / 64 KB | 2376 B / 20 KB | 编译、链接通过 |

日志：[build-debug.log](uart/build-debug.log)、[build-release.log](uart/build-release.log)。RAM表格是链接分配，不是运行峰值测量。

最终ELF：

- `build/MotorDebug/stmc8t6.elf`：DBF593F46E1CFAC2809430CA5A070D0E819EA7D8F8AA7F3E984726F6D8048A4B
- `build/MotorRelease/stmc8t6.elf`：0783C0C5F04FB2F4D54ADBA7B8A70B750C0B85C2987A836F5EE673C3F0D7A184

## 实施前仍需确认

桌面ESP32工程交付后，又核对四个共享/peer文件及两个ESP bridge文件与STM生产模块/测试快照逐文件SHA256一致；APK实物文件SHA256与协作聊天交付值一致。日志：[delivery-consistency.log](uart/delivery-consistency.log)。

ESP32源码：`C:\Users\y\Desktop\c8t6\esp32_robot`。安卓APK：`C:\Users\y\Documents\Codex\2026-10-13\ban\outputs\Motor-Control-v3\Motor-Control-v3.apk`。该目录中的source.zip为ESP32/APK及独立STM示例包；本聊天实际修改的STM32工程仍在`stmc8t6`目录。

ESP32/APK构建与签名细节由协作聊天记录在`esp32_robot/TEST_REPORT.md`；本聊天确认了交付文件和共享源码一致性，没有在本聊天重新执行ESP-IDF或APK构建。默认ESP UART/ADC关闭，实际STM连续运动也关闭。

1. 实际ESP32-S3板型及UART TX/RX GPIO，双方3.3V TTL电平、供电与公共地/隔离方案；ESP默认UART关闭且GPIO=-1。
2. T60实物版本、电气接口与转换电路、DIR建立/保持时间、PUL输入脉宽、ENA要求、动力供电。
3. T60每圈指令脉冲数、正确物理方向、机械允许频率/换向方式；当前100–1000Hz和2/10ms均为待确认的软件示例。
4. MCU实物HSE确为8MHz，PA8/PB0/PA2/PA3排针引出无板载冲突。

当前交付的软件连续模式关闭。后续确认参数后先测MCU和接口波形，再低速空载实测，最后评估带载加减速及往复。未确认前不能声称手机已控制真实电机，或电机已准确转动22.5°。
