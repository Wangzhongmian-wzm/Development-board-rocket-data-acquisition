param([string]$Port = "COM7", [int]$Seconds = 10)
# ASCII only - no BOM needed.
# Pulses the ESP32 auto-reset circuit (RTS=EN, DTR=IO0) so we capture the boot banner.

$p = New-Object System.IO.Ports.SerialPort $Port, 115200, "None", 8, "One"
$p.ReadTimeout = 500

$p.DtrEnable = $false     # IO0 high  -> normal boot (not bootloader)
$p.RtsEnable = $true      # EN low    -> hold in reset

try { $p.Open() } catch { Write-Output "OPEN FAILED: $($_.Exception.Message)"; exit 1 }

Start-Sleep -Milliseconds 300
$p.RtsEnable = $false     # EN high   -> release, chip boots
Start-Sleep -Milliseconds 100

$end = (Get-Date).AddSeconds($Seconds)
$sb = New-Object System.Text.StringBuilder
while ((Get-Date) -lt $end) {
    try { [void]$sb.Append($p.ReadExisting()) } catch { }
    Start-Sleep -Milliseconds 120
}
$p.Close()
Write-Output $sb.ToString()
