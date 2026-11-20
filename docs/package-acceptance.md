# Linux 武將包驗收工具

`tools/autotest/package_acceptance.py` 在真實專用伺服器與 TUI 托管對局上，依序驗收武將包。它使用 `tui_network_smoke.py` 的對局／清理流程，以及 `verification_report.py`、`skill_branch_report.py` 的決策與技能分支判讀；不產生假局面、不代答技能、不重寫 AI。

## 三項獨立結果

| 項目 | 通過條件 |
|---|---|
| Playback | 實際指定武將的座位已確認、自然 GAME_OVER、TUI 完整結局與乾淨退出、伺服器正常清理、無殘留程序／埠 |
| Subject AI／Room AI | 指定座位的正式遊戲決策／全房決策有 isolated 接受證據，沒有拒絕、回退、其他控制者接管或待查訊號；選將不計為正式遊戲接管 |
| Skill coverage | 每個可見直接技能有已審核規格；所宣告分支在實局被觀測且符合預期，未知歸因保持未知 |

原生 `--acceptance-catalog` 匯出當前已載入的武將、包、直接／關聯技能、翻譯正文、V2 定義類型及 code identity。V2 以原生型別判斷，不以名稱或缺少 `Q_OBJECT` 的具體類別名稱猜測。工具保留非 V2 技能，不默默略過；定義為 V2 仍需行為證據。

總結只有三項均通過且可見直接技能皆為 V2 時才 PASS。沒有規格為 NOT_SPECIFIED，草稿為 UNREVIEWED，未遇到分支為 NOT_COVERED，資料不足為 UNKNOWN。FAIL、REVIEW、TIMEOUT 與未完成清單會分別保留。這套入口不包含各平台 GUI／觸控／遙控器驗收。

## Linux 準備

先依 [Linux 開發環境](linux-development-environment.md) 準備 Python 3.10+、CMake 4.2+、Ninja、GCC、Qt6 Core／Network／WebSockets 與 SWIG。server/TUI 不要求 GUI Qt 或 Xvfb。

來源必須包含本輪工具／C++ 修改，以及權威 extensions 倉庫的 `ai/ → lua/ai/`、`extensions/ → extensions/`、共用 `lua/ → lua/`。尚未提交的修改不會因 git clone 自動出現在雲端；不要用遠端舊 commit 取代欲驗版本。同步後先建置，再建立計畫。

```bash
cmake --preset linux-server-gcc-debug
cmake --build --preset linux-server-debug \
  --target qsanguosha_server qsanguosha_tui --parallel 2
```

已有相同建置設定時只需增量 build。並行數依雲端 RAM 決定；遊戲並行數另由工具 `--jobs` 控制。

## 建立清單，不啟動對局

```bash
python3 tools/autotest/package_acceptance.py plan \
  --root "$PWD" \
  --server "$PWD/debug/qsanguosha_server" \
  --tui "$PWD/debug/qsanguosha_tui" \
  --groups standard,nos,wind,fire \
  --modes 03_1v2 --seeds 20261113,20261114 \
  --out "$PWD/artifacts/v2-acceptance"
```

`standard` 選 standard 包中非 `nos*` 武將；`nos` 選當前 registry 中所有 `nos*` 武將（含其他包的懷舊武將，實際 package 保留在清單）。其他 group 精確對應原生包 ID，例如 `wind`、`fire`、`forest`；重複武將只由最早匹配的 group 排程。隱藏武將也保留，以免遺漏 NOS；實際無法強制入座者會 FAIL，不算通過。

當前 registry 的 `standard` 包中非 NOS 武將只有孫權、孫尚香、甄姬、諸葛亮；曹操、劉備、關羽等位於 `strengthen` 包。需要它們時使用 `--groups standard,nos,wind,fire,strengthen`，以 `catalog.json` 的實際 package 為準。

預設每名武將一個 `03_1v2` 模式、一個固定 seed、單局 1800 秒、啟動 120 秒；可明確指定 `--modes`、`--seeds`、`--game-seconds`、`--startup-seconds`。`--only sunquan,nos_caocao` 可縮小準備範圍，不會自動新增未選包的武將。完整擴展載入較慢時，可調整 `--catalog-seconds`（預設 120 秒）；逾時會保留 `catalog.log` 與部分輸出。

輸出含 `catalog.json`、`source-manifest.json`、`plan.json`、`state.json`、`summary.json/.md`，以及逐技能 `spec-drafts/`。計畫凍結執行檔 SHA-256、來源／Lua 雜湊、會影響規則或 AI 的環境設定雜湊與規格；來源不只檢查 Git HEAD，也包含 ignored Lua 與本地修改。建置本身與來源是否相符仍由上述建置步驟確保。

輸出目錄請用 `artifacts/` 或自訂獨立目錄，不放入 `src/`、`lua/`、`extensions/` 等輸入樹。

## 執行與續跑

```bash
# 先完成一局，最多花 35 分鐘，含啟動／單局預算。
python3 tools/autotest/package_acceptance.py run \
  --out artifacts/v2-acceptance --budget-minutes 35 --jobs 1 --max-games 1

# 同一份計畫繼續未執行項；先完成 standard，再進入 nos、wind、fire。
python3 tools/autotest/package_acceptance.py run \
  --out artifacts/v2-acceptance --budget-minutes 240 --jobs 2

python3 tools/autotest/package_acceptance.py status --out artifacts/v2-acceptance
```

總預算為必填，不預設整晚無限跑。到期不開新局，進行中對局受剩餘預算限制；正常清理另有有界寬限。Ctrl+C／SIGTERM 要求子 runner 經既有清理路徑退出。每局使用獨立設定、user data、run ID、證據與結果目錄，不修改遊戲回答來提高覆蓋率。

失敗／逾時不自動重試。需要重現時，用 `summary.json` 中完整 cell ID：

```bash
python3 tools/autotest/package_acceptance.py run \
  --out artifacts/v2-acceptance --budget-minutes 35 \
  --retry standard/sunquan/03_1v2/20261113
```

每次重試新增 attempt 目錄，既有失敗保留在 state 與 summary 的 previous_attempts。中斷後只在原 host、已記錄程序均不再存活時恢復；活著的舊程序不會被當成可重跑的空閒工作。保存 PID 和 OS 啟動時間避免 PID 重用誤殺。跨機搬運中斷中的 batch 不自動接手程序。

版本或規格改了便用新 out 建計畫，工具拒絕把新結果塞進舊版計畫。執行中輸入改變會使 batch INVALIDATED。不要手改 plan/state 讓檢查過關。

## 技能規格與覆蓋

草稿只列出當前技能正文與待審核項，不從程式實作反推「正確答案」。按 [結算歷史規格](resolution-history.md#驗收分支報告) 填 `branches`、核對獨立規則來源後才標 `reviewed: true`。

批次規格沿用 version 1；每一分支的 `when.where` 必須至少有一個值為 `$subject`，通常是 `data.skill_owner`。批次工具從當局 `[AUTOTEST] force general` 取得唯一角色 objectName，再產生該局 bound-specs；不能把武將 ID 當 seat，也不會把其他座位的同名技能混進去。`$trigger.data.skill_owner` 等既有規格引用維持不變。

審核後另建計畫並使用 `--spec-dir /path/to/reviewed-specs`。未審核草稿可以載入，但仍顯示 UNREVIEWED。不同 general/skill 不得有重複規格。跨 seed／模式只合併各分支的觀察狀態，FAIL 與 UNKNOWN 保留，未遇到分支不會蓋掉另一局已觀察的 PASS。

查看每局 `verification/verification.json` 的 decision ID、回退原因、歷史 scope、技能效果及原始檔案位置；完整服務端歷史僅作離線驗收，不送進 isolated AI 或外部模型。程序失敗和技能效果錯誤需要按問題層定位，工具不會自動修改原生引擎。

CLI 退出碼：`0` 全部驗收條件通過；`1` 已觀察明確失敗；`2` 待驗／待查／逾時／輸入或版本問題。`plan` 成功返回 0 只表示清單建立完成。
