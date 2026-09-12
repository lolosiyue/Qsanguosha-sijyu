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

# QSAN_EXTENSIONS_REF may be a branch, tag or full commit id. `clone --branch`
# cannot take a commit id, so fetch the ref into an empty sparse repository.
git init --quiet "$clone_dir"
git -C "$clone_dir" remote add origin "$repo"
git -C "$clone_dir" sparse-checkout set ai extensions lua
git -C "$clone_dir" fetch --quiet --depth 1 --filter=blob:none origin "$ref"
git -C "$clone_dir" checkout --quiet --detach FETCH_HEAD
fetched_commit=$(git -C "$clone_dir" rev-parse HEAD)
if [[ -n "${GITHUB_ENV:-}" ]]; then
    echo "QSAN_EXTENSIONS_COMMIT=$fetched_commit" >> "$GITHUB_ENV"
fi

ai_target="$root/lua/ai"
extensions_target="$root/extensions"
lua_target="$root/lua"
mkdir -p "$ai_target" "$extensions_target" "$lua_target"

# Copy whole .lua trees, not just the top level: the repository keeps content in
# subdirectories (extensions/temp, ai/isolated, ai/temp) and a flat copy drops it.
# Losing extensions/temp is not a quiet degradation - the engine then fails to
# bootstrap with "extensions/gaoda.lua: attempt to concatenate a nil value".
copy_lua_tree()
{
    local source=$1 target=$2
    [[ -d "$source" ]] || return 0
    ( cd "$source" && find . -type f -name '*.lua' -exec cp -f --parents -- {} "$target/" \; )
}

copy_lua_tree "$clone_dir/ai" "$ai_target"
copy_lua_tree "$clone_dir/extensions" "$extensions_target"
copy_lua_tree "$clone_dir/lua" "$lua_target"

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
# Every .lua the upstream repository has must exist here: a silently thinner copy is
# what "attempt to concatenate a nil value" during engine bootstrap looks like.
missing=0
while IFS= read -r relative; do
    case $relative in
        ai/*) destination="$ai_target/${relative#ai/}" ;;
        extensions/*) destination="$root/$relative" ;;
        lua/*) destination="$root/$relative" ;;
        *) continue ;;
    esac
    if [[ ! -f "$destination" ]]; then
        echo "Missing after fetch: $relative" >&2
        missing=$((missing + 1))
    fi
done < <(cd "$clone_dir" && find ai extensions lua -type f -name '*.lua' 2>/dev/null)
if (( missing > 0 )); then
    echo "$missing file(s) from the extensions repository were not copied" >&2
    exit 1
fi

if [[ ! -f "$ai_target/isolated/ask-for-use-card.lua" ]]; then
    echo 'lua/ai/isolated is incomplete: ask-for-use-card.lua is missing after fetch' >&2
    exit 1
fi
if [[ ! -f "$ai_target/isolated-bootstrap.lua" ]]; then
    echo 'lua/ai is incomplete: isolated-bootstrap.lua is missing after fetch' >&2
    exit 1
fi
if [[ ! -f "$ai_target/isolated-facades.lua" ]]; then
    echo 'lua/ai is incomplete: isolated-facades.lua is missing after fetch' >&2
    exit 1
fi

ai_count=$(find "$ai_target" -type f -name '*.lua' | wc -l)
extensions_count=$(find "$extensions_target" -type f -name '*.lua' | wc -l)
lua_count=$(find "$lua_target" -maxdepth 1 -type f -name '*.lua' | wc -l)
if (( ai_count == 0 || extensions_count == 0 || lua_count == 0 )); then
    echo 'Fetched runtime Lua content is empty' >&2
    exit 1
fi

echo "[fetch-extensions] ok: lua/ai=$ai_count, extensions=$extensions_count, lua=$lua_count (ref=$ref commit=$fetched_commit)"
