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

### Lua 端的正確做法

Lua 沒有可呼叫的 `sgs.SafeTurn*` API；同等安全路徑須在各自技能內，對照 `safeTurnCardToEquip` 的順序自行實作：

1. `cloneCard(目標裝備名, 原牌花色, 原牌點數)` 產生真正的裝備子類（必須確認結果為 `EquipCard`）。
2. 以 `WrappedCard::takeOver`（或 Lua 同等手段）接管原手牌 ID。
3. 走完整換裝移動流程（先換下舊裝備，再移入裝備區）。

## 額外提醒

- 要「視為某張真實裝備（如 `crossbow`）」時，`cloneCard` 的名稱必須對應真正的 `EquipCard` 子類。
- 若技能只允許某些類別（武器／防具／坐騎／寶物），先在選牌階段限制，再進入換裝流程。
- 若需要通用 Lua API，須先將 C++ helper 抽為共用工具並經 SWIG 暴露（尚未落地，見上方更正）。
