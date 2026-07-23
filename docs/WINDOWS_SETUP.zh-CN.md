# Windows 11 构建、安装与联调指南

本文记录已经在真实 Windows 11 x64 主机上走通的完整流程，以及开发过程中已经确认的故障原因。
目标是让新的开发者不必重复证书、VHF、旧进程和防火墙方面的试错。

当前版本仍是开发原型：驱动使用测试签名，TCP 链路没有认证和加密。不要在日常主机上长期保持
测试模式，也不要把 TCP 39871 暴露到公网。

## 1. 已验证的软件环境

- Windows 11 x64
- Visual Studio 2026
- Windows SDK/WDK `10.0.28000`
- KMDF 1.33
- CMake（Visual Studio 的 C++ 工作负载包含）
- 管理员 PowerShell（仅安装、签名、驱动和防火墙操作需要）

按照 Microsoft 的
[下载并安装 WDK 官方指南](https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk)
安装 Visual Studio 2026。选择 **Desktop development with C++** 工作负载，并在
**Individual components** 中选择 Windows Driver Kit 和官方页面列出的 Spectre mitigated
组件。SDK 与 WDK 的 build number 必须一致；本项目当前使用 28000。

如果 Visual Studio 中没有驱动项目模板，通常是 WDK Visual Studio 扩展没有正确安装。回到
Visual Studio Installer，选择 **Modify → Individual components → Windows Driver Kit**。

## 2. 拉取源码并确认分支

```powershell
git clone https://github.com/fakebob2004/touchpad.git
cd touchpad
git switch agent/windows-vhf-touchpad
git pull --ff-only
git status --short --branch
```

在 PR 合并后，可直接使用默认分支。开始构建前，`git status` 不应显示意外的本地修改。

## 3. 构建 Receiver 和测试

在普通 Developer PowerShell 中运行，不需要管理员权限：

```powershell
cmake -S windows -B out\windows -A x64
cmake --build out\windows --config Release
ctest --test-dir out\windows -C Release --output-on-failure
```

应看到：

```text
100% tests passed
```

正式使用的 Receiver 是：

```text
out\windows\Release\mtp-receiver.exe
```

不要再运行早期手工构建的 `out\manual\mtp-receiver-diag2.exe`。旧程序曾经继续占用 39871，
造成代码已经更新、实际运行的却仍是旧版本。

## 4. 构建 KMDF/VHF 驱动

以下命令会自动查找 Visual Studio 2026 的 MSBuild。构建本身不需要管理员权限：

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.Component.MSBuild `
    -property installationPath
$msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'

& $msbuild .\windows\driver\MtpVhfTouchpad.vcxproj `
    /t:Build `
    /p:Configuration=Release `
    /p:Platform=x64 `
    /p:SignMode=Off `
    /p:InfVerif_Enable=false `
    /p:ApiValidator_Enable=false `
    /m
```

当前 28000 WDK 安装在构建期间可能打印 `InfVerif.dll` 加载警告；本机上 SYS、INF 和 CAT
仍可正常生成，随后用独立的 `Inf2Cat` 完成权威包验证。输出包位于：

```text
windows\driver\x64\Release\MtpVhfTouchpad\
```

其中应包含：

```text
MtpVhfTouchpad.sys
MtpVhfTouchpad.inf
mtpvhftouchpad.cat
```

## 5. 启用测试模式

从本节开始使用管理员 PowerShell。

根据 Microsoft 的
[测试签名模式说明](https://learn.microsoft.com/windows-hardware/drivers/install/the-testsigning-boot-configuration-option)，
测试模式需要管理员权限并在修改后重启。Secure Boot 会阻止启用该选项；修改固件设置前先确认
BitLocker 恢复密钥并按需暂停保护。

```powershell
Confirm-SecureBootUEFI
bcdedit /set testsigning on
```

重启后确认：

```powershell
bcdedit /enum '{current}' |
    Select-String -Pattern 'testsigning'
```

预期为 `testsigning Yes`。

## 6. 创建并信任测试证书

同一台开发机只需创建一次。不要因为签名失败反复创建同名证书；记录最终使用的 Thumbprint。

```powershell
$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject 'CN=MTP Touchpad Test' `
    -KeyAlgorithm RSA `
    -KeyLength 2048 `
    -HashAlgorithm SHA256 `
    -CertStoreLocation 'Cert:\LocalMachine\My' `
    -NotAfter (Get-Date).AddYears(5)

$cerPath = Join-Path $env:TEMP 'MtpTouchpadTest.cer'
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null
Import-Certificate -FilePath $cerPath `
    -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
Import-Certificate -FilePath $cerPath `
    -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' | Out-Null

$thumbprint = $cert.Thumbprint
$thumbprint
```

确认同一个证书同时存在于三个存储区：

```powershell
'My', 'Root', 'TrustedPublisher' | ForEach-Object {
    $store = $_
    Get-ChildItem "Cert:\LocalMachine\$store" |
        Where-Object Thumbprint -eq $thumbprint |
        Select-Object @{Name='Store';Expression={$store}}, Subject, Thumbprint
}
```

## 7. 按正确顺序生成并签署驱动包

关键顺序是：

```text
最终 SYS → 签 SYS → Inf2Cat → 签 CAT → 验证
```

`Inf2Cat` 之后不能再修改 INF 或 SYS，否则 CAT 中的哈希立即失效。

```powershell
$kit = '10.0.28000.0'
$kitRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
$signtool = Join-Path $kitRoot "bin\$kit\x64\signtool.exe"
$inf2cat = Join-Path $kitRoot "bin\$kit\x86\Inf2Cat.exe"
$pkg = (Resolve-Path `
    '.\windows\driver\x64\Release\MtpVhfTouchpad').Path

& $signtool sign /v /fd SHA256 /sm /s My `
    /sha1 $thumbprint "$pkg\MtpVhfTouchpad.sys"
if ($LASTEXITCODE -ne 0) { throw 'SYS signing failed' }

& $inf2cat "/driver:$pkg" /os:10_X64 /uselocaltime
if ($LASTEXITCODE -ne 0) { throw 'Inf2Cat failed' }

& $signtool sign /v /fd SHA256 /sm /s My `
    /sha1 $thumbprint "$pkg\mtpvhftouchpad.cat"
if ($LASTEXITCODE -ne 0) { throw 'CAT signing failed' }

& $signtool verify /v /pa "$pkg\MtpVhfTouchpad.sys"
& $signtool verify /v /pa "$pkg\mtpvhftouchpad.cat"
& $signtool verify /v /kp /c "$pkg\mtpvhftouchpad.cat" `
    "$pkg\MtpVhfTouchpad.sys"
```

三个验证都应成功。开发证书没有时间戳是预期现象。

## 8. 安装驱动

使用 WDK 28000 自带的 DevCon：

```powershell
$devcon = Join-Path $kitRoot "Tools\$kit\x64\devcon.exe"

& $devcon install "$pkg\MtpVhfTouchpad.inf" 'Root\MtpVhfTouchpad'
if ($LASTEXITCODE -ne 0) { throw 'Driver installation failed' }
```

检查 source device：

```powershell
$source = Get-PnpDevice |
    Where-Object FriendlyName -eq 'Remote Mac Precision Touchpad' |
    Select-Object -First 1

$source | Format-List Status, Problem, InstanceId
& $devcon stack "@$($source.InstanceId)"
```

健康结果必须同时满足：

- `Status: OK`
- `Problem: CM_PROB_NONE`
- `Controlling service: MtpVhfTouchpad`
- `Lower filters: vhf`

随后应出现由 VHF 枚举出的子设备：

```powershell
Get-PnpDevice -PresentOnly |
    Where-Object {
        $_.InstanceId -Like 'HID\HID_DEVICE_SYSTEM_VHF*' -or
        $_.InstanceId -Match 'VID_1209&PID_3987'
    } |
    Format-Table Status, Class, FriendlyName, InstanceId -AutoSize
```

Windows 通常把它显示为“符合 HID 标准的触摸板”或 `HID-compliant touch pad`。

## 9. 启动 Receiver 并准备局域网

在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass `
    -File .\windows\tools\prepare_receiver.ps1
```

脚本会请求 UAC，并完成：

- 选择当前有默认网关的 IPv4 网卡；
- 将网络配置为 Private；
- 创建只允许 `Private + LocalSubnet + TCP 39871` 的入站规则；
- 清理仓库内占用端口的旧 Receiver；
- 启动 `out\windows\Release\mtp-receiver.exe`；
- 打印 Windows IPv4 和准确的 Mac 启动命令。

不要手工把 39871 开放到 Public 网络或公网。

Windows 端应显示：

```text
listening on TCP 39871
```

不能显示：

```text
driver not present; parse/log mode
```

## 10. 合成输入验证

先用本机合成数据排除 Mac 和网络因素：

```powershell
python .\windows\tools\synthetic_touch.py `
    --host 127.0.0.1 `
    --port 39871 `
    --duration 5
```

预期 Windows 指针移动。客户端断开后 Receiver 应打印类似：

```text
driver status: submits=... last_ntstatus=0x0 report_bytes=50
active=0 input_mode=3 function=0x3 get_feature=... set_feature=...
```

其中：

- `submits > 0`
- `last_ntstatus=0x0`
- `report_bytes=50`
- `active=0`
- `input_mode=3`
- `function=0x3`

## 11. Mac 联调

在 Mac 上先测试 TCP：

```sh
nc -vz WINDOWS_IP 39871
```

再运行 Windows 准备脚本打印出的命令：

```sh
./build/mac-touch-agent WINDOWS_IP 39871
```

验收顺序：

1. 单指移动指针，且上下方向正确；
2. 双指滚动；
3. 双指缩放；
4. 三指和四指系统手势；
5. 带触点断开，Windows 在 200ms 内释放全部触点；
6. 只重启 Agent/Receiver，驱动无需重装且能够重新连接。

## 12. 已确认的试错结论

### `VhfCreate` 失败、设备代码 31

真正原因不是 PTP 描述符、Feature 回调、设备类或 VHF 调用时机，而是 INF 没有把系统
`vhf.sys` 加入 source device 的下层过滤器：

```ini
[MtpVhfTouchpad_Install.NT.HW]
AddReg=MtpVhfTouchpad_Vhf_AddReg

[MtpVhfTouchpad_Vhf_AddReg]
HKR,,"LowerFilters",0x00010000,"vhf"
```

只链接 `VhfKm.lib` 不会自动把 `vhf.sys` 放入设备栈。`devcon stack` 必须看到
`Lower filters: vhf`。

### 驱动正常、提交成功，但指针不动

已经确认两个关键字段：

- Confidence 必须置位；
- Scan Time 必须来自实际采集时间并使用 PTP 要求的 100µs 单位，不能长期发送无效常量。

当 `submits` 增长且 `last_ntstatus=0` 时，不要首先重装驱动；先检查 HID report 的语义。

### Mac 上下方向相反

真实 Mac 坐标与 Windows HID Y 方向相反，Receiver 使用：

```text
hid_y = clamp(1 - mac_y)
```

这是用户态映射，修复后只需重建并重启 Receiver，不需要重装驱动。

### 重启后突然失效

曾经是旧 `mtp-receiver-diag2.exe` 仍占用 39871，同时防火墙规则也绑定旧 EXE。结果是源码已经
更新，但 Mac 始终连到旧进程。现在 `prepare_receiver.ps1` 会使用唯一的 Release Receiver，
替换仓库内旧监听进程，并把防火墙规则绑定到同一文件。

### 设置 URI 只打开普通设置页

`ms-settings:devices-touchpad` 的页面行为不是可靠验收标准。以以下证据为准：

- VHF 子设备存在且没有 PnP problem；
- Windows 使用 `input.inf` 枚举 HID-compliant touch pad；
- Feature Report 显示 `input_mode=3`、`function=0x3`；
- 合成输入和真实 Mac 输入能够移动指针及触发手势。

### `ROOT\MTPVHFTOUCHPAD\*` 查不到设备

DevCon 创建后，实际 source device 实例可能显示为 `ROOT\HIDCLASS\0001`。应按 FriendlyName
或实际 InstanceId 查询，不要假定实例路径保持为硬件 ID 字符串。

### `sc query MtpVhfTouchpad` 初次显示 STOPPED/1077

这只表示服务尚未因 PnP 启动，不足以证明签名或 SYS 损坏。结合 PnP problem status、
`devcon stack` 和 `setupapi.dev.log` 判断。

### 第三方 HID Upper Filter

本机设备栈中出现过 `acsehidremap`。它没有造成这次 VHF 启动失败，也不应在没有证据时卸载。
排障应先确认本项目的 source device、`vhf` lower filter 和 report 状态。

## 13. 恢复日常安全配置

停止开发测试后，可以在管理员 PowerShell 中关闭测试模式：

```powershell
bcdedit /set testsigning off
```

重启后再按需要恢复 Secure Boot。不要在没有 BitLocker 恢复密钥的情况下修改固件安全设置。
