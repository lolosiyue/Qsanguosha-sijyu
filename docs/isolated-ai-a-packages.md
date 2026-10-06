# A 開頭套件的 isolated AI

這批對應 `smart-ai.lua` 依 package 名稱載入的六個檔案：`AIgeneral`、`AnimationCardpack`、`animecard`、`animic`、`arknights`、`assassins`。原始策略位於 `lua/ai/<package>-ai.lua`；新入口位於 `lua/ai/isolated/<package>-ai.lua`，兩者分別屬於 gameplay 與 isolated VM。

沿用 `AiLuaRuntime::loadPackageScripts()` 的大小寫不敏感套件發現規則。六檔均在 `ai-runtime-manifest.json` 登記為非 required 的 `package_handler`，沒有加入核心腳本清單；使用 `AiIsolatedScripts` override 時仍須自行包含所需套件。`AnimationCardpack_card` 是另一個 package 名稱，本批對應原有 `AnimationCardpack-ai.lua`，不另外建立別名檔。

## 資料與回應

- 詢問上下文新增 `context.player` 及 `context.use`。Native 僅傳玩家 object name、公開牌面（含 subtype）、來源與目標順序；不傳 pointer、支付子牌 ID 或任意 QVariant/tag。Facade 提供 `getDecisionContext()`、`getDecisionData():toPlayer()`／`:toCardUse()`。
- `ai_skill_*` 使用 isolated ABI。實體回應牌使用 ID／`answer.cards`；主動技能綁定 activation instance，轉化使用 authority-issued conversion ticket。原版需要臨時 `cloneCard` 的策略不能用任意手牌替代。
- 可見值不足或所需能力尚未投影時使用 `ai_unsupported`；不是確定拒絕，也不算完整覆蓋。舊版 AI 保留供既有 fallback 路徑使用。
- 排序與價值使用既有 isolated 共用層，共用評分與隱藏資訊估計與 legacy 不盡相同。

## 覆蓋邊界

| 套件 | 已接入的主要策略 | 尚未覆蓋的主要能力 |
|---|---|---|
| AIgeneral | 通義詢問、千問 invoke、會員神力純值分支及策略 hook | 身份推測／私有角色狀態、貼吧制禮傷害 helper、主動轉化與複雜傷害／連環判斷 |
| AnimationCardpack | 武器 invoke、部分棄牌／選項及卡牌策略、數值 hook | 技能臨時 Slash、拼點結果、任意 tag、複雜自訂牌效果及轉化 |
| animecard | 武器價值、reijyuu 的用牌／選人／選項／無懈、tacos 策略 | Se_Elucidator 的技能虛擬 Duel 試算 |
| animic | modao invoke／choice、cuisheng、huanglue、luafenwei、guihao 與 caiduan 的候選入口、yui_changxin | modao tag／handcard_defense、完整 isGoodTarget／willSkipPlayPhase／needToThrowArmor、shenni 移牌資料、未授權轉化與舊意圖 callback |
| arknights | 共振、緘默、劍雨、坍縮、止戈、萬象、逐夜、破曉及傷害 hook；過載／追獵主動入口 | 過載缺 Slash conversion ticket、追獵低體力時對手 canSlash、逐夜 view-as 救援計數、耀陽 Qinggang 試算效果 |
| assassins | 謀潰、藏匿、毒醫、竭緣、斷指等可見資料分支及聲優選項 | 未投影的傷害／Slash helper、身份局勢推測、拼點後續、技能 tag、封印等轉化 |

決策的 `NotCovered` 可走既有 legacy fallback；未移植的事件／意圖 hook 沒有同等 fallback（如 `KaiyuanShengshi` 未在 isolated 註冊）。覆蓋邊界以檔內相鄰的 `ai_unsupported` 為準。本批只涵蓋來源與靜態檢查，未取得建置、執行期或完整對局證據。
