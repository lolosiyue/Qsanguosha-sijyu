param(
    [string]$QtRoot = 'C:\Qt\6.5.3\msvc2019_64',
    [string]$CMakeExe = '',
    [switch]$Deploy,
    [string]$FmodRuntime = ''
)

$arguments = @{
    Configuration = 'Release'
    QtRoot = $QtRoot
    Deploy = $Deploy
}
if (-not [string]::IsNullOrWhiteSpace($CMakeExe)) {
    $arguments.CMakeExe = $CMakeExe
}
if (-not [string]::IsNullOrWhiteSpace($FmodRuntime)) {
    $arguments.FmodRuntime = $FmodRuntime
}

& (Join-Path $PSScriptRoot 'build-cmake.ps1') @arguments
exit $LASTEXITCODE
