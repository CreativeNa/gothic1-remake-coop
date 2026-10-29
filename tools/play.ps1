# Usage: .\play.ps1 [-Count 2] [-Dev] [-Extra "<more args>"]
# Without -Dev the game uses the shipped precompiled script cache (modkit sources are NOT used).
param([int]$Count = 1, [switch]$Dev, [string]$Extra = "")
$exe = "C:\Program Files (x86)\Steam\steamapps\common\Gothic1R-coop\game-test\G1R\Binaries\Win64\G1R-Win64-Shipping.exe"
$gameArgs = "-console -windowed -ResX=1280 -ResY=720 -no-intro-cinematic -log $Extra"
if ($Dev) { $gameArgs += " -as-development-mode" }
1..$Count | ForEach-Object { Start-Process $exe -ArgumentList $gameArgs -WorkingDirectory (Split-Path $exe); Start-Sleep 5 }
