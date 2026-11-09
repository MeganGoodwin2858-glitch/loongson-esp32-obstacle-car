# 组员上手流程

本流程适用于取得完整源码后的首次使用。先检查目录中存在 `CMakeLists.txt`、`main/`、`loongson/`、`phone/` 和 `tests/`，再执行下面的命令。远端同步进度见 [GitHub 交付记录](github-status.md)。

## 先确认自己负责的模块

| 工作 | 主要入口 | 开始前阅读 |
| --- | --- | --- |
| ESP32 车控、停车与控制权 | `main/car_control/car_core.h`、`car_service.cc` | [BLE 协议](ble360-protocol.md)与 [运行说明](operation.md) |
| 超声波测距 | `hcsr04.cc`、`range_core.h` | [接线说明](wiring.md)与 [UART 遥测协议](uart-hcsr04-protocol.md) |
| 龙芯避障策略 | `loongson/avoidance_uart.py`、`car_uart.py` | [龙芯入口说明](../loongson/README.md) |
| Wi-Fi 手柄 | `phone/wifi.html`、`main/web_server/` | [构建与测试](build-and-test.md) |
| BLE 手柄 | `phone/index.html`、`phone/controller.js` | [BLE 协议](ble360-protocol.md) |
| 板级接线、OLED、电机 | `main/boards/qebabe-xiaoche/config.h`、`config/sdkconfig.car` | [接线说明](wiring.md)与 [故障排查](troubleshooting.md) |

## 在电脑上熟悉协议

安装 Python 3.9 及以上和 Node.js，在工程根目录运行。

```sh
node tests/phone_test.cjs
python -m unittest discover -s tests -p "test_*.py" -v
```

Python 会发现 49 项测试，其中 7 项需要已编译的固件核心模拟器。查看结尾的通过和跳过数量，完整回归入口见 [构建与测试](build-and-test.md)。

## 构建 ESP32 固件

准备 ESP-IDF 5.5.4 开发终端。下列 PowerShell 命令从工程根目录执行。

```powershell
Copy-Item -LiteralPath config/sdkconfig.car -Destination sdkconfig
python tools/embed_wifi_page.py
idf.py -D CMAKE_NINJA_FORCE_RESPONSE_FILE=ON -B build build
```

组件管理器依据 `dependencies.lock` 恢复托管依赖。离线环境按 [依赖说明](dependencies.md)解压独立依赖包。

构建结束后保存完整日志和 `build/xiaozhi.bin` 的 SHA256，核对目标 ESP32-S3、板型 `qebabe-xiaoche`、16MB Flash、DIO/40MHz 与 OLED 选项。

## 在龙芯板上观察通信

把完整 `loongson/` 目录复制到先锋派 Linux 工作目录，在该目录执行。

```sh
python3 avoidance_uart.py --preflight --port /dev/ttyS2
python3 avoidance_uart.py --port /dev/ttyS2
```

预检输出应确认 UART2 身份、控制台占用和设备树信息。监视输出中重点观察 `kind=status`、`kind=telemetry`、`result`、`sample_seq` 和 `age_ms`。状态、测距和错误信息的含义见 [故障排查](troubleshooting.md)。

## 接续任务

从 [待办](../TODO.md)选取一项，记录目标和验收条件。在分支上完成修改，按 [协作约定](collaboration.md)运行相关检查，并使用 [测试记录模板](test-record.md)附上结果。

当前 OLED 黑屏、轮向、停距、负载供电和实车自动避障仍列为待验收，接手时优先核对对应证据。
