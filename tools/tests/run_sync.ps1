# Runs the three shared-world sync tests back to back (story / quests, world items, chests); heroes are put back to their start between them.
#   run_sync.ps1 [-Start]     -Start: restart both copies first (lite settings: about 1.5 GB less memory per copy; the watchdog stops everything if the PC runs short of memory) and wait until both are in the game; the start positions are recorded on the first test
param([switch]$Start)
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dev = Join-Path $c "tools\dev.ps1"
if ($Start) {
    & (Join-Path $c "tools\harness.ps1") stop | Out-Null; Start-Sleep 3
    & (Join-Path $c "tools\harness_start.ps1") -Lite | Out-Null
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt 200) { Start-Sleep 5; $o = & $dev probe "frames" 2>&1 | Out-String; if (([regex]::Matches($o, "have_remote=1")).Count -ge 2) { break } }
    Start-Sleep 60                                                           # modules start, the world settles
}
$bad = 0
foreach ($t in "story", "items", "loot", "bodyloot", "chests") {
    "=== $t"
    & (Join-Path $PSScriptRoot "test_$t.ps1") 2>&1 | Where-Object { $_ -match "PASS|FAIL|checks passed|Error" }
    if ($LASTEXITCODE -ne 0) { $bad++ }
}
"sync tests: $(5 - $bad) of 5 passed"
exit $(if ($bad -eq 0) { 0 } else { 1 })
