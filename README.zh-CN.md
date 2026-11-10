<p align="left">
    <img src="src/Resources/icons/pinpointstudio_1024.png" width="150" />
  </p>

[English](README.md) · [Français](README.fr.md) · [Deutsch](README.de.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · **简体中文**

> 说明：本文件是 README.md 的译文；所链接的文档目前仅有英文版。

# PinPoint Studio

PinPoint Studio 是一款免费、开源、跨平台的高尔夫挥杆分析桌面应用。它将高速相机、手机、可穿戴传感器和弹道测量仪（launch monitor）整合在一起，在本机上用计算机视觉测量挥杆，并解释分析得出的结论——除非你自己选择，否则你的数据不会被发送到任何地方。

它面向球手、教练和研究人员，可在 Windows、macOS 和 Linux 上运行。

> **状态**：测试版（v0.1）。采集、分析、回看和诊断已可端到端运行；地面反作用力和 AI 教练两种训练仍在开发中。

<img width="2560" height="1440" alt="beta3-screenshot" src="https://github.com/user-attachments/assets/c060ba3b-2927-4380-8c58-a34cc4c424a2" />

## 主要功能

- **多相机采集**——支持 USB 摄像头、GenICam 工业相机（Aravis）和 Teledyne FLIR 高速相机（Spinnaker），并配有专门拍摄触球瞬间的特写相机。
- **手机当相机**——运行 PinPoint Capture 的 iPhone 可通过 WiFi 或 USB 线加入训练，连接全程加密。
- **无标记挥杆追踪**——全身姿态估计（ViTPose），无需贴胶带或标记点即可追踪杆身和杆头，并可检测高尔夫球。
- **自动击球检测**——通过 IMU 检测到的触球冲击、触球声音或弹道测量仪识别击球，每一杆都会在打出的同时完成分析并保存。
- **挥杆指标**——十个 P-positions（从站位到收杆）、节奏、触球时的球杆状态（club delivery）、身体旋转、身体姿势、手腕角度和运动链顺序（kinematic sequence），每项指标都标明其可信程度。
- **诊断**——对照常模范围指出挥杆中的问题和优点，并给出可能的原因和对应的练习方法，既针对每一杆，也针对整次训练。
- **手腕分析**——使用 Witmotion IMU 或 HackMotion wG3 传感器，测量挥杆全程中前导手腕（lead wrist）的屈曲、伸展和立腕。
- **弹道测量仪**——支持 Foresight GCQuad（通过 FSX2020），以及任何带有 GSPro Open Connect 桥接的设备（Garmin R10、Rapsodo MLM2PRO、SkyTrak+ 等）。读数会与产生它的那次挥杆配对。
- **回看**——所有相机画面同步回放，并带有叠加标注、图表和阶段时间轴；另有挥杆资料库，将每次训练都保存在磁盘上。
- **真值标注**——在应用内以帧级精度标注 P-positions，用于验证和调校分析算法。
- **语音**——本地语音转文字（whisper.cpp）和文字转语音（Kokoro），并可选用云端服务作为后备。
- **本地优先，保护隐私**——分析在本机运行，并在可用时使用 GPU 加速（CUDA、Vulkan、CoreML、Metal）。没有遥测；除非你自行配置，否则云端服务处于关闭状态。
- **应用内更新**——Windows 和 macOS 提供已签名的安装包，可就地自行更新。可自行更新的 Linux AppImage 正在开发中；在此之前，Linux 需从源码构建。

## 获取方式

安装包发布在 [Releases 页面](https://github.com/PinPoint-Golf/PinPointStudio/releases)。如需从源码构建，请参阅 **[BUILDING.md](BUILDING.md)**。

## 仓库结构

| 路径 | 内容 |
|---|---|
| `src/` | 应用程序本体，每个子系统一个文件夹（见下文） |
| `tests/` | 总测试构建。各测试套件与对应代码放在一起，位于 `src/<area>/tests/` |
| `tools/` | 离线工具和实验工具：SwingLab（在已录制的挥杆上重放分析管线）、杆身/球/触球实验工具、弹道测量仪模拟程序、打包脚本 |
| `packaging/` | Windows 安装包和 appcast 脚本、macOS 打包资源 |
| `cmake/` | 构建模块：Qt 查找、QML 模块、字体、OpenSSL、代码签名 |
| `third_party/` | 随仓库附带的少量第三方源码（ESKF 姿态滤波器） |
| `docs/` | 文档，按读者对象组织（见下文） |

`src/` 内部：

| 文件夹 | 子系统 |
|---|---|
| `Gui/` | Qt Quick / QML 界面及其 C++ 模型 |
| `Video/`, `Pose/` | 相机后端；姿态、球和人体分割模型 |
| `Analysis/`, `Metrics/`, `Diagnostics/` | 挥杆分析管线、指标目录、挥杆问题诊断 |
| `Buffer/` | 无锁事件缓冲区，负责对所有传感器的数据做时间对齐 |
| `IMU/`, `LaunchMonitor/`, `Ppcp/`, `Audio/` | 传感器、弹道测量仪、手机采集、麦克风和击球声 |
| `Export/` | 挥杆视频和 `swing.json` 文档 |
| `STT/`, `TTS/`, `LLM/`, `Secrets/` | 语音、AI 教练、API 密钥存储 |
| `Update/`, `Core/`, `Resources/`, `Shaders/` | 应用内更新、公共基础设施、内置资源、GPU 着色器 |

## 文档

| 位置 | 内容 |
|---|---|
| [BUILDING.md](BUILDING.md) | 依赖项、各平台的构建方法、打包、运行测试 |
| [`docs/user/`](docs/user) | 使用应用：[诊断指南](docs/user/pinpoint-diagnostics-guide.md)、[手腕校准](docs/user/wristcalibration.md)、[手机采集](docs/user/phone_capture.md)、UX 设计 |
| [`docs/developer/`](docs/developer) | 开发者指南：[分析管线](docs/developer/analysis_pipeline_developer_guide.md)、[测试](docs/developer/testing_developer_guide.md)、[SwingLab](docs/developer/swinglab_developer_guide.md)、[功能开关](docs/developer/feature_switches_developer_guide.md)、[翻译](docs/developer/translations_developer_guide.md)、事件缓冲区、诊断、指标 |
| [`docs/design/`](docs/design) | 各子系统的设计记录，以及 [QML 设计系统](docs/design/pinpoint_qml_design_system.md) |
| [`docs/reference/`](docs/reference) | 文件格式（[`swing.json`](docs/reference/swing_json_schema.md)、[挥杆文件夹](docs/reference/swing_folder_layout.md)）、正负号约定、设备协议、常模数据 |
| [`docs/validation/`](docs/validation), [`docs/research/`](docs/research) | 验证方案、样本库结果和研究笔记 |
| `docs/implementation/` | 工作计划和发布操作手册 |

## 相关项目

以下姊妹仓库作为依赖项使用：

- [libppcp](https://github.com/PinPoint-Golf/libppcp)——PinPoint Capture Protocol 协议，与手机应用共用
- [PinPoint Capture](https://github.com/PinPoint-Golf/PinPointCapture)——iPhone 采集应用
- [libwrist](https://github.com/PinPoint-Golf/libwrist)——HackMotion wG3 传感器驱动
- [libgspro](https://github.com/PinPoint-Golf/libgspro)——GSPro Open Connect 服务端

## 许可证

PinPoint Studio 依据 GNU General Public License 第 2 版或更高版本授权。由于所链接组件的缘故，分发的二进制文件依据 GPL-3.0 提供。请参阅 [LICENSE](LICENSE)；各第三方组件及其条款见 [LICENSEDEPS.md](LICENSEDEPS.md)。
