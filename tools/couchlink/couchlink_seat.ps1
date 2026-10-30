# Helper started by Couchlink for every seat (see gothic1r-coop-test.json): finds this seat's game process by its
# "-CoopSeat=<n>" command line argument, writes the net config and injects the coop DLL.
#   two players on one PC: seat 0 listens on UDP 40001 and talks to 40002, seat 1 the other way round, both via 127.0.0.1
param(
    [Parameter(Mandatory = $true)][int]$Seat,
    [string]$Peer = "127.0.0.1",
    [int]$BasePort = 40001,
    [string]$Dll = "",                 # default: the newest native\build\coopdll_*.dll
    [int]$SettleSeconds = 20           # the game needs ~20-30 s after its process exists before the DLL can be injected (at 8 s the engine has not created its objects yet and the DLL never hooks)
)
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$nat = Join-Path $root "native\build"
$log = Join-Path $nat "couchlink_seat$Seat.log"
function Log($m) { "$(Get-Date -f HH:mm:ss) $m" | Add-Content $log }

try {
    if (-not $Dll) { $Dll = (Get-ChildItem "$nat\coopdll_*.dll" | Sort-Object LastWriteTime -Descending | Select-Object -First 1).Name }
    Log "seat ${Seat}: waiting for the game process (dll $Dll)"
    $proc = $null
    for ($i = 0; $i -lt 360 -and -not $proc; $i++) {                       # up to 3 minutes
        $q = Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$Seat(\s|$)" } | Select-Object -First 1
        if ($q) { $proc = Get-Process -Id $q.ProcessId -ErrorAction SilentlyContinue } else { Start-Sleep -Milliseconds 500 }
    }
    if (-not $proc) { Log "seat ${Seat}: game process not found"; exit 1 }
    Log "seat ${Seat}: game pid $($proc.Id)"
    Start-Sleep $SettleSeconds
    $local = $BasePort + $Seat
    $peerPort = $BasePort + (1 - $Seat)
    Set-Content "$nat\cfg_$($proc.Id).txt" "$local $Peer $peerPort S$Seat"
    $out = & "$nat\inject.exe" $proc.Id "$nat\$Dll" 2>&1
    Log "seat ${Seat}: inject -> $out"
} catch { Log "seat ${Seat}: ERROR $_"; exit 1 }
