# Plays the cutscenes in batches of at most 2 per start of the game: heavy scenes load 3+ GB of world data per copy, after three scenes two copies no longer fit into 32 GB, so both copies are
# restarted after every second scene. A batch that is stopped by the memory watcher is repeated scene by scene.
#   run_cutscene_sweep.ps1 [-Scenes A,B,...] [-GuestStart Prophecy,...]
param([string[]]$Scenes = @("Uriziel", "Extro", "ExtroMagic", "OldMineExplosion", "UluMuluPresentation", "SleeperTempleOpening", "FireDemonBloodDoor"), [string[]]$GuestStart = @("Prophecy"))
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$batches = New-Object System.Collections.ArrayList
for ($i = 0; $i -lt $Scenes.Count; $i += 2) { [void]$batches.Add([pscustomobject]@{ Scenes = @($Scenes[$i..([math]::Min($i + 1, $Scenes.Count - 1))]); Guest = @() }) }
if ($GuestStart.Count -gt 0) { [void]$batches.Add([pscustomobject]@{ Scenes = @(); Guest = $GuestStart }) }
$summary = New-Object System.Collections.ArrayList
function StartBoth {
    & (Join-Path $c "tools/harness.ps1") stop | Out-Null; Start-Sleep 4
    & (Join-Path $c "tools/harness_start.ps1") -Lite | Out-Null
    $t0 = Get-Date; $ready = $false
    while (((Get-Date) - $t0).TotalSeconds -lt 360 -and -not $ready) { Start-Sleep 3; $o = & (Join-Path $c "tools/dev.ps1") probe "frames" 2>&1 | Out-String; $ready = (([regex]::Matches($o, "have_remote=1")).Count -ge 2) }
    if ($ready) { Start-Sleep 4 }
    return $ready
}
function RunBatch($scenes, $guest, $report) {
    $mw = Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$c\tools\memwatch.ps1`" -Seconds 900 -MinMB 900" -WindowStyle Hidden -PassThru
    $sc = if ($scenes.Count) { $scenes } else { @("none") }; $gs = if ($guest.Count) { $guest } else { @("none") }
    try { $out = & (Join-Path $c "tools/tests/test_cutscenes.ps1") -Scenes $sc -GuestStarts $gs -Report $report 2>&1 | Out-String } catch { $out = "the test stopped: $($_.Exception.Message)" }          # a game that died ends the test with an error: the sweep goes on
    try { Stop-Process -Id $mw.Id -Force -ErrorAction SilentlyContinue } catch {}
    $mwf = Join-Path $c "native/build/memwatch.txt"; $m = Get-Content $mwf -ErrorAction SilentlyContinue
    $emerg = @($m | Where-Object { $_ -match "EMERGENCY" }).Count -gt 0
    $minav = ($m | ForEach-Object { if ($_ -match "avail (\d+)") { [int]$matches[1] } } | Measure-Object -Minimum).Minimum
    return @{ Out = $out; Emergency = $emerg; MinAvail = $minav }
}
foreach ($b in $batches) {
    $names = (@($b.Scenes) + @($b.Guest | ForEach-Object { "$_ (guest)" })) -join " + "
    "=== batch: $names"
    if (-not (StartBoth)) { [void]$summary.Add("${names}: the copies did not start"); "copies did not start"; continue }
    $r = RunBatch $b.Scenes $b.Guest ("cutscenes_" + (($names -replace '[^A-Za-z0-9]+', '_').Trim('_')))
    ($r.Out -split "`n") | Where-Object { $_ -match "PASS|FAIL" } | ForEach-Object { $_.Trim() }
    $line = (($r.Out -split "`n") | Where-Object { $_ -match "checks passed" } | Select-Object -Last 1)
    [void]$summary.Add("${names}: $line; lowest available memory $($r.MinAvail) MB$(if ($r.Emergency) { '; STOPPED by the memory watcher' })")
    if ($r.Emergency -and ($b.Scenes.Count + $b.Guest.Count) -gt 1) {                              # the pair did not fit: scene by scene, each after a restart
        foreach ($one in @($b.Scenes)) { "--- repeating $one alone"; if (-not (StartBoth)) { continue }; $r1 = RunBatch @($one) @() "cutscene_$one"; ($r1.Out -split "`n") | Where-Object { $_ -match "PASS|FAIL" } | ForEach-Object { $_.Trim() }
            [void]$summary.Add("$one alone: $((($r1.Out -split "`n") | Where-Object { $_ -match 'checks passed' } | Select-Object -Last 1)); lowest available memory $($r1.MinAvail) MB$(if ($r1.Emergency) { '; STOPPED by the memory watcher' })") }
    }
}
& (Join-Path $c "tools/harness.ps1") stop | Out-Null
"=== summary"; $summary
