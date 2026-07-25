# <img src="https://github.com/matth-x/MicroOcpp/assets/63792403/1c49d1ad-7afc-48d3-a54e-9aef2d4886db" alt="Icon" height="24"> &nbsp; MicroOCPP

[![Build Status]( https://github.com/matth-x/MicroOcpp/workflows/PlatformIO%20CI/badge.svg)](https://github.com/matth-x/MicroOcpp/actions)
[![Unit tests]( https://github.com/matth-x/MicroOcpp/workflows/Unit%20tests/badge.svg)](https://github.com/matth-x/MicroOcpp/actions)
[![codecov](https://codecov.io/github/matth-x/ArduinoOcpp/branch/develop/graph/badge.svg?token=UN6LO96HM7)](https://codecov.io/github/matth-x/ArduinoOcpp)

面向微控制器的 OCPP 1.6 / 2.0.1 客户端。可移植 C/C++，兼容 Espressif、Arduino、NXP、STM、Linux 等平台。

:heavy_check_mark: 已对接 [15+ 家商用中心系统](https://www.micro-ocpp.com/#h.314525e8447cc93c_81)

:heavy_check_mark: 可用于公共充电桩（符合 Eichrecht）

:heavy_check_mark: 支持全部 OCPP 1.6 功能配置文件，以及 [基础 OCPP 2.0.1 UC](https://github.com/matth-x/MicroOcpp/tree/feature/prepare-release?tab=readme-ov-file#ocpp-201-and-iso-15118)

参考集成：[OpenEVSE](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/blob/master/src/ocpp.cpp) | 技术介绍：[文档](https://matth-x.github.io/MicroOcpp/intro-tech) | 网站：[www.micro-ocpp.com](https://www.micro-ocpp.com)

> 英文版说明见 [README.en.md](README.en.md)

## 对 AI 友好的代码

AI 模型在 MicroOCPP 代码库上表现很好。即将发布的 MO v2.0 会进一步优化代码，使 AI 集成结果更稳定（预览见 `develop/remodel-api` 分支）。目标是：只需少量提问，就能把 MO 接入现有充电桩工程。

目前 `develop/remodel-api` 尚未稳定，但推荐用于新开发。入门方式：把 `MicroOcpp.h`（现已统一 C/C++）放入上下文，询问模型为接入你的代码库还需要哪些信息。

如果工具在 MicroOCPP 上遇到问题，欢迎在 GitHub 提 Issue。任何有助于进一步优化代码库的反馈都很欢迎。

## 测试 / 演示应用

*主仓库：[MicroOcppSimulator](https://github.com/matth-x/MicroOcppSimulator)*

Simulator 是 MicroOCPP 的演示与开发工具，可快速验证与不同 OCPP 后端的兼容性。它会模拟完整充电站，并为 MicroOCPP 提供 GUI 与模拟硬件绑定，可在浏览器中运行（WebAssembly）：[在线试用](https://demo.micro-ocpp.com/)

本仓库也提供桌面示例：

- `examples/pc`：最小 PC（Linux/WSL）示例
- `examples/pc_web`：带 Web 仪表盘的 PC 示例（`webapp/` 模块）

<div align="center"><img src="https://github.com/matth-x/MicroOcpp/assets/63792403/27f2819b-41fd-41a7-88a8-9e673b8a88b8" alt="Screenshot" width="800em" href="https://demo.micro-ocpp.com/"></div>

#### 使用方法

**配置 OCPP 服务器**：进入 “Control Center”。在 WebSocket 选项中填写 OCPP 后端 URL、充电桩 ID，以及（如有）鉴权密钥，点击 “Update WebSocket” 保存。Simulator 应能连接到 OCPP 服务器。如需排查连接状态，可打开浏览器开发者工具。

若手头没有 OCPP 服务器，可将充电桩 ID 留空，后端地址填写：`wss://echo.websocket.events/`（该服务器由 Lob.com 赞助）

**RFID 鉴权**：进入 “Control Center” > “Connectors” > “Transaction”，将 idTag 改为目标值。

## 性能基准

*完整报告：[MicroOCPP benchmarks](https://matth-x.github.io/MicroOcpp/benchmarks/)*

以下数据在 ESP32 @ 160MHz 上测得，对双物理枪头充电桩属于较乐观的最佳情况（例如使用 `-Os` 编译，并关闭调试输出与日志）。

| 项目 | 数值 |
| :--- | ---: |
| Flash 体积（最小） | 121,170 B |
| 堆占用（空闲） | 12,308 B |
| 堆占用（峰值） | 21,916 B |
| 初始化 | 21 ms |
| `loop()` 调用（空闲） | 0.05 ms |
| 发送较大报文 | 5 ms |

实际工程中，执行时间很大程度上取决于 IO 延迟；堆占用也会明显受预约、本地鉴权、充电配置文件列表等配置影响。

## 开发者指南

PlatformIO 包：[MicroOcpp](https://registry.platformio.org/libraries/matth-x/MicroOcpp)

MicroOCPP 实现 OCPP 通信行为：在硬件状态变化或 RFID 输入更新时，自动发起对应 OCPP 操作；同时处理来自服务器的新数据，本地持久化，并在适用时更新硬件控制。

建议以 `examples/ESP/main.cpp` 作为第一个工程起点。它是一个最小示例，演示如何建立 OCPP 连接，以及如何启动/停止充电会话。API 文档见 [`MicroOcpp.h`](https://github.com/matth-x/MicroOcpp/blob/main/src/MicroOcpp.h)。也可参阅[文档站点](https://matth-x.github.io/MicroOcpp)。

PC 示例（本仓库）：

```bash
# 最小 PC 示例
cd examples/pc
cmake -S . -B ./build && cmake --build ./build -j --target mo_pc_example
./build/mo_pc_example

# 带 Web 仪表盘
cd examples/pc_web
cmake -S . -B ./build && cmake --build ./build -j --target mo_pc_web
./build/mo_pc_web
# 浏览器打开 http://localhost:8000
```

单元测试默认关闭，需要时：

```bash
cmake -S . -B ./build -DMO_BUILD_UNIT_TESTS=ON
```

### 依赖

必需：

- [bblanchon/ArduinoJSON](https://github.com/bblanchon/ArduinoJson)（版本 `6.21`）

若启用 Arduino 集成：

- [Links2004/arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets)（版本 `2.4.1`）

若使用内置证书存储（启用编译宏 `MO_ENABLE_MBEDTLS=1`）：

- [Mbed-TLS/mbedtls](https://github.com/Mbed-TLS/mbedtls)（版本 `2.28.1`）

若使用 PlatformIO，可将 `platformio.ini` 中的依赖复制到自己的配置；也可在 PIO 库管理器中添加 `matth-x/MicroOcpp@1.2.0` 安装完整库及依赖。

## OCPP 2.0.1 与 ISO 15118

已实现以下 OCPP 2.0.1 用例：

| UC | 说明 | 备注 |
| :--- | :--- | :--- |
| B01 - B04<br>B11 - B12 | 开通配置（Provisioning） | 由 OCPP 1.6 移植 |
| B05 - B07 | 变量（Variables） | |
| C01 - C06 | 鉴权选项 | |
| C15 | 离线鉴权 | |
| E01 - E12 | 交易（Transactions） | |
| F01 - F03<br>F05 - F06 | 远程控制 | |
| G01 - G04 | 可用性 | |
| J02 | 交易相关 MeterValues | 尚不支持持久化 |
| M03 - M05 | 证书管理 | 启用 Mbed-TLS 以使用内置证书存储 |
| P01 - P02 | 数据传输 | |
| - | 协议协商 | 充电桩可在运行时选择 OCPP 版本 |

OCPP 2.0.1 功能仍处于 alpha 阶段。默认关闭且不参与编译，因此不影响固件体积。启用方式：设置编译宏 `MO_ENABLE_V201=1`，并以 ProtocolVersion 参数 `2.0.1` 初始化库（参见 Simulator 中的[示例](https://github.com/matth-x/MicroOcppSimulator/blob/657e606c3b178d3add242935d413c72624130ff3/src/main.cpp#L43-L47)）。

按 OCPP 1.6 集成的工程，升级到 2.0.1 后通常仍可运行，使用的仍是 `MicroOcpp.h` 中的同一套 API。

ISO 15118 定义了部分包含充电桩与服务器报文交互的用例。本库通过处理其 OCPP 侧通信，方便接入 ISO 15118。

## 联系方式

如有问题或发现潜在缺陷，欢迎提交 Issue。这类反馈很有价值，能帮助暴露代码问题并提升项目清晰度。不便公开讨论的问题，可通过 LinkedIn 或以下邮箱联系：

:envelope: : matthias [A⊤] micro-ocpp [DО⊤] com
