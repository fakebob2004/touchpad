# MacBook 触控板桥接

[English](README.md) · [Windows 交接文档](docs/WINDOWS_HANDOFF.md) · [许可证](LICENSE)

[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![macOS](https://img.shields.io/badge/macOS-Apple%20Silicon-black.svg)](#环境要求)
[![Windows](https://img.shields.io/badge/Windows-11%20x64-0078D4.svg)](#环境要求)

## 为什么？

很多程序员已经习惯双手分工：

- 右手使用鼠标，负责精确指向、选择、绘图或 CAD；
- 左手负责滚动论文、文档、浏览器和代码，并进行双指缩放。

Windows 上优秀的独立触控板并不多，但很多人的主机旁边其实已经放着一台 MacBook。
与其再买一块触控板，不如直接把 MacBook 的触控板变成 Windows 触控板。

```text
┌─────────────────────────────┐
│          Windows PC         │
└─────────────────────────────┘

           ⌨ 键盘

MacBook 触控板             鼠标
    左手                  右手
  滚动 / 缩放            精确操作
```

> **不用再买 Windows 触控板，把你已有的 MacBook 利用起来。**

macOS 通过可信的有线局域网发送原始触点；Windows 使用 KMDF 和 Virtual HID Framework
（VHF）将其呈现为原生触控板。滚动、双指缩放和系统手势由 Windows 触控板栈处理，而不是
模拟鼠标事件。

> **实验性项目：** 当前端到端原型已经能使用真实 Mac 输入移动 Windows 指针，并支持原生
> 双指滚动和双指缩放。现阶段仍需要 Windows 测试签名驱动和 macOS 私有框架，不适合无人值守
> 或高安全要求的生产部署。

## 当前状态

| 功能 | 状态 |
|---|---|
| MacBook 内建触控板原始采集 | 已验证，5 触点、约 125 Hz |
| 基于 TCP 的版本化 MTP1 协议 | 已实现 |
| 重连、复位、超时和触点释放 | 已实现 |
| Windows VHF 精确式触控板枚举 | 已在 Windows 11 x64 验证 |
| 指针移动 | 已使用真实 Mac 输入验证 |
| 原生双指滚动 | 已验证 |
| 原生双指缩放 | 已验证 |
| 物理按压/单击（Precision Touchpad Button 1） | 已按测试 Mac 校准压力回退，等待 Windows 拖动验证 |
| 三指和四指手势 | 等待更多真实设备验证 |
| 配对、认证和加密 | 尚未实现 |
| 正式驱动签名和安装器 | 尚未实现 |

## 架构

```text
MacBook 内建触控板
    -> MultitouchSupport.framework
    -> mac-touch-agent
    -> MTP1 / TCP 39871
    -> Windows Receiver
    -> 固定大小 IOCTL
    -> KMDF + VHF
    -> Windows 精确式触控板栈
```

Mac 发送完整的原始触点帧，而不是手势。网络逻辑完全位于用户态；Windows 驱动只接受有界、
验证后的固定结构，并通过 VHF 提交最多五触点的精确式触控板报告。

## 环境要求

### macOS

- 带内建触控板的 Apple Silicon MacBook
- Xcode Command Line Tools
- 当前原型已在现代 macOS 上验证

采集层会动态加载 Apple 私有的 `MultitouchSupport.framework`。其 ABI 可能随 macOS 更新而
变化，使用该框架的软件不适合通过 Mac App Store 分发。

### Windows

- Windows 11 x64
- Visual Studio 2026 和 C++ 桌面开发工具
- Windows SDK/WDK 10.0.28000
- 管理员权限
- 当前驱动包需要测试签名模式

## 快速开始

### 1. 构建并验证 macOS 采集

```sh
make clean
make
./build/mac-capture-probe --duration 10 > touches.jsonl
python3 tools/analyze_capture.py touches.jsonl
```

完整说明见 [macOS 安装与操作指南](docs/MAC_SETUP.zh-CN.md) 和
[macOS 端开发记录](docs/MAC_DEVELOPMENT.zh-CN.md)。

### 2. 准备 Windows

先构建并安装 KMDF/VHF 驱动和 Receiver。当前启动与重启流程见：

- [Windows 完整构建、安装与联调指南](docs/WINDOWS_SETUP.zh-CN.md)
- [Windows 重启与联调交接](docs/WINDOWS_RESTART_HANDOFF.md)
- [VHF 启动问题及解决方案](docs/VHF_STARTUP_ISSUE.md)

在仓库根目录使用管理员 PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\tools\prepare_receiver.ps1
```

脚本会配置仅限 Private/LocalSubnet 的防火墙规则、启动 Receiver，并打印 Mac 端应执行的命令。

### 3. 发送触控数据

```sh
nc -vz WINDOWS_IP 39871
./build/mac-touch-agent WINDOWS_IP 39871
```

使用 `Ctrl-C` 停止。连接断开或活跃触点超过 200ms 没有更新时，Windows 会释放全部触点。

Agent 不拦截 macOS 本机输入。如果希望外接鼠标控制 Mac、内建触控板专门控制 Windows，请开启：

```text
系统设置 → 辅助功能 → 指针控制 → 鼠标与触控板
→ 有鼠标或无线触控板时忽略内建触控板
```

## 仓库结构

```text
mac/            原始触点探针和 TCP Agent
protocol/       权威 MTP1 编解码实现
windows/driver/ KMDF VHF 源驱动
windows/receiver/
                TCP 解析、触点映射和驱动 IOCTL 客户端
windows/tests/  解析器和会话测试
windows/tools/  Windows 准备与合成输入工具
tools/          macOS 采集分析器和参考接收器
docs/           协议、联调和启动记录
```

## 协议与安全属性

MTP1 使用 36 字节大端序消息头和最多十条 44 字节触点记录。每个 TCP 会话依次发送
`HELLO`、`RESET` 和严格递增的 `FRAME`。Windows Receiver 会拒绝非法长度、重复 ID、
非有限数值、序列不连续和过大的触点集合。

字节级协议见 [Windows Receiver 交接文档](docs/WINDOWS_HANDOFF.md)。

当前原型在可信局域网中以明文方式监听，不包含认证和加密。不要将 TCP 39871 暴露到公网。

## 开发

在 macOS 运行协议测试：

```sh
make test
```

在 Windows Developer PowerShell 运行 Receiver 测试：

```powershell
cmake -S windows -B out\windows -A x64
cmake --build out\windows --config Release
ctest --test-dir out\windows -C Release --output-on-failure
```

贡献代码时应保持网络逻辑位于用户态、保持完整帧语义，并为协议或触点生命周期变化增加测试。
详见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 开发方式

本原型从 macOS 可行性探针、MTP1 协议、Windows VHF 驱动到文档，**全程采用 GPT-5.6 Sol
（Light）进行 vibe coding**，大约使用了一个周额度的 60%。

主要开发分耗时约三小时。Windows 驱动曾在设备管理器中出现黄色感叹号；排查
`VhfCreate` 启动失败和缺失的 `vhf` lower filter 时，短暂让网页端高思考模式介入协助定位。

真实设备操作、安装、风险决策和端到端验收由项目作者完成。AI 生成的代码在合并前均在实际
Mac 和 Windows 机器上完成编译与测试。

## 路线图

- 在更多 MacBook 机型上验证三指和四指手势
- 将 Windows Receiver 封装为系统服务
- 增加设备配对、认证和加密传输
- 增加正式签名与安装器
- 验证睡眠唤醒、网络切换和长时间恢复
- 为 macOS 增加功能受限但稳定的公开 API 采集后端

## 许可证

项目采用 [Apache License 2.0](LICENSE)。该宽松许可证允许商业和非商业使用，并包含明确的
贡献者专利授权。

部分 Windows 精确式触控板报告语义参考并改编自
`imbushuo/mac-precision-touchpad` 中采用 MIT 许可证的 SPI 部分。GPL 实现仅用于架构研究，
没有包含在本仓库中。详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和
[开源复用评估](docs/OPEN_SOURCE_REUSE.md)。

Apple、MacBook、macOS、Microsoft、Windows 和 Precision Touchpad 是各自权利人的商标。
本项目为独立项目，未获得 Apple 或 Microsoft 的认可或背书。
