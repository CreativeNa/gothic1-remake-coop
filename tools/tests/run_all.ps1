# Automated end-to-end suite for the two-copy coop mod. No human needed: it starts both copies, drives them with game input actions and probe commands,
# asserts on numbers (positions, flags, log lines, fps, crashes) and writes docs\reports\last_run.md with PASS / FAIL per check.
#   run_all.ps1 [-NoStart] [-Quick]      -NoStart: use the copies that are already running;  -Quick: skip the quick-load rounds
param([switch]$NoStart, [switch]$Quick, [string]$Npc = "Ratford", [string]$Net = "", [string]$Report = "last_run")   # -Net "loss=10 delay=150 jitter=60 dup=2": run through the impairment proxy
$c = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$dev = Join-Path $c "tools\dev.ps1"; $log = Join-Path $c "native\build\coopdll.log"
$results = New-Object System.Collections.ArrayList
function Check($name, [bool]$ok, $detail = "") { [void]$results.Add([pscustomobject]@{ Check = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = "$detail" }); "{0,-5} {1}  {2}" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail }
function PidOf($seat) { (Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$seat(\s|$)" } | Select-Object -First 1).ProcessId }
function Probe($seat, $cmd) { $o = & $dev probe "@$seat $cmd" 2>&1 | Out-String; $id = PidOf $seat; if (-not $id) { return "" }; (($o -split "===== game pid ") | Where-Object { $_ -match "^$id" }) -join "`n" }
function LogLines { Get-Content $log -ErrorAction SilentlyContinue }
function Focus($seat) { & (Join-Path $c "tools\realclick.ps1") -Seat $seat -Click 0 | Out-Null }
function Ready { foreach ($s in 0, 1) { if (-not ((Probe $s "frames") -match "have_remote=1")) { return $false } }; return $true }

# ---- 0. start ----
$proxy = $null; $proxyFile = Join-Path $c "native\build\net_proxy.txt"; $proxyStats = Join-Path $c "native\build\net_proxy_stats.txt"
if ($Net) {
    $args2 = @("--port", "41001", "--base", "40001", "--stats", $proxyStats)
    foreach ($kv in ($Net -split "\s+")) { if ($kv -match "^(loss|delay|jitter|dup)=([\d.]+)$") { $args2 += "--$($matches[1])"; $args2 += $matches[2] } }
    Set-Content $proxyFile "41001"
    $args2 = @('"' + (Join-Path $PSScriptRoot "netproxy.py") + '"') + @($args2 | ForEach-Object { if ($_ -match "\s") { '"' + $_ + '"' } else { $_ } })     # paths with spaces need quotes
    $proxy = Start-Process python -ArgumentList $args2 -PassThru -WindowStyle Hidden
    Start-Sleep 1
    "network proxy started: $Net"
} elseif (Test-Path $proxyFile) { Remove-Item $proxyFile }
if (-not $NoStart) {
    & (Join-Path $c "tools\harness.ps1") stop; Start-Sleep 3
    & (Join-Path $c "tools\harness_start.ps1") | Out-Null
    $t0 = Get-Date; while (((Get-Date) - $t0).TotalSeconds -lt 150 -and -not (Ready)) { Start-Sleep 5 }
    Check "boot: both copies reach the world and see each other" (Ready) ("after {0:N0} s" -f ((Get-Date) - $t0).TotalSeconds)
} else { Check "boot: both copies running and linked" (Ready) }
$hp = PidOf 0; $gp = PidOf 1
Check "boot: two game processes" ([bool]($hp -and $gp)) "host $hp, guest $gp"
if (-not ($hp -and $gp)) { $results | Format-Table | Out-String | Write-Host; exit 1 }
Start-Sleep 20                                                                    # let the NPC lists fill

# ---- 1a. the packets really arrive (the same save gives equal NPC positions even without any sync, so positions alone prove nothing) ----
Start-Sleep 6
$mir = @(LogLines | Select-String "^\[$gp\] N seat 1: .*mirroring [1-9]")
Check "net: the guest receives the host's NPC data and mirrors NPCs" ($mir.Count -gt 0) ($mir | Select-Object -Last 1 | ForEach-Object { $_.Line.Substring($_.Line.IndexOf("N seat")) })

# ---- 1. numeric NPC sync (positions, flags) ----
$diff = & (Join-Path $c "tools\diff_npcs.ps1") -Quiet -MaxDist 200
Check "npc sync: every NPC of the host is at the same place in the guest" ($LASTEXITCODE -eq 0) ($diff | Select-Object -Last 1)

# ---- 2. knock-out: the host hero hits the NPC with real input actions until the host reports it defeated ----
$mark = (LogLines | Measure-Object).Count
Focus 0
$n = ([regex]::Match((Probe 0 "npcs 20000 40"), "([0-9A-F]{16}) Character_[^ ]*$Npc")).Groups[1].Value
Check "knock-out: NPC $Npc found in the host" ([bool]$n)
if ($n) {
    Probe 0 "npchp 0x$n 6" | Out-Null
    Probe 0 "tpto 0x$n hero 170 0 10" | Out-Null; Start-Sleep 1; Probe 0 "lookat 0x$n 0 -20" | Out-Null
    Probe 0 "inject IA_Ability_Action_EquipWeaponAuto" | Out-Null; Start-Sleep 3
    $acts = "IA_Combat_Attack_Top", "IA_Combat_Attack_Left", "IA_Combat_Attack_Right", "IA_Combat_Attack_Bottom"
    $down = $false
    for ($i = 0; $i -lt 16 -and -not $down; $i++) {
        Probe 0 "lookat 0x$n 0 -20" | Out-Null; Probe 0 "inject $($acts[$i % 4])" | Out-Null; Start-Sleep -Milliseconds 800
        $down = (Probe 0 "npcinfo $Npc") -match "defeated 1"
    }
    Check "knock-out: the host reports the NPC defeated" $down "after $i attacks"
    Start-Sleep 4
    $new = LogLines | Select-Object -Skip $mark
    Check "knock-out: the guest switched an NPC to the bone-driven mesh" ([bool]($new | Select-String "^\[$gp\] N guest: NPC .* now bone-driven")) ""
    $diff2 = & (Join-Path $c "tools\diff_npcs.ps1") -Quiet -MaxDist 250 -Name $Npc
    Check "knock-out: the lying NPC is at the same place in both copies" ($LASTEXITCODE -eq 0) ($diff2 | Select-Object -Last 1)
}

# ---- 2b. ownership: the guest hero goes to the NPC while the host hero is far away: the guest takes the NPC over and knocks it out, the host mirrors it ----
$mark2 = (LogLines | Measure-Object).Count
$sc = ([regex]::Match((Probe 0 "npcs 20000 40"), "([0-9A-F]{16}) Character_[^ ]*$Npc")).Groups[1].Value     # the host hero stays in the same area (same streaming cells), just clearly farther away
$gn = ([regex]::Match((Probe 1 "npcs 20000 40"), "([0-9A-F]{16}) Character_[^ ]*$Npc")).Groups[1].Value
if ($sc -and $gn) {
    Probe 0 "tpto hero 0x$sc 1300 0 10" | Out-Null; Start-Sleep 1                                     # the host hero far from the NPC
    Probe 1 "tpto hero 0x$gn 170 0 10" | Out-Null; Start-Sleep 1; Probe 1 "lookat 0x$gn 0 -20" | Out-Null
    Start-Sleep 5
    $own = LogLines | Select-Object -Skip $mark2
    Check "ownership: the guest takes the NPC over when its hero is clearly nearer" ([bool]($own | Select-String "^\[$gp\] N owner of [0-9a-f]+ -> seat 1")) ""
    Focus 1; Probe 1 "npchp 0x$gn 6" | Out-Null
    Probe 1 "inject IA_Ability_Action_EquipWeaponAuto" | Out-Null; Start-Sleep 3
    $down2 = $false
    for ($i = 0; $i -lt 28 -and -not $down2; $i++) {
        if ($i -eq 12) { Focus 1; Probe 1 "inject IA_Ability_Action_EquipWeaponAuto" | Out-Null; Start-Sleep 2 }     # still not down: the weapon may have been put away, draw it again
        Probe 1 "lookat 0x$gn 0 -20" | Out-Null; Probe 1 "inject $($acts[$i % 4])" | Out-Null; Start-Sleep -Milliseconds 800
        $down2 = (Probe 1 "npcinfo $Npc") -match "defeated 1"
    }
    Check "ownership: the guest's own hits knock the NPC out (it simulates it)" $down2 "after $i attacks"
    Start-Sleep 4
    $own = LogLines | Select-Object -Skip $mark2
    Check "ownership: the host mirrors the NPC with the bone-driven mesh" ([bool]($own | Select-String "^\[$hp\] N guest: NPC .* now bone-driven")) ""
    $diff3 = & (Join-Path $c "tools\diff_npcs.ps1") -Quiet -MaxDist 300 -Name $Npc
    Check "ownership: the NPC is at the same place in both copies" ($LASTEXITCODE -eq 0) ($diff3 | Select-Object -Last 1)
    foreach ($hv in 77, 0) {                                                                       # the owner's health numbers (and a death) arrive on the mirror
        Probe 1 "npchp 0x$gn $hv" | Out-Null; Start-Sleep 3
        $hh = [regex]::Match((Probe 0 "npcinfo $Npc"), "health (-?\d+)").Groups[1].Value
        Check "health: the mirror shows the owner's health $hv" ($hh -ne "" -and [math]::Abs([int]$hh - $hv) -le 2) "mirror shows '$hh'"
    }
} else { Check "ownership: NPCs found for the scenario" $false "scavenger '$sc', $Npc in guest '$gn'" }

# ---- 3. world reloads with the NPC down ----
if (-not $Quick) {
    Focus 1; Probe 1 "inject IA_QuickSave" | Out-Null; Start-Sleep 5
    foreach ($r in 1, 2) {
        Focus 1; Probe 1 "inject IA_QuickLoad" | Out-Null; Start-Sleep 50
        Check "quick load #${r}: both copies survive" ([bool]((PidOf 0) -and (PidOf 1)))
    }
    Check "quick load: both copies see each other again" (Ready)
}

# ---- 4. frame rate and crashes ----
foreach ($s in 0, 1) {
    $p = PidOf $s
    $f = @(LogLines | Select-String "^\[$p\] PR fps ([\d.]+)" | Select-Object -Last 6 | ForEach-Object { [double]$_.Matches[0].Groups[1].Value } | Sort-Object)
    $med = if ($f.Count) { $f[[int]($f.Count / 2)] } else { 0 }
    Check "fps: seat $s median of the last samples >= 15" ($med -ge 15) ("{0:N1} fps" -f $med)
}
$ours = @(& (Join-Path $c "tools\logq.ps1") -Crashes -Last 40 | Where-Object { $_ -match "our code at that moment: call=(?!none)|module=(?!none)|EXCEPTION" })
Check "stability: no access violation or exception inside our code (this run)" ($ours.Count -eq 0) ($ours | Select-Object -First 2 | Out-String).Trim()
$dumps = @(Get-ChildItem (Join-Path $c "game-test\G1R\Binaries\Win64") -Filter *.dmp -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-25) })
Check "stability: no new crash dump" ($dumps.Count -eq 0) (($dumps | ForEach-Object Name) -join ", ")

# ---- report ----
$dir = Join-Path $c "docs\reports"; New-Item -ItemType Directory -Force $dir | Out-Null
$pass = @($results | Where-Object Result -eq "PASS").Count
$md = "# Automated run $(Get-Date -Format 'yyyy-MM-dd HH:mm')`n`n$pass / $($results.Count) checks passed.`n`n| Result | Check | Detail |`n|---|---|---|`n"
foreach ($r in $results) { $md += "| $($r.Result) | $($r.Check) | $($r.Detail -replace '\|', '/' -replace "`r?`n", ' ') |`n" }
if ($Net) { $md = $md.Replace("# Automated run", "# Automated run [network: $Net]") ; $st = Get-Content $proxyStats -ErrorAction SilentlyContinue; if ($st) { $md += "`nProxy: $st`n" } }
Set-Content (Join-Path $dir "$Report.md") $md -Encoding UTF8
if ($proxy) { Stop-Process -Id $proxy.Id -Force -ErrorAction SilentlyContinue; Remove-Item $proxyFile -ErrorAction SilentlyContinue }
"`n$pass / $($results.Count) checks passed -> docs\reports\$Report.md"
exit $(if ($pass -eq $results.Count) { 0 } else { 1 })
