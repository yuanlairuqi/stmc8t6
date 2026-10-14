param(
    [ValidateSet('build','flash','monitor','menuconfig')][string]$Action='build',
    [string]$Port,
    [string]$IdfProfile='C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
)
$ErrorActionPreference='Stop'
$env:PROCESSOR_ARCHITECTURE='AMD64'
$env:PYTHONUTF8='1'
if(!(Test-Path -LiteralPath $IdfProfile)){throw "ESP-IDF profile not found: $IdfProfile"}
& $IdfProfile
$taskProject=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'prepare_build.ps1')
. (Join-Path $PSScriptRoot 'native_workspace.ps1')
$taskWorkProject=$taskProject
if($taskProject -match '[^\x00-\x7F]') {
    $taskWorkProject=New-AsciiNativeWorkspace $taskProject
    foreach($folder in @('main','shared','components','managed_components')) {
        $source=Join-Path $taskProject $folder
        if(Test-Path -LiteralPath $source) { Sync-NativeSourceDirectory $source $taskWorkProject $folder }
    }
    foreach($file in Get-ChildItem -LiteralPath $taskProject -File | Where-Object {$_.Name -eq 'CMakeLists.txt' -or $_.Name -like 'sdkconfig*' -or $_.Name -eq 'dependencies.lock' -or $_.Extension -eq '.csv'}) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $taskWorkProject $file.Name) -Force
    }
    Write-Output "Native tools use an ASCII source snapshot: $taskWorkProject"
}
if($Action -in @('build','menuconfig','flash')) {
    Initialize-ProjectBuildDirectory $taskWorkProject (Join-Path $taskWorkProject 'build')
}
Push-Location -LiteralPath $taskWorkProject
try {
    switch($Action) {
        'build' { idf.py -B build -D IDF_TARGET=esp32s3 build }
        'flash' {
            if(!$Port){throw 'Specify -Port COMx after connecting the ESP32-S3.'}
            idf.py -B build -D IDF_TARGET=esp32s3 build
            if($LASTEXITCODE -ne 0){throw 'Build failed; flash cancelled'}
            idf.py -B build -p $Port flash
        }
        'monitor' {
            if(!$Port){throw 'Specify -Port COMx.'}
            idf.py -B build -p $Port monitor
        }
        'menuconfig' { idf.py -B build -D IDF_TARGET=esp32s3 menuconfig }
    }
    if($LASTEXITCODE -ne 0){throw "idf.py failed: $LASTEXITCODE"}
    if($taskWorkProject -ne $taskProject) {
        if($Action -eq 'menuconfig') { Copy-Item -LiteralPath (Join-Path $taskWorkProject 'sdkconfig') -Destination (Join-Path $taskProject 'sdkconfig') -Force }
        if($Action -in @('build','flash')) {
            foreach($relative in @('esp32_robot.bin','esp32_robot.elf','bootloader/bootloader.bin','partition_table/partition-table.bin','flasher_args.json','flash_args')) {
                $from=Join-Path $taskWorkProject "build/$relative"
                if(Test-Path -LiteralPath $from) {
                    $to=Join-Path $taskProject "build/$relative"
                    New-Item -ItemType Directory -Force (Split-Path $to -Parent) | Out-Null
                    Copy-Item -LiteralPath $from -Destination $to -Force
                }
            }
            [IO.File]::WriteAllText((Join-Path $taskProject 'build/native-workspace.txt'),$taskWorkProject,[Text.UTF8Encoding]::new($false))
        }
    }
} finally { Pop-Location }
