$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$gcc='D:/mingw/mingw64/bin/gcc.exe'
if (-not (Test-Path $gcc)) { throw "Native test compiler missing: $gcc" }
$testDir=Join-Path $projectRoot 'build/HostTests'
New-Item -ItemType Directory -Force $testDir | Out-Null
$oldPath=$env:PATH
$env:PATH="$(Split-Path $gcc -Parent);$oldPath"
Push-Location $projectRoot
try {
    $exe=Join-Path $testDir 'test_robot_uart.exe'
    & $gcc -std=c11 -Wall -Wextra -Werror -DMOTOR_HOST_TEST -DROBOT_UART_HOST_TEST -Itests/stubs -ICore/Inc -Iverification/uart/esp_reference tests/test_robot_uart.c Core/Src/motor_motion.c Core/Src/robot_uart.c Core/Src/robot_wire.c Core/Src/robot_peer.c verification/uart/esp_reference/robot_bridge.c -o $exe
    if($LASTEXITCODE -ne 0) { throw 'UART host test compilation failed' }
    foreach($scenario in @('standalone','waiting-stop','running-stop','finished-batch-stop','rx-overflow','uart-error','rx-rearm-fail','bad-stream','new-session-move','tx-buffer','tx-timeout','tx-start-fail','link-timeout','duplicate','cross-bridge')) {
        & $exe $scenario
        if($LASTEXITCODE -ne 0) { throw "UART host test failed: $scenario" }
    }
    # ESP32 test snapshot against the STM32 production wire/peer modules.
    $protocolExe=Join-Path $testDir 'test_shared_protocol.exe'
    & $gcc -std=c11 -Wall -Wextra -Werror -ICore/Inc -Iverification/uart/esp_reference verification/uart/esp_reference/test_link.c verification/uart/esp_reference/robot_bridge.c Core/Src/robot_wire.c Core/Src/robot_peer.c -o $protocolExe
    if($LASTEXITCODE -ne 0) { throw 'Shared protocol test compilation failed' }
    & $protocolExe
    if($LASTEXITCODE -ne 0) { throw 'Shared protocol tests failed' }
    foreach($enabled in @(0,1)) {
        $continuousExe=Join-Path $testDir "test_continuous_$enabled.exe"
        & $gcc -std=c11 -Wall -Wextra -Werror -DMOTOR_HOST_TEST "-DMOTOR_CONTINUOUS_ENABLED=$enabled" -Itests/stubs -ICore/Inc tests/test_continuous.c Core/Src/motor_motion.c -o $continuousExe
        if($LASTEXITCODE -ne 0) { throw 'Continuous test compilation failed' }
        & $continuousExe
        if($LASTEXITCODE -ne 0) { throw "Continuous tests failed: enabled=$enabled" }
    }
    $enabledUARTExe=Join-Path $testDir 'test_robot_uart_enabled.exe'
    & $gcc -std=c11 -Wall -Wextra -Werror -DMOTOR_HOST_TEST -DROBOT_UART_HOST_TEST -DMOTOR_CONTINUOUS_ENABLED=1 -Itests/stubs -ICore/Inc -Iverification/uart/esp_reference tests/test_robot_uart.c Core/Src/motor_motion.c Core/Src/robot_uart.c Core/Src/robot_wire.c Core/Src/robot_peer.c verification/uart/esp_reference/robot_bridge.c -o $enabledUARTExe
    if($LASTEXITCODE -ne 0) { throw 'Enabled UART test compilation failed' }
    & $enabledUARTExe enabled-cycle
    if($LASTEXITCODE -ne 0) { throw 'Enabled UART cycle test failed' }
} finally { Pop-Location; $env:PATH=$oldPath }
