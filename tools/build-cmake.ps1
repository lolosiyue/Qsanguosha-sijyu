param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$QtRoot = 'C:\Qt\6.5.3\msvc2019_64',
    [string]$CMakeExe = '',
    [switch]$Deploy,
    [string]$FmodRuntime = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($CMakeExe)) {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -ne $cmakeCommand) {
        $CMakeExe = $cmakeCommand.Source
    } else {
        $CMakeExe = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
    }
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

    $configureArguments = @('--preset', 'vs2019-x64')
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
