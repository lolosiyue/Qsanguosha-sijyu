# 自動化測試工具 (tools/autotest)

使用獨立 runner 執行 headless、網路對局與 GUI 契約檢查。
參數與預設值由各 runner 的 `main()` 及
[`runner_common.py`](runner_common.py) 的 `common_args()` 定義；以 `--help` 查詢。

## 前置

- 先以 `tools/build-cmake.ps1 -Configuration Release` 編譯 (需含 `qsanguosha_server`)
- 執行時 cwd 需含 `config.ini` (runner 會自動選 `release\` 或 `debug\`)

## headless_runner.py — 純 AI 壓力/回歸測試 (可平行)

每模式一個 process, 內部連續跑 N 局 (正式對局使用 SmartAI), 以 exit code + log 標記判定。
`--headless` 會在程序內啟用 AI，不受設定檔的 `EnableAI=false` 影響，也不回寫設定檔。
選將前保留 TrustAI 作初始化，正式開局沿用 `Room::cloneAI()` 建立 SmartAI；Lua AI 建立失敗時仍走既有 TrustAI fallback，runner 會檢查既有 SmartAI 失敗標記。

用法:

```powershell
python tools\autotest\headless_runner.py `
    --exe .\release\QSanguosha.exe `
    --seed 20260828 `
    --exe-root . `
    --modes 08p `
    --games 5 --parallel 2
```

`--exe`／`--seed` 為必填 (runner 契約無隱式執行檔發現／隱式種子, 見
[Lua 擴展規格](../../docs/lua-ext-spec.md)); seed 須為 unsigned 32-bit, 慣例用當日日期
`yyyyMMdd` (如 20260828, 建議用 `run_headless.bat` 自動生成並隨日期前進)。
`--modes` 預設 `20p`。模式 ID 以 server registry 為準: runner 會用
`--exe-root` 下的 `qsanguosha_server.exe` 跑 `--list-game-modes` 查詢
(查不到時改用內建靜態兜底表), 未註冊的模式直接 exit 2。
`--parallel` 為同時執行的 **process 總數**：模式數 ≥ parallel 時每個模式
一個 process；模式數不足時同一模式開多份 (round-robin)，每份獨立 log
(`<mode>-N.log` / `<mode>-N-headless.log`)。例: `--modes 08p --parallel 10`
= 10 個 08p process 同時跑。注意多 process 同時打 08p 對 CPU/RAM 負載高。
`--deadline-minutes` 為整批時間預算：到期不再啟動新模式，進行中的局停止；
CSV 的 `budget` 欄記 `stopped`／`not_run`，這些模式不算失敗。

輸出: `tools\autotest\autotest-logs\headless\<時間戳>-<pid>\<mode>[-N].log`
+ `<mode>[-N]-headless.log`; 彙總表 `summary-headless-<時間>.csv` 在
`autotest-logs\` 根目錄。

## network_runner.py — 真實網路測試 (可平行)

`qsanguosha_server` 常駐 + 每局重啟 GUI client; client 自動選將、
自動填 AI 開局、自動托管, 由 client 參數驅動:

```powershell
python tools\autotest\network_runner.py `
    --exe-root . `
    --modes 10p,20p,05p `
    --runs 2 --general heg_zhanglu

# 1v1 KOF 用佔位選將:
python tools\autotest\network_runner.py `
    --modes 02_1v1 --runs 2 --general x0

# 雙將模式: 指定主將 + 副將 (未指定副將 = server 清單隨機):
python tools\autotest\network_runner.py `
    --modes 20p --runs 1 --general s4_huangzhong --general2 zhenji
```

輸出: `tools\autotest\autotest-logs\network\<時間戳>\<mode>\server.log` / `runN.log`
+ `summary-network-<時間>.csv` (彙總表在 `autotest-logs\` 根目錄)。
模式、局數與選將預設值見 [`network_runner.py` 的 `main()`](network_runner.py) 及 `common_args()`。

`--port` 可指定 server 監聽 port (預設 9527)。

`--general` 可逗號分隔多個武將, 每個模式 × 每個武將是一個 job。`--parallel N`
同時跑 N 個 job: 第 i 個 slot 用 port `--port`+i, client 以
`--window-geometry=WxH+X+Y` 平鋪成 N 格小窗 (不寫回 config.ini 的視窗大小),
並帶 `--mute --effects-profile=none` 關閉 BGM、音效與動畫特效。
多武將時 log 目錄為 `<mode>-<general>`, 彙總表多一欄 `general`。

```powershell
python tools\autotest\network_runner.py `
    --exe-root . --modes 05p --runs 1 `
    --general zhenji,caocao,liubei,sunquan --parallel 4
```

本 runner 的責任是 **soak**: 連跑多局、閃退就重啟重試、以通過率作結論。
要驗「一局固定 seed 的合約」請改用下面的 `gui_network_smoke.py`。

## skill_scenario_runner.py — 技能情境：擺局面、直接觸發、斷言結算 (可平行)

`qsanguosha_skill_scenario` 以 case 的局面建真 Room（沿用 MiniSceneRule 的 setup），
setup 完成後執行 case 的 Lua `trigger`，跑完即結束對局，輸出 history facts 與終局盤面。
runner 把 report 正規化為座位號與牌 spec，再比對 case 的 `expect`。
其餘詢問由 SmartAI 回答；`script` 以既有 test override 固定指定詢問的答案。
case 是一次性檔案，放 scratchpad，不進 repo。

```powershell
cmake --build builds\cmake-vs2026 --config Release --target qsanguosha_skill_scenario
python tools\autotest\skill_scenario_runner.py case1.json case2.json `
    --exe .\release\qsanguosha_skill_scenario.exe --seed 20261011 `
    --qt-bin H:\Qt6111\6.11.1\msvc2022_64\bin --jobs 4
```

target 為 `EXCLUDE_FROM_ALL`，需明確指定建置。Release 每個 case 約 10 秒（多半是引擎與 Lua 啟動），Debug 約 70 秒。
輸出在 case 旁的 `results\`：`<case>.report.json`（原始）、`<case>.view.json`（比對用）、`<case>.log`。
exit code：0 全 PASS、1 有 FAIL、2 有 host 錯誤或逾時。

case 格式：

```json
{
  "seats": [
    {"general": "caocao", "hp": 3},
    {"general": "sujiang", "hand": ["slash/spade/7"]}
  ],
  "pile": ["peach"],
  "script": [{"seat": 1, "type": "choice", "key": "jianxiong", "answer": "obtain"}],
  "trigger": "use('slash/spade/7', P[2], {P[1]})",
  "expect": {
    "damage": [{"from": 2, "to": 1, "amount": 1}],
    "invoked": [{"seat": 1, "skill": "jianxiong"}],
    "seats": {"1": {"hp": 2, "hand": ["slash/spade/7"]}}
  }
}
```

| 欄位 | 內容 |
|---|---|
| `seats[]` | `general`（必填）、`general2`、`role`（預設第 1 席 lord，其餘 rebel）、`hp`、`maxhp`、`hujia`、`kingdom`、`chained`、`turned`、`starter`（預設第 1 席）、`skills`（取得技能）、`marks`（`{"@x": 2}`）、`hand`／`equip`／`judge`（牌 spec 或 id）、`draw`（開局摸牌數，預設 0）、`hidden`（國戰暗置主將，需 `hegemony`）。未知 key 直接報錯 |
| `pile` | 牌堆頂，第一張在最上 |
| `hegemony` | `true` 時盤面擺好後才開啟國戰規則（不經國戰選將），供暗將、亮將與明置判斷 |
| `script[]` | `seat`、`type`、`key`、`answer`。`type`：`skill_invoke`（bool）、`choice`（字串）、`player_chosen`（座位號）、`card_chosen`（牌 spec）、`card`（牌 spec 或 legacy 牌字串；key 為技能名或 pattern）、`activate`（`"pass"`）。key 是該詢問的 reason，與技能程式呼叫的 `askForXxx` 一致 |
| `trigger` | Lua，在 room thread 執行 |
| `expect` | 見下方比對規則 |

牌 spec 為 `name`、`name/suit` 或 `name/suit/number`。setup 從本局牌組取未用過的相符牌。

trigger 可用：

| 名稱 | 作用 |
|---|---|
| `room`、`P[n]` | Room 與第 n 席 |
| `card(spec)` | 先找角色持有的相符牌，再找牌堆 |
| `use(c, from, {targets})` | `room:useCard`；被拒絕時報錯 |
| `activate(c, from, {targets})` | 以出牌階段發動使用：設 current、出牌階段、用牌原因 PLAY 後 `useCard` |
| `select_use(c, from, {targets})` | 同 `activate`，但如 client／AI 選擇提交：重驗目標、次數與距離，國戰 TargetMod 亮將規劃也會執行；被拒絕時報錯 |
| `skillcard(class, skill, owner, {specs})` | clone 技能卡並綁定 owner 的 V2 技能實例 |

其他時機直接呼叫引擎 API，如 `room:damage(...)`、`room:loseHp(P[1])`、`room:judge(...)`、`P[1]:play({sgs.Player_Start})`。
trigger 從技能依賴的最上游事件開始：技能查「本次傷害是否由殺造成」就 `use` 殺，不直接 `room:damage`。

比對規則（`expect` 對 `view.json`）：

- 物件：子集合；列表：依序的子序列，`[]` 表示必須為空；牌 spec 可只寫前段（`"peach"` 匹配 `"peach/heart/3"`）。
- `error`：省略時要求 trigger 無錯；給字串時要求 Lua 錯誤訊息含該字串。
- view 欄位：`seats`（`"1"` 起，含 hp、maxhp、alive、hand、equip、judge、marks、piles、skills…）、`damage`（from、to、amount、hp_loss、absorbed、nature、card）、`invoked`（seat、skill；略去 `#` 隱藏技能）、`used`、`responded`、`recover`、`moves`（card、from、to、from_place、to_place、skill）、`winner`（trigger 中分出勝負時為勝方，否則 `"."`）。

注意：

- 詢問種類以技能程式為準。例：本專案的奸雄以 `askForChoice`（obtain／draw／cancel）詢問，`skill_invoke` 對它無效。
- test override 只涵蓋上表 `type`；其他詢問（棄牌、AG、遺計、觀星等）由 SmartAI 回答。
- SmartAI 沒有 Lua AI 的新技能不會發動；要測技能本身時以 `script` 指定答案。

## skill-text-audit.py — 技能描述與定義比對 (靜態)

`tools/skill-text-audit.py` 執行 `qsanguosha_skill_scenario --describe-skills`，把描述開頭標籤（锁定技、限定技、觉醒技、主公技、转换技、使命技、隐匿技）與定義的頻率／旗標比對，並把已宣告 LimitScope 的技能與描述中的「出牌阶段限／每回合限／每轮限」比對。舊式技能在回呼內自行計次，靜態無法比對使用次數。

```powershell
python tools\skill-text-audit.py                      # 預設 release\qsanguosha_skill_scenario.exe
python tools\skill-text-audit.py --skills skills.json # 用既有 dump
```

輸出 `artifacts/skill-text-audit/report.json` 與 `summary.md`（`-o` 可改）。結果是待人工確認的候選，不是判定：執行期改變頻率的技能、共用描述的變體可能合理不一致。隱藏與無擁有者技能（裝備、輔助技）只檢查「描述宣稱、定義缺少」一個方向。不新增案例或 fixture。

## general_acceptance.py — 單一武將驗收 (技能情境 + UI + 全模式 + 各平台)

把同一個武將串過每個驗證層。遊戲階段由 server `--force-general` 把武將交給房主
（headless 為第 0 席），各平台 client 不需改動。agent 流程見本機
`.agents/skills/general-acceptance/SKILL.md`。

預設 `--profile trustee`：只跑 `gui,tui`，由產品既有托管完成對局，不建立技能 case、
不逐招選牌或輸入。指定 `--stages` 時只執行列出的階段；完整技能／詢問驗收須明確選 `--profile full`。
托管結局只證明客戶端對局路徑，報告將技能分支列為 NOT RUN。

```powershell
python tools\autotest\general_acceptance.py --general s4_cloud_huangzhong --seed 20261012 `
    --profile trustee --stages gui,tui --qt-bin H:\Qt6111\6.11.1\msvc2022_64\bin
```

完整流程：

```powershell
python tools\autotest\general_acceptance.py --general s4_cloud_huangzhong --seed 20261012 `
    --profile full --cases <scratch>\cases --qt-bin H:\Qt6111\6.11.1\msvc2022_64\bin --jobs 6 `
    --stages describe,skill,ui,headless,gui,gui_heg,tui
```

| stage | 內容 | 判定 |
|---|---|---|
| `sync`（自動） | 任一 stage 會載入 L 的 Lua 時先執行：`lua/ai`、`extensions`、`lua` 與 H 權威樹（`--extensions-authority`）以大小＋SHA-256 比對，列出 H 未提交檔 | L 較舊或缺檔 FAIL，載入 Lua 的 stage 一律 BLOCKED；L 較新但 H 在其後也有提交或未提交修改（兩邊都改）FAIL，須先以 H 為底合併；只有 L 較新（未回寫）WARN；`--skip-sync-check` 略過 |
| `describe` | `qsanguosha_skill_scenario --describe-general`：package、技能與翻譯後描述 | 武將存在 |
| `skill` | `--cases` 目錄交給 `skill_scenario_runner.py` | 全 PASS |
| `ui` | 每個可見技能生成一份發動詢問 case 給 `skill_ui_runner.py`：詢問顯示翻譯名而非 id | runner exit 0 |
| `headless` | registry 全部模式（不含 `*_emjr_cc*` 曹操劇情關卡）+ 6 個劇情模式，各 1 局；`--headless-minutes`（預設 60，0 = 不限）為整批預算 | 對局完成、有 force 標記；Lua 錯誤記 WARN；預算內未完成的模式記 STOPPED／NOT RUN，stage 至多 WARN |
| `gui` | `network_runner.py` 03_1v2 | 同上 |
| `gui_heg` | `network_runner.py` 08p 國戰：stage 目錄的 `hegemony.ini`（`EnableHegemony=true`）經 `--server-config` 交給 server `--config`，只在 server 記憶體生效，不寫回 `config.ini` | 同上 |
| `tui` | `tui_network_smoke.py` 03_1v2 | 同上 |
| `android` | `--android-serial`：本機 server + `adb reverse` + 產品 `--network-ui-smoke` 03_1v2；雷電14 x86_64 Debug 或雷電9 ARMv7 Release。Release 不可 `run-as`，改從 logcat 的 `NETWORK_UI_STAGE`／`NETWORK_UI_RESULT` 取 client result（`android/logcat-smoke.txt`） | server 結局、client result `ok`、force 標記 |
| `xp` | VM 內依 XP 文件跑 `QSanguoshaXPServer --force-general` 03_1v2，`--xp-evidence` 指向取回的 log | `GAME_STARTED`/`GAME_OVER` 與 force 標記 |
| `web`／`sheets` | `--web-evidence`／`--sheets-evidence` 匯入獨立真實托管驗收證據；不啟動 browser、workbook 或 tunnel | 身分、勝方、退出／orphan／port 均通過；缺件 NOT RUN，矛盾 FAIL |
| `linux` | 本機無環境，記 NOT RUN | — |

輸出：`autotest-logs/general-acceptance/<general>-<時間>/`（或 `--out-dir`）下各 stage 目錄、`summary.json`、`summary.md`。
任何 stage FAIL 或 CRASH 時 exit 1。

### Web/Sheets evidence — 托管結果匯入

先依 skill 完成真實客戶端托管局，再以新輸出目錄彙整；此命令只讀既有證據，不重開對局：

```powershell
python tools\autotest\general_acceptance.py --general s4_cloud_huangzhong --seed 20261012 `
    --profile trustee --stages web,sheets --web-evidence <run>\web `
    --sheets-evidence <run>\sheets --out-dir <run>\aggregate
```

每個 evidence 目錄必須有下列檔案。缺件記 NOT RUN；格式錯誤、身分／結果矛盾或清理失敗記 FAIL。
只支援托管對局結果匯入，不能代替 `full` 的技能分支或操作驗收，也不能把舊 PASS 文字當作新證據。

| 檔案 | 內容 |
|---|---|
| `client-state.txt` | 結局時直接複製的實際客戶端文字：自己的 player ID、武將翻譯名、結局與勝方。Web 可存 AX；Sheets 應複製結果與自己座位的工作表文字／TSV，canvas 的 AX 可能不含儲存格。不得從 server log 或截圖重建 |
| `client-observation.json` | 以下 schema；由本局 live UI 觀察填入，snapshot 雜湊綁定前一檔。不得預填成功值 |
| `server.log`、`server-autotest.log` | `force general` 指向自己的 ID、開始與自然結局／winner；Lua 錯誤另列 WARN |
| `host.json` | `server_args` 含 general／seed／mode／TCP／WS 旗標；`processes` 為名稱→PID，`host_pid`、`ports` 完整列出本局擁有的資源 |
| `cleanup.json` | `reason`、同一組 `processes` 的 `pid`／`exit_code=0`／`alive=false`／`shutdown`、`ports_released`。shutdown 為 graceful-console、graceful-gateway-finally、console-break 或 already；timeout／forced／crash 不算通過 |
| `process-check.json` | 清理後實查的 `owned_pids` 與 `alive_owned_pids=[]`，涵蓋 host、各服務、Sheets bridge／helper |
| `artifact-manifest.json` | 本局 `general`／`seed`／`mode`，Web 另需 `rules_identity_match=true`；Sheets 另需 `scripts_match=true`，均須先實際核對 |
| `bridge-exit.json`（Sheets） | 從私有 `exit.json` 僅匯出 `native_exit_code`、`clean_native_exit`、`forced_termination`、`cleanup_error`；正常為 0／true／false／null，不複製 session token |

`client-observation.json` 欄位示意（用實際觀察取代示例值）：

```json
{
  "schema_version": 1,
  "profile": "trustee",
  "general": "s4_cloud_huangzhong",
  "seed": 20261012,
  "mode": "03_1v2",
  "player_id": "sgs1",
  "general_name": "谋黄忠",
  "trustee_enabled": true,
  "runtime_ready": true,
  "runtime_errors": [],
  "game_over": true,
  "winner": "rebel",
  "winner_text": "sgs1 反贼",
  "snapshot_sha256": "SHA-256 of the exact client-state.txt bytes"
}
```

`winner` 使用原生 winner token；`winner_text` 是客戶端勝方區的原樣文字，必須存在於 snapshot 並含該 token／翻譯身份。
Sheets 額外記錄 `https_paired=true`、實際 `bridge_pid` 與 `helper_pids`；直接連專用 server 未建立 helper 時填空陣列。
有 helper 時仍需把它納入 host／exit／process-check，不能只看 bridge exit。
匯入不掃描或搬動全域 `dmp/`，避免影響同期對局；自身退出碼閃退會進同一份 `crashes.json`。

閃退統一收在同一份報告，stage 記 CRASH：

| 來源 | 收集方式 |
|---|---|
| 各 runner 的閃退 exit code | skill 情境 host、skill_ui `CRASH`、headless `crashed`、network_runner 閃退 note、TUI lifecycle |
| CrashHandler dump | stage 期間 `dmp/`（倉庫根、執行 cwd、`release/`）新增的 `crash-*`（`.dmp`、`.txt` 原生與 Lua 堆疊、`-config.ini`）移到 `crashes/<stage>/` |
| Android | stage 開始後的 `logcat -b crash`（`android/logcat-crash.txt`）與 server exit code |
| XP | `--xp-evidence` 內的 `*.dmp` 與同名 `.txt` |

同一 stage 只有一個 exit code 閃退和一個 dump 時合併為一筆。`crashes.json` 為機器格式；
`summary.md` 的 Crashes 段列出 stage、process、例外碼、模組偏移與 crash 摘要。
跨批次的 dump 對 log／record 比對仍用 `crash_report.py`。

## gui_network_smoke.py — 一局真實 TCP 網路對局的合約驗證 (Linux GUI M2)

一個獨立的 `qsanguosha_server` process + 一個獨立的 GUI client process, 中間走
真正的 TCP。client 以 `--network-ui-smoke` 啟動, 由真正的 RoomScene/Dashboard
回答 askFor (按下真正的 CardItem / Photo / 按鈕), 驗收要求完成一局並乾淨退出:

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
`02p`, 5 人局是 `05p`。詳細契約見 [Linux 開發指南](../../docs/linux-development-environment.md)。

## XP VM — 使用 legacy 專用驗收入口

XP 的配對程式為 `QSanguoshaXP.exe` / `QSanguoshaXPServer.exe`；外部伺服器
AI 對局使用 `--xp-acceptance=external`。本次 XP 獨立入口未啟動主線
`--auto-robots` / `--network-ui-smoke` driver，不能拿候場畫面判定成功。
專用 driver 有 10 分鐘期限，按設計填 robot 並啟用托管，不涵蓋手動 request UI。

建置、部署、ISO、Guest Control／無憑證 CMD 備援、server INI 陷阱與清理方式，
統一查閱 [XP legacy 指南](../../docs/windows-xp-legacy-build.md#xp-acceptance-entry-and-isolated-settings)，
不另建一套 XP runner 流程。2026-10-03 單次測試已完成執行：server 有完整結局，
GUI `FAIL_timeout`／exit 1；保留兩者不同結果，不自動 retry。
後續字體修正只有 source/static 證據，尚未重建或驗收畫面。

## crash_report.py — 集中閃退資訊 (可持續執行)

掃描 exe-root 的 `dmp/` + `record/` 與 `tools\autotest\autotest-logs\` 全部
批次 log, 以時間戳自動匹配「閃退局 ↔ dmp ↔ log ↔ record」, 並以純 Python
解析 minidump (例外碼 / 位址 / 崩潰模組 RVA, 不需 cdb/windbg):

```powershell
python tools\autotest\crash_report.py `
    --exe-root .
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
QML media component、乾淨關閉。CI 可在沒有音訊裝置時執行，並記錄 `output_device: false`；實際聽感由有音訊裝置的環境驗收。

契約細節與 exit code 對照見
[Linux 開發指南](../../docs/linux-development-environment.md) §4.7。

## tools/ci/linux-gui-effects-smoke.sh — 效果 profile 合約 (Linux GUI M2B-B)

同 multimedia smoke 一樣是「起一個 GUI process, 驗它印出的 structured
marker」。一個 profile 一次執行。

```bash
for profile in none reduced full; do
    bash tools/ci/linux-gui-effects-smoke.sh ./relwithdebinfo/QSanguosha artifacts \
        --profile "$profile" --no-xvfb --platform xcb --label "wslg-$profile"
done
```

驗 `EFFECTS_STAGE` / `EFFECTS_PROFILE_RESULT` / `EFFECTS_RESULT` 三種 marker:
profile 解析（要求的 profile 必須真正執行得到, 而且 resolution source 必須是
`cli`）、exactly-once completion、frame animation／GIF／Spine 的缺資產降級、
每個 profile 的物件預算、乾淨關閉。截圖保存為失敗附件；此檢查涵蓋物件與事件契約。

`gui_network_smoke.py --effects-profile <p>` 則檢查指定 profile 下的完整 TCP 對局； `none` 那次會額外驗證整局打完之後 Spine／QMovie／
QML 疊層／video object 全部為 0。

契約細節與 exit code 對照見
[Linux 開發指南](../../docs/linux-development-environment.md) §4.8。

## 一鍵 batch (選擇寫在 bat 頂部)

| 檔案 | 用途 | 頂部變數 |
|---|---|---|
| `run_headless.bat` | headless 壓力測試 | `MODES/GAMES/PARALLEL/GENERAL/GENERAL2/SPAWNDELAY/LOG_DIR/LABEL`（`EXE`/`SEED` 具預設，可覆寫） |
| `run_network.bat` | 真實網路測試 | `MODES/RUNS/GENERAL/GENERAL2/PARALLEL/CONSOLE/LOG_DIR/LABEL` |

例: 08p 一局、主將 s4_huangzhong、副將隨機 → 改 `run_network.bat` 頂部
`MODES=08p RUNS=1 GENERAL=s4_huangzhong GENERAL2=` 後直接執行。

## C++ 支援參數

| 參數 | 目標 | 說明 |
|---|---|---|
| `--headless --game-mode <id> --games <N>` | QSanguosha.exe | headless 壓力測試指定模式與局數 (局數預設 10000; 未給 `--game-mode` 時用 config.ini 的 `GameMode`) |
| `--game-mode <id>` | qsanguosha_server.exe | 網路伺服器覆寫 GameMode (劇情模式 id 亦可用於 headless) |
| `--force-general <名>` | qsanguosha_server.exe／QSanguosha.exe `--headless` | 開局 (選將完成後) 把房主換成該武將, 無房主時換主公; 任何模式生效; stdout `[AUTOTEST] force general <seat> <名>` |
| `--test-general <名>` | QSanguosha.exe | 自動選將 (FreeChoose; 1v1 用 x0) |
| `--test-general2 <名>` | QSanguosha.exe | 雙將模式自動選副將 |
| `--headless --takeover <turn_NNN_turn.json> [--takeover-seat SGSn]` | QSanguosha.exe | 每局在新 Room 還原該回合快照 (Lua 重新從磁碟載入), 改技能後重跑同一局面; 模式與種子取自快照, 全座位 SmartAI; 見 [replay-v2](../../docs/replay-v2.md#headless-takeover-lua-edit-loop) |
| `--describe-skills --report <json>` | qsanguosha_skill_scenario.exe | 全部技能的描述與定義中繼資料 (頻率、主公／轉換／限定旗標、使用範圍、擁有者), 供 `tools/skill-text-audit.py` |
| `--auto-robots` | QSanguosha.exe | owner 進房自動填 AI 並開局 |
| `--window-geometry=WxH+X+Y` | QSanguosha.exe | 固定主視窗大小與位置, 關閉時不寫回 config.ini |
| `--mute` | QSanguosha.exe | 使用靜音音訊後端 (BGM/音效全關, 不改設定的音量) |
| `--effects-profile=none` | QSanguosha.exe | 動畫特效全關 (不寫回設定) |
| stdout `[AUTOTEST] game start/over <winner>` | qsanguosha_server.exe | runner 結束偵測 |

## 注意

- `taskkill /IM QSanguosha.exe` 會誤殺 server; runner 一律按 PID 精準結束。
- 02_1v1 的 KOF 選將以 `x0` 佔位 (伺服器映射到隨機未知將), 無法指定特定武將。
- runner 已跨平台: 執行檔名、process group spawn、process-tree 清理、exit code
  解讀 (Windows NTSTATUS / POSIX 訊號) 全部集中在 `runner_common.py`, 所以同一份
  runner 在 Windows 與 Linux 都跑得到。Linux 上 GUI client 需要一個可用的
  DISPLAY (WSLg, 或由 `gui_network_smoke.py --xvfb` 自動起 Xvfb)。
