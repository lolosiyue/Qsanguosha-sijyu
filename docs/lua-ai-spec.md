# Lua AI 規範 (AI Specification)

本文件記錄 `lua/ai/` 下 AI 腳本的撰寫慣例、全域註冊表 API 與最佳實踐。

---

## 1. 檔案結構

### 1.1 層級關係

```
smart-ai.lua          # 核心基底：SmartAI 類別、全域表宣告、工具函數（第一個載入）
PROTECTION_PATTERNS.lua  # 除錯/防崩潰模式範本
{package}-ai.lua      # 套件級 AI 檔案，為各武將技能填入全域註冊表
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

定義於 `smart-ai.lua:8803`，流暢 API：

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
| `hasManjuanEffect(player)` | 是否有滿寵技能影響（定義於 `lua/ai/bgm-ai.lua`） |
| `hasJueqingEffect(from, to, nature)` | 是否有絕情效果 |

---

## 4. 全域註冊表（Global Registration Tables）

以下為 `smart-ai.lua:62-152` 定義的核心回呼表。

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
sgs.ai_skill_pindian.skill_name = function(minusecard, self, requestor, maxcard, mincard)
    -- callback 必須回傳 Card 物件；數字 ID 不會通過 SWIG Card* 轉換。
    if self:isEnemy(requestor) then return maxcard end
    return mincard or minusecard
end
```

AI 自己發起拼點時，`SmartAI:askForPindian` 會優先讀取
`self.<reason>_card`；主動技能決策應在提交使用時保存 Card 物件。

### 4.12 ViewAsSkillV2 主動技決策

ViewAsSkillV2 不另設平行 callback 表；出牌階段沿用 `ai_fill_skill`／
`ai_skill_use_func`，特定詢問沿用 `ai_skill_use[pattern]`。新簽名只增加選用的
`request` 參數，Lua 5.4 會忽略多餘參數，因此舊 AI 不需修改。

Play phase 空閒時機：

```lua
sgs.ai_fill_skill.skill_name = function(self, inclusive, request)
    local card = sgs.ActiveSkillCard()
    card:setSkillName("skill_name")
    -- card:addSubcard(id)       -- n > 0 時加入選牌
    -- card:setUserString(value) -- 技能需要 opaque choice 時使用
    return card
end

sgs.ai_skill_use_func.skill_name = function(card, use, self, request)
    local target = self.enemies[1]
    if not target then return end
    use.card = card
    use.to:append(target)
end

sgs.ai_use_value.skill_name = 6
sgs.ai_use_priority.skill_name = 3
sgs.ai_card_intention.skill_name = 80
```

特定 `askForUseCard`／`@@skill` 詢問：

```lua
sgs.ai_skill_use["@@skill_name"] = function(self, prompt, method, pattern, request)
    -- 沒有 request 時是舊版一般詢問路徑。
    if not request then return "." end

    local card = self:getMaxCard()
    local target = self.enemies[1]
    if not card or not target then return end

    return {
        cards = { card:getEffectiveId() },
        targets = { target:objectName() },
        user_string = ""
    }
end
```

| `request` getter | 用途 |
|------------------|------|
| `isValid()` | 是否為可用的 V2 activation request |
| `getReason()` / `getPattern()` | 使用或回應情境 |
| `getPrompt()` / `getHandlingMethod()` | `askForUseCard` 的提示與處理方法 |
| `getInitiator()` | 發起技能的 `ServerPlayer` |
| `getActivationSkillName()` / `getActivationInstanceId()` | 玩家實際使用的技能入口 |
| `getSourceSkillName()` / `getSourceInstanceID()` | root source 技能實例 |
| `isActivationQuotaAvailable()` / `isSourceQuotaAvailable()` | 入口與來源配額狀態 |

| 入口 | 舊版回傳 | V2 升級回傳 |
|---|---|---|
| `ai_fill_skill` | Card 或 Card 陣列 | 相同；第三參數可讀取 request |
| `ai_skill_use_func` | 修改 `use` | 相同；第四參數可讀取 request |
| `ai_skill_use[pattern]` | 卡牌使用字串或 `"."` | `{cards, targets, user_string}`；第五參數可讀取 request |

回應轉化的 `ai_cardsview`／`ai_cardsview_valuable` 同樣沿用原 registry 與回傳格式，
新式 callback 可讀取第 4 個選用 `request`：

```lua
sgs.ai_cardsview.skill_name = function(self, class_name, player, request)
    if class_name ~= "Jink" then return end
    if request and request:getReason() ~= sgs.CardUseStruct_CARD_USE_REASON_RESPONSE then
        return
    end
    return "jink:skill_name[no_suit:0]=."
end
```

舊三參數 callback 無需修改。V2 會先按當前 response／response-use 情境建立 request，
再依序查 activation skill callback、source skill callback；回傳牌會補上 request 的
activation/source instance 身分。legacy ViewAsSkill 仍使用 `isEnabledAtResponse()`，不建立 request。
舊字串本身不承載 instance ID；`SmartAI:askForCard()` 會在第二回傳值附上該候選牌的
request，LuaAI bridge 複製 request 並補回精確身分後才交給 Room 權威驗證。舊 AI 只回傳
單一字串時，第二值自然為 nil；bridge 會依 activation skill 名稱及同一 reason／pattern
重建 request，因此保持相容。

`ai_skill_use` 的舊字串仍只解析一次；若字串中的技能名符合當前 activation 或 source，
Room 會補上權威 instance 身分再驗證。結構化表的 `cards` 必須是整數 ID 陣列，
`targets` 必須是玩家 objectName 陣列；欄位型別錯誤會 fail-closed。

`ActiveSkillCard` 的 `ai_skill_use_func`、`ai_use_value`、`ai_use_priority` 與
`ai_card_intention` 均以 `card:getSkillName()` 索引。`ai_card_intention` 可使用固定數值或
既有的函數簽名；attached activation 未設定專屬項目時會先回退 source skill，最後才回退
共用的 `ActiveSkillCard` 類別項目。

| 時機 | AI 入口 | 回傳／提交格式 |
|---|---|---|
| Play phase 空閒 `activate` | `ai_fill_skill` → `ai_skill_use_func` | `ActiveSkillCard`／普通轉化 Card + `CardUseStruct` |
| 特定 `askForUseCard`／回應詢問 | `ai_skill_use[pattern]` | 舊字串或 `{cards, targets, user_string}` |
| 回應牌列舉／估值 | `ai_cardsview_valuable` → `ai_cardsview` | 舊卡牌字串／字串陣列；第 4 參數可讀取 request |

`ActiveSkillCard` 只承載 AI 選出的 subcards、targets 與 user string；Room 仍會按 skill name
解析 activation instance，重跑 `resolveActiveSkillRequest()` 並建立權威最終 Card。同名多實例
時，`fillSkillCards()` 會先取得第一個通過 `canActivate`／配額檢查的 request，把 activation
與 source instance 寫入虛擬牌，並將同一 request 傳給後續 `ai_skill_use_func`；沒有可用
instance 時不加入候選牌。attached 入口沒有專屬 fill／use_func 時會回退 source skill callback。

### 4.13 卡牌需要判斷

```lua
sgs.ai_cardneed.skill_name = function(to, card, self)
    return to:getHandcardNum() < 3 and card:isRed()
end
```

### 4.14 需要受傷判斷

```lua
sgs.ai_need_damaged.skill_name = function(self, attacker, player)
    if attacker and self:isEnemy(attacker, player) and self:isWeak(attacker)
    then return not self:isWeak(player) end
    return false
end
```

### 4.15 蠱惑相關

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

### 4.16 使用修正

```lua
-- 使用前修正（影響出牌決策）
sgs.ai_use_revises.skill_name = function(self, card, use)
    if card:isKindOf("Slash") and not card:isVirtualCard() then
        sgs.ai_use_priority[card:getClassName()] =
            sgs.ai_use_priority[card:getClassName()] + 3
    end
end
```

### 4.17 動態價值

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

### 8.1 定義於 `lua/utilities.lua`

由 `lua/sanguosha.lua` 以 `dofile` 載入（sanguosha.lua:16）。

| 函數 | 說明 |
|------|------|
| `sgs.QList2Table(ql)` | QList 轉 Lua Table |
| `sgs.qlist(obj)` | 迭代 QList |
| `sgs.list(obj)` | 通用迭代（支援 QList 與 Table） |
| `RandomList(tbl)` | 隨機順序列表（定義於 `extensions/addFunction.lua`） |

### 8.2 定義於 `smart-ai.lua`

| 函數 | 說明 |
|------|------|
| `addAiSkills(name)` | 註冊 AI 技能（回傳空白表） |
| `isCard(name, card, player)` | 判斷卡牌是否為指定類型（含轉化） |
| `isRolePredictable()` | 身份是否可預測 |
| `getKnownCard(player, ...)` | 獲取已知的特定牌數量 |
| `getCardsNum(name, player, from)` | 計算指定牌總數 |
| `getKnownCards(player, from)` | 獲取所有已知牌 |
| `hasManjuanEffect(player)` | 是否有滿寵技能影響（定義於 `lua/ai/bgm-ai.lua`） |
| `hasJueqingEffect(from, to, nature)` | 是否有絕情效果 |
| `dummyCard(name)` | 建立虛擬卡用於判斷（SWIG 導出的 C++ 全域函數，`src/core/util.h`） |
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

1. `lua/config.lua` — 設定載入（`src/server/room-runtime.cpp:469`）
2. `lua/sanguosha.lua` — 主載入入口：內部依序 `dofile` `lua/utilities.lua`（工具函數，sgs.QList2Table 等）與 `lua/sgs_ex.lua`（基礎 API，CreateTriggerSkill 等）（sanguosha.lua:16-17）
3. `lua/ai/smart-ai.lua` — SmartAI 類別與全域表（`src/server/room-runtime.cpp:474`）
4. `lua/ai/{套件}-ai.lua` — 各套件 AI（依賴關係自行處理）

套件級檔案內無明確載入依賴 — 所有 AI 檔案均在伺服器啟動時載入，並填入全域表。

---

## 15. AI 執行環境與錯誤處理

### 15.1 通用 AIRequest／AIResult

AI callback 不再以技能專用 request/result 互相轉接。`activate` 與 `askForUseCard`
都接收唯讀 `AIRequest`，並回傳可序列化的 `AIResult`；結果至少包含 `decisionId`、
`stateRevision`、標記式 `ActionKind` 與 `CardActionSpec`。ActiveSkillV2 的
activation/source identity 與 quota 只放在 `AIRequest.SkillActionContext`。

`AIResult` 返回後立即複製為 value；不得把 Card 指標、Lua userdata 或 callback 暫存交給
Gameplay。RoomThread 會驗證 result 是否回送同一 request 的 revision、牌 ID、目標 ID
與 quota，失敗即 fail-closed。Room 的權威 gameplay revision ledger 會在牌移動、HP／玩家
屬性、死亡狀態、mark、技能集合／instance state、phase／current、card limitation 等會影響
決策的 mutation 完成後推進；純 request/query、log、animation 與 notification 不推進。
提交時若 Room 已不在 request 的 revision，結果視為 stale 並拒絕。

`Player::flags` 暫不直接推進 revision：既有 `smart-ai.lua` 會在一次 decision 內把同一個
namespace 當推演暫存使用，若視為權威 mutation，所有合法 legacy result 都會被誤判 stale。
需要影響 isolated AI 的新 gameplay 狀態必須使用 typed property、mark、skill instance state
或其他已分類的權威 API；不得新增依賴 generic AI scratch flag 的 isolated decision contract。

`activate` 的 legacy callback 仍可在 callback 期間填寫 `use.card`／`use.to`；bridge 只在
同一 gate 內讀取其牌 ID 與目標 ID，複製成 `AIResult.CardActionSpec` 後才提交，禁止把
`use_card` 指標保存到下一次 callback 或交給 Gameplay。`askForUseCard` 則直接回傳同一
`CardActionSpec` 的結構化欄位。

### 15.2 AI VM 分離與遷移模式

- 每個 Room 的 `AiLuaRuntime` 與 Gameplay Lua VM 分離；Isolated handler 只取得
  value-only request、viewer-scoped `AIWorldView`、decision-scoped `AiRng` 與 `AiData`。
- `LegacyDirect` 僅供過渡；`LegacyAdapted` 將既有 `activate`／`askForUseCard` 結果複製成
  `AIResult`，再走通用 Room 驗證 gate。
- 新 AI 使用 `Isolated`；第一階段 `Isolated Shadow` 以同一 request 與獨立 deterministic
  `AiRng` 計算，只把 official/shadow 差異寫入 bounded audit，不影響正式結果。遷移期間
  同一 Room 可按 callback 混用 `LegacyAdapted` 與 `Isolated`，共享 C++ `AiDataStore`。
- `AiData` 持久化由 C++ `AiDataStore` 管理固定路徑、JSON/大小驗證、process lock 與
  原子寫入；Isolated VM 不取得 raw `io`、`os`、`coroutine` 或 native `sgs` binding，
  只可呼叫 `ai_data.read()`／`ai_data.write(json)`。C++ 會重建只含 primitive enum 的安全
  `sgs` table；`Player::Phase`、`Card::Suit` 與 `Card::HandlingMethod` 常數由各自 `staticMetaObject` 的 `QMetaEnum`
  反射注入，不另維護手寫 key/value 清單。
- `AiLegacyDirectCallbacks`、`AiLegacyAdaptedCallbacks`、`AiIsolatedCallbacks`、
  `AiShadowCallbacks` 可用 `activate`、`askForUseCard` 或 `askForUseCard:skill_name`
  設定 callback 級路由；Room 初始化後路由表凍結。
- `isolated-bootstrap.lua` 只管理 generic handler registry、dispatch 與統一的結果轉換（§15.2.3）；C++ 在 sandbox 安裝後、
  configured scripts 之前 mandatory 載入 `isolated-facades.lua`。後者是整個 Isolated Runtime
  共用的 value facade 層，不屬於 `AiIsolatedScripts` allowlist，也不依賴任何 decision-specific
  dispatcher。
- generic decision handler 以 `ai_register_handler(kind, function(self, request) ... end)` 註冊。
  `activate` 與 `use_card` 都由 bootstrap 先建立純值 `SmartAIView`；`self.player` 是
  `PlayerView`。world view 缺失或 viewer／self 不匹配時不會呼叫 handler，直接回傳
  unhandled。bootstrap、mandatory facade 與 configured scripts 都受 initialization
  instruction budget 保護，超限時只停用該 Room 的 Isolated VM，不阻塞 Room 建立。
- `AiIsolatedScripts` 只接受 `lua/ai/isolated/` 下的單一 `.lua` 檔名；腳本在 mandatory
  runtime 層就緒後由 C++ loader 載入。
- `askForUseCard` 預設進入 Shadow，`activate` 在自己的 Shadow 階段開始前維持
  `LegacyAdapted`。`ask-for-use-card.lua` 只提供 use-card pattern／skill registry 與
  decision-specific dispatch；pattern handler 使用
  `ai_skill_use[pattern] = function(self, prompt, request)` 註冊，legacy 形狀的
  callback 另由 `ai_skill_use_legacy` 註冊（§15.2.3）。`PlayerView`
  依 snapshot 現有的 string／number／boolean 欄位與 legacy 命名規則動態建立 scalar getter；
  集合由明確的 value facade adapter 提供。C++ 內部的 `AISkillView` 只是 DTO，Lua 端只取得
  `SkillView`，不暴露其 native 位址。Room 衍生行為、native-returning 與 mutation 方法不會
  自動生成，查詢時回 `nil`。skill exact handler 優先於 pattern handler，之後才是 prompt 前綴（§15.2.3）。
  尚未註冊或 facade 無法建立的 request 回傳 unhandled，audit 分類為 `NotCovered`，不得
  計入 `Mismatch`。
- 第一個正式 handler 是 `standard-ai.lua` 的 `@@lianying`。目前只覆蓋官方可由
  `AIWorldView` 完整重現的確定分支：自身 phase 不晚於 `Play` 且 `lianying` mark 為 1；
  handler 透過 `self.player` 與 C++ 注入的 `sgs.Player_Play` 判斷，不含 phase magic number；
  需要 move/effect userdata 或友方排序的其餘分支維持 `NotCovered`。
- Shadow audit 以 `NotCovered`、`Match`、`Mismatch`、`Error` 四態分類並維持固定大小的
  累積計數；pattern 與 result payload 只保留 capped value/hash。只有 `Match`／`Mismatch`
  代表可用於穩定度比較的已覆蓋 decision。
- `AIResult` boundary 限制單字串 64 KiB、選牌 2048 張、目標 64 名；Shadow audit 僅保留
  capped value/hash 摘要與有限筆數，避免 payload 從 Lua allocator 放大到 C++ heap。
- AI VM 錯誤、無 handler 或 instruction budget 超限時走該玩家現有 legacy AI fallback；
  memory/instruction 錯誤在 callback 返回後才重建 VM。

#### 15.2.1 AIWorldView

模式／身份 hook 與 Room 身份明示服務的介面、相容界線及驗證狀態見
[AI 身份、陣營與身份明示解耦](ai-identity-mode-decoupling-plan.md)。模式規則與觀察者推測
狀態保留在每個 Room 的 Lua VM，`mode_policy` 僅傳遞已驗證的純值判定至隔離 AI。

`request.world_view` 是 request 建立當下的 immutable value snapshot，不含 `Room *`、
`ServerPlayer *`、`Card *`、`QVariant` userdata 或 Lua userdata。

| 欄位 | 內容與可見性 |
|---|---|
| `revision` | 字串形式的 Room revision，必須等於 `request.state_revision` |
| `self` | viewer 的 HP、phase、identity、marks、可見 skill instances 等值資料 |
| `players` | 其他玩家的 public/viewer-visible 摘要與 skill `correctState`；不含手牌 identity 或私有 skill `state` |
| `hand_cards` | 僅 viewer 自己的手牌 `CardView` 值欄位與 `isKindOf` 型別名稱集合 |
| `current_player`／`current_phase` | 當前回合權威狀態 |
| `player_order`／`alive_player_order` | Room 的玩家名單與存活名單順序，元素只有 object_name；不以 self／players 的排列猜測座次 |

國戰未公開的武將、勢力與原生技能不進入其他 viewer 的 snapshot；透過
`only_viewers` 設定的 mark 只會出現在授權 viewer 的 `public_marks`。Lua 端只以 primitive、
array 與 string-key table 讀取 snapshot。

| Value facade | 純查詢 API |
|---|---|
| `PlayerView` | scalar getters、`getMark(name)`、`hasSkill(name)`、`getEquips()`、`getJudgingArea()`、`getSkills()` |
| `CardView` | `getId()`、`getEffectiveId()`、`objectName()`、`getClassName()`、`getSuit()`、`getNumber()`、`getSkillName()`、`isKindOf(name)`、`isRed()`、`isBlack()` |
| `SkillView` | metadata getters、`getState()`、`getStateValue(key, default)`、`getCorrectState()`、`getCorrectStateValue(key, default)` |
| `RoomView`（`self.room`） | `getMode()`、`getCurrent()`、`getPlayers()`、`getAlivePlayers()`、`getAllPlayers(include_dead)`、`getOtherPlayers(except, include_dead)`、`findPlayerByObjectName(name, include_dead)` |

#### 15.2.2 共用轉接層：request 內物件映射

後續共用入口的全域／原生依賴、快照缺口及分批順序見
[SmartAI 共用轉接層依賴盤點](smart-ai-adapter-dependency-audit.md)；該盤點不代表新增介面已實作或通過執行驗證。

`isolated-facades.lua` 在每次 `SmartAIView.new(request)` 建立獨立 `RoomView` 與玩家映射。
同一 request 的 `self.player`、房間查詢、friends／enemies 共用同一個 `PlayerView`，可用
`==` 比較；不同 request 即使玩家名稱相同也不共用代理或快取。轉接層只使用快照純值，
不引入 `global_room`／`current_self`，不把舊 gameplay 的 `sgs` 或 userdata 搬入隔離 VM。

| 介面／邊界 | 語意 |
|---|---|
| 玩家／卡牌／技能／牌 ID 清單 | 回傳新的 `AIList`（帶 metatable 的連續 Lua array），可排序／移除而不改快照順序；支援 `ipairs`、`#` 與下列值集合介面，並非 SWIG QList |
| `getAllPlayers`／`getOtherPlayers` | 與 RoomRoster 一樣從當前玩家開始旋轉，預設排除死亡；無當前玩家時保留原生直接返回全名單的行為 |
| `getCurrent()` | 以 current_player 解析同一份玩家映射；沒有當前玩家回 nil |
| `PlayerView:getHandcards()`／`handCards()` | 僅 viewer 可取得 CardView 陣列／牌 ID 陣列；其他玩家或缺少手牌投影回 nil，已知空手牌回空陣列 |
| `PlayerView:getCards(flags)` | 支援 h／e／j 組合，按手牌、裝備、判定區順序；要求未知手牌時整體回 nil，不返回部分牌區假裝完整 |
| 舊快照缺少名單順序 | 相應清單查詢回 nil，不自行按座號或 self 優先重排 |
| 未支援原生能力 | `getRoom()`、`getTag()`、`getPile()`、距離／合法性推演與 mutation API 仍不提供；不得把缺失能力當作安全的預設決策 |

代理型別與集合契約（2026-09-17，程式／契約案例已寫，執行驗證尚未進行）：

| 介面 | 契約 |
|---|---|
| `AIValue.kind(value)` | 建構器建立的代理分為 `player/card/skill/room/ai/request`（`request` 是 §15.2.3 的 legacy request view）；其他值回 nil。不改寫 Lua `type()`，也不以同名方法猜型別 |
| `AIValue.isPlayer/isCard/isSkill` | 明確區分代理種類；原生 userdata 不屬於隔離代理 |
| `AIValue.isList(value)` | 接受 `AIList` 或連續普通 array（含空 array）；拒絕代理、字典、稀疏表及未知 metatable |
| `AIList.new(array)` | 建立淺層集合副本；nil 原樣回 nil；非集合輸入報錯。`AIList.new({})` 表示已知空集合 |
| `length/isEmpty/first/last/at` | `at(0)` 對應 `[1]`；越界／非法索引回 nil；空集合 first/last 回 nil |
| `contains/append/removeOne` | 操作呼叫端集合，元素用 Lua `==` 比較；append 禁止 nil，removeOne 回成功與否。玩家同 request 共用身份；CardView／SkillView 每次查詢為新代理，跨查詢比較應使用公開 ID／instance，而非假定 native pointer 身份 |
| sandbox `sgs.qlist/list/QList2Table` | qlist 回零基索引，list 回一基索引；QList2Table 回普通 array 淺副本並保留 nil。迭代 nil／非集合會報錯，不吞成空集合；只補缺少的 helpers，不覆寫 gameplay VM 既有 SWIG 實作 |
| `PlayerView:hasSkills(names)` | `|` 表示任一組、`+` 表示組內全部；沿用 hasSkill 的可見／有效／instance 語意。技能投影缺失回 nil；不新增 include_lose 等原生能力 |
| `SmartAIView:hasSkills(names, playerOrList)` | 省略第二參數時查自身；單一 PlayerView 回 boolean（資料未知回 nil）；玩家集合回第一個匹配的 PlayerView 或 nil。拒絕非玩家／混合集合 |
| 副本／未知 | 名單修改不改 snapshot 順序；卡牌／技能集合元素也複製 DTO，避免透過 `_view` 改原輸入。缺少 equips／judging_area／skills 投影回 nil；這不是所有代理的深度防寫保證 |

本批僅接入 `SmartAIView:hasSkills` 共用 helper。舊 SmartAI 的 getDisplayCards／getKnownCards／
isCard／aiUseCard 等 userdata guard 與 native 查詢仍待後續分批處理（回呼 ABI 與結果轉換見 §15.2.3），
不能直接把 PlayerView 傳入這些舊入口。未暴露 `sgs.SPlayerList/CardList` 原生建構器。

此檢查點只擴充共用轉接層，不新增技能 handler、不切換 Isolated／Shadow 路由，
也不宣稱整份 SmartAI 可直接在 sandbox 執行。`tests/lua/isolated-adapter-contract.lua`
由既有 room-runtime-isolation suite 在真實 sandbox 載入；原生測試另覆蓋 C++ 順序投影、
序列化及 activate／use_card 共用入口。2026-09-17：程式與契約原始碼完成，尚未建置或執行。

`PlayerView:getSkills()` 一個可見 instance 對應一個 `SkillView`，保留同名多實例與
`instance_id`；`hasSkill("name#instance")` 可精確查詢，invalid instance 不算持有。
`SkillView.state` 是 owner-only：只出現在 `world.self.skills`；其他玩家即使技能可見也不會
取得 private state。`correctState` 則跟隨 skill instance 的既有可見性進入 snapshot。
兩者都會先轉成 JSON-safe 純值，僅接受 null、boolean、有限數字、字串、array 與
string-key object；最大深度 8、最多 1024 個值、單字串最大 64 KiB，不能安全轉換的
`QObject *`／userdata／其他 `QVariant` 型別會被移除。getter 每次回傳 table 副本，Lua
修改副本不會回寫 snapshot；所有 setter 與 `getRoom()` 均為 `nil`。

#### 15.2.3 共用轉接層：回呼 ABI、分派與結果轉換

`ask-for-use-card.lua` 每個 key 只屬於一種 ABI。同一 key 同時註冊到新舊兩張表沒有可驗證
的分派規則，因此在註冊當下報錯，不在分派時擇一；同一張表內覆寫仍允許，維持套件後載入
可覆蓋的既有行為。

| Registry | Callback 形狀 |
|---|---|
| `ai_skill_use[pattern]`、`ai_register_use_card_handler` | `function(self, prompt, request)`；第三參數是本次的純值 request table |
| `ai_register_use_card_skill_handler(skill_name, handler)` | 同上形狀，依 `request.skill_action.activation_skill` 命中 |
| `ai_skill_use_legacy[pattern]`、`ai_register_use_card_legacy_handler` | `function(self, prompt, method, pattern, request)`，與 `SmartAI:askForUseCard` 的呼叫完全相同 |
| `ai_register_use_card_legacy_skill_handler(skill_name, handler)` | legacy 形狀，依 activation_skill 命中 |

分派順序固定為 skill exact → pattern exact → prompt 前綴（`prompt` 第一個 `:` 之前）。
pattern 以 request 的原始字串查表，`!` 保留在 key 內（`ai_skill_use["@@f_yifa!"]` 沿用舊寫
法）；legacy callback 收到的第四參數則是去掉 `!` 的 pattern，與舊實作一致。legacy 在
pattern 與 prompt 之間還有一段 `sgs.cardEffect` 自動拒絕，它需要事件上下文，快照沒有來源，
維持未覆蓋而不以近似條件重現；legacy 尾端的自動找牌需要 B2 候選模型，同樣不在此層重建
（舊實作在帶 request 時本來也直接返回）。

| Handler 回傳 | status | 轉換結果 |
|---|---|---|
| `nil` | `unhandled` | 續查下一層；全部未命中則整個 request 回 unhandled，audit 記 `NotCovered` |
| `"."`、`""` | `declined` | `{kind="pass"}`；pattern 以 `!` 結尾時不接受並續查，與舊規則相同 |
| 其他字串 | `use_card` | `{kind="use_card", card=<原字串>}`；`->targets` 仍由 C++ `CardUseStruct::parse` 處理 |
| `{kind="pass"}` | `pass` | 明示放棄，compulsory 也接受 |
| `{kind="use_card", card/cards/targets/user_string}` | `use_card` | 複製為值型結果 |
| `{accepted=true, cards=…, targets=…}` | `use_card` | 舊結構化答案轉為 `kind="use_card"` |
| `{cards=…, targets=…, user_string=…}` | `use_card` | §4.12 記載的 V2 回傳形狀，無 `accepted` 也視為動作 |
| `{accepted=false}` | `pass` | 明示放棄 |
| 其他型別、`kind`／`accepted`／動作欄位皆無、欄位型別錯誤、稀疏 `cards`／`targets` | 例外 | `AIResultValue.normalize` 直接 error，runtime 記 `AI_RUNTIME_ERROR` 並回退舊 AI，不得當成合法 pass |

`ai_decide` 在回傳前對所有 decision kind 套用同一個 `AIResultValue.normalize`，use-card 分派層
在每一層另外用它區分 unhandled／declined；轉換冪等，兩層都呼叫不改變結果。

明示的 table 結果在此一律接受，不再像 legacy 只在有 request 時才承認 table；`!` 只用來
拒絕 `"."` 這種未表態的放棄，不否決 handler 明講的 pass。`AIResultValue.normalize` 回傳副本，
handler 之後改自己的 table 不影響已提交結果。稀疏集合在 Lua 端就報錯而不交給 C++：
`lua_rawlen` 會把 `{[2]=id}` 讀成空集合，靜默少牌比明確錯誤更難查。

legacy callback 的第五參數是 `AILegacyRequest`，只在 request 帶 `skill_action` 時建立，
與原生 `AiLegacyRequestView` 只在 V2 技能動作存在的條件一致，其餘情況傳 `nil`。

| 介面 | 語意 |
|---|---|
| `isValid()` | viewer 與 activation／source 的 owner、skill、instance（> 0）齊備，對應原生 `SkillInstanceRef::isValid()` |
| `getDecisionId()`／`getStateRevision()` | 與原生一致回字串 |
| `getPattern()`／`getPrompt()`／`getHandlingMethod()` | request 原值；`getPattern()` 保留 `!` |
| `getActivationOwner/SkillName/InstanceId`、`getSourceOwner/SkillName/InstanceID` | 單次 request 的 skill action identity |
| `isActivationQuotaAvailable()`／`isSourceQuotaAvailable()` | 本次 request 的 quota 判定 |
| `getInitiator()` | 回同一 request 的 `PlayerView` 身份投影，不是 `ServerPlayer *`；查不到回 nil |
| `getReason()`／`getDecisionKind()` | 不提供。`CardUseStruct::CardUseReason` 沒有 `QMetaEnum` 可反射，沙箱沒有對應常數；給了 getter 只會讓與 nil 的比較靜默走錯分支，缺方法的呼叫錯誤才是誠實訊號 |

沙箱 `sgs` 另反射 `Card::HandlingMethod`（`sgs.Card_MethodUse` 等），讓 legacy callback 的第三
參數可以比對；其餘 enum 與 `string:split/contains/startsWith` 等工具仍未提供。

此檢查點只定義回呼 ABI、分派與結果轉換，不新增技能 handler、不改路由、不擴大
DecisionKind。契約案例在 `tests/lua/isolated-adapter-contract.lua`（request view 與
normalize），分派與轉換的端到端案例在 `tests/room-runtime-isolation-test.cpp`。
2026-09-17：程式與契約原始碼完成，尚未建置或執行。

#### 15.2.4 共用入口的型別邊界（legacy 側）

`smart-ai.lua` 的共用入口原本以 `type(x)=="userdata"` 當唯一判斷：不是 userdata 就回空集合、
回 false 或直接把值丟給 `sgs.Sanguosha:getCard()`。值代理是 table，會同時踩到這兩種錯誤——
被當成「沒有」，或被當成卡牌 ID 回查 Engine。本批把判斷換成明確辨識，並定義缺失語意。
邊界函式放在 `lua/ai/value-boundary.lua`，由 `smart-ai.lua` 以既有的 `dofile` 方式載入，
本身只用純 Lua，不碰 Engine、Room 或 gameplay 全域，契約測試才能載同一份定義。

| 入口 | 型別邊界 |
|---|---|
| `aiValueKind(value)` | 只有在 `AIValue` 存在（隔離 VM 載入 facade）時回代理種類；gameplay VM 一律 nil，原生分支不受影響 |
| `aiRejectValueView(entry, value)` | 收到代理就以入口名稱報錯，非代理回 false。未支援要當場說明，不回空集合或預設決策 |
| `aiCardId(card)` | userdata、CardView 與純 ID 統一換成 effective id；不是牌回 nil，不把不明型別交給 Engine |
| `aiSkillKey(skill, instance_id)` | 技能身份是「名稱 + 實例」；原生 Skill 沒有 instance getter，由呼叫端提供，SkillView 自帶 |

| 已更新的共用入口 | 對代理的處理 |
|---|---|
| `isCard` | CardView 直接用投影的 `isKindOf` 作答並回傳同一個代理；其餘代理報錯。非 userdata 先經 `aiCardId`，取不到牌就回 nil，不再把 table 交給 `Engine:getCard` |
| `getKnownCards` | 可見性 flag、hand pile 與 `pileOpen` 未投影，代理報錯；不再用空清單表示「沒有已知牌」 |
| `getDisplayCards`／`hasDisplaySkills` | `display_cards` property 未投影，代理報錯 |
| `aiUseCard` | 值型 use builder 待候選契約，代理報錯；不再當成「沒有出牌」 |
| `CardFilter` | 會暫改 Room card mapping（C 類），牌與擁有者代理都報錯；ID 改走 `aiCardId` |
| `sgs.getPlayerSkillList` | 技能清單與主公技判定是原生查詢，代理報錯；快取對拍改用 `aiSkillKey` 比較，同名多實例不再被判為相同 |
| `aiCardKey` | ActiveSkillCard 的技能部分改用 `aiSkillKey`，與其他入口同一種拼法 |

原生輸入的行為不變：新分支只在 `AIValue` 存在時成立，而 gameplay VM 不載入
`isolated-facades.lua`。`isCard` 是唯一例外——取不到牌時由「在 nil 上呼叫方法」改成回 nil。

仍以舊 guard 判斷的 `evaluateWeapon`（`type(card)~="userdata"` 回 -1）與 `needToThrowArmor`
（回 false）屬傷害／防禦族，依盤點要整族一起做值型投影，不在本批。

契約案例在 `tests/lua/value-boundary-contract.lua`，由 room-runtime-isolation suite 以獨立
Lua state 載入（不啟動 Room，也不載 Engine），涵蓋沒有 facade 時的原生分支、代理辨識、
卡牌／技能身份與未支援訊息。2026-09-17：程式與契約原始碼完成，只做 Lua 語法檢查，
未建置、未執行。`lua/ai/smart-ai.lua`（SHA-256 `7BFB480DE8354354D00D628AE6E0CFC88E691333B56BC16BF0A0C19F1C549ED4`）與 `lua/ai/value-boundary.lua`
都不在主倉庫版本控制內。

#### 15.2.5 值型詢問：請求種類、候選與答案

`DecisionKind` 增加 `SkillInvoke`、`Choice`、`Suit`、`Kingdom`、`General`。這些詢問不產生
出牌，request 也不帶 pattern／method，而是帶一份純值 `options`：

| `request.options` | 內容 |
|---|---|
| `reason` | 技能名或詢問原因，同時是 registry 的鍵與路由的 skill 欄位 |
| `choices` | 字串候選陣列（invoke 是 `yes`／`no`，suit 是四個花色名） |
| `default_choice` | 權威端的預設值；沒有預設就不出現這個欄位，不用空字串代表「沒有」 |
| `optional` | 是否可拒答 |
| `min_count`／`max_count` | 數量上下限，本批都是 1 |

答案是 `AIResult::Answer` 加一個字串 `answer`；`Pass` 表示拒答。C++ 轉回各呼叫點原本的
型別：invoke 只認 `yes`，suit 只認四個花色名（認不得就當拒答，不猜），choice／kingdom／
general 直接回字串，候選合法性仍由原呼叫點既有的驗證與隨機退路處理。

handler 以 `(self, options, request)` 註冊，每個 kind 一張表，鍵是 `reason`：

```lua
ai_skill_invoke["skill_name"] = function(self, options, request) return true end
ai_skill_choice["skill_name"] = function(self, options) return options.choices[1] end
```

`ai_skill_suit`、`ai_skill_kingdom`、`ai_general_choice` 同形。沒有註冊就回 unhandled，
交回舊 AI；不在這層編預設答案。舊 `sgs.ai_skill_invoke`／`ai_skill_choice` 的第二參數是
QVariant `data`，在事件上下文投影（見盤點 B3）之前無法跨界，因此這批沒有 legacy 形狀的
adapter，兩張表也不互為 alias。

| Handler 回傳 | 轉換 |
|---|---|
| `nil` | unhandled，audit 記 `NotCovered` |
| `true`／`false` | 只有 `skill_invoke` 可用；true 是 `yes`，false 是拒答 |
| 字串 | 該字串就是答案；空字串是拒答 |
| `{kind="answer", answer="..."}` | 明示答案，空字串視為錯誤 |
| `{kind="pass"}` | 明示拒答 |
| 其他（含 `kind="use_card"`） | 例外；出牌動作不是值型答案 |

路由沿用同一張表：新 kind 沒有預設路由，`routeFor` 回 `LegacyDirect`，所以不設定時行為
與改動前一致。`AiIsolatedCallbacks` 等四份設定的 callback 名稱現在由一份白名單決定，
含 `askForSkillInvoke`、`askForChoice`、`askForSuit`、`askForKingdom`、`askForGeneral`。
Shadow 走同一個 audit。隔離結果過期（decision id 或 revision 不符）直接不採用；legacy 答案
與 `LegacyAdapted` 出牌一樣，按 callback 執行後的 revision 蓋章，這是既有相容路徑。

隔離 handler 出錯或拒答時回退舊 AI 的答案，呼叫點原本的預設值與驗證都不動——錯誤不會
變成一個非法選項。

選牌與選人族用同一個 request／result 模型：`Discard`、`AmazingGrace`、`CardChosen`、
`Yiji`、`PlayerChosen`、`PlayersChosen`。`options` 多兩個純值候選欄位：

| 欄位 | 內容 |
|---|---|
| `card_ids` | 該次詢問提供的牌 ID；discard 是呼叫點已濾掉禁棄與限制後的合法集合 |
| `players` | 候選玩家的 object name；選人族與 yiji 的收牌對象都在這裡 |

`discard` 的 pattern 沿用 `request.pattern`（與出牌詢問同一個欄位），數量上下限在
`min_count`／`max_count`，`optional` 表示可以不選。`card_chosen` 只給 `players`（被選牌的人）
與 `choices`（牌區字母），不給 ID：別人的手牌沒有投影，候選要等牌區投影批次。

| Handler 回傳 | 轉換 |
|---|---|
| 數字 | 只有選牌族可用，等同 `cards = {id}` |
| 數字陣列 | 選牌族的多選 |
| 字串 | 只有選人族可用，等同 `targets = {name}`；空字串是拒答 |
| 字串陣列 | 選人族的多選 |
| `{kind="answer", cards=…, targets=…}` | 明示形式；`yiji` 必須用這種，因為它同時要牌與人 |
| `{kind="pass"}`／`nil` | 拒答／未覆蓋 |
| 型別混用、稀疏陣列、空的 answer 表 | 例外 |

C++ 端再做一次授權檢查：答案裡的牌必須在 `card_ids` 內、玩家必須在 `players` 內，
重複或未提供的目標直接拒絕整個答案，不截斷也不補齊；被拒絕時回退舊 AI，呼叫點原本的
驗證與預設值不動。`players_chosen` 另外檢查數量在 `min_count`／`max_count` 之間。

隔離側 registry 與其他值型詢問同形：`ai_skill_discard`、`ai_skill_askforag`、
`ai_skill_cardchosen`、`ai_skill_askforyiji`、`ai_skill_playerchosen`、
`ai_skill_playerschosen`，鍵一樣是 reason。

回應牌族（`askForCard`、`askForNullification`、`askForCardShow`、`askForPindian`、
`askForSinglePeach`）共用 `RespondCard` 一種 kind，因為結果形狀相同：一張自己手上的牌。
`options.question` 記錄實際是哪個詢問，`options.reason` 是該詢問的 reason／pattern，
`request.pattern` 與 `request.handling_method` 沿用出牌詢問的欄位，`options.card_ids`
是自己的手牌 ID，`options.players` 放該詢問的關係人（無懈的來源與目標、拼點對手、瀕死者）。

這族的舊答案是 `const Card *`，可能是轉化出來的虛擬牌。虛擬牌轉成字串再 parse 回來會換一
個物件，影響回應的驗證與生命週期，因此 **legacy 路由的指標原樣回傳**，不繞經值模型；
Shadow 只為了比對而把官方答案投影成 ID 或字串記進 audit。隔離路由只接受
`{cards = {id}}`，且該 ID 必須是這名玩家手上或裝備區的實體牌，否則整份答案作廢並回退舊
AI。轉化牌（view-as）要等值型出牌與造卡批次，本批不接受。

隔離側 registry 依 `options.question` 再分表：`ai_skill_cardask`、`ai_nullification`、
`ai_cardshow`、`ai_skill_pindian`、`ai_skill_singlepeach`，每張表的鍵一樣是 reason。

`Guanxing` 的答案是兩堆有序的牌：`cards` 是上面那堆、`bottom_cards` 是下面那堆，兩堆合起來
必須剛好是題目給的整組牌（順序可換、不可增刪），所以一定要用明示的 answer 表。
`TriggerOrder` 的候選是 `skill[#instance][:owner]` 字串，答案照字串回傳，由呼叫點沿用原本的
正規化與隨機退路；它的 `skills` 映射與 `data` 仍留在 legacy 呼叫裡，沒有跨界。

至此二十個 AI 公開入口都走同一條通路：`activate`／`askForUseCard` 用出牌結果，其餘用值型
答案。仍留在舊 AI 的能力是轉化牌（view-as）與需要 `QVariant data` 的事件上下文——前者等
值型造牌批次，後者等事件投影批次；這兩種情況隔離 handler 回 unhandled，由舊 AI 作答。

2026-09-17：程式與契約原始碼完成，`tests/lua/isolated-adapter-contract.lua` 補了各 kind 的
轉換案例，`tests/room-runtime-isolation-test.cpp` 補了從 Room facade 到隔離 VM 的端到端案例
（值型詢問、選牌選人的候選授權、回應牌的 request 形狀與未持有牌被拒）；尚未建置或執行。

#### 15.2.6 牌區投影：可見牌、牌堆與位置索引

快照多出四個牌區欄位，全部只放這名 viewer 能看到的內容：

| 欄位 | 內容與可見性 |
|---|---|
| `world.discard_pile` | 棄牌堆的 `CardView` 陣列；公開區，任何人都看得到 |
| `player.known_cards` | 該玩家手上、這名 viewer 可見的牌（整手公開、單張 `visible` flag、或 `visible_<viewer>_<owner>` flag）。viewer 自己的手牌仍在 `world.hand_cards` |
| `player.hand_visible` | 整手是否公開。配合 `handcard_count` 與 `known_cards` 可分辨「已知為空」與「還有未知牌」 |
| `player.piles` | 每個具名牌堆的 `{name, count, open, hand_pile}`；`card_ids` 只在 `open` 時出現 |
| `player.display_cards` | 明置牌 ID；明置是桌面公開資訊 |

| 查詢 | 語意 |
|---|---|
| `PlayerView:getKnownCards()` | viewer 自己回完整手牌，其他人回可見的那些；缺投影回 nil，可見但沒有牌回空集合 |
| `PlayerView:isHandVisible()` | 整手是否公開；沒有這個欄位（例如舊快照）回 nil |
| `PlayerView:getPileNames()`／`getPileCount(name)` | 牌堆存在與大小是公開的；沒有牌堆投影回 nil |
| `PlayerView:getPile(name)` | 只有開放的牌堆回 ID 集合；關閉的牌堆回 nil——那是看不到，不是沒有牌 |
| `PlayerView:getHandPile()` | `wooden_ox` 與 `&` 開頭的牌堆合起來；其中任一堆關閉就整體回 nil |
| `PlayerView:getPileName(id)`／`getDisplayCards()` | 只在可見範圍內回答 |
| `RoomView:getDiscardPile()`／`getDiscardCards()` | 棄牌堆 ID 或 `CardView` |
| `RoomView:getCardOwner(id)`／`getCardPlace(id)`／`isCardKnown(id)` | 位置索引在建立 `RoomView` 時由快照組成（本人手牌、可見牌、裝備、判定區、開放牌堆、棄牌堆）。未進索引的 ID 一律回 nil，不會改用 Engine 補讀 |

`Player::Place` 也由 `QMetaEnum` 反射進沙箱 `sgs`，所以 `getCardPlace` 回的是與 gameplay
同一組常數（`sgs.Player_PlaceHand` 等），不是自訂字串。

索引只在 request 建立時組一次，和其他代理一樣不跨 request 重用；牌移動後舊 request 的
索引就過期，提交時仍由 revision 檢查擋下。

2026-09-17：程式與契約案例完成（`tests/lua/isolated-adapter-contract.lua` 涵蓋未知／部分可見／
已知為空與位置索引），尚未建置或執行。

#### 15.2.7 共用衍生資料：玩家、卡牌、技能與純值工具

共用入口常問的衍生值改由快照提供，缺投影就沒有那個 getter，不會回一個看起來合理的預設值。

| 來源 | 欄位 | 對應查詢 |
|---|---|---|
| `AIPlayerView` | `max_cards`、`hujia`、`attack_range`、`gender`、`lord` | `getMaxCards()`、`getHujia()`、`getAttackRange()`、`getGender()`、`isLord()` |
| `AIPlayerView` | `equip_slots`（槽號→牌 ID，空槽不出現） | `getEquip(slot)`、`hasEquip([slot])`；缺投影回 nil，有投影但空槽回 nil／false |
| `AICardView` | `type_id`、`handling_method`、`virtual_card`、`target_fixed`、`damage_card`、`subcards` | 同名 getter 與 `getSubcards()`／`subcardsLength()` |
| `AISkillView` | `skill_classes`（原生類別鏈）、`frequency`、`lord_skill`、`attached_lord_skill`、`lord_skill_effective` | `getSkillClass()`、`inherits(name)`、`getFrequency()`、`isLordSkill()`、`isLordSkillEffective()` |

`inherits` 只比對快照裡的類別鏈，所以 `LuaTriggerSkill` 也答得出 `TriggerSkill`；沒有類別
投影回 nil，不會退回「不是」。主公技是否生效由權威端算好（`hasLordSkill`），不是由 AI 自己
用身份字串推。

沙箱 `sgs` 反射的枚舉增加 `Card::CardType`、`Skill::Frequency`、`General::Gender`
（加上前面批次的 `Player::Phase`／`Place`、`Card::Suit`／`HandlingMethod`），仍是
`QMetaEnum` 反射，不手抄清單。

`string.split`／`contains`／`startsWith`／`endsWith` 以純 Lua 補進沙箱，與 gameplay VM 同名
同語意，只做字串處理；若環境已有實作則不覆寫。

2026-09-17：程式與契約案例完成，尚未建置或執行。

#### 15.2.8 合法候選契約：可用牌、可選目標與距離

出牌類請求（`Activate`／`UseCard`／`RespondCard`）多帶一份 `request.card_candidates`，
每張自己的牌一筆，由權威端在建立 request 時算好：

| 欄位 | 內容 |
|---|---|
| `card_id` | 該牌的 effective id |
| `available` | `Card::isAvailable(player)`，這次情境下能不能用 |
| `limited`／`jilei` | `isCardLimited(card, method)`／`isJilei`，分別是限制與己方禁棄 |
| `target_fixed` | 目標是否固定，固定時不需要也不接受選目標 |
| `max_targets` | `targetFilter` 回報的票數上限；沒有合法目標時是 0 |
| `legal_targets` | 尚未選任何目標時，通過 `targetFilter` 與禁止技能的目標名單 |

`legal_targets` 是「還沒選任何目標」的狀態；選了第一個目標後合法集合可能縮小，所以
`max_targets` 與 `target_fixed` 一起給，讓 handler 知道還能不能再選。需要「選了 A 之後
B 是否仍合法」這種逐步收斂的查詢，屬於值型推演批次，本批不提供，也不開同步查詢繞過隔離。

`world.distances` 是存活玩家之間的距離表（來源在外層，目標在內層）。距離會受來源自己的
技能與裝備影響，所以由權威端算；`RoomView:distanceTo(from, to)`、`PlayerView:distanceTo`
與 `inMyAttackRange` 只查表，查不到回 nil。自己到自己是 0。

`CandidateView` 是新的代理種類（`AIValue.kind` 回 `candidate`），查詢
`SmartAIView:getCardCandidates()`／`getCardCandidate(id)`；沒有候選投影的請求回 nil，
表示「這次沒問到」，不是「沒有可用的牌」。

轉牌與過濾後的候選（view-as／`CardFilter` 結果）仍不在這裡：它們要先有值型造牌介面，
屬於下一批。

2026-09-17：程式與契約案例完成，尚未建置或執行。

#### 15.2.9 值型出牌與推演暫存

隔離 handler 不造牌，只描述要哪張牌。`use_card` 結果除了舊的 `card` 字串外，可改帶
`card_spec`：

| 欄位 | 內容 |
|---|---|
| `name` | 引擎卡牌名（`slash`、`jink`…），必填 |
| `suit`／`number` | 要用哪個花色點數複製；省略表示交給權威端決定 |
| `skill` | 轉化出這張牌的技能名 |
| `subcards` | 支付的牌 ID |

`card` 與 `card_spec` 只能擇一，兩者都給是錯誤。權威端收到 spec 後才
`Engine::cloneCard` 造牌，並先驗：花色點數在範圍內、技能確實屬於這名玩家（或確實是一個
view-as 技能）、每張 subcard 都是他當下手上或裝備區的牌。任何一項不符就整份結果作廢，
不會造出半張牌。造好的牌以 `setOwnedCard` 交給 `CardUseStruct`，生命週期仍由權威端管，
technique 與既有 ActiveSkillCard proxy 相同；帶 skill action 的請求也會補上 activation／
source 來源。

`dummyCard`／`Card_Parse`／`cloneCard` 這些原生造牌入口仍然不開放給隔離 VM——它們是
C 類。需要虛擬牌就用 spec。

推演暫存：`ai_decide` 每次決策會確保 `request.scratch` 是一張空表。要暫時記「假設先打這張
牌」之類的推演狀態就寫在這裡，它只活在這次 request，不進 Room、不跨 request、也不會被
別的觀察者看到。舊實作用玩家 flag／property／history 當暫存的做法不再需要，也不允許——
那些是權威狀態。

2026-09-17：程式與契約案例完成，尚未建置或執行。

#### 15.2.10 技能實例候選與來源關係

出牌類請求除了原本那個「這次是為哪個實例建的」`skill_action` 之外，另帶
`request.skill_actions`：這名玩家在這次請求下所有可啟動的實例，每筆都由權威端用
`canActivate` 與次數檢查算過。

| 欄位 | 內容 |
|---|---|
| `activation_owner`／`activation_skill`／`activation_instance` | 實際入口的擁有者、技能名與實例 ID |
| `source_owner`／`source_skill`／`source_instance` | 根來源；與 activation 不同就是借用或轉化而來 |
| `activation_quota_available`／`source_quota_available` | 這次是否還有次數 |

`SkillActionView`（`AIValue.kind` 回 `skill_action`）提供同名 getter、`isValid()`、
`isBorrowed()` 與 `toAnswer()`；查詢用 `SmartAIView:getSkillActions()` 或
`getSkillAction(skill, instance)`。同名多實例是不同筆，`getSkillAction("alpha")` 只在沒指定
instance 時回第一筆，不會把兩個實例併成一個。

結果要指名用了哪個實例時，帶 `skill_action = {skill=…, instance=…, owner=…}`（owner 省略
就是自己）。權威端一律重新檢查：owner 必須是提問的玩家，非 `Activate` 的請求還必須命中
`skill_actions` 裡真的提供過的那一筆，接著用 `findSkillInstance` 重建上下文並重跑
`canActivate` 與次數檢查，才會造 proxy。AI 指名只是選擇，不是授權。

2026-09-17：程式與契約案例完成，尚未建置或執行。

#### 15.2.11 值型事件上下文

`RoomThread` 每次 trigger 在通知舊 AI 的同一個位置，另外呼叫
`Room::recordAiEvent`，把事件當下就轉成純值存進每個 Room 的有界紀錄（上限 64 筆，
先進先出）。轉換只讀一次還活著的結構，不保留任何指標；`QVariant`、`Card *`、
`ServerPlayer *` 都不會進入快照。

| `kind` | 來源 | 帶的值 |
|---|---|---|
| `damage` | `DamageStruct` | from／to／amount／nature／reason／牌名與非虛擬牌 ID |
| `card_move` | `CardsMoveOneTimeStruct` | from／to／to_place／張數與牌 ID |
| `card_effect` | `CardEffectStruct` | from／to／牌名 |
| `judge` | `JudgeStruct *` | who／reason／good／判定牌 |
| `dying` | `DyingStruct` | 瀕死者 |
| `card_use` | `CardUseStruct` | from／targets／牌名 |
| `other` | 其他 trigger | 只有 trigger 編號與 target |

每筆都有 `sequence`（Room 內遞增）與 `revision`（當下的權威版本），所以 handler 能分辨
順序，也能看出哪些事件屬於更早的狀態。可見性在錄製當下決定：移入手牌或私有牌堆的
ID 只留給新的持有者，其餘是公開的；建立快照時非該 viewer 的私有 ID 會被移除。

隔離側用 `SmartAIView:getEvents([kind])` 與 `getLastEvent([kind])` 讀，元素是 `EventView`
（`AIValue.kind` 回 `event`），每次查詢是複本，改了不會回寫快照。沒有事件投影的舊快照回
nil（未知），有投影但沒有該類事件回空集合。

這取代了舊 `filterEvent` 直接吃 `QVariant` 的做法：舊路徑仍在（legacy AI 照常收到
`filterEvent`），但隔離側只看這份值型紀錄。「目前正在結算哪張牌」這種即時查詢不提供——
current player 不等於傷害來源或正在結算的 use，要什麼就從事件序列自己判斷。

2026-09-17：程式與契約案例完成，尚未建置或執行。

#### 15.2.12 AI 狀態分層：規則、推測、暫存與持久化

隔離側的狀態明確分成四層，各有自己的生命週期：

| 層 | 放什麼 | 生命週期與邊界 |
|---|---|---|
| 規則註冊 | handler registry（`ai_skill_use`、`ai_skill_choice`…） | 載入腳本時建立，整個 Room VM 共用；不放對局資料 |
| 觀察者推測 | `ai_memory`（意圖、身份猜測、回合統計…） | 跨 request，但按 viewer 分區；只收純值，VM 重建即消失 |
| 決策暫存 | `request.scratch` | 只活在這次決策，推演寫這裡 |
| 持久化 | `ai_data.read()`／`write()` | 由 C++ `AiDataStore` 管路徑、大小與原子寫入 |

`ai_memory` 的規則：`remember(viewer, key, value)` 只接受 number／string／boolean 與由它們
組成的表（深度 8、值 1024、每位觀察者 256 筆），拒收代理、函式、userdata 與帶 metatable 的
物件——所以不可能把 facade、原生指標或別的觀察者的秘密留到下一次決策。存取都是深複製，
改回傳值不會影響記憶。

handler 用 `SmartAIView:remember(key, value)` 與 `recall(key)`，自動綁在這次請求的觀察者上，
讀不到也寫不了別人的記憶。推測會過期，所以另有 `rememberAt`／`recallAt`：連同當時的
`state_revision` 一起存，之後由 handler 自己決定還算不算數——記憶是「相信什麼」，權威資料
一律看當次快照。

VM 因指令上限或記憶體上限重建時，registry 由載入腳本重建，`ai_memory` 歸零。handler 必須
容忍 `recall` 回 nil，不能假設記憶一定在。`current_self`／`global_room` 這類隱式上下文在隔離
VM 不存在，狀態只能從 request、記憶或 `ai_data` 來。

2026-09-17：程式與契約案例完成，尚未建置或執行。

#### 15.2.13 模式、身份與控制鏈

規則關係與推測關係是兩套查詢，互相不頂替：

| 查詢 | 來源 |
|---|---|
| `relationTo`／`isFriend`／`isEnemy`／`getFriends`／`getEnemies`／`objectiveLevel` | 模式規則算出來的 `mode_policy`；模式未覆蓋時一律回 nil |
| `believeRelation(other, relation)`／`believedRelationTo(other)` | 這名觀察者自己的推測，存在 `ai_memory` 並附上當時的 `state_revision` |

`isModeManaged()` 與 `requireModePolicy()` 讓 handler 明確判斷模式有沒有被覆蓋。未覆蓋就是
不知道：不能當成中立，也不能當成沒有敵人——這正是舊實作最容易出錯的地方。

身份與陣營的其他接點：`RoomView:getLord()`（依快照的 `lord` 旗標，沒有主公回 nil）、
`RoomView:getLieges([kingdom])`（存活、非主公，可依勢力過濾）、
`PlayerView:isSameKingdom(other)`（任一方勢力未公開就回 nil，不會判成不同勢力）。

控制鏈（一人多控）由 `controller` 欄位投影：`PlayerView:getController()`、
`isControlledBy(other)` 與 `SmartAIView:sharesController(a, b)`；缺欄位回 nil。

模式規則本身仍在各 Room 的 gameplay VM（`mode-ai.lua`）評估，只把已驗證的純值判定送進隔離
VM，這一層不變。

2026-09-17：程式與契約案例完成，尚未建置或執行。

#### 15.2.14 提交與過期結果

每種結果都要通過同一組檢查才會提交，檢查一律在權威端做：

| 檢查 | 出牌結果（`applyResult`） | 值型答案（`runAnswer` 與各 adapter） | 回應牌（`decideResponse`） |
|---|---|---|---|
| decision ID 與 revision | result 的 ID／revision 必須等於 request，且 request 的 revision 必須仍是當前 revision | 同左；不符就丟掉隔離答案 | 同左 |
| 過期時的處置 | 回 false，維持呼叫點原本的結果 | 改用舊 AI 的答案 | 改用舊 AI 的 `const Card *` |
| 候選授權 | 目標必須存在且不重複；技能實例必須是提供過的候選並重驗 `canActivate` 與次數 | 選牌必須在 `card_ids` 內、選人必須在 `players` 內、數量在上下限內 | 牌必須是這名玩家當下持有的實體牌 |
| 來源 | `card_spec` 的技能歸屬與每張 subcard 的持有都要驗；造牌在權威端 | — | — |
| 最終合法性 | 仍由既有 gameplay 流程（`Room::useCard` 等）做最後驗證 | 呼叫點原本的驗證與預設值不變 | `validateInResponse` 等原路徑不變 |

隔離答案過期時**不會**事後改 stamp 去接受它——這是明文禁止的。舊 AI 的答案則沿用既有
`LegacyAdapted` 相容做法：它是在當前 Room 上即時跑出來的，所以按 callback 執行後的 revision
蓋章。

候選與數量檢查只套用在隔離答案上。舊 AI 的答案維持它在這條路由存在之前的行為：指標原樣
回傳、清單原樣採用，避免新增的檢查改變既有對局結果。這個分界由 `runAnswer` 的
`fromIsolated` 回報。

2026-09-17：程式完成，尚未建置或執行。

#### 15.2.15 通用決策流程（隔離版）

`lua/ai/isolated/decision-core.lua` 是共用演算法的隔離版本，只靠快照與 request 候選運作，
沒有 gameplay 全域，也不查 Engine。個別武將策略仍然用註冊表擴充，不寫進這裡。

| 區塊 | 內容 |
|---|---|
| 估值 | `ai_keep_value`／`ai_use_value`／`ai_use_priority` 值型註冊表（數字或 `function(self, card)`），查不到時用內建基本牌預設；`getKeepValue`／`getUseValue`／`getUsePriority` |
| 排序 | `sortByKeepValue`／`sortByUseValue`／`sortByUsePriority`：在副本上排序，快照順序不動；同分用牌 ID 打破平手，保證決定性 |
| 威脅／防禦 | `getCardsNum(class, player)`（只算看得見的牌）、`getDefense`（HP＋護甲＋可見閃桃＋未知手牌折算）、`isWeak`、`getThreat`（攻擊牌數加上雙向攻擊範圍） |
| 回合候選 | `getTurnUse()`：把 `request.card_candidates` 轉成值型出牌方案（牌、合法目標、上限、優先序、價值），依優先序排好 |
| 選目標 | `pickTargets(use, count)`：模式有覆蓋時先敵後弱再看威脅，沒有敵人就不選；模式未覆蓋時保持候選原順序，不亂猜關係 |
| 出牌流程 | `planTurnUse()` 依序找第一個打得動的方案，回值型結果 `{kind="use_card", card_id=…, targets=…}` |

出牌結果多了一種值型寫法：`card_id` 指自己手上（或裝備區）那張實體牌。`card`、`card_spec`
與 `card_id` 三者只能擇一，權威端收到 `card_id` 時先驗持有，再從 Engine 取那張牌，AI 不經手
任何 Card 物件。

`decision-core.lua` 另註冊了通用 `activate` handler：沒有候選投影時回 unhandled（交回舊 AI），
有候選但沒有可行方案時回 pass。它排在預設腳本裡，但 `activate` 的預設路由仍是
`LegacyAdapted`，所以要等路由切到 Shadow／Isolated 才會實際參與。

2026-09-17：程式與原生案例完成（`decisionCorePlansATurnFromCandidates`），尚未建置或執行。

#### 15.2.16 載入分層與覆蓋率

隔離 VM 的載入順序是固定的四段，每段各有邊界：

| 段 | 內容 | 邊界 |
|---|---|---|
| bootstrap | `ai_register_handler`／`ai_decide`／`ai_memory`／`ai_coverage` | 只有 registry 與分派，不含任何對局知識 |
| sandbox | 受控 `math.random`、`debug.traceback`、`ai_data`、反射出來的安全 `sgs` 枚舉 | `io`／`os`／`package`／`require`／`load` 等一律移除 |
| mandatory facade | `isolated-facades.lua` | 值 facade 與結果轉換，整個 runtime 共用 |
| configured scripts | `AiIsolatedScripts` 允許清單內的 `lua/ai/isolated/*.lua` | 預設是 use-card 分派、值型詢問分派、通用決策核心與 `standard-ai` |

新 VM 因此能獨立啟動：它不載入 `smart-ai.lua`，也不需要 gameplay VM 的任何全域。舊擴展
要進來只能經由上面第四段的允許清單，不能整包 `dofile` 原生依賴。

覆蓋率不用猜：每個 registry 自己向 `ai_coverage.declare(kind, list_keys)` 申報鍵，
`ai_coverage.covers(kind[, key])`、`describe()` 與 `summary()` 回報目前這個 VM 接得住什麼。
沒有申報就是沒覆蓋——切換路由與驗收要看這份報告，而不是「跑跑看有沒有錯」。

回退邊界同樣明確：隔離 handler 回 unhandled、結果過期或執行出錯時，一律由舊 AI 作答，
audit 分別記成 `NotCovered` 與 `Error`，不會靜默吞掉。

2026-09-17：程式與原生案例完成（`coverageReportListsWhatIsWired`），尚未建置或執行。

#### 15.2.17 切換與驗收程序

切換以 callback 為單位，一律 `LegacyDirect/LegacyAdapted → Shadow → Isolated`，靠資料決定而
不是感覺：

| 統計 | 來源 | 判讀 |
|---|---|---|
| 覆蓋 | `ai_coverage.covers(kind, key)` | 沒申報就是沒接，Shadow 也不必開 |
| `NotCovered` | shadow audit | 隔離側沒有 handler；不是差異 |
| `Match`／`Mismatch` | shadow audit | 只有這兩者能拿來比穩定度 |
| `Error` | shadow audit | 隔離 handler 出錯或回傳非法結果 |
| `legacyFallbacks` | `AiShadowAuditSummary::legacyFallbacks` | 走 Isolated 路由但最後由舊 AI 作答的次數（未覆蓋、過期、出錯都算） |

統計同時有全域與逐 callback 兩份（`shadowAuditSummary()` 與
`shadowAuditSummary(callbackName)`／`auditedCallbacks()`），所以「哪個入口還在回退」是可以
列出來的清單，不是模糊印象。

建議的每個入口驗收門檻（需另獲建置與執行授權）：

1. 開 Shadow 跑完整對局，`Error` 必須為 0，`Mismatch` 要能逐筆解釋（不同但同樣合法，或確定
   是舊 AI 的既有缺陷）。
2. `NotCovered` 比例與 `ai_coverage` 的申報一致：沒申報的 key 才允許 NotCovered。
3. 切 Isolated 後 `legacyFallbacks` 只應來自仍未覆蓋的 key；清單要能對得上第 2 點。
4. 隔離性：`tests/room-runtime-isolation-test.cpp` 全綠（含 VM 分離、沙箱封鎖、可見性、
   代理契約、值型詢問、候選授權、決策核心與覆蓋率報告）。
5. 重建：指令／記憶體上限觸發後 VM 重建，`ai_memory` 歸零而決策仍能繼續（既有案例
   `aiInstructionLimitRebuildsRuntime`）。
6. 效能：以 `QSAN_AI_PROBE=1` 比較切換前後單次決策耗時，候選與距離投影不應讓熱路徑退化。

這一節描述的是程序與門檻。實際跑完整對局、量效能與逐筆解釋差異需要建置與執行授權，
在此之前不宣稱任何入口已完成切換。

2026-09-17：統計與覆蓋率機制完成，驗收本身尚未執行。

### 15.3 RoomThread 邊界

`RoomThread` 同步執行 AI callback 與 Gameplay callback。`activate`／`askForUseCard`
在同一 gate 取得 request、執行 callback、驗證 result，再交給既有 `Room::useCard` 或
response resolver；Shadow 結果只能產生 audit value，不得回寫 Room 或等待另一條執行緒。

### 15.4 pcall 保護模式

所有 Lua callback 仍須由 runtime 的受控 `pcall` 邊界包覆；錯誤、nil 或無效型別回傳
均轉為 `AIResult` 失敗並由 C++ runtime 統一記錄，不在 Lua 腳本引入獨立除錯模組。

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
