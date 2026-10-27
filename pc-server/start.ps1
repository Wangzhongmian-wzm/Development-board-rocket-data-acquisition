<#
    掌控板 BMP388 数据服务

    启动后浏览器打开 http://localhost:8000
    这个窗口不要关，关掉服务就停了。

    系统执行策略是 Restricted，双击/右键运行会被拦。
    用下面这条命令启动（-ExecutionPolicy Bypass 只对本次进程生效，不改系统设置），
    在任意目录的 PowerShell 或 CMD 窗口里粘贴都能跑：

        powershell -ExecutionPolicy Bypass -File "C:\Users\Administrator\Desktop\项目\pc-server\启动服务.ps1"
#>

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
Write-Host '  掌控板 BMP388 数据服务' -ForegroundColor Cyan
Write-Host '  ----------------------------------------'

# node.exe 是 Mind+ 自带的，不用单独安装；但 Mind+ 卸载或挪位置就没了
if (-not (Test-Path -LiteralPath $NodeExe)) {
    Stop-WithMessage "找不到 node.exe`n  $NodeExe`n  确认 Mind+ 还装在原位置。"
}

if (-not (Test-Path -LiteralPath $ServerJs)) {
    Stop-WithMessage "找不到 server.js`n  $ServerJs"
}

# 端口被占通常是已经有一个服务在跑了，与其抛一堆 node 报错，不如直接说清楚
$busy = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if ($busy) {
    Stop-WithMessage "端口 $Port 已被占用（进程 PID $($busy.OwningProcess)）。`n  可能已经有一个服务在跑了 —— 直接打开 $Url 看看。" 'Yellow'
}

Write-Host "  浏览器打开   $Url"
Write-Host "  这个窗口不要关，关掉服务就停了。"
Write-Host '  ----------------------------------------'

try {
    & $NodeExe $ServerJs
}
finally {
    Write-Host ''
    Write-Host '  服务已停止。'
    Read-Host '  按回车退出'
}
