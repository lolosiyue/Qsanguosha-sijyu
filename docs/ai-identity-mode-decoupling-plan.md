# AI 身份、陣營與身份明示解耦

狀態：已完成本批次程式與契約測試原始碼；尚未建置或執行測試。

## 範圍

| 項目 | 實作方向 |
|---|---|
| SmartAI | 把身份／模式分支的共用決策入口接到 hook；未註冊的舊模式沿用既有策略 |
| 每房間架構 | hook 註冊表、每位觀察者的推測狀態及快取屬於該 Room 的 Lua VM |
| AIWorldView | 延續新版純值輸入邊界，把觀察者可見資料與 hook 結果送進隔離 AI |
| 身份明示 | PlayerStateService 管理全場明示與個別可見；通知只傳遞狀態 |
| 不在本批次 | 村民玩法、通用身份機率算法、全面改寫技能 AI、重構 C++ 劇本敵友規則、C++ 動態陣營系統 |

本文件取代早期較大的計劃；頂層 teams 是 Lua 模式 AI 的簡易設定，沒有新增 GameModeStruct 的 C++ 陣營政策或更改勝負／獎懲。

## 模式 hook

```lua
-- 只示意介面；role_a / role_b 需先註冊為實際身份。
createMode{
    name = "模式名稱", class = "mode_key", roles = "AABB",
    teams = { side_a = {"role_a"}, side_b = {"role_b"} },
    ai = {
        relation = function(context, fromId, toId, state)
            -- nil 使用 teams 預設；unknown 明確保留未知。
            return nil
        end,
    },
}
```

也可由獨立 AI 腳本呼叫 `sgs.registerModeAI(modeId, spec)`；直接註冊時將 teams 放在 spec。

### 模式預設與身份覆寫

`createMode.roles` 是遊戲身份配置；`createMode.ai.roles` 是該模式按 **AI 觀察者自身身份**
選用的策略表，兩者用途不同。同一身份可以在不同模式註冊不同策略。

```lua
createMode{
    name = "模式名稱", class = "mode_key", roles = "ZCFFVN",
    ai = {
        -- 模式共用預設；由模式作者提供完整判斷。
        objective = modeObjective,
        onIntention = modeIntention,
        gameProcess = modeGameProcess,
        roles = {
            villager = {
                objective = villagerObjective,
                onIntention = villagerIntention,
            },
            renegade = { objective = renegadeObjective },
        },
    },
}
```

以上函式名稱均為模式作者自行定義的函式，並非引擎內建。每個 hook 分別選擇：
身份專屬 hook 存在就呼叫它，否則呼叫模式共用 hook。角色 hook 回傳 nil 使用該 hook
的內建預設，不再呼叫模式版本；`onIntention` 也只執行所選版本一次。需要共用推測時，
可讓各身份的 hook 明確呼叫同一個輔助函式。

| 判斷 | 優先順序 |
|---|---|
| 當前觀察者對目標的關係 | 自身／共同控制者保護 → 明確 relation → 可見 teams 映射 → 有效 objective 的正負 → unknown |
| 評分推導 | 小於 0 為 friend、大於 0 為 enemy、等於 0 為 neutral |
| 其他玩家對目標的關係 | 自身／共同控制者保護 → relation → 可見 teams 映射 → unknown；不挪用觀察者評分 |
| relation 的 nil | 允許繼續使用 teams／評分；明確 unknown 或錯誤則不往下推導 |
| objective 的錯誤／非有限值／超出範圍 | 評分 0，不憑這個無效結果建立敵友 |

註冊表會複製 mode／roles 的 hook 定義；重新 `registerModeAI` 是整份替換，並清空
該模式各觀察者的推測狀態。未註冊的舊四身份模式仍沿用舊演算法；已註冊模式中未提供
策略的身份使用模式預設或安全預設，不會偷偷混用全域四身份推測。

### 目標身份配對與局勢輸入

模式可用 `ai.objectiveByRole` 設定共用配對，或用
`ai.roles.<觀察者身份>.objectiveByRole` 設定該觀察者身份的配對。鍵是目標的可見／推測
身份，值可為 -5～5 分數或 `function(context, targetId, state, process)`；不是目標的秘密身份。

```lua
ai = {
    objectiveByRole = { role_a = -2 },
    roles = {
        villager = {
            objectiveByRole = {
                role_b = 5,
                role_c = function(context, targetId, state, process)
                    return process.value > 0 and -1 or 3
                end,
            },
        },
    },
}
```

範例身份都須先註冊。配對缺少時，先使用同層 `objective` 函式；身份層沒有此函式則
沿用模式的配對／objective。明確配對函式回傳 nil 時不繼續找模式配對，改用既有
teams／relation 的預設評分。全無配對、評分或關係設定時，維持 unknown。
配對仍受明確 relation／teams 與控制鏈保護的既有優先順序約束。

每次評估只呼叫一次所選 `gameProcess`，驗證後將 `{value=數值, label=字串}` 作為
objective／配對函式的第四參數；舊三參數函式保持相容。模式或身份覆寫 gameProcess 後，
目標評分使用同一結果，不再由身份策略私下呼叫另一份局勢算法。

### 意圖規則註冊（不列舉身份分支）

`ai.intentions` 設定模式預設；`ai.roles.<身份>.intentions` 覆寫該觀察者身份的整份規則。
以下是 `createMode.ai` 表中的片段，`new_role` 須先透過 `addRoleMapping` 註冊：

```lua
roles = {
    villager = {
        intentions = {
            rules = {
                {target = "new_role", update = function(context, event, values, state)
                    -- 這是此模式定義的證據，不是通用 AI 對新身份的假設。
                    values.suspicion = (values.suspicion or 0) + event.level
                end},
            },
            infer = {
                {role = "renegade", test = function(context, event, values, state)
                    return (values.suspicion or 0) > 20
                end},
            },
        },
    },
}
```

| 設定 | 語意 |
|---|---|
| `ignoreActors = {"role_name", ...}` | 忽略指定可見／推測行為者身份；預設空 |
| `rules = {{actor=..., target=..., update=...}, ...}` | actor／target 分別比對行為者／目標身份，省略的欄位匹配所有身份；多條符合時依陣列順序執行 |
| `infer = {{role=..., test=...}, ...}` | 證據更新後，第一個 test 回傳 true 的候選身份成為推測；沒有符合時清除該玩家推測 |
| `event` | 純值 `{from, to, actor_role, target_role, level}`；from／to 是 object_name |
| `values` | 該觀察者對行為者的證據副本，可使用自訂字串鍵與有限數值，缺值由規則使用 `or 0` 處理 |

核心只做資料驗證、配對、依序呼叫與儲存，不知道主忠反內／村民等身份。身份配對取
當前觀察者可見身份，否則取此觀察者原有推測，沒有推測則為 `unknown`。所有配對依
事件開始時的身份執行，不會被同一事件剛產生的推測改變。沒有任何配對時不更新證據或推測。
已知身份不產生推測覆蓋。更新／推測失敗或產生非有限證據時，不提交這次證據副本。
`context`、`event`、`state` 在規則回呼內視為唯讀，只有 `values` 可修改。

規則於註冊時複製；新增／改規則後需重新註冊該模式。空的 `intentions = {}` 可明確
停用該身份的模式預設意圖更新。同一層不可同時設定 `intentions` 與 `onIntention`；
後者保留作完整自訂事件回呼。身份層與模式層沿用既有的 hook 選擇優先順序，不自動合併規則。

### 村民策略參考

村民參考拆成兩層：`ai/role-policies/villager.lua` 只提供 `compareStrength` 等純值算法，
不含任何身份名稱、陣營對照或意圖規則；`ai/mode-policies/villager.lua` 提供模式 AI 設定，
集中宣告目標配對、身份對局勢的貢獻及意圖規則。使用時載入模式設定：

```lua
local modeAI = dofile("lua/ai/mode-policies/villager.lua")
-- new_role 須先註冊；只在模式設定補新身份的配對與局勢貢獻。
modeAI.roles.villager.objectiveByRole.new_role = -2
modeAI.strengthByRole.new_role = {weight = -1}
-- 將 modeAI 交給 createMode 的 ai 欄位或 registerModeAI(modeId, modeAI)。
```

這是可選的 AI 策略，不註冊身份、不建立模式、不改勝負／擊殺獎懲。
它把 TODO/human 村民評分的主體抽離：敵視推測內奸、隨局勢選邊、比較其他村民的
體力＋手牌、均勢觀望。objective 正負會經共用接點建立敵友，出牌沿用現有卡牌 AI。
意圖證據保存在 `state.role_values`，推測保存在 `state.inferred_roles`，均屬 Room VM
內的當前觀察者；可見身份永遠優先於推測，不改共享 `sgs.ai_role`。
模式設定提供 `intentions.rules`／`intentions.infer`，不再定義列舉身份的
`policy.onIntention`。攻擊村民、援助另一陣營及各推測門檻均為可更改的註冊項目。
模式的 `strengthByRole` 是範例局勢函式使用的資料：weight 是每位存活玩家
`3 + hp` 的帶正負權重，anchor 指定局勢判斷的關鍵身份；它不是引擎隊伍或勝負設定。
沒有設定的新身份不自動加入任一方。其他觀察者身份仍需提供模式預設／專屬策略，
本範例只示範村民，不宣稱已完成主忠反內的全部策略。

配對／意圖註冊表會複製，但 Lua 函式保留其 closure；模式資料須在註冊前設定完成，
更改後重新註冊以清除舊推測與快取。每次 dofile 建立獨立的模式設定表。

與 TODO/human 不宣稱逐值等價：本範例局勢只以可見／推測身份的存活玩家數與體力估算，
不搬入原生 getDefense、秘密身份剩餘名額、未經可見性檢查的 getRole 或隨機意圖倍率。
沒有證據的玩家保持未知，不因分數都為零就推定為村民。這些差異是為了符合 AIWorldView
邊界；不是已通過對局驗收或已部署的完整村民模式。

| hook | 回傳與用途 |
|---|---|
| relation(context, fromId, toId, state) | friend / enemy / neutral / unknown；nil 使用陣營／觀察者評分預設，支援非對稱關係 |
| objective(context, targetId, state, process) | -5 至 5 的有限數值；nil 沿用關係的預設評分；process 是本次 gameProcess 的驗證結果 |
| objectiveByRole | 依目標的可見／推測身份選擇評分函式或常數；支援模式共用與觀察者身份覆寫 |
| rolePredictable(context, state) | 明確 boolean；nil 依服務提供的身份可見性判斷 |
| gameProcess(context, state) | 有限數值、可選局勢字串；預設 0 / neutral |
| onIntention(context, fromId, toId, level, state) | 更新該觀察者的推測狀態；沒有 hook 就不跑四身份推測 |
| intentions | 宣告式意圖規則；註冊時轉成同一 onIntention 分派入口，核心不含具體身份規則 |

- 同一 teams 表內可把多種身份歸為同隊；相同非空陣營友善、不同非空陣營敵對。
- 隱藏身份即使有陣營映射也不拿來判斷。特殊聯盟、動態立場由 relation 讀公開標記等 context 資料判定。
- state 是 Room VM 中每位觀察者獨立的 AI 推測資料；查詢 hook 視其為唯讀，由 onIntention 更新。
- context 只有 primitive tables，不傳 Room/Player/Card userdata。擴展必須只使用提供的 context，不捕捉 gameplay 物件讀取秘密或在查詢期間變更遊戲。
- 缺少規則的新身份、非法結果與回呼失敗保留未知；不將中立／未知自動列入 enemies。
- 控制鏈友方保護優先於模式的敵對／評分結果。
- 舊 SmartAI 的共用入口包含 objectiveLevel、isFriend/isEnemy、getFriends/getEnemies、updatePlayers、compareRoleEvaluation、adjustAIRole，以及全域相容入口的身份／意圖／局勢判斷。
- 舊模式專屬 if/else 留作 fallback；技能自身硬寫的身份策略沒有批量改寫。

## Room service 與恢復

| API | 語意 |
|---|---|
| isRoleRevealed(player) | 唯一 role_shown 狀態：身份牌是否全場明示 |
| canSeeRole(viewer, target) | 本人、全場明示或目前身份的個別授權 |
| revealRole(player) | 先登記明示與 revision，再發送 role_shown / role |
| revealRoleTo(viewer, target) | 記錄個別可見授權後同步，不全場明示 |
| syncRole(viewer, target) | 唯讀補送；不建立授權、不推進 revision |

- 私下授權使用 JSON-safe 玩家動態屬性 `_role_visibility`，由現有快照的 dynamicProperties 保存；身份變更清除舊授權，包括 A→B→A。
- 原本應公開身份的初始化、死亡等入口改走 revealRole；Room 的舊 broadcastProperty(role) 保留相容轉接，底層傳輸不再改 role_shown。
- 舊 notifyProperty(role) 在實際送出目前身份時登記該觀察者授權；重連／接管補送使用 syncRole。
- takeover 直接還原 role 與 roleShown，不經公開屬性設定；不再以主公／死亡／模式名稱推定補送時是否公開。
- 保留既有協議及 Player.hasShownRole；新的 AI 可見性由服務提供。

## AIWorldView 與隔離 facade

新增 mode_id、custom_roles、mode_policy；玩家新增 role_revealed、role_visible、controller。
mode_policy 保存 managed、predictable、觀察者視角的關係表、目標評分及局勢結果。
C++ 在所屬 Room 的 gameplay VM 評估 hook，再驗證、複製純值到快照；隔離 VM 不取得另一個 VM 的 closure。
SmartAI 的純值快取以 Room stateRevision 與 hook／推測狀態 generation 失效；AI 推測資料不屬於遊戲接管狀態，沿用重建 AI 的既有規則。
SmartAIView 增加敵友／評分／可預測性與 friends/enemies 查詢，沒有新模式策略時保持未覆蓋，不假裝知道敵友。

## 交付與驗證檢查點

| 項目 | 位置／狀態 |
|---|---|
| 主倉庫 | L:\finaldebug\qsan-ai-identity，codex/ai-identity-mode-decoupling，起點 2759c82 |
| Lua 倉庫 | L:\finaldebug\qsan-ai-identity-lua，同名分支，起點 0ae04e4 |
| 純 Lua 契約 | tests/lua/mode-ai-contract.lua：多身份同隊、隱藏身份、非對稱關係、未知／錯誤、不同觀察者狀態、隔離 facade |
| 本批新增契約原始碼 | 模式／身份覆寫、缺 hook 繼承、評分符號推導、明確關係優先、第三方關係未知、重新註冊清狀態、兩 VM 狀態隔離、村民推測與明示覆蓋 |
| 意圖規則新增契約原始碼 | 新身份規則、模式隔離、身份空規則覆寫、複製註冊定義、未知身份不洩漏、依序累加、候選優先順序、錯誤不提交 |
| 目標配對新增契約原始碼 | 模式／身份配對繼承、nil／0 區分、未知身份保留未知、複製配對定義、局勢只評估一次、覆寫局勢傳到評分、新身份只改模式設定 |
| 原生契約 | 既有 room-runtime-isolation-test 加入服務 revision、私下授權失效及兩 Room 同模式註冊隔離 |
| 靜態檢查 | 檢查差異及 diff --check；不等同於建置或執行通過 |
| 待授權 gate | 正常 SWIG 生成／受影響 target 建置，再執行約定範圍 focused checks |
| 未執行 gate | 本地 CTest、完整對局、跨平台矩陣與 CI；長時驗證交遠端或使用者 |

不得覆寫原工作樹其他 agent 的未提交工作。外部 Lua 回寫僅限本次檔案，先比較原始基準與目前權威檔，再 SHA-256 核對。
主倉庫與外部 Lua 使用各自的功能分支及互相連結的草稿 PR。

六個 Lua 檔（包含 `ai/role-policies/villager.lua` 與 `ai/mode-policies/villager.lua`）
同步至外部 H 權威倉庫及隔離工作樹；同步時先核對前次雜湊，保留其他 agent 修改。雜湊清單在
`builds/ai-identity-source-sync.json`。H 上其他 agent 的修改保留。新 Lua 需與本批次 C++／SWIG
一併部署；原 L 執行期未更新，隔離工作樹尚未準備完整遊戲素材或建置輸出。
