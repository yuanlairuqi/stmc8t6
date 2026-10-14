function New-AsciiNativeWorkspace([string]$SourceRoot) {
    $tempBase=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if($tempBase -match '[^\x00-\x7F]') { throw 'TEMP contains non-ASCII characters; choose an ASCII TEMP directory for native tools' }
    $resolved=[IO.Path]::GetFullPath($SourceRoot)
    $hasher=[Security.Cryptography.SHA256]::Create()
    try {$id=[BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($resolved))).Replace('-','').Substring(0,16)}
    finally {$hasher.Dispose()}
    $work=Join-Path $tempBase ('motor-native-'+$id)
    $marker=Join-Path $work '.source-root'
    if(Test-Path -LiteralPath $work) {
        if(-not(Test-Path -LiteralPath $marker) -or [IO.File]::ReadAllText($marker) -ne $resolved) {
            throw "Unowned native workspace: $work"
        }
        if((Get-Item -LiteralPath $work).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked native workspace rejected' }
    } else {New-Item -ItemType Directory -Path $work | Out-Null; [IO.File]::WriteAllText($marker,$resolved,[Text.UTF8Encoding]::new($false))}
    return $work
}
function Sync-NativeSourceDirectory([string]$Source,[string]$Workspace,[string]$Name) {
    if($Name -match '[/\\.]') { throw 'Native source directory name must be a single safe segment' }
    $destination=[IO.Path]::GetFullPath((Join-Path $Workspace $Name))
    $prefix=[IO.Path]::GetFullPath($Workspace).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if(-not $destination.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Native snapshot path escaped workspace' }
    if(Test-Path -LiteralPath $destination) {
        if((Get-Item -LiteralPath $destination).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked native snapshot rejected' }
        Remove-Item -LiteralPath $destination -Recurse -Force
    }
    Copy-Item -LiteralPath $Source -Destination $destination -Recurse
}
