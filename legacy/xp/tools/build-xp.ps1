[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$QtRoot,
    [string]$CMakeExe,
    [switch]$Deploy,
    [string]$FmodRuntime,
    [string]$AssetRoot,
    [string]$VcRedistDir,
    [string]$UcrtRedistDir,
    [string]$DeployRoot
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path

function Get-CMakeMajorMinor([string]$Path) {
    $firstLine = & $Path --version 2>$null | Select-Object -First 1
    if ($firstLine -notmatch "cmake version (\d+)\.(\d+)") {
        return $null
    }
    return @{ Major = [int]$Matches[1]; Minor = [int]$Matches[2] }
}

function Test-CMake42Plus([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or !(Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    $version = Get-CMakeMajorMinor $Path
    return $null -ne $version -and ($version.Major -gt 4 -or ($version.Major -eq 4 -and $version.Minor -ge 2))
}

if ([string]::IsNullOrWhiteSpace($CMakeExe)) {
    $candidates = @()
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -ne $cmakeCommand) {
        $candidates += $cmakeCommand.Source
    }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $vsPath = & $vswhere -latest -products * -version "[18.0,19.0)" -property installationPath
        if (![string]::IsNullOrWhiteSpace($vsPath)) {
            $candidates += (Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-CMake42Plus $candidate) {
            $CMakeExe = $candidate
            break
        }
    }
}
if (!(Test-CMake42Plus $CMakeExe)) {
    throw "CMake 4.2+ not found. Pass -CMakeExe or install the VS 2026 CMake component."
}

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = Join-Path $repoRoot "builds\xp-qt56\install\5.6.3-v141_xp-x86"
}
$QtRoot = [IO.Path]::GetFullPath($QtRoot)
if (!(Test-Path -LiteralPath (Join-Path $QtRoot "bin\qmake.exe"))) {
    throw "Qt 5.6.3 x86 is missing: $QtRoot"
}
$qtVersion = (& (Join-Path $QtRoot "bin\qmake.exe") -query QT_VERSION).Trim()
if ($qtVersion -ne "5.6.3") {
    throw "XP legacy build requires Qt 5.6.3 (got $qtVersion): $QtRoot"
}
if ([string]::IsNullOrWhiteSpace($AssetRoot)) {
    $AssetRoot = $repoRoot
}

if ($Deploy) {
    if ([string]::IsNullOrWhiteSpace($VcRedistDir)) {
        $vcRedistRoot = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2017\BuildTools\VC\Redist\MSVC"
        $VcRedistDir = Get-ChildItem -LiteralPath $vcRedistRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "x86\Microsoft.VC141.CRT" } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
            Select-Object -First 1
    }
    if ([string]::IsNullOrWhiteSpace($UcrtRedistDir)) {
        $UcrtRedistDir = "${env:ProgramFiles(x86)}\Windows Kits\10\Redist\ucrt\DLLs\x86"
    }
    if (!(Test-Path -LiteralPath $VcRedistDir -PathType Container)) {
        throw "x86 Microsoft.VC141.CRT directory is missing: $VcRedistDir"
    }
    if (!(Test-Path -LiteralPath $UcrtRedistDir -PathType Container)) {
        throw "x86 app-local UCRT directory is missing: $UcrtRedistDir"
    }
}

$oldQtRoot = $env:QSAN_XP_QTDIR
try {
    $env:QSAN_XP_QTDIR = $QtRoot
    $configureArguments = @(
        "--preset", "xp-vs2017-x86",
        "-DQt5_DIR=$([IO.Path]::GetFullPath((Join-Path $QtRoot 'lib\cmake\Qt5')))",
        "-DQSAN_XP_ASSET_ROOT=$([IO.Path]::GetFullPath($AssetRoot))"
    )
    if ($Deploy) {
        if (!(Test-Path -LiteralPath $FmodRuntime -PathType Leaf)) {
            throw "-Deploy requires the matching x86 FMOD runtime DLL: $FmodRuntime"
        }
        $fmodVersion = (Get-Item -LiteralPath $FmodRuntime).VersionInfo.FileVersion
        if ($fmodVersion -ne "4.44.53") {
            throw "XP deployment requires FMOD Ex 4.44.53 (got $fmodVersion): $FmodRuntime"
        }
        $fmodVariable = if ($Configuration -eq "Debug") {
            "QSAN_XP_FMOD_RUNTIME_DEBUG"
        } else {
            "QSAN_XP_FMOD_RUNTIME_RELEASE"
        }
        $configureArguments += "-D${fmodVariable}=$([IO.Path]::GetFullPath($FmodRuntime))"
        $configureArguments += "-DQSAN_XP_QT_ROOT=$QtRoot"
        $configureArguments += "-DQSAN_XP_VC_REDIST_DIR=$([IO.Path]::GetFullPath($VcRedistDir))"
        $configureArguments += "-DQSAN_XP_UCRT_REDIST_DIR=$([IO.Path]::GetFullPath($UcrtRedistDir))"
        if (![string]::IsNullOrWhiteSpace($DeployRoot)) {
            $configureArguments += "-DQSAN_XP_DEPLOY_ROOT=$([IO.Path]::GetFullPath($DeployRoot))"
        } else {
            $configureArguments += "-DQSAN_XP_DEPLOY_ROOT="
        }
    }

    & $CMakeExe @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "XP CMake configure failed with exit code $LASTEXITCODE"
    }

    $buildPreset = if ($Deploy) {
        "xp-deploy-$($Configuration.ToLowerInvariant())"
    } else {
        "xp-$($Configuration.ToLowerInvariant())"
    }
    & $CMakeExe --build --preset $buildPreset --parallel 8
    if ($LASTEXITCODE -ne 0) {
        throw "XP CMake build failed with exit code $LASTEXITCODE"
    }

    $outputDir = if ($Configuration -eq "Debug") { "xp-debug" } else { "xp-release" }
    $peCheckArguments = @{
        Executable = Join-Path $repoRoot "$outputDir\QSanguoshaXP.exe"
        QtRoot = $QtRoot
        Configuration = $Configuration
    }
    if ($Deploy) {
        $fmodName = if ($Configuration -eq "Debug") { "fmodexL.dll" } else { "fmodex.dll" }
        $portableRoot = if ([string]::IsNullOrWhiteSpace($DeployRoot)) {
            Join-Path $repoRoot $outputDir
        } else {
            [IO.Path]::GetFullPath($DeployRoot)
        }
        $peCheckArguments.Executable = Join-Path $portableRoot "QSanguoshaXP.exe"
        $peCheckArguments.FmodRuntime = Join-Path $portableRoot $fmodName
        $peCheckArguments.DeploymentRoot = $portableRoot
    }
    & (Join-Path $PSScriptRoot "check-xp-pe.ps1") @peCheckArguments
    if ($LASTEXITCODE -ne 0) {
        throw "XP PE validation failed with exit code $LASTEXITCODE"
    }
} finally {
    $env:QSAN_XP_QTDIR = $oldQtRoot
}
