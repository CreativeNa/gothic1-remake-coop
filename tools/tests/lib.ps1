# Shared helpers for the sync tests (dot-source it after defining Probe): puts both heroes back where they started, so the tests can run back to back.
#   Restore-Heroes      the first call after a fresh start records both hero positions in native\build\test_spawn.txt (harness_start.ps1 deletes the file);
#                       later calls teleport a hero that is more than 800 units away from its recorded position back and give the world time to settle
function Set-TestSafety {                                                     # NPCs ignore the heroes (the game's own "Invisible" cheat, Gothic.IgnorePlayerPerceptions): taking things from chests in front of guards must not get a hero killed; per process, nothing is saved
    foreach ($s in 0, 1) { Probe $s "cc Gothic.IgnorePlayerPerceptions 1" | Out-Null }
}
function Restore-Heroes {
    Set-TestSafety
    $c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    $f = Join-Path $c "native\build\test_spawn.txt"
    $pos = @{}
    foreach ($s in 0, 1) { $m = [regex]::Match((Probe $s "heroinfo"), "HEROINFO pos (-?\d+) (-?\d+) (-?\d+)"); if ($m.Success) { $pos[$s] = @([int]$m.Groups[1].Value, [int]$m.Groups[2].Value, [int]$m.Groups[3].Value) } }
    if ($pos.Count -lt 2) { return }
    if (-not (Test-Path $f)) { Set-Content $f ("{0} {1} {2} {3} {4} {5}" -f $pos[0][0], $pos[0][1], $pos[0][2], $pos[1][0], $pos[1][1], $pos[1][2]); return }
    $v = (Get-Content $f -TotalCount 1) -split " " | ForEach-Object { [int]$_ }
    $moved = $false
    foreach ($s in 0, 1) {
        $b = $s * 3; $dx = $pos[$s][0] - $v[$b]; $dy = $pos[$s][1] - $v[$b + 1]
        if ([math]::Sqrt($dx * $dx + $dy * $dy) -gt 800) { Probe $s "tp hero $($v[$b]) $($v[$b + 1]) $($v[$b + 2] + 60)" | Out-Null; $moved = $true }
    }
    if ($moved) { Start-Sleep 12 }
}
