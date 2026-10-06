# 新技能的 Isolated AI 寫法

新技能分成「遊戲規則」與「AI 決策」兩份。規則執行與最終合法性由 native／gameplay VM 負責；isolated AI 只讀當前觀察者可見的純值快照並提出答案。

移植既有 SmartAI 時，按[移植遵循文件](isolated-ai-migration-playbook.md)建立逐分支策略帳；已知失敗與未驗項見[2026-10-03 驗收](isolated-ai-acceptance-20261003.md)。

## 檔案位置

| 內容 | 外部 extensions 權威倉庫 | L 工作樹 |
|---|---|---|
| 技能規則 | `extensions/<package>.lua` | `extensions/<package>.lua` |
| 新 AI | `ai/isolated/<package>-ai.lua` | `lua/ai/isolated/<package>-ai.lua` |

套件啟用時，loader 會找對應 `<package>-ai.lua`；個別套件不加入 `ai_isolated_core`。`AiIsolatedScripts` 會覆寫預設載入清單，須自行包含所需核心與套件腳本。新增 isolated 檔案須登記 `docs/ai-runtime-manifest.json`（套件 handler 標為非 required）。修改 L 端後逐檔同步回外部倉庫。

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

鍵值對應實際 request 的 reason／pattern／技能名（不一定是顯示名稱）。新 ABI 的 `options` 不是 QVariant；`sgs.ai_skill_*` 相容表是 legacy ABI，參數位置不可混用。

## 例：受傷才選擇回血

假設技能 `demo_recover` 詢問「是否接受無代價回血」，`demo_reward` 提供 `recover`、`draw` 選項：

```lua
-- lua/ai/isolated/demo-ai.lua
local function visible_hp(self, key)
    local hp = type(self.player.getHp) == "function" and self.player:getHp()
    local max_hp = type(self.player.getMaxHp) == "function" and self.player:getMaxHp()
    if type(hp) ~= "number" or type(max_hp) ~= "number" then
        ai_unsupported(key .. " requires visible HP", key)  -- 缺資料保留未覆蓋，不猜成健康或受傷
    end
    return hp, max_hp
end

ai_skill_invoke.demo_recover = function(self, options, request)
    local hp, max_hp = visible_hp(self, "demo_recover")
    return hp < max_hp  -- true 發動；false 明確不發動
end

ai_skill_choice.demo_reward = function(self, options, request)
    local hp, max_hp = visible_hp(self, "demo_reward")
    local preferred = hp < max_hp and "recover" or "draw"
    for _, choice in ipairs(options.choices or {}) do  -- 只回 authority 實際提供的選項
        if choice == preferred then return choice end
    end
    ai_unsupported("demo_reward has no supported offered choice", "demo_reward")
end
```

## 主動／轉化技能

1. 先完成 gameplay 的 V2 技能規則與選牌／目標／成本驗證，參考 [V2 規範](active-skill-v2-refactor-plan.md)。
2. 確認 native request 已提供該技能實例的 conversion ticket；`self:getConversions()`、`self:getConversion(id)` 取得的是純值票。
3. 結果是既有牌族時，用 `self:tryUseCard(conversion)`／`self:aiUseCard(conversion)` 共用規劃；前者返回 `plan, status`（`planned`／`declined`／`unsupported`）。
4. 新牌族寫 `ai_card_use` 填入 `use.card` 與 `use.to`（`AIList`），目標從當次候選組合選取，只回有完整 authority 支持的計劃。
5. `card_spec` 正規化後，native 仍驗 conversion ID、activation／source owner 與 instance、成本、request kind、目標與 revision。不拼 legacy 牌字串、`cloneCard` 或 `Card_Parse`。

一般 V2 轉化有現成投影，不必為每個技能新增 host registry；未知成本或 V1 轉化仍可能 `NotCovered`。`hasIndependentAIConversion()` 是 C++ 的嚴格 opt-in，只用於固定數量、獨立選牌且不改變生成牌身份／目標規則的技能。

## 資料與回傳界線

| 情況 | 寫法／含義 |
|---|---|
| 有合法答案 | 回當次 offered choice、ID／target 或授權計劃 |
| 明確不發動 | invoke 回 `false`；其他 request 只在允許拒絕時回 `{kind="pass"}` |
| 未覆蓋 | `ai_unsupported(reason, key)`；不拿空集合或 0 代替 unknown |
| handler 回 nil | dispatcher 繼續其他已接通策略／default，最終仍無答案才是未覆蓋。值型詢問若 SmartAI 有同理由 hook（C++ 標 `options.context.legacy_hook`），共用 default 讓位給 SmartAI |
| 關係 | 用 `self:isFriend`／mode policy；未知不等於敵人，不硬編碼角色 |
| 可見資料 | `self.player`、`self.room` 是 value facade，不是 native Player／Room |
| 跨詢問記憶 | 只存純值、按 viewer 分區；不保留 facade、userdata 或他人私有資料 |

`findPlayerTo*` 只在 projected candidates 上工作，不自行證明 distance／range／prohibition 合法。不覆寫整個 `ai_decide`，也不呼叫 SmartAI。

## 驗證

驗收 gate 與證據要求見[移植遵循文件第 9 節](isolated-ai-migration-playbook.md#9-檢查點與驗收)；倉庫不新增測試套件或 fixture。行為驗收須實際觸發：有答案、明確拒絕、必要資料未知、非法候選、借用／多實例身份；V2 conversion 另驗 native 重建與拒絕。

更多純值 API 與既有契約見 [Lua AI 規範](lua-ai-spec.md)、[共用層](isolated-ai-common-layer.md)。
