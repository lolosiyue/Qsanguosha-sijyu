# Isolated AI layer 現況

這份文件描述 room-scoped、viewer-scoped 的 isolated AI 分層，並以目前 source 實作為準。它不是 SmartAI 相容完成宣告，也不代表任意武將 AI parity。

## 分層

| 層 | 目前責任 | 純值界線 |
|---|---|---|
| Native room/runtime | 建立 request、投影 card/target/event、驗證回答、管理生命期 | native pointer、userdata、實際 legality 只在這層取得與驗證，不傳入 Lua facade。 |
| AI decision coordinator | 依 request 建立候選、skill instance/conversion ticket、target projection 與 response authority | 只授權可核對的 card identity、cost、source/activation identity；未知 coverage 保持 incomplete。 |
| `AiLuaRuntime` | 載入 isolated bootstrap/facade、解析純值回答、處理 event delta | 出牌與 RespondCard 都可使用 `card_spec`；其他選擇答案不接受轉化規格。runtime 不依賴 SmartAI。 |
| Lua isolated facades | 提供 viewer-visible world、request、cards、skills、events 與 strategy hooks | 不暴露 Room/player/card pointer、QVariant 或 Lua userdata；hidden data 維持 count/unknown。 |
| `decision-core.lua` | value、use、keep、defense、threat、target helper 與 skill hook 消費 | 只在目前 projected candidates 上排序與選擇；不宣稱 native legality。 |
| mode/event layer | standard mode loader、viewer event projection、atomic intention/memory commit | 每一 observer 使用自己的 visible world；一個 event source，delta 全部成功才 commit。 |

## 目前接線

- standard mode loader 在 `room-runtime.cpp` 建立 mode AI 後註冊 standard mode AI；isolated mode 以 `modeAIUsesIsolatedEvents` 接入 event path。這是 new architecture source 證據，不能以 SmartAI fallback 代替。
- `ask-for-choice.lua` 與 `ask-for-use-card.lua` 已提供 ask-for defaults；`respond_card` 會消費純值 request 與 conversion proposal。
- C++ response conversion 使用實際 active skill instance 建立 authority ticket。Lua 的 `card_spec` 回答需回到 native 端核對 request kind、conversion identity、activation/source identity、cost 與 offered cards；show/pindian 不接受 spec。
- `decision-core.lua` 已消費 `ai_suit_priority`、`ai_card_priority`、`ai_chaofeng`、`ai_weapon_value`、`ai_armor_value`、`ai_skill_defense`、`ai_NeedPeach`，以及 dynamic damage/value inputs；draw/discard/damage/best-damage wrappers 只在 projected candidates 上工作。
- event coordinator 在指定事件階段對每個 observer 產生可見 event/world，runtime 先累積及驗證 delta，再準備單一觀察者狀態；移除指令 hook 後才提交。內部 skill choice/invoke 答案只給當事者，Yiji 不投影私有支付牌 ID。

## bounded policy

state 掃描是每 viewer `O(n + visible state)`，排序是 `O(n log n)`，全 observer event 約 `O(v(n + visible state))`；包作者 hook 的成本另計。target/candidate projection 共用 request budget 16384、conversion probes 512、單 request conversions 64、event log 64、單事件 delta 64。超出上限會保留 incomplete/unknown，不能當作沒有候選或合法性已知。

## 明確未完成項

完整 legacy V1 view-as/轉化 callback、個別 skill hook 的 native API、完整 cardEffect/prohibition/distance/range/usage legality、秘密資訊與所有逐武將策略，仍是 native/coverage debt。相容名稱、registry、facade 或 fallback 不足以宣稱完成；本層目前不能稱完整任意武將 AI parity。

2026-09-21 授權檢查點已通過 SWIG 重新生成、Debug engine／server／runtime runner 編譯、完整 `ai-common`（exit 0，112 秒）及 `card-lifetime`（exit 0，407 秒）。未執行 CTest 或完整對局。詳見 [共用入口盤點](isolated-ai-common-inventory.md)；新技能作者見 [撰寫指南](isolated-ai-authoring-guide.md)。
