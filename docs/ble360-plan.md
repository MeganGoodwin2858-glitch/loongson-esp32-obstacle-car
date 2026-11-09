# BLE 360° Implementation Plan

**Goal:** 在原始小智小车工作副本实现并测试 BLE 360° 控制，不烧录。
**Architecture:** 独立控制核心 + NimBLE/UART/PWM 服务 + 原 Application 适配 + 手机手柄。
**Tech Stack:** C++17 / ESP-IDF 5.5.4 / NimBLE / JavaScript / Python。
**Spec:** ble360-design.md

## Global Constraints
仅修改本次解压工作副本；禁止设备访问和烧录；完整保留非电机业务；构建完全离线。

## Review Focus
旧动作在切换后复活；停止前缓冲的旧运动帧；错误数据续命；手机发送队列积压；PWM 故障继续转动。

## Task 1: 控制核心和协议
- [x] 先写并运行行为测试（缺核心时应失败）。
- [x] 实现 Mix、Decode、Core::Receive/Connect/Disconnect/Tick/Legacy，20字节 CRC 帧。
- [x] MSVC /W4 /WX 编译并运行全部行为测试。

## Task 2: 固件集成
- [x] 新增 car_service.cc/h：串行控制、零输出后换向、300ms watchdog、BLE 单连接、可选 UART。
- [x] 将 Application 电机入口适配到服务，删除直接 PWM/延迟停止路径。
- [x] 配置 NimBLE、保留原板型，离线完整构建并检查镜像大小。

## Task 3: 手机和龙芯端
- [x] 手机先写帧和发送队列测试，再实现 360° 单指摇杆、速度、松手、急停、模式和状态。
- [x] 提供龙芯 UART 客户端与协议说明，默认不发运动、不启用未确认引脚。
- [x] 离线运行 Node / Python 测试。

## Task 4: 交付
- [x] 审查所有电机输出入口及故障路径，核对 RAR 哈希不变。
- [x] 输出测试记录、变更清单、完整源码压缩包和编译产物（未烧录）。

