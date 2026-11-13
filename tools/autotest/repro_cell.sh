#!/usr/bin/env bash
# Re-run one headless acceptance cell with core dumps enabled, then pull a backtrace.
#
# usage: bash tools/autotest/repro_cell.sh <general> <mode> <seed> [--date 20261110]
#
# Core files on this machine (checked 2026-11-13):
#   /proc/sys/kernel/core_pattern  = core   -> relative, written in the crashing process's cwd
#   /proc/sys/kernel/core_uses_pid = 1      -> the file is named core.<pid>
#   soft `ulimit -c` is 0 by default (hard limit unlimited), so it is raised inside the scope.
# headless_runner.py starts QSanguosha with cwd = runner_common.resolve_workdir(exe_root):
# release/ or debug/ when that directory holds config.ini + font/ + image/, else the repo
# root. Today that is the repo root, not the directory beside the exe. The game installs no
# signal handler on Linux (crashhandler.cpp is Windows SEH only), so SIGSEGV/SIGABRT dump core.
#
# Output: .maint/repro-<general>-<mode>-<timestamp>/ with runner.out, the runner's log tree,
# the core (moved here, single copy) and gdb.out (bt 30 + info registers).
set -u

if [ $# -lt 3 ]; then
    echo "usage: bash tools/autotest/repro_cell.sh <general> <mode> <seed> [--date 20261110]" >&2
    exit 2
fi
general=$1 mode=$2 seed=$3
shift 3
date_tag=""
while [ $# -gt 0 ]; do
    case "$1" in
        --date) date_tag=${2:-}; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root" || exit 2
exe="$root/relwithdebinfo/QSanguosha"
stamp=$(date +%Y%m%d-%H%M%S)
out="$root/.maint/repro-$general-$mode-$stamp"
unit="repro-$general-$mode-$stamp"
mkdir -p "$out"

pattern=$(cat /proc/sys/kernel/core_pattern)
workdir=$(python3 -B -c "import sys; sys.path.insert(0, 'tools/autotest'); \
from runner_common import resolve_workdir; import os; print(os.path.abspath(resolve_workdir('.')))")
{
    echo "general=$general mode=$mode seed=$seed date=${date_tag:-none}"
    echo "exe=$exe sha256=$(sha256sum "$exe" | cut -c1-64)"
    echo "core_pattern=$pattern core_uses_pid=$(cat /proc/sys/kernel/core_uses_pid) game_cwd=$workdir"
} > "$out/run-info.txt"
marker="$out/.started"
touch "$marker"

# systemd-run --scope runs the command in this process tree, so the raised core limit and
# the environment reach headless_runner.py and the QSanguosha child it spawns.
systemd-run --user --scope --unit="$unit" bash -c '
    ulimit -c unlimited
    echo "ulimit -c inside scope: $(ulimit -c)"
    cd "$1" && QT_QPA_PLATFORM=offscreen python3 -B tools/autotest/headless_runner.py \
        --exe relwithdebinfo/QSanguosha --exe-root . --seed "$2" --modes "$3" --games 1 \
        --force-general "$4" --log-dir "$5/headless"
    echo "runner_exit=$?"
' repro "$root" "$seed" "$mode" "$general" "$out" > "$out/runner.out" 2>&1
scope_exit=$?

summary=$(ls -t "$out"/headless/summary-headless-*.csv 2>/dev/null | head -1)
game_exit=$(python3 -B -c "import csv,sys; rows=list(csv.DictReader(open(sys.argv[1], encoding='utf-8-sig'))); \
print(rows[0]['exit'] if rows else '?')" "$summary" 2>/dev/null || echo "?")
game_log=$(ls "$out"/headless/headless/*/"$mode".log 2>/dev/null | head -1)

core=""
case "$pattern" in
    \|*) echo "core_pattern pipes to a handler ($pattern); collect with coredumpctl instead" >> "$out/run-info.txt" ;;
    *)
        core_dir=$(dirname "$pattern")
        [ "$core_dir" = "." ] && core_dir=""
        for dir in ${core_dir:+"$core_dir"} "$workdir" "$root" "$HOME"; do
            found=$(find "$dir" -maxdepth 1 -type f -name 'core*' -newer "$marker" 2>/dev/null | head -1)
            if [ -n "$found" ]; then
                mv "$found" "$out/" && core="$out/$(basename "$found")"
                break
            fi
        done ;;
esac

gdb_out=""
if [ -n "$core" ]; then
    gdb_bin=$(command -v gdb || true)
    gdb_env=()
    if [ -z "$gdb_bin" ] && [ -x "$root/.maint/gdbx/usr/bin/gdb" ]; then
        gdb_bin="$root/.maint/gdbx/usr/bin/gdb"
        gdb_env=(env "LD_LIBRARY_PATH=$root/.maint/gdbx/usr/lib/x86_64-linux-gnu:$root/.maint/gdbx/usr/lib")
    fi
    if [ -n "$gdb_bin" ]; then
        gdb_out="$out/gdb.out"
        "${gdb_env[@]}" "$gdb_bin" -batch -ex 'bt 30' -ex 'info registers' "$exe" "$core" > "$gdb_out" 2>&1
    else
        echo "no gdb found; core kept at $core" >> "$out/run-info.txt"
    fi
fi

echo "SUMMARY game_exit=$game_exit scope_exit=$scope_exit core=${core:-none} log=${game_log:-none} gdb=${gdb_out:-none}"
