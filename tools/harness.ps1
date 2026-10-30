# Drives the two harness copies without a human (start them with tools\harness_start.ps1).
#   harness.ps1 near                 both heroes next to the NPC (default Ratford), host (seat 0) and guest (seat 1)
#   harness.ps1 hp <value>           set the NPC's health in the HOST copy only (0 / 1 = knocked out)
#   harness.ps1 shot [name]          screenshot of both windows into %TEMP%\g1shots\<name>_seat0/1.png
#   harness.ps1 stop                 close both copies
param([Parameter(Position = 0)][string]$Cmd = "shot", [Parameter(Position = 1)][string]$Arg = "", [string]$Npc = "Ratford")
$c = Split-Path $PSScriptRoot -Parent
$dev = "$c\tools\dev.ps1"
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
# probe commands go to every running game; take only the answer of one pid
function Probe($seat, $cmds) {
    $o = & $dev probe @($cmds) 2>&1 | Out-String
    $parts = $o -split "===== game pid "
    $id = PidOf $seat
    ($parts | Where-Object { $_ -match "^$id" }) -join "`n"
}
function NpcPtr($seat) {
    $o = Probe $seat "npcs 20000 40"
    ([regex]::Match($o, "([0-9A-F]{16}) Character_[^ ]*$Npc")).Groups[1].Value
}
switch ($Cmd) {
    "near" {
        foreach ($seat in 0, 1) { $n = NpcPtr $seat; if (-not $n) { "seat ${seat}: NPC $Npc not found"; continue }; (Probe $seat "tpto hero 0x$n 220 0 20") | Select-String "tpto"; Start-Sleep 1; (Probe $seat "lookat 0x$n -22") | Select-String "lookat" }
    }
    "hp" {
        $n = NpcPtr 0; if (-not $n) { throw "NPC $Npc not found in the host" }
        Probe 0 "npchp 0x$n $Arg" | Select-String "health set|no Attr"
    }
    "shot" {
        $name = if ($Arg) { $Arg } else { "shot" }
        foreach ($seat in 0, 1) { & "$c\tools\winshot.ps1" -Seat $seat -Out "$env:TEMP\g1shots\${name}_seat$seat.png" }
    }
    "stop" { Get-Process G1R-Win64-Shipping -ErrorAction SilentlyContinue | Stop-Process -Force }
}
