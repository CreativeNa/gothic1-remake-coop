# Cutscene replay test (both copies running in the game): a ForceStartCinematic call on the cinematic manager of one copy (the call the game scripts make to start a cutscene) is replayed on the
# other copy, and not echoed back. The TEST cinematic is used. The cinematic state is left started: restart the games afterwards.
param([string]$Report = "cine")
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dev = Join-Path $c "tools/dev.ps1"; $log = Join-Path $c "native/build/coopdll.log"
$results = New-Object System.Collections.ArrayList
function Check($name, [bool]$ok, $detail = "") { [void]$results.Add([pscustomobject]@{ Check = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = "$detail" }); "{0,-5} {1}  {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail }
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; if (-not $id) { return "" }; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
if (-not ((PidOf 0) -and (PidOf 1))) { throw "start both copies first" }
function Mgr($seat) { [regex]::Match((Probe $seat "findall GothicCinematicManagerSubsystemScript * 5"), "([0-9A-F]{16}) GothicCinematicManagerSubsystemScript_").Groups[1].Value }
function RunOne($from, $to, $label) {
    $fromPid = PidOf $from; $toPid = PidOf $to; $m = Mgr $from
    Check "$label setup: the cinematic manager exists" ([bool]$m) $m
    if (-not $m) { return }
    $mark = (Get-Content $log | Measure-Object -Line).Lines
    Probe $from "call 0x$m GothicCinematicManagerSubsystem ForceStartCinematic LevelSequence=p:cls:TESTCinematic" | Out-Null
    $seen = @(); $got = @()
    for ($w = 0; $w -lt 20 -and -not ($seen.Count -ge 1 -and $got.Count -ge 1); $w++) {
        Start-Sleep 1; $new = Get-Content $log | Select-Object -Skip $mark
        $seen = @($new | Where-Object { $_ -match "^\[$fromPid\] S cine: ForceStartCinematic TESTCinematic" })
        $got = @($new | Where-Object { $_ -match "^\[$toPid\] S cine: start TESTCinematic replayed here \(ok\)" })
    }
    Check "$label the start is seen on the copy that made it" ($seen.Count -ge 1) ($seen | Select-Object -First 1)
    Check "$label the other copy starts the same cutscene" ($got.Count -ge 1) ($got | Select-Object -First 1)
    Start-Sleep 3; $new = Get-Content $log | Select-Object -Skip $mark
    $echo = @($new | Where-Object { $_ -match "^\[$fromPid\] S cine: start TESTCinematic replayed here" })
    Check "$label no echo back" ($echo.Count -eq 0) ($echo | Select-Object -First 1)
}
RunOne 0 1 "host->guest"
$pass = @($results | Where-Object Result -eq "PASS").Count
$dir = Join-Path $c "docs/reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$md = "# Cutscene replay $(Get-Date -Format 'yyyy-MM-dd HH:mm')`n`n$pass / $($results.Count) checks passed.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/') |`n" }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
"$pass / $($results.Count) checks passed"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
