param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$ToolchainRoot = '',
    [string]$QtRoot = '',
    [string]$QtHostPath = 'H:\Qt6111\6.11.1\msvc2022_64',
    [string]$SdkRoot = '',
    [string]$NdkRoot = '',
    [string]$JavaRoot = '',
    [string]$CMakeExe = '',
    [string]$NinjaExe = 'H:\Qt6111\Tools\Ninja\ninja.exe',
    [ValidateRange(1, 32)]
    [int]$Parallel = 8
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (!$ToolchainRoot) { $ToolchainRoot = Join-Path $repoRoot 'builds/android-toolchain' }
if (!$QtRoot) { $QtRoot = Join-Path $ToolchainRoot 'qt/6.11.1/android_arm64_v8a' }
if (!$SdkRoot) { $SdkRoot = Join-Path $ToolchainRoot 'sdk' }
if (!$NdkRoot) {
    $NdkRoot = Join-Path $env:LOCALAPPDATA 'Android/Sdk/ndk/27.2.12479018'
}
if (!$JavaRoot) {
    $javaCandidates = @(Get-ChildItem -LiteralPath (Join-Path $ToolchainRoot 'jdk') -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin/javac.exe') })
    if ($javaCandidates.Count -ne 1) { throw 'Specify -JavaRoot pointing to JDK 21.' }
    $JavaRoot = $javaCandidates[0].FullName
}
if (!$CMakeExe) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $CMakeExe = & $vswhere -latest -products '*' -find 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' |
            Select-Object -First 1
    }
}

$freeTypeSource = Join-Path $ToolchainRoot 'src/freetype-2.14.3'
$freeTypeBuild = Join-Path $ToolchainRoot 'freetype-build-arm64'
$freeTypeRoot = Join-Path $ToolchainRoot 'freetype-arm64'
foreach ($required in @(
    $CMakeExe, $NinjaExe,
    (Join-Path $QtRoot 'lib/cmake/Qt6/qt.toolchain.cmake'),
    (Join-Path $QtHostPath 'bin/moc.exe'),
    (Join-Path $JavaRoot 'bin/javac.exe'),
    (Join-Path $NdkRoot 'build/cmake/android.toolchain.cmake'),
    (Join-Path $SdkRoot 'platforms/android-36/android.jar'),
    (Join-Path $SdkRoot 'build-tools/36.0.0/zipalign.exe'),
    (Join-Path $freeTypeSource 'CMakeLists.txt')
)) {
    if (!$required -or !(Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Android build prerequisite is missing: $required"
    }
}
if ((Get-Content -LiteralPath (Join-Path $NdkRoot 'source.properties') -Raw) -notmatch 'Pkg.Revision\s*=\s*27\.2\.12479018') {
    throw 'Android requires NDK 27.2.12479018.'
}
$javaVersion = & (Join-Path $JavaRoot 'bin/javac.exe') -version 2>&1
if ("$javaVersion" -notmatch '^javac 21\.') { throw "Android requires JDK 21; found $javaVersion" }

# Keep all Java/Gradle caches and target products out of desktop build trees.
$environment = @{
    QSAN_ANDROID_QT_ROOT = $QtRoot
    QSAN_QT_HOST_PATH = $QtHostPath
    QSAN_ANDROID_FREETYPE_ROOT = $freeTypeRoot
    ANDROID_SDK_ROOT = $SdkRoot
    ANDROID_HOME = $SdkRoot
    ANDROID_NDK_ROOT = $NdkRoot
    JAVA_HOME = $JavaRoot
    ANDROID_USER_HOME = (Join-Path $ToolchainRoot 'android-user')
    GRADLE_USER_HOME = (Join-Path $ToolchainRoot 'gradle')
    PATH = "$(Join-Path $JavaRoot 'bin');$(Split-Path -Parent $NinjaExe);$(Join-Path $QtHostPath 'bin');$env:PATH"
}
$savedEnvironment = @{}
foreach ($key in $environment.Keys) {
    $savedEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    [Environment]::SetEnvironmentVariable($key, $environment[$key], 'Process')
}

function Invoke-CMake([string[]]$CMakeArguments) {
    & $CMakeExe @CMakeArguments
    if ($LASTEXITCODE -ne 0) { throw "CMake failed ($LASTEXITCODE): $($CMakeArguments -join ' ')" }
}

Push-Location $repoRoot
try {
    # FreeType is an Android static dependency; never rewrite include/ or lib/.
    if (!(Test-Path -LiteralPath (Join-Path $freeTypeRoot 'lib/libfreetype.a'))) {
        Invoke-CMake -CMakeArguments @('-S', $freeTypeSource, '-B', $freeTypeBuild, '-G', 'Ninja',
            "-DCMAKE_MAKE_PROGRAM=$NinjaExe", "-DCMAKE_TOOLCHAIN_FILE=$NdkRoot/build/cmake/android.toolchain.cmake",
            '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-28',
            '-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON', '-DCMAKE_BUILD_TYPE=Release',
            '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', "-DCMAKE_INSTALL_PREFIX=$freeTypeRoot",
            '-DBUILD_SHARED_LIBS=OFF', '-DFT_DISABLE_ZLIB=ON', '-DFT_DISABLE_BZIP2=ON',
            '-DFT_DISABLE_PNG=ON', '-DFT_DISABLE_HARFBUZZ=ON', '-DFT_DISABLE_BROTLI=ON')
        Invoke-CMake -CMakeArguments @('--build', $freeTypeBuild, '--parallel', "$Parallel")
        Invoke-CMake -CMakeArguments @('--install', $freeTypeBuild)
    }
    $preset = "android-arm64-$($Configuration.ToLowerInvariant())"
    Invoke-CMake -CMakeArguments @('--preset', $preset, "-DCMAKE_MAKE_PROGRAM=$NinjaExe")
    Invoke-CMake -CMakeArguments @('--build', '--preset', "$preset-apk", '--parallel', "$Parallel")
} finally {
    Pop-Location
    foreach ($key in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($key, $savedEnvironment[$key], 'Process')
    }
}
