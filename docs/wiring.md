# 接线说明

本表由恢复的源码配置和 2026-11-05 接线纠正记录交叉核对。ESP32 使用 GPIO 编号，龙芯使用 J8 物理脚号。施工前断电，按板上丝印确认方向和针脚。

## 两板 UART

使用 3.3V TTL、115200 baud、8N1，双方共地。

| ESP32-S3 | 方向 | 龙芯先锋派 J8 |
| --- | --- | --- |
| IO8，UART1 TX | → | 物理 10 脚，UART2 RX |
| IO9，UART1 RX | ← | 物理 8 脚，UART2 TX |
| GND | ↔ | GND，例如物理 6 脚 |

龙芯侧设备节点为 `/dev/ttyS2`，寄存器地址为 `0x16100800`。`/dev/ttyS0` 用于调试控制台。J8 的物理 8、10 脚位于同一列并相邻。按第 1 脚标记确认下图方向。

```text
 1    2
 3    4
 5    6   GND
 7    8   UART2_TX → ESP32 IO9
 9   10   UART2_RX ← ESP32 IO8
```

ESP32 扩展板内圈的 IO8、IO9 中间隔着 IO3、IO46。历史上曾把 IO18 当作 IO8，产生错误信号。IO18 在源码中用于灯控制。

两块板按各自要求供电，电源正极分别连接各自供电输入，UART 信号采用交叉接法。龙芯针脚依据见 [供应商引脚复用图](evidence/先锋派引脚复用图.png)及 [原理图相关页](evidence/先锋派原理图第30页.png)。

## HC-SR04

| HC-SR04 | ESP32 连接 |
| --- | --- |
| VCC | 合适的 5V 供电 |
| GND | 与两板共地 |
| TRIG | IO10 |
| ECHO | 经降压后接 IO11 |

历史接线采用 ECHO 经 2.2kΩ 串联到 IO11，IO11 连接点再经 3.3kΩ 接 GND。输入为 5V 时，理想分压约为 3.0V。实际电压和波形待现场测量。ESP32 输入需要合适电平，接线时保留这一降压环节。

采样周期为 80ms，软件接受 20..4000mm。自动模式联锁使用 180ms 时效、300mm 前进限制和 120mm 极近停车阈值。单个前向传感器的覆盖范围、盲区和软质目标响应需要实测。

## 电机驱动

源码声明使用 TC1508 与两轮差速结构。

| 信号 | GPIO |
| --- | --- |
| 左轮正向输入 LF | IO12 |
| 左轮反向输入 LB | IO13 |
| 右轮正向输入 RF | IO14 |
| 右轮反向输入 RB | IO21 |

PWM 为 20kHz、10bit。驱动端电源电压、电机额定值和实际输入丝印待实物核对。首次验证应在轮子离地时检查正反向和急停，再测负载供电与机械停距。

## OLED

当前配置快照选择 SSD1306 128×64，I²C 地址 `0x3C`，总线速率 400kHz。

| OLED | 连接 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SCL | IO42 |
| SDA | IO41 |

2026-11-05 已存记录中 OLED 初始化日志正常，用户观察始终黑屏。控制器型号、模块供电和实物四针顺序待核对。UART 使用 IO8、IO9，OLED 使用 IO41、IO42。

## 配置依据

- `main/boards/qebabe-xiaoche/config.h` 声明电机、OLED、音频、按键与灯引脚。
- `config/sdkconfig.car` 保存 UART、HC-SR04、Flash 和 OLED 的实际构建选项。
- `main/car_control/car_service.cc` 实现 UART、LEDC 和 BLE 初始化。
- `main/car_control/hcsr04.cc` 实现 TRIG/RMT 测距。
