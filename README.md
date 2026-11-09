# 龙芯先锋派 × ESP32-S3 避障小车

团队协作源码快照，整理日期为 2026-11-06。资料来自“添加 BLE 手机小车控制”对话对应的本地工程、用户上传源码和后续交付包。目录保留 ESP-IDF 工程结构，便于组员继续编译、测试和修改。

团队文档更新日期为 2026-11-09。新组员可先阅读 [上手流程](docs/getting-started.md)，按负责模块查找源码和验证入口。

完整工程已上传到 [MeganGoodwin2858-glitch/loongson-esp32-obstacle-car](https://github.com/MeganGoodwin2858-glitch/loongson-esp32-obstacle-car) 的 `main` 分支，仓库可见性为公开。上传与逐文件核对记录见 [GitHub 交付记录](docs/github-status.md)。

## 当前进度

| 项目 | 已核实状态 |
| --- | --- |
| ESP32 源码 | 完整工程存在，板型为 `qebabe-xiaoche`，目标为 ESP32-S3 |
| 手机控制 | BLE 页面与 Wi-Fi 页面源码均存在 |
| 龙芯程序 | Python UART 协议与 HC-SR04 避障状态机源码存在 |
| 测距与停车联锁 | ESP32 RMT 驱动、距离处理、超时停车和急停逻辑存在 |
| 2026-11-05 已存板测 | 应用回读一致，UART 双向通信通过，收到有效测距遥测 |
| OLED | 用户现场反馈持续黑屏，待定位 |
| 实车验收 | 轮向、速度、停距、负载供电和自动避障效果待验收 |

本次恢复后的测试结果见 [验证报告](docs/verification.md)。硬件进度依据 [2026-11-05 检测证据](docs/evidence/2026-11-05/esp32/combined-result.json)，其中正向 UART 约 10 秒接收 197 帧状态和 197 帧测距，解析错误为 0，反向急停得到确认。测距中包含 3 帧超范围结果，实际距离精度仍待标定。

## 快速开始

1. 阅读 [接线说明](docs/wiring.md)，核对实物板版本、GPIO 丝印与龙芯 J8 物理脚号。
2. 按 [构建与测试说明](docs/build-and-test.md)准备 ESP-IDF 5.5.4，复制 `config/sdkconfig.car` 为根目录的 `sdkconfig`，再构建。
3. 手机与小车处于同一 Wi-Fi 时，用设备当前 IP 打开网页，先使能，再操作摇杆。历史 IP 随 DHCP 改变。
4. 龙芯端先运行只读预检，再运行监视模式，观察连续有效状态和测距。
5. 自动模式需要手机 BLE 保持连接并切到“龙芯自动”。结合 [运行说明](docs/operation.md)和现场验收步骤操作。

## 目录

```text
.
├── main/                       ESP32 应用、板级支持、音频、显示和车控源码
│   ├── boards/qebabe-xiaoche/   本项目板级配置与电机适配
│   ├── car_control/            BLE、PWM、UART、HC-SR04 和停车联锁
│   └── web_server/             HTTP 控制与嵌入网页
├── loongson/                   龙芯 Python UART 客户端与避障状态机
├── phone/                      BLE 与 Wi-Fi 手机页面
├── tests/                      已有 C++、Python、Node 测试与固件核心模拟器
├── tools/                      网页生成与桌面测试入口
├── config/sdkconfig.car        已核实工程的完整配置快照
├── partitions/                 原有 Flash 分区表
├── scripts/                    原工程构建、资源和语言处理脚本
├── docs/                       接线、协议、进度、来源和验证说明
├── CMakeLists.txt
├── dependencies.lock           已有 ESP-IDF 组件版本锁
├── LICENSE                     原工程 MIT 许可证
├── CHANGELOG.md
└── TODO.md
```

`managed_components/` 由组件管理器按 `main/idf_component.yml` 和 `dependencies.lock` 恢复。离线构建可使用单独的依赖 ZIP，将其中的 `managed_components/` 放到工程根目录。依赖包与源码包分别交付，Git 历史记录项目源码和必要资源。

## 功能与入口

- [BLE 协议](docs/ble360-protocol.md)定义手机帧格式、会话、序号、CRC、使能和急停。
- [UART 与测距协议](docs/uart-hcsr04-protocol.md)定义龙芯命令、状态、遥测及超时行为。
- `loongson/avoidance_uart.py` 是自动避障入口，`loongson/car_uart.py` 提供兼容协议工具。
- `phone/index.html` 和 `phone/controller.js` 提供 BLE 页面。浏览器需要支持 Web Bluetooth，实际手机兼容性待验证。
- `phone/wifi.html` 提供 Wi-Fi 手动控制。修改后运行 `python tools/embed_wifi_page.py`，再重新编译。
- 手动、自动和原功能模式通过控制核心分配控制权。原工程的语音、音频、显示和 MCP 等源码一并保留。

## 团队协作

开始工作前查看 [版本记录](CHANGELOG.md)、[待办](TODO.md)和 [协作约定](docs/collaboration.md)。提交硬件进展时附上日期、板型、源码版本、接线、操作和测试结果，区分桌面测试、协议通信与实车验收。

[故障排查](docs/troubleshooting.md)覆盖网页控制、UART、自动使能、测距、OLED 和构建问题。[测试记录模板](docs/test-record.md)可直接用于任务与合并请求。

[源码恢复与来源说明](docs/recovery.md)列出已找到的文件和待补资料。[逐文件清单](docs/source-inventory.json)保存原始来源、SHA256 和与上传版本的比较结果。

## 许可证

保留原工程 `LICENSE` 及第三方文件自身声明。供应商原理图、芯片手册、内核和 U-Boot 资料的来源与处理见 [依赖及第三方说明](docs/dependencies.md)。
