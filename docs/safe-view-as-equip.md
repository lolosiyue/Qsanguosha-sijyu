# Safe ViewAs Equip（手牌安全視為裝備）

## 原則（避免閃退）

- 不要直接改原手牌 `TypeId` / 類型。
- 應使用：`cloneCard(目標裝備名, 原牌花色, 原牌點數)` 生成真正的裝備子類。
- 之後用 `WrappedCard::takeOver`（或 Lua 同等）接管該手牌 ID。
- 最後走裝備牌移動流程（先換下舊裝備，再移入裝備區）。

## C++ 參考實作

已在 [src/package/yjcm2023.cpp](../src/package/yjcm2023.cpp) 實作：

- `zhizheEquipObjectNameByArea(int area)`：裝備位 -> 裝備牌名映射。
- `safeTurnCardToEquip(...)`：
  - 僅允許手牌（`PlaceHand`）。
  - 強檢查 `cloneCard` 結果必須是 `EquipCard`。
  - 強檢查目標裝備位存在（`hasEquipArea(location)`）。
  - 按 `S_REASON_CHANGE_EQUIP` + `S_REASON_USE` 完整換裝流程。
- `GongqiaoCard::use` 已改為調用 `safeTurnCardToEquip`。

## Lua 參考實作

> **更正（2026-08-21 校對）**：`lua/sgs_ex.lua` 並未提供 `sgs.SafeTurn*` 通用 Lua API；安全換裝的權威實作為 `src/package/yjcm2023.cpp` 內的**靜態 C++ helper** `zhizheEquipObjectNameByArea()`／`safeTurnCardToEquip()`，僅由 `GongqiaoCard::use` 等 C++ 技能直接調用。Lua 擴充如需同等安全路徑，應在各自技能內參照該 helper 的 `cloneCard`→`WrappedCard::takeOver`→換裝流程自行實作，或將 helper 抽為共用工具後再以 SWIG 暴露（尚未落地）。

原文件所述 `lua/sgs_ex.lua` 的三個 `sgs.*` API 屬規劃未落地，已更正：

- ~~`sgs.GetZhizheEquipObjectNameByArea(area)`~~
- ~~`sgs.SafeTurnHandCardToEquip(room, player, card_id, equip_name, skill_name)`~~
- ~~`sgs.SafeTurnHandCardToZhizheEquip(room, player, card_id, area, skill_name)`~~

### Lua 用法示例

```lua
-- 把一张手牌按“武器位”安全视为装備并装上
local ok = sgs.SafeTurnHandCardToZhizheEquip(room, player, card_id, 0, "my_skill")
if not ok then
    return false
end
```

```lua
-- 指定具体装備名（必须是 EquipCard）
local ok = sgs.SafeTurnHandCardToEquip(room, player, card_id, "_zhizhe_armor", "my_skill")
if not ok then
    return false
end
```

## 額外提醒

- 若你要“視為某張真實裝備（如 `crossbow`）”，同樣可用 `SafeTurnHandCardToEquip`，但要確保該名稱對應 `EquipCard`。
- 若技能只允許某些類別（武器/防具/坐騎/寶物），先限制選擇，再調用安全函式。
