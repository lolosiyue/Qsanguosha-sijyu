[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [Parameter(Mandatory = $true)]
    [string]$QtRoot,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$FmodRuntime,
    [string]$DeploymentRoot,
    [string]$Dumpbin = "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Tools\MSVC\14.16.27023\bin\HostX86\x86\dumpbin.exe"
)

$ErrorActionPreference = "Stop"

function Get-PortableExecutableHeader([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "Not a PE file: $Path"
    }

    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    $signatureIsInvalid = $peOffset -ge 0 -and $peOffset + 96 -lt $bytes.Length `
        -and [BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550
    if ($peOffset -lt 0 -or $peOffset + 96 -ge $bytes.Length -or $signatureIsInvalid) {
        throw "Invalid PE signature: $Path"
    }

    $optional = $peOffset + 24
    [pscustomobject]@{
        Path = $Path
        Machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
        OsMajor = [BitConverter]::ToUInt16($bytes, $optional + 40)
        OsMinor = [BitConverter]::ToUInt16($bytes, $optional + 42)
        SubsystemMajor = [BitConverter]::ToUInt16($bytes, $optional + 48)
        SubsystemMinor = [BitConverter]::ToUInt16($bytes, $optional + 50)
        Subsystem = [BitConverter]::ToUInt16($bytes, $optional + 68)
    }
}

function Assert-XpHeader([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "XP validation input is missing: $Path"
    }

    $header = Get-PortableExecutableHeader $Path
    if ($header.Machine -ne 0x014c) {
        throw "XP binary is not x86 (machine 0x$('{0:X4}' -f $header.Machine)): $Path"
    }
    if ($header.OsMajor -ne 5 -or $header.OsMinor -ne 1) {
        throw "XP binary OS version is $($header.OsMajor).$('{0:D2}' -f $header.OsMinor), expected 5.01: $Path"
    }
    if ($header.SubsystemMajor -ne 5 -or $header.SubsystemMinor -ne 1) {
        throw "XP binary subsystem version is $($header.SubsystemMajor).$('{0:D2}' -f $header.SubsystemMinor), expected 5.01: $Path"
    }

    Write-Output ("XP_PE_OK {0} machine=x86 os={1}.{2:D2} subsystem={3}.{4:D2}" -f
        $Path, $header.OsMajor, $header.OsMinor,
        $header.SubsystemMajor, $header.SubsystemMinor)
}

function Assert-X86Header([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "XP validation input is missing: $Path"
    }
    $header = Get-PortableExecutableHeader $Path
    if ($header.Machine -ne 0x014c) {
        throw "XP deployment contains a non-x86 binary: $Path"
    }
}

if (!(Test-Path -LiteralPath $Dumpbin -PathType Leaf)) {
    throw "dumpbin.exe is missing: $Dumpbin"
}

$debugSuffix = if ($Configuration -eq "Debug") { "d" } else { "" }
$inputs = @(
    [pscustomobject]@{ Path = [IO.Path]::GetFullPath($Executable); RequireXpHeader = $true },
    [pscustomobject]@{ Path = [IO.Path]::GetFullPath((Join-Path $QtRoot "bin\Qt5Core${debugSuffix}.dll")); RequireXpHeader = $false },
    [pscustomobject]@{ Path = [IO.Path]::GetFullPath((Join-Path $QtRoot "bin\Qt5Gui${debugSuffix}.dll")); RequireXpHeader = $false },
    [pscustomobject]@{ Path = [IO.Path]::GetFullPath((Join-Path $QtRoot "bin\Qt5Network${debugSuffix}.dll")); RequireXpHeader = $false },
    [pscustomobject]@{ Path = [IO.Path]::GetFullPath((Join-Path $QtRoot "bin\Qt5Widgets${debugSuffix}.dll")); RequireXpHeader = $false },
    [pscustomobject]@{ Path = [IO.Path]::GetFullPath((Join-Path $QtRoot "plugins\platforms\qwindows${debugSuffix}.dll")); RequireXpHeader = $false }
)
if (![string]::IsNullOrWhiteSpace($FmodRuntime)) {
    $inputs += [pscustomobject]@{
        Path = [IO.Path]::GetFullPath($FmodRuntime)
        RequireXpHeader = $true
    }
}

# These APIs were introduced after XP. Dynamic GetProcAddress use is allowed;
# only a loader-visible import is rejected here.
$postXpImports = @(
    "AddDllDirectory",
    "CancelIoEx",
    "CreateSymbolicLinkW",
    "GetFileInformationByHandleEx",
    "GetFinalPathNameByHandleW",
    "GetSystemTimePreciseAsFileTime",
    "GetThreadId",
    "GetTickCount64",
    "GetUserDefaultLocaleName",
    "InitializeCriticalSectionEx",
    "QueryFullProcessImageNameW",
    "RemoveDllDirectory",
    "SetDefaultDllDirectories",
    "SetThreadStackGuarantee"
)

foreach ($input in $inputs) {
    $inputPath = $input.Path
    if ($input.RequireXpHeader) {
        Assert-XpHeader $inputPath
    } else {
        # Qt's official 5.6.3 MSVC binaries stamp 6.00 in their DLL headers,
        # but the same binaries are exercised in the XP guest. Keep the hard
        # 5.01 gate on our EXE/FMOD and validate vendor Qt by x86 + imports.
        Assert-X86Header $inputPath
        $qtHeader = Get-PortableExecutableHeader $inputPath
        Write-Output ("XP_QT_VENDOR_PE_INFO {0} os={1}.{2:D2} subsystem={3}.{4:D2}" -f
            $inputPath, $qtHeader.OsMajor, $qtHeader.OsMinor,
            $qtHeader.SubsystemMajor, $qtHeader.SubsystemMinor)
    }
    $imports = (& $Dumpbin /nologo /imports $inputPath 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin /imports failed for $inputPath"
    }
    foreach ($api in $postXpImports) {
        if ($imports -match "(?m)^\s+(?:[0-9A-Fa-f]+\s+)+$([regex]::Escape($api))\s*$") {
            throw "Post-XP direct import '$api' found in $inputPath"
        }
    }
}

Write-Output "XP_IMPORTS_OK checked=$($inputs.Count)"

if (![string]::IsNullOrWhiteSpace($DeploymentRoot)) {
    if (!(Test-Path -LiteralPath $DeploymentRoot -PathType Container)) {
        throw "XP deployment root is missing: $DeploymentRoot"
    }
    $deploymentInputs = Get-ChildItem -LiteralPath $DeploymentRoot -Filter "*.dll" -File -Recurse
    foreach ($inputFile in $deploymentInputs) {
        Assert-X86Header $inputFile.FullName
    }
    Write-Output "XP_DEPLOYMENT_X86_OK checked=$($deploymentInputs.Count)"
}
