[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$RepositoryRoot,
    [string]$QtRoot = 'H:\Qt6111\6.11.1\msvc2022_64',
    [string]$BuildDirectory = 'builds\cmake-vs2026'
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
    $RepositoryRoot = Split-Path -Parent $scriptRoot
}

function Require-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }
}

function Copy-IfDifferent([string]$Source, [string]$Destination) {
    Require-File $Source 'Runtime source'
    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
        if ($sourceHash -eq $destinationHash) {
            return
        }
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

$root = [IO.Path]::GetFullPath($RepositoryRoot)
$qtRootPath = [IO.Path]::GetFullPath($QtRoot)
$buildPath = [IO.Path]::GetFullPath((Join-Path $root $BuildDirectory))
$outputName = if ($Configuration -eq 'Debug') { 'excel-debug' } else { 'excel-release' }
$outputPath = Join-Path $root $outputName
$windeployqt = Join-Path $qtRootPath 'bin\windeployqt.exe'
$qtConfig = if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }

Require-File $windeployqt 'Qt windeployqt'
Require-File (Join-Path $buildPath 'CMakeCache.txt') 'CMake cache'
if (-not (Test-Path -LiteralPath $outputPath -PathType Container)) {
    throw "Runtime output directory not found: $outputPath"
}

$executables = @(
    (Join-Path $outputPath 'QSanguoshaSheetsBridge.exe'),
    (Join-Path $outputPath 'QSanguoshaExcelServer.exe')
)
foreach ($executable in $executables) {
    Require-File $executable 'Sheets runtime executable'
}

# windeployqt is the Qt supported deployment mechanism; invoke it once per
# executable so each target's Qt imports and plugins are resolved.
foreach ($executable in $executables) {
    Write-Host "Deploying Qt runtime for $(Split-Path -Leaf $executable) ($Configuration)..."
    & $windeployqt $qtConfig '--compiler-runtime' $executable
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed for $executable with exit code $LASTEXITCODE"
    }
}

$cacheText = Get-Content -LiteralPath (Join-Path $buildPath 'CMakeCache.txt') -Raw
$audioMatch = [regex]::Match($cacheText, '(?m)^QSAN_AUDIO_BACKEND:STRING=([^\r\n]*)')
if (-not $audioMatch.Success) {
    throw 'QSAN_AUDIO_BACKEND is missing from the CMake cache; refusing to guess native runtime dependencies.'
}

if ($audioMatch.Groups[1].Value.Trim() -eq 'FMOD') {
    $fmodName = if ($Configuration -eq 'Debug') { 'fmodexL64.dll' } else { 'fmodex64.dll' }
    $fmodSource = Join-Path $root (Join-Path 'TODO\anime' $fmodName)
    # The dedicated server has no audio sources/import; FMOD is a bridge-only
    # runtime dependency even though both targets share the output directory.
    Copy-IfDifferent $fmodSource (Join-Path (Split-Path -Parent $executables[0]) $fmodName)
    Write-Host "Copied FMOD runtime $fmodName for the configured FMOD backend."
}

# Fail early if a required Qt loader DLL is still absent after deployment.
$qtCoreName = if ($Configuration -eq 'Debug') { 'Qt6Cored.dll' } else { 'Qt6Core.dll' }
foreach ($executable in $executables) {
    $deployedQtCore = Join-Path (Split-Path -Parent $executable) $qtCoreName
    Require-File $deployedQtCore 'Deployed Qt Core runtime'
}

Write-Host "Sheets/Excel runtime deployment complete: $outputPath"
