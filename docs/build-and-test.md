# 构建与测试

## 环境

| 用途 | 环境 |
| --- | --- |
| ESP32 固件 | ESP-IDF 5.5.4、ESP32-S3 工具链、Python 环境、CMake、Ninja |
| 桌面 C++ 测试 | Visual Studio x64 开发终端，支持 C++17 |
| Python 协议与状态机 | Python 3.9 及以上，仅使用标准库 |
| 手机协议测试 | Node.js |
| 龙芯运行 | 先锋派 Linux、Python 3.9 及以上、UART2 |

已有 `idf_component.yml` 声明最低 IDF 版本为 5.4.0，本项目恢复验证采用 5.5.4。团队先使用该版本复现，再单独评估升级。

## 获取依赖

根目录的 `dependencies.lock` 与 `main/idf_component.yml` 一并纳入版本管理。联网环境由 ESP-IDF 组件管理器恢复 `managed_components/`。离线环境可将交付的依赖 ZIP 解压到工程根目录，形成 `managed_components/<组件名>/`。

本次检查了现有 55 个组件清单中的 8,304 项文件，均与其 `CHECKSUMS.json` 一致。离线依赖包携带这些实际文件，开发工具链需要在各电脑安装。[依赖说明](dependencies.md)记录具体边界。

## ESP32 构建

在 ESP-IDF 5.5.4 开发终端进入仓库根目录。Windows PowerShell 执行以下命令。

```powershell
Copy-Item -LiteralPath config/sdkconfig.car -Destination sdkconfig
python tools/embed_wifi_page.py
idf.py -D CMAKE_NINJA_FORCE_RESPONSE_FILE=ON -B build build
```

Linux/macOS 的等价入口如下。

```sh
cp config/sdkconfig.car sdkconfig
python tools/embed_wifi_page.py
idf.py -D CMAKE_NINJA_FORCE_RESPONSE_FILE=ON -B build build
```

构建结果通常包含 `build/xiaozhi.bin`、bootloader、分区表及资源镜像。以实际构建日志和 `flasher_args.json` 核对地址与内容。

配置快照使用 `qebabe-xiaoche`、ESP32-S3、16MB Flash、DIO/40MHz、Octal PSRAM、UART TX8/RX9、TRIG10/ECHO11 和 SSD1306 128×64。原有 `sdkconfig.defaults.esp32s3` 含 QIO 默认值，因此复现本车时以 `config/sdkconfig.car` 为起点。执行 `set-target` 或删除 `sdkconfig` 会重新应用默认值，需要重新核对板型与 Flash 参数。

修改 Wi-Fi 页面后先运行 `tools/embed_wifi_page.py`，检查 `main/web_server/remote_page.h` 的差异，再编译。

## 桌面完整回归

在 Visual Studio x64 开发终端设置 `IDF_PATH` 指向 ESP-IDF 目录，保证 Python 和 Node.js 可用，然后从仓库根目录执行。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_host_tests.ps1
```

已有脚本依次运行以下项目。

1. `car_core_test.cpp` 检查控制核心、模式、会话、重放、超时和急停。
2. `web_safety_test.cpp` 使用 ESP-IDF 的 cJSON 检查 HTTP 请求处理。
3. `phone_test.cjs` 检查手机帧、CRC、队列合并、停车优先级和过期命令。
4. `range_test.cpp` 检查测距处理、联锁和 UART 接收。
5. `firmware_simulator.cpp` 编译实际 C++ 控制核心的桌面模拟器。
6. Python `unittest` 执行协议、预检、避障状态机与跨语言字节流集成测试。

可以单独运行不需要编译器的协议测试。

```sh
node tests/phone_test.cjs
python -m unittest discover -s tests -p "test_*.py" -v
```

完整的 Python 运行会发现 49 项测试，其中 7 项依赖 `CAR_FIRMWARE_SIMULATOR` 指向已编译的模拟器。缺少模拟器时这些项目会显示跳过。完整回归脚本会构建并配置模拟器。

## 上板验证

将完整 `loongson/` 目录复制到龙芯 Linux 工作目录，保持两个 Python 文件相邻，先执行以下命令。

```sh
cd loongson
python3 avoidance_uart.py --preflight --port /dev/ttyS2
python3 avoidance_uart.py --port /dev/ttyS2
```

按 [运行说明](operation.md)逐项检查手机连接、UART、测距、使能与急停。固件部署前需要核对具体芯片、Flash 分区、bootloader、资源和配网数据，并保存可恢复备份。已有板子的应用写入历史地址为 `0x20000`，新板部署方案需要结合完整分区表和资源准备。

每次实测记录源码提交号、配置、接线、固件哈希、命令与结果。恢复后的实际运行结果见 [验证报告](verification.md)。
