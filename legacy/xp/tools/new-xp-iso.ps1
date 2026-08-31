[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDirectory,
    [Parameter(Mandatory = $true)]
    [string]$IsoPath,
    [ValidatePattern("^[A-Za-z0-9_]{1,32}$")]
    [string]$VolumeLabel = "QSAN_XP",
    [switch]$ReuseStage,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$source = (Resolve-Path -LiteralPath $SourceDirectory).Path
$destination = [IO.Path]::GetFullPath($IsoPath)
$temporary = "$destination.partial"
$stage = "$destination.stage"

if (!(Test-Path -LiteralPath $source -PathType Container)) {
    throw "ISO source directory is missing: $source"
}
if ((Test-Path -LiteralPath $destination) -and !$Force) {
    throw "ISO already exists; pass -Force to replace it: $destination"
}
if (Test-Path -LiteralPath $temporary) {
    if (!$Force) {
        throw "Partial ISO exists; pass -Force to replace it: $temporary"
    }
    Remove-Item -LiteralPath $temporary -Force
}
if ($ReuseStage) {
    if (!(Test-Path -LiteralPath $stage -PathType Container)) {
        throw "Reusable ISO stage is missing: $stage"
    }
    if (!(Test-Path -LiteralPath (Join-Path $stage "PAYLOAD\XP-PAYLOAD.OK") -PathType Leaf)) {
        throw "Reusable ISO stage is incomplete: $stage"
    }
} elseif (Test-Path -LiteralPath $stage) {
    if (!$Force) {
        throw "Generated ISO stage exists; pass -Force to replace it: $stage"
    }
    Remove-Item -LiteralPath $stage -Recurse -Force
}

$parent = Split-Path -Parent $destination
if (!(Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
if (!$ReuseStage) {
    New-Item -ItemType Directory -Path $stage | Out-Null

    # Keep the portable layout intact. XP's EXPAND.EXE flattens CAB destination
    # directories, while a Joliet tree plus XCOPY preserves plugins and assets.
    $payloadRoot = Join-Path $stage "PAYLOAD"
    New-Item -ItemType Directory -Path $payloadRoot | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $source -Force) {
        Copy-Item -LiteralPath $item.FullName -Destination $payloadRoot -Recurse -Force
    }
    [IO.File]::WriteAllText((Join-Path $payloadRoot "XP-PAYLOAD.OK"),
        "QSanguosha XP payload complete`r`n", [Text.Encoding]::ASCII)
    $payloadFiles = @(Get-ChildItem -LiteralPath $payloadRoot -File -Recurse)
    $payloadBytes = ($payloadFiles | Measure-Object -Property Length -Sum).Sum
    Write-Output "XP_ISO_PAYLOAD_READY=$payloadRoot files=$($payloadFiles.Count) bytes=$payloadBytes"

}

# Refresh helpers even when the 2.5 GB payload stage is reused.
$mediaAssets = Join-Path $PSScriptRoot "..\assets\packed-media"
foreach ($helper in @("AUTORUN.INF", "INSTALL.CMD", "RUNXP.CMD")) {
    Copy-Item -LiteralPath (Join-Path $mediaAssets $helper) -Destination $stage -Force
}

$image = New-Object -ComObject IMAPI2FS.MsftFileSystemImage
# IMAPI rejects the read-only DVD-ROM enum here; DISK (0x0c) supplies
# unrestricted image defaults, then ISO9660 + Joliet are selected explicitly.
$image.ChooseImageDefaultsForMediaType(12)
$image.FileSystemsToCreate = 3
$image.VolumeName = $VolumeLabel
$image.Root.AddTree($stage, $false)
$result = $image.CreateResultImage()
if (!("QSanComStreamCopy" -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot "ComStreamCopy.cs")
}
$isoLength = [long]$result.TotalBlocks * [long]$result.BlockSize
$imageStream = $result.ImageStream
[QSanComStreamCopy]::CopyToFile($imageStream, $temporary, $isoLength)
[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($imageStream)
[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($result)
[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($image)

if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Force
}
Move-Item -LiteralPath $temporary -Destination $destination
$hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
Write-Output "XP_ISO_READY=$destination"
Write-Output "XP_ISO_SHA256=$hash"
Write-Output "XP_ISO_STAGE=$stage"
