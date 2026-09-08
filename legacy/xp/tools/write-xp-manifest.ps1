[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PayloadRoot,
    [Parameter(Mandatory = $true)][string]$BuildIdentity,
    [string]$SourceRoot = ''
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($BuildIdentity)) {
    throw 'BuildIdentity must not be empty'
}
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
}
$root = (Resolve-Path -LiteralPath $PayloadRoot).Path
$source = (Resolve-Path -LiteralPath $SourceRoot).Path
$files = [Collections.Generic.List[object]]::new()
# Record rule/extension content independently of the binary source identity.
foreach ($directory in @('lua', 'extensions', 'lang')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root $directory) -File -Recurse) {
        $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
        if ($relative -match '/(\.git|logs|temp|data)/') { continue }
        $files.Add([ordered]@{path=$relative; bytes=$file.Length;
            sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()})
    }
}
$executables = @('QSanguoshaXP.exe', 'QSanguoshaXPServer.exe') | ForEach-Object {
    $path = Join-Path $root $_
    [ordered]@{path=$_; bytes=(Get-Item -LiteralPath $path).Length;
        sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$manifest = [ordered]@{
    schema=1
    sourceHead=(& git -C $source rev-parse HEAD)
    sourceDirty=(@(& git -C $source status --porcelain -uno).Count -gt 0)
    buildIdentity=$BuildIdentity
    toolchain='v141_xp Win32; Qt 5.6.3; XP SP3 x86'
    executables=$executables
    runtimeFiles=@($files | Sort-Object path)
}
$path = Join-Path $root 'xp-payload-manifest.json'
$partial = "$path.partial"
try {
    [IO.File]::WriteAllText($partial, ($manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $partial -Destination $path -Force
} finally {
    if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
}
Write-Output "XP_MANIFEST=$path"
Write-Output "XP_MANIFEST_SHA256=$((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash)"
Write-Output "XP_RUNTIME_FILES=$($files.Count)"
