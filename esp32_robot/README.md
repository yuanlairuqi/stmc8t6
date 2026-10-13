# 安卓电机控制台 v3

工程：`C:\Users\y\Desktop\c8t6\esp32_robot`。链路：安卓手机 → BLE → ESP32-S3 → 3.3V UART → STM32F103C8T6 → 电机驱动器。

界面只有正转启动、反转启动、停止、0–100%速度，以及持续运行/定时交替两种模式。持续模式点击启动后运行到停止；交替模式从所选方向开始循环，正转/反转各自时长可设0.1–3600秒（默认各2秒）。运行中可以调速，改模式会先停止；修改时长需先停止。0%自动发送STOP。退到后台、关闭页面、蓝牙/串口断开、指令确认超时均触发停止保护，重连不会自动恢复运行。

**交付的默认ESP固件关闭UART和ADC，供BLE通信演示；ESP TX/RX仍为待定。请确认引脚后启用UART并重新构建。STM32独立示例默认只做逻辑演示，电机驱动接口未接入。默认固件不是已经完成实机控制的固件。**

## 文件与构建

- `release/ESP32-Control-debug.apk`：可侧载测试APK（应用名“电机控制台”，v3）。
- `release/firmware/`：ESP32-S3默认BLE演示固件、bootloader、分区表、ELF。
- `android/`：Java原生安卓源码，Android 6及以上；SDK35、JDK17。
- `main/`：ESP-IDF v6.1工程，BLE、状态管理、UART转发、可选ADC。
- `shared/`：UART固定32字节v2编解码和CRC。
- `stm32_example/`：独立STM32接口示例与往复执行核心，不是现有STM工程的替代工程。
- `PROTOCOL.md`：完整通信字段与确认规则。
- `TEST_REPORT.md`、`release/verification/`：验证范围、日志。

```powershell
.\tools\firmware.ps1 -Action build
.\tools\firmware.ps1 -Action menuconfig
# 确认实际COM和模块配置后才烧录，COM5只是示例
.\tools\firmware.ps1 -Action flash -Port COM5
.\tools\firmware.ps1 -Action monitor -Port COM5
.\tools\test-host.ps1
.\android\build-apk.ps1 -SdkPath 'C:\path\to\Android\Sdk' -JavaPath 'D:\uu'
```

Android Studio也可导入`android/`，或用脚本直接运行aapt2、javac、D8、zipalign、apksigner。桌面工程保留原测试签名密钥，可覆盖安装原版；源码包不包含私钥，重新生成签名可能需要卸载旧APK。默认ESP构建使用4MB Flash、无PSRAM，需核对实际模组。

## UART与硬件待定项

已从配合的STM32工程核对：USART2，PA2=TX、PA3=RX，115200/8N1、无流控。ESP侧UART1/2可配置，TX/RX尚未确定；`menuconfig` → `Robot UART to STM32`中填实际引脚、启用UART。代码拒绝-1、相同或无效GPIO，初始化失败不会放行运动。

| ESP32-S3 | STM32F103C8T6 |
|---|---|
| 已确认的TX GPIO | PA3 / USART2_RX |
| 已确认的RX GPIO | PA2 / USART2_TX |
| GND | GND |

这里是3.3V逻辑UART，不能直接接RS232电平。测试启用分支曾使用GPIO4/5和ADC GPIO1作为**编译参数**，并非已经确定的接线，交付默认配置未采用它们。

电机接口位于`stm32_example/robot_port.c`：Ready、Apply、Stop、Stopped、Active、LocalBusy、Battery。Apply接收方向1正/2反及1–100%，须按驱动器实现同方向调速、换向停输出、DIR保持/建立时间和必要的加减速；Active只在输出生成真正启动时返回true。未实现时不能启用硬件模式。输出APPLIED表示控制器输出状态，并非编码器证明电机已转动。

往复时长从逻辑方向请求开始计时，包含驱动器换向等待；0.1秒最小值、主循环调度与换向延时会影响实际出脉冲时长，不能宣称实测精度。0–100%保留硬件换算接口，尚无确定RPM/Hz标定。电池来源优先STM有效回传，或ESP可选校准ADC；未知时显示“未接入”，不会伪造电量。

## 独立STM32示例接入

复制`shared/`及`stm32_example/`到自己的工程、添加编译路径，初始化UART后调用`RobotUART_Init(&huart2)`；主循环持续调用`RobotUART_Poll()`，将已有HAL RX完成/错误回调转发给`RobotUART_RxComplete(huart)`/`RobotUART_Error(huart)`。示例不会定义覆盖现有HAL回调，也不选择GPIO或定时器。RX中断只入环形缓冲，协议与电机钩子在主循环执行，TX使用中断，避免阻塞看门狗。

首次启动或ESP重启必须STOP握手。新的手机参数每200ms发送一次；ESP运动租约800ms，STM运动租约500ms，串口重试使用原token、PING不续租。往复核心在STM端执行，同参数心跳不重新计时，反转阶段调速保留当前方向和阶段时间。任何停止优先于待发运动，过期运动不会延后执行。

## 手机上验证

安装APK→授予附近设备权限→搜索`ESP32-Robot`→连接→等待首次STOP确认。旧安卓可能需要定位权限与定位服务。先在默认BLE演示验证按钮、速度和模式参数收发；该演示没有STM往复输出。UART启用并接入匹配v2协议STM后，界面分别显示ESP接收序号和STM确认序号。STM离线、未就绪、故障、本地动作占用时锁定运动，STOP仍可发送。

旧BLE v1/v2、旧UART24字节版本均不兼容；APK、ESP与STM共享协议须同步升级。本次完成编译和自动化模型测试，尚未烧录板卡或在安卓真机测试。

官方实现参考：[ESP32-S3 UART](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/uart.html)、[Android BLE收发](https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data)、[Android蓝牙权限](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions)。
