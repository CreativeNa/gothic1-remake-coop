# Test harness: starts BOTH coop copies without CouchLink (seat 0 = host, seat 1 = guest), side by side on the primary monitor,
# and the per-seat helper that injects the newest coopdll. Autoload takes them into the newest save.
# Afterwards use tools\harness.ps1 (wait / shot / probe) to drive and to look at the game without a human.
param([int]$W = 800, [int]$H = 450, [int[]]$Seats = @(0, 1), [switch]$NoWatchdog, [switch]$Lite)   # watchdog.ps1 keeps the PC usable (priority, RAM / CPU limits). Do NOT start the game with -nosound: fmodstudio.dll then crashes (null read) a few seconds after the start         # -Seats 0 starts only the host copy (half the memory, enough for probing the game)
$c = Split-Path $PSScriptRoot -Parent
$exe = "$c\game-test\G1R\Binaries\Win64\G1R-Win64-Shipping.exe"
if (Get-Process G1R-Win64-Shipping -ErrorAction SilentlyContinue) { throw "a game is already running" }
Remove-Item "$c\native\build\autoload_done_*.txt", "$c\native\build\cfg_*.txt" -ErrorAction SilentlyContinue
$lg = "$c\native\build\coopdll.log"                       # one log per run: the old one is archived, so logq and the test suite only ever see this run
if (Test-Path $lg) { New-Item -ItemType Directory -Force "$c\native\build\logs" | Out-Null; Move-Item $lg ("$c\native\build\logs\coopdll_{0:yyyyMMdd_HHmmss}.log" -f (Get-Date)) -Force -ErrorAction SilentlyContinue }
Remove-Item "$c\native\build\couchlink_seat*.log" -ErrorAction SilentlyContinue
Remove-Item "$c\native\build\test_spawn.txt" -ErrorAction SilentlyContinue     # the sync tests record the heroes' start positions on their first run
Add-Type -AssemblyName System.Windows.Forms
$s = [Windows.Forms.Screen]::PrimaryScreen.Bounds
foreach ($seat in $Seats) {
    $x = $s.X + 20 + $seat * ($W + 20); $y = $s.Y + 40
    Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$c\tools\couchlink\couchlink_seat.ps1`" -Seat $seat" -WindowStyle Hidden
    $noGap = if ($seat -gt 0) { " -NoLoadGap" } else { "" }          # this script already waited for the previous copy: autoload must not wait for it again
    $liteArgs = if ($Lite) { " -ExecCmds=`"r.Streaming.PoolSize 200,r.MipMapLODBias 3,r.Streaming.MaxTempMemoryAllowed 32,r.ScreenPercentage 50`"" } else { "" }     # -Lite: smaller texture pool, lower mips, half resolution (command-line only, nothing is saved)
    Start-Process $exe -ArgumentList "-windowed -ResX=$W -ResY=$H -WinX=$x -WinY=$y -no-intro-cinematic -CoopSeat=$seat$liteArgs$noGap" -WorkingDirectory (Split-Path $exe)
    if ($seat -eq 0 -and $Seats.Count -gt 1) {                       # loading a save takes about 13 GB per copy for a minute: the second copy starts only after the first is in the game (two loads at once ran the PC out of memory)
        $t0 = Get-Date
        while (((Get-Date) - $t0).TotalSeconds -lt 180) {
            Start-Sleep 5
            $o = & "$c\tools\dev.ps1" probe "frames" 2>&1 | Out-String
            if ($o -match "hero=0000[0-9A-F]*[1-9A-F]") { break }
        }
        Start-Sleep 10
    } elseif ($seat -eq 0) { Start-Sleep 13 }
}
if (-not $NoWatchdog) { Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$c\tools\watchdog.ps1`"" -WindowStyle Hidden }
"copies started: $($Seats -join ', ')"
