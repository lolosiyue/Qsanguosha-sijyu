# ProhibitSkillV2 功能與開發指南

## 1. 功能定位

`ProhibitSkillV2` 為卡牌禁止目標建立逐持有者、逐技能實例的 callback。它繼承 `ProhibitSkill`，仍由 Engine 的共用 prohibition 入口查詢；既有 `ProhibitSkill` 維持原有定義級 callback，不會自動遷移。

V2 以 `CorrectSkillHolderSelector` 選出持有者，沿用既有 enum，不新增 selector、amount 或數值聚合模型。Engine 對每個有效持有實例建立短生命期的 `ProhibitSkillContext`，呼叫 Lua callback；任一 callback 回傳 `true` 即禁止並停止後續檢查。同名技能持有數量不會乘上或加權禁止結果。

C++ 技能實作 `isProhibited(const ProhibitSkillContext &context)`。查詢必須經 `Engine::isProhibited()`；V2 的舊四參數重載固定回傳 false，避免略過引擎的持有者／實例閘門。

## 2. Lua 建立技能

```lua
local guard = sgs.CreateProhibitSkillV2 {
	name = "guard_target_v2",
	frequency = sgs.Skill_Compulsory,
	-- 預設為 CorrectSkill_Secondary：檢查被指定目標的有效實例。
	holder_selector = sgs.CorrectSkill_Secondary,
	supports_source_less = false,
	is_prohibited = function(self, ctx)
		-- 關鍵邏輯：禁止 Slash 指定持有者為目標。
		local holder = ctx:getHolder()
		local target = ctx:getTo()
		local card = ctx:getCard()
		if holder == target and card:isKindOf("Slash") then
			return true
		end
		return false
	end,
}
```

Factory 參數：

| 欄位 | 行為 |
|---|---|
| `name` | 技能定義名稱，必填 |
| `frequency` | 技能頻率，沿用現有 Lua 技能頻率；省略時使用既有 factory 預設 |
| `holder_selector` | `CorrectSkillHolderSelector`；省略時為 `sgs.CorrectSkill_Secondary` |
| `supports_source_less` | 是否允許 `from == nil` 時呼叫；省略時為 `false` |
| `gate` | 僅 `System`：callback 回傳 `true` 的必要條件，至少一項成立才呼叫 callback。格式同 [CorrectSkillV2 §4.1](CorrectSkillV2功能與開發指南.md#41-system-門檻gate)，primary 為 `from`、secondary 為 `to` |
| `is_prohibited` | `function(self, ctx)`，回報本次有效持有實例是否禁止 |

回傳值採嚴格布林契約：只有 Lua `true` 會禁止；`false` 或 `nil` 表示不禁止。其他型別會記錄 warning 並視為不禁止；callback 錯誤也會記錄 warning 並視為不禁止。這能避免錯誤或意外回傳值把卡牌永久鎖死。

## 3. 持有者選擇與執行流程

| Selector | 候選持有者 |
|---|---|
| `sgs.CorrectSkill_Primary` | 存活的 `from`；為空或不存活時沒有候選者 |
| `sgs.CorrectSkill_Secondary` | 存活的 `to`；為空或不存活時沒有候選者 |
| `sgs.CorrectSkill_Participants` | 存在且存活的 `from`、`to`，去重後檢查 |
| `sgs.CorrectSkill_AllHolders` | 以至少一個存在的 `from`／`to` 為查詢 anchor，檢查場上存活且持有此技能的玩家；兩者皆 nil 時沒有候選者 |
| `sgs.CorrectSkill_System` | 不選玩家；對此技能定義執行一次，context 的 holder 為空、instance ref 無效 |

Engine 先依 selector 選玩家，再取得每名玩家此技能的所有 instance ID，並以 `isSkillInstanceEffectAvailable()` 排除無效實例及不可用來源，最後才呼叫 callback。這沿用 CorrectSkillV2 的精確實例失效、隱藏技能可用性與 related helper 來源規則；國戰暗置武將的天生來源不參與，禁止查詢也不觸發亮將。沒有有效持有者時，持有者型 selector 不會呼叫 Lua callback；`System` 每次查詢執行一次，宣告 `gate` 時先檢查門檻。

`System` 與 source-less 是兩個獨立設定。`System` 表示 callback 不經持有者閘門；`supports_source_less` 表示 callback 是否接受沒有使用者的查詢。若 `from == nil` 且 `supports_source_less` 為 false，該 V2 技能不會執行，即使 selector 是 `System`。若為 true，selector 仍按自身規則選 holder；System 仍沒有 holder 或 instance ref。

空值邊界如下：null card 直接回報不禁止，不執行任何 callback；`from` 或 `to` 為空時，只能選取存在且存活的參與者，Secondary 在 `to` 為空時候選集合為空。`Participants` 對單一存在且存活的參與者仍可執行。`AllHolders` 必須至少有一個 `from` 或 `to` 作查詢 anchor；兩者皆 nil 時沒有候選者。source-less opt-in 不代表允許 null card，也不會自動令 null target 成為合法目標。

## 4. Context 與逐實例狀態

| Getter | 說明 |
|---|---|
| `getInstanceRef()` | 本次持有實例的 `SkillInstanceRef`；System 為無效 ref |
| `getHolder()` | 本次實例持有者；System 為 nil |
| `getFrom()` | 卡牌使用者，可能為 nil |
| `getTo()` | 正在檢查的目標，可能為 nil |
| `getCard()` | 被檢查的卡牌 |
| `getOtherTargets()` | 其他已選目標，Lua `PlayerList` |
| `getStateValue(key, default)` | 讀取本實例既有 `correctState` 值 |

Context 只在 callback 執行期間有效；不可保存 context、其中的玩家／卡牌參考，或在 callback 結束後使用。Callback 是查詢，不得改變遊戲狀態。逐實例 state 沿用 `SkillInstance::correctState`；需要在正常房間流程中更新時，使用既有 Room API：

```lua
room:setSkillInstanceCorrectState(holder, ref, "enabled", sgs.QVariant(value))
```

其中 `holder` 是 `ServerPlayer`，`ref` 是該實例的 `SkillInstanceRef`。完整 state 規則見 [CorrectSkillV2 功能與開發指南的 correctState 章節](CorrectSkillV2功能與開發指南.md#10-correctstate)。System 沒有實例，因此沒有逐實例 state；`getStateValue` 使用 default。

## 5. 遷移與相容性

把持有者自己的禁止效果遷移到 V2 時，使用預設 Secondary 可表達「目標持有技能」；若規則取決於使用者持有技能，指定 `CorrectSkill_Primary`。兩邊持有者的技能都可能禁止時，指定 `CorrectSkill_Participants`，Engine 會去重玩家並逐一檢查實例。

全場光環使用 `CorrectSkill_AllHolders`。例如光環持有者可以禁止任一目標被 Slash 指定：

```lua
local aura = sgs.CreateProhibitSkillV2 {
	name = "slash_aura_v2",
	holder_selector = sgs.CorrectSkill_AllHolders,
	is_prohibited = function(self, ctx)
		-- 關鍵邏輯：任一存活光環持有者均可禁止本次 Slash。
		return ctx:getCard():isKindOf("Slash")
			and ctx:getTo() ~= nil
			and ctx:getHolder() ~= ctx:getTo()
	end,
}
```

既有 `ProhibitSkill` 不會因為有 V2 factory 就改變執行方式；逐步遷移時應保留舊技能，直到每項持有者、null source、設備與模式語意都明確對應。實體裝備若沒有技能實例，仍沿用既有 legacy prohibition。效果由標記、旗標、牌堆或牌上技能名驅動，失去技能後仍延續的禁止技使用 `System`，並以 `gate` 列出必要條件。

## 6. 執行環境與效能邊界

ProhibitSkillV2 經 Engine 共用入口執行，Room、server 與 client 查詢沿用同一 dispatch 與 runtime skill registry。Room runtime 註冊的技能與 bootstrap registry 合併後，仍由 Engine 統一查詢；不要為 client、server 或 Room 另建一份 prohibition 語意。

每次查詢仍需走訪註冊的 prohibition 定義以找出命中項；命中後立即短路。`AllHolders` 還需要在存在查詢 anchor 時掃描場上候選玩家及其相關有效實例，其他 selector 僅檢查存活查詢參與者。這份契約沒有性能測試證據，因此不宣稱任何加速比例或絕對延遲改善。
