# v3 验证记录（2026-10-13）

已完成：

- ESP-IDF v6.1、ESP32-S3目标默认UART/ADC关闭配置真实编译、链接、生成BIN/ELF；应用大小0x6f820（456736）字节，1MB分区尚余56%。
- UART和ADC启用的独立测试配置真实编译、链接通过。该配置仅检验条件编译分支，用UART1 GPIO4/5、ADC1通道0和100k/33k占位电阻，未作为真实接线或部署固件。
- Android SDK35/JDK17资源、Java、D8、zipalign构建成功，APK versionCode3/name3.0、应用名“电机控制台”，minSdk23/target35，v1/v2/v3签名验证通过。仅使用测试签名。
- 生产ESP控制核心：正/反、停止、800ms租约、断连、防重放、时钟/序号回绕、非法长度/参数、10000包随机测试，以及模式/时长解析。
- UART/生产STM peer/ESP bridge：CRC向量与逐字节破坏、10000随机字节、流式切分/连续帧/噪声重同步、启动STOP、丢ACK、相同token重试不续租、STOP抢占、重启session、未就绪和驱动错误。
- 往复模式：不等时长300/200ms循环、同参数新token心跳保留阶段、阶段切换时ACK仍匹配、失联停止、反转阶段调速保留方向和phase_at且不增加stop调用；重复STOP根据Stopped保持实际输出停止确认。
- Android生产协议/命令队列：真实C生成20字节状态跨语言验证、模式时长编码和范围、双层STOP确认、未知/旧ACK、串行GATT、STOP优先、过期运动丢弃、ACK/write期限、重连不恢复运行。
- 独立STM示例使用真实STM32F1 HAL/CMSIS头文件由ARM GCC执行 -Wall -Wextra -Werror 语法检查通过。此项未独立链接完整STM工程。

结果日志在release/verification。部分JDK中文编译提示编码显示不完整，是旧BLE兼容API弃用提示，编译和验签均成功。

未验证：APK安卓真机安装/屏幕/权限/真实BLE收发；ESP/ST硬件烧录和UART电气收发；实际PWM频率、换向、RPM、负载、停机时间、电池测量。本次串口枚举无板卡，ADB列表无手机。主机HAL模型和编译不能代替硬件测试。

交付默认UART与ADC关闭，独立STM示例硬件模式关闭。0–100%物理速度换算、电机驱动接口、ESP串口GPIO、DIR时序和电池分压待确认。
