param([string]$CubeMXPath='D:\STM32CubeMX\STM32CubeMX.exe')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$ioc=Join-Path $projectRoot 'stmc8t6.ioc'
$java=Join-Path (Split-Path $CubeMXPath -Parent) 'jre/bin/java.exe'
foreach($file in @($ioc,$CubeMXPath,$java)) {
    if(-not (Test-Path -LiteralPath $file)) { throw "Missing generation input/tool: $file" }
}
$commands=@("config load $ioc",'project generate',"config save $ioc",'exit')
$script=Join-Path ([IO.Path]::GetTempPath()) ('stmc8t6-cube-'+[guid]::NewGuid()+'.txt')
[IO.File]::WriteAllLines($script,$commands,[Text.UTF8Encoding]::new($false))
try {
    $transcript=& $java "-Duser.home=$env:USERPROFILE" '-Dfile.encoding=UTF-8' -jar $CubeMXPath -q $script 2>&1
    $exitCode=$LASTEXITCODE
    $transcript | ForEach-Object { Write-Output $_ }
    if($exitCode -ne 0 -or ($transcript | Where-Object {"$_" -match '^KO$'})) { throw 'CubeMX reported a failed command' }
    $expected=Join-Path $projectRoot 'Core/Src/main.c'
    if(-not (($transcript -join "`n").Contains($expected))) { throw "CubeMX did not report generation into the expected project: $expected" }
    if($LASTEXITCODE -ne 0) { throw "CubeMX generation failed ($LASTEXITCODE)" }
} finally { Remove-Item -LiteralPath $script -ErrorAction SilentlyContinue }
