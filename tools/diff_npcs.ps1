# Numeric comparison of the NPCs of the two running copies (host seat 0 vs guest seat 1): position, yaw, defeated flag, health.
#   diff_npcs.ps1 [-Name Ratford] [-MaxDist 150]   prints a table and returns the mismatching rows; exit code 1 if there are any
param([string]$Name = "Character_", [double]$MaxDist = 150, [switch]$Quiet)
$c = Split-Path $PSScriptRoot -Parent
$dev = Join-Path $c "tools\dev.ps1"
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Rows($seat) {
    $o = & $dev probe "@$seat npcinfo $Name" 2>&1 | Out-String; $id = PidOf $seat
    $part = (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n"
    foreach ($l in ($part -split "`n")) {
        if ($l -match 'NPCINFO (\S+) pos (-?\d+) (-?\d+) (-?\d+) yaw (-?\d+) defeated (\d) health (-?\d+) controller (\d)') {
            [pscustomobject]@{ Name = $matches[1]; X = [double]$matches[2]; Y = [double]$matches[3]; Z = [double]$matches[4]; Yaw = [double]$matches[5]; Defeated = [int]$matches[6]; Health = [double]$matches[7]; Controller = [int]$matches[8] }
        }
    }
}
$h = @(Rows 0); $g = @(Rows 1)
$out = foreach ($a in $h) {
    $b = $g | Where-Object { $_.Name -eq $a.Name } | Select-Object -First 1
    if (-not $b) { [pscustomobject]@{ Name = $a.Name; Dist = $null; HostDefeated = $a.Defeated; GuestDefeated = $null; HostHp = $a.Health; GuestHp = $null; Verdict = "MISSING in guest" }; continue }
    $d = [math]::Sqrt([math]::Pow($a.X - $b.X, 2) + [math]::Pow($a.Y - $b.Y, 2) + [math]::Pow($a.Z - $b.Z, 2))
    $v = if ($d -gt $MaxDist) { "POSITION off by $([int]$d)" } elseif ($a.Health -ge 0 -and $b.Health -ge 0 -and [math]::Abs($a.Health - $b.Health) -gt 8) { "HEALTH differs ($($a.Health) vs $($b.Health))" } else { "ok" }
    [pscustomobject]@{ Name = $a.Name -replace '^Character_', ''; Dist = [int]$d; HostDefeated = $a.Defeated; GuestDefeated = $b.Defeated; HostHp = $a.Health; GuestHp = $b.Health; Verdict = $v }
}
if (-not $Quiet) { $out | Format-Table -AutoSize | Out-String | Write-Host }
$bad = @($out | Where-Object { $_.Verdict -ne "ok" })
"NPCs compared: $(@($out).Count), mismatching: $($bad.Count)"
exit $(if ($bad.Count) { 1 } else { 0 })
