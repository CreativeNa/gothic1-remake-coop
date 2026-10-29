# Connects a running game to a friend by IP and injects the coop DLL.
#   1. start Gothic 1 Remake and load a save (or do it after: the DLL waits for the hero)
#   2. .\join.ps1 -Peer <friend's IP or host name>
# Both players run the same command with the OTHER player's address. Ports: each side listens on -LocalPort and sends to
# the friend's -PeerPort (default: the same number, 40001). UDP only.
#
# Reaching each other:
#   - same LAN or a VPN (Radmin VPN, ZeroTier, Hamachi): use the LAN/VPN address of the friend, nothing else needed
#   - over the internet: both players run join.ps1 at about the same time with each other's PUBLIC IP (many home routers then
#     open the path by themselves, "UDP hole punching"); if it does not connect, forward the UDP port on the router to this PC
#   - Windows Firewall may ask to allow the game: allow it for private networks (this script does not change any firewall setting)
param(
    [Parameter(Mandatory = $true)][string]$Peer,      # "1.2.3.4", "host.name" or "1.2.3.4:40001"
    [int]$LocalPort = 40001,
    [int]$PeerPort = 0,                                # 0 = same as -LocalPort (or the port given after ':' in -Peer)
    [string]$Id = $env:COMPUTERNAME,
    [string]$Dll = "",                                 # default: the newest native\build\coopdll_*.dll
    [int]$ProcessId = 0                                # default: the running G1R-Win64-Shipping process
)
$ErrorActionPreference = "Stop"
$nat = Join-Path (Split-Path $PSScriptRoot -Parent) "native\build"

$peerHost = $Peer
if ($Peer -match "^(.+):(\d+)$") { $peerHost = $Matches[1]; if ($PeerPort -eq 0) { $PeerPort = [int]$Matches[2] } }
if ($PeerPort -eq 0) { $PeerPort = $LocalPort }

if (-not $Dll) {
    $latest = Get-ChildItem "$nat\coopdll_*.dll" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw "no coopdll_*.dll in $nat - build it first (native\build_dll.bat <suffix>)" }
    $Dll = $latest.Name
}
if (-not (Test-Path "$nat\$Dll")) { throw "$nat\$Dll not found" }
if (-not (Test-Path "$nat\inject.exe")) { throw "$nat\inject.exe not found - run native\build.bat" }

if ($ProcessId -eq 0) {
    $procs = @(Get-Process G1R-Win64-Shipping -ErrorAction SilentlyContinue)
    if ($procs.Count -eq 0) { throw "the game is not running - start Gothic 1 Remake first" }
    if ($procs.Count -gt 1) { throw "several game processes are running ($($procs.Id -join ', ')) - pass -ProcessId <pid>" }
    $ProcessId = $procs[0].Id
}

$Id = ($Id -replace "[^A-Za-z0-9_-]", "")
if (-not $Id) { $Id = "P" }
if ($Id.Length -gt 15) { $Id = $Id.Substring(0, 15) }
Set-Content "$nat\cfg_$ProcessId.txt" "$LocalPort $peerHost $PeerPort $Id"
"game pid $ProcessId, listening on UDP $LocalPort, friend at ${peerHost}:$PeerPort, DLL $Dll"
"your addresses on this PC: " + ((Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -notmatch "^(127\.|169\.254\.)" } | ForEach-Object { $_.IPAddress }) -join ", ")
& "$nat\inject.exe" $ProcessId "$nat\$Dll"
"injected. Load a save; the friend appears when both sides are in the game. Log: $nat\coopdll.log"
