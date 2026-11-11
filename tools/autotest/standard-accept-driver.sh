#!/usr/bin/env bash
# 原版 standard 25 武將 headless 驗收：可中斷續跑的驅動外殼。
#
# 為什麼需要外層驅動：general_acceptance.py 一次只跑一個武將，它自己的
# headless stage 沒有任何續跑能力 —— 中途被打斷（本機每天 05:00 Windows
# 會強制關機）就得整個武將重跑。所以把「武將」當成 checkpoint 單位：
# 每完成一個武將就把狀態寫進 state/，下次啟動只補未完成的。
#
# 用法：
#   tools/autotest/standard-accept-driver.sh            # 續跑未完的
#   tools/autotest/standard-accept-driver.sh --status   # 只看進度
#   tools/autotest/standard-accept-driver.sh --reset caocao
#
# 同一時間只允許一個實例（flock）。要中斷直接 kill 掉這個進程即可；
# state 是每武將一寫，最壞情況只損失當前這一個武將。

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${OUT_DIR:-$ROOT/artifacts/standard-accept-20261110}"
STATE="$OUT/state"
RAW="$OUT/raw"
LOCK="$STATE/driver.lock"
LOG="$OUT/driver.log"
EXE_ROOT="$ROOT"
JOBS="${JOBS:-2}"
BASE_SEED="${BASE_SEED:-20261110}"

# 21 個 strengthen 強化的原版人 + 4 個 addGenerals 非 nos 武將。順序固定，
# 這樣 seed 與輸出目錄在多次執行之間一致，續跑不會換到別的對局。
GENERALS=(
  caocao simayi xiahoudun zhangliao xuchu
  guojia liubei guanyu zhangfei zhaoyun
  machao huangyueying ganning lvmeng huanggai
  zhouyu daqiao luxun huatuo lvbu
  diaochan zhenji zhugeliang sunquan sunshangxiang
)

mkdir -p "$STATE" "$RAW"

log() { printf '[%s] %s\n' "$(date '+%F %T')" "$*" | tee -a "$LOG"; }

seed_for() {
  # 每個武將一個穩定 seed：BASE_SEED + 序號 * 10，方便重跑同一局。
  local idx="$1"
  echo $(( BASE_SEED + idx * 10 ))
}

layerB_exclude() {
  local f="$OUT/layerB-exclude.txt"
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

  mkdir -p "$dir"
  log "START $general seed=$seed out=$dir"
  local started; started=$(date +%s)

  QT_QPA_PLATFORM=offscreen python3 "$ROOT/tools/autotest/general_acceptance.py" \
      --general "$general" \
      --seed "$seed" \
      --stages headless \
      --exclude-modes "$(layerB_exclude)" \
      --jobs 1 \
      --headless-minutes 0 \
      --skip-sync-check \
      --out-dir "$dir" >>"$dir/acceptance.log" 2>&1
  local rc=$?
  local elapsed=$(( $(date +%s) - started ))

  # rc 不能當完成判據。實測：guanyu 撞上預設 60 分鐘預算，16 個模式只跑了 5 個、
  # 另外 11 個是 NOT RUN，general_acceptance.py 仍然 exit 0。只看 rc 會把「缺格」
  # 記成完成，之後永遠不會補跑，summary.tsv 會靜靜留下一整批空白而顯示 PASS。
  # 真正的判據是這一輪的 CSV：必須有 16 行，且每行 finished == expected。
  local coverage; coverage=$(python3 - "$dir" <<'PY'
import csv, glob, sys
rows = []
for path in glob.glob(sys.argv[1] + "/headless/summary-headless-*.csv"):
    rows = list(csv.DictReader(open(path, encoding="utf-8-sig")))
    break
if not rows:
    print("nocsv")
    raise SystemExit
bad = [r["mode"] for r in rows if not r.get("finished") or r["finished"] != r.get("expected")]
print("ok" if len(rows) == 16 and not bad else "incomplete:" + ",".join(bad))
PY
)

  # 只登記真正跑完的武將；沒跑完就不寫狀態檔，下次啟動會自動重跑這一將。
  if [[ "$coverage" != "ok" ]]; then
    log "INCOMPLETE $general rc=$rc coverage=$coverage ${elapsed}s (重跑)"
    write_progress
    return 1
  fi

  printf '%s\trc=%s\tseed=%s\tseconds=%s\t%s\n' \
      "$general" "$rc" "$seed" "$elapsed" "$(date '+%F %T')" > "$tag.status"
  log "DONE $general rc=$rc ${elapsed}s"
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
    echo "# 原版 standard 25 武將驗收 — 進度"
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
    echo "續跑：\`tools/autotest/standard-accept-driver.sh\`（冪等，已完成的會跳過）。"
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
