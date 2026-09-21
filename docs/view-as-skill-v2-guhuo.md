# ViewAsSkillV2：guhuo 選牌視為技（以 s4_ganglu 為例）

本文 `lua/ai/` 路徑指部署檔案；版本與取得方式見[外部 Lua 來源](lua-ai-spec.md#外部-lua-來源)。

本文件示範作者 API，**不是已部署的 s4_ganglu 遷移**。正式 `extensions/scarlet.lua`、
`lua/ai/scarlet-ai.lua` 保持原樣。以下程式尚未經建置或對局驗證。

## 1. 使用哪種寫法

`guhuo_type = "l"` 只負責顯示基本牌宣告框，不會自動實作轉化、二次詢問或次數限制。
「選一張牌當 Slash／Jink／Peach」應讓 `create_card` 返回普通卡；不需要另一個
`CreateSkillCard`，也不需要複製普通卡的三套目標 callback。

| s4_ganglu 舊位置 | V2 寫法 |
|---|---|
| `setGuhuoDialog("l")` | `guhuo_type = "l"` |
| `view_filter` 選一張 | `n = 1`；預設選牌規則即可 |
| `view_as` 的宣告與副牌 | `request:getUserString()`、`getSelectedCardIds()` |
| `on_validate` 建立基本牌 | 無副作用的 `create_card`，client/server 同樣返回普通卡 |
| 回應時選屬性殺、桃／酒 | `cost(skill, room, ctx, request)`，結果放 `ctx.updated_card` |
| 普通卡目標／效果 | 普通卡本身負責；不填 V2 custom target/effect hooks |
| 每回合限一次 | `limit_scope = sgs.Skill_Limit_Turn`、`max_usage_limit = 1` |
| 乒戮重設剛膂 | 正式遷移時同步改成相應 instance 的 `resetUsage(ctx)` |
| 無距離限制 | 保留既有 `s4_ganglu_buff`，生成卡仍設相同 skillName |

## 2. 宣告資料如何傳遞

| 階段 | `request:getUserString()` |
|---|---|
| Qt guhuo 出牌預覽 | 從技能同名 player tag 擷取宣告卡 objectName |
| TUI／Web 等共用建牌入口 | 呼叫端傳入的 `SkillCardBuildRequest.userString` |
| 普通卡送達伺服器、cost／pay | 提交普通卡的 canonical objectName，例如 `fire_slash` |
| 通用 ActiveSkillCard | 保留原本 opaque userString，不改成 `ActiveSkillCard` |

`getTag` 並沒有失效：舊 guhuo 仍把選中的卡存入 `Self:getTag(skillName)`。
Qt V2 預覽現在由框架讀同一個 tag，放入 request，作者不必再重複讀取。
舊寫法也是先從 tag 取得選擇，再用 SkillCard 的 userString 傳给伺服器，並非伺服器直接讀取 Self。
因此「client 可以讀 tag」與「server 使用提交資料重建」可以同時成立。

伺服器不讀 `sgs.Self` 或客戶端 tag。普通卡只傳實際牌名，不保留 `normal_slash` 等
UI 別名或任意複合文字；需要這些額外資料的自訂動作仍用 ActiveSkillCard。
空宣告的回應預覽可由 `request:getPattern()` 選出確定的候選，不能詢問玩家。
`create_card` 必須自行限制允許的牌種、擴展包及 response pattern；宣告不是權限證明。

## 3. 一張牌轉基本牌的骨架

這是拆解寫法的骨架；實際遷移還需接入下一節的 `cost`、原有發動條件、AI 與乒戮重設。
牌種以引擎的 `BasicCard` 類型判斷，不維護基本牌名稱白名單。
回應候選由已註冊引擎卡牌動態列舉；新增基本牌不需逐個修改技能。

```lua
local function gangluCard(skill, request, name)
    local ids = request:getSelectedCardIds()
    if ids:length() ~= 1 then return nil end
    if name == "normal_slash" then name = "slash" end
    local material = sgs.Sanguosha:getCard(ids:first())
    if not material then return nil end
    local card = sgs.Sanguosha:cloneCard(name, material:getSuit(), material:getNumber())
    if not card then return nil end
    card:deleteLater()
    -- clone 成功不代表技能允許：按類型驗證，讓新增基本牌自然適用。
    if not card:isKindOf("BasicCard") then return nil end
    card:addSubcard(ids:first())
    card:setSkillName(skill:objectName())
    return card
end

local function gangluResponseNames(skill, request)
    local names, seen = {}, {}
    -- 按引擎 ID 穩定列舉並去重；不使用固定牌名表或亂數選擇。
    for id = 0, sgs.Sanguosha:getCardCount() - 1 do
        local prototype = sgs.Sanguosha:getEngineCard(id)
        if prototype and prototype:isKindOf("BasicCard") then
            local name = prototype:objectName()
            if not seen[name] then
                seen[name] = true
                local card = gangluCard(skill, request, name)
                if card and sgs.Sanguosha:matchExpPattern(
                    request:getPattern(), request:getInitiator(), card) then
                    table.insert(names, name)
                end
            end
        end
    end
    return names
end

local function gangluPreview(skill, request)
    local name = request:getUserString()
    if name == "" then
        -- 回應預覽沒有 dialog 宣告；先產生確定的普通卡，二次選擇留給 cost。
        name = gangluResponseNames(skill, request)[1] or ""
    end
    local card = gangluCard(skill, request, name)
    if not card then return nil end
    if request:getReason() ~= sgs.CardUseStruct_CARD_USE_REASON_PLAY
        and not sgs.Sanguosha:matchExpPattern(request:getPattern(), request:getInitiator(), card) then
        return nil
    end
    return card
end

s4_ganglu = sgs.CreateViewAsSkillV2 {
    name = "s4_ganglu",
    n = 1,
    response_or_use = true,
    guhuo_type = "l",
    limit_scope = sgs.Skill_Limit_Turn,
    max_usage_limit = 1,

    can_activate = function(skill, request)
        local reason = request:getReason()
        -- 此處接舊 enabled_at_play／enabled_at_response 的基本牌可用性檢查。
        -- 初次按鈕查詢尚未選牌，不能在這裡要求 ids:length() == 1。
        return reason == sgs.CardUseStruct_CARD_USE_REASON_PLAY
            or reason == sgs.CardUseStruct_CARD_USE_REASON_RESPONSE
            or reason == sgs.CardUseStruct_CARD_USE_REASON_RESPONSE_USE
    end,

    create_card = gangluPreview,
    -- cost = 下一節 callback；普通卡不需 can_select_target／targets_feasible。
}
```

預設 `pay` 不會再棄一次普通卡的副牌；副牌由正常卡牌使用／回應管線處理。
只有通用 ActiveSkillCard 的預設支付會自行棄置選牌。

## 4. 回應時再選屬性殺／桃酒

把舊 `on_validate`／`on_validate_in_response` 的 `askForChoice` 移入 `cost`。
不能放在 `create_card`，也不能用 `currentRoom()` 分端，令客戶端返回 proxy、伺服器返回另一類卡。

```lua
cost = function(skill, room, ctx, request)
    local reason, pattern = request:getReason(), request:getPattern()
    if reason == sgs.CardUseStruct_CARD_USE_REASON_PLAY then return true end
    local choices = gangluResponseNames(skill, request)
    local key
    if pattern == "slash" then
        key = "s4_ganglu_slash"
        -- 只有舊 slash 選項的特殊語義需要別名；屬性殺仍動態列舉。
        for i, name in ipairs(choices) do
            if name == "slash" then choices[i] = "normal_slash" end
        end
        table.insert(choices, 1, "slash")
    elseif reason == sgs.CardUseStruct_CARD_USE_REASON_RESPONSE and pattern == "peach+analeptic" then
        key = "s4_ganglu_saveself"
    else
        -- 新增基本牌若同樣符合回應 pattern，也進入同一個選擇流程。
        key = skill:objectName()
    end

    -- 正式版本須先依禁包、Global_PreventPeach、卡牌限制等過濾 choices。
    if #choices == 0 then return false end
    local choice = room:askForChoice(ctx.invoker, key, table.concat(choices, "+"))
    if choice == "slash" then
        -- 保留舊剛膂的「選 slash 時沿用素材殺屬性」語義。
        local material = sgs.Sanguosha:getCard(request:getSelectedCardIds():first())
        if material:isKindOf("Slash") then choice = material:objectName() end
    end
    local card = gangluCard(skill, request, choice)
    if not card or not sgs.Sanguosha:matchExpPattern(pattern, ctx.invoker, card) then return false end

    -- 替換的是本次 execution 的牌；不寫 Room/Player tag，不另開 useCard。
    card:setActivationSkill(skill:objectName(), request:getActivationInstanceId())
    card:setSourceSkill(ctx.use_card:getSourceSkillName(), ctx.use_card:getSourceSkillInstanceId())
    ctx.updated_card = card
    return true
end,
```

此段只示範詢問與替換接點，不是完整合法性過濾器。候選卡仍須符合當前卡牌限制與已選目標；
預覽牌與替換牌的目標規則不同時，必須在 cost 返回 true 前驗證，不能依賴先前預覽的結果。
`normal_slash` 在 clone 前正規化成 `slash`；明確選普通殺時不可再從素材還原成屬性殺。

## 5. 正式遷移前仍需完成

| 項目 | 要求 |
|---|---|
| 發動合法性 | 搬移原 play／response 可用性，檢查禁包、桃限制、回應 pattern |
| 不計次數 | 核對普通卡 history 與 V2 usage 分別如何記錄，不把二者混為同一計數 |
| 原 mark 與乒戮 | 決定沿用自訂計數或改原生 quota；重設與 UI 顯示需一起遷移 |
| AI | 按 §4.12 request-aware 入口提交宣告，不能繼續回舊 `#s4_ganglu` |
| 自動化 | 合法 Slash、禁止／錯誤目標、Jink 純回應、response-use 空草稿、cost 二次選擇 |
| 人工驗收 | guhuo 框、屬性殺、桃酒、取消、回合次數、乒戮重設、實例隔離 |

普通卡重建與原生目標規則的回歸案例尚未執行；Qt／Web／TUI／完整對局未驗收。

相關契約：[V2 遷移規範](active-skill-v2-migration-guide.md)、[Lua 作者 API](lua-ext-spec.md#521-viewasskillv2主動技-v2)、
[AI §4.12](lua-ai-spec.md#412-viewasskillv2-主動技決策)。
