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
    --exe-root L:\finaldebug\QSanguosha-v2 `
    --modes 10p,20p,02_1v1,05p `
    --games 5 --parallel 2
```

`--parallel` = 同時執行的 **process 總數**：模式數 ≥ parallel 時每個模式
一個 process；模式數不足時同一模式開多份 (round-robin)，每份獨立 log
(`<mode>-N.log` / `<mode>-N-headless.log`)。例: `--modes 20p --parallel 10`
= 10 個 20p process 同時跑。注意多 process 同時打 20p 對 CPU/RAM 負載高。

輸出: `autotest-logs\headless\<mode>[-N].log` + `summary-headless-<時間>.csv`

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

輸出: `autotest-logs\network\<mode>\server.log` / `runN.log` + `summary-network-<時間>.csv`

## 一鍵 batch (選擇寫在 bat 頂部)

| 檔案 | 用途 | 頂部變數 |
|---|---|---|
| `run_headless.bat` | headless 壓力測試 | `MODES/GAMES/PARALLEL/LOG_DIR/LABEL` |
| `run_network.bat` | 真實網路測試 | `MODES/RUNS/GENERAL/GENERAL2/LOG_DIR/LABEL` |

例: 20p 一局、主將 s4_huangzhong、副將隨機 → 改 `run_network.bat` 頂部
`MODES=20p RUNS=1 GENERAL=s4_huangzhong GENERAL2=` 後直接執行。

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
