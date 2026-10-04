# SmartAI → isolated AI 移植遵循文件

本文件供執行移植的模型逐步遵循。目標是保留指定 SmartAI 策略，使用現有純值契約完成移植；不是讓檔案載入、註冊表有鍵或對局繼續跑就算完成。

先讀當前 `AGENTS.md`，再讀本文件。實際 API 以當前原始碼為準；[2026-10-03 驗收](isolated-ai-acceptance-20261003.md) 是有缺口的時間點紀錄，不能當成未來版本的通過證明。新增技能入門見[撰寫指南](isolated-ai-authoring-guide.md)，架構見[共用層](isolated-ai-common-layer.md)。

## 0. 開始前填工作卡

每批先限定一個套件、數個有共同依賴的 callback；不要一次聲稱搬完全部 SmartAI。

| 欄位 | 必填內容 |
|---|---|
| 任務 | 套件名、技能名、callback key，以及明確不在本批的項目 |
| 來源 | `lua/ai/<package>-ai.lua`、實際技能規則所在檔；記錄此次檔案雜湊 |
| 目的 | `lua/ai/isolated/<package>-ai.lua`；既有 shared helper 的消費位置 |
| 權威 | 外部倉庫 `ai/<package>-ai.lua`／`ai/isolated/<package>-ai.lua` 的對應路徑 |
| 基準 | 主倉庫分支、HEAD、dirty 範圍；外部倉庫 branch/origin/status/divergence |
| 完成範圍 | 靜態接線、來源同步、建置、執行期、策略等價各自要求到哪一級 |
| 執行限制 | 已授權的檢查點、目標與時間限制；沒有授權的 gate 記 NOT RUN |

L 的 `lua/ai/` 被主倉庫忽略。主倉庫 `git status` 乾淨不代表 Lua 沒有修改。外部權威是 `H:\Program file\Game\sgs\Qsgs\working\extensions`，不是 L 的 `lua/ai/.git`。不得覆蓋已有 dirty 檔或為了驗收拉取一份不同來源。

## 1. 先列策略帳，再改碼

逐個列出原版 callback、數值表、技能名單、事件與跨詢問狀態；只掃 `ai_skill_*` 會漏掉意圖、傷害、keep/value、view-as 與 revises。複雜技能先拆分支，保留原版判斷順序與預設答案。

每列都必須有下列欄位；尚未確認的欄位寫 `UNKNOWN`，不得省略：

| 原版檔／symbol／分支 | 原輸入、回傳、副作用、RNG | isolated 入口與 consumer | 必要投影／完整性 | 結果與缺口 | 證據 |
|---|---|---|---|---|---|
| 例：`SmartAI:getCard` 多張候選排序 | 排序後取第一張 | `SmartAIView:getCard` → `sortByUsePriority` | 卡片及 priority 已知 | 2026-10-03 初次驗收發現忽略排序回傳；後續已修來源，runtime 仍待驗 | 驗收 F2 與後續修正 |
| 待填：一個實際 callback 分支 | 不可只填「同原版」 | dispatcher → registry → handler → normalization | 列欄位及 native 產生端 | 已搬／部分／未搬，具體原因 | source／build／runtime 分列 |

`已搬` 只描述實作；`策略等價已驗` 必須另有對應局面證據。禁止用檔案數、handler 數、`ai_unsupported` 出現次數計算行為覆蓋率。

## 2. 找既有 API 與實際消費端

原生程式先用 codebase-memory-mcp 的 `search_graph`、`trace_path`、`get_code_snippet`；圖可能落後 dirty 檔，需核對實際來源。`lua/ai/` 不入圖時用定向文字搜尋，不把「查不到」當成 API 不存在。

| 要確認什麼 | 先讀哪裡 |
|---|---|
| 預設載入及結果分流 | `isolated-bootstrap.lua` 的 `ai_isolated_core`、`ai_decide` |
| 值、可見性、unknown、答案格式 | `isolated-facades.lua` 的相應 getter、`AIResultValue.normalize` |
| 詢問 ABI 與 reason key | `isolated/ask-for-choice.lua` 的 `make_registry`／`legacy_call`；`ask-for-use-card.lua` |
| 估值、排序、規劃 | `isolated/decision-core.lua`；`smart-ai-functions.lua` |
| 註冊表是否真的被呼叫 | `strategy-hooks.lua` 加實際讀取該表的 consumer；不能只找到表宣告 |
| 事件／意圖 | `event-intention.lua` 的 card／choice／damage／skill consumers；`mode-ai.lua` |
| 候選與票券由誰發出、如何驗證 | `src/server/ai-decision-coordinator.cpp` 的 request 建立、`buildSpecCard`、`applyResult`、`responseCard` |
| sandbox、套件載入、路由 | `src/server/ai-runtime.cpp`、`room-runtime.cpp` |

沿用已有 helper、registry、`AIList`、`AIUsePlan`。技能名單與個別策略留在套件檔。沒有必要能力時列出缺口；不要新增「萬用 SmartAI 模擬器」、任意 tag 代理、平行規則引擎，或把缺口改成保守答案。

## 3. 選對 ABI，不做名稱替換式搬運

以下是入口索引，實際位置參數仍須讀 dispatcher。

| 任務 | isolated 入口／ABI |
|---|---|
| invoke、choice、discard、playerchosen 等值型詢問 | 無前綴 `ai_skill_*[reason](self, options, request)` |
| 回應使用牌 | `ai_skill_use[pattern](self, prompt, request)` |
| 主動技能 | `ai_skill_activate[skill_name](self, request)` |
| 卡牌策略 | `ai_card_use[name_or_class](self, card, use)`，填 `use.card`／`use.to` |
| 舊 ABI 相容註冊 | `sgs.ai_skill_*` 的相容表，依 `legacy_call` 保留位置參數；不是 gameplay userdata |
| 事件 | `ai_event_callback[event][visible_skill](self, eventPlayer, event)`；`event` 是純值 DTO |
| Yiji 意圖函式 | isolated `(self, event)`，不能照搬 legacy 私有 cards 參數 |

兩張同名 registry 不一定是同一個 table。禁止把 `sgs.ai_skill_*` 和無前綴表合併。相容 ABI 只調整呼叫形式，不保證所有舊 helper 或 QVariant 操作可用。

確認實際 `reason`、`pattern`、大小寫及 `options.question`，不可用中文顯示名稱猜 key。`options.context.legacy_hook` 會讓 shared default 讓位給舊 SmartAI；這是相容路徑，不是移植完成。

## 4. 每個分支做五項對照

| 項目 | 必須保留或明確記錄的內容 |
|---|---|
| 條件與優先序 | 原版 if／elseif 順序、邊界值、提前 return、fallback；不要合併成手牌數啟發式 |
| 排序與並列 | 排序方向、tie-break、是否改原清單；isolated `sort`／`sortBy*` 多數回傳副本，必須接回且檢查 nil |
| 隨機 | 使用現有 runtime RNG；不新增 seed、不換成時鐘、不額外抽樣；若共同算法不同，不能宣稱完全相等 |
| false／nil／空集合 | false 是已知否；nil 可能是 unknown／未處理；空集合只在完整投影下表示已知為空 |
| 跨 callback 狀態 | 按 Room＋viewer 的純值 memory／事件提交；不存 facade、userdata、舊票券、另一觀察者私有狀態 |

排序的最低限度正確寫法：

```lua
local ordered = self:sortByKeepValue(cards)
-- 排序回傳副本；未知估值不可繼續使用未排序的原清單。
if ordered == nil then ai_unsupported("keep order is unknown", skill_key) end
cards = ordered
```

保留 unknown 的關係判斷：

```lua
local source_friend = false
if damage.from ~= nil then
    local relation = self:relationTo(damage.from)
    -- 先分辨未知，再判斷友方；不能用 and/or 把 nil 折成 false。
    if relation == nil or relation == "unknown" then
        ai_unsupported("source relation is unknown", skill_key)
    end
    source_friend = relation == "friend"
end
```

這些是局部寫法示例，`cards`／`damage`／`skill_key` 必須由該 handler 的已驗證輸入提供。不要直接新增示例技能。

## 5. 可見資料與授權邊界

只讀當次 viewer 的快照。先驗欄位存在，再驗完整性，最後做策略判斷。未知身份不是敵人；未知手牌不是零張；未列完候選不是沒有合法行動。

特別注意當前 `isFriend()`／`isEnemy()` 只在 relation 為 nil 時回 nil；字串 `"unknown"` 也會得到 false。若原策略需要確定關係，必須先讀 `relationTo()` 並排除 nil／`"unknown"`，不能以 `isFriend(...) == false` 證明已知非友方。見驗收 F3；不要只修 `and/or` 語法。

| 資料／動作 | 規則 |
|---|---|
| `self.player`／`self.room` | value facade；不能新增 native Room／Player／Card pointer 或 userdata 通道 |
| `getCardsNum`／可見牌 | 區分已知數量、估計與全部真實手牌；不得悄悄把原版估計換成可見張數並稱等價 |
| tag、property、事件 data | 只用已投影且有型別／可見性規則的資料；禁止任意 QVariant/tag 回查 |
| 實體牌／選人 | 回當次 offered ID／object name，遵守數量、順序、optional／compulsory |
| 主動技能 | 使用本題 skill action／activation instance，不能只靠技能名挑任意同名實例 |
| 轉化 | 使用 authority-issued conversion ticket；不 `Card_Parse`、`cloneCard` 或拼 legacy 虛擬牌字串 |
| `card_spec` | 保留 conversion、activation/source owner/instance、成本、revision 的契約；native 重建與合法性重驗仍必要 |
| RespondCard | 已支援授權 V2 conversion；show／pindian 仍是 offered 實體牌限定；不代表全部 V1 view-as 已完成 |

成本只選票券授權且可見的候選，不注入任意支付子牌 ID。不要為了得到票券而把 `hasIndependentAIConversion()` 無條件改成 true；它只適合真正獨立、固定數量且不改生成牌身份或目標規則的成本。

共用 `tryUseCard` 返回 `plan, status`；分清 `planned`、`declined`、`unsupported`。有已知完整合法 action 時，可以保留其他候選的 unknown 紀錄並回答；沒有可行 action 且仍有 unknown 時，才以未覆蓋結束。不能把整題所有未知清掉，也不能因旁支未知丟掉已知合法答案。

## 6. 回傳值與錯誤不能互換

| 回傳／狀態 | 意義 | 是否可當完成證據 |
|---|---|---|
| 合法 answer／use plan | 策略提出答案，尚待 native 驗證 | native 接受且來源已確認後，才是該分支證據 |
| invoke `false`、允許拒絕時的 `{kind="pass"}` | 已處理的否／拒絕 | 需符合原策略與詢問規則 |
| handler `nil` | dispatcher 可再找相容 handler／default；最終仍 nil 是 unhandled | 不能算 isolated 已答 |
| `ai_unsupported(reason, key)` | 明確未覆蓋；由既有邊界收集 | 記錄缺口，不用 pass 掩蓋 |
| Error／非法答案 | 程式或契約失敗 | FAIL，即使 SmartAI 保底後仍完局 |
| `NotCovered` metadata | coverage debt；可能與同題已知合法答案並存 | 分開報告，不能等同整題必然 fallback |

不要用 `pcall` 吞掉錯誤再回 false，也不要覆寫 `ai_decide` 讓所有失敗變成 pass。

## 7. 事件與意圖要另外驗

`event-intention.lua` 已消費卡牌意圖、選人／Yiji／技能 choice/invoke、傷害以及 visible-skill callback。不能再概括成「所有 legacy intention 都未接線」，也不能假定全部 legacy 表項都能直接執行。

逐項核對事件投影、callback ABI、可見 skill、from/to、是否重複計算、delta 全批驗證與提交。Yiji 不得重建隱藏支付牌；身份推論不得共用全域 `sgs.ai_role`。事件沒有同等的逐詢問 SmartAI fallback 保證，必須在策略帳單獨記錄。

## 8. 載入、manifest 與同步

套件檔以啟用 package 名稱不分大小寫發現，登記為非 required `package_handler`；不要塞進核心清單。`AiIsolatedScripts` 是完整覆寫，設定後不自動補齊核心或套件；空清單也有語意。

新增真正共用核心檔時，同批核對：`ai_isolated_core`、manifest 的有序 core list／required entry、兩個 fetcher 的檔案存在檢查、CMake 資產必要清單。以目前 checker 的實際結果判定；只改 manifest 不足以完成部署。

L 修改完成後按 AGENTS 規定列出逐檔 L→H 映射、先核對 H dirty/divergence、保留既有工作，再作授權範圍內的同步與 SHA-256 核對。若 H 同路徑有不同且來源不明的修改，停該檔同步並報衝突；不能覆蓋來取得相等。不得複製 `.git`、logs/data/temp、`.bak`，不得強制加入主倉庫；commit／push 另依授權。

## 9. 檢查點與驗收

| Gate | 最低證據 | 不足時的結論 |
|---|---|---|
| 靜態 | 策略帳、consumer/ABI、語法、manifest、scoped diff | FAIL 或 PARTIAL；語法過關不是策略完成 |
| 來源同步 | 本批 L/H 逐檔雜湊、外部 status；版本／發布狀態分列 | HASH MISMATCH／未發布，不稱可重現部署 |
| 建置 | 宣告檢查點後執行已授權的受影響目標；改 SWIG 時含重新生成證據 | NOT RUN／FAIL；不得挪用舊執行檔 |
| 行為 | 合法答覆、明確拒絕、必要資料未知、非法候選、同名多實例／借用、過期票等實際觸發 | 沒觸發的分支 NOT RUN；不新增測試套件填數 |
| 獨立 isolated | 每題路由、isolated 結果、native 接受／拒絕及 fallback 可被對應核對 | 缺觀測能力是 BLOCKED；無 error log 不等於零 fallback |
| 完整局 | 授權後的 GAME_OVER、winner、client/server 乾淨退出、無 orphan、端口釋放，加上 isolated 來源證據 | 逾時／fallback／crash／user-stop 都不是完整通過 |

可用靜態入口：`python tools/ai/check-ai-runtime-manifest.py`、本批 `git diff --check`。Lua 通用 parser 可能不認專案 legacy 方言（如 `continue`）；必須區分 parser 限制與產品 Lua 錯誤。

倉庫已移除舊測試套件，不新增 fixture／測試目標／契約測試文件。`tools/autotest/headless_runner.py` 預設單局很長，不能當成 ≤60 秒 focused gate；未取得長測授權不得啟動。現有 `QSAN_AI_PROBE` 計時不包含 request 建立且可包含 legacy fallback，不能作 isolated 純策略效能證據。驗收觀測缺口不得用臨時「永遠回 pass」或新測試套件繞過。

## 10. 停止條件與交付格式

API／投影不足、策略語意不明、H 同檔衝突、需要新增 native 能力、既有 gate 不能觀測結果時：先完成不依賴該缺口的授權工作，將該分支列為 BLOCKED／未覆蓋。說明缺什麼、影響哪個 callback、最小後續方案；不要自行擴大為原生引擎除錯或反覆重跑對局。

交付必須列出修改檔、逐分支策略帳、unknown 與行為差異、各 gate 的 PASS/FAIL/NOT RUN/BLOCKED、L/H 同步與 commit/push 狀態。只做靜態時寫「靜態接線完成」，禁止寫「完整移植完成」。

給下一個執行模型的工作指令模板：

```text
依 AGENTS.md 與 docs/isolated-ai-migration-playbook.md，移植以下有限範圍：
套件／callbacks：<填寫>
舊來源及規則：<填寫>
目標及允許修改檔：<填寫>
已確認 shared APIs／consumer：<填寫>
已知缺口及不准擴大範圍：<填寫>
已授權 gate、checkpoint、時間限制：<填寫>
先交逐分支策略帳，再逐項實作；unknown 不准改 pass，排序必須接回傳值。
不得假定註冊等於消費、完局等於 isolated 通過；不用新測試套件。
完成後按 gate 分列證據及 L/H 同步結果，保留所有不相關 dirty 工作。
```
