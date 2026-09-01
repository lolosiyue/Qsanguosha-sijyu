# Cloudflare R2：同步執行期資源（image/audio/qml/font）。
# 用法：
#   powershell -NoProfile -File tools/r2/r2.ps1 whoami
#   powershell -NoProfile -File tools/r2/r2.ps1 setup
#   powershell -NoProfile -File tools/r2/r2.ps1 sync
#   powershell -NoProfile -File tools/r2/r2.ps1 list
#   powershell -NoProfile -File tools/r2/r2.ps1 url
param(
    [Parameter(Position = 0)]
    [ValidateSet('whoami', 'setup', 'sync', 'put', 'list', 'url')]
    [string]$Command = 'setup',

    [string]$File = '',
    [string]$Key = '',
    [string]$Bucket = 'qsanguosha-sijyu-resources',
    [string]$OldBucket = 'qsanguosha-sijyu-releases',
    [string[]]$Roots = @('image', 'audio', 'qml', 'font', 'hero-skin'),
    [ValidateSet('weur', 'eeur', 'apac', 'wnam', 'enam', 'oc')]
    [string]$Location = 'apac',
    [int]$Concurrency = 20,
    [switch]$Resume,
    [int]$Skip = 0
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$ConfigFile = Join-Path $PSScriptRoot 'wrangler.toml'
$ManifestDir = Join-Path $PSScriptRoot '.wrangler'
$Npx = (Get-Command npx.cmd -ErrorAction Stop).Source

function Invoke-Wrangler {
    param([Parameter(Mandatory = $true)][string[]]$WranglerArgs)
    & $Npx --yes wrangler@4 --config $ConfigFile @WranglerArgs
    if ($LASTEXITCODE -ne 0) {
        throw "wrangler failed ($LASTEXITCODE): $($WranglerArgs -join ' ')"
    }
}

function Get-ResourceManifestPath {
    if (-not (Test-Path -LiteralPath $ManifestDir)) {
        New-Item -ItemType Directory -Path $ManifestDir | Out-Null
    }
    return (Join-Path $ManifestDir 'sync-manifest.json')
}

function Write-ResourceManifest {
    $manifestPath = Get-ResourceManifestPath
    $utf8 = New-Object System.Text.UTF8Encoding $false
    $writer = New-Object System.IO.StreamWriter($manifestPath, $false, $utf8)
    try {
        $writer.Write('[')
        $first = $true
        $count = 0
        foreach ($rootName in $Roots) {
            $rootPath = Join-Path $RepoRoot $rootName
            if (-not (Test-Path -LiteralPath $rootPath)) {
                Write-Host "skip missing: $rootName"
                continue
            }
            Get-ChildItem -LiteralPath $rootPath -Recurse -File -ErrorAction Stop | ForEach-Object {
                $rel = $_.FullName.Substring($RepoRoot.Length).TrimStart('\', '/').Replace('\', '/')
                $keyJson = $rel | ConvertTo-Json -Compress
                $fileJson = $_.FullName | ConvertTo-Json -Compress
                if (-not $first) { $writer.Write(',') }
                $first = $false
                $writer.Write('{"key":')
                $writer.Write($keyJson)
                $writer.Write(',"file":')
                $writer.Write($fileJson)
                $writer.Write('}')
                $count++
            }
        }
        $writer.Write(']')
        Write-Host "manifest files=$count path=$manifestPath"
    }
    finally {
        $writer.Close()
    }
    return $manifestPath
}

switch ($Command) {
    'whoami' {
        Invoke-Wrangler @('whoami')
    }
    'setup' {
        $listOutput = & $Npx --yes wrangler@4 --config $ConfigFile r2 bucket list
        if ($LASTEXITCODE -ne 0) {
            throw 'wrangler r2 bucket list failed'
        }
        if ($listOutput -notmatch [regex]::Escape($Bucket)) {
            Invoke-Wrangler @('r2', 'bucket', 'create', $Bucket, '--location', $Location)
        }
        else {
            Write-Host "bucket exists: $Bucket"
        }
        if ($listOutput -match [regex]::Escape($OldBucket)) {
            Write-Host "removing empty bucket: $OldBucket"
            Invoke-Wrangler @('r2', 'bucket', 'delete', $OldBucket)
        }
        Invoke-Wrangler @('r2', 'bucket', 'dev-url', 'enable', $Bucket, '--force')
        Invoke-Wrangler @('r2', 'bucket', 'dev-url', 'get', $Bucket)
        Invoke-Wrangler @('r2', 'bucket', 'info', $Bucket)
    }
    'sync' {
        $progressPath = Join-Path $ManifestDir 'sync-progress.json'
        if ($Resume) {
            if (-not (Test-Path -LiteralPath $progressPath)) {
                throw "missing progress file: $progressPath"
            }
            $progress = Get-Content -LiteralPath $progressPath -Raw -Encoding UTF8 | ConvertFrom-Json
            $Skip = [int]$progress.resume_index
            Write-Host "resume from index=$Skip key=$($progress.verified_remote.next_key)"
        }
        $manifestPath = Write-ResourceManifest
        if ($Skip -gt 0) {
            $sliced = Join-Path $ManifestDir 'sync-manifest-resume.json'
            node -e "const fs=require('fs'); const j=JSON.parse(fs.readFileSync(process.argv[1],'utf8')); const n=Number(process.argv[2]); fs.writeFileSync(process.argv[3], JSON.stringify(j.slice(n))); console.log('sliced', n, '->', j.length-n);" $manifestPath $Skip $sliced
            if ($LASTEXITCODE -ne 0) { throw 'slice manifest failed' }
            $manifestPath = $sliced
        }
        Invoke-Wrangler @(
            'r2', 'bulk', 'put', $Bucket,
            '--filename', $manifestPath,
            '--remote',
            '--concurrency', "$Concurrency"
        )
    }
    'put' {
        if ([string]::IsNullOrWhiteSpace($File)) {
            throw 'put requires -File'
        }
        $resolved = (Resolve-Path -LiteralPath $File).Path
        if ([string]::IsNullOrWhiteSpace($Key)) {
            if ($resolved.StartsWith($RepoRoot, [StringComparison]::OrdinalIgnoreCase)) {
                $Key = $resolved.Substring($RepoRoot.Length).TrimStart('\', '/').Replace('\', '/')
            }
            else {
                $Key = [IO.Path]::GetFileName($resolved)
            }
        }
        Invoke-Wrangler @(
            'r2', 'object', 'put', ($Bucket + '/' + $Key),
            '--file', $resolved,
            '--remote'
        )
    }
    'list' {
        Invoke-Wrangler @('r2', 'bucket', 'list')
        Invoke-Wrangler @('r2', 'object', 'list', $Bucket, '--remote')
    }
    'url' {
        Invoke-Wrangler @('r2', 'bucket', 'dev-url', 'get', $Bucket)
    }
}
