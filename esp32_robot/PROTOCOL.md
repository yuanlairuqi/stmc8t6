# 电机控制协议：BLE v3 / UART v2

全部多字节整数为无符号小端。方向0停止、1正转、2反转；模式0持续、1定时交替。速度1–100%才允许方向1/2，速度0必须STOP。模式1正反时长各100–3600000ms。循环从请求方向开始，按当前方向时长交替；正常心跳只续租，调速保留阶段，方向/模式/时长变化重建阶段。实际物理转动尚无传感器反馈。

## BLE

广播名`ESP32-Robot`，一台手机。Service=`6e400001-b5a3-f393-e0a9-e50e24dcca9e`，Command=`6e400002-b5a3-f393-e0a9-e50e24dcca9e`（Write with response），Status=`6e400003-b5a3-f393-e0a9-e50e24dcca9e`（Read/Notify），CCCD=0x2902。16字节命令和20字节通知适配默认ATT MTU，不依赖扩展MTU。

| 命令偏移 | 内容 |
|---|---|
| 0 | 版本3 |
| 1 | 请求方向0/1/2 |
| 2 | 速度百分比，STOP=0 |
| 3 | 模式0/1 |
| 4–7 | phoneSeq uint32 |
| 8–11 | 正转时长ms uint32，模式0忽略 |
| 12–15 | 反转时长ms uint32，模式0忽略 |

APK STOP固定模式0和两时长0，不受输入框无效值影响。ESP确认仅表示解析接受；独立STM确认才表示STM处理了该UART命令。非法报文会停止，但不以新seq虚假确认接受。

| 状态偏移 | 内容 |
|---|---|
| 0 | 版本3 |
| 1 | STM逻辑当前方向0/1/2，5=本地动作占用；无UART时为ESP演示方向 |
| 2 | flags：bit0 ESP就绪，1演示，2电压有效，3STM在线，4STM连续控制就绪，5输出已应用，6STM故障，7UART配置启用 |
| 3–4 | battery mV uint16，0xFFFF未知 |
| 5 | ESP已接受的请求方向 |
| 6 | ESP已接受的请求速度百分比 |
| 7 | error：0正常1非法2手机租约过期3BLE断开4ADC失败5UART未配置6STM离线7STM拒绝/故障8UART发送失败9STM确认超时 |
| 8–11 | ESP已接受的phoneSeq |
| 12–15 | STM已确认的phoneSeq |
| 16–17 | ESP运行秒数uint16，每65536秒回绕 |
| 18 | STM回报模式0/1（无UART时为ESP演示模式） |
| 19 | STM回报当前阶段方向0/1/2（无UART时为ESP演示方向） |

STM离线时方向/模式的缓存不能当作当前状态，APK显示未知。bit5由STM Active/Stopped输出检查得出，不等于编码器实测，也不能用ESP ACK代替。

## UART

固定32字节，115200/8N1。帧头A5 5A，版本2；CRC16-CCITT-FALSE（poly0x1021、init0xFFFF、不反转、xorout0），覆盖0..29，结果小端；已知向量`123456789`=0x29B1。

| 全帧偏移 | 字段 |
|---|---|
| 0–1 | A5 5A |
| 2 | 2 |
| 3 | 类型1COMMAND、2STATUS、3PING |
| 4–7 | ESP启动session uint32，随机非零 |
| 8–11 | UART token uint32 |
| 12–29 | payload18字节 |
| 30–31 | CRC16小端 |

COMMAND payload：0方向、1速度、2–3租约ms（ESP固定500，允许100–1000）、4–7phoneSeq、8模式、9保留0、10–13正转ms、14–17反转ms。

STATUS payload：0当前逻辑方向、1当前速度、2flags（READY=1/DEMO=2/BATTERY=4/APPLIED=8/LOCAL_BUSY=16）、3error（0正常1非法2租约过期3未就绪4驱动错误）、4–5电池mV（FFFF未知）、6–9phoneSeq、10模式、11当前方向阶段、12–17保留0。session/token回显最近被处理的命令，PING不推进确认序号。

新的session必须从STOP开始。新的token按uint32半范围比较防重放；重复/旧运动不续租，重复/旧STOP仍执行停止但保留原token/phoneSeq。格式错误强制停止。PING只报告健康、不续租。处于演示时绝不置APPLIED；移动APPLIED还要求Active返回真实输出生成状态，停止APPLIED要求Stopped返回输出已停。

## 时序与队列

手机200ms心跳；ESP800ms租约；STM500ms租约；ESP重试100ms、命令确认期限400ms、健康PING200ms、STM离线期限600ms；设备状态约100ms通知。APK应用确认1000ms、GATT写入1500ms、状态过期1500ms。主循环和操作系统调度带来延迟，以上是逻辑保护期限，不是硬实时实测制动时间。

ESP一条等待确认命令+一个最新待发参数，STOP抢占；ACK丢失后丢弃排队运动并发STOP。超时重试原token不能延长STM运动。APK串行GATT写入，最新参数覆盖未发送参数，未发送运动超过300ms丢弃，最多4条运动等待应用确认；STOP不受其限制。首次连接需已知STOP同时获ESP和STM确认（无UART仅ESP演示），失联/后台停止且重连不自动恢复运行。

往复phase_at从发出逻辑方向请求的时间开始，硬件DIR保持/建立等待包含在该段逻辑时长内。Apply实现负责硬件换向与调速，主循环应持续服务硬件状态；实际脉冲频率映射和电机状态反馈独立确认。
