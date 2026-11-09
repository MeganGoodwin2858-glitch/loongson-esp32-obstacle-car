# Wi-Fi 手机手柄修复

此版本修复“网页接收请求但电机不动”：旧网页调用 Legacy 动作，控制核心要求先由 BLE 切 Legacy 模式并使能，回调返回值却被丢弃，HTTP 仍显示成功。现在 `/` 为独立的 Wi-Fi 手动控制页面，不需要手机浏览器支持 Web Bluetooth。

手机与 ESP32 在同一 Wi-Fi，访问设备当前 IP（DHCP 地址可能变化）。先点“使能遥控”，再按住摇杆拖动，上前进、下后退、左右转向。默认速度上限 40%，中心死区 12%。松手、切后台或停车后撤销使能，需要再次手动使能。急停锁定后，解除急停仍保持停车。

网页每 80ms 在一个未完成运动请求的限制下发送最新控制向量；已使能但未触摸时发送零向量。ESP32 每 10ms 检查 300ms 失联停车，不以 HTTP 连接是否仍存在判断在线。拥塞时可能更早停止控制，不自动重启。BLE 连接接管时先停车；网页不接管已有 BLE 或龙芯自动控制会话。龙芯自动和原动作模式仍在原 BLE 手柄中切换。

## 源码与构建

- `main/car_control/car_core.h`：独立网页会话、序号、防重放、停车、BLE 接管和 UART 全局急停。
- `main/car_control/car_service.h/.cc`：互斥访问、硬件状态、随机会话、PWM 输出适配。
- `main/web_server/web_server.cc`：真实状态、使能、控制、急停、解除入口；旧运动端点返回 410，提示刷新页面。
- `phone/wifi.html`：可编辑页面；`tools/embed_wifi_page.py` 生成 `main/web_server/remote_page.h`。修改 HTML 后必须重新生成并编译。
- `main/boards/common/wifi_board.cc`：车控配置强制 Wi-Fi 性能模式，防止语音结束后省电产生超过看门狗的延迟。持续无线功耗会增加。
- `tests/car_core_test.cpp`：新增网页控制权、重放、超时、接管及 UART 急停回归。

本机入口是工程外 `work/build_avoidance.ps1`（包含重新生成网页步骤）和 `work/run_avoid_tests.cmd`。工具链 ESP-IDF 5.5.4、ESP32-S3，板型 qebabe-xiaoche，DIO/40MHz，16MB Flash。移至其他电脑后需按其工具链位置调整脚本中的路径。托管依赖随完整源码包提供；本机离线构建无下载。

## HTTP 协议

所有控制 POST 需要 `X-Car-Control: 1`，没有跨站 CORS 授权；部署在可信局域网。GET `/api/remote/status` 返回 ready/armed/estop/ble/web/mode/left/right，不公开当前会话。POST `/api/remote/arm` 空体，成功返回仅此次响应携带的 uint32 会话。已有 BLE、其他网页拥有者、自动模式、急停或硬件故障时拒绝。

POST `/api/remote/command` 为原协议的 20 字节小端帧，支持 DRIVE/STOP；CRC16-CCITT-FALSE 和序号检查不变。DRIVE 使用 x/y = -1000..1000、speed = 0..100、随机会话和递增序号。旧会话不影响新拥有者；当前会话重放使其停车。STOP 撤销会话。POST `/api/remote/estop` 空体，无条件锁定并停车；POST `/api/remote/clear` 空体，仅在没有 BLE 和活动控制者时清除，保持未使能。拒绝返回 409，非法长度/缺必要头返回 400。

## 验证边界

本机测试和 ESP32 目标编译的结果，以及目标设备实际烧录/HTTP 测试，以交付包根目录的《网页遥控修复与验证.md》及证据为准。主机和模拟浏览器的非零向量不属于电机实测；真实设备验收仅发送零向量，不替用户试车。没有验证 PWM 波形、轮向、带负载运动、机械停车距离、HC-SR04 实际距离、龙芯物理 UART 或自动避障。原 OLED 初始化失败也不属于本次网页修复的验收项目。
