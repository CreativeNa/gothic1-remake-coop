# Starts two game instances and injects native\build\<dll> into both.
#   A: 1280x720 on the primary monitor.  B: 1280x720 on the second monitor (primary if there is only one).
#   B keeps minimum graphics settings (two full instances overloaded this PC once); the resolution is the same as A's.
# Each instance gets native\build\cfg_<pid>.txt: "<localPort> <peerHost> <peerPort> <id>" (A:40001 <-> B:40002).
param([string]$Dll = "coopdll_8.dll", [int]$WaitSeconds = 45,
      [switch]$ViaJoin)   # configure and inject through tools\join.ps1 (the same path a friend uses), peers addressed as 127.0.0.1
$c = "C:\Program Files (x86)\Steam\steamapps\common\Gothic1R-coop"
$exe = "$c\game-test\G1R\Binaries\Win64\G1R-Win64-Shipping.exe"
$nat = "$c\native\build"
Get-Process G1R-Win64-Shipping -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process coophelper -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 3
Remove-Item "$nat\cfg_*.txt" -ErrorAction SilentlyContinue

Add-Type -AssemblyName System.Windows.Forms
$screens = [Windows.Forms.Screen]::AllScreens
$primary = $screens | Where-Object { $_.Primary } | Select-Object -First 1
$second = $screens | Where-Object { -not $_.Primary } | Select-Object -First 1
if (-not $second) { $second = $primary }
$ax = $primary.Bounds.X + 60;  $ay = $primary.Bounds.Y + 60
$bx = $second.Bounds.X + 60;   $by = $second.Bounds.Y + 60

$argsA = "-console -windowed -ResX=1280 -ResY=720 -WinX=$ax -WinY=$ay -no-intro-cinematic"
$argsB = "-console -windowed -ResX=1280 -ResY=720 -WinX=$bx -WinY=$by -no-intro-cinematic -ExecCmds=`"sg.ShadowQuality 0,sg.PostProcessQuality 0,sg.TextureQuality 0,sg.EffectsQuality 0,sg.FoliageQuality 0,sg.ViewDistanceQuality 0`""
$pa = Start-Process $exe -ArgumentList $argsA -WorkingDirectory (Split-Path $exe) -PassThru
Start-Sleep 8
$pb = Start-Process $exe -ArgumentList $argsB -WorkingDirectory (Split-Path $exe) -PassThru
"A pid $($pa.Id) at $ax,$ay ; B pid $($pb.Id) at $bx,$by"
Start-Sleep $WaitSeconds
if ($ViaJoin) {
    & "$PSScriptRoot\join.ps1" -Peer "127.0.0.1:40002" -LocalPort 40001 -Id A -Dll $Dll -ProcessId $pa.Id
    & "$PSScriptRoot\join.ps1" -Peer "localhost:40001" -LocalPort 40002 -Id B -Dll $Dll -ProcessId $pb.Id
} else {
    Set-Content "$nat\cfg_$($pa.Id).txt" "40001 127.0.0.1 40002 A"
    Set-Content "$nat\cfg_$($pb.Id).txt" "40002 127.0.0.1 40001 B"
    foreach ($p in $pa, $pb) { & "$nat\inject.exe" $p.Id "$nat\$Dll" }
}
