# Plays every cutscene on both copies (needs both copies running in the game): the scene is started on one copy with the game's own call (ForceStartCinematic), the test waits until the cinematic
# manager of BOTH copies plays it (state != 0 and a cinematic object), takes a picture of both windows, skips it and waits until both are back to state 0. Memory, crashes and the running
# copies are checked after every scene. Report: docs/reports/cutscenes.md, pictures: docs/img/cutscenes/.
#   test_cutscenes.ps1 [-Scenes A,B,...] [-GuestStarts Prophecy,...]
param([string[]]$Scenes = @("IntroPt2", "Prophecy", "FireMages", "Uriziel", "Extro", "ExtroMagic", "OldMineExplosion", "UluMuluPresentation", "SleeperTempleOpening", "FireDemonBloodDoor"), [string[]]$GuestStarts = @("Prophecy", "FireMages"), [string]$Report = "cutscenes")
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$GuestStarts = @($GuestStarts | Where-Object { $_ -and $_ -ne "none" })          # -GuestStarts none: no scene is started from the guest side
$dev = Join-Path $c "tools/dev.ps1"; $log = Join-Path $c "native/build/coopdll.log"; $img = Join-Path $c "docs/img/cutscenes"; New-Item -ItemType Directory -Force $img | Out-Null
$results = New-Object System.Collections.ArrayList
function Check($name, [bool]$ok, $detail = "") { [void]$results.Add([pscustomobject]@{ Check = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = "$detail" }); Write-Host ("{0,-5} {1}  {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail) }
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; if (-not $id) { return "" }; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
. (Join-Path $PSScriptRoot "lib.ps1")
if (-not ((PidOf 0) -and (PidOf 1))) { throw "start both copies first" }
function Mgr($seat) { [regex]::Match((Probe $seat "findall GothicCinematicManagerSubsystemScript * 5"), "([0-9A-F]{16}) GothicCinematicManagerSubsystemScript_").Groups[1].Value }
function State($seat, $m) { if (-not (PidOf $seat)) { return @{ State = -1; Obj = $false } } $o = Probe $seat "get 0x$m m_CurrentCinematicState"; $s = [regex]::Match($o, "= (\d+)").Groups[1].Value; $cn = Probe $seat "get 0x$m m_Cinematic"; $has = ($cn -notmatch "= null"); if ($s -eq "") { return @{ State = -1; Obj = $false } }; return @{ State = [int]$s; Obj = $has } }
function Reset($seat, $m) { Probe $seat "set 0x$m m_CurrentCinematicState 0" | Out-Null; Probe $seat "set 0x$m m_RequestCinematicState 0" | Out-Null }
function Shot($seat, $name) { $f = (& (Join-Path $c "tools/winshot.ps1") -Seat $seat 2>&1 | Select-Object -Last 1); if ($f -and (Test-Path $f)) { Copy-Item $f (Join-Path $img "$name.png") -Force } }
function Avail { [int](Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory).AvailableMBytes }
$ok3 = 0; for ($w = 0; $w -lt 60 -and $ok3 -lt 2; $w++) { if ((Avail) -gt 4000) { $ok3++ } else { $ok3 = 0 }; Start-Sleep 2 }           # a copy that is still loading its world needs several GB more: wait until the memory has settled
$m0 = Mgr 0; $m1 = Mgr 1
Check "setup: both copies have a cinematic manager" ([bool]($m0 -and $m1)) "$m0 $m1"
if (-not ($m0 -and $m1)) { exit 1 }
$mgr = @{ 0 = $m0; 1 = $m1 }
$avMark = (Get-Content $log | Where-Object { $_ -match "AV at" } | Measure-Object).Count
function PlayScene($scene, $from) {
    $to = 1 - $from; $cls = "${scene}Cinematic"
    Reset 0 $m0; Reset 1 $m1; Start-Sleep 1
    Probe $from "call 0x$($mgr[$from]) GothicCinematicManagerSubsystem ForceStartCinematic LevelSequence=p:cls:$cls" | Out-Null
    $pf = $false; $pt = $false; $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt 60 -and -not ($pf -and $pt)) {
        Start-Sleep 3
        $a = State $from $mgr[$from]; $b = State $to $mgr[$to]
        $pf = ($a.State -ge 2 -and $a.Obj); $pt = ($b.State -ge 2 -and $b.Obj)
    }
    $label = "$scene (started on the $(if ($from -eq 0) { 'host' } else { 'guest' }))"
    Check "$label plays on the copy that started it" $pf ("{0:N0} s" -f ((Get-Date) - $t0).TotalSeconds)
    Check "$label plays on the other copy as well" $pt
    if ($pf -or $pt) { Start-Sleep 3; Shot 0 "${scene}_from$from`_seat0"; Shot 1 "${scene}_from$from`_seat1" }
    $mem = Avail
    Probe $from "call 0x$($mgr[$from]) GothicCinematicManagerSubsystem Server_OnSkipCutsceneRequest PlayerId=i:0" | Out-Null
    $done = $false; $t1 = Get-Date
    while (((Get-Date) - $t1).TotalSeconds -lt 30 -and -not $done) { Start-Sleep 3; $a = State 0 $m0; $b = State 1 $m1; $done = ($a.State -eq 0 -and $b.State -eq 0) }
    Check "$label skipping it on one copy ends it on both" $done
    if (-not $done) { Reset 0 $m0; Reset 1 $m1 }
    $alive = [bool]((PidOf 0) -and (PidOf 1))
    $avNow = (Get-Content $log | Where-Object { $_ -match "AV at" } | Measure-Object).Count
    Check "$label both copies still running, available memory $mem MB" $alive
    if (-not $alive) { return $false }
    return $true
}
foreach ($sc in $Scenes) { if (-not (PlayScene $sc 0)) { break }; Restore-Heroes }
foreach ($sc in $GuestStarts) { if (-not ((PidOf 0) -and (PidOf 1))) { break }; if (-not (PlayScene $sc 1)) { break }; Restore-Heroes }
Start-Sleep 12
$alive = [bool]((PidOf 0) -and (PidOf 1))
Check "stability: both copies still running 12 s after the last scene" $alive "available $(Avail) MB"
$avNew = (Get-Content $log | Where-Object { $_ -match "AV at" } | Measure-Object).Count - $avMark
$crash = @(Get-Content $log | Where-Object { $_ -match "AV at G1R-Win64-Shipping.exe\+0x5be3977" })
Check "stability: no crash at the audio predicate (exe+0x5be3977)" ($crash.Count -eq 0) "$($crash.Count) faults (caught or not); $avNew access violations logged in all"
$pass = @($results | Where-Object Result -eq "PASS").Count
$dir = Join-Path $c "docs/reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$md = "# Cutscenes on two copies $(Get-Date -Format 'yyyy-MM-dd HH:mm')`n`n$pass / $($results.Count) checks passed. Pictures of both windows during every scene: ``docs/img/cutscenes/``.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/') |`n" }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
"$pass / $($results.Count) checks passed"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
