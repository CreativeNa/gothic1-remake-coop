# Chest sync test (both copies running in the game): a chest emptied next to one hero is empty in the other copy too, in both directions.
#   test_chests.ps1 [-Report chests]
param([string]$Report = "chests")
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dev = Join-Path $c "tools\dev.ps1"; $log = Join-Path $c "native\build\coopdll.log"
$results = New-Object System.Collections.ArrayList
function Check($name, [bool]$ok, $detail = "") { [void]$results.Add([pscustomobject]@{ Check = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = "$detail" }); "{0,-5} {1}  {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail }
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; if (-not $id) { return "" }; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
function Hero($seat) { [regex]::Match((Probe $seat "frames"), "hero=([0-9A-F]+)").Groups[1].Value }
if (-not ((PidOf 0) -and (PidOf 1))) { throw "start both copies first" }
$skip = @{}
function RunOne($from, $to, $label) {
    $ptr = $null
    foreach ($m in [regex]::Matches((Probe $from "ioscan Chest 40"), "([0-9A-F]{16}) \S+ \(")) {
        $p = $m.Groups[1].Value; if ($skip[$p]) { continue }
        if ((Probe $from "emptychest 0x$p dry") -match "(\d+) \+ (\d+) items before" -and ([int]$matches[1] + [int]$matches[2]) -gt 0) { $ptr = $p; break }
    }
    Check "$label setup: a chest with something inside is loaded" ([bool]$ptr) $ptr
    if (-not $ptr) { return }
    $skip[$ptr] = 1
    $t = Probe $from "tpto 0x$(Hero $from) 0x$ptr 200 0 80"; $pos = [regex]::Match($t, "target at (-?\d+) (-?\d+) (-?\d+)")
    if (-not $pos.Success) { Check "$label setup: hero next to the chest" $false; return }
    Probe $to "tp hero $($pos.Groups[1].Value) $([int]$pos.Groups[2].Value + 150) $([int]$pos.Groups[3].Value + 80)" | Out-Null
    Start-Sleep 50
    $mark = (Get-Content $log | Measure-Object -Line).Lines
    $r = Probe $from "emptychest 0x$ptr"
    Start-Sleep 8
    $new = Get-Content $log | Select-Object -Skip $mark
    $sent = @($new | Where-Object { $_ -match "^\[$(PidOf $from)\] S chest .* emptied here" })
    $got = @($new | Where-Object { $_ -match "^\[$(PidOf $to)\] S chest .* emptied" })
    Check "$label the looting copy notices the empty chest" ($sent.Count -eq 1) ($sent | Select-Object -First 1)
    Check "$label the other copy empties the same chest" ($got.Count -ge 1) ($got | Select-Object -First 1)
}
RunOne 0 1 "host->guest"
RunOne 1 0 "guest->host"
$pass = @($results | Where-Object Result -eq "PASS").Count
$dir = Join-Path $c "docs\reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$md = "# Chest sync $(Get-Date -Format 'yyyy-MM-dd HH:mm')`n`n$pass / $($results.Count) checks passed.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/') |`n" }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
"$pass / $($results.Count) checks passed"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
