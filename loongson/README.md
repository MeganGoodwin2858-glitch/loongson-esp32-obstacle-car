# 先锋派 UART 单前向 HC-SR04 避障客户端

新增 `avoidance_uart.py` 是本项目的避障入口。`car_uart.py` 保留为既有协议实现，新入口直接复用其中的 CRC16、命令编码和状态解码。只依赖 Python 标准库，不需要安装 `pyserial`。Python 3.9 及以上可运行；离线测试已在当前 Windows 主机执行，先锋派 Linux 上的 termios、真实 UART、HC-SR04 与车轮行为均未实测。本次不烧录、不连接真实串口。

## 接线与运行顺序

先锋派使用 J8 的 UART2：8 脚 TX 连接 ESP32 RX(GPIO9)，10 脚 RX 连接 ESP32 TX(GPIO8)，双方 GND 相连；使用 3.3V TTL，不连接 RS232。先锋派 UART0(`/dev/ttyS0`)保留给调试控制台。HC-SR04 在 ESP32 端采样，TRIG 为 GPIO10，ECHO 为 GPIO11；板上引出、复用和接线必须由人工核对。HC-SR04 常见模块用 5V 供电，5V ECHO 必须经合适分压或单向电平转换后接入 ESP32，不能把 5V ECHO 直接接 GPIO。

先把本目录两个 Python 文件放在先锋派的同一目录。以下为先锋派终端命令，不是本次硬件执行记录：

```sh
python3 avoidance_uart.py --preflight
python3 avoidance_uart.py
```

第一条只读设备属性、sysfs 和 `/proc`，不会打开 UART。第二条以 monitor 模式打开 `/dev/ttyS2` 的只读文件描述符、配置 115200/8N1 并打印状态和测距；它没有发送命令的权限。退出用 Ctrl+C。客户端关闭时尽力恢复进入前的 termios 设置。

`--preflight` 检查设备必须是字符设备、不能是 `ttyS0`，检查 `/proc/consoles`、内核 `console=` 参数及 getty 进程占用，核对 sysfs 的 `0x16100800` 与 `/proc/device-tree/aliases/serial2`。运动还要求设备名确实是 `ttyS2` 且地址和别名均通过。读取失败、串口被控制台或 getty 占用均拒绝打开；程序不会修改设备树、权限或系统服务。预检的 JSON 会给出具体原因，需要人工处理后重试。打开设备时另有文件锁与 TIOCEXCL 排他保护。

monitor 能连续收到有效状态和测距后退出，先完成接线和轮胎离地等现场确认，确保手机持续 BLE 连接、清除急停并重新选择 Auto，再由人工运行：

```sh
python3 avoidance_uart.py --enable-motion --wiring-confirmed
```

两个开关必须同时明确给出。`--wiring-confirmed` 表示操作者已确认实际接线，它不会自动检查接线。默认向右原地转；可用 `--turn-direction left` 改为向左。若启动时 Auto 已使能，程序拒绝接管，必须人工重新选 Auto。中途任何故障都会锁定退出，程序不会自动重新 ARM。

## 会话、时效与避障行为

状态需要同时满足 Auto、BLE 已连接、UART-ready、急停已清除。客户端绑定状态中的 `auto_session`，只接收同会话遥测，得到三个有效新采样后发一次 ARM；等待之后收到新的 armed 状态确认才开始 DRIVE。控制周期为 50ms；停车等待期间发零向量 DRIVE 保持会话，结束时用 STOP 取消使能。命令序号递增。若手机切模式或会话变化，客户端退出且不发旧会话 STOP。其它自身故障只在仍属于自己的会话时尽力 STOP，ESP32 的会话校验、传感器联锁和 300ms 命令看门狗仍需独立生效。

测距周期为 80ms，ESP32 状态和遥测各每 50ms 上报。有效距离为 20..4000mm；无回波、范围错误、噪声、关闭和过期都不可视为道路畅通。主机依据 `接收单调时刻 - reported age` 估计采样时刻，超过 180ms 就失效。相同 `sample_seq` 不算新样本、不增加准备或清路计数，也不能延长有效期；乱序样本导致终止。流解析支持任意分片、多帧与有界缓冲；噪声、CRC 错误、溢出都以 error 事件交给控制器，发生错误后即使后续帧恢复正常也不能继续运动。

| 条件 | 行为 |
| --- | --- |
| 前方 >450mm | 前进，速度参数 30% |
| 300mm < 距离 <=450mm | 前进，速度参数 20% |
| 120mm < 距离 <=300mm | 零向量停车至少 160ms，然后速度参数 25% 原地转向 |
| 原地转向 | 至少转 200ms，并连续收到三个新采样 >450mm 后进入 SETTLE |
| SETTLE | 停车至少 160ms，再依据最新距离决定前进或重新停车转向 |
| 转向达到 800ms 仍未确认清路 | 终止，要求人工重新启动 |
| <=120mm、传感器无效或过期、状态断流 >150ms、急停、BLE 断连、armed 丢失 | 终止，要求人工重新启动 |

单前向传感器没有后方信息，不会自动倒车。上述距离、速度和时长是固定初始工程参数，未经实车制动距离、转向方向、路面或负载标定；速度参数百分比也不是实测车速。

## 新增遥测帧

所有帧固定 20 字节；多字节数值采用小端。CRC 为 CRC16-CCITT-FALSE（初值 FFFF，多项式 1021），覆盖字节 0..17，CRC 自身以小端放在 18..19。旧命令 `0xa5` 与状态 `0x5a` 沿用 `car_uart.py`。

| 字节 | 含义 |
| --- | --- |
| 0..1 | `5b 01` |
| 2 | result：0 有效、1 超时、2 范围错误、3 噪声、4 关闭、5 过期 |
| 3 | flags：bit0 传感器开启，bit1 前进受限，bit2 极近停车 |
| 4..7 | `auto_session` u32 |
| 8..11 | `sample_seq` u32 |
| 12..13 | `distance_mm` u16，无效时 FFFF；可能是 ESP32 的保守滤波距离 |
| 14..15 | `age_ms` u16，距原始采样的年龄 |
| 16..17 | `echo_us` u16，最近原始 Echo 脉宽 |
| 18..19 | CRC16 |

## 离线测试与集成

在工程根目录运行：

```sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

测试不打开串口、不联网、不等待真实时间。对 Linux 元数据的测试使用临时目录。`StreamParser.feed(bytes)` 返回按顺序排列的事件字典，`kind` 为 `status`、`telemetry` 或 `error`；把每个事件传给 `Controller.on_event(event, monotonic_seconds)`，然后按 50ms 调用 `Controller.tick(monotonic_seconds)` 得到待发送的 20 字节命令列表。模拟器可使用同一接口，将虚拟毫秒时钟除以 1000 传入。一个 read 批次的所有事件必须先处理完再发送 tick 返回值，使该批内稍后的模式变化可以取消尚未发出的 STOP。

`Controller` 只处理协议与决策，不访问硬件；`PosixUART` 负责 Linux UART 配置与有截止时间的读写。所有 POSIX 专用导入均延迟到实际打开串口时进行，因此 Windows 可以直接运行协议、时序和故障回归测试。
