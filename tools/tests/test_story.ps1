# Story / quest sync test (both copies must be running in the game): a story flag and a quest state changed on one copy appear on the other, in both directions.
#   test_story.ps1 [-Report story]
param([string]$Report = "story")
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dev = Join-Path $c "tools\dev.ps1"
$results = New-Object System.Collections.ArrayList
function Check($name, [bool]$ok, $detail = "") { [void]$results.Add([pscustomobject]@{ Check = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = "$detail" }); "{0,-5} {1}  {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail }
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; if (-not $id) { return "" }; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
function Ptr($seat, $pattern, $cls) { $m = [regex]::Match((Probe $seat "findall $pattern"), "([0-9A-F]{16}) $cls\S* \(") ; $m.Groups[1].Value }
function Val($seat, $ptr, $prop) { $m = [regex]::Match((Probe $seat "get 0x$ptr $prop"), "= (-?\d+)\s*$", "Multiline"); if ($m.Success) { [int]$m.Groups[1].Value } else { -999 } }
$sh = Ptr 0 "StoryG1R" "StoryG1R_"; $sg = Ptr 1 "StoryG1R" "StoryG1R_"
$qn = "Instance_Quest_BanditsCamp_BANDITSTRUST_BANDITSTRUST_OBJ_BACK"
$qh = Ptr 0 "BANDITSTRUST_OBJ_BACK" "Instance_Quest"; $qg = Ptr 1 "BANDITSTRUST_OBJ_BACK" "Instance_Quest"
Check "setup: story and quest objects found in both copies" ($sh -and $sg -and $qh -and $qg) "$sh $sg $qh $qg"
if (-not ($sh -and $sg -and $qh -and $qg)) { exit 1 }
$orig = Val 0 $sh "Drax_GotBeer"; $v1 = 1 - [Math]::Max(0, $orig)
Probe 0 "set 0x$sh Drax_GotBeer $v1" | Out-Null; Start-Sleep 4
Check "story flag: host change reaches the guest" ((Val 1 $sg "Drax_GotBeer") -eq $v1) "host set $v1, guest $(Val 1 $sg 'Drax_GotBeer')"
Probe 1 "set 0x$sg Drax_GotBeer $orig" | Out-Null; Start-Sleep 4
Check "story flag: guest change reaches the host" ((Val 0 $sh "Drax_GotBeer") -eq $orig) "guest set $orig, host $(Val 0 $sh 'Drax_GotBeer')"
Probe 0 "set 0x$qh State 1" | Out-Null; Start-Sleep 4                  # a defined starting point whatever an earlier run left behind
$q0 = 1
Probe 0 "call 0x$qh Quest StartQuest" | Out-Null; Start-Sleep 4
Check "quest: started on the host, running on the guest" ((Val 1 $qg "State") -eq 2) "host $(Val 0 $qh 'State'), guest $(Val 1 $qg 'State')"
Probe 1 "set 0x$qg State 4" | Out-Null; Start-Sleep 4
Check "quest: finished on the guest, finished on the host" ((Val 0 $qh "State") -eq 4) "guest $(Val 1 $qg 'State'), host $(Val 0 $qh 'State')"
Probe 0 "set 0x$qh State $q0" | Out-Null; Start-Sleep 4                 # put it back
Check "quest: reset on the host is reset on the guest" ((Val 1 $qg "State") -eq $q0) "guest $(Val 1 $qg 'State') (wanted $q0)"
$pass = @($results | Where-Object Result -eq "PASS").Count
$dir = Join-Path $c "docs\reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$md = "# Story sync $(Get-Date -Format 'yyyy-MM-dd HH:mm')`n`n$pass / $($results.Count) checks passed.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/') |`n" }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
"$pass / $($results.Count) checks passed"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
