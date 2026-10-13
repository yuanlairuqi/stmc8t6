param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$bundleRoot=Join-Path $env:LOCALAPPDATA 'stm32cube/bundles'
function Find-BundleBinary([string]$package,[string]$binary) {
    $match=Get-ChildItem (Join-Path $bundleRoot $package) -Directory |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName "bin/$binary" } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $match) { throw "Missing installed STM32 bundle: $package / $binary" }
    return $match
}
$cmake=Find-BundleBinary 'cmake' 'cmake.exe'
$gcc=Find-BundleBinary 'gnu-tools-for-stm32' 'arm-none-eabi-gcc.exe'
$ninja=Find-BundleBinary 'ninja' 'ninja.exe'
$size=Join-Path (Split-Path $gcc -Parent) 'arm-none-eabi-size.exe'
$oldPath=$env:PATH
try {
    $env:PATH="$(Split-Path $gcc -Parent);$(Split-Path $ninja -Parent);$(Split-Path $cmake -Parent);$oldPath"
    Push-Location $projectRoot
    try {
        $buildDir="build/Motor$Configuration"
        Write-Output "CMake: $cmake"
        Write-Output "GCC: $gcc"
        & $cmake --preset $Configuration -B $buildDir
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
        & $cmake --build $buildDir --clean-first
        if ($LASTEXITCODE -ne 0) { throw 'Compile/link failed' }
        & $size "$buildDir/stmc8t6.elf"
        if ($LASTEXITCODE -ne 0) { throw 'ELF size check failed' }
    } finally { Pop-Location }
} finally { $env:PATH=$oldPath }
