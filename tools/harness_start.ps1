# Test harness: starts BOTH coop copies without CouchLink (seat 0 = host, seat 1 = guest), side by side on the primary monitor,
# and the per-seat helper that injects the newest coopdll. Autoload takes them into the newest save.
# Afterwards use tools\harness.ps1 (wait / shot / probe) to drive and to look at the game without a human.
param([int]$W = 800, [int]$H = 450, [int[]]$Seats = @(0, 1))         # -Seats 0 starts only the host copy (half the memory, enough for probing the game)
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
    Start-Process $exe -ArgumentList "-windowed -ResX=$W -ResY=$H -WinX=$x -WinY=$y -no-intro-cinematic -CoopSeat=$seat" -WorkingDirectory (Split-Path $exe)
    if ($seat -eq 0) { Start-Sleep 13 }
}
"copies started: $($Seats -join ', ')"
