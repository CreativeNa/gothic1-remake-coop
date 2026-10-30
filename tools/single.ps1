# Starts ONE copy of the test game, to find out who causes a crash.
#   .\single.ps1 -Dll none  -ModsOff     vanilla test copy: no coop DLL, UE4SS Lua mods switched off
#   .\single.ps1 -Dll none               no coop DLL, the usual Lua mods
#   .\single.ps1 -Dll coopdll_40.dll     the coop DLL in this one copy (no friend, so no puppet)
# -ModsOff edits game-test\...\Mods\mods.txt (backup mods.txt.single_bak) and puts it back when the game exits.
param([string]$Dll = "none", [switch]$ModsOff, [int]$InjectAfterSeconds = 40)
$c = Split-Path $PSScriptRoot -Parent
$exe = "$c\game-test\G1R\Binaries\Win64\G1R-Win64-Shipping.exe"
$mods = "$c\game-test\G1R\Binaries\Win64\Mods\mods.txt"
$nat = "$c\native\build"
if (Get-Process G1R-Win64-Shipping -ErrorAction SilentlyContinue) { throw "a game process is already running - close it first (so the test is clean)" }
if ($ModsOff) {
    Copy-Item $mods "$mods.single_bak" -Force
    $t = Get-Content $mods -Raw
    [IO.File]::WriteAllText($mods, ([regex]::Replace($t, "(?m)^(\S+)\s*:\s*1", '$1 : 0')), (New-Object Text.UTF8Encoding $false))
    "Lua mods switched off (backup: mods.txt.single_bak)"
}
Add-Type -AssemblyName System.Windows.Forms
$s = [Windows.Forms.Screen]::PrimaryScreen
$p = Start-Process $exe -ArgumentList "-windowed -ResX=1280 -ResY=720 -WinX=$($s.Bounds.X + 60) -WinY=$($s.Bounds.Y + 60) -no-intro-cinematic" -WorkingDirectory (Split-Path $exe) -PassThru
"game pid $($p.Id); dll: $Dll; mods off: $ModsOff"
if ($Dll -ne "none") {
    Start-Sleep $InjectAfterSeconds
    & "$nat\inject.exe" $p.Id "$nat\$Dll"
}
"Load a save and swing the sword. This script restores the mods when the game closes."
$p.WaitForExit()
"game exited with code $($p.ExitCode) at $(Get-Date -Format HH:mm:ss)"
if ($ModsOff -and (Test-Path "$mods.single_bak")) { Copy-Item "$mods.single_bak" $mods -Force; Remove-Item "$mods.single_bak"; "mods.txt restored" }
