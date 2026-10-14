# 安卓 / ESP32-S3 / STM32F103C8T6 电机控制

手机通过 BLE 向 ESP32-S3 发送控制参数，ESP32 经 UART 转发到 STM32，由 STM32 驱动电机。支持正转、反转、0–100%速度、持续运行和定时交替正反转；两段时长分别可设0.1–3600秒。

**当前版本完成编译与软件模型验证，尚未进行手机、板卡或电机实测。ESP默认关闭UART/ADC，STM默认关闭连续电机输出；速度映射、ESP串口GPIO及驱动电气/换向参数须确认后配置。**

## 2026-10-14硬件核查与构建修复

- [检查、修复与验证报告](docs/AUDIT_REPORT_20261014.md)：按影响分级列出已确认缺陷、待核实风险和可选改进。
- [蓝板/Stamp-S3Bat DIP硬件连接方案](docs/HARDWARE_CONNECTIONS.md)：UART、条件式PUL/DIR接口、动力/编码器接线以及CAN预留边界。
- 当前原工程移至本README所在目录；构建/Git在当前目录执行。CubeMX优先使用`stmc8t6/tools/regenerate_cube.ps1`，不执行旧目录的生成命令。
- 中文目录下ESP/安卓原生工具通过独立ASCII源码工作区构建，产物回收到原目录；依赖与框架未升级。
- **关闭连续输出不关闭STM复位后一次22.5°动作。首次测试先不接T60 PUL；这不是已验证的实机控制工程。**
## 目录

```text
c8t6/                         # 统一 Git 根目录
├── stmc8t6/                  # STM32 实际工程、CubeMX IOC、驱动、测试
├── esp32_robot/              # ESP-IDF 工程与独立 STM32 接口示例
│   └── android/              # 原生安卓 Java App
├── tools/test_all.py         # 两端协议一致性与生产 C/Java 主机测试
├── .github/workflows/ci.yml  # push / PR 自动主机测试
└── docs/GIT_GUIDE.md          # 日常版本管理方法
```

原STM32仓库历史保留，首次结构调整只改变文件路径。当前归并工程的Git根目录是本README所在目录；历史路径`C:\Users\y\Desktop\c8t6`不再作为本工程工具的硬编码目标。构建缓存、备份、私钥、机器配置和桌面的硬件参考资料保留在本地；APK和固件通过GitHub Releases发布，不混入源码历史。

## 构建和使用

- [ESP32 / 安卓说明](esp32_robot/README.md)：ESP-IDF v6.1、ESP32-S3；安卓源码在`esp32_robot/android`，SDK35、JDK17。
- [STM32 UART接口与连续电机说明](stmc8t6/README_UART.md)：USART2，PA2 TX / PA3 RX，115200/8N1。
- [STM32原本单次运动说明](stmc8t6/README_MOTOR.md)：保留原上电22.5°动作逻辑及与串口STOP的关系。
- [BLE v3 / UART v2协议](esp32_robot/PROTOCOL.md)：16字节BLE命令、20字节通知、32字节UART帧、双层确认。
- [版本与下载](https://github.com/yuanlairuqi/stmc8t6/releases)：v3.0.0测试APK及默认BLE演示固件。

运行模式由STM核心执行。心跳只续租，不重启往复阶段；在反转阶段调速保留方向与阶段时间。手机进入后台、链路失联、确认超时均触发停止，不在重连后自动继续。

## 自动测试

Linux安装GCC和JDK后，在根目录执行：

```sh
python3 tools/test_all.py
```

Windows使用当前已安装的本地工具：

```powershell
python tools/test_all.py --cc 'D:\mingw\code\mingw64\bin\gcc.exe' --java-bin 'D:\uu\bin'
```

测试直接编译生产C文件和生产安卓协议/发送队列，包含38个主机测试组，并检查STM与ESP共享文件一致性。模型测试不证明实物波形或机械运动。STM真实ARM编译、ESP-IDF固件编译、APK构建属于另外的工具链验证，见[ESP测试记录](esp32_robot/TEST_REPORT.md)及[STM验证报告](stmc8t6/verification/UART_REPORT.md)。

## 第三方许可

ST HAL/CMSIS文件保持原厂许可证，见`stmc8t6/Drivers`。本仓库未为自有代码额外授予开源许可；如需公开再分发许可，请由作者确定。
