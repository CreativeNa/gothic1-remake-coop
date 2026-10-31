# World item sync test (both copies running in the game): an item that disappears next to the host's hero (a pickup) is removed in the guest's world too, and the other way round.
#   test_items.ps1 [-Report items]
param([string]$Report = "items")
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dev = Join-Path $c "tools\dev.ps1"; $log = Join-Path $c "native\build\coopdll.log"
$results = New-Object System.Collections.ArrayList
function Check($name, [bool]$ok, $detail = "") { [void]$results.Add([pscustomobject]@{ Check = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = "$detail" }); "{0,-5} {1}  {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail }
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; if (-not $id) { return "" }; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
function Hero($seat) { [regex]::Match((Probe $seat "frames"), "hero=([0-9A-F]+)").Groups[1].Value }
$hp = PidOf 0; $gp = PidOf 1
if (-not ($hp -and $gp)) { throw "start both copies first" }
function RunOne($from, $to, $label, $item) {
    $mark = (Get-Content $log | Measure-Object -Line).Lines
    $m = [regex]::Match((Probe $from "ioscan $item 1"), "([0-9A-F]{16}) ")
    if (-not $m.Success) { Check "$label setup: an item of this kind is loaded" $false "none"; return }
    $t = Probe $from "tpto 0x$(Hero $from) 0x$($m.Groups[1].Value) 150 0 80"; $pos = [regex]::Match($t, "target at (-?\d+) (-?\d+) (-?\d+)")
    Check "$label setup: hero next to the item" $pos.Success $pos.Value
    if (-not $pos.Success) { return }
    $x = [int]$pos.Groups[1].Value; $y = [int]$pos.Groups[2].Value; $z = [int]$pos.Groups[3].Value
    Probe $to "tp hero $x $($y + 150) $($z + 80)" | Out-Null
    Start-Sleep 45                                                              # the world settles (15 s) and the item list is built (a slice of the object table per second); what the game removes by itself at the teleport is over by then
    $best = $null; $bd = 1e9                                                     # now the item closest to the hero that still exists
    foreach ($mm in [regex]::Matches((Probe $from "ioscan $item 40"), "([0-9A-F]{16}) \S+ \(\S+\) pos (-?\d+) (-?\d+) (-?\d+)")) {
        $d = [math]::Sqrt([math]::Pow([int]$mm.Groups[2].Value - $x, 2) + [math]::Pow([int]$mm.Groups[3].Value - $y, 2)); if ($d -lt $bd) { $bd = $d; $best = $mm.Groups[1].Value }
    }
    Check "$label an item is still lying within 450 units of the hero" ($best -and $bd -lt 450) ("{0:N0} units" -f $bd)
    if (-not ($best -and $bd -lt 450)) { return }
    $mark = (Get-Content $log | Measure-Object -Line).Lines
    Probe $from "call 0x$best Actor K2_DestroyActor" | Out-Null                  # what a pickup does to the actor
    Start-Sleep 6
    $new = Get-Content $log | Select-Object -Skip $mark
    $sent = @($new | Where-Object { $_ -match "^\[$(PidOf $from)\] S item .* picked up here" })
    $got = @($new | Where-Object { $_ -match "^\[$(PidOf $to)\] S item .* removed" })
    Check "$label the pickup is noticed on the picking copy" ($sent.Count -eq 1) ($sent | Select-Object -First 1)
    Check "$label the other copy removes the same item" ($got.Count -ge 1) ($got | Select-Object -First 1)
}
RunOne 0 1 "host->guest" "Mushroom"
RunOne 1 0 "guest->host" "Brush"
$pass = @($results | Where-Object Result -eq "PASS").Count
$dir = Join-Path $c "docs\reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$md = "# World item sync $(Get-Date -Format 'yyyy-MM-dd HH:mm')`n`n$pass / $($results.Count) checks passed.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/') |`n" }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
"$pass / $($results.Count) checks passed"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
