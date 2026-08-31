[CmdletBinding()]
param(
    [string]$WorkRoot,
    [string]$SourceArchive,
    [string]$InstallRoot,
    [switch]$Download
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
if ([string]::IsNullOrWhiteSpace($WorkRoot)) {
    $WorkRoot = Join-Path $repoRoot "builds\xp-qt56"
}
$WorkRoot = [IO.Path]::GetFullPath($WorkRoot)
if ([string]::IsNullOrWhiteSpace($SourceArchive)) {
    $SourceArchive = Join-Path $WorkRoot "downloads\qtbase-opensource-src-5.6.3.zip"
}
if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = Join-Path $WorkRoot "install\5.6.3-v141_xp-x86"
}

$sourceUrl = "https://download.qt.io/new_archive/qt/5.6/5.6.3/submodules/qtbase-opensource-src-5.6.3.zip"
$expectedMd5 = "a8c4c767b71c0fd4994f4eb91534bfae"
$sourceParent = Join-Path $WorkRoot "source"
$sourceRoot = Join-Path $sourceParent "qtbase-opensource-src-5.6.3"
$buildRoot = Join-Path $WorkRoot "build"
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$vcTools = "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Tools\MSVC\14.16.27023"
$sdk71 = "C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A"
$ucrtVersion = "10.0.17763.0"
$ucrtRoot = "C:\Program Files (x86)\Windows Kits\10"

foreach ($requiredPath in @($vcvars, "$vcTools\include", "$vcTools\lib\x86",
        "$sdk71\Include", "$sdk71\Lib", "$ucrtRoot\Include\$ucrtVersion\ucrt")) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required XP build dependency is missing: $requiredPath"
    }
}

if (!(Test-Path -LiteralPath $SourceArchive)) {
    if (!$Download) {
        throw "Qt source archive is missing: $SourceArchive (pass -Download to fetch the official archive)"
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $SourceArchive) -Force | Out-Null
    Invoke-WebRequest -UseBasicParsing -Uri $sourceUrl -OutFile $SourceArchive
}

$actualMd5 = (Get-FileHash -LiteralPath $SourceArchive -Algorithm MD5).Hash.ToLowerInvariant()
if ($actualMd5 -ne $expectedMd5) {
    throw "Qt source checksum mismatch: expected $expectedMd5, got $actualMd5"
}

if (!(Test-Path -LiteralPath "$sourceRoot\configure.bat")) {
    New-Item -ItemType Directory -Path $sourceParent -Force | Out-Null
    Expand-Archive -LiteralPath $SourceArchive -DestinationPath $sourceParent -Force
}

$patchRoot = Join-Path $repoRoot "legacy\xp\patches"
$repoPrefix = $repoRoot.TrimEnd('\') + '\'
$sourceInsideRepo = $sourceRoot.StartsWith(
    $repoPrefix, [StringComparison]::OrdinalIgnoreCase)
if ($sourceInsideRepo) {
    $patchWorkingDirectory = $repoRoot
    $patchDirectory = $sourceRoot.Substring($repoPrefix.Length).Replace('\', '/')
} else {
    $patchWorkingDirectory = $sourceRoot
    $patchDirectory = $null
}
function Invoke-GitApply {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [switch]$Quiet
    )

    $savedErrorActionPreference = $ErrorActionPreference
    try {
        # A failed --check is expected when deciding whether a patch was
        # already applied. Do not let native stderr bypass the exit-code test.
        $ErrorActionPreference = 'Continue'
        if ($Quiet) {
            & git @Arguments 2>$null
        } else {
            & git @Arguments
        }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
}
foreach ($patchFile in Get-ChildItem -LiteralPath $patchRoot -Filter "*.patch" -File) {
    $applyArguments = @('-C', $patchWorkingDirectory, 'apply')
    if (![string]::IsNullOrEmpty($patchDirectory)) {
        $applyArguments += "--directory=$patchDirectory"
    }
    $applyCheck = Invoke-GitApply -Quiet -Arguments (
        $applyArguments + @('--check', $patchFile.FullName))
    if ($applyCheck -eq 0) {
        $applyResult = Invoke-GitApply -Arguments (
            $applyArguments + @($patchFile.FullName))
        if ($applyResult -ne 0) {
            throw "Failed to apply Qt XP patch: $($patchFile.Name)"
        }
        continue
    }

    $reverseCheck = Invoke-GitApply -Quiet -Arguments (
        $applyArguments + @('--reverse', '--check', $patchFile.FullName))
    if ($reverseCheck -ne 0) {
        throw "Qt XP patch is neither applicable nor already applied: $($patchFile.Name)"
    }
}
New-Item -ItemType Directory -Path $buildRoot,$InstallRoot -Force | Out-Null

$configure = @(
    "`"$sourceRoot\configure.bat`"",
    "-prefix `"$InstallRoot`"",
    "-platform win32-msvc2015",
    "-target xp",
    "-opensource -confirm-license",
    "-debug-and-release",
    "-shared",
    "-no-opengl",
    "-no-openssl",
    "-no-dbus",
    "-no-icu",
    "-qt-zlib",
    "-qt-libpng",
    "-qt-libjpeg",
    "-qt-pcre",
    "-nomake examples",
    "-nomake tests",
    "-make libs",
    "-mp"
) -join " "

$commands = @(
    "setlocal EnableDelayedExpansion",
    "call `"$vcvars`" x86",
    "set `"INCLUDE=$vcTools\include;$ucrtRoot\Include\$ucrtVersion\ucrt;$sdk71\Include`"",
    "set `"LIB=$vcTools\lib\x86;$ucrtRoot\Lib\$ucrtVersion\ucrt\x86;$sdk71\Lib`"",
    "set `"CL=/D_USING_V110_SDK71_ /D_WIN32_WINNT=0x0501 /DWINVER=0x0501 /Zc:threadSafeInit-`"",
    "cd /d `"$buildRoot`"",
    $configure,
    "`"$buildRoot\bin\qmake.exe`" `"$sourceRoot\src\src.pro`" -o `"$buildRoot\src\Makefile`"",
    "nmake",
    "nmake install"
) -join " && "

& "$env:SystemRoot\System32\cmd.exe" /d /s /c $commands
if ($LASTEXITCODE -ne 0) {
    throw "Qt 5.6.3 XP build failed with exit code $LASTEXITCODE"
}

Write-Output "QSAN_XP_QTDIR=$InstallRoot"
