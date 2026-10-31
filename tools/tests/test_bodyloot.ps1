# Body loot replay test (both copies running in the game): when the game takes an item out of an NPC's inventory on one copy (Server_ConsumeItem on the NPC's container module),
# the same call is replayed on the same NPC in the other copy, and not echoed back. The call is made through the probe, the way the game makes it when a player takes the item.
# The item ids of an NPC are not known: ids 1..5 are tried until one is replayed successfully.
param([string]$Report = "bodyloot")
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dev = Join-Path $c "tools/dev.ps1"; $log = Join-Path $c "native/build/coopdll.log"
$results = New-Object System.Collections.ArrayList
function Check($name, [bool]$ok, $detail = "") { [void]$results.Add([pscustomobject]@{ Check = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = "$detail" }); "{0,-5} {1}  {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail }
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; if (-not $id) { return "" }; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
. (Join-Path $PSScriptRoot "lib.ps1")
if (-not ((PidOf 0) -and (PidOf 1))) { throw "start both copies first" }
Restore-Heroes
function RunOne($from, $to, $label) {
    $fromPid = PidOf $from; $toPid = PidOf $to
    $hp = [regex]::Match((Probe $from "heroinfo"), "HEROINFO pos (-?\d+) (-?\d+)"); $hx = [int]$hp.Groups[1].Value; $hy = [int]$hp.Groups[2].Value
    $rx = "BODY ([0-9A-F]{16}) (\S+) module ([0-9A-F]{16}) pos (-?\d+) (-?\d+)"
    $other = @{}; foreach ($b in [regex]::Matches((Probe $to "bodylist 3500 80"), $rx)) { $other[$b.Groups[2].Value] = 1 }
    $best = $null; $bd = 1e9
    foreach ($b in [regex]::Matches((Probe $from "bodylist 3500 80"), $rx)) {
        if (-not $other[$b.Groups[2].Value]) { continue }
        $d = [math]::Sqrt([math]::Pow([int]$b.Groups[4].Value - $hx, 2) + [math]::Pow([int]$b.Groups[5].Value - $hy, 2)); if ($d -lt $bd) { $bd = $d; $best = $b }
    }
    Check "$label setup: an NPC with an inventory exists in both copies" ([bool]$best) $(if ($best) { $best.Groups[2].Value } else { "none" })
    if (-not $best) { return }
    $name = $best.Groups[2].Value; $mod = $best.Groups[3].Value
    $tracked = $false                                                          # wait (up to 40 s) until both copies track this NPC
    for ($w = 0; $w -lt 40 -and -not $tracked; $w++) {
        $all = Get-Content $log
        $tracked = (@($all | Where-Object { $_ -match "^\[$fromPid\] S body tracked: $([regex]::Escape($name))$" }).Count -ge 1) -and (@($all | Where-Object { $_ -match "^\[$toPid\] S body tracked: $([regex]::Escape($name))$" }).Count -ge 1)
        if (-not $tracked) { Start-Sleep 1 }
    }
    Check "$label the NPC is tracked in both copies" $tracked $name
    if (-not $tracked) { return }
    $got = @(); $sent = @(); $okId = 0
    for ($id = 1; $id -le 5 -and -not $okId; $id++) {
        $mark = (Get-Content $log | Measure-Object -Line).Lines
        Probe $from "call 0x$mod DataModule_Container Server_ConsumeItem InventoryType=i:1 ID=i:$id ItemAmount=i:1" | Out-Null
        for ($w = 0; $w -lt 8; $w++) {
            Start-Sleep 1; $new = Get-Content $log | Select-Object -Skip $mark
            $sent = @($new | Where-Object { $_ -match "^\[$fromPid\] S loot: chest $([regex]::Escape($name)), item id $id x1 taken here" })
            $got = @($new | Where-Object { $_ -match "^\[$toPid\] S loot: chest $([regex]::Escape($name)), item id $id x1 taken by the other player \(replay ok\)" })
            if ($sent.Count -ge 1 -and $got.Count -ge 1) { $okId = $id; break }
        }
    }
    Check "$label the take is noticed on the copy that made it (once)" ($sent.Count -eq 1) ($sent | Select-Object -First 1)
    Check "$label the other copy replays it on the same NPC" ($got.Count -ge 1) ($got | Select-Object -First 1)
    Start-Sleep 3; $new = Get-Content $log | Select-Object -Skip $mark
    $echo = @($new | Where-Object { $_ -match "^\[$toPid\] S loot: chest $([regex]::Escape($name)), .* taken here" -or $_ -match "^\[$fromPid\] S loot: chest $([regex]::Escape($name)), .* taken by the other player" })
    Check "$label no echo back" ($echo.Count -eq 0) ($echo | Select-Object -First 1)
}
RunOne 0 1 "host->guest"
RunOne 1 0 "guest->host"
Restore-Heroes
$pass = @($results | Where-Object Result -eq "PASS").Count
$dir = Join-Path $c "docs/reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$md = "# Body loot replay $(Get-Date -Format 'yyyy-MM-dd HH:mm')`n`n$pass / $($results.Count) checks passed.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/') |`n" }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
"$pass / $($results.Count) checks passed"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
