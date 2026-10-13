$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$gcc='D:/mingw/mingw64/bin/gcc.exe'
if (-not (Test-Path $gcc)) { throw "Native test compiler missing: $gcc" }
$testDir=Join-Path $projectRoot 'build/HostTests'
New-Item -ItemType Directory -Force $testDir | Out-Null
Push-Location $projectRoot
$oldPath=$env:PATH
$env:PATH="$(Split-Path $gcc -Parent);$oldPath"
try {
    foreach($ppr in @(7,16,200,1600,4080,4096,4112,8176,8192,40000)) {
        $exe=Join-Path $testDir "test_motor_$ppr.exe"
        & $gcc -std=c11 -Wall -Wextra -Werror -DMOTOR_HOST_TEST "-DMOTOR_DRIVER_PULSES_PER_REV=$ppr" -Itests/stubs -ICore/Inc tests/test_motor.c Core/Src/motor_motion.c -o $exe
        if($LASTEXITCODE -ne 0) { throw 'Host test compilation failed' }
        & $exe
        if($LASTEXITCODE -ne 0) { throw "Host test failed for ppr=$ppr" }
    }
    foreach($scenario in @('bad-mode','unexpected-stop','null-timer','wrong-timer','inverted-polarity')) {
        & (Join-Path $testDir 'test_motor_1600.exe') $scenario
        if($LASTEXITCODE -ne 0) { throw "Host test failed: $scenario" }
    }
} finally { $env:PATH=$oldPath; Pop-Location }
