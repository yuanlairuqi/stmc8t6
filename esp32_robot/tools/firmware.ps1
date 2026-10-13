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
Push-Location -LiteralPath $taskProject
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
} finally { Pop-Location }
