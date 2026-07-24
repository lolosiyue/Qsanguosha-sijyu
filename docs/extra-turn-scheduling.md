# 額外回合排程系統

## 目的

引擎保留立即執行的 `ServerPlayer::gainAnExtraTurn()`，並由 `Room` 提供延後至當前回合結束後處理的排程 API。Lua 不再以字串 Room Tag 自建額外回合佇列。

## API

| API | 用途 |
|---|---|
| `player:gainAnExtraTurn(phases)` | 立即、巢狀執行額外回合；既有語意不變 |
| `room:scheduleExtraTurn(player, reason, phases, times)` | Legacy 技能按名稱排程 |
| `room:scheduleExtraTurn(player, sourceRef, phases, times)` | V2 技能按 `SkillInstanceRef` 精確排程 |
| `room:isCurrentExtraTurn()` | 查詢目前是否正在執行額外回合 |
| `room:getCurrentExtraTurnReason()` | 取得目前排程的文字來源；立即 API 回傳空字串 |
| `room:getCurrentExtraTurnSourceRef()` | 取得目前排程的技能實例來源；Legacy／立即 API 回傳無效引用 |

`phases` 為空時執行完整回合；非空時依傳入順序執行，引擎自動補上 `NotActive`。`times` 預設為 `1`，小於或等於零時不建立排程。API 回傳實際排入的數量。

## Lua 範例

Legacy 技能：

```lua
room:scheduleExtraTurn(target, self:objectName())
```

指定階段及次數：

```lua
local phases = sgs.PhaseList()
phases:append(sgs.Player_Draw)
phases:append(sgs.Player_Discard)
room:scheduleExtraTurn(target, self:objectName(), phases, 2)
```

TriggerV2 多實例技能：

```lua
room:scheduleExtraTurn(target, ctx:getSourceRef())
```

後續事件可精確判斷來源：

```lua
if room:isCurrentExtraTurn() then
    local source = room:getCurrentExtraTurnSourceRef()
    if source:isValid() then
        -- ownerObjectName + skillName + instanceID 唯一識別來源實例
    end
end
```

## 排程規則

| 項目 | 規則 |
|---|---|
| 執行時點 | 當前角色進入 `NotActive` 且回合結束觸發完成後、`RoundEnd` 與 `_lun` 清理前 |
| 多名角色 | 每批按引擎行動順序排序 |
| 同一角色多次 | 保持連續並依原排入次序執行 |
| 執行期間新增排程 | 進入下一批快照；本批完成後重新排序 |
| 目標死亡或移出遊戲 | 排入時拒絕；執行前再次檢查並略過 |
| 來源技能稍後失去 | 不取消已成立的排程；`sourceRef` 作歷史來源識別 |
| `TurnBroken` | 只終止當次額外回合，繼續同批其餘項目 |
| 模式級控制事件 | 停止本次處理並向外傳遞；尚未執行項目保留 |
| 日誌與語音 | 核心不自動發送，由技能自行處理 |

## 相容狀態

執行立即或排程額外回合時，核心繼續維護：

```text
Global_ExtraTurn<playerObjectName>
@extra_turn
```

兩者均支援巢狀額外回合，內層結束後恢復外層狀態。核心不新增通用 `ExtraTurn` Room Tag，也不接管擴展自行使用的 `isExtraTurn` Player Flag。

