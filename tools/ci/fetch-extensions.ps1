# Fetches runtime Lua content from the extensions repository (single source of truth):
#   <repo>/ai/            -> <root>/lua/ai/     (smart-ai.lua + all package AI scripts)
#   <repo>/extensions/    -> <root>/extensions/ (Lua extension packages)
#   <repo>/lua/           -> <root>/lua/        (shared libs, e.g. luaoldenemy_lib.lua)
#
# Why not git submodule: the extensions repo root also contains src/, doc/, etc.
# and only its ai/, extensions/ and lua/ subfolders are needed at runtime.
#
# Usage (CI):
#   & .\tools\ci\fetch-extensions.ps1
# Usage (local verification):
#   & .\tools\ci\fetch-extensions.ps1 -Root C:\tmp\sgs-verify
param(
    [string]$Root = $env:GITHUB_WORKSPACE,
    [string]$Repo = "https://github.com/lolosiyue/extensions.git",
    [string]$Ref = "main"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path -LiteralPath $Root)) {
    throw "Root directory does not exist: '$Root' (pass -Root or run on a CI workspace)"
}

$aiTarget = Join-Path $Root "lua\ai"
$extTarget = Join-Path $Root "extensions"
$luaTarget = Join-Path $Root "lua"
$tempBase = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$cloneDir = Join-Path $tempBase "sgs-extensions-fetch"

# Sparse clone: fetch only the ai/, extensions/ and lua/ subfolders (partial clone, ~9 MB)
if (Test-Path -LiteralPath $cloneDir) {
    Remove-Item -LiteralPath $cloneDir -Recurse -Force
}
git clone --depth 1 --filter=blob:none --sparse --branch $Ref $Repo $cloneDir
if ($LASTEXITCODE -ne 0) {
    throw "git clone failed (exit=$LASTEXITCODE, repo=$Repo ref=$Ref)"
}
git -C $cloneDir sparse-checkout set ai extensions lua
if ($LASTEXITCODE -ne 0) {
    throw "sparse-checkout failed (exit=$LASTEXITCODE)"
}

# <repo>/ai/*.lua -> <root>/lua/ai/ + isolated
New-Item -ItemType Directory -Path $aiTarget -Force | Out-Null
Copy-Item -Path (Join-Path $cloneDir "ai\*.lua") -Destination $aiTarget -Force
New-Item -ItemType Directory -Path (Join-Path $aiTarget "isolated") -Force | Out-Null
Copy-Item -Path (Join-Path $cloneDir "ai\isolated\*.lua") -Destination (Join-Path $aiTarget "isolated") -Force -ErrorAction SilentlyContinue
if (-not (Test-Path -LiteralPath (Join-Path $aiTarget "smart-ai.lua"))) {
    throw "lua/ai is incomplete: smart-ai.lua missing after fetch"
}
# Case-sensitive 修正：上游 extensions 倉庫檔名大小寫混合，Linux 下 pcall(dofile,"lua/ai/"..sl) 會失配
# 對齊 fetch-extensions.sh 的 sed 邏輯： "lua/ai/"..sl -> "lua/ai/"..ai_file (保留原始檔案大小寫)
$smartAiPath = Join-Path $aiTarget "smart-ai.lua"
$smartAiContent = Get-Content -LiteralPath $smartAiPath -Raw
if ($smartAiContent.Contains('"lua/ai/"..sl')) {
    $smartAiContent = $smartAiContent.Replace('"lua/ai/"..sl', '"lua/ai/"..ai_file')
    Set-Content -LiteralPath $smartAiPath -Value $smartAiContent -NoNewline -Encoding UTF8
}
if (-not ((Get-Content -LiteralPath $smartAiPath -Raw).Contains('"lua/ai/"..ai_file'))) {
    throw "lua/ai/smart-ai.lua patch failed: lowercase AI filename loop not fixed"
}
if (-not (Test-Path -LiteralPath (Join-Path $aiTarget "isolated\ask-for-use-card.lua"))) {
    throw "lua/ai/isolated is incomplete: ask-for-use-card.lua missing after fetch"
}

# <repo>/extensions/*.lua -> <root>/extensions/ (skip temp/ and non-lua files)
New-Item -ItemType Directory -Path $extTarget -Force | Out-Null
Copy-Item -Path (Join-Path $cloneDir "extensions\*.lua") -Destination $extTarget -Force

# <repo>/lua/*.lua -> <root>/lua/ (shared libs such as luaoldenemy_lib.lua; main-repo files are kept)
New-Item -ItemType Directory -Path $luaTarget -Force | Out-Null
Copy-Item -Path (Join-Path $cloneDir "lua\*.lua") -Destination $luaTarget -Force
if (-not (Test-Path -LiteralPath (Join-Path $luaTarget "luaoldenemy_lib.lua"))) {
    throw "lua is incomplete: luaoldenemy_lib.lua missing after fetch"
}

Remove-Item -LiteralPath $cloneDir -Recurse -Force

$aiCount = (Get-ChildItem -Path $aiTarget -Filter *.lua -File).Count
$extCount = (Get-ChildItem -Path $extTarget -Filter *.lua -File).Count
$luaCount = (Get-ChildItem -Path $luaTarget -Filter *.lua -File).Count
if ($aiCount -eq 0) {
    throw "lua/ai has no .lua files after fetch"
}
if ($extCount -eq 0) {
    throw "extensions has no .lua files after fetch"
}
if ($luaCount -eq 0) {
    throw "lua has no .lua files after fetch"
}
Write-Output ("[fetch-extensions] ok: lua/ai={0} files, extensions={1} files, lua={2} files (ref={3})" -f $aiCount, $extCount, $luaCount, $Ref)
