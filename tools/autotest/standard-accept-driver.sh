#!/usr/bin/env bash
# 武將 headless 驗收：可中斷續跑的驅動外殼。PACK 決定跑哪個包。
#
# 為什麼需要外層驅動：general_acceptance.py 一次只跑一個武將，它自己的
# headless stage 沒有任何續跑能力 —— 中途被打斷（本機每天 05:00 Windows
# 會強制關機）就得整個武將重跑。所以把「武將」當成 checkpoint 單位：
# 每完成一個武將就把狀態寫進 state/，下次啟動只補未完成的。
#
# 用法：
#   tools/autotest/standard-accept-driver.sh            # 續跑未完的（預設 PACK=standard）
#   PACK=mountain tools/autotest/standard-accept-driver.sh  # 依引擎的包名列出武將
#   tools/autotest/standard-accept-driver.sh --status   # 只看進度
#   tools/autotest/standard-accept-driver.sh --reset caocao
#
# PACK 會決定輸出目錄：artifacts/<PACK>-accept-<date>/。
# 新的包需要建置 qsanguosha_skill_scenario，以 --list-generals 查清單。
#
# 同一時間只允許一個實例（flock）。要中斷直接 kill 掉這個進程即可；
# state 是每武將一寫，最壞情況只損失當前這一個武將。

set -uo pipefail

# 崩潰要留 core。沒有 core 就只能靠日誌猜崩點 —— 2026-11-18 的
# xunyu/03_1v2 與 dianwei/10_shijia 兩格都因此沒有證據可看。
# 子進程繼承這個上限，包括 general_acceptance.py 與它 spawn 的 QSanguosha；
# runner_common.ensure_core_dumps() 在 Python 那層再兜一次。
# kernel 的 core_pattern 是相對路徑 `core`，所以 core 會落在對局的 cwd
# （resolve_workdir 指到倉庫根），.gitignore 已擋住它。
ulimit -c unlimited 2>/dev/null || true

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PACK="${PACK:-standard}"
OUT="${OUT_DIR:-$ROOT/artifacts/${PACK}-accept-20261110}"
STATE="$OUT/state"
RAW="$OUT/raw"
LOCK="$STATE/driver.lock"
LOG="$OUT/driver.log"
EXE_ROOT="$ROOT"
JOBS="${JOBS:-2}"
# BASE_SEED 覆蓋包的 seed 起點，供重跑既有對局使用。
BASE_SEED="${BASE_SEED:-}"

# 21 個 strengthen 強化的原版人 + 4 個 addGenerals 非 nos 武將。順序固定，
# 這樣 seed 與輸出目錄在多次執行之間一致，續跑不會換到別的對局。
STANDARD_GENERALS=(
  caocao simayi xiahoudun zhangliao xuchu
  guojia liubei guanyu zhangfei zhaoyun
  machao huangyueying ganning lvmeng huanggai
  zhouyu daqiao luxun huatuo lvbu
  diaochan zhenji zhugeliang sunquan sunshangxiang
)

# 風包 WindPackage（src/package/wind.cpp:2569）。這些武將只存在於原始包，
# 沒有 strengthen 版，所以矩陣跑的是原版技能本身。
WIND_GENERALS=(
  xiahouyuan caoren huangzhong weiyan xiaoqiao
  zhoutai zhangjiao yuji shenguanyu shenlvmeng
)

# 火包 FirePackage（src/package/fire.cpp:1888）。
FIRE_GENERALS=(
  dianwei xunyu pangtong wolong taishici
  yuanshao yanliangwenchou pangde shenzhouyu shenzhugeliang
)

# 林包 ThicketPackage（src/package/thicket.cpp:1681）。源碼裡 caopi 與 dongzhuo
# 寫成 "caopi$"、"dongzhuo$"，那個 $ 只是主公標記（src/core/general.cpp:19-20
# 的 lord = copy.contains("$") 之後會 remove），實際名字沒有 $。
THICKET_GENERALS=(
  xuhuang caopi menghuo zhurong sunjian
  lusu dongzhuo jiaxu shencaocao shenlvbu
)

# 這四個包已留下驗收紀錄。沿用原清單與起點，保住每位武將的 seed。
case "$PACK" in
  standard) GENERALS=("${STANDARD_GENERALS[@]}"); PACK_SEED_BASE="${PACK_SEED_BASE:-20261110}" ;;
  wind)     GENERALS=("${WIND_GENERALS[@]}");     PACK_SEED_BASE="${PACK_SEED_BASE:-20261210}" ;;
  fire)     GENERALS=("${FIRE_GENERALS[@]}");     PACK_SEED_BASE="${PACK_SEED_BASE:-20261310}" ;;
  thicket)  GENERALS=("${THICKET_GENERALS[@]}");  PACK_SEED_BASE="${PACK_SEED_BASE:-20261410}" ;;
  *)
    roster="$(python3 - "$ROOT" "$PACK" <<'PY'
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

root, package = sys.argv[1:]
sys.path.insert(0, str(Path(root) / "tools" / "autotest"))
from runner_common import find_exe, resolve_workdir

try:
    host = find_exe(root, "qsanguosha_skill_scenario")
    with TemporaryDirectory(prefix="qs-package-generals-") as directory:
        report = Path(directory) / "generals.json"
        subprocess.run(
            [host, "--list-generals", "--package", package, "--report", str(report)],
            cwd=resolve_workdir(root), stdout=sys.stderr, check=True, timeout=120,
        )
        names = json.loads(report.read_text(encoding="utf-8"))
    if not isinstance(names, list) or not names:
        raise ValueError("package did not return a nonempty list of generals")
    if any(not isinstance(name, str) or not name or name in (".", "..")
           or any(char in name for char in "/\\\r\n\0") for name in names):
        raise ValueError("package returned an invalid general name")
    if len(names) != len(set(names)):
        raise ValueError("package returned duplicate general names")
except (OSError, ValueError, subprocess.SubprocessError) as error:
    print(f"Cannot list generals for PACK={package}: {error}", file=sys.stderr)
    sys.exit(2)
print("\n".join(names))
PY
)" || exit $?
    mapfile -t GENERALS <<< "$roster"
    checksum="$(printf '%s' "$PACK" | cksum)" || exit $?
    PACK_SEED_BASE="${PACK_SEED_BASE:-$((100000000 + ${checksum%% *} % 1000000000))}"
    ;;
esac

# 風火包不套用 standard 那輪的 layerB-exclude.txt：那份清單是為 strengthen
# 武將的層 B 排的，跟這兩個包無關。要排除模式就自己給 LAYERB_EXCLUDE。
if [[ "$PACK" != "standard" ]]; then
  LAYERB_EXCLUDE="${LAYERB_EXCLUDE:-artifacts/${PACK}-accept-20261110/layerB-exclude.txt}"
fi

mkdir -p "$STATE" "$RAW"

log() { printf '[%s] %s\n' "$(date '+%F %T')" "$*" | tee -a "$LOG"; }

seed_for() {
  # 每個武將一個穩定 seed：包起點 + 序號 * 10，方便重跑同一局。
  # 給 BASE_SEED 時它覆蓋包起點（要重跑出跟某份舊報告完全一樣的對局時用）。
  local idx="$1"
  local base="${BASE_SEED:-$PACK_SEED_BASE}"
  echo $(( base + idx * 10 ))
}

layerB_exclude() {
  local f="${LAYERB_EXCLUDE:-$OUT/layerB-exclude.txt}"
  if [[ -f "$f" ]]; then tr -d '\n' < "$f"; else echo ""; fi
}

run_one() {
  local general="$1" idx="$2"
  local seed; seed="$(seed_for "$idx")"
  local dir="$RAW/$general"
  local tag="$STATE/$general"

  if [[ -f "$tag.status" ]]; then
    log "SKIP $general (already recorded: $(cat "$tag.status"))"
    return 0
  fi

  # 已知缺陷格還在修：跳過整個武將，避免每次開機都重跑一遍、卡在同一格崩潰
  # 浪費一兩個小時。這裡刻意不寫 status 檔，所以修好後把名字移出清單就會重跑。
  if [[ " ${SKIP_GENERALS:-} " == *" $general "* ]]; then
    log "SKIP $general (in SKIP_GENERALS)"
    return 0
  fi

  mkdir -p "$dir"
  # 上一輪留下的 modes.txt 不能拿來判這一輪：general_acceptance.py 若在寫出新表之前
  # 就失敗，coverage 會拿舊矩陣判成 ok。先刪掉，失敗時 coverage 會報 nomodes。
  rm -f "$dir/headless/modes.txt"
  log "START $general seed=$seed out=$dir"
  local started; started=$(date +%s)

  QT_QPA_PLATFORM=offscreen python3 "$ROOT/tools/autotest/general_acceptance.py" \
      --general "$general" \
      --seed "$seed" \
      --stages headless \
      --exclude-modes "$(layerB_exclude)" \
      --resume \
      --jobs 1 \
      --headless-minutes 0 \
      --skip-sync-check \
      --out-dir "$dir" >>"$dir/acceptance.log" 2>&1
  local rc=$?
  local elapsed=$(( $(date +%s) - started ))

  # rc 不能當完成判據。實測：guanyu 撞上預設 60 分鐘預算，16 個模式只跑了 5 個、
  # 另外 11 個是 NOT RUN，general_acceptance.py 仍然 exit 0。只看 rc 會把「缺格」
  # 記成完成，之後永遠不會補跑，summary.tsv 會靜靜留下一整批空白而顯示 PASS。
  #
  # 真正的判據是逐格的：本輪矩陣（general_acceptance.py 寫的 headless/modes.txt）
  # 裡每一格，最新一次嘗試都已收局。規則在 acceptance_cells.py，跟
  # general_acceptance.py --resume 共用同一份，兩邊不會再各算各的。
  # 以前這裡只讀「最新一份 CSV」並要求剛好 16 行：16 是 standard 包的模式數，
  # wind 包 48 格永遠判 INCOMPLETE；續跑只跑剩餘格，最新 CSV 也永遠不會是整份。
  # 輸出第一個字是 ok / nomodes / nocsv / incomplete:...，後面是給人看的細節。
  local coverage; coverage=$(python3 "$ROOT/tools/autotest/acceptance_cells.py" coverage "$dir/headless")

  # 只登記真正跑完的武將；沒跑完就不寫狀態檔，下次啟動會自動重跑這一將。
  if [[ "${coverage%% *}" != "ok" ]]; then
    log "INCOMPLETE $general rc=$rc coverage=$coverage ${elapsed}s (重跑)"
    write_progress
    return 1
  fi

  printf '%s\trc=%s\tseed=%s\tseconds=%s\t%s\n' \
      "$general" "$rc" "$seed" "$elapsed" "$(date '+%F %T')" > "$tag.status"
  log "DONE $general rc=$rc ${elapsed}s coverage=$coverage"
  write_progress
  return 0
}

# PROGRESS.md 給人看；state/*.status 給機器讀。兩者在每次 driver 啟動、
# 每個武將完成、以及 --status 時都會重寫，所以關機後重開也接得上。
# 進度一律由 state/ 重新算，不依賴呼叫者傳入的數字。
write_progress() {
  local f="$OUT/PROGRESS.md"
  local done=0 failed=0 pending=0
  local g tag line
  for g in "${GENERALS[@]}"; do
    tag="$STATE/$g.status"
    if [[ ! -f "$tag" ]]; then pending=$((pending+1)); continue; fi
    line="$(cat "$tag")"
    if [[ "$(cut -f2 <<<"$line")" == "rc=0" ]]; then done=$((done+1)); else failed=$((failed+1)); fi
  done
  {
    echo "# ${PACK} 包武將 headless 驗收 — 進度"
    echo
    echo "更新時間：$(date '+%F %T %Z')"
    echo
    echo "完成 **$done** / 未完成 **$pending** / 非零退出 **$failed**（共 ${#GENERALS[@]}）"
    echo
    echo "| 武將 | 狀態 | seed | 耗時 | 完成時間 |"
    echo "|---|---|---|---|---|"
    for g in "${GENERALS[@]}"; do
      tag="$STATE/$g.status"
      if [[ -f "$tag" ]]; then
        line="$(cat "$tag")"
        echo "| $g | $(cut -f2 <<<"$line") | $(cut -f3 <<<"$line") | $(cut -f4 <<<"$line") | $(cut -f5 <<<"$line") |"
      else
        echo "| $g | pending | | | |"
      fi
    done
    echo
    echo "狀態檔：\`$STATE/<general>.status\`；原始 log：\`$RAW/<general>/\`。"
    echo "續跑：\`PACK=$PACK tools/autotest/standard-accept-driver.sh\`（冪等，已完成的會跳過）。"
  } > "$f"
}

status() {
  printf '%-16s %-6s %-18s %s\n' GENERAL STATUS SEED SECONDS
  local g tag line
  for g in "${GENERALS[@]}"; do
    tag="$STATE/$g.status"
    if [[ -f "$tag" ]]; then
      line="$(cat "$tag")"
      printf '%-16s %-6s %-18s %s\n' "$g" "$(cut -f2 <<<"$line")" "$(cut -f3 <<<"$line")" "$(cut -f4 <<<"$line")"
    else
      printf '%-16s %-6s\n' "$g" "pending"
    fi
  done
  log "SUMMARY $(count_states)"
  write_progress
}

count_states() {
  local done=0 failed=0 pending=0 g tag line
  for g in "${GENERALS[@]}"; do
    tag="$STATE/$g.status"
    if [[ ! -f "$tag" ]]; then pending=$((pending+1)); continue; fi
    line="$(cat "$tag")"
    if [[ "$(cut -f2 <<<"$line")" == "rc=0" ]]; then done=$((done+1)); else failed=$((failed+1)); fi
  done
  echo "done=$done nonzero=$failed pending=$pending of ${#GENERALS[@]}"
}

if [[ "${1:-}" == "--status" ]]; then
  status
  exit 0
fi

if [[ "${1:-}" == "--reset" ]]; then
  g="${2:?usage: --reset <general>}"
  rm -f "$STATE/$g.status"
  log "RESET $g"
  exit 0
fi

exec 9>"$LOCK"
if ! flock -n 9; then
  log "already running (lock held by pid $(cat "$LOCK.pid" 2>/dev/null || echo '?')); exit"
  exit 0
fi
echo $$ > "$LOCK.pid"

trap 'log "INTERRUPTED"; exit 130' INT TERM

log "driver start: ${#GENERALS[@]} generals, jobs=$JOBS, out=$OUT"
write_progress

# 外層並行 JOBS 個武將，內部 --jobs 1：總併發 = JOBS，配合 12 核 15GB 的機器。
running=0
for i in "${!GENERALS[@]}"; do
  run_one "${GENERALS[$i]}" "$i" &
  running=$((running + 1))
  if (( running >= JOBS )); then
    wait -n
    running=$((running - 1))
  fi
done
wait

status
log "driver finished"
