# GitHub 交付记录

完整工程已上传到 [MeganGoodwin2858-glitch/loongson-esp32-obstacle-car](https://github.com/MeganGoodwin2858-glitch/loongson-esp32-obstacle-car) 的 `main` 分支，当前仓库可见性为公开。

## 上传与核对

2026-11-09，使用 `MeganGoodwin2858-glitch` 的标准 Git 登录完成同步。首次完整核对基线为提交 `5f4ec4da1516c3fe8224dd9f022b3bf9819f93b3`。

重新从 GitHub 获取 `main`，核对服务器公布的分支提交，并逐一比较远端文件树与本地交付目录。1,034 个文件的 Git 内容哈希全部一致，缺失、额外和内容差异均为 0。随后更新交付状态文档，最终提交与核对结果记录在下载包外部的 `delivery-checksums.json` 中。

## 交付内容

仓库包含完整 ESP32 应用、龙芯客户端、BLE/Wi-Fi 页面、已有测试、必要资源及团队文档。源码 ZIP 与仓库对应，离线托管依赖 ZIP 单独提供，文件名与 SHA256 见 [依赖说明](dependencies.md)。

GitHub 应用安装范围当前为所有仓库。源码提交使用已核对身份的 Git 登录。

## 验证范围

上传校验确认文件内容和版本。软件测试与目标编译证据见 [验证报告](verification.md)，执行日期为 2026-11-06。OLED、距离标定、轮向、机械停车与实车自动避障按 [待办](../TODO.md)继续验收。
