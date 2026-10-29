# Starts two game instances (ids A and B) plus a UDP helper for each (A:40001 <-> B:40002 on localhost).
# Both instances share game-test\...\Mods\G1RCoopProbe\net for the helper files.
# B runs small and at minimum graphics: two full instances overload this PC (B showed a black screen).
$c = "C:\Program Files (x86)\Steam\steamapps\common\Gothic1R-coop"
$exe = "$c\game-test\G1R\Binaries\Win64\G1R-Win64-Shipping.exe"
$net = "$c\game-test\G1R\Binaries\Win64\Mods\G1RCoopProbe\net"
$helper = "$c\helper\build\coophelper.exe"
New-Item -ItemType Directory -Force $net | Out-Null
Get-Process coophelper -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Process $helper -ArgumentList "`"$net`" A 40001 127.0.0.1 40002" -WindowStyle Hidden
Start-Process $helper -ArgumentList "`"$net`" B 40002 127.0.0.1 40001" -WindowStyle Hidden

$argsA = '-console -windowed -ResX=1280 -ResY=720 -no-intro-cinematic -log'
$argsB = '-console -windowed -ResX=640 -ResY=360 -no-intro-cinematic -log -ExecCmds="r.ScreenPercentage 50,sg.ShadowQuality 0,sg.PostProcessQuality 0,sg.TextureQuality 0,sg.EffectsQuality 0,sg.FoliageQuality 0,sg.ViewDistanceQuality 0"'
foreach ($id in 'A', 'B') {
    $env:G1RCOOP_ID = $id
    $a = if ($id -eq 'B') { $argsB } else { $argsA }
    Start-Process $exe -ArgumentList $a -WorkingDirectory (Split-Path $exe)
    Start-Sleep 8
}
Remove-Item Env:\G1RCOOP_ID
