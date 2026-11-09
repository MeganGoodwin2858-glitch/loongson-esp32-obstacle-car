# 依赖与第三方资料

## ESP-IDF 组件

已有工程通过 `main/idf_component.yml` 声明依赖，通过 `dependencies.lock` 记录解析版本。恢复时发现实际 `managed_components/` 约 604MB，包含 55 个带文件校验清单的组件。

本次按各自 `CHECKSUMS.json` 逐项校验 8,304 个文件，结果一致。离线依赖 ZIP 从该目录生成，保留组件清单、资源和许可证。依赖自身的授权条款以各组件文件为准。

Git 忽略 `managed_components/`，克隆后按锁文件联网恢复，或使用独立依赖 ZIP。离线包的角色是保留本次实际使用的依赖，ESP-IDF、编译器、CMake、Ninja、Visual Studio、Node.js 和 Python 安装由各开发电脑准备。

## 离线包核对与解压

依赖包文件名为 `loongson-esp32-dependencies.zip`，大小为 293,993,121 字节，SHA256 为 `b21228660f892a34cb360aecff2a8155acc333a85b3f177c1cfbb6d876cbd6fe`。包内有 8,414 个文件，其中 8,304 项列入组件自己的校验清单。

将依赖 ZIP 放到工程根目录，在 PowerShell 执行。

```powershell
Get-FileHash -LiteralPath .\loongson-esp32-dependencies.zip -Algorithm SHA256
Expand-Archive -LiteralPath .\loongson-esp32-dependencies.zip -DestinationPath .
```

核对输出哈希后解压，目录应为根目录下的 `managed_components/`。随后按 [构建说明](build-and-test.md)配置并编译。解压时若发现同名文件，先核对现有组件与锁文件版本，再确定使用哪份依赖。

## 已保留授权信息

- 根目录 `LICENSE` 来自上传工程，内容为 MIT License，并保留原著作权声明。
- `main/third_party/` 及源码中的原有授权声明随文件保留。
- 设备树片段保留各自文件中的 SPDX/版权信息。
- 供应商原理图和手册的再分发范围需要依据原资料授权处理，索引记录用户实际提供的文件。

## 供应商资料索引

用户提供过 `原理图.zip`、`固件.zip`、`内核.zip`、`用户手册.zip`、`芯片手册.zip`、`常用工具软件.zip`。已有交付资料记录了它们的大小与 SHA256，本次恢复核对相关文件实际存在，并提取既有目录索引供团队查找。

`docs/supplier-archive-index.json` 标明 archive member、文件大小、输入哈希和设备树核对结果。其哈希注明来自已有记录，区别于本次重新计算的原工程 RAR 哈希。

## 本次包的内容

源码 ZIP 包含完整应用、必要构建文件、资源、协议、测试和团队文档。依赖 ZIP 另行提供托管组件目录。两者通过交付校验文件关联。

历史二进制固件、全片恢复镜像、NVS、配网数据和本机工具安装路径分别保留在原工作环境。团队固件应由当前源码、配置与依赖生成，并在发布时附上源码提交号、构建环境和哈希。
