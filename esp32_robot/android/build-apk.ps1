param(
    [Parameter(Mandatory=$true)][string]$SdkPath,
    [string]$JavaPath = 'D:\uu',
    [string]$BuildToolsVersion = '35.0.0'
)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
. (Join-Path (Split-Path $taskRoot -Parent) 'tools/native_workspace.ps1')
$taskInputRoot=$taskRoot
if($taskRoot -match '[^\x00-\x7F]') {
    $taskInputRoot=New-AsciiNativeWorkspace $taskRoot
    $taskApp=Join-Path $taskInputRoot 'app'
    New-Item -ItemType Directory -Force $taskApp | Out-Null
    Sync-NativeSourceDirectory (Join-Path $taskRoot 'app/src') $taskApp 'src'
    Write-Output "Android native tools use an ASCII source snapshot: $taskInputRoot"
}
$taskSdk = (Resolve-Path -LiteralPath $SdkPath).Path
$taskJavaBin = Join-Path $JavaPath 'bin'
$env:JAVA_HOME = $JavaPath
$taskTools = Join-Path $taskSdk ('build-tools\'+$BuildToolsVersion)
if (!(Test-Path -LiteralPath $taskTools)) { $taskTools=Join-Path $taskSdk 'android-15' }
$taskAndroidJar = Join-Path $taskSdk 'platforms\android-35\android.jar'
if (!(Test-Path -LiteralPath $taskAndroidJar)) { $taskAndroidJar=Join-Path $taskSdk 'android-35\android.jar' }
foreach($taskRequired in @($taskAndroidJar,(Join-Path $taskTools 'aapt2.exe'),(Join-Path $taskJavaBin 'javac.exe'))) {
    if(!(Test-Path -LiteralPath $taskRequired)){throw "Missing tool: $taskRequired"}
}
$taskBuild = Join-Path $taskInputRoot ('build\manual-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $taskBuild,(Join-Path $taskBuild 'gen'),(Join-Path $taskBuild 'classes'),(Join-Path $taskBuild 'dex') -Force | Out-Null
function Invoke-Checked([string]$Tool,[string[]]$Arguments) {
    & $Tool @Arguments
    if($LASTEXITCODE -ne 0){throw "Tool failed ($LASTEXITCODE): $Tool"}
}
Invoke-Checked (Join-Path $taskTools 'aapt2.exe') @('compile','--dir',(Join-Path $taskInputRoot 'app\src\main\res'),'-o',(Join-Path $taskBuild 'resources.zip'))
Invoke-Checked (Join-Path $taskTools 'aapt2.exe') @('link','-I',$taskAndroidJar,'--manifest',(Join-Path $taskInputRoot 'app\src\main\AndroidManifest.xml'),
    '--java',(Join-Path $taskBuild 'gen'),'-o',(Join-Path $taskBuild 'unsigned.apk'),'--min-sdk-version','23','--target-sdk-version','35',
    '--version-code','3','--version-name','3.0',(Join-Path $taskBuild 'resources.zip'))
$taskSources = @((Get-ChildItem -LiteralPath (Join-Path $taskInputRoot 'app\src\main\java') -Filter '*.java' -Recurse).FullName) +
    @((Get-ChildItem -LiteralPath (Join-Path $taskBuild 'gen') -Filter '*.java' -Recurse).FullName)
$taskCompileArgs = @('-encoding','UTF-8','-source','8','-target','8','-bootclasspath',($taskAndroidJar+';'+(Join-Path $taskTools 'core-lambda-stubs.jar')),
    '-d',(Join-Path $taskBuild 'classes')) + $taskSources
Invoke-Checked (Join-Path $taskJavaBin 'javac.exe') $taskCompileArgs
Invoke-Checked (Join-Path $taskJavaBin 'jar.exe') @('cf',(Join-Path $taskBuild 'classes.jar'),'-C',(Join-Path $taskBuild 'classes'),'.')
Invoke-Checked (Join-Path $taskJavaBin 'java.exe') @('-cp',(Join-Path $taskTools 'lib\d8.jar'),'com.android.tools.r8.D8','--min-api','23',
    '--lib',$taskAndroidJar,'--output',(Join-Path $taskBuild 'dex'),(Join-Path $taskBuild 'classes.jar'))
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskApk = [System.IO.Compression.ZipFile]::Open((Join-Path $taskBuild 'unsigned.apk'),[System.IO.Compression.ZipArchiveMode]::Update)
try {
    foreach($taskDex in Get-ChildItem -LiteralPath (Join-Path $taskBuild 'dex') -Filter '*.dex') {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($taskApk,$taskDex.FullName,$taskDex.Name) | Out-Null
    }
} finally { $taskApk.Dispose() }
Invoke-Checked (Join-Path $taskTools 'zipalign.exe') @('-f','4',(Join-Path $taskBuild 'unsigned.apk'),(Join-Path $taskBuild 'aligned.apk'))
$taskKey = Join-Path $taskRoot 'build\robot-debug.jks'
New-Item -ItemType Directory -Force (Split-Path $taskKey -Parent) | Out-Null
if(!(Test-Path -LiteralPath $taskKey)) {
    Invoke-Checked (Join-Path $taskJavaBin 'keytool.exe') @('-genkeypair','-keystore',$taskKey,'-storepass','android','-keypass','android',
        '-alias','robot-debug','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=ESP32 Robot Debug','-noprompt')
}
$taskOutput=Join-Path $taskRoot 'build\ESP32-Control-debug.apk'
$taskSignedOutput=Join-Path $taskBuild 'signed.apk'
Invoke-Checked (Join-Path $taskJavaBin 'java.exe') @('-jar',(Join-Path $taskTools 'lib\apksigner.jar'),'sign','--ks',$taskKey,
    '--ks-key-alias','robot-debug','--ks-pass','pass:android','--key-pass','pass:android','--out',$taskSignedOutput,(Join-Path $taskBuild 'aligned.apk'))
Invoke-Checked (Join-Path $taskJavaBin 'java.exe') @('-jar',(Join-Path $taskTools 'lib\apksigner.jar'),'verify','--verbose',$taskSignedOutput)
Invoke-Checked (Join-Path $taskTools 'zipalign.exe') @('-c','4',$taskSignedOutput)
Copy-Item -LiteralPath $taskSignedOutput -Destination $taskOutput -Force
if(Test-Path -LiteralPath ($taskSignedOutput+'.idsig')) { Copy-Item -LiteralPath ($taskSignedOutput+'.idsig') -Destination ($taskOutput+'.idsig') -Force }
Write-Output "APK verified: $taskOutput"
