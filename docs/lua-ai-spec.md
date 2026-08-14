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
`request` 參數，Lua 5.2 會忽略多餘參數，因此舊 AI 不需修改。

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
  `sgs` table；`Player::Phase` 常數由 `Player::staticMetaObject` 的 `QMetaEnum` 反射注入，
  不另維護手寫 key/value 清單。
- `AiLegacyDirectCallbacks`、`AiLegacyAdaptedCallbacks`、`AiIsolatedCallbacks`、
  `AiShadowCallbacks` 可用 `activate`、`askForUseCard` 或 `askForUseCard:skill_name`
  設定 callback 級路由；Room 初始化後路由表凍結。
- `AiIsolatedScripts` 只接受 `lua/ai/isolated/` 下的單一 `.lua` 檔名；腳本在 sandbox
  安裝後由 C++ loader 載入，以 `ai_register_handler(kind, callback)` 註冊 decision handler；
  bootstrap 與腳本頂層執行同樣受 initialization instruction budget 保護，超限時只停用
  該 Room 的 Isolated VM，不阻塞 Room 建立。
- `askForUseCard` 預設進入 Shadow，`activate` 在自己的 Shadow 階段開始前維持
  `LegacyAdapted`。預設 isolated script `ask-for-use-card.lua` 提供 pattern 與 skill handler
  registry；pattern handler 使用 `ai_skill_use[pattern] = function(self, prompt, request)` 註冊。
  `self` 是純值 snapshot facade `SmartAIView`，`self.player` 是 `PlayerView`。`PlayerView`
  依 snapshot 現有的 string／number／boolean 欄位與 legacy 命名規則動態建立 scalar getter；
  `getMark(name)` 是只讀 `public_marks` 的明確 semantic adapter。集合、Room 衍生行為、
  native-returning 與 mutation 方法不會自動生成，查詢時回 `nil`。skill exact handler 優先於
  pattern handler。
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

`request.world_view` 是 request 建立當下的 immutable value snapshot，不含 `Room *`、
`ServerPlayer *`、`Card *`、`QVariant` userdata 或 Lua userdata。

| 欄位 | 內容與可見性 |
|---|---|
| `revision` | 字串形式的 Room revision，必須等於 `request.state_revision` |
| `self` | viewer 的 HP、phase、identity、marks、可見 skill instances 等值資料 |
| `players` | 其他玩家的 public/viewer-visible 摘要；不含手牌 identity |
| `hand_cards` | 僅 viewer 自己的手牌 card ID 與基本 value 欄位 |
| `current_player`／`current_phase` | 當前回合權威狀態 |

國戰未公開的武將、勢力與原生技能不進入其他 viewer 的 snapshot；透過
`only_viewers` 設定的 mark 只會出現在授權 viewer 的 `public_marks`。Lua 端只以 primitive、
array 與 string-key table 讀取 snapshot。

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
