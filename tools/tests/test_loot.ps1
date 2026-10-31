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
$flag = Join-Path $c "native\build\scan_fast.txt"; Set-Content $flag "1"                  # whole-world item scan in ~2 s instead of 15 s while the test runs
function ChestPtr($seat) { [regex]::Match((Probe $seat "ioscan Chest 80"), "([0-9A-F]{16}) Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755").Groups[1].Value }
function RunOne($from, $to, $label, $itemId) {
    $fromPid = PidOf $from; $toPid = PidOf $to                                        # once: PidOf inside the log filters ran a process query per log line (minutes)
    $pf = ""; $pt = ""; for ($w = 0; $w -lt 20 -and -not ($pf -and $pt); $w++) { $pf = ChestPtr $from; $pt = ChestPtr $to; if (-not ($pf -and $pt)) { Start-Sleep 2 } }     # the chest has to be loaded in both copies                                    # a chest whose items have the ids 1, 2 and 3 after a fresh start (seen when a player took them)
    Check "$label setup: the same chest is loaded in both copies" ([bool]($pf -and $pt)) "$pf $pt"
    if (-not ($pf -and $pt)) { return }
    Probe $from "tpto 0x$(Hero $from) 0x$pf 0 -130 40" | Out-Null; Probe $to "tpto 0x$(Hero $to) 0x$pt 0 -130 40" | Out-Null      # both heroes stand in front of the chest
    $cn = "Interactive_Chest_Right_Variant_C_UAID_E89C256C6855A5DA02_1705760755"; $tracked = $false                  # wait (up to 40 s) until both copies track the chest
    for ($w = 0; $w -lt 40 -and -not $tracked; $w++) {
        $all = Get-Content $log
        $tracked = (@($all | Where-Object { $_ -match "^\[$fromPid\] S chest tracked: $cn," }).Count -ge 1) -and (@($all | Where-Object { $_ -match "^\[$toPid\] S chest tracked: $cn," }).Count -ge 1)
        if (-not $tracked) { Start-Sleep 1 }
    }
    $mod = [regex]::Match((Probe $from "contdump 0x$pf"), "container ([0-9A-F]{16})").Groups[1].Value
    Check "$label setup: the chest has a container module" ([bool]$mod) $mod
    if (-not $mod) { return }
    $mark = (Get-Content $log | Measure-Object -Line).Lines
    Probe $from "call 0x$mod DataModule_Container Server_ConsumeItem InventoryType=i:1 ID=i:$itemId ItemAmount=i:1" | Out-Null      # what the game does when a player takes the item
    $sent = @(); $got = @()
    for ($w = 0; $w -lt 15 -and -not ($sent.Count -ge 1 -and $got.Count -ge 1); $w++) {
        Start-Sleep 1; $new = Get-Content $log | Select-Object -Skip $mark
        $sent = @($new | Where-Object { $_ -match "^\[$fromPid\] S loot: chest .* item id $itemId x1 taken here" })
        $got = @($new | Where-Object { $_ -match "^\[$toPid\] S loot: chest .* item id $itemId x1 taken by the other player \(replay ok\)" })
    }
    Check "$label the take is noticed on the copy that made it (once)" ($sent.Count -eq 1) ($sent | Select-Object -First 1)
    Check "$label the other copy replays it on the same chest" ($got.Count -ge 1) ($got | Select-Object -First 1)
    Start-Sleep 3; $new = Get-Content $log | Select-Object -Skip $mark
    $echo = @($new | Where-Object { $_ -match "^\[$toPid\] S loot: chest .* item id $itemId x1 taken here" -or $_ -match "^\[$fromPid\] S loot: chest .* item id $itemId x1 taken by the other player" })
    Check "$label no echo back" ($echo.Count -eq 0) ($echo | Select-Object -First 1)
}
RunOne 0 1 "host->guest" 1
RunOne 1 0 "guest->host" 2
Remove-Item $flag -ErrorAction SilentlyContinue
Restore-Heroes
$pass = @($results | Where-Object Result -eq "PASS").Count
$dir = Join-Path $c "docs\reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$md = "# Chest loot replay $(Get-Date -Format 'yyyy-MM-dd HH:mm')`n`n$pass / $($results.Count) checks passed.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/') |`n" }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
"$pass / $($results.Count) checks passed"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
