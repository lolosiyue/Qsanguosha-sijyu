#!/usr/bin/env bash

set -euo pipefail

# Fetch runtime Lua content from the extensions repository (single source of truth):
#   <repo>/ai/*.lua         -> <root>/lua/ai/
#   <repo>/extensions/*.lua -> <root>/extensions/
#   <repo>/lua/*.lua        -> <root>/lua/
#
# Usage:
#   tools/ci/fetch-extensions.sh [repository-root]

root=${1:-${GITHUB_WORKSPACE:-}}
repo=${QSAN_EXTENSIONS_REPO:-https://github.com/lolosiyue/extensions.git}
ref=${QSAN_EXTENSIONS_REF:-main}

if [[ -z "$root" || ! -d "$root" ]]; then
    echo "Repository root does not exist: '$root'" >&2
    exit 1
fi

temp_base=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
clone_dir=$(mktemp -d "$temp_base/sgs-extensions-fetch.XXXXXX")
cleanup()
{
    rm -rf -- "$clone_dir"
}
trap cleanup EXIT

git clone --depth 1 --filter=blob:none --sparse --branch "$ref" "$repo" "$clone_dir"
git -C "$clone_dir" sparse-checkout set ai extensions lua
fetched_commit=$(git -C "$clone_dir" rev-parse HEAD)

ai_target="$root/lua/ai"
extensions_target="$root/extensions"
lua_target="$root/lua"
mkdir -p "$ai_target" "$extensions_target" "$lua_target"

find "$clone_dir/ai" -maxdepth 1 -type f -name '*.lua' -exec cp -f -- {} "$ai_target/" \;
mkdir -p "$ai_target/isolated"
find "$clone_dir/ai/isolated" -maxdepth 1 -type f -name '*.lua' -exec cp -f -- {} "$ai_target/isolated/" \;
find "$clone_dir/extensions" -maxdepth 1 -type f -name '*.lua' -exec cp -f -- {} "$extensions_target/" \;
find "$clone_dir/lua" -maxdepth 1 -type f -name '*.lua' -exec cp -f -- {} "$lua_target/" \;

# Workaround (upstream bug in lolosiyue/extensions): the AI load loop uses the
# lowercased package name as filename ("lua/ai/"..sl), but the files on disk are
# mixed-case (e.g. NyarzFirst-ai.lua). On case-sensitive filesystems dofile()
# fails for every mixed-case package. Patch it to use the real filename instead.
sed -i 's/"lua\/ai\/"\.\.sl/"lua\/ai\/"\.\.ai_file/g' "$ai_target/smart-ai.lua"
if ! grep -q '"lua/ai/"\.\.ai_file' "$ai_target/smart-ai.lua"; then
    echo 'lua/ai/smart-ai.lua patch failed: lowercase AI filename loop not fixed' >&2
    exit 1
fi

if [[ ! -f "$ai_target/smart-ai.lua" ]]; then
    echo 'lua/ai is incomplete: smart-ai.lua is missing after fetch' >&2
    exit 1
fi
if [[ ! -f "$lua_target/luaoldenemy_lib.lua" ]]; then
    echo 'lua is incomplete: luaoldenemy_lib.lua is missing after fetch' >&2
    exit 1
fi
if [[ ! -f "$ai_target/isolated/ask-for-use-card.lua" ]]; then
    echo 'lua/ai/isolated is incomplete: ask-for-use-card.lua is missing after fetch' >&2
    exit 1
fi

ai_count=$(find "$ai_target" -maxdepth 1 -type f -name '*.lua' | wc -l)
extensions_count=$(find "$extensions_target" -maxdepth 1 -type f -name '*.lua' | wc -l)
lua_count=$(find "$lua_target" -maxdepth 1 -type f -name '*.lua' | wc -l)
if (( ai_count == 0 || extensions_count == 0 || lua_count == 0 )); then
    echo 'Fetched runtime Lua content is empty' >&2
    exit 1
fi

echo "[fetch-extensions] ok: lua/ai=$ai_count, extensions=$extensions_count, lua=$lua_count (ref=$ref commit=$fetched_commit)"
