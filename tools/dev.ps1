# Development helper for the coop DLL. Usage:
#   .\dev.ps1 build [anim|equip|pose|probe]     rebuild modules (hot-reloaded within ~1 s by every running game)
#   .\dev.ps1 core <suffix>                      build native\build\coopdll_<suffix>.dll (needs a game restart to take effect)
#   .\dev.ps1 probe "<command>" ["<command>"..]  run commands in the live game(s) through the probe module and print the answers
#                                                (try: .\dev.ps1 probe help)
#   .\dev.ps1 shots                              screenshot every monitor to %TEMP%\g1shots\screenN.png
#   .\dev.ps1 status                             running game processes, last problems in the log, newest crash dump decoded
#   .\dev.ps1 log [-Filter text] [-Last 40]      tail of native\build\coopdll.log
param(
    [Parameter(Position = 0)][string]$Cmd = "status",
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Rest,
    [string]$Filter = "", [int]$Last = 40, [int]$TimeoutSec = 10
)
$root = Split-Path $PSScriptRoot -Parent
$nat = Join-Path $root "native"
$bld = Join-Path $nat "build"
$games = @(Get-Process G1R-Win64-Shipping -ErrorAction SilentlyContinue)

switch ($Cmd) {
    "build" {
        $what = if ($Rest) { $Rest[0] } else { "" }
        $out = cmd /c "`"$nat\build_module.bat`" $what" 2>&1
        $out | Where-Object { $_ -match "error|warning" } | ForEach-Object { $_ }
        Get-ChildItem "$bld\modules" -Filter "*.dll" | Where-Object { $_.Name -notmatch "_\d+_\d+\.dll$" } | Select-Object Name, LastWriteTime | Format-Table -AutoSize | Out-String | Write-Host
    }
    "core" {
        if (-not $Rest) { throw "usage: dev.ps1 core <suffix>" }
        $out = cmd /c "`"$nat\build_dll.bat`" $($Rest[0])" 2>&1
        $out | Where-Object { $_ -match "error|warning" } | ForEach-Object { $_ }
        Get-Item "$bld\coopdll_$($Rest[0]).dll" | Select-Object Name, LastWriteTime, Length | Format-Table -AutoSize | Out-String | Write-Host
    }
    "probe" {
        if (-not $Rest) { $Rest = @("help") }
        if ($games.Count -eq 0) { throw "no game process is running" }
        $id = "#" + (Get-Date -Format "HHmmssfff")
        Get-ChildItem "$bld\probe_out_*.txt" -ErrorAction SilentlyContinue | Remove-Item -ErrorAction SilentlyContinue
        $text = $id + "`n" + ($Rest -join "`n") + "`n"
        [IO.File]::WriteAllText("$bld\probe_cmd.txt", $text, (New-Object Text.UTF8Encoding $false))
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        $done = @{}
        while ((Get-Date) -lt $deadline -and $done.Count -lt $games.Count) {
            foreach ($g in $games) {
                $f = "$bld\probe_out_$($g.Id).txt"
                if (-not $done[$g.Id] -and (Test-Path $f)) {
                    $first = (Get-Content $f -TotalCount 1)
                    if ($first -like "$id*") { $done[$g.Id] = $true; "===== game pid $($g.Id)"; Get-Content $f | Select-Object -Skip 1 }
                }
            }
            Start-Sleep -Milliseconds 200
        }
        foreach ($g in $games) { if (-not $done[$g.Id]) { "===== game pid $($g.Id): no answer (is the DLL injected, is the probe module loaded? see: dev.ps1 log -Filter probe)" } }
    }
    "shots" {
        Add-Type -AssemblyName System.Windows.Forms, System.Drawing
        $dir = Join-Path $env:TEMP "g1shots"; New-Item -ItemType Directory -Force $dir | Out-Null
        $i = 0
        foreach ($s in [Windows.Forms.Screen]::AllScreens | Sort-Object { $_.Bounds.X }) {
            $b = $s.Bounds; $bmp = New-Object Drawing.Bitmap $b.Width, $b.Height
            $g = [Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.X, $b.Y, 0, 0, $bmp.Size)
            $p = Join-Path $dir "screen$i.png"; $bmp.Save($p); "$p  ($($b.Width)x$($b.Height) at $($b.X),$($b.Y))"
            $g.Dispose(); $bmp.Dispose(); $i++
        }
    }
    "log" {
        $lines = Get-Content "$bld\coopdll.log" -Tail 4000
        if ($Filter) { $lines = $lines | Where-Object { $_ -match $Filter } }
        $lines | Select-Object -Last $Last
    }
    "status" {
        "game processes: " + $(if ($games) { ($games | ForEach-Object { "$($_.Id) (started $($_.StartTime.ToString('HH:mm:ss')))" }) -join ", " } else { "none" })
        $lines = Get-Content "$bld\coopdll.log" -Tail 6000
        "--- problems in the log (last 8):"
        $lines | Where-Object { $_ -match "EXCEPTION|ABORT|failed|not found|no answer|ignored a packet" } | Select-Object -Last 8
        "--- network:"
        $lines | Where-Object { $_ -match "\] net:|N3 first packet" } | Select-Object -Last 4
        $dmp = Get-ChildItem "$root\game-test\G1R\Binaries\Win64\crash_*.dmp" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($dmp) {
            "--- newest crash dump: $($dmp.Name) ($($dmp.LastWriteTime.ToString('MM-dd HH:mm:ss')))"
            $py = (Get-Command python -ErrorAction SilentlyContinue).Source
            if ($py) { & $py -I "$PSScriptRoot\dmpinfo.py" $dmp.FullName | Select-Object -First 14 } else { "(python not found; decode it with tools\dmpinfo.py)" }
        }
    }
    default { "unknown command '$Cmd'. See the header of this file." }
}
