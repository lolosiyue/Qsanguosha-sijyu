# Lua SkillV2 複合技能組裝器

`require("lua.skill_assembly")` 把一組原生 V2 effect 定義成一個可見技能，
並註冊其他 effect 的 `SourceHelper` 關係。它不新增事件派發器、技能實例、
使用次數表或 Lua 狀態倉庫；沒有 DSL parser，也不執行文字形式的規則。

## 完整例子

[可載入的範例套件](../lua/skill_assembly_example.lua) 定義「受到傷害後摸 1 張牌，
手牌上限 +1」。由擴充載入器 opt-in 載入並把回傳的 Package 交給既有套件載入流程；
核心不預設載入這個示範武將。每個 `Damaged` 事件觸發一次，不按傷害點數重複。
最大手牌數仍以引擎當下 HP／其他修正計算，範例只增加每份有效實例的貢獻。

```lua
local Assembly = require("lua.skill_assembly")
local skill = Assembly.create {
    name = "example_resilience",
    effects = {
        {
            id = "draw", kind = "trigger", events = sgs.Damaged,
            frequency = sgs.Skill_Compulsory, base_amount = 1,
            can_trigger = function(self, event, room, player, data)
                if player and player:isAlive() and player:hasSkill(self:objectName()) then
                    return self:objectName(), player
                end
                return ""
            end,
            on_effect = function(self, event, room, player, ctx)
                local n = self:getEffectiveAmount(ctx)
                if n > 0 then player:drawCards(n, "example_resilience") end
                return false
            end,
        },
        {
            id = "hand_limit", kind = "maxcards", base_amount = 1,
            correct_func = function(self, ctx) return true end,
        },
    },
}
skill:register(extension, general)
```

產生的定義是 `example_resilience`（TriggerSkillV2）與
`#example_resilience__hand_limit`（MaxCardsSkillV2）。`register` 一次註冊所有定義，
呼叫 `extension:insertRelatedSkills`，再以名稱綁定 general。
必須在 Package 回傳給引擎之前呼叫，與手寫 package 的關聯註冊時機相同。
可省略 general，以便之後動態取得技能；相同 assembly/package 重複 register 不重複寫關係。
撞到任何既有定義名稱會報錯，不借用未知定義。不能跨 Package 重複註冊同一 assembly。

## Authoring API

| 介面 | 結果 |
| --- | --- |
| `Assembly.new {name=..., effects=...}` | 可增加 effect 的 builder；effects 可省略 |
| `builder:addEffect(kind, {id=..., ...})` | 回傳 builder，可鏈接；建立後禁止新增 |
| `builder:createSkill()` | 建立並回傳可見的原生根 Skill；重複呼叫回傳相同定義 |
| `Assembly.create {name=..., effects={...}}` | 立即建立，回傳 assembly，仍可 register／查 effect |
| `assembly:register(package[, general])` | 註冊所有定義和關係，回傳可見根 Skill |
| `assembly:effectName(id)` | 原生定義名稱；未知 id 報錯 |
| `assembly:effectRef(owner, rootInstanceID, id)` | live root 或其指定直接 helper 的 SkillInstanceRef；缺失／模糊回 nil |

`name` 與 effect `id` 使用 ASCII 英數字／底線；effects 必須是 dense array。
每個 effect 都必須有明確且不重複的 id，不接受自行覆蓋 name。
根只容納 `name` 與 `effects`，各 effect 的原生設定放在 effect 內。

| kind | 原生 factory |
| --- | --- |
| `trigger` | `sgs.CreateTriggerSkillV2` |
| `active`、`viewas` | `sgs.CreateViewAsSkillV2` |
| `distance` | `sgs.CreateDistanceSkillV2` |
| `maxcards` | `sgs.CreateMaxCardsSkillV2` |
| `targetmod` | `sgs.CreateTargetModSkillV2` |
| `atkrange` | `sgs.CreateAttackRangeSkillV2` |

一組最多一個 active/viewas，該效果優先作可見根；沒有則取第一個 trigger，
再沒有則取第一個 correction。其餘名稱為 `#<name>__<id>`。
在根選擇不變時，重新排列效果不改 helper 名稱。
新增 active 或更改根 effect 會改變原生 identity，不能當作存檔／回放相容的重新排序。

組裝器拷貝 spec 的 table，保留 callback function 與原生 userdata，移除 `id/kind` 後
送到既有 factory。`events`、`on_record`、`on_cost`、`on_pay`、`on_effect`、
`base_amount`、`limit_scope`、`max_usage_limit`、`get_usage_ref`，以及 active 的
`history_key`、`get_amount_ref` 都沿用原生契約。不會把各效果的 scope/history
自動合併成一份，也不自動把 trigger callback 的 self 改成根 skill。
若想共享原生使用紀錄，作者明確提供合法的 `get_usage_ref`。
事件、使用次數與狀態清理仍由原生流程及作者 callbacks 決定。

`target_tip` 只接受 declarative table，交給既有 factory/native validator；不接受 callback。
這是文字提示資料，不是新的決策或合法性判定機制。
禁止 `global=true`、scenario/rule、System holder selector 與巢狀 `view_as_skill`，
因為這裡每個效果都屬於 player instance。需要系統規則或裝備來源時使用原有 factories。
其他原生 spec 欄位的型別與支援範圍仍由對應 factory 負責。

## 逐 instance 加強與失去

`base_amount` 是共享定義預設；遊戲中不得用 `setBaseAmount` 當作某份實例的 buff。
三選一獎勵可以保存「玩家 objectName＋根 instance ID＋effect id」，並使用既有 Room API：

```lua
local ref = skill:effectRef(player, chosenRootID, "hand_limit")
if ref then room:addSkillInstanceAmount(player, ref, 1, "reward_hand_limit") end
```

選 `"draw"` 只改指定根的摸牌數；選 `"hand_limit"` 只改該根的直接 helper。
同玩家另一份同名技能、另一玩家同 ID、SourceAttached grant、孫節點及其他 helper 不被選中。
回傳的是 owner＋key 值引用；不保存底層 SkillInstance 指標，長期使用前需重新解析。
helper 的 instance ID 不等於根 ID，必須使用回傳 ref。

範例有兩個 root A/B 時，初始各抽 1，總上限貢獻 +2。A 的 draw amount 加 1 後，
A 抽 2、B 抽 1；A 的 hand_limit helper amount 加 1 後，總上限貢獻 +3。
對 `room:detachSkillFromPlayer(player, "example_resilience#" .. idA)` 精準移除 A，
引擎同時移除 A 的 helpers，B 的數值及狀態保留。
不要只傳裸技能名後自行猜引擎選了哪一份。

取得根時，既有 `EventAcquireSkill` 先通知根，再建立及通知 helpers。
因此根 acquire callback 內 helper 可能尚不存在，`effectRef` 回 nil；
若需初始化 child，使用對应 helper acquire 事件，或在正常 acquire 流程返回後處理。
失去事件沿原生 `SkillChangeStruct` 帶確切 source/parent/instance ID；組裝器不攔截、
重排或再次送出這些事件。失去根時其 private state 已被移除，清理不能假設仍能讀它。
既有國戰揭示、失效與 SourceHelper source gate 仍由 native 判斷。

## 驗證與來源

從 repo 根執行 `lua tools/authoring/tests/test_skill_assembly.lua`（Lua 5.4）。
測試載入真的 `lua/sgs_ex.lua`，只在 native 邊界提供 doubles，涵蓋主動／觸發根選擇、
所有 correction factory、scope/history callback 轉交、註冊衝突、spec 快照、
精確 owner/parent 解析、缺失／模糊 ref、範例 callback 的不同實例 amount 與零覆寫。
此 Lua contract suite 共 75 項檢查通過；它不是對局 runner，native 邊界仍是 doubles。

另於 Qt 6.11.1、重新編譯的 engine static library 與 SWIG 上執行 evidence-only
C++ probe，31 項檢查通過。它透過既有 `RoomTestAccess` 補齊正常 Room 初始化，
使用真正的 bootstrap／Room VM 載入範例，原生取得兩根及直接 SourceHelper、
獨立覆寫 root/helper amount、實算 maxcards、執行 Room damage 與抽牌、精準撤回一根、
再次受傷只由存活根抽牌；也實際透過 SWIG 呼叫 `effectRef`，用回傳 ref 修改數值，
驗證移除後查不到原根及 helper。正常 teardown 的 `CARD_LIFETIME_ZERO` 為零。
Native probe／build script／run.log／初始化與資產说明留在工作證據目錄
`/workspace/skillv2-evidence/assembly-native/`，不作為新產品 runner 發布。
客戶端同步、國戰可見性、互動選擇與完整對局流程不在此 probe 驗收範圍。

設計參考固定於 `freekill-core` 的 `c19441690711b73ffb427b3e7974ec7e92e33bea`，
`ltk/core/skill_skeleton.lua` 中 addEffect/createSkill 的「效果集合＋主效果＋related」
結構；該來源根 LICENSE 為 GPLv3。本模組依本 repo 的 V2 factory 與
SourceHelper 介面獨立實作，沒有搬入該引擎、規則、AI 或事件 dispatcher。
