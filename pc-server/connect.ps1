<#
    连接掌控板热点，然后启动数据服务

    干嘛的：AP 模式下板子自己发热点，电脑得先连上去，才能收数据。
    这个脚本把「连热点 → 等分配 IP → 核对 IP → 启动服务 → 开浏览器」串成一步。

    注意：连上板子热点后，这台电脑的无线网卡就没外网了。
          想同时保留外网，把网线插到 ASES 路由器上，让有线走外网、无线连板子。

    运行（执行策略是 Restricted，必须带 Bypass）：
        powershell -ExecutionPolicy Bypass -File "C:\Users\Administrator\Desktop\项目\pc-server\connect.ps1"
#>

$Ssid     = 'BMP388'
$WantIP   = '192.168.4.2'          # 板子代码里 SERVER_HOST 写的地址
$NodeExe  = 'C:\Program Files\Mind+\apps\deps\mind-link\node.exe'
$ServerJs = Join-Path $PSScriptRoot 'server.js'
$Port     = 8000
$Url      = "http://localhost:$Port"

function Stop-WithMessage {
    param([string]$Message, [string]$Color = 'Red')
    Write-Host ''
    Write-Host "  $Message" -ForegroundColor $Color
    Write-Host ''
    Read-Host '  按回车退出'
    exit 1
}

Write-Host ''
Write-Host '  连接掌控板热点' -ForegroundColor Cyan
Write-Host '  ----------------------------------------'

# 找 192.168.4.x 的地址，判断有没有连上板子
function Get-BoardIP {
    Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -like '192.168.4.*' -and $_.IPAddress -ne '192.168.4.1' } |
        Select-Object -First 1 -ExpandProperty IPAddress
}

$ip = Get-BoardIP
if ($ip) {
    Write-Host "  已经连着板子热点（$ip），跳过连接步骤。"
} else {
    Write-Host "  正在连接 $Ssid ..."
    netsh wlan connect name=$Ssid | Out-Null

    for ($i = 1; $i -le 25; $i++) {
        Start-Sleep -Seconds 1
        $ip = Get-BoardIP
        if ($ip) { break }
        Write-Host "    等待分配地址... ${i}s"
    }
}

if (-not $ip) {
    Write-Host ''
    if (-not (Test-Path -LiteralPath $NodeExe)) {
        Stop-WithMessage "连不上热点 $Ssid，而且找不到 node.exe`n  $NodeExe"
    }
    Stop-WithMessage @"
连不上热点 $Ssid。逐个排查：
    1. 板子插着电吗？OLED 第1行应该显示 192.168.4.1
    2. 板子上的灯是蓝的吗？蓝=热点开着等人连；红=热点没开起来
    3. 手动连一下：点右下角网络图标，找 $Ssid，密码 12345678
    4. 连上后重新跑本脚本
"@ 'Yellow'
}

Write-Host "  已连接，本机地址 $ip" -ForegroundColor Green

# 板子固件里 SERVER_HOST 写死了一个地址，对不上就收不到数据
if ($ip -ne $WantIP) {
    Write-Host ''
    Write-Host "  [注意] 板子固件里写的是 $WantIP，但你实际拿到的是 $ip" -ForegroundColor Yellow
    Write-Host "         板子会把数据发到 $WantIP，你这边收不到。" -ForegroundColor Yellow
    Write-Host "         改 BMP388_WiFi.ino 第 29 行的 SERVER_HOST 为 $ip 重新烧录即可。" -ForegroundColor Yellow
    Write-Host ''
    Read-Host '  按回车继续（或 Ctrl+C 退出先去改固件）'
}

if (-not (Test-Path -LiteralPath $NodeExe)) {
    Stop-WithMessage "找不到 node.exe`n  $NodeExe`n  确认 Mind+ 还装在原位置。"
}
if (-not (Test-Path -LiteralPath $ServerJs)) {
    Stop-WithMessage "找不到 server.js`n  $ServerJs"
}

$busy = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if ($busy) {
    Stop-WithMessage "端口 $Port 已被占用（PID $($busy.OwningProcess)）。`n  可能服务已经在跑了 —— 直接打开 $Url 看看。" 'Yellow'
}

Write-Host '  ----------------------------------------'
Write-Host "  启动服务，浏览器打开 $Url"
Write-Host "  这个窗口不要关，关掉服务就停了。"
Write-Host '  ----------------------------------------'

Start-Process $Url
try {
    & $NodeExe $ServerJs
}
finally {
    Write-Host ''
    Write-Host '  服务已停止。'
    Read-Host '  按回车退出'
}
