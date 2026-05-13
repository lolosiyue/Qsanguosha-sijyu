# Juguan Presenter / Guhuo 回退 對 SmartAI 影響說明

## 結論

- 目前定案是 `JuguanDialog` 保留 Dashboard presenter，`GuhuoDialog` 回退到原本 modal dialog
- 影響範圍仍然只在客戶端展示層與互動流程 (UX)
- `SmartAI` 的出牌決策、質疑判斷、禁用項判斷，仍然沿用原本的 tag / mark / card name 協議
- 因此這一輪不應改變 AI 行為；改變的是 `Juguan` 玩家看到選項的方式，以及 `Guhuo` 不再強行共用同一套擁擠 presenter

## 變更範圍

| 層級 | 本輪是否改動 | 說明 |
|------|--------------|------|
| `Dashboard` / `RoomScene` presenter | 是 | 目前只把 `JuguanDialog` 的候選項改為在手牌區顯示，並用單列疊放 option item 呈現 |
| `GuhuoDialog` / `JuguanDialog` 選項 API | 是 | 新增 `prepareOptions()`、`getOptionNames()`、`getOptionCard()`、`applyOption()` 等可重用介面 |
| `GuhuoCard::validate()` / `validateInResponse()` | 否 | 仍由原本邏輯處理 `slash` / `normal_slash` / 實際 clone card |
| `JuguanCard::onUse()` / `JuguanVS` | 否 | 仍由原本 `user_string` 與 `Self->getTag("juguan")` 驅動 |
| Lua `SmartAI` 決策 | 否 | 仍讀取 room tag、player mark、card class / objectName，不直接依賴 dialog widget |

## 為什麼不影響 SmartAI

### 1. AI 沒有直接操作 dialog widget

Lua 側找不到直接使用 `GuhuoDialog` / `JuguanDialog` widget 的決策邏輯；只有註冊技能時把 `guhuo_type` / `juguan_type` 綁到技能描述，讓客戶端知道要用哪種對話框類型。

對應位置：

- `lua/sgs_ex.lua` 只做 `skill:setGuhuoDialog(...)`
- `lua/sgs_ex.lua` 只做 `skill:setJuguanDialog(...)`

也就是說，Lua AI 不會去點按鈕，也不會依賴 `popup()` / `exec()` 的視窗生命週期。

### 2. Guhuo 的 AI 依賴的是 room tag 與 card name

`guhuo` 的質疑與出牌邏輯，仍然是透過原本的 room tag 與 card 名稱判斷：

- `lua/ai/wind-ai.lua` 讀 `self.room:getTag("GuhuoType")`
- `lua/ai/wind-ai.lua` 把 `normal_slash` 正規化成 `slash`
- `lua/ai/wind-ai.lua` 仍用 `@GuhuoCard=id:objectName` 這種既有格式組裝 AI 出牌

這次即使把 `GuhuoDialog` 回退成 modal，也沒有更動這些協議。

### 3. Juguan 的 AI 依賴的是 mark 與可用牌型

`juguan` 類技能的 AI，仍然是根據玩家身上的 `*_juguan_remove_*` mark 與實際牌型可用性做決策，例如：

- `lua/ai/NyarzSecond-ai.lua` 讀 `gsjici_juguan_remove_<pattern>`
- `src/package/ol.cpp` 的 `JuguanDialog::isButtonEnabled()` 仍沿用 `objectName() + "_juguan_remove_" + button_name` 的同一套判斷方式
- `src/package/ol.cpp` 的 `JuguanCard::onUse()` 仍然 clone `user_string` 指向的原始牌型

因此目前只有 `Juguan` 把「人類玩家怎麼選」從 modal dialog 改成 Dashboard presenter；AI 看到的規則訊號沒有改。

## 這次真正改到的是什麼

### Guhuo

- 通用 `GuhuoDialog` 不再額外插入 UI 用的 `normal_slash` 候選
- 若本來就有 `slash` / `fire_slash` / `thunder_slash`，就直接按既有分類顯示
- 後端對 `normal_slash` 的兼容分支仍保留，避免影響其他技能或既有流程
- 但選牌 UI 已回退為原本 modal dialog，不再使用 Dashboard presenter

### Juguan

- `JuguanDialog` 保留 presenter 路徑
- 候選牌顯示與選中確認改在 `Dashboard` 完成
- 真正回寫給規則層時，仍是 `applyOption()` 把選中的 `Card *` 塞回 `Self->setTag(objectName(), ...)`
- presenter 顯示期間會封住 `showCardFilterContainer()`，避免手牌分類容器蓋住選項列

## 目前風險邊界

| 項目 | 狀態 | 說明 |
|------|------|------|
| 人類玩家 UX | 已改 | 目前只針對 `Juguan` 改成手牌區單列疊放、hover/selected 上浮；`Guhuo` 回到舊 dialog |
| 客戶端事件時序 | 已改 | `RoomScene` 保留薄 presenter，但目前只對 `JuguanDialog` 啟用 |
| AI 決策模型 | 未改 | Lua `SmartAI` 不需要知道 widget 長相 |
| 伺服器規則結算 | 未改 | `validate()` / `onUse()` / mark / tag 協議保持原樣 |

## 什麼情況下才會真的影響 AI

只有在以下情況才可能波及 `SmartAI`：

- 改掉 `GuhuoType`、`NosGuhuoType`、`Self->setTag("juguan", ...)` 這些既有 tag 協議
- 改掉 `*_guhuo_remove_*` / `*_juguan_remove_*` 這些 mark 命名
- 改掉 AI 目前依賴的 `@GuhuoCard=id:objectName` 或 `#skill:.::pattern` 牌字串格式
- 把目前仍在 C++ 規則層處理的 `slash` / `normal_slash` 正規化挪到另一條不相容的協議上

本輪都沒有做這些事。

## 建議驗證

雖然理論上不影響 `SmartAI`，仍建議做兩個回歸測試：

1. 人機局中讓 AI 使用 `guhuo` / `juguan` 類技能，確認出牌與質疑行為和重構前一致
2. 人類玩家操作 `Juguan` 類技能，確認 presenter 選項與最後實際結算牌型一致
3. 人類玩家操作 `Guhuo`，確認已回到舊 dialog，且不再受手牌區 option layout 影響