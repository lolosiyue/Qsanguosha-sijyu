# SmartAI → isolated AI 移植遵循文件

供執行移植的模型逐步遵循。目標是用現有純值契約保留指定 SmartAI 策略；檔案能載入、註冊表有鍵、對局能跑完都不算完成。

先讀當前 `AGENTS.md`；API 以當前原始碼為準。[2026-10-03 驗收](isolated-ai-acceptance-20261003.md)是時間點紀錄。新增技能入門見[撰寫指南](isolated-ai-authoring-guide.md)，架構見[共用層](isolated-ai-common-layer.md)。

## 0. 開始前填工作卡

每批限定一個套件、數個有共同依賴的 callback，不一次搬完全部 SmartAI。

| 欄位 | 內容 |
|---|---|
| 任務 | 套件名、技能名、callback key，及明確不在本批的項目 |
| 來源／目的 | `lua/ai/<package>-ai.lua`（及技能規則所在檔，記錄雜湊）→ `lua/ai/isolated/<package>-ai.lua`；shared helper 的消費位置 |
| 權威 | 外部倉庫 `ai/<package>-ai.lua`／`ai/isolated/<package>-ai.lua` |
| 基準 | 主倉庫分支、HEAD、dirty 範圍；外部倉庫 branch／origin／status／divergence |
| 完成範圍與限制 | 靜態接線、來源同步、建置、執行期、策略等價各要求到哪一級；已授權的檢查點與時間限制，未授權的 gate 記 NOT RUN |

L 的 `lua/ai/` 被主倉庫忽略，主倉庫 `git status` 乾淨不代表 Lua 沒改。外部權威是 `H:\Program file\Game\sgs\Qsgs\working\extensions`，不是 L 的 `lua/ai/.git`；不覆蓋已有 dirty 檔。

## 1. 先列策略帳，再改碼

逐個列出原版 callback、數值表、技能名單、事件與跨詢問狀態（只掃 `ai_skill_*` 會漏掉意圖、傷害、keep/value、view-as、revises）。複雜技能先拆分支，保留原版判斷順序與預設答案。每列欄位如下，未確認的填 `UNKNOWN`：

| 原版檔／symbol／分支 | 原輸入、回傳、副作用、RNG | isolated 入口與 consumer | 必要投影／完整性 | 結果與缺口 | 證據 |
|---|---|---|---|---|---|
| 一個實際 callback 分支（不可只填「同原版」） | | dispatcher → registry → handler → normalization | 欄位及 native 產生端 | 已搬／部分／未搬，具體原因 | source／build／runtime 分列 |

`已搬` 只描述實作；`策略等價已驗` 另需對應局面證據。不以檔案數、handler 數、`ai_unsupported` 次數計算覆蓋率。

## 2. 找既有 API 與實際消費端

原生程式先用 codebase-memory-mcp（`search_graph`、`trace_path`、`get_code_snippet`）並核對實際來源；`lua/ai/` 不入圖，改用定向文字搜尋。

| 要確認什麼 | 先讀哪裡 |
|---|---|
| 預設載入及結果分流 | `isolated-bootstrap.lua` 的 `ai_isolated_core`、`ai_decide` |
| 值、可見性、unknown、答案格式 | `isolated-facades.lua` 的 getter、`AIResultValue.normalize` |
| 詢問 ABI 與 reason key | `isolated/ask-for-choice.lua` 的 `make_registry`／`legacy_call`；`ask-for-use-card.lua` |
| 估值、排序、規劃 | `isolated/decision-core.lua`；`smart-ai-functions.lua` |
| 註冊表是否真的被呼叫 | `strategy-hooks.lua` 加實際讀取該表的 consumer（只找到表宣告不算） |
| 事件／意圖 | `event-intention.lua` 的 card／choice／damage／skill consumers；`mode-ai.lua` |
| 候選與票券的發出與驗證 | `src/server/ai-decision-coordinator.cpp` 的 request 建立、`buildSpecCard`、`applyResult`、`responseCard` |
| sandbox、套件載入、路由 | `src/server/ai-runtime.cpp`、`room-runtime.cpp` |

沿用已有 helper、registry、`AIList`、`AIUsePlan`，技能名單與個別策略留在套件檔。缺必要能力時列出缺口，不新增「萬用 SmartAI 模擬器」、任意 tag 代理或平行規則引擎，也不把缺口改成保守答案。

## 3. 選對 ABI

入口索引如下，位置參數仍須讀 dispatcher。

| 任務 | isolated 入口／ABI |
|---|---|
| invoke、choice、discard、playerchosen 等值型詢問 | 無前綴 `ai_skill_*[reason](self, options, request)` |
| 回應使用牌 | `ai_skill_use[pattern](self, prompt, request)` |
| 主動技能 | `ai_skill_activate[skill_name](self, request)` |
| 卡牌策略 | `ai_card_use[name_or_class](self, card, use)`，填 `use.card`／`use.to` |
| 舊 ABI 相容註冊 | `sgs.ai_skill_*` 相容表，依 `legacy_call` 保留位置參數（不是 gameplay userdata） |
| 事件 | `ai_event_callback[event][visible_skill](self, eventPlayer, event)`，`event` 是純值 DTO |
| Yiji 意圖函式 | isolated `(self, event)`，不照搬 legacy 私有 cards 參數 |

`sgs.ai_skill_*` 與無前綴表不一定是同一個 table，不可合併。以實際 `reason`、`pattern`、大小寫及 `options.question` 為 key，不用中文顯示名稱猜。`options.context.legacy_hook` 會讓 shared default 讓位給舊 SmartAI，那是相容路徑，不算移植完成。

## 4. 每個分支做五項對照

| 項目 | 必須保留或記錄 |
|---|---|
| 條件與優先序 | 原版 if／elseif 順序、邊界值、提前 return、fallback；不合併成手牌數啟發式 |
| 排序與並列 | 排序方向、tie-break、是否改原清單；isolated `sort`／`sortBy*` 多數回傳副本，必須接回且檢查 nil |
| 隨機 | 使用現有 runtime RNG；不新增 seed、不換時鐘、不額外抽樣 |
| false／nil／空集合 | false 是已知否；nil 可能是 unknown／未處理；空集合只在完整投影下表示已知為空 |
| 跨 callback 狀態 | 按 Room＋viewer 的純值 memory／事件提交；不存 facade、userdata、舊票券或另一觀察者的私有狀態 |

```lua
-- 排序回傳副本；未知估值不可繼續用未排序的原清單
local ordered = self:sortByKeepValue(cards)
if ordered == nil then ai_unsupported("keep order is unknown", skill_key) end
cards = ordered

-- 先分辨 unknown 再判斷友方；不能用 and/or 把 nil 折成 false
local source_friend = false
if damage.from ~= nil then
    local relation = self:relationTo(damage.from)
    if relation == nil or relation == "unknown" then
        ai_unsupported("source relation is unknown", skill_key)
    end
    source_friend = relation == "friend"
end
```

## 5. 可見資料與授權邊界

只讀當次 viewer 的快照；先驗欄位存在，再驗完整性，最後做策略判斷。未知身份不是敵人，未知手牌不是零張，未列完的候選不是沒有合法行動。

`isFriend()`／`isEnemy()` 只在 relation 為 nil 時回 nil，字串 `"unknown"` 也得 false。原策略需要確定關係時，先讀 `relationTo()` 排除 nil／`"unknown"`，不能以 `isFriend(...) == false` 證明已知非友方。

| 資料／動作 | 規則 |
|---|---|
| `self.player`／`self.room` | value facade；不新增 native Room／Player／Card pointer 或 userdata 通道 |
| `getCardsNum`／可見牌 | 區分已知數量、估計與真實手牌；不把原版估計換成可見張數並稱等價 |
| tag、property、事件 data | 只用已投影且有型別／可見性規則的資料；禁止任意 QVariant／tag 回查 |
| 實體牌／選人 | 回當次 offered ID／object name，遵守數量、順序、optional／compulsory |
| 主動技能 | 使用本題 skill action／activation instance，不靠技能名挑任意同名實例 |
| 轉化 | 使用 authority-issued conversion ticket；不 `Card_Parse`、`cloneCard` 或拼 legacy 虛擬牌字串 |
| `card_spec` | 保留 conversion、activation／source owner／instance、成本、revision 契約；native 重建與合法性重驗仍必要 |
| RespondCard | 支援授權 V2 conversion；show／pindian 限 offered 實體牌；V1 view-as 未全部完成 |

成本只選票券授權且可見的候選，不注入任意支付子牌 ID；不為取得票券而把 `hasIndependentAIConversion()` 無條件改 true（僅適合獨立、固定數量且不改生成牌身份或目標規則的成本）。

共用 `tryUseCard` 返回 `plan, status`，分清 `planned`、`declined`、`unsupported`：已有完整合法 action 時，可保留其他候選的 unknown 紀錄並作答；沒有可行 action 且仍有 unknown 時，才以未覆蓋結束。

## 6. 回傳值與錯誤

| 回傳／狀態 | 意義 | 完成證據？ |
|---|---|---|
| 合法 answer／use plan | 策略提出答案，待 native 驗證 | native 接受且來源確認後才算 |
| invoke `false`、可拒絕時的 `{kind="pass"}` | 已處理的否／拒絕 | 需符合原策略與詢問規則 |
| handler `nil` | dispatcher 可再找相容 handler／default；最終仍 nil 即 unhandled | 不算 isolated 已答 |
| `ai_unsupported(reason, key)` | 明確未覆蓋，由既有邊界收集 | 記錄缺口，不用 pass 掩蓋 |
| Error／非法答案 | 程式或契約失敗 | FAIL（即使 SmartAI 保底後仍完局） |
| `NotCovered` metadata | coverage debt，可與同題已知合法答案並存 | 分開報告 |

不用 `pcall` 吞錯再回 false，不覆寫 `ai_decide` 讓所有失敗變成 pass。

## 7. 事件與意圖

`event-intention.lua` 已消費卡牌意圖、選人／Yiji／技能 choice／invoke、傷害與 visible-skill callback；legacy 表項不能假定都可直接執行。逐項核對事件投影、callback ABI、可見 skill、from／to、是否重複計算、delta 全批驗證與提交。Yiji 不重建隱藏支付牌；身份推論不共用全域 `sgs.ai_role`。事件沒有逐詢問的 SmartAI fallback 保證，須在策略帳單獨記錄。

## 8. 載入、manifest 與同步

套件檔以啟用 package 名稱不分大小寫發現，登記為非 required `package_handler`，不放進核心清單。`AiIsolatedScripts` 是完整覆寫（空清單也有語意），設定後不自動補齊核心或套件。

新增共用核心檔時，同批核對 `ai_isolated_core`、manifest 的有序 core list／required entry、兩個 fetcher 的檔案存在檢查、CMake 資產必要清單，以 `python tools/ai/check-ai-runtime-manifest.py` 的結果判定。

L 修改完成後依 AGENTS 規定：列出逐檔 L→H 映射、核對 H dirty／divergence、同步後 SHA-256 核對。H 同路徑有來源不明的不同修改時，停該檔同步並報衝突。commit／push 另依授權。

## 9. 檢查點與驗收

| Gate | 最低證據 | 不足時 |
|---|---|---|
| 靜態 | 策略帳、consumer／ABI、語法、manifest、scoped diff | FAIL／PARTIAL（語法過關不是策略完成） |
| 來源同步 | 本批 L/H 逐檔雜湊、外部 status；版本／發布狀態分列 | HASH MISMATCH／未發布 |
| 建置 | 宣告檢查點後，執行已授權的受影響目標；改 SWIG 時含重新生成證據 | NOT RUN／FAIL |
| 行為 | 合法答覆、明確拒絕、必要資料未知、非法候選、同名多實例／借用、過期票等實際觸發 | 沒觸發的分支 NOT RUN |
| 獨立 isolated | 每題路由、isolated 結果、native 接受／拒絕及 fallback 可被對應核對 | 缺觀測能力 = BLOCKED；無 error log 不等於零 fallback |
| 完整局 | 授權後的 GAME_OVER、winner、client／server 乾淨退出、無 orphan、端口釋放，加 isolated 來源證據 | 逾時／fallback／crash／user-stop 都不算通過 |

Lua 通用 parser 可能不認專案 legacy 方言（如 `continue`），須區分 parser 限制與產品 Lua 錯誤。倉庫已移除測試套件，不新增 fixture／測試目標。`tools/autotest/headless_runner.py` 預設單局很長，不是 ≤60 秒 focused gate；未獲長測授權不啟動。觀測缺口不用臨時「永遠回 pass」繞過。

## 10. 停止條件與交付

API／投影不足、策略語意不明、H 同檔衝突、需要新增 native 能力、既有 gate 不能觀測結果時：先完成不依賴該缺口的授權工作，將該分支列為 BLOCKED／未覆蓋，說明缺什麼、影響哪個 callback、最小後續方案，不自行擴大為原生引擎除錯。

交付列出：修改檔、逐分支策略帳、unknown 與行為差異、各 gate 的 PASS／FAIL／NOT RUN／BLOCKED、L/H 同步與 commit／push 狀態。只做靜態時寫「靜態接線完成」，不寫「完整移植完成」。

給下一個執行模型的工作指令只需填：套件／callbacks、舊來源及規則、目標與允許修改檔、已確認的 shared APIs／consumer、已知缺口與不擴大範圍、已授權的 gate／checkpoint／時間限制。
