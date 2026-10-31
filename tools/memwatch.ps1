# Logs the game's working set and the available RAM every 2 s to native\build\memwatch.txt and stops the games when available RAM falls under -MinMB (a second safety net, for diagnosis).
param([int]$MinMB = 1200, [int]$Seconds = 900)
$c = Split-Path $PSScriptRoot -Parent; $f = Join-Path $c "native/build/memwatch.txt"; Set-Content $f ""
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    $g = @(Get-Process G1R-Win64-Shipping -ErrorAction SilentlyContinue); $a = (Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory).AvailableMBytes
    $ws = [int](($g | Measure-Object WorkingSet64 -Sum).Sum / 1MB)
    Add-Content $f ("{0:HH:mm:ss} games {1} ws {2} avail {3}" -f (Get-Date), $g.Count, $ws, $a)
    if ($a -lt $MinMB -and $g.Count -gt 0) { $g | Stop-Process -Force; Add-Content $f "EMERGENCY STOP"; break }
    Start-Sleep 2
}
