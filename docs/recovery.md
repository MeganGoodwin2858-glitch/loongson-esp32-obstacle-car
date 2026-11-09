# 源码恢复与来源核验

整理日期为 2026-11-06。目标是恢复既有工程，建立团队可以接续维护的源码目录。

## 资料链

1. 用户引用的 ChatGPT 对话为“避障小车GitHub仓库”，标识为 `6aecbacb-f4dc-83ec-9796-e61313ba8090`。
2. 其中引用“添加 BLE 手机小车控制”，本地聊天标识为 `01a1a5a2-1081-7541-b9dc-beebf0275ed4`。
3. 当前工具集缺少 `read_thread`。本次读取对应本地保存记录，并以磁盘实际文件核实源码。
4. 原上传文件 `xiaozhi-esp32-main.rar` 实际存在，本次计算 SHA256 为 `8c7de11de7a9bffaacd40f51c8ad4d6f881a8bcfb2cdc87c7259dcf28da45a88`，与原交付记录一致。
5. 完整工作树为原聊天工作目录中的 `work/car-v2`。后续 `ESP32_本次重烧_程序与检测证据_20261105.zip` 包含 63 个源码/配置片段，逐一与完整工作树比对均一致。

用户上传工程的原始解压目录也实际存在，用于比较文件变化。恢复快照保留原 ESP-IDF 根目录布局，避免改变已有构建与测试的相对路径。

## 已找到的真实文件

| 内容 | 入口与文件 |
| --- | --- |
| ESP32 完整应用 | `CMakeLists.txt`、`main/`、`partitions/`、`scripts/` |
| 板级配置 | `main/boards/qebabe-xiaoche/config.h`、`compact_wifi_board.cc`、`motor_controller.h` |
| 控制核心 | `main/car_control/car_core.h`、`car_service.cc/.h` |
| 测距与串口 | `hcsr04.cc/.h`、`range_core.h`、`uart_receiver.h` |
| 网页控制 | `main/web_server/`、`phone/wifi.html` |
| BLE 页面 | `phone/index.html`、`phone/controller.js` |
| 龙芯避障 | `loongson/avoidance_uart.py`、`car_uart.py` |
| 回归测试 | `tests/` 中 8 个 C++、Python、Node 源文件 |
| 构建依赖 | `main/idf_component.yml`、`dependencies.lock`、实际存在的托管组件目录 |
| 配置与协议 | `config/sdkconfig.car`、BLE/UART 协议文档 |

最初复制的 1,013 个文件中，956 个与原上传解压文件一致，9 个已修改，43 个为上传后的开发新增文件，另有 5 个后续检测证据 JSON。随后新增本次团队说明文档。完整逐文件来源和 SHA256 见 `source-inventory.json`。

9 个修改文件涉及 `application.cc`、Wi-Fi 板级公共代码、当前板级实现、主组件 CMake/Kconfig、`main.cc`、HTTP 服务、通用默认配置和完整配置快照。此分类描述磁盘文件与原上传版本的差异。

## 源码与进度的证据范围

- “真实存在”依据文件字节、路径、来源包及哈希比较。
- BLE、UART、HC-SR04 和自动避障由原聊天后续开发引入，逐文件清单保留其新增属性。
- 原工程 `PROJECT_VER` 为 `2.1.0`，来自已有 CMake。本次恢复使用日期标识 `recovered-2026-11-06`。
- 最近完整交付证据为 2026-11-05 22 点一轮重烧与检测。聊天随后出现新的试车请求及准备过程，当前已存资料缺少该轮试车的完整最终结论，版本进度按待验收记录。

## 资料分层

`docs/upstream/` 保存原工程说明，`docs/history/` 保存过去分阶段的 README。其中包含当时“烧录前”或“尚待通信验证”等状态，应结合当前 `README.md`、`CHANGELOG.md` 与检测日期阅读。

2026-11-09，将龙芯开发阶段 README 按原始字节归档为 `docs/history/README_loongson.md`，逐文件清单继续记录其来源和哈希。`loongson/README.md` 更新为当前运行入口和验证进度，新增上手、排查和测试记录模板。

供应商六个 ZIP 的文件目录、已有哈希和设备树核对记录归入 `supplier-archive-index.json`。相关原理图页、引脚图与设备树片段保留在 `docs/evidence/`。大体积 SDK、系统镜像、内核和 U-Boot 压缩包留作外部资料，索引可供追溯。

## 待补资料

- OLED 的确切控制器型号、模块供电测量和可见显示结果。
- 电机、驱动、电池的实际型号与电压参数。
- 新一轮试车的最终记录，包含轮向、低速动作、停距及自动避障结果。
- 真实手机 BLE 兼容性结果。
- 团队成员 GitHub 用户名、责任分工与后续发布版本规则。
