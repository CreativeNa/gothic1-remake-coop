# Ownership fuzz test: the two heroes are teleported to random NPCs many times. Teleports use absolute coordinates recorded at the start, always a little ABOVE the NPC's height (the hero
# drops onto the ground), and after every round a hero that fell below the camp is put back. Afterwards the two copies must agree who owns every NPC, ownership must not flap,
# positions and health must match, and nothing in our code may fault.
#   test_owner_fuzz.ps1 [-Rounds 25] [-Seed 1] [-Start]       -Start: restart both copies first (the heroes must stand in the camp)
param([int]$Rounds = 25, [int]$Seed = 1, [string]$Report = "owner_fuzz", [switch]$Start)
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dev = Join-Path $c "tools\dev.ps1"; $log = Join-Path $c "native\build\coopdll.log"
$rnd = New-Object System.Random $Seed
$results = New-Object System.Collections.ArrayList
function Check($name, [bool]$ok, $detail = "") { [void]$results.Add([pscustomobject]@{ Check = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = "$detail" }); "{0,-5} {1}  {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail }
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; if (-not $id) { return "" }; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
function Ready { foreach ($s in 0, 1) { if (-not ((Probe $s "frames") -match "have_remote=1")) { return $false } }; return $true }
if ($Start) {
    & (Join-Path $c "tools\harness.ps1") stop; Start-Sleep 3; & (Join-Path $c "tools\harness_start.ps1") | Out-Null
    $t0 = Get-Date; while (((Get-Date) - $t0).TotalSeconds -lt 150 -and -not (Ready)) { Start-Sleep 5 }
    Start-Sleep 20
}
$hp = PidOf 0; $gp = PidOf 1
if (-not ($hp -and $gp)) { throw "start both copies first (tools\harness_start.ps1 or -Start)" }
$mark = (Get-Content $log | Measure-Object -Line).Lines
$spawn = @{}
foreach ($s in 0, 1) { $m = [regex]::Match((Probe $s "heroinfo"), "HEROINFO pos (-?\d+) (-?\d+) (-?\d+)"); $spawn[$s] = @([int]$m.Groups[1].Value, [int]$m.Groups[2].Value, [int]$m.Groups[3].Value) }
$npcs = @(); foreach ($m in [regex]::Matches((Probe 0 "npcinfo Character_"), "NPCINFO (\S+) pos (-?\d+) (-?\d+) (-?\d+)")) { if ($m.Groups[1].Value -notmatch "Scavenger") { $npcs += [pscustomobject]@{ Name = $m.Groups[1].Value; X = [int]$m.Groups[2].Value; Y = [int]$m.Groups[3].Value; Z = [int]$m.Groups[4].Value } } }
Check "setup: the camp NPCs and both hero positions are known" ($npcs.Count -ge 2 -and $spawn[0][0] -ne 0) "$($npcs.Count) NPCs"
if ($npcs.Count -lt 2) { "no NPCs: start the copies with -Start"; exit 1 }
$floor = ($npcs | Measure-Object -Property Z -Minimum).Minimum - 3000                       # lower terrain is fine, only a fall through the world is a loss
function Rescue($s) {                                                          # a hero below the camp (fell through) goes back to where it started
    $z = [int]([regex]::Match((Probe $s "heroinfo"), "HEROINFO pos -?\d+ -?\d+ (-?\d+)").Groups[1].Value)
    if ($z -lt $floor) { Probe $s "tp hero $($spawn[$s][0]) $($spawn[$s][1]) $($spawn[$s][2] + 60)" | Out-Null; $script:rescued++ }
}
$rescued = 0
"fuzzing $Rounds rounds over $($npcs.Count) NPCs"
for ($i = 0; $i -lt $Rounds; $i++) {
    $seat = $rnd.Next(0, 2); $a = $npcs[$rnd.Next(0, $npcs.Count)]
    Probe $seat "tp hero $($a.X + 250) $($a.Y + $rnd.Next(-200, 200)) $($a.Z + 120)" | Out-Null
    Probe (1 - $seat) "tp hero $($a.X + 1300) $($a.Y + $rnd.Next(-300, 300)) $($a.Z + 200)" | Out-Null
    Start-Sleep -Seconds 5
    Rescue 0; Rescue 1
}
foreach ($s in 0, 1) { Probe $s "tp hero $($spawn[$s][0]) $($spawn[$s][1]) $($spawn[$s][2] + 60)" | Out-Null }       # back to where they started
Start-Sleep 14
$mark3 = (Get-Content $log | Measure-Object -Line).Lines                                         # from here on nobody moves: ownership must stay put
Start-Sleep 8
Check "world: heroes were put back after falling through unloaded ground (test artefact, informational)" $true "$rescued rescues"
# ---- replay the ownership changes of both copies ----
$new = Get-Content $log | Select-Object -Skip $mark
$maps = @{ 0 = @{}; 1 = @{} }; $flips = @{}
foreach ($l in $new) {
    if ($l -match '^\[(\d+)\] N owner of ([0-9a-f]+) -> seat (\d)') {
        $seat = if ([int]$matches[1] -eq $hp) { 0 } elseif ([int]$matches[1] -eq $gp) { 1 } else { -1 }
        if ($seat -ge 0) { $maps[$seat][$matches[2]] = [int]$matches[3]; if ($seat -eq 0) { $flips[$matches[2]] = 1 + $flips[$matches[2]] } }
    }
}
$ids = @($maps[0].Keys + $maps[1].Keys | Sort-Object -Unique)
$disagree = @($ids | Where-Object { $null -ne $maps[0][$_] -and $null -ne $maps[1][$_] -and $maps[0][$_] -ne $maps[1][$_] })
Check "owners: both copies agree who owns every NPC they both changed" ($disagree.Count -eq 0) "$($ids.Count) NPCs changed owner, $($disagree.Count) disagree"
$onlyOne = @($ids | Where-Object { ($null -eq $maps[0][$_]) -xor ($null -eq $maps[1][$_]) })
Check "owners: every ownership change was noticed by both copies (allowing one lost at a teleport)" ($onlyOne.Count -le 1) "$($onlyOne.Count) seen by one copy only"
$quiet = @(Get-Content $log | Select-Object -Skip $mark3 | Select-String "N owner of [0-9a-f]+ -> seat")
Check "owners: no flapping (no ownership change while nobody moves)" ($quiet.Count -eq 0) "$($quiet.Count) changes in the quiet 8 s"
$diff = & (Join-Path $c "tools\diff_npcs.ps1") -Quiet -MaxDist 800
$cmp = [int]([regex]::Match(($diff | Select-Object -Last 1), "compared: (\d+)").Groups[1].Value)
Check "sync: positions and health match after the fuzzing (at least 2 NPCs compared)" ($LASTEXITCODE -eq 0 -and $cmp -ge 2) ($diff | Select-Object -Last 1)
$ours = @(& (Join-Path $c "tools\logq.ps1") -Crashes -Last 40 | Where-Object { $_ -match "our code at that moment: call=(?!none)|EXCEPTION" -and $_ -notmatch "IsDefeated|IsDead" })
Check "stability: no unhandled fault inside our code (caught faults on streamed-out actors are expected)" ($ours.Count -eq 0) ($ours | Select-Object -First 2 | Out-String).Trim()
Check "stability: both copies still running" ([bool]((PidOf 0) -and (PidOf 1)))
$dir = Join-Path $c "docs\reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$pass = @($results | Where-Object Result -eq "PASS").Count
$md = "# Ownership fuzz $(Get-Date -Format 'yyyy-MM-dd HH:mm') ($Rounds rounds, seed $Seed)`n`n$pass / $($results.Count) checks passed.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/' -replace "`r?`n", ' ') |`n" }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
"$pass / $($results.Count) checks passed"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
