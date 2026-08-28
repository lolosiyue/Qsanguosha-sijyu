# 自動化測試工具 (tools/autotest)

GUI 解耦後的現代化測試 runner, 取代舊的 `L:\QsgsFinal\autotest.py`
(螢幕截圖 + batch 檔流程, 已 obsolete, 不再維護)。

## 前置

- 先以 `tools/build-cmake.ps1 -Configuration Release` 編譯 (需含 `qsanguosha_server`)
- 執行時 cwd 需含 `config.ini` (runner 會自動選 `release\` 或 `debug\`)

## headless_runner.py — 純 AI 壓力/回歸測試 (可平行)

每模式一個 process, 內部連續跑 N 局 (全 TrustAI), 以 exit code + log 標記判定:

```powershell
python tools\autotest\headless_runner.py `
    --exe L:\finaldebug\QSanguosha-v2\release\QSanguosha.exe `
    --seed 20260828 `
    --exe-root L:\finaldebug\QSanguosha-v2 `
    --modes 08p `
    --games 5 --parallel 2
```

`--exe`／`--seed` 為必填 (runner 契約無隱式執行檔發現／隱式種子, 見
`docs/lua-ext-spec.md`); seed 須為 unsigned 32-bit, 慣例用當日日期
`yyyyMMdd` (如 20260828, 建議用 `run_headless.bat` 自動生成並隨日期前進)。
`--parallel` 為同時執行的 **process 總數**：模式數 ≥ parallel 時每個模式
一個 process；模式數不足時同一模式開多份 (round-robin)，每份獨立 log
(`<mode>-N.log` / `<mode>-N-headless.log`)。例: `--modes 08p --parallel 10`
= 10 個 08p process 同時跑。注意多 process 同時打 08p 對 CPU/RAM 負載高。

輸出: `tools\autotest\autotest-logs\headless\<mode>[-N].log` + `summary-headless-<時間>.csv`

## network_runner.py — 真實網路測試 (串行)

`qsanguosha_server` 常駐 + 每局重啟 GUI client; client 自動選將、
自動填 AI 開局、自動托管, **零截圖、零座標**:

```powershell
python tools\autotest\network_runner.py `
    --exe-root L:\finaldebug\QSanguosha-v2 `
    --modes 10p,20p,05p `
    --runs 2 --general heg_zhanglu

# 1v1 KOF 用佔位選將:
python tools\autotest\network_runner.py `
    --modes 02_1v1 --runs 2 --general x0

# 雙將模式: 指定主將 + 副將 (未指定副將 = server 清單隨機):
python tools\autotest\network_runner.py `
    --modes 20p --runs 1 --general s4_huangzhong --general2 zhenji
```

輸出: `tools\autotest\autotest-logs\network\<mode>\server.log` / `runN.log` + `summary-network-<時間>.csv`

`--port` 可指定 server 監聽 port (預設 9527); 平行跑多份時各自指定。

本 runner 的責任是 **soak**: 連跑多局、閃退就重啟重試、以通過率作結論。
要驗「一局固定 seed 的合約」請改用下面的 `gui_network_smoke.py`。

## gui_network_smoke.py — 一局真實 TCP 網路對局的合約驗證 (Linux GUI M2)

一個獨立的 `qsanguosha_server` process + 一個獨立的 GUI client process, 中間走
真正的 TCP。client 以 `--network-ui-smoke` 啟動, 由真正的 RoomScene/Dashboard
回答 askFor (撳真正的 CardItem / Photo / 按鈕), 打完一局後自己乾淨退出:

```bash
# Linux 本機 (WSLg, 用現有 DISPLAY)
python3 tools/autotest/gui_network_smoke.py --exe-root . \
    --mode 02p --seed 20260828 --artifact-dir gui-network-artifacts \
    --no-xvfb --platform xcb

# CI (Xvfb)
python3 tools/autotest/gui_network_smoke.py --exe-root . \
    --mode 05p --seed 20260828 --artifact-dir gui-network-artifacts \
    --xvfb --platform xcb
```

同 `network_runner.py` 的分別: 這裡**不會 retry**。任何一個 stage 缺失、client
不是 exit 0、server 沒有寫出 game over、留下孤兒 process 或 port 沒有釋放, 都是
失敗。`--seed` 是必填的, 沒有隱式預設。

輸出: `<artifact-dir>/network-ui-smoke-<mode>-summary.json` (含 mode/seed/port/
兩個執行檔的 SHA-256/extensions commit)、`-result.json`、client/server log、
marker log; 失敗時另存最後 UI state 與截圖。

模式 ID 以 registry 為準 (`qsanguosha_server --list-game-modes`): 2 人局是
`02p`, 5 人局是 `05p`。詳細契約見 `docs/linux-development-environment.md`。

## crash_report.py — 集中閃退資訊 (可持續執行)

掃描 exe-root 的 `dmp/` + `record/` 與 `tools\autotest\autotest-logs\` 全部
批次 log, 以時間戳自動匹配「閃退局 ↔ dmp ↔ log ↔ record」, 並以純 Python
解析 minidump (例外碼 / 位址 / 崩潰模組 RVA, 不需 cdb/windbg):

```powershell
python tools\autotest\crash_report.py `
    --exe-root \\DESKTOP-VON1J9F\game\sgs\QSanguoshaFinal
```

輸出: `tools\autotest\autotest-logs\crash-report\<時間戳>\inventory.csv`
(每顆 dmp 一列) + `batches.csv` (各批次局數摘要)。每次執行新增時間戳
目錄, 不刪改任何既有檔案。

## tools/ci/linux-gui-multimedia-smoke.sh — Qt 音訊/影片背景合約 (Linux GUI M2B-A)

不在 `tools/autotest/` 而在 `tools/ci/`, 因為它同 M1 的
`linux-gui-startup-smoke.sh` 一樣是「起一個 GUI process, 驗它印出的
structured marker」而不是驅動一局遊戲。

```bash
bash tools/ci/linux-gui-multimedia-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
    --no-xvfb --platform xcb --label wslg --expect-backend qt
```

驗 `MULTIMEDIA_STAGE` / `VIDEO_BACKEND_RESULT` / `MULTIMEDIA_RESULT` 三種
marker: audio backend 選擇、短 UI 音效、語音 player pool、BGM、缺資產降級、
QML media component、乾淨關閉。**不要求真的聽到聲音** — CI runner 沒有音訊
裝置, `output_device: false` 是被記錄的正常狀態。

契約細節與 exit code 對照見
`docs/linux-development-environment.md` §4.7。

## tools/ci/linux-gui-effects-smoke.sh — 效果 profile 合約 (Linux GUI M2B-B)

同 multimedia smoke 一樣係「起一個 GUI process, 驗它印出的 structured
marker」。一個 profile 一次執行。

```bash
for profile in none reduced full; do
    bash tools/ci/linux-gui-effects-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
        --profile "$profile" --no-xvfb --platform xcb --label "wslg-$profile"
done
```

驗 `EFFECTS_STAGE` / `EFFECTS_PROFILE_RESULT` / `EFFECTS_RESULT` 三種 marker:
profile 解析（要求嘅 profile 一定要真係行到, 而且 resolution source 要係
`cli`）、exactly-once completion、frame animation／GIF／Spine 的缺資產降級、
每個 profile 的物件預算、乾淨關閉。**不比較 pixel** — screenshot 只作
failure artifact。

`gui_network_smoke.py --effects-profile <p>` 則用真 TCP 打完一整局來證明
「跳咗動畫都唔會卡死」; `none` 嗰次會額外驗成局打完之後 Spine／QMovie／
QML 疊層／video object 全部係 0。

契約細節與 exit code 對照見
`docs/linux-development-environment.md` §4.8。

## 一鍵 batch (選擇寫在 bat 頂部)

| 檔案 | 用途 | 頂部變數 |
|---|---|---|
| `run_headless.bat` | headless 壓力測試 | `MODES/GAMES/PARALLEL/GENERAL/GENERAL2/SPAWNDELAY/LOG_DIR/LABEL`（`EXE`/`SEED` 具預設，可覆寫） |
| `run_network.bat` | 真實網路測試 | `MODES/RUNS/GENERAL/GENERAL2/CONSOLE/LOG_DIR/LABEL` |

例: 08p 一局、主將 s4_huangzhong、副將隨機 → 改 `run_network.bat` 頂部
`MODES=08p RUNS=1 GENERAL=s4_huangzhong GENERAL2=` 後直接執行。

## C++ 支援參數

| 參數 | 目標 | 說明 |
|---|---|---|
| `--headless --game-mode <id> --games <N>` | QSanguosha.exe | headless 壓力測試指定模式與局數 (預設 08p/10000) |
| `--game-mode <id>` | qsanguosha_server.exe | 網路伺服器覆寫 GameMode |
| `--test-general <名>` | QSanguosha.exe | 自動選將 (FreeChoose; 1v1 用 x0) |
| `--auto-robots` | QSanguosha.exe | owner 進房自動填 AI 並開局 |
| stdout `[AUTOTEST] game start/over <winner>` | qsanguosha_server.exe | runner 結束偵測 |

## 注意

- 舊 `startserver.bat` 指向已不存在的 `H:\...\0705\`; 一律以 runner spawn。
- `taskkill /IM QSanguosha.exe` 會誤殺 server; runner 一律按 PID 精準結束。
- 02_1v1 的 KOF 選將以 `x0` 佔位 (伺服器映射到隨機未知將), 無法指定特定武將。
- runner 已跨平台: 執行檔名、process group spawn、process-tree 清理、exit code
  解讀 (Windows NTSTATUS / POSIX 訊號) 全部集中在 `runner_common.py`, 所以同一份
  runner 在 Windows 與 Linux 都跑得到。Linux 上 GUI client 需要一個可用的
  DISPLAY (WSLg, 或由 `gui_network_smoke.py --xvfb` 自動起 Xvfb)。
