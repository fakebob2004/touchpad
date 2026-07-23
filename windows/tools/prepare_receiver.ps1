[CmdletBinding()]
param(
    [int]$Port = 39871
)

$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $arguments = @(
        '-NoProfile'
        '-ExecutionPolicy', 'Bypass'
        '-File', "`"$PSCommandPath`""
        '-Port', $Port
    )
    Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments
    exit
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$receiver = Join-Path $repoRoot 'out\manual\mtp-receiver-diag2.exe'
if (-not (Test-Path -LiteralPath $receiver)) {
    throw "Receiver not found: $receiver"
}

$configuration = Get-NetIPConfiguration |
    Where-Object {
        $_.IPv4DefaultGateway -and
        $_.NetAdapter.Status -eq 'Up' -and
        $_.IPv4Address
    } |
    Select-Object -First 1

if (-not $configuration) {
    throw 'No active IPv4 adapter with a default gateway was found.'
}

$profile = Get-NetConnectionProfile -InterfaceIndex $configuration.InterfaceIndex
if ($profile.NetworkCategory -ne 'Private') {
    Set-NetConnectionProfile -InterfaceIndex $configuration.InterfaceIndex `
        -NetworkCategory Private
}

$ruleName = 'MTP Touchpad Receiver (TCP 39871)'
Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue |
    Remove-NetFirewallRule

New-NetFirewallRule `
    -DisplayName $ruleName `
    -Direction Inbound `
    -Action Allow `
    -Protocol TCP `
    -LocalPort $Port `
    -Profile Private `
    -RemoteAddress LocalSubnet `
    -Program $receiver | Out-Null

$existing = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if (-not $existing) {
    Start-Process -FilePath $receiver -ArgumentList $Port
    Start-Sleep -Milliseconds 750
}

$address = $configuration.IPv4Address.IPAddress
$listening = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue

Write-Host ''
Write-Host 'Windows touchpad receiver is ready.' -ForegroundColor Green
Write-Host "Interface : $($configuration.InterfaceAlias)"
Write-Host "IPv4      : $address"
Write-Host "Port      : $Port"
Write-Host "Firewall  : Private / LocalSubnet only"
Write-Host "Listening : $([bool]$listening)"
Write-Host ''
Write-Host 'Run this on the Mac:'
Write-Host "./build/mac-touch-agent $address $Port"
Write-Host ''
Write-Host 'Connectivity-only check from macOS:'
Write-Host "nc -vz $address $Port"

Read-Host 'Press Enter to close'
