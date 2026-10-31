# Drives the two harness copies without a human (start them with tools\harness_start.ps1). Every command goes to ONE seat only ("@<seat> ..." prefix in the probe).
#   harness.ps1 shot [name]          screenshot of both windows into %TEMP%\g1shots\<name>_seat0/1.png
#   harness.ps1 near                 NPC (default Ratford) is moved next to each hero, cameras look at it (the NPC is moved, not the hero: a hero teleported into rock is stuck)
#   harness.ps1 hp <value>           set the NPC's health in the HOST copy (seat 0)
#   harness.ps1 act <InputAction>    press an Enhanced Input action in the host, e.g. IA_Ability_Action_EquipWeaponAuto
#   harness.ps1 fight [n]            host hero draws the weapon and attacks the NPC n times with real game input actions (no keys, no mouse)
#   harness.ps1 stop                 close both copies
param([Parameter(Position = 0)][string]$Cmd = "shot", [Parameter(Position = 1)][string]$Arg = "", [string]$Npc = "Ratford")
$c = Split-Path $PSScriptRoot -Parent
$dev = Join-Path $c "tools\dev.ps1"
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmds) {
    $o = & $dev probe @($cmds | ForEach-Object { "@$seat $_" }) 2>&1 | Out-String
    $id = PidOf $seat
    (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n"
}
function NpcPtr($seat) {
    $o = Probe $seat "npcs 20000 40"
    ([regex]::Match($o, "([0-9A-F]{16}) Character_[^ ]*$Npc")).Groups[1].Value
}
switch ($Cmd) {
    "near" {
        foreach ($seat in 0, 1) {
            $n = NpcPtr $seat; if (-not $n) { "seat ${seat}: NPC $Npc not found"; continue }
            Probe $seat "tpto 0x$n hero 260 0 10" | Select-String "tpto ran"; Start-Sleep 1
            Probe $seat "lookat 0x$n" | Select-String "lookat ran"
        }
    }
    "hp" {
        $n = NpcPtr 0; if (-not $n) { throw "NPC $Npc not found in the host" }
        Probe 0 "npchp 0x$n $Arg" | Select-String "health set|no Attr"
    }
    "act" { & (Join-Path $c "tools
ealclick.ps1") -Seat 0 -Click 0 | Out-Null; Probe 0 "inject $Arg" | Select-String "inject" }
    "fight" {
        & (Join-Path $c "tools
ealclick.ps1") -Seat 0 -Click 0 | Out-Null      # the game only reads input while its window has the focus
        $n = NpcPtr 0; if (-not $n) { throw "NPC $Npc not found in the host" }
        Probe 0 "tpto 0x$n hero 140 0 10" | Out-Null; Start-Sleep 1; Probe 0 "lookat 0x$n" | Out-Null; Start-Sleep 1
        Probe 0 "inject IA_Ability_Action_EquipWeaponAuto" | Select-String "inject"; Start-Sleep 3
        $k = if ($Arg) { [int]$Arg } else { 8 }
        $acts = "IA_Combat_Attack_Top", "IA_Combat_Attack_Left", "IA_Combat_Attack_Right", "IA_Combat_Attack_Bottom"
        for ($i = 0; $i -lt $k; $i++) { Probe 0 "lookat 0x$n" | Out-Null; Probe 0 "inject $($acts[$i % 4])" | Out-Null; Start-Sleep -Milliseconds 900 }
    }
    "shot" {
        $name = if ($Arg) { $Arg } else { "shot" }
        foreach ($seat in 0, 1) { & (Join-Path $c "tools\winshot.ps1") -Seat $seat -Out "$env:TEMP\g1shots\${name}_seat$seat.png" }
    }
    "stop" { Get-Process G1R-Win64-Shipping -ErrorAction SilentlyContinue | Stop-Process -Force }
}
