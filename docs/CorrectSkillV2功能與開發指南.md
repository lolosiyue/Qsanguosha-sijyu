# CorrectSkillV2 功能與開發指南

## 1. 功能定位

`CorrectSkillV2` 是多實例技能使用的數值修正系統。它解決舊引擎只能按技能定義計算一次，或把一次 callback 結果錯誤乘上同名技能持有數量的問題。

新系統的最小計算單位是「持有者＋技能名＋實例 ID」，即 `SkillInstanceRef`。每個有效實例分別建立上下文、執行 callback，再由 Engine 聚合結果。

| 能力 | Legacy CorrectSkill | CorrectSkillV2 |
|---|---|---|
| 同一玩家持有多個同名實例 | 每個技能定義計算一次 | 每個有效實例分別計算 |
| 不同玩家同名技能隔離 | 無逐實例上下文 | 以 owner＋instanceID 隔離 |
| 每實例數值 | 不支援 | base amount＋current override |
| 每實例公開狀態 | 不支援 | `correctState` |
| 持有者來源 | 類別固定 | 五種 Holder Selector |
| Lua callback | 類別各自定義 | 統一三態回傳契約 |
| 精確失效 | 以技能名為主 | 可排除指定 instanceID |

目前只應使用測試技能驗證。正式技能須在完整執行期矩陣通過後才可遷移。

## 2. 支援類別

| 用途 | C++ 類別 | Lua factory | 數值方向 |
|---|---|---|---|
| 距離修正 | `DistanceSkillV2` | `sgs.CreateDistanceSkillV2` | 負數縮短距離，正數增加距離 |
| 手牌上限 | `MaxCardsSkillV2` | `sgs.CreateMaxCardsSkillV2` | 正數增加，負數減少 |
| 卡牌目標修正 | `TargetModSkillV2` | `sgs.CreateTargetModSkillV2` | 依 `modType` 修正次數、距離或目標數 |
| 攻擊範圍 | `AttackRangeSkillV2` | `sgs.CreateAttackRangeSkillV2` | 正數增加，負數減少 |

四類技能均提供：

- 共享基礎值 (Base Amount)：技能定義的預設數值。
- 實例目前值 (Current Amount)：每個技能實例可有獨立 override。
- 持有者選擇器 (Holder Selector)：決定本次查詢要遍歷哪些玩家。
- `CorrectSkillContext`：向 callback 提供只讀查詢資料。
- `CorrectSkillResult`：明確表達不適用、數值貢獻或 Residue 無限。

## 3. 計算流程

| 順序 | Engine 行為 |
|---|---|
| 1 | 根據修正類型建立 primary、secondary、card、modType 等查詢資料 |
| 2 | 按 Holder Selector 選出玩家；`System` 不選玩家 |
| 3 | 對每位玩家取得該技能所有有效 instanceID |
| 4 | 精確失效 (Instance-scoped Invalidity) 的實例不進入 callback |
| 5 | 讀取該實例 current amount 與 `correctState`，建立 `CorrectSkillContext` |
| 6 | 每個實例分別呼叫 callback |
| 7 | 普通修正保留有號整數後相加；fixed 結果取所有適用值的最大值 |

不同玩家即使使用相同技能名及相同 instanceID，仍因 owner 不同而視為不同 `SkillInstanceRef`，不會互相污染。

## 4. Holder Selector

| Lua 常量 | 行為 | 典型用途 |
|---|---|---|
| `sgs.CorrectSkill_Primary` | Distance／TargetMod 使用 `from`；MaxCards／AttackRange 使用 `target` | 技能只影響持有者自己的計算 |
| `sgs.CorrectSkill_Secondary` | Distance／TargetMod 使用 `to`；沒有 secondary 時不貢獻 | 被指定目標持有的防禦／距離技能 |
| `sgs.CorrectSkill_Participants` | primary、secondary 去重後各自檢查 | 雙方技能都可能影響結果 |
| `sgs.CorrectSkill_AllHolders` | 全場所有存活且持有技能的玩家 | 全場光環或場地效果 |
| `sgs.CorrectSkill_System` | 沒有 holder 與 instanceRef，只用共享 base 計算一次 | 模式規則或系統常數 |

`System` 沒有技能實例，因此不能使用逐實例 amount override 或 `correctState`。

## 5. CorrectSkillContext

| Getter | 型別 | 說明 |
|---|---|---|
| `getInstanceRef()` | `SkillInstanceRef` | 本次實例；System 為無效 ref |
| `getHolder()` | `Player` | 本次實例持有者；System 為空 |
| `getPrimary()` | `Player` | 查詢主體 |
| `getSecondary()` | `Player` | Distance／TargetMod 的另一參與者，可能為空 |
| `getCard()` | `Card` | TargetMod 查詢卡牌，其他類型通常為空 |
| `getModType()` | `int` | TargetMod 的 `Residue`／`DistanceLimit`／`ExtraTarget` |
| `includesWeapon()` | `bool` | AttackRange 查詢是否包含武器 |
| `getCurrentAmount()` | `int` | 本實例 override；沒有 override 時為共享 base |
| `getStateValue(key, default)` | `QVariant` | 讀取本實例公開 `correctState` |

callback 不應修改 context。需要改變持久狀態時，使用 Room API。

## 6. Lua callback 契約

所有 V2 correct callback 採用相同回傳語意：

| Lua 回傳 | 結果 |
|---|---|
| `nil` 或 `false` | 本實例不適用，不參與聚合 |
| `true` | 使用 `ctx:getCurrentAmount()` |
| 數字 | 使用該數值；零與負數均有效 |
| 其他型別 | 記錄 warning，失敗關閉 (Fail-closed) |
| callback 發生錯誤 | 記錄 warning，本實例本次貢獻為零 |

只有 `TargetModSkill_Residue` 的 `-1` 表示無限次數；其他類型的 `-1` 保留為普通負數。

## 7. Lua 建立技能

### 7.1 DistanceSkillV2

```lua
local short_distance = sgs.CreateDistanceSkillV2 {
	name = "short_distance_v2",
	base_amount = -1,
	holder_selector = sgs.CorrectSkill_Primary,
	correct_func = function(skill, ctx)
		-- 關鍵邏輯：true 使用本實例 current amount。
		return true
	end,
	fixed_func = function(skill, ctx)
		-- nil 表示本實例沒有 fixed 值。
		return nil
	end,
}
```

### 7.2 MaxCardsSkillV2

```lua
local hand_limit = sgs.CreateMaxCardsSkillV2 {
	name = "hand_limit_v2",
	base_amount = 2,
	holder_selector = sgs.CorrectSkill_Primary,
	correct_func = function(skill, ctx)
		return ctx:getHolder():isWounded() and true or false
	end,
}
```

### 7.3 TargetModSkillV2

```lua
local slash_residue = sgs.CreateTargetModSkillV2 {
	name = "slash_residue_v2",
	pattern = "Slash",
	base_amount = 1,
	holder_selector = sgs.CorrectSkill_Primary,
	correct_func = function(skill, ctx)
		-- 關鍵邏輯：同一 callback 依 modType 判斷修正項目。
		if ctx:getModType() == sgs.TargetModSkill_Residue then
			return true
		end
		return false
	end,
}
```

若 Residue 要表示無限次數，callback 明確回傳 `-1`。不要把其他修正類型的 `-1` 當成無限。

### 7.4 AttackRangeSkillV2

```lua
local attack_range = sgs.CreateAttackRangeSkillV2 {
	name = "attack_range_v2",
	base_amount = 1,
	holder_selector = sgs.CorrectSkill_Primary,
	correct_func = function(skill, ctx)
		if not ctx:includesWeapon() then
			return false
		end
		return true
	end,
	fixed_func = function(skill, ctx)
		return nil
	end,
}
```

`fixed_func` 只適用於 Distance、MaxCards、AttackRange。TargetMod V2 只有 `correct_func`。

## 8. amount 模型

### 8.1 數值來源與優先順序

| 使用路徑 | 優先順序 | 說明 |
|---|---|---|
| CorrectSkillV2 callback | 實例 current override → 技能共享 base | Engine 把結果寫入 `CorrectSkillContext.currentAmount` |
| TriggerV2／ViewAsSkillV2 單次執行 | execution-local `modified_amount` → amount ref 的 current/base | transient 修改不會寫回技能實例 |

`modified_amount` 只屬於 TriggerV2／ViewAsSkillV2 的 `SkillContext`，不屬於 `CorrectSkillContext`。它必須使用 `setModifiedAmount()`、`clearModifiedAmount()`、`hasModifiedAmount()`；零與負數都是已設定的有效值。

### 8.2 建立 SkillInstanceRef

`acquireSkill()` 回傳 instanceID。需要保存或重建完整 ref：

```lua
local instance_id = ROOM:acquireSkill(player, "short_distance_v2")
local ref = sgs.SkillInstanceRef(
	player:objectName(),
	sgs.SkillInstanceKey("short_distance_v2", instance_id)
)
```

不要只用技能名尋找要修改的實例。

### 8.3 Room amount API

```lua
ROOM:setSkillInstanceAmount(player, ref, -2, "skill_effect")
ROOM:addSkillInstanceAmount(player, ref, 1, "skill_growth")
ROOM:resetSkillInstanceAmount(player, ref, "skill_expired")

local amount = ROOM:getSkillInstanceAmount(ref)
```

| API | 行為 |
|---|---|
| `setSkillInstanceAmount` | 設定明確 override；零與負數有效 |
| `addSkillInstanceAmount` | 在 current amount 上加入 signed delta；整數溢位時拒絕 |
| `resetSkillInstanceAmount` | 回到技能 base；正常情況會移除 override |
| `getSkillInstanceAmount` | 取得 override 或 base |

## 9. amount 事件

| 事件 | 可否修改資料 | 用途 |
|---|---|---|
| `EventSkillAmountChanging` | 可以修改 `newAmount` 或設定 `canceled` | 攔截、增減、取消數值變更 |
| `EventSkillAmountChanged` | 僅通知 | 更新其他狀態或記錄結果 |

`SkillAmountChangeStruct` 欄位：

| 欄位 | 說明 |
|---|---|
| `source` | 發起改值的玩家 |
| `skillRef` | 被修改的精確技能實例 |
| `oldAmount`／`newAmount` | 修改前後數值 |
| `reason` | 呼叫端提供的原因字串 |
| `canceled` | Changing 階段設為 `true` 可取消 |
| `resetToBase` | 本次操作是否由 reset 發起 |

相同 `SkillInstanceRef` 在 Changing 至 Changed 完成前再次改值會被拒絕，避免事件遞迴。不同 ref 仍可正常修改。

C++ 事件處理範例：

```cpp
bool trigger(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
{
    SkillAmountChangeStruct change = data.value<SkillAmountChangeStruct>();
    if (event == EventSkillAmountChanging && change.reason == "double_amount") {
        // 關鍵邏輯：Changing 修改後必須寫回 QVariant。
        change.newAmount *= 2;
        data = QVariant::fromValue(change);
    }
    return false;
}
```

## 10. correctState

`SkillInstance::correctState` 是 CorrectSkillV2 可公開讀取的逐實例狀態。它與伺服器私有 `state` 分離，不應把私有狀態放進 `correctState`。

```lua
ROOM:setSkillInstanceCorrectState(player, ref, "enabled", true)
ROOM:setSkillInstanceCorrectState(player, ref, "level", 2)
ROOM:removeSkillInstanceCorrectState(player, ref, "level")
ROOM:clearSkillInstanceCorrectState(player, ref)
```

| 操作 | 同步粒度 |
|---|---|
| set | 單一 key |
| remove | 單一 key |
| clear | 整個 `correctState` map |

改變 `correctState` 不會觸發 `skill_acquired`，也不會影響同名技能的其他實例。

## 11. fixed 值

Distance、MaxCards、AttackRange 可提供 `fixed_func`／`getFixedValue()`。

| callback 結果 | Engine 行為 |
|---|---|
| 不適用 | 不參與 fixed 聚合 |
| 適用數字 | 與其他適用實例比較 |
| 多個適用值 | 取最大值 |
| 沒有任何適用值 | 視為沒有 fixed 修正 |

fixed 與普通 signed correction 是兩條獨立查詢路徑，不應用特殊負數模擬「沒有 fixed 值」。

## 12. C++ 自訂技能

```cpp
class ShortDistanceV2 : public DistanceSkillV2
{
public:
    ShortDistanceV2() : DistanceSkillV2("short_distance_v2")
    {
        setBaseAmount(-1);
        setHolderSelector(CorrectSkill_Primary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        if (!context.getHolder())
            return CorrectSkillResult::noEffect();
        // 關鍵邏輯：每次只回傳目前 instance 的貢獻。
        return CorrectSkillResult::useAmount(context.getCurrentAmount());
    }
};
```

Skill 衍生類別不加入 `Q_OBJECT`。callback 不得自行遍歷同名技能持有者，也不得把結果乘以實例數；Engine 已負責逐實例呼叫。

## 13. Snapshot、Delta 與 UI

技能實例快照 (Snapshot) 保持舊 8／9 欄相容，必要時在第 10 欄附加 metadata map：

| metadata key | 說明 |
|---|---|
| `has_amount` | 是否存在 current amount override |
| `amount` | override 數值 |
| `correct_state` | 公開狀態 map |

amount 與 correctState 使用獨立增量同步 (Delta Sync)。伺服器沿用技能實例原有可見權限，只向本來有權接收 metadata 的客戶端發送資料；隱藏技能不因此額外曝光。

客戶端套用 delta 後發出：

- `skill_instance_amount_changed`
- `skill_instance_correct_state_changed`

RoomScene 收到信號後重新驗證技能按鈕、選牌狀態與目標預覽。

## 14. Legacy 相容規則

| 規則 | 保證 |
|---|---|
| Legacy 四類 CorrectSkill | 每個技能定義計算一次，不改正式技能舊語意 |
| V2 類別 | Engine 特別辨識，不會落入 legacy 分支重複計算 |
| 舊 8／9 欄 snapshot | 客戶端繼續接受 |
| 舊全域 amount override | 已移除，新技能不得依賴 |
| 舊同名技能乘法 property | 已移除，新技能不得依賴 |

## 15. 遷移檢查表

| 檢查 | 要求 |
|---|---|
| 類別 | 選用正確的四類 V2 factory／C++ class |
| selector | 明確決定 Primary、Secondary、Participants、AllHolders 或 System |
| callback | 只計算單一 context instance，不自行乘實例數 |
| 回傳值 | `nil/false`、`true`、數字三態使用正確 |
| 負數 | 確認只有 Residue `-1` 使用無限語意 |
| instanceRef | 持久改值必須保存 owner＋skillName＋instanceID |
| state | 公開修正狀態放 `correctState`，私密資料留在 server state |
| 失效 | 驗證指定 instanceID 失效不影響其他實例 |
| 同步 | 驗證重連 snapshot、delta 與隱藏 metadata 權限 |
| 回歸 | 驗證同一玩家多實例及另一玩家同名技能隔離 |

## 16. 驗證與目前開放狀態

| 項目 | 狀態 |
|---|---|
| 核心程式與 SWIG | 已完成 |
| Release x64 編譯 | 已通過，0 errors |
| C++／Lua fixture | 已建立 |
| 本機 executable runtime | `-1073741701 / 0xC000007B`，未能啟動 |
| CorrectSkillV2 正式技能 | 尚未開放 |
| TriggerV2 正式技能 | 可小量新增並附獨立回歸 |
| ViewAsSkillV2 正式技能 | 尚未開放 |

CorrectSkillV2 必須在可正常啟動的環境完成 Room lifecycle、client reconnect、隱藏 metadata 及 legacy 對局回歸，才可開始遷移正式技能。

## 17. 相關檔案

| 內容 | 位置 |
|---|---|
| Engine 規格摘要 | [`engine-correct-skills.md`](engine-correct-skills.md) |
| 驗證矩陣 | [`correct-skill-v2-test-matrix.md`](correct-skill-v2-test-matrix.md) |
| 多實例總體設計 | [`skill-instance-refactor-plan.md`](skill-instance-refactor-plan.md)（現行權威；舊 [`multi-skill-instance-design.md`](multi-skill-instance-design.md) 設計未採納，僅存檔） |
| C++ 類別與 context | `src/core/skill.h`、`src/core/skill.cpp` |
| amount／correctState Room API | `src/server/room.h`、`src/server/room.cpp` |
| Lua factory | `lua/sgs_ex.lua` |
| SWIG Lua callback | `swig/luaskills.i` |
| C++ 測試 fixture | `src/package/standard-generals.cpp` 的 `~test` package |
| Lua factory smoke | `lua/test/examples/test_correct_skill_v2.lua` |
| Room integration fixture | `lua/test/examples/test_correct_skill_v2_room.lua` |
