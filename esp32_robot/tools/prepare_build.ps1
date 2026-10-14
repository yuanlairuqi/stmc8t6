function Initialize-ProjectBuildDirectory([string]$ProjectRoot, [string]$BuildDirectory) {
    $source=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\','/')
    $build=[IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\','/')
    $cache=Join-Path $build 'CMakeCache.txt'
    if(-not (Test-Path -LiteralPath $cache)) { return }
    $cacheHome=Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1
    if(-not $cacheHome) { throw "Invalid CMake cache, choose a fresh build directory: $build" }
    $recorded=[IO.Path]::GetFullPath($cacheHome.Matches[0].Groups[1].Value).TrimEnd('\','/')
    if($source.Equals($recorded,[StringComparison]::OrdinalIgnoreCase)) { return }
    $prefix=$source+[IO.Path]::DirectorySeparatorChar
    if(-not $build.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) {
        throw "Relocated cache is outside project; choose a new build directory: $build"
    }
    if((Get-Item -LiteralPath $build).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Refusing to move a linked build directory: $build"
    }
    $archiveRoot=Join-Path $source 'backups/relocated-builds'
    $archive=Join-Path $archiveRoot ((Split-Path $build -Leaf)+'-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
    $archive=[IO.Path]::GetFullPath($archive)
    if(-not $archive.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Archive path escaped project' }
    New-Item -ItemType Directory -Force -Path $archiveRoot | Out-Null
    Move-Item -LiteralPath $build -Destination $archive
    Write-Output "Archived cache from another source directory: $archive"
}
