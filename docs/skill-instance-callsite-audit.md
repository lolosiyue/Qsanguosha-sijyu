# 技能實例舊呼叫點人工審核清單

盤點日期：2026-07-16。此文件只記錄與分類，不批量修改 `acquireSkill()` 呼叫點。

## 結論與建議

`Room::acquireSkill()` 現在每次呼叫都建立新的 `SourceAcquired` 實例。舊呼叫點不能一律加上 `hasSkill()`，否則會破壞多實例語義。

建議採以下規則逐點審核：

| 舊語義 | 建議處理 |
|---|---|
| 每次效果都應再獲得一份技能 | 保持 `acquireSkill()` |
| 只要求玩家至少持有一份 | 保留或補上精確 `hasSkill(baseName)` guard |
| 先移除舊技能再換成新技能 | 記錄並傳遞精確 instanceID；不得只按 base name 猜測 |
| 暫時技能在結束時收回 | 保存取得時回傳的 instanceID，結束時精確移除 |
| helper／附加技能 | 由父實例自動建立與級聯移除，不應另行取得同一 helper |

## 盤點統計

以下是排除純註解行後的 lexical inventory；定義、宣告與 UI compatibility handler 仍可能包含在數字內，人工審核時需再排除。

| 類別 | 數量 |
|---|---:|
| `acquireSkill(` | 660 |
| `detachSkillFromPlayer(` | 563 |
| C++ `Room::acquireSkill` 呼叫 | 156 |
| 同行已有 `hasSkill` guard | 22 |
| Lua 已遷移 typed `SkillChangeStruct` 讀取 | 244 |
| C++ 已遷移 typed `SkillChangeStruct` 讀取 | 8 |

### acquire 呼叫分布

| 區域 | 數量 |
|---|---:|
| `extensions/` | 487 |
| `src/package/` | 130 |
| `src/scenario/` | 24 |
| `src/server/` | 11 |
| `src/client/`、`src/ui/` | 6 |
| `src/` 其他 | 2 |

### 高密度檔案（優先人工審核）

| 呼叫數 | 檔案 |
|---:|---|
| 71 | `extensions/fcDIY.lua` |
| 40 | `extensions/newgenerals.lua` |
| 36 | `extensions/yuri.lua` |
| 27 | `extensions/lol2015.lua` |
| 25 | `src/package/ol.cpp` |
| 22 | `extensions/touhouproject.lua` |
| 22 | `extensions/OverseasVersion.lua` |
| 22 | `extensions/sgs10th.lua` |
| 22 | `src/package/tenyear2.cpp` |
| 19 | `extensions/yongjian.lua` |
| 19 | `extensions/genius.lua` |
| 17 | `extensions/gaoda.lua` |
| 16 | `extensions/htms.lua` |
| 14 | `extensions/sijyuoffline.lua` |
| 14 | `src/package/mobile.cpp` |

## 已確認為 ensure 語義的同行 guard

下列 22 點已自行用 `hasSkill()` 防止重複取得；在新的 `hasSkill()` 多實例語義下仍可維持「至少一份」行為：

- `extensions/fcDIY.lua:3734-3736`
- `extensions/htms.lua:15436`
- `extensions/hunlie.lua:2088-2094`
- `extensions/hunlie.lua:7232-7238`
- `extensions/mojiangprevious.lua:1168`
- `extensions/sy.lua:1954`
- `extensions/sy.lua:3503-3504`
- `extensions/yuri.lua:9574-9578`
- `extensions/yuri.lua:15470`

注意：只檢查同行 guard；跨行條件仍需人工審核。

## EventAcquireSkill／EventLoseSkill listener

舊 `data:toString()` 仍由 `SkillChangeStruct -> QString` converter 回傳 base skill name，因此相容行為不變；新程式應改用：

```cpp
SkillChangeStruct change = data.value<SkillChangeStruct>();
if (change.skillName == objectName()) {
    // change.instanceID 可精確定位實例
}
```

```lua
local change = data:toSkillChange()
if change.skillName == self:objectName() then
    -- change.instanceID 可精確定位實例
end
```

第一輪同行掃描遷移 97 點；第二輪 ±4 行近鄰掃描再遷移 147 點。完成後同範圍的舊 `data:toString()` 殘留為 0，共 244 個 Lua typed 讀取。

原 Lua 同行字串 listener 的高密度檔案：

| 數量 | 檔案 |
|---:|---|
| 14 | `extensions/temp/extraheg.lua` |
| 12 | `extensions/scarletayuhuo.lua` |
| 11 | `extensions/scarlet.lua` |
| 10 | `extensions/sijyuoffline.lua` |
| 6 | `extensions/Dragon.lua` |
| 6 | `extensions/htms.lua` |
| 5 | `extensions/zhenghuoCMT.lua` |
| 4 | `extensions/keguibao.lua` |

## 可重跑的完整清單命令

行號會隨後續修改漂移，因此完整清單以來源搜尋為準：

```powershell
rg -n --no-heading "acquireSkill\(" src lua extensions --glob '*.cpp' --glob '*.h' --glob '*.lua'
rg -n --no-heading "detachSkillFromPlayer\(" src lua extensions --glob '*.cpp' --glob '*.h' --glob '*.lua'
rg -n --no-heading "Event(Acquire|Lose)Skill.*data:toString|data:toString.*Event(Acquire|Lose)Skill" lua extensions --glob '*.lua'
rg -n --no-heading "Event(Acquire|Lose)Skill.*data\.toString|data\.toString.*Event(Acquire|Lose)Skill" src --glob '*.cpp'
```

## 人工審核狀態

- [x] 完成全域盤點與優先級分類。
- [x] 確認 22 個同行 guard 為 ensure 語義。
- [ ] 逐一確認其餘呼叫是 stack、ensure、replace 或 temporary。
- [ ] 對 temporary／replace 呼叫保存並使用 `acquireSkill()` 回傳的 instanceID。
- [x] 將 EventAcquire/Lose 的 C++ 與 Lua 字串 listener 遷移至 `SkillChangeStruct` typed 讀取。
