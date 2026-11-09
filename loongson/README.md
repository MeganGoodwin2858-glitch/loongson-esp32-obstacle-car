# 先锋派 UART 与 HC-SR04 避障入口

本目录包含两个实际存在的 Python 程序，使用 Python 3.9 及以上和标准库。

| 文件 | 用途 |
| --- | --- |
| `avoidance_uart.py` | 串口预检、监视、自动避障状态机与 Linux UART 运行入口 |
| `car_uart.py` | 既有 CRC、帧编码和状态解码，供避障入口复用 |

## 当前验证进度

2026-11-05 已存记录支持龙芯与 ESP32 双向 UART 通信及有效测距遥测，反向急停得到确认。2026-11-06 已执行的 49 项 Python 测试包含协议、状态机和 C++ 核心模拟器集成，详见 [验证记录](../docs/verification.md)。

实际距离精度、轮向、机械停车与实车自动避障效果仍待验收。

## 运行

保持两个 Python 文件相邻，在先锋派 Linux 的本目录执行以下命令。

```sh
python3 avoidance_uart.py --preflight --port /dev/ttyS2
python3 avoidance_uart.py --port /dev/ttyS2
```

第一条检查设备身份与占用，第二条进入默认监视模式。根据 [接线说明](../docs/wiring.md)核对 IO8/IO9 与龙芯 J8 物理 10/8 脚及共地。

现场条件和控制会话确认后，按 [运行与验收说明](../docs/operation.md)使用自动模式。自动入口要求 BLE 在线、Auto 模式、连续新测距、使能确认，以及同时提供 `--enable-motion --wiring-confirmed`。

## 开发与排查

- [组员上手流程](../docs/getting-started.md)提供模块与测试入口。
- [UART 遥测协议](../docs/uart-hcsr04-protocol.md)定义帧格式、会话与时效。
- [故障排查](../docs/troubleshooting.md)解释预检、控制权、测距和退出原因。
- [测试记录模板](../docs/test-record.md)用于记录板上与实车验收。

开发阶段的原始说明按字节归档到 [历史 README](../docs/history/README_loongson.md)，其中的验证进度对应当时日期。
