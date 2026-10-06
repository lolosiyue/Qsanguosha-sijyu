# isolated AI／SmartAI 現有移植驗收（2026-10-03）

時間點紀錄：F1–F3 已完成來源修正，F4 仍 BLOCKED，執行期與完整對局 NOT RUN。原始碼已具備純值 VM、共用策略、套件 handler、事件及 V2 轉化接線。後續移植流程見[遵循文件](isolated-ai-migration-playbook.md)。

初次驗收只盤點、不改碼；使用者隨後授權修正 F1–F3（沒有 commit／push，主倉庫 `debug` 與外部 AI 倉庫既有 dirty 工作均保留）。靜態證據在 `builds/isolated-ai-acceptance-20261003/`（`static-audit.json`、`source-baseline.json`）與 `builds/isolated-ai-fixes-20261003/`，皆為忽略的本機產物。

## Gate 結果

| Gate | 結果 | 界線 |
|---|---|---|
| manifest 一致性 | PASS（修正後；初次 FAIL，3 個問題） | 29 files／11 required／7 core／13 package handlers |
| isolated 語法樹 | PASS（靜態） | 24 檔 tree-sitter 無 parse error，不等於產品 Lua load 成功 |
| legacy 語法樹 | 未能完整驗證 | 9 檔有 parser error；來源使用專案 Lua 方言 |
| L/H 來源一致性 | PASS | 40 檔雜湊／位元組一致；修正後 3 個 Lua 檔已回寫外部權威倉庫並核對 SHA-256 |
| 外部發布 | 未驗證 | 本機 main ahead 2／behind 0；沒有 fetch／commit／push |
| 策略等價 | 部分 | F2、F3 已修，其餘未逐技能驗證 |
| 建置／產品 Lua 載入 | NOT RUN | CMake 部署清單修正尚無 configure／打包證據 |
| 零 fallback 的 isolated runtime | BLOCKED | 見 F4 |
| 完整對局、GUI、跨平台、CI | NOT RUN | |

## 已修缺陷

| 項目 | 問題 | 修正 |
|---|---|---|
| F1（P1） | 新增核心 `smart-ai-functions.lua` 已在 bootstrap／manifest，但兩個 fetcher 的必要檔檢查與 CMake `QSAN_ASSET_REQUIRED` 未列入 | 三處補入 |
| F2（P2） | `getCard`、`askForDiscard`、`getGeneralDuelCard` 沒有接回 `sortBy*` 回傳的副本（亦忽略未知評分的 nil） | 全部使用排序副本；未知時 `ai_unsupported`（`askForDiscard` 回 nil，由 MeowBeige 轉成 unsupported） |
| F3（P2） | `isFriend()` 對字串 `"unknown"` 回 false，MeowBeige 因而把未知來源關係當成已知非友方 | 新增 MeowBeige 專用關係讀取函式，先以 `relationTo` 檢查 nil／`unknown` |

F2、F3 的通用寫法已併入[遵循文件](isolated-ai-migration-playbook.md)第 4、5 節。

## 未解：F4（P1，驗收能力）

`room-runtime.cpp:575` 在 EnableAI 下仍載入 SmartAI。coordinator 的值型詢問（`:1641`）、RespondCard（`:2402`）、用牌決策（`:2670`）都可在 isolated 未處理、native 驗證拒絕或 runtime 出錯後走 legacy，且部分原結果被覆寫，沒有逐題 route／native rejection／fallback 報表。`ai_coverage.outcomes()` 只描述 Lua 結果分類；現有 headless runner 只檢查對局結果及退出。因此「跑一局沒報錯」不能證明零 fallback；補觀測能力屬原生變更，需另行授權。

## 代表性覆蓋缺口

函式存在不等於完整 SmartAI 等價；下表不用 unsupported 數量推算覆蓋率。

| 層／套件 | 已接線 | 主要缺口 |
|---|---|---|
| 共用層 | 7 core：值型詢問、卡牌規劃、retrial、strategy hooks、event intention、SmartAI helper | — |
| AIgeneral | 純值詢問與策略入口 | deep_seek 私有角色推論、tieba_zhili 特殊策略 |
| AnimationCardpack／animecard | 卡牌／詢問／數值入口 | Elucidator 臨時 Slash 策略、技能效果推演 |
| animic | 多種 invoke／choice／候選入口 | tag、移牌、個別能力投影 |
| arknights | 詢問、activation、傷害 hooks | view-as 救援計數 |
| assassins | 值型與主動策略入口 | 複雜 helper／tag／轉化分支 |
| Scarlet | 多個 V2 ticket 策略、單挑、技能 hooks | 大量明確未覆蓋分支 |
| mcompetition | shared helper 技能名單及詢問／主動策略 | shenyi phase record 等資料 |
| meow | shared helper 技能名單及詢問策略 | 其餘缺失仍 unsupported |
| keguibao | conversion 與 skill_action 策略 | instance、目標／角色等 unknown 須逐分支驗 |
| kexianbao | shared helper、conversion、skill_action、選人 | 拼點、完整候選、旗標資料 |
| shadow | `y_` 策略與技能名單 | y_huaiju／y_youfang／y_zhixi 的 move context |
| standard | 小型 `@@lianying` handler | 其餘 standard package |

## 文件契約

- RespondCard 接受當次 authority 發出的 V2 `card_spec`；show／pindian 限實體牌；V1 view-as 未全部完成。
- `event-intention.lua` 已有 card／choice／damage／visible-skill consumers；legacy 事件須逐個核對純值 ABI。
- `AiIsolatedScripts` 是完整覆寫；一般預設 isolated，EnableHegemony 另有 LegacyAdapted 預設，再套顯式覆寫。
- 舊 `ai-common`／room-runtime-isolation suite 已移除，不新增或執行舊 fixture。
- `QSAN_AI_PROBE` 計時位於 request 建立後，可含 SmartAI fallback，不能作 isolated 純策略耗時。
