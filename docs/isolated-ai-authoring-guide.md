# 新技能的 Isolated AI 寫法

新技能分成「遊戲規則」與「AI 決策」兩份。規則執行與最終合法性仍由 native／gameplay VM 負責；isolated AI 只讀當前觀察者可見的純值快照，提出答案。

## 檔案位置

| 內容 | 外部 extensions 權威倉庫 | L 工作樹 |
|---|---|---|
| 技能規則 | `extensions/<package>.lua` | `extensions/<package>.lua` |
| 新 AI | `ai/isolated/<package>-ai.lua` | `lua/ai/isolated/<package>-ai.lua` |
| 共用測試 | 主倉庫 `tests/lua/` | 同左 |

套件啟用時，loader 會找對應 `<package>-ai.lua`；不要將個別套件加入 `ai_isolated_core`。若設定了 `AiIsolatedScripts`，它會覆寫預設載入清單，須確認有載入測試所需核心與套件腳本。新增 isolated 檔案亦須登記 `docs/ai-runtime-manifest.json`；套件 handler 標為非 required。修改 L 端後逐檔同步回外部倉庫，不將 `lua/ai/` 強制加入主倉庫。

## 選擇入口

| 技能詢問／策略 | 新 AI 入口 |
|---|---|
| 是否發動 | `ai_skill_invoke[reason](self, options, request)` |
| 選項 | `ai_skill_choice[reason](self, options, request)` |
| 選玩家／棄牌 | `ai_skill_playerchosen[reason]`／`ai_skill_discard[reason]`，同一新 ABI |
| 回應出牌 | `ai_skill_use[pattern](self, prompt, request)` |
| 主動技能 | `ai_skill_activate[skill_name](self, request)` |
| 自訂卡牌策略 | `ai_card_use[card_name_or_class](self, card, use)` |
| 估值、保牌、傷害等偏好 | 先找共用 helper 與 `strategy-hooks.lua` 已有 consumer |

鍵值必須對應實際 request 的 reason／pattern／技能名，不一定等於顯示名稱。新 ABI 的 `options` 不是 QVariant；`sgs.ai_skill_*` 相容表另有 legacy ABI，不能混用參數位置。

## 小例子：受傷才選擇回血

假設 gameplay 技能 `demo_recover` 只詢問「是否接受無代價回血」，隨後的 `demo_reward` 提供 `recover`、`draw` 選項。以下只示範這個已明確定義的規則，不是所有回復技能的通用策略。

```lua
-- lua/ai/isolated/demo-ai.lua
ai_skill_invoke.demo_recover = function(self, options, request)
    -- 缺少必要資料時保留未覆蓋，不猜成健康或受傷。
    local hp = type(self.player.getHp) == "function" and self.player:getHp()
    local max_hp = type(self.player.getMaxHp) == "function" and self.player:getMaxHp()
    if type(hp) ~= "number" or type(max_hp) ~= "number" then
        ai_unsupported("demo_recover requires visible HP", "demo_recover")
    end
    return hp < max_hp -- true 發動；false 明確不發動。
end

ai_skill_choice.demo_reward = function(self, options, request)
    local hp = type(self.player.getHp) == "function" and self.player:getHp()
    local max_hp = type(self.player.getMaxHp) == "function" and self.player:getMaxHp()
    if type(hp) ~= "number" or type(max_hp) ~= "number" then
        ai_unsupported("demo_reward requires visible HP", "demo_reward")
    end
    local preferred = hp < max_hp and "recover" or "draw"
    -- 只回覆 authority 實際提供的選項。
    for _, choice in ipairs(options.choices or {}) do
        if choice == preferred then return choice end
    end
    ai_unsupported("demo_reward has no supported offered choice", "demo_reward")
end
```

## 主動／轉化技能

1. 先完成 gameplay 的 V2 技能規則與選牌／目標／成本驗證，參考 [V2 規範](active-skill-v2-refactor-plan.md)。
2. 確認 native request 已提供該技能實例的 conversion ticket。`self:getConversions()`、`self:getConversion(id)` 取得的是純值票，不是可任意造牌的 API。
3. 若結果是既有牌族，優先使用 `self:tryUseCard(conversion)`／`self:aiUseCard(conversion)` 共用規劃。前者返回 `plan, status`：`planned`、`declined`、`unsupported`；未知分支不能被冒充成 pass。
4. 若是新牌族，寫 `ai_card_use` 填入 `use.card` 與 `use.to`（`AIList`）；目標須從當次候選組合選取。只回有完整 authority 支持的計劃。
5. 正規化產生 `card_spec` 後，native 仍驗 conversion ID、activation/source owner 與 instance、成本、request kind、目標與 revision。不要自己拼 legacy 牌字串、`cloneCard` 或 `Card_Parse`。

一般 V2 轉化有現成投影，無須為每個技能新增 host registry。未知成本或 V1 轉化仍可能 `NotCovered`。`hasIndependentAIConversion()` 是 C++ 的嚴格 opt-in：只有固定數量、獨立選牌且不改變生成牌身份／目標規則的技能才可使用，不能為了拿到票而直接回 true。

## 資料與回傳界線

| 情況 | 寫法／含義 |
|---|---|
| 有合法答案 | 回當次 offered choice、ID／target 或授權計劃 |
| 明確不發動 | invoke 回 `false`；其他 request 只在允許拒絕時回 `{kind="pass"}` |
| 未覆蓋 | `ai_unsupported(reason, key)`；不要拿空集合或 0 代替 unknown |
| handler 回 nil | 讓該 dispatcher 繼續其他已接通策略／default；最終仍無答案才是未覆蓋 |
| 關係 | 用 `self:isFriend`／mode policy；未知不等於敵人，不硬編碼角色 |
| 可見資料 | `self.player`、`self.room` 都是 value facade，不是 native Player／Room |
| 跨詢問記憶 | 只存純值、按 viewer 分區；不保留 facade、userdata 或他人私有資料 |

`findPlayerTo*` 只在 projected candidates 上工作，不自行證明 distance／range／prohibition 合法。避免覆寫整個 `ai_decide`，也不呼叫 SmartAI。

## 驗證

至少補「有答案、明確拒絕、必要資料未知、非法候選、借用／多實例身份」適用案例，放到 `tests/lua/` 並接入 native runner。執行適用 focused 契約；V2 conversion 另驗 native 重建與拒絕。分開記錄 registry 登記、契約結果與新技能的完整對局結果。

更多純值 API 與既有契約見 [Lua AI 規範](lua-ai-spec.md)、[共用層](isolated-ai-common-layer.md)、[`tests/lua/isolated-response-use-contract.lua`](../tests/lua/isolated-response-use-contract.lua) 與 [`tests/lua/isolated-strategic-helpers-contract.lua`](../tests/lua/isolated-strategic-helpers-contract.lua)。本文範例未註冊為實際技能，僅供作者對照。
