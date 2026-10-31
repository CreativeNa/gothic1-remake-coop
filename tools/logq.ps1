# Query native\build\coopdll.log by seat, tag and time. Every log line is "[pid] TAG text"; the seat of a pid comes from the Couchlink / harness helper logs.
#   logq.ps1 -Summary                      per seat: lines per tag, crashes, exceptions, last fps (of the newest run only)
#   logq.ps1 -Seat 1 -Tag N -Last 40       the last 40 NPC lines of the guest
#   logq.ps1 -Grep "bone-driven" -Seat 1   free text filter
#   logq.ps1 -Crashes                      every access violation that was in OUR code (module / call), and in-game crashes of the game thread
param([int]$Seat = -1, [string]$Tag = "", [string]$Grep = "", [int]$Last = 60, [switch]$Summary, [switch]$Crashes, [switch]$AllRuns)
$c = Split-Path $PSScriptRoot -Parent
$log = Join-Path $c "native\build\coopdll.log"
$map = @{}; $newest = @()                           # pid -> seat; the newest pid of each seat is the last "game pid" line of its helper log
foreach ($s in 0, 1) {
    $f = Join-Path $c "native\build\couchlink_seat$s.log"
    if (Test-Path $f) { $last = $null; Get-Content $f | ForEach-Object { if ($_ -match "game pid (\d+)") { $map[[int]$matches[1]] = $s; $last = [int]$matches[1] } }; if ($last) { $newest += $last } }
}
$lines = Get-Content $log
if (-not $AllRuns -and $newest) {                       # newest run only: from the first line of the newest pids
    $first = ($lines | Select-String -Pattern ("^\[(" + ($newest -join "|") + ")\] ") | Select-Object -First 1).LineNumber
    if ($first) { $lines = $lines[($first - 1)..($lines.Count - 1)] }
}
function Parse($l) { if ($l -match '^\[(\d+)\]\s+(\S+)\s*(.*)$') { [pscustomobject]@{ Pid = [int]$matches[1]; Tag = $matches[2]; Text = $matches[3]; Seat = $(if ($map.ContainsKey([int]$matches[1])) { $map[[int]$matches[1]] } else { -1 }); Raw = $l } } }
$rows = $lines | ForEach-Object { Parse $_ } | Where-Object { $_ }
if ($Summary) {
    foreach ($s in 0, 1) {
        $r = $rows | Where-Object { $_.Seat -eq $s }
        "== seat $s ($($r.Count) lines)"
        $r | Group-Object Tag | Sort-Object Count -Descending | Select-Object -First 12 | ForEach-Object { "   {0,-8} {1}" -f $_.Name, $_.Count }
        $av = $r | Where-Object { $_.Tag -eq "AV" -and $_.Text -match "^at " }
        "   access violations: $($av.Count)   (in our code: $(($r | Where-Object { $_.Text -match 'our code at that moment: call=(?!none)|module=(?!none)' }).Count))"
        "   exceptions caught in our calls: $(($r | Where-Object { $_.Tag -eq 'EXCEPTION' }).Count)"
        $fps = $r | Where-Object { $_.Tag -eq "PR" -and $_.Text -match "^fps ([\d.]+)" } | Select-Object -Last 1
        if ($fps) { "   last: $($fps.Text)" }
    }
    return
}
if ($Crashes) {
    $rows | Where-Object { ($_.Tag -eq "AV" -and $_.Text -match "our code at that moment: call=(?!none)|module=(?!none)") -or $_.Tag -eq "EXCEPTION" -or ($_.Tag -eq "AV" -and $_.Text -match "GAME thread") } | ForEach-Object { "seat {0} [{1}] {2}" -f $_.Seat, $_.Pid, $_.Raw.Substring($_.Raw.IndexOf("]") + 2) } | Select-Object -Last $Last
    return
}
$sel = $rows
if ($Seat -ge 0) { $sel = $sel | Where-Object { $_.Seat -eq $Seat } }
if ($Tag) { $sel = $sel | Where-Object { $_.Tag -eq $Tag } }
if ($Grep) { $sel = $sel | Where-Object { $_.Raw -match $Grep } }
$sel | Select-Object -Last $Last | ForEach-Object { "s{0} {1}" -f $_.Seat, $_.Raw.Substring($_.Raw.IndexOf("]") + 2) }
