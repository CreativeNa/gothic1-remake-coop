# Safety net for test runs with two game copies: keeps the PC usable.
#  - the game processes run at BelowNormal priority (the desktop and other programs stay responsive)
#  - if AVAILABLE RAM (free + reclaimable cache) falls under -MinFreeMB, or the CPU stays above -CpuPct for -CpuSeconds, or the run lasts longer than -MaxMinutes,
#    the game copies and the test scripts are stopped and the reason is written to native\build\watchdog.log
# harness_start.ps1 starts it by default (hidden); it ends by itself when no game is running any more.
param([int]$MinFreeMB = 800, [int]$CpuPct = 97, [int]$CpuSeconds = 25, [int]$MaxMinutes = 25)
$c = Split-Path $PSScriptRoot -Parent
$log = Join-Path $c "native/build/watchdog.log"
function Log($m) { "{0:HH:mm:ss} {1}" -f (Get-Date), $m | Add-Content $log }
$t0 = Get-Date; $hot = 0; $seen = $false
Log "watchdog started (min free $MinFreeMB MB, cpu $CpuPct % for $CpuSeconds s, max $MaxMinutes min)"
while ($true) {
    $games = @(Get-Process G1R-Win64-Shipping -ErrorAction SilentlyContinue)
    if ($games.Count -gt 0) { $seen = $true; foreach ($g in $games) { try { if ($g.PriorityClass -ne "BelowNormal") { $g.PriorityClass = "BelowNormal" } } catch {} } }
    elseif ($seen -or ((Get-Date) - $t0).TotalMinutes -gt 4) { Log "no game running, watchdog ends"; break }
    $free = [int](Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory).AvailableMBytes      # available = free + cache that Windows can give back at once (locale independent)
    $cpu = (Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average).Average
    if ($cpu -ge $CpuPct) { $hot += 2 } else { $hot = 0 }
    $why = $null
    if ($games.Count -gt 0) {
        if ($free -lt $MinFreeMB) { $why = "available RAM $free MB is under $MinFreeMB MB" }
        elseif ($hot -ge $CpuSeconds) { $why = "CPU at $cpu % for $hot s" }
        elseif (((Get-Date) - $t0).TotalMinutes -gt $MaxMinutes) { $why = "run longer than $MaxMinutes min" }
    }
    if ($why) {
        Log "STOP: $why - stopping the games and the test scripts"
        Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -match 'run_sync|run_all|test_|harness\.ps1' -and $_.ProcessId -ne $PID } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        Get-Process G1R-Win64-Shipping -ErrorAction SilentlyContinue | Stop-Process -Force
        break
    }
    Start-Sleep 2
}
