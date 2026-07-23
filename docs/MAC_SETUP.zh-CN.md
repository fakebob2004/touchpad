# macOS 安装与操作指南

[English](MAC_SETUP.md)

macOS 端目前是前台命令行 Agent：读取 MacBook 内建触控板的原始触点，并向 Windows
Receiver 发送完整的 MTP1 帧。它不会安装系统服务、注入事件，也不会拦截 macOS 本机输入。

## 1. 可选：让 macOS 忽略内建触控板

当外接鼠标存在时，直接使用 macOS 自带设置，不需要本项目拦截输入：

```text
系统设置
→ 辅助功能
→ 指针控制
→ 鼠标与触控板
→ 有鼠标或无线触控板时忽略内建触控板
```

如果希望外接鼠标继续控制 Mac，而 MacBook 内建触控板专门控制 Windows，请开启该选项。
只有在 macOS 实际检测到鼠标或无线触控板时，这个选项才会生效。

## 2. 构建

先安装 Xcode Command Line Tools，然后执行：

```sh
cd /path/to/touchpad
make clean
make
```

生成两个工具：

- `build/mac-capture-probe`：本地原始触点诊断工具；
- `build/mac-touch-agent`：MTP1 TCP 发送端。

## 3. 验证原始触点

```sh
./build/mac-capture-probe --duration 10 > touches.jsonl
python3 tools/analyze_capture.py touches.jsonl
```

采集期间依次使用单指、双指和五指移动。已验证机器的健康结果为：

```text
max_contacts: 5
estimated_hz: 约 125
duplicate_id_frames: []
invalid_json_lines: []
```

实时摘要中的 `overall_rate` 包含手指离开触控板的空闲时间，因此可能低于真实活动采样率。

## 4. 准备并连接 Windows

先启动 Windows Receiver，然后使用它当前的局域网 IPv4：

```sh
nc -vz WINDOWS_IP 39871
./build/mac-touch-agent WINDOWS_IP 39871
```

例如：

```sh
./build/mac-touch-agent 192.168.31.115 39871
```

正常启动输出：

```text
connected to WINDOWS_IP:39871
streaming 1 built-in trackpad(s) to WINDOWS_IP:39871
```

使用 `Ctrl-C` 停止。只要 Agent 仍在运行，断线后会自动尝试重连。

## 5. 验收

1. 单指移动，确认 Windows 指针的水平和垂直方向都正确。
2. 抬起手指，确认 Windows 指针立即停止。
3. 验证原生双指滚动。
4. 验证原生双指缩放。
5. 手指按住时停止 Agent，确认 Windows 在 200ms 内释放全部触点。
6. 重启 Windows Receiver 和 Mac Agent，确认新的 `HELLO`/`RESET` 会话正常建立。

## 常见问题

### `captured=0`

运行期间没有收到原始触点回调。请在 Agent 运行时实际触摸并移动内建触控板。重新运行
`mac-capture-probe`，可以区分触点采集问题和网络问题。

### Agent 一直没有输出 `connected`

- 先启动 Windows Receiver。
- 确认 Windows 网络类型为“专用”。
- 执行 `nc -vz WINDOWS_IP 39871`。
- 检查 Windows 的 Private/LocalSubnet 防火墙规则。

### Mac 仍然响应内建触控板

Agent 有意不拦截本机事件。请连接外接鼠标，并开启第 1 节中的 macOS 系统设置。如果 macOS
没有检测到外接指针设备，该设置不会禁用内建触控板。

### macOS 更新后无法采集

项目依赖私有 `MultitouchSupport.framework` 符号和推断出的触点结构。请先运行探针，并报告
macOS 版本、Mac 型号、启动诊断和采集分析结果。不要在没有验证的情况下静默修改 ABI 布局。

## 安全说明

MTP1 当前使用未认证、未加密的 TCP。请仅在可信的私有局域网中使用，不要把 39871 端口暴露
到公网。
