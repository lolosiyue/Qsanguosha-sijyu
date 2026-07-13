# Lua AI 規範 (AI Specification)

本文件記錄 `lua/ai/` 下 AI 腳本的撰寫慣例、全域註冊表 API 與最佳實踐。

---

## 1. 檔案結構

### 1.1 層級關係

```
smart-ai.lua          # 核心基底：SmartAI 類別、全域表宣告、工具函數（第一個載入）
PROTECTION_PATTERNS.lua  # 除錯/防崩潰模式範本
{package}-ai.lua      # 套件級 AI 檔案，為各武將技能填入全域註冊表
ai-debug-logger.lua   # AI 除錯日誌工具
```

### 1.2 套件級 AI 檔案慣例

檔案命名：`{包前綴}-ai.lua`，例如 `standard-ai.lua`、`mobile-ai.lua`、`sijyu-ai.lua`、`yjcm-ai.lua`

檔案結構順序：

```
1. 技能註冊（sgs.ai_skills 或 addAiSkills）
2. 技能卡片使用邏輯（sgs.ai_skill_use_func）
3. 優先級與價值設定（sgs.ai_use_priority / sgs.ai_use_value）
4. 仇恨值設定（sgs.ai_card_intention）
5. 觸發決策（sgs.ai_skill_invoke）
6. 選擇決策（sgs.ai_skill_choice / sgs.ai_skill_playerchosen）
7. 棄牌決策（sgs.ai_skill_discard / sgs.ai_skill_cardchosen）
8. 其他修飾函數（sgs.ai_slash_prohibit / sgs.ai_need_damaged / sgs.ai_cardneed 等）
```

每個技能以 `--{技能名}` 註解標記區塊開頭。

---

## 2. 技能註冊（Skill Registration）

AI 技能註冊有兩種方式，效果相同：

### 2.1 傳統方式（舊檔案使用）

```lua
local my_skill = {}
my_skill.name = "my_skill"
table.insert(sgs.ai_skills, my_skill)
my_skill.getTurnUseCard = function(self)
    if self:needBear() then return end
    return sgs.Card_Parse("@MySkillCard=.")
end
```

### 2.2 `addAiSkills` 方式（新檔案推薦）

定義於 `smart-ai.lua:8869`，流暢 API：

```lua
addAiSkills("my_skill").getTurnUseCard = function(self)
    if self:needBear() then return end
    return sgs.Card_Parse("@MySkillCard=.")
end
```

### 2.3 `getTurnUseCard` 回傳慣例

| 情況 | 回傳值 |
|------|--------|
| 可以使用技能 | `sgs.Card_Parse("@CardName=subcard_ids")` |
| 不可使用（條件不符） | `return`（nil） |

```lua
-- 無子卡的技能卡
sgs.Card_Parse("@MyCard=.")
-- 有子卡的技能卡
sgs.Card_Parse("@MyCard=id1+id2+id3")
```

---

## 3. 技能卡片使用邏輯（sgs.ai_skill_use_func）

當 AI 決定使用一張技能卡時，引擎查詢此表：

```lua
sgs.ai_skill_use_func.MySkillCard = function(card, use, self)
    -- card: 技能卡物件
    -- use: sgs.CardUseStruct 引用，設定 use.card 與 use.to
    -- self: SmartAI 實例

    -- 填充 use.card（可沿用傳入的 card 或重新解析）
    use.card = card
    -- 或
    use.card = sgs.Card_Parse("@MySkillCard=id")

    -- 填充 use.to（目標列表）
    use.to:append(target)
end
```

### 常用輔助方法（定義於 SmartAI）

| 方法 | 用途 |
|------|------|
| `self:isFriend(player)` | 判斷是否為友軍 |
| `self:isEnemy(player)` | 判斷是否為敵軍 |
| `self:isWeak(player)` | 判斷是否體力低下 |
| `self:isKongcheng(player)` | 判斷是否空城 |
| `self:needBear()` | 是否需保留手牌 |
| `self:needKongcheng(player)` | 是否需維持空城 |
| `self:getOverflow()` | 手牌溢出量 |
| `self:getCardsNum(cardName)` | 取得指定牌數量 |
| `self:sortByUseValue(cards, desc)` | 按使用價值排序 |
| `self:sortByKeepValue(cards)` | 按保留價值排序 |
| `self:getCardNeedPlayer(cards, ...)` | 最需要這些牌的隊友 |
| `self:findPlayerToDraw(...)` | 適合補牌的目標 |
| `self:doDisCard(player, flags)` | 是否該拆棄目標牌 |
| `self:getDangerousCard(player)` | 危險牌（如八卦陣） |
| `self:getValuableCard(player)` | 有價值牌 |
| `self:AssistTarget()` | 輔助目標（配合技） |
| `self:canAttack(player)` | 能否攻擊目標 |
| `self:canDraw(player)` | 目標能否摸牌 |
| `self:canDiscard(from, flags)` | 能否棄置目標牌 |
| `self:needToThrowArmor()` | 是否需要棄掉防具 |
| `self:willSkipPlayPhase(player)` | 是否會跳過出牌階段 |
| `self:hasSkills(skillList, player)` | 是否有列表中任一技能 |
| `self:ajustDamage(from, to, dmg, card)` | 計算修正後傷害 |
| `hasManjuanEffect(player)` | 是否有滿寵技能影響 |
| `hasJueqingEffect(from, to, nature)` | 是否有絕情效果 |

---

## 4. 全域註冊表（Global Registration Tables）

以下為 `smart-ai.lua:44-134` 定義的核心回呼表。

### 4.1 技能觸發決策

```lua
-- 觸發/不觸發（回傳 true/false）
sgs.ai_skill_invoke.skill_name = function(self, data)
    local damage = data:toDamage()
    return self:isFriend(damage.to)
end

-- 簡寫：總是觸發
sgs.ai_skill_invoke.skill_name = true
```

### 4.2 選項選擇

```lua
sgs.ai_skill_choice.skill_name = function(self, choices, data)
    -- choices: 以 "+" 分隔的選項字串
    -- data: QVariant
    local target = data:toPlayer()
    if self:isFriend(target) then return "recover" end
    return "draw"
end
```

### 4.3 玩家選擇

```lua
sgs.ai_skill_playerchosen.skill_name = function(self, targets)
    -- targets: sgs.SPlayerList（引擎過濾後的候選）
    targets = sgs.QList2Table(targets)  -- 轉為 Lua Table 方便操作
    self:sort(targets, "defense")
    for _, enemy in ipairs(self.enemies) do
        if self:doDisCard(enemy, "he") then return enemy end
    end
    return nil  -- 回傳 nil 表示取消
end

-- 同時設定選擇造成的仇恨變化
sgs.ai_playerchosen_intention.skill_name = function(self, from, to)
    if self:isEnemy(to) then sgs.updateIntention(from, to, 80) end
end
```

### 4.4 棄牌決策

```lua
sgs.ai_skill_discard.skill_name = function(self, discard_num, min_num, optional, include_equip)
    local cards = sgs.QList2Table(self.player:getCards("he"))
    self:sortByUseValue(cards, true)
    local give = {}
    for _, c in ipairs(cards) do
        table.insert(give, c:getEffectiveId())
        if #give >= discard_num then break end
    end
    return give  -- 回傳卡牌 ID 列表
end
```

### 4.5 卡牌選擇（從目標區域選牌）

```lua
sgs.ai_skill_cardchosen.skill_name = function(self, who, flags, method)
    -- who: 目標玩家
    -- flags: "h"（手牌）/ "e"（裝備）/ "j"（判定區）
    local cards = sgs.QList2Table(who:getCards(flags))
    return cards[1]:getId()  -- 回傳卡牌 ID
end
```

### 4.6 卡牌回應（cardask）

```lua
sgs.ai_skill_cardask["@pattern"] = function(self, data)
    -- data: 取決於上下文
    local damage = data:toDamage()
    -- 回傳卡牌 ID 表示打出該牌，回傳 "." 表示取消/無法打出
    for _, card in sgs.qlist(self.player:getHandcards()) do
        if card:isRed() then return card:getEffectiveId() end
    end
    return "."
end

-- 簡寫：總是回應「可」
sgs.ai_skill_cardask["@pattern"] = true
```

### 4.7 複合技能使用（@@）

```lua
sgs.ai_skill_use["@@complex_skill"] = function(self, prompt)
    -- 回傳 Card_Parse 字串或 "."
    return "@@MyCard=id1+id2"
end
```

### 4.8 殺閃禁止

```lua
-- 阻止對目標使用殺（回傳 true 即禁止）
sgs.ai_slash_prohibit.skill_name = function(self, from, to, card)
    if self:isFriend(from, to) then return false end
    return from:getHp() < 2
end
```

### 4.9 手推車選牌

```lua
sgs.ai_skill_askforag.skill_name = function(self, card_ids, data)
    for _, id in ipairs(card_ids) do
        local card = sgs.Sanguosha:getCard(id)
        if card:isKindOf("Peach") then return id end
    end
    return -1  -- 放棄
end
```

### 4.10 給予牌決策

```lua
sgs.ai_skill_askforyiji.skill_name = function(self, card_ids)
    return card_ids  -- 回傳要給出的 ID 列表
end
```

### 4.11 拼點決策

```lua
sgs.ai_skill_pindian.skill_name = function(self, from, to)
    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByKeepValue(cards)
    return cards[1]:getId()  -- 回傳拼點牌 ID
end
```

### 4.12 卡牌需要判斷

```lua
sgs.ai_cardneed.skill_name = function(to, card, self)
    return to:getHandcardNum() < 3 and card:isRed()
end
```

### 4.13 需要受傷判斷

```lua
sgs.ai_need_damaged.skill_name = function(self, attacker, player)
    if attacker and self:isEnemy(attacker, player) and self:isWeak(attacker)
    then return not self:isWeak(player) end
    return false
end
```

### 4.14 蠱惑相關

```lua
sgs.ai_guhuo_card.skill_name = function(self, toname, class_name)
    if class_name == "Slash" then
        local cards = self:addHandPile("he")
        for _, h in sgs.list(cards) do
            if h:isRed() then
                local c = dummyCard(toname)
                c:setSkillName("skill_name")
                c:addSubcard(h)
                return c:toString()
            end
        end
    end
end
```

### 4.15 使用修正

```lua
-- 使用前修正（影響出牌決策）
sgs.ai_use_revises.skill_name = function(self, card, use)
    if card:isKindOf("Slash") and not card:isVirtualCard() then
        sgs.ai_use_priority[card:getClassName()] =
            sgs.ai_use_priority[card:getClassName()] + 3
    end
end
```

### 4.16 動態價值

```lua
sgs.dynamic_value.damage_card.MySkillCard = true
sgs.dynamic_value.control_usecard.MySkillCard = true
sgs.dynamic_value.benefit.MySkillCard = true
sgs.dynamic_value.lucky_chance.MySkillCard = true
```

---

## 5. 卡牌優先級與價值系統

### 5.1 數值設定

```lua
-- 出牌價值（越高越優先使用）
sgs.ai_use_value.MyCard = 6.7
sgs.ai_use_value.MyCard = 0  -- 不使用

-- 出牌優先級（越小越優先）
sgs.ai_use_priority.MyCard = 3.0
sgs.ai_use_priority.MyCard = 2.635  -- 插在武器中間

-- 卡牌保留價值（越高越不想丟）
sgs.ai_keep_value.MyCard = 4.5

-- 武器射程
sgs.weapon_range.MyWeapon = 4

-- 防具價值
sgs.ai_armor_value.MyArmor = function(player, self, card)
    return 6
end

-- 花色優先級
sgs.ai_suit_priority.skill_name = "club|spade|heart|diamond"
```

### 5.2 浮點數間隔約定

| 範圍 | 用途 | 範例值 |
|------|------|--------|
| 0–1 | 棄牌/不重要的操作 | 0, 0.5 |
| 1–2 | 戰術性錦囊 | 1.2, 1.6 |
| 2–3 | 武器裝備 | 2.3, 2.635 |
| 3–5 | 延時錦囊、普通技能 | 3.0, 4.0 |
| 5–8 | 關鍵技能、過河拆橋等 | 5.5, 6.7 |
| 8–9 | 強力技能 | 8.0, 8.2 |
| 9+ | 桃、無中等核心牌 | 9.5, 9.9 |

---

## 6. 仇恨值系統（Intention）

```lua
-- 固定值（負數降低仇恨＝友好，正數增加仇恨＝敵意）
sgs.ai_card_intention.MyCard = -80    -- 非常友好
sgs.ai_card_intention.MyCard = 80     -- 非常敵意
sgs.ai_card_intention.MyCard = 0      -- 中性

-- 函數形式（動態計算）
sgs.ai_card_intention.MyCard = function(self, card, from, tos)
    local intention = -20
    for _, to in sgs.list(tos) do
        sgs.updateIntention(from, to, intention)
    end
end
```

### 常見仇恨值參考

| 情境 | 建議值 |
|------|--------|
| 給隊友補牌/補血 | -80 ~ -100 |
| 對敵人造成傷害 | 80 ~ 100 |
| 棄對手關鍵牌 | 50 ~ 80 |
| 給對手廢牌 | 0 |
| 無差別效果 | 0 ~ 20 |
| 對主公不敬 | +30 額外 |

---

## 7. SmartAI 方法擴展

### 7.1 新增工具方法

```lua
-- 在套件級檔案中直接定義
function SmartAI:myHelper(player, card)
    return self:isFriend(player) and card:isRed()
end
```

### 7.2 覆寫預設方法

```lua
function SmartAI:useCardMyCardType(card, use)
    -- 覆寫 useCardMyCardType 方法
    if not self:canAttack(enemy) then return end
    use.card = card
    use.to:append(enemy)
end
```

### 7.3 套件連接（aiConnect）

```lua
-- 註冊套件連接，讓 useCardByClassName 能查詢套件策略
function aiConnect(player)
    local connects = {}
    -- 預設連接
    -- ...
    return connects
end

-- 在 SmartAI:useCardByClassName 中查詢
sgs.ai_skill_carduse["my_connect"] = function(self, card, use)
    if card:isKindOf("Slash") then
        -- 自訂殺的使用邏輯
        return true  -- 回傳 true 表示接管決策
    end
end
```

---

## 8. 工具函數與全域輔助

### 8.1 定義於 `lua/sgs_ex.lua`

| 函數 | 說明 |
|------|------|
| `sgs.QList2Table(ql)` | QList 轉 Lua Table |
| `sgs.qlist(obj)` | 迭代 QList |
| `sgs.list(obj)` | 通用迭代（支援 QList 與 Table） |
| `RandomList(tbl)` | 隨機順序列表 |

### 8.2 定義於 `smart-ai.lua`

| 函數 | 說明 |
|------|------|
| `addAiSkills(name)` | 註冊 AI 技能（回傳空白表） |
| `isCard(name, card, player)` | 判斷卡牌是否為指定類型（含轉化） |
| `isRolePredictable()` | 身份是否可預測 |
| `getKnownCard(player, ...)` | 獲取已知的特定牌數量 |
| `getCardsNum(name, player, from)` | 計算指定牌總數 |
| `getKnownCards(player, from)` | 獲取所有已知牌 |
| `hasManjuanEffect(player)` | 是否有滿寵技能影響 |
| `hasJueqingEffect(from, to, nature)` | 是否有絕情效果 |
| `dummyCard(name)` | 建立虛擬卡用於判斷 |
| `dumpGameState(room, card)` | 除錯用狀態傾印 |

---

## 9. 卡牌解析（Card_Parse）

技能卡解析格式：

```lua
-- 無子卡（技能卡本身不消耗牌）
sgs.Card_Parse("@CardName=.")

-- 有子卡
sgs.Card_Parse("@CardName=id1+id2+id3")

-- 動態拼接
sgs.Card_Parse("@CardName="..table.concat(id_list, "+"))
```

---

## 10. SmartAI 排序方法

| 方法 | 說明 |
|------|------|
| `self:sort(list, key)` | 按指定 key 排序（hp / defense / handcard） |
| `self:sortByUseValue(cards, desc)` | 按使用價值排序（高→低） |
| `self:sortByKeepValue(cards)` | 按保留價值排序（低→高＝先丟） |
| `self:sortByCardNeed(list)` | 按卡牌需求排序 |

---

## 11. 自訂事件回呼

```lua
sgs.ai_event_callback[event_type].skill_name = function(self, event, player, data, room)
    -- 自訂事件處理
    -- 回傳 true 表示中斷事件鏈
    return false
end
```

---

## 12. 選擇反饋（ai_choicemade_filter）

```lua
sgs.ai_choicemade_filter.cardUsed.skill_name = function(self, player, promptlist)
    -- 卡牌使用後的仇恨調整
end

sgs.ai_choicemade_filter.skillInvoke.skill_name = function(self, player, promptlist)
    -- 技能觸發後的仇恨調整
end

sgs.ai_choicemade_filter.cardChosen.skill_name = function(self, player, promptlist)
    -- 選牌後的仇恨調整
end
```

---

## 13. 命名慣例

| 類別 | 模式 | 範例 |
|------|------|------|
| AI 技能表變數 | `{技能名}_skill` | `nosjujian_skill`、`kuangxi_skill` |
| 技能卡名稱 | `{技能名}Card`（PascalCase） | `NosJujianCard`、`MobileZhiQiaiCard` |
| 解析標記 | `@{技能名}Card` | `@NosJujianCard`、`@MobileZhiQiaiCard` |
| 註冊表鍵（技能） | 全小寫底線 | `nosjujian`、`mobilezhiqiai` |
| 註冊表鍵（卡牌） | PascalCase | `NosJujianCard`、`MobileZhiQiaiCard` |
| 註冊表鍵（cardask） | `"@標記"` | `"@nosenyuan-heart"`、`"@xiaoguo"` |
| 註冊表鍵（複合） | `"@@標記"` | `"@@guowu2"`、`"@@yuqi1"` |
| SmartAI 方法 | camelCase | `useCardByClassName`、`targetRevises` |
| 全域輔助函數 | camelCase | `addAiSkills`、`isCard`、`getKnownCard` |

---

## 14. 檔案載入順序

1. `lua/sgs_ex.lua` — 基礎 API（CreateTriggerSkill 等）
2. `lua/ai/smart-ai.lua` — SmartAI 類別與全域表
3. `lua/ai/{套件}-ai.lua` — 各套件 AI（依賴關係自行處理）

套件級檔案內無明確載入依賴 — 所有 AI 檔案均在伺服器啟動時載入，並填入全域表。

---

## 15. 除錯與穩定性

### 15.1 AI 除錯日誌

```lua
-- 啟用日誌
local AILogger = require "ai.ai-debug-logger"
local logger = AILogger
if logger then logger:init() end

-- 禁用日誌（生產環境）
local logger = nil
```

### 15.2 pcall 保護模式

參考 `lua/ai/PROTECTION_PATTERNS.lua` 為高風險函數添加保護。

### 15.3 狀態傾印

```lua
dumpGameState(room, card)  -- 輸出完整遊戲狀態至 lua/ai/state_dump.log
```

---

## 16. 最佳實踐摘要

1. **優先使用 `addAiSkills`** 而非手動 `table.insert(sgs.ai_skills, ...)`
2. **每個技能一個區塊**，以 `--{技能名}` 註解標記開頭
3. **排序目標**時使用 `self:sort()` 系列方法，而非自行實作
4. **避免硬編碼**卡牌 ID，總是透過 API 動態獲取
5. **設定仇恨值**時考慮身份局特性和主公額外懲罰
6. **回傳 nil / "."** 表示放棄操作，避免傳回無效卡牌 ID
7. **QList → Table 轉換**：使用 `sgs.QList2Table(list)` 或 `sgs.list(list)` 迭代
8. **方法優先**：能用 `sgs.ai_skill_*` 回呼解決的，不新增 SmartAI 方法
9. **尾綴一致性**：套件前綴務必統一（如 `mobile*`、`tenyear*`、`ol*`）
10. **檢查目標有效性**：使用 `target:isAlive()`、`self.player:isProhibited(target, card)` 等
