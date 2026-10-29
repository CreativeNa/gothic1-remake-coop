# Launch test copy forcing the plain IpNetDriver and no online platform (to test listen without Steam).
param([int]$Count = 1)
$exe = "C:\Program Files (x86)\Steam\steamapps\common\Gothic1R-coop\game-test\G1R\Binaries\Win64\G1R-Win64-Shipping.exe"
$q = '"'
$def = '(DefName=' + $q + 'GameNetDriver' + $q + ',DriverClassName=' + $q + '/Script/OnlineSubsystemUtils.IpNetDriver' + $q + ',DriverClassNameFallback=' + $q + '/Script/OnlineSubsystemUtils.IpNetDriver' + $q + ')'
$a = @(
  '-console','-windowed','-ResX=1280','-ResY=720','-no-intro-cinematic','-log',
  '-ini:Engine:[/Script/Engine.Engine]:!NetDriverDefinitions=ClearArray',
  ('-ini:Engine:[/Script/Engine.Engine]:+NetDriverDefinitions=' + $def),
  '-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=Null'
)
$argString = ($a | ForEach-Object { if ($_ -match '\s|"') { '"' + ($_ -replace '"','\"') + '"' } else { $_ } }) -join ' '
1..$Count | ForEach-Object { Start-Process $exe -ArgumentList $argString -WorkingDirectory (Split-Path $exe); Start-Sleep 5 }
