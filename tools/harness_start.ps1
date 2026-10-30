# Test harness: starts BOTH coop copies without CouchLink (seat 0 = host, seat 1 = guest), side by side on the primary monitor,
# and the per-seat helper that injects the newest coopdll. Autoload takes them into the newest save.
# Afterwards use tools\harness.ps1 (wait / shot / probe) to drive and to look at the game without a human.
param([int]$W = 800, [int]$H = 450)
$c = Split-Path $PSScriptRoot -Parent
$exe = "$c\game-test\G1R\Binaries\Win64\G1R-Win64-Shipping.exe"
if (Get-Process G1R-Win64-Shipping -ErrorAction SilentlyContinue) { throw "a game is already running" }
Remove-Item "$c\native\build\autoload_done_*.txt", "$c\native\build\cfg_*.txt" -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.Windows.Forms
$s = [Windows.Forms.Screen]::PrimaryScreen.Bounds
foreach ($seat in 0, 1) {
    $x = $s.X + 20 + $seat * ($W + 20); $y = $s.Y + 40
    Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$c\tools\couchlink\couchlink_seat.ps1`" -Seat $seat" -WindowStyle Hidden
    Start-Process $exe -ArgumentList "-windowed -ResX=$W -ResY=$H -WinX=$x -WinY=$y -no-intro-cinematic -CoopSeat=$seat" -WorkingDirectory (Split-Path $exe)
    if ($seat -eq 0) { Start-Sleep 13 }
}
"both copies started"
