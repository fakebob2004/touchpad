# macOS 端开发记录

[English](MAC_DEVELOPMENT.md)

macOS 端代码有意保持精简。手势识别和原生精确式触控板协议由 Windows 负责；Mac 只有三个
职责：忠实采集原始触点、编码完整 MTP1 帧、在重连和异常情况下安全地传递当前状态。

## 1. 最初真正的技术风险

项目能否成立，取决于 macOS 能不能提供稳定的原始触点，而不只是滚动和手势事件。公开的
`NSTouch` 可以在应用事件路径中提供标准化坐标，但不能预先假定它满足全局、后台和完整几何
数据需求。

因此第一步探针动态加载 Apple 私有框架：

```text
/System/Library/PrivateFrameworks/MultitouchSupport.framework
```

程序通过 `dlopen`/`dlsym` 解析所需符号，不直接链接未公开的 SDK 接口。所有推断出的结构和
函数类型集中隔离在 `mac/Probe/MultitouchSupportABI.h`，并对 `MTTouch` 大小进行编译期断言。

## 2. 探针验证了什么

真实采集验证结果：

- 在测试的 Apple Silicon MacBook 上可以找到一个内建触控设备；
- 支持五指同时接触；
- Contact ID 在一次触点生命周期内保持稳定；
- 活动状态下中位帧间隔为 8ms，约 125Hz；
- 坐标、速度、面积、椭圆轴、角度和密度字段都有有效数据；
- 私有符号 `MTRegisterButtonStateCallback` 可用，可直接报告物理按下/松开，而无需用触点
  density 猜测压力；
- 已记录测试中没有非法帧和重复 ID。

探针把 JSON Lines 写到 stdout，把诊断写到 stderr。私有框架最初会向 stdout 输出一行硬件
信息，污染采集文件；后来把启动阶段输出重定向，保证 JSONL 可以直接分析。

## 3. 为什么 Mac 不识别手势

如果在 macOS 识别滚动、缩放和多指手势，项目就会退化成远程鼠标模拟，并失去 Windows 原生
设置。MTP1 因此发送完整的当前触点集合：

```text
identifier + state + flags + x/y + geometry + button state + monotonic timestamp
```

Windows 将触点映射到 HID slot，并交给精确式触控板栈识别手势。因此双指滚动和双指缩放能
原生工作，而不需要 Mac 端手势代码。

物理单击同样不在 Mac 端识别为手势。Agent 将私有框架的按键变化写入 MTP1 帧级 `BUTTON`
位，并在按下和松开时立即使用最近一次完整触点快照发送一帧，避免松开晚于最后一个触点帧时
让 Windows 遗留“按住”状态。

## 4. 实时性与重连

私有框架回调不能阻塞在 TCP 上。`mac-touch-agent` 在回调路径编码数据，写入 256 帧有界队列，
由独立发送线程配合 `TCP_NODELAY` 执行 Socket 写入。

每次连接严格从以下序列开始：

```text
HELLO(sequence=N)
RESET(sequence=N+1)
FRAME(sequence=N+2)
```

早期原型曾让控制消息复用 sequence，严格的 Windows Receiver 在重启后正确拒绝了该数据流。
修复后，每条消息都会递增 sequence。队列溢出时不会带着非法缺口继续发送，而是重新连接并
建立新的 `HELLO`/`RESET` 会话。

## 5. 坐标方向

网络协议保留 Mac 原始标准化坐标。真实端到端测试发现，Mac Y 轴与 Windows 精确式触控板表面
方向相反，因此在 Windows 映射层修正：

```text
hid_y = 1 - mac_y
```

不在网络协议中反转坐标，可以保留原始传感器含义，也允许未来其他 Receiver 自行选择方向。

## 6. macOS 本机输入策略

开发过程中考虑过使用 CGEventTap 拦截本机输入，但最终放弃：

- 需要“辅助功能”和“输入监控”权限；
- 无法可靠区分内建触控板与外接鼠标；
- 全局拦截会增加不必要的安全和可用性风险。

最终 Agent 不拦截 macOS 输入。需要把内建触控板专用于 Windows 的用户，使用 macOS 自带的
“有鼠标或无线触控板时忽略内建触控板”设置。

## 7. 为什么 Mac 端代码看起来不多

这是有意的职责收敛：

- 不实现手势识别器；
- 不在 macOS 创建虚拟设备或内核扩展；
- 暂时没有 UI、配对、安装器和后台守护进程；
- 不尝试普通 USB-C Device Mode；
- 复用共享协议编码器，不创建第二套序列化实现。

困难部分是验证私有传感器路径和建立异常安全边界，而不是写一个庞大的应用。Mac 端越小，
未来 macOS 私有 ABI 发生变化时需要维护的范围越小。

## 后续 macOS 工作

- 测试更多 Apple Silicon 代际和 macOS 版本；
- 封装签名的菜单栏/后台 Agent；
- 增加发现、配对、认证和加密；
- 处理睡眠唤醒和网络接口变化；
- 研究功能受限的公开 API 后端；
- 在跨机型测量后增加校准过的设备元数据。
