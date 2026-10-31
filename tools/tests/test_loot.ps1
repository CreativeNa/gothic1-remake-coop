# Chest loot replay test (both copies running in the game): when the game takes an item out of a chest on one copy (Server_ConsumeItem on the chest's container module),
# the same call is replayed on the same chest in the other copy, and not echoed back. The call is made through the probe, the way the game makes it when a player takes an item.
#   test_loot.ps1 [-Report loot]
param([string]$Report = "loot")
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dev = Join-Path $c "tools\dev.ps1"; $log = Join-Path $c "native\build\coopdll.log"
$results = New-Object System.Collections.ArrayList
function Check($name, [bool]$ok, $detail = "") { [void]$results.Add([pscustomobject]@{ Check = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = "$detail" }); "{0,-5} {1}  {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail }
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; if (-not $id) { return "" }; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
. (Join-Path $PSScriptRoot "lib.ps1")
function Hero($seat) { [regex]::Match((Probe $seat "frames"), "hero=([0-9A-F]+)").Groups[1].Value }
if (-not ((PidOf 0) -and (PidOf 1))) { throw "start both copies first" }
Restore-Heroes
function RunOne($from, $to, $label, $itemId) {
    $m = [regex]::Match((Probe $from "ioscan Chest 1"), "([0-9A-F]{16}) \S+ \(")
    Check "$label setup: a chest is loaded" $m.Success
    if (-not $m.Success) { return }
    $ptr = $m.Groups[1].Value
    $t = Probe $from "tpto 0x$(Hero $from) 0x$ptr 0 -130 40"; $pos = [regex]::Match($t, "target at (-?\d+) (-?\d+) (-?\d+)")
    if (-not $pos.Success) { Check "$label setup: hero next to the chest" $false; return }
    Probe $to "tp hero $($pos.Groups[1].Value) $([int]$pos.Groups[2].Value - 130) $([int]$pos.Groups[3].Value + 40)" | Out-Null
    Start-Sleep 50
    $mod = [regex]::Match((Probe $from "contdump 0x$ptr"), "container ([0-9A-F]{16})").Groups[1].Value
    Check "$label setup: the chest has a container module" ([bool]$mod) $mod
    if (-not $mod) { return }
    $mark = (Get-Content $log | Measure-Object -Line).Lines
    Probe $from "call 0x$mod DataModule_Container Server_ConsumeItem InventoryType=i:1 ID=i:$itemId ItemAmount=i:1" | Out-Null      # what the game does when a player takes the item
    $sent = @(); $got = @()
    for ($w = 0; $w -lt 20 -and -not ($sent.Count -ge 1 -and $got.Count -ge 1); $w++) {
        Start-Sleep 1; $new = Get-Content $log | Select-Object -Skip $mark
        $sent = @($new | Where-Object { $_ -match "^\[$(PidOf $from)\] S loot: chest .* item id $itemId x1 taken here" })
        $got = @($new | Where-Object { $_ -match "^\[$(PidOf $to)\] S loot: chest .* item id $itemId x1 taken by the other player \(replay ok\)" })
    }
    Check "$label the take is noticed on the copy that made it (once)" ($sent.Count -eq 1) ($sent | Select-Object -First 1)
    Check "$label the other copy replays it on the same chest" ($got.Count -ge 1) ($got | Select-Object -First 1)
    Start-Sleep 4; $new = Get-Content $log | Select-Object -Skip $mark
    $echo = @($new | Where-Object { $_ -match "^\[$(PidOf $to)\] S loot: chest .* item id $itemId x1 taken here" -or $_ -match "^\[$(PidOf $from)\] S loot: chest .* item id $itemId x1 taken by the other player" })
    Check "$label no echo back" ($echo.Count -eq 0) ($echo | Select-Object -First 1)
}
RunOne 0 1 "host->guest" 7
RunOne 1 0 "guest->host" 8
Restore-Heroes
$pass = @($results | Where-Object Result -eq "PASS").Count
$dir = Join-Path $c "docs\reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$md = "# Chest loot replay $(Get-Date -Format 'yyyy-MM-dd HH:mm')`n`n$pass / $($results.Count) checks passed.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/') |`n" }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
"$pass / $($results.Count) checks passed"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
