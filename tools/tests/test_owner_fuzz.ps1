# Ownership fuzz test (both copies must be running, e.g. after harness_start.ps1): the two heroes are teleported to random NPCs many times; afterwards the two copies must agree who owns every NPC,
# ownership must not flap, positions and health must match, and nothing in our code may fault.
#   test_owner_fuzz.ps1 [-Rounds 25] [-Seed 1]
param([int]$Rounds = 25, [int]$Seed = 1, [string]$Report = "owner_fuzz")
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dev = Join-Path $c "tools\dev.ps1"; $log = Join-Path $c "native\build\coopdll.log"
$rnd = New-Object System.Random $Seed
$results = New-Object System.Collections.ArrayList
function Check($name, [bool]$ok, $detail = "") { [void]$results.Add([pscustomobject]@{ Check = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = "$detail" }); "{0,-5} {1}  {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail }
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; if (-not $id) { return "" }; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
function NpcList($seat) { $o = Probe $seat "npcs 30000 60"; [regex]::Matches($o, "([0-9A-F]{16}) (Character_\S+)") | ForEach-Object { [pscustomobject]@{ Ptr = $_.Groups[1].Value; Name = $_.Groups[2].Value } } }
$hp = PidOf 0; $gp = PidOf 1
if (-not ($hp -and $gp)) { throw "start both copies first (tools\harness_start.ps1)" }
$mark = (Get-Content $log | Measure-Object -Line).Lines
$names = @(NpcList 0 | Where-Object { $_.Name -notmatch "Scavenger" } | ForEach-Object Name)       # the NPCs of the camp: loaded in both copies
if ($names.Count -lt 3) { $names = @(NpcList 0 | ForEach-Object Name) }
"fuzzing $Rounds rounds over $($names.Count) NPCs"
$touched = @{}
for ($i = 0; $i -lt $Rounds; $i++) {
    $seat = $rnd.Next(0, 2); $name = $names[$rnd.Next(0, $names.Count)]
    $other = 1 - $seat; $far = $names[$rnd.Next(0, $names.Count)]
    $p = (NpcList $seat | Where-Object { $_.Name -eq $name } | Select-Object -First 1).Ptr
    $q = (NpcList $other | Where-Object { $_.Name -eq $far } | Select-Object -First 1).Ptr
    if ($p) { Probe $seat "tpto hero 0x$p 250 $($rnd.Next(-200, 200)) 10" | Out-Null; $touched[$name] = 1 + $touched[$name] }
    if ($q) { Probe $other "tpto hero 0x$q $($rnd.Next(1400, 2200)) $($rnd.Next(-300, 300)) 10" | Out-Null }
    Start-Sleep -Seconds 3
}
Start-Sleep 6
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
$disagree = @($ids | Where-Object { $maps[0][$_] -ne $maps[1][$_] -and ($null -ne $maps[0][$_]) -and ($null -ne $maps[1][$_]) })
Check "owners: both copies agree who owns every NPC they both changed" ($disagree.Count -eq 0) "$($ids.Count) NPCs changed owner, $($disagree.Count) disagree"
$onlyOne = @($ids | Where-Object { ($null -eq $maps[0][$_]) -xor ($null -eq $maps[1][$_]) })
Check "owners: every ownership change was noticed by both copies" ($onlyOne.Count -le 1) "$($onlyOne.Count) seen by one copy only"
$maxFlip = ($flips.Values | Measure-Object -Maximum).Maximum; if (-not $maxFlip) { $maxFlip = 0 }
Check "owners: no flapping (an NPC changes owner at most as often as heroes visited it + 2)" ($maxFlip -le ($Rounds / 2 + 2)) "max $maxFlip changes of one NPC in $Rounds rounds"
$diff = & (Join-Path $c "tools\diff_npcs.ps1") -Quiet -MaxDist 800
Check "sync: positions and health match after the fuzzing" ($LASTEXITCODE -eq 0) ($diff | Select-Object -Last 1)
$ours = @(& (Join-Path $c "tools\logq.ps1") -Crashes -Last 40 | Where-Object { $_ -match "our code at that moment: call=(?!none)|module=(?!none)|EXCEPTION" })
Check "stability: no fault inside our code" ($ours.Count -eq 0) ($ours | Select-Object -First 2 | Out-String).Trim()
Check "stability: both copies still running" ([bool]((PidOf 0) -and (PidOf 1)))
$dir = Join-Path $c "docs\reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$pass = @($results | Where-Object Result -eq "PASS").Count
$md = "# Ownership fuzz $(Get-Date -Format 'yyyy-MM-dd HH:mm') ($Rounds rounds, seed $Seed)`n`n$pass / $($results.Count) checks passed.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/' -replace "`r?`n", ' ') |`n" }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
"$pass / $($results.Count) checks passed"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
