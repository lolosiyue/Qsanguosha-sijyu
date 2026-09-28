# HUMAN Lua V2：規則與裝備橋接

本文件記錄本輪新增 API。驗證狀態：僅來源／靜態檢查；SWIG 重新生成、編譯及遊戲驗收尚未執行。

## 共用 callback 契約

| 欄位 | 簽名與意義 |
|---|---|
| `can_trigger` | `function(self, event, room, event_player, data)`；回傳 `self:objectName(), decision_player`，無候選回傳 `""`。也接受既有 `"skill|skill", "playerName|playerName"` 多玩家格式。 |
| `on_record` | `function(self, event, room, event_player, ctx)`；每事件一次，清理／記錄用；沒有回傳值。 |
| `on_cost` | `function(self, event, room, decision_player, ctx)`；是否同意發動，預設 `true`。 |
| `on_pay` | 同上；實際支付，失敗回傳 `false`，預設 `true`。 |
| `on_effect` | 同上；執行效果，回傳 `true` 中斷原事件，通常回傳 `false`。 |
| `on_effect_target` | 同上再加最後一個 `target` 參數；逐目標 V2 效果。 |
| `on_turn_broken` | `function(self, callback_name, event, room, decision_player, ctx)`；處理 `TurnBroken`／`StageChange` 中断後仍由原生拋出。 |

`ctx.original_data` 是原事件 QVariant；`ctx.invoker` 是原事件玩家，可能為 nil。
成本／支付／效果 callback 的 `decision_player` 是 `ctx.owner`，不應誤當原事件玩家。

## 無玩家實例規則

```lua
local rule = sgs.CreateRuleSkillV2 {
    name = "#example-rule",
    frequency = sgs.Skill_Compulsory,
    events = { sgs.GameStart },
    can_trigger = function(self, event, room, player, data)
        if room:getMode() ~= "example_mode" then return "" end
        local decision_player = player or room:getAllPlayers():first()
        return self:objectName(), decision_player
    end,
    on_effect = function(self, event, room, decision_player, ctx)
        -- 使用 ctx.original_data 讀寫事件；依需求處理 ctx.invoker == nil。
        return false
    end,
}
```

仍須沿用套件的 `Sanguosha:addSkills` 註冊定義。Factory 自動設為 global，
但不建立或授予任何 Player skill instance。規則透過既有 `collectTriggerContexts`、
`recordEvent` 與事件優先序掛鉤進入 V2 dispatcher。

selector 只允許回傳自身完整名稱；不接受另一技能、`#instanceId`、倍率或目標編碼。
`ctx.instanceID == 0`，`activationRef`／`sourceRef` 均為無效引用。
`on_record` 的 `ctx.owner == nil`；selector 必須選擇房內的真實 decision player，
原事件玩家可以為 nil。一般 `CreateTriggerSkillV2 { global = true }` 維持原本玩家实例語意。

模式隔離屬 Lua 規則責任：`can_trigger` 與 `on_record` 各自檢查 mode；
record 不以 can_trigger 的結果作為執行條件。規則不能借用普通玩家實例取得授權或次數。

### Scenario 接線

`CreateRuleSkillV2 { scenario = scenario, ... }` 自動呼叫 `scenario:setRule(skill)`，
使 rule 歸該 Scenario package 所有，設 `global = false`，未指定 priority 時用原生 ScenarioRule 的 `0`。
`Scenario::getRule/setRule` 容器接受 `TriggerSkill*`；GameSessionController 仍於原位置
呼叫 `addTriggerSkill`，不新增 dispatcher。規則仍須在 selector／record 明確處理 mode。

## Lua 裝備觸發技

```lua
local equip = sgs.CreateEquipSkillV2 {
    name = "example_weapon",
    equipment = "example_weapon",       -- 裝備卡 objectName
    equipment_type = "weapon",          -- weapon / armor / treasure / offensive_horse / defensive_horse
    frequency = sgs.Skill_Compulsory,
    events = { sgs.DamageCaused },
    can_trigger = function(self, event, room, player, data)
        if not player then return "" end
        return self:objectName(), player
    end,
    on_effect = function(self, event, room, player, ctx)
        return false
    end,
}
```

原生類 `LuaEquipSkillV2 : EquipSkillV2` 保留裝備來源權威：一般來源必須通過
`hasWeapon`／`hasArmorEffect`／`hasTreasure`／`hasOffensiveHorse`／`hasDefensiveHorse`；虛擬裝備保留精確來源 instance ref、
失效與預亮檢查。Lua selector 回傳候選不代表自動取得裝備效果。

支持 `events`、數值或逐事件陣列 `priority`、`frequency`、`base_amount`、
`view_as_skill` 及上表 callbacks。`global` 預設為 true，使定義與卸裝紀錄進入 dispatcher；
這不放寬來源檢查，可顯式設 false 改由既有裝備安裝註冊流程負責。成本／支付／效果進入既有 V2 流程；不接受 `on_trigger`。

卸裝清理放在 `on_record`，因此離開裝備區且失去技能後仍能處理事件。
需要離場時發動效果的裝備可設 `movement_source = true`：只在 `CardsMoveOneTime`，
且 `move.from == ctx.owner`、事件列出相同名稱的實體裝備由 `PlaceEquip` 離開時，
原生才承認事件來源。這不授予虛擬裝備、不接受任意旗標或 Lua bool 繞過來源檢查。
支付消耗裝備及虛擬来源後續失效均沿用 `EquipSkillV2` 契約。

裝備主動技使用 `CreateViewAsSkillV2` 並放入 `view_as_skill`，其 name 必須與
`CreateEquipSkillV2.name` 相同。只有同名已註冊 EquipSkillV2 的精確 active component
才被 `LuaViewAsSkillV2::isEquipSkill` 識別，零實例入口仍經原生 `prepareEquipSource`。
一般 Lua ViewAsSkillV2 不會因缺少 instance 而取得裝備權限。

純 active 遷移需保留舊 SkillCard 次數鍵時，CreateViewAsSkillV2.history_key 可指定非空字串；
未指定或空字串仍使用原生 historyKey(request)，並非另外建立次數系統。

## 精確附屬實例 SWIG 查詢

本輪僅公開既有權威操作，未新增實例管理系統：

- `room:attachSkillToPlayer(player, skillName, parentRef[, visible])` 回傳精確 child ref；父來源必須仍存在。
- `room:detachAttachedSkill(childRef)` 只移除附屬來源鏈，不按裸技能名刪除其他取得來源。
- `player:getSkillInstanceParentRef(skillName, instanceID)` 回傳 parentRef 值快照；不存在回無效 ref，不交出 registry 內部指標。
- `player:isSkillInstanceEffectAvailable(skillName, instanceID[, targetModPreviewOwner])` 沿既有有效性與精確根來源的揭示狀態判定。

宣告已逐項與 C++ 對齊；仍須 checkpoint 授權後正常 CMake 生成並編譯 SWIG wrapper。
