param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$QtRoot = 'H:\Qt6111\6.11.1\msvc2022_64',
    [string]$CMakeExe = '',
    [switch]$Deploy,
    [string]$FmodRuntime = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

function Get-CMakeMajorMinor([string]$cmakePath) {
    $firstLine = & $cmakePath --version 2>$null | Select-Object -First 1
    if ($firstLine -notmatch 'cmake version (\d+)\.(\d+)') {
        return $null
    }
    return @{
        Major = [int]$Matches[1]
        Minor = [int]$Matches[2]
    }
}

function Test-CMake42Plus([string]$cmakePath) {
    if ([string]::IsNullOrWhiteSpace($cmakePath) -or -not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
        return $false
    }
    $ver = Get-CMakeMajorMinor $cmakePath
    if ($null -eq $ver) {
        return $false
    }
    return ($ver.Major -gt 4) -or ($ver.Major -eq 4 -and $ver.Minor -ge 2)
}

if ([string]::IsNullOrWhiteSpace($CMakeExe)) {
    $candidates = @()
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -ne $cmakeCommand) {
        $candidates += $cmakeCommand.Source
    }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $vsPath = & $vswhere -latest -products * -version '[18.0,19.0)' -property installationPath
        if (-not [string]::IsNullOrWhiteSpace($vsPath)) {
            $candidates += (Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-CMake42Plus $candidate) {
            $CMakeExe = $candidate
            break
        }
    }
}

if (-not (Test-CMake42Plus $CMakeExe)) {
    throw 'CMake 4.2+ not found. Use Visual Studio 2026 bundled CMake or install CMake 4.2+. Do not use C:\Qt\Tools\CMake_64.'
}

foreach ($required in @($CMakeExe, (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required build dependency not found: $required"
    }
}

if ($Deploy -and $Configuration -eq 'Release') {
    if ([string]::IsNullOrWhiteSpace($FmodRuntime) -or -not (Test-Path -LiteralPath $FmodRuntime -PathType Leaf)) {
        throw 'Release deployment requires a valid -FmodRuntime path.'
    }
}

$oldQtDir = $env:QTDIR
try {
    $env:QTDIR = $QtRoot

    $configureArguments = @('--preset', 'vs2026-x64')
    if (-not [string]::IsNullOrWhiteSpace($FmodRuntime)) {
        $configureArguments += "-DQSAN_FMOD_RUNTIME=$FmodRuntime"
    }

    Push-Location $repoRoot
    try {
        & $CMakeExe @configureArguments
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }

        $buildPreset = $Configuration.ToLowerInvariant()
        & $CMakeExe --build --preset $buildPreset
        if ($LASTEXITCODE -ne 0) { throw "CMake $Configuration build failed: $LASTEXITCODE" }

        if ($Deploy) {
            & $CMakeExe --build --preset "deploy-$buildPreset"
            if ($LASTEXITCODE -ne 0) { throw "CMake $Configuration deployment failed: $LASTEXITCODE" }
        }
    } finally {
        Pop-Location
    }
} finally {
    if ($null -eq $oldQtDir) {
        Remove-Item Env:QTDIR -ErrorAction SilentlyContinue
    } else {
        $env:QTDIR = $oldQtDir
    }
}

$executable = Join-Path $repoRoot "$($Configuration.ToLowerInvariant())\QSanguosha.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Build completed without the executable: $executable"
}

$artifact = Get-Item -LiteralPath $executable
$hash = Get-FileHash -LiteralPath $executable -Algorithm SHA256
Write-Output "CMake $Configuration x64 build succeeded: $($artifact.FullName)"
Write-Output "SHA-256: $($hash.Hash)"
