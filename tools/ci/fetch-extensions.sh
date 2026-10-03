#!/usr/bin/env bash

set -euo pipefail

# Fetch runtime Lua content from the extensions repository, the single source of truth.

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

# QSAN_EXTENSIONS_REF may be a branch, tag, or commit; commit IDs require fetching into a sparse repository.
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

# Copy lua/ai/ recursively so isolated and temporary subdirectories are retained.
copy_lua_tree()
{
    local source=$1 target=$2
    [[ -d "$source" ]] || return 0
    ( cd "$source" && find . -type f -name '*.lua' -exec cp -f --parents -- {} "$target/" \; )
}

# Keep extensions/ and lua/ flat: nested Lua files are undeclared and rejected by contentScanIsDeclared().
copy_lua_top_level()
{
    local source=$1 target=$2
    [[ -d "$source" ]] || return 0
    find "$source" -maxdepth 1 -type f -name '*.lua' -exec cp -f -- {} "$target/" \;
}

copy_lua_tree "$clone_dir/ai" "$ai_target"
copy_lua_top_level "$clone_dir/extensions" "$extensions_target"
copy_lua_top_level "$clone_dir/lua" "$lua_target"

# Rewrite AI loader paths to preserve the actual upstream filename case on case-sensitive filesystems.
sed -i 's/"lua\/ai\/"\.\.sl/"lua\/ai\/"\.\.ai_file/g' "$ai_target/smart-ai.lua"
if ! grep -q '"lua/ai/"\.\.ai_file' "$ai_target/smart-ai.lua"; then
    echo 'lua/ai/smart-ai.lua patch failed: lowercase AI filename loop not fixed' >&2
    exit 1
fi

# Repair malformed declarations in the pinned upstream content before packaging.
python3 - "$extensions_target" << 'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
sgs10th = root / "sgs10th.lua"
if sgs10th.is_file():
    text = sgs10th.read_text(encoding="utf-8")
    old = "sgs.QVariant( .. n)"
    new = 'sgs.QVariant("draw:" .. n)'
    count = text.count(old)
    if count == 1:
        sgs10th.write_text(text.replace(old, new, 1), encoding="utf-8")
    elif count != 0:
        raise SystemExit(f"expected one {old!r} in {sgs10th}, found {count}")
offline = root / "sijyuoffline.lua"
if offline.is_file():
    text = offline.read_text(encoding="utf-8")
    needle = "sfofl_analepticchan:addSkill(sfofl_meiniang)"
    definition = 'sfofl_analepticchan = sgs.General(extension_s, "sfofl_analepticchan", "qun", 4, false)\n'
    if needle in text and "sfofl_analepticchan = sgs.General(" not in text:
        offline.write_text(text.replace(needle, definition + needle, 1), encoding="utf-8")
PY

if [[ ! -f "$ai_target/smart-ai.lua" ]]; then
    echo 'lua/ai is incomplete: smart-ai.lua is missing after fetch' >&2
    exit 1
fi
if [[ ! -f "$lua_target/luaoldenemy_lib.lua" ]]; then
    echo 'lua is incomplete: luaoldenemy_lib.lua is missing after fetch' >&2
    exit 1
fi
# Verify every upstream Lua file is present, except undeclared nested content intentionally left out.
# patterns have to come first.
missing=0
while IFS= read -r relative; do
    case $relative in
        ai/*) destination="$ai_target/${relative#ai/}" ;;
        extensions/*/*|lua/*/*) continue ;;
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

# The isolated AI runtime: bootstrap, facades, then the dispatchers that
# lua/ai/isolated-bootstrap.lua declares in ai_isolated_core. Loading aborts
# AiLuaRuntime::initialize() on the first core script it cannot read, and
# AiRouteRegistry::routeFor() defaults to AiRouteIsolated, so an upstream rename of
# any one of these leaves every Room without an AI. Fail here rather than at Room
# creation. Per-package handlers (isolated/<package>-ai.lua) are deliberately not
# checked: a package without one simply has no isolated AI yet.
# docs/ai-runtime-manifest.json holds the full list.
for relative in mode-ai.lua isolated-bootstrap.lua isolated-facades.lua \
        isolated/ask-for-use-card.lua isolated/ask-for-choice.lua \
        isolated/decision-core.lua isolated/retrial.lua \
        isolated/strategy-hooks.lua isolated/event-intention.lua; do
    if [[ ! -f "$ai_target/$relative" ]]; then
        echo "lua/ai is incomplete: $relative is missing after fetch" >&2
        exit 1
    fi
done

ai_count=$(find "$ai_target" -type f -name '*.lua' | wc -l)
extensions_count=$(find "$extensions_target" -maxdepth 1 -type f -name '*.lua' | wc -l)
lua_count=$(find "$lua_target" -maxdepth 1 -type f -name '*.lua' | wc -l)
if (( ai_count == 0 || extensions_count == 0 || lua_count == 0 )); then
    echo 'Fetched runtime Lua content is empty' >&2
    exit 1
fi

echo "[fetch-extensions] ok: lua/ai=$ai_count, extensions=$extensions_count, lua=$lua_count (ref=$ref commit=$fetched_commit)"
