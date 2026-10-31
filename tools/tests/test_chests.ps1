# Chest sync test (both copies running in the game): a chest emptied next to one hero is empty in the other copy too, in both directions.
#   test_chests.ps1 [-Report chests]
param([string]$Report = "chests")
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dev = Join-Path $c "tools\dev.ps1"; $log = Join-Path $c "native\build\coopdll.log"
$results = New-Object System.Collections.ArrayList
function Check($name, [bool]$ok, $detail = "") { [void]$results.Add([pscustomobject]@{ Check = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = "$detail" }); "{0,-5} {1}  {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail }
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; if (-not $id) { return "" }; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
. (Join-Path $PSScriptRoot "lib.ps1")
$fastFlag = Join-Path $c "native/build/scan_fast.txt"; Set-Content $fastFlag "1"          # whole-world item scan in ~2 s instead of 15 s while the test runs
if ((PidOf 0) -and (PidOf 1)) { Restore-Heroes }
function Hero($seat) { [regex]::Match((Probe $seat "frames"), "hero=([0-9A-F]+)").Groups[1].Value }
if (-not ((PidOf 0) -and (PidOf 1))) { throw "start both copies first" }
$skip = @{}
function RunOne($from, $to, $label) {
    $fromPid = PidOf $from; $toPid = PidOf $to                                        # once: PidOf inside the log filters ran a process query per log line (minutes)
    $ptr = $null; $name = ""
    $hp = [regex]::Match((Probe $from "heroinfo"), "HEROINFO pos (-?\d+) (-?\d+)"); $hx = [int]$hp.Groups[1].Value; $hy = [int]$hp.Groups[2].Value; $bd0 = 1e9
    foreach ($m in [regex]::Matches((Probe $from "chestlist 200 1"), "CHEST ([0-9A-F]{16}) (\S+) items \d+ \+ \d+ pos (-?\d+) (-?\d+)")) {          # one call: every chest that has something inside; the one nearest to the hero (loaded in both copies)
        if ($skip[$m.Groups[1].Value]) { continue }
        $d0 = [math]::Sqrt([math]::Pow([int]$m.Groups[3].Value - $hx, 2) + [math]::Pow([int]$m.Groups[4].Value - $hy, 2)); if ($d0 -lt $bd0) { $bd0 = $d0; $ptr = $m.Groups[1].Value; $name = $m.Groups[2].Value }
    }
    Check "$label setup: a chest with something inside is loaded" ([bool]$ptr) $ptr
    if (-not $ptr) { return }
    $skip[$ptr] = 1
    $t = Probe $from "tpto 0x$(Hero $from) 0x$ptr 200 0 80"; $pos = [regex]::Match($t, "target at (-?\d+) (-?\d+) (-?\d+)")
    if (-not $pos.Success) { Check "$label setup: hero next to the chest" $false; return }
    Probe $to "tp hero $($pos.Groups[1].Value) $([int]$pos.Groups[2].Value + 150) $([int]$pos.Groups[3].Value + 80)" | Out-Null
    $mark0 = (Get-Content $log | Measure-Object -Line).Lines
    $tracked = $false                                                          # wait until BOTH copies track this chest (loaded and found by the item scan)
    for ($w = 0; $w -lt 40 -and -not $tracked; $w++) {
        Start-Sleep 1; $n0 = Get-Content $log                                       # the whole log: a chest that was already tracked earlier is not logged again
        $tracked = (@($n0 | Where-Object { $_ -match "^\[$fromPid\] S chest tracked: $([regex]::Escape($name))," }).Count -ge 1) -and (@($n0 | Where-Object { $_ -match "^\[$toPid\] S chest tracked: $([regex]::Escape($name))," }).Count -ge 1)
    }
    Check "$label the chest is tracked in both copies" $tracked $name
    if (-not $tracked) { return }
    $mark = (Get-Content $log | Measure-Object -Line).Lines
    $r = Probe $from "emptychest 0x$ptr"
    Start-Sleep 8
    $new = Get-Content $log | Select-Object -Skip $mark
    $sent = @($new | Where-Object { $_ -match "^\[$fromPid\] S chest .* emptied here" })
    $got = @($new | Where-Object { $_ -match "^\[$toPid\] S chest .* emptied" })
    Check "$label the looting copy notices the empty chest" ($sent.Count -eq 1) ($sent | Select-Object -First 1)
    Check "$label the other copy empties the same chest" ($got.Count -ge 1) ($got | Select-Object -First 1)
}
RunOne 0 1 "host->guest"
RunOne 1 0 "guest->host"
if ((PidOf 0) -and (PidOf 1)) { Restore-Heroes }
Remove-Item $fastFlag -ErrorAction SilentlyContinue
$pass = @($results | Where-Object Result -eq "PASS").Count
$dir = Join-Path $c "docs\reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$md = "# Chest sync $(Get-Date -Format 'yyyy-MM-dd HH:mm')`n`n$pass / $($results.Count) checks passed.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/') |`n" }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
"$pass / $($results.Count) checks passed"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
