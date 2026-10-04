# isolated AI／SmartAI 現有移植驗收（2026-10-03）

最新狀態：**F1–F3 已完成來源修正，完整移植仍未通過執行期驗收**。本頁先保留初次驗收的失敗證據，末節記錄同日後續修正。原始碼已具備純值 VM、共用策略、套件 handler、事件及 V2 轉化接線；執行期與完整對局未跑，獨立 isolated 驗收仍欠 F4 的 native 決策來源／fallback 觀測證據。

初次交付只有驗收與文件；使用者隨後授權修正程式缺陷，修正範圍限定 F1–F3，沒有 commit／push。主倉庫 `debug` 與外部 AI 倉庫的既有 dirty 工作均保留。後續執行移植使用[遵循文件](isolated-ai-migration-playbook.md)。

## 初次驗收範圍與證據

盤點 manifest 的 29 筆記錄、11 個 required 檔、7 個 isolated core、13 個 package handlers；比對 40 個 Lua 檔的 L/H 位元組與 SHA-256，含 22 個 isolated 目錄檔、bootstrap/facade、SmartAI/mode/value-boundary 及 13 個舊套件來源。套件盤點不是逐技能全分支窮舉。

本機可重查證據在 `builds/isolated-ai-acceptance-20261003/static-audit.json`，分支／HEAD／外部 dirty 清單在同目錄 `source-baseline.json`；該目錄為忽略的本機驗收產物，不作部署來源。`audit_sources.py` 是唯讀靜態盤點工具，沒有執行 Lua 或建立玩法 fixture。程式圖用於 native discovery，實際工作檔用於確認 dirty 版本；Lua AI 以定向讀檔核對。

| Gate | 結果 | 實際界線 |
|---|---|---|
| manifest 一致性 | **FAIL** | checker exit 1，3 個問題，見 F1 |
| isolated 語法樹 | **PASS（靜態）** | 22 個目錄檔＋bootstrap/facade 共 24 檔，tree-sitter 無 parse error；不是產品 Lua load 成功 |
| legacy 語法樹 | **未能完整驗證** | 9 檔有通用 parser error；來源使用專案 Lua 方言，不能直接判定為產品語法錯誤 |
| L/H 來源一致性 | **PASS（本機）** | 此次 40 檔雜湊／位元組全部相同 |
| 外部發布 | **未驗證遠端發布** | 本機 main、origin 正確，與本機 upstream ref 比較 ahead 2/behind 0；6 個 tracked AI 檔修改、6 個 isolated 檔 untracked（含新共用核心）；沒有 fetch／commit／push |
| 策略等價 | **FAIL／其餘部分待驗** | F2、F3 有具體靜態反例；未完整逐技能驗證 |
| 建置／產品 Lua 載入 | **NOT RUN** | 本輪沒有 source 修正，不以舊二進位替本輪來源背書 |
| 零 fallback 的 isolated runtime | **BLOCKED** | 見 F4；目前的成功完局／無 error log 不足以證明 |
| 完整對局、GUI、跨平台、CI | **NOT RUN** | 未啟動長時間 gate；不能以靜態結果替代 |

靜態工具早期逐節點走訪曾以 Windows access violation 結束，未產生文字日誌；退出碼與最終文件檢查存於 `verification-notes.json`。最終改為只讀整樹 error flag，取得上述完整結果。此為 Python/parser 工具故障，不是遊戲程序或 Lua runtime 崩潰。

## 初次未通過項（行號為修正前來源）

### F1 — P1：新增核心沒有完整部署檢查

`isolated-bootstrap.lua` 的 `ai_isolated_core` 和 manifest 已包含 `smart-ai-functions.lua`，但：

| 缺漏位置 | 實際影響 |
|---|---|
| `tools/ci/fetch-extensions.sh:153` | fetch 後必要檔檢查未包含新核心 |
| `tools/ci/fetch-extensions.ps1:72` | 同上，Windows 路徑亦缺少 |
| `CMakeLists.txt:1146` 的 `QSAN_ASSET_REQUIRED` | 必要資產清單未要求新核心 |

`python tools/ai/check-ai-runtime-manifest.py` 實際報 3 problems。這不證明當前複製器一定漏複製該檔，而是缺檔來源不能被相應 gate 攔截；清單與來源不一致，部署驗收不能通過。

### F2 — P2：三處選牌忽略排序函式回傳

`lua/ai/isolated/decision-core.lua:194` 的 `sorted_copy` 建立新清單；`:215`、`:219`、`:223` 的 `sortByKeepValue`／`sortByUseValue`／`sortByUsePriority` 都回傳副本，未知評分則回 nil。

| 呼叫端 | 偏差及觸發條件 |
|---|---|
| `isolated/smart-ai-functions.lua:98` `getCard`，`:102` | 多張候選沒有接回排序結果，`:105` 取原清單第一張；高 priority 在後時選錯 |
| 同檔 `:592` `askForDiscard`，`:595` | 沒有接回 keep 排序，依手牌原順序棄牌；低 keep 在後時選錯；MeowBeige `isolated/meow-ai.lua:126` 消費此 helper |
| `isolated/scarlet-ai.lua:60` `getGeneralDuelCard`，`:67` | `s4_txbw_yishi` 分支排序後仍取原第一張，沒有按低 use value 選牌 |

三處亦忽略排序失敗的 nil，故未知評分可變成看似有效的選牌。這是可從 producer／consumer 直接確認的語意錯配，不是 runtime 重現證據；最小修正應接回新清單並保留 unknown，而非把共用排序改成原地修改。

### F3 — P2：MeowBeige 把未知關係當成已知非友方

靜態可達情境是普通身份模式、受傷目標為 viewer 自己、傷害來源身份隱藏且尚無推測。`mode-ai.lua:451` 把自己列為 friend，而 `:450`、`:458` 至 `:474` 可以保留來源為 `"unknown"`；native `ai-runtime.cpp:631` 明確接受該關係值，不是 malformed policy。

`isolated-facades.lua:1967` 的 `isFriend` 僅對 nil 回 nil，字串 `"unknown"` 會成為 false。`isolated/meow-ai.lua:95` 至 `:100` 因而得到 help=true／source_friend=false，nil guard 無法攔截。`:72` 至 `:76` 再以 `not friend` 選擇 Spade；一般面朝上且其他條件已知時，後續 `:112` 至 `:114` 可選黑桃支付牌。

這是把四態關係當成 boolean 的語意錯配，單改 `and ... or false` 不足以修正。需在這些依賴關係確定性的分支明確檢查 `relationTo()` 的 nil／`"unknown"`，區分「沒有來源」與「來源關係未知」。本輪只有靜態可達證據，尚未執行該局面。

### F4 — P1（驗收能力）：SmartAI 保底缺少逐題可核對來源

`room-runtime.cpp:575` 在 EnableAI 下仍載入 SmartAI。coordinator 的值型詢問 `:1641`、RespondCard `:2402`、用牌決策 `:2670` 都可在 isolated 未處理、native 驗證拒絕或 runtime 出錯後走 legacy；部分原結果被覆寫，沒有相應逐題 route／native rejection／fallback 結果報表。合法的 pass／invoke false 不等於驗證拒絕。

`ai_coverage.outcomes()` 只描述 Lua 結果分類，不能證明 native 已接受，亦不能覆蓋 native 拒絕後的 fallback。現有 headless runner 能檢查對局結果及退出，但沒有 isolated fallback gate。因此本輪不以「跑一局沒報錯」替代獨立驗收，也不自行新增原生觀測修正。

## 已接線與尚未覆蓋

| 層／套件 | 已核對現況 | 保留的邊界 |
|---|---|---|
| 共用層 | 7 core；值型詢問、卡牌規劃、retrial、strategy hooks、event intention、SmartAI helper | 函式存在不等於完整 SmartAI 等價；F2 影響共用消費端 |
| AIgeneral | 純值詢問與策略入口 | deep_seek 私有角色推論、tieba_zhili 特殊策略仍明確 unsupported |
| AnimationCardpack | 卡牌／詢問／數值入口 | Elucidator 臨時 Slash 策略等明確 unsupported |
| animecard | 卡牌策略與選項入口 | Se_Elucidator 等缺少技能效果推演 |
| animic | 多種 invoke／choice／候選入口 | tag、移牌、個別能力缺少投影；見檔內 unsupported |
| arknights | 詢問、activation、傷害 hooks | view-as 救援計數等未覆蓋 |
| assassins | 值型與主動策略入口 | 複雜 helper／tag／轉化分支仍未完成 |
| Scarlet | 多個 V2 ticket 策略、單挑、技能 hooks | F2 單挑排序偏差；仍有大量明確未覆蓋分支 |
| mcompetition | shared helper 技能名單及詢問／主動策略 | shenyi phase record 等資料未投影 |
| meow | shared helper 技能名單及詢問策略 | F2 棄牌 helper、F3 unknown，其他缺失仍 unsupported |
| keguibao | conversion 與 skill_action 策略 | instance、目標／角色等 unknown 仍須逐分支驗 |
| kexianbao | shared helper、conversion、skill_action 與選人 | 拼點／完整候選／旗標資料等仍有缺口 |
| shadow | `y_` 策略與技能名單 | y_huaiju／y_youfang／y_zhixi 等 move context 明確未投影 |
| standard | 小型 `@@lianying` handler | 只有此入口不能視為整個 standard package 完整移植 |

以上只列代表性缺口；沒有以 unsupported 數量推算覆蓋率，也沒有宣稱各檔其餘分支全對。

## 本輪釐清並同步的文件契約

- RespondCard 已接受當次 authority 發出的 V2 `card_spec`；show／pindian 限實體牌。全部 V1 view-as 仍不可宣稱完成。
- `event-intention.lua` 已有 card／choice／damage／visible-skill consumers；legacy 事件仍需逐個核對純值 ABI，不能概括全部未接或全部等價。
- `AiIsolatedScripts` 是完整覆寫。一般預設 isolated 之外，EnableHegemony 仍有 LegacyAdapted 預設，再套顯式覆寫。
- 舊 `ai-common`／room-runtime-isolation suite 已移除，不要求下一個模型新增或執行舊 fixture。
- `QSAN_AI_PROBE` 計時位於 request 建立後，可包含 SmartAI fallback，不能直接作 isolated 全流程／純策略耗時。

## 同日後續修正：F1–F3

| 項目 | 修正 |
|---|---|
| F1 | 兩個 fetcher 必要檔檢查及 CMake 資產必要清單補入 `smart-ai-functions.lua`；manifest 靜態 gate 已由 3 problems 變成 PASS（29 files／11 required／7 core／13 package handlers）。 |
| F2 | `getCard`、`askForDiscard`、`getGeneralDuelCard` 都使用排序回傳副本，保留原排序方向。`getCard`／單挑的排序未知直接發出 `ai_unsupported`，避免呼叫端把 nil 當無牌；棄牌 helper 回 nil，由 MeowBeige 明確轉成 unsupported。 |
| F3 | 新增限 MeowBeige 使用的關係讀取函式，先以 `relationTo` 檢查 nil／`unknown`。cardask 與 choice 共用的 `beige_suit` 均接入，保留已知 friend／enemy／neutral 的既有決策順序。 |
| F4 | 原生觀測能力未修改，獨立 isolated runtime 驗收仍 BLOCKED。 |

修正證據存於 `builds/isolated-ai-fixes-20261003/`，與初次驗收快照分開。manifest、3 個受影響 Lua 檔的語法樹／空白檢查、PowerShell parser、Bash `-n`（LF 副本）及 scoped `git diff --check` 均通過；獨立靜態審查指出的排序 nil 呼叫端缺口已補齊，複核無新發現。

`smart-ai-functions.lua`、`scarlet-ai.lua`、`meow-ai.lua` 已逐檔回寫外部權威倉庫；寫前比對修正前雜湊，寫後 SHA-256 全部一致，結果存於 `sync.json`。H 端既有修改及 untracked 檔保留，沒有 commit／push。

本批未建置、執行 Lua、啟動對局或新增測試套件。CMake 部署清單修正尚無 configure／打包執行證據；靜態 PASS 不等於部署及玩法通過。本報告不授權額外建置、長對局或引擎除錯。
