# Automated knock-out test (both copies must be running, see harness_start.ps1): the host hero fights Ratford with real game input actions until the
# game logs "Defeated", then both windows are photographed several times and joined into docs\img\ko_<n>.png (host | guest).
param([string]$Npc = "Ratford", [int]$MaxAttacks = 14, [int]$GuestDist = 330, [int]$GuestPitch = -35)   # the guest camera: distance behind the host hero and pitch (use -8 and 150 for a low close-up)
$c = Split-Path $PSScriptRoot -Parent
$dev = Join-Path $c "tools\dev.ps1"; $log = Join-Path $c "native\build\coopdll.log"; $tmp = Join-Path $env:TEMP "g1shots"
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
function NpcPtr($seat) { ([regex]::Match((Probe $seat "npcs 20000 40"), "([0-9A-F]{16}) Character_[^ ]*$Npc")).Groups[1].Value }
$hp = PidOf 0; $gp = PidOf 1
$hn = NpcPtr 0; $gn = NpcPtr 1; if (-not $hn -or -not $gn) { throw "NPC $Npc not found (host '$hn', guest '$gn')" }
Probe 0 "npchp 0x$hn 6" | Out-Null
& (Join-Path $c "tools\realclick.ps1") -Seat 0 -Click 0 | Out-Null                       # the game reads input only while its window has the focus
Probe 0 "tpto 0x$hn hero 170 0 10" | Out-Null; Start-Sleep 1
Probe 0 "lookat 0x$hn 0 -35" | Out-Null
Probe 0 "inject IA_Ability_Action_EquipWeaponAuto" | Out-Null; Start-Sleep 3
Probe 1 "tpto hero puppet $GuestDist 0 10" | Out-Null; Start-Sleep 2
Probe 1 "lookat 0x$gn 0 $GuestPitch" | Out-Null
$mark = (Get-Content $log -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
$acts = "IA_Combat_Attack_Top", "IA_Combat_Attack_Left", "IA_Combat_Attack_Right", "IA_Combat_Attack_Bottom"
$down = $false
for ($i = 0; $i -lt $MaxAttacks -and -not $down; $i++) {
    Probe 0 "lookat 0x$hn 0 -35" | Out-Null; Probe 0 "inject $($acts[$i % 4])" | Out-Null; Start-Sleep -Milliseconds 900
    $down = [bool](Select-String -Path $log -Pattern "^\[$hp\] A local action -> .*Defeated" | Select-Object -Last 1 | Where-Object { $_.LineNumber -gt $mark })
}
"knocked out: $down after $($i) attacks"
New-Item -ItemType Directory -Force (Join-Path $c "docs\img") | Out-Null
foreach ($k in 1..3) {
    foreach ($seat in 0, 1) { & (Join-Path $c "tools\winshot.ps1") -Seat $seat -Out "$tmp\ko${k}_seat$seat.png" | Out-Null }
    Start-Sleep -Milliseconds 1500
}
Add-Type -AssemblyName System.Drawing
foreach ($k in 1..3) {
    $a = [Drawing.Image]::FromFile("$tmp\ko${k}_seat0.png"); $b = [Drawing.Image]::FromFile("$tmp\ko${k}_seat1.png")
    $bmp = New-Object Drawing.Bitmap ($a.Width + $b.Width + 8), ([Math]::Max($a.Height, $b.Height) + 22); $g = [Drawing.Graphics]::FromImage($bmp); $g.Clear([Drawing.Color]::FromArgb(24, 24, 24))
    $g.DrawImage($a, 0, 22); $g.DrawImage($b, $a.Width + 8, 22)
    $f = New-Object Drawing.Font "Segoe UI", 9; $g.DrawString("host (seat 0): the NPC is fought here", $f, [Drawing.Brushes]::White, 4, 3); $g.DrawString("guest (seat 1): the same NPC mirrored from the host", $f, [Drawing.Brushes]::White, $a.Width + 12, 3)
    $bmp.Save((Join-Path $c "docs\img\ko_$k.png")); $g.Dispose(); $bmp.Dispose(); $a.Dispose(); $b.Dispose()
}
"images: docs\img\ko_1..3.png"
