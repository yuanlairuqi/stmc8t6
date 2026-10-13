param(
    [string]$Compiler='D:\mingw\code\mingw64\bin\gcc.exe',
    [string]$JavaPath='D:\uu'
)
$ErrorActionPreference='Stop'
$taskProject=Split-Path -Parent $PSScriptRoot
$taskResults=Join-Path $taskProject 'build\host-tests'
New-Item -ItemType Directory -Path $taskResults -Force | Out-Null
$env:Path=(Split-Path -Parent $Compiler)+';'+$env:Path
$taskCompileArgs=@('-std=c11','-Wall','-Wextra','-Werror','-I',(Join-Path $taskProject 'main'),
    (Join-Path $taskProject 'tests\test_control.c'),(Join-Path $taskProject 'main\robot_control.c'),'-o',(Join-Path $taskResults 'test_control.exe'))
& $Compiler @taskCompileArgs
if($LASTEXITCODE -ne 0){throw 'C test compile failed'}
& (Join-Path $taskResults 'test_control.exe') (Join-Path $taskResults 'status.bin')
if($LASTEXITCODE -ne 0){throw 'C control tests failed'}
$taskLinkArgs=@('-std=c11','-Wall','-Wextra','-Werror','-I',(Join-Path $taskProject 'main'),'-I',(Join-Path $taskProject 'shared'),'-I',(Join-Path $taskProject 'stm32_example'),
    (Join-Path $taskProject 'tests\test_link.c'),(Join-Path $taskProject 'main\robot_bridge.c'),(Join-Path $taskProject 'shared\robot_wire.c'),
    (Join-Path $taskProject 'stm32_example\robot_peer.c'),'-o',(Join-Path $taskResults 'test_link.exe'))
& $Compiler @taskLinkArgs
if($LASTEXITCODE -ne 0){throw 'UART tests compile failed'}
& (Join-Path $taskResults 'test_link.exe')
if($LASTEXITCODE -ne 0){throw 'UART/STM32 tests failed'}
$taskJavaArgs=@('-encoding','UTF-8','-d',$taskResults,
    (Join-Path $taskProject 'android\app\src\main\java\cn\robot\ble\Protocol.java'),
    (Join-Path $taskProject 'android\app\src\main\java\cn\robot\ble\CommandChannel.java'),
    (Join-Path $taskProject 'android\tests\ProtocolTest.java'),(Join-Path $taskProject 'android\tests\CommandChannelTest.java'))
& (Join-Path $JavaPath 'bin\javac.exe') @taskJavaArgs
if($LASTEXITCODE -ne 0){throw 'Java tests compile failed'}
& (Join-Path $JavaPath 'bin\java.exe') -ea -cp $taskResults ProtocolTest (Join-Path $taskResults 'status.bin')
if($LASTEXITCODE -ne 0){throw 'Android protocol tests failed'}
& (Join-Path $JavaPath 'bin\java.exe') -ea -cp $taskResults CommandChannelTest
if($LASTEXITCODE -ne 0){throw 'Android command channel tests failed'}
