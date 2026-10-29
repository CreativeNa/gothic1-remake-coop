# Run once the game is installed. Copies the install to a test folder (original untouched),
# installs the G1RCoop mod and the UE4SS probe, then you launch with play.ps1.
$common = "C:\Program Files (x86)\Steam\steamapps\common"
$src    = Join-Path $common "Gothic 1 Remake"
$root   = Join-Path $common "Gothic1R-coop"
$test   = Join-Path $root "game-test"

if (-not (Test-Path "$src\G1R\Binaries\Win64\G1R-Win64-Shipping.exe")) {
    throw "Game exe not found in $src - install the game first."
}
if (-not (Test-Path $test)) {
    robocopy $src $test /E /XD "$src\G1R\Saved" /NFL /NDL /NJH /NJS | Out-Null
}
New-Item -ItemType Directory -Force "$test\G1R\Mods" | Out-Null
robocopy "$root\mod\G1RCoop" "$test\G1R\Mods\G1RCoop" /E /NFL /NDL /NJH /NJS | Out-Null

$probe = "$test\G1R\Binaries\Win64\Mods\G1RCoopProbe"
if (Test-Path "$test\G1R\Binaries\Win64\UE4SS.dll") {
    robocopy "$root\tools\ue4ss_probe" $probe /E /NFL /NDL /NJH /NJS | Out-Null
    New-Item -ItemType File -Force "$probe\enabled.txt" | Out-Null
} else { Write-Warning "UE4SS not found in the copy - probe not installed." }
"Done. Test copy: $test"
