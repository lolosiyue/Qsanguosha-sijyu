-- P1 的作者介面契約：排序、出牌計劃與未覆蓋傳遞。
-- 在真正的 sandbox 裡跑，但只需要 isolated-bootstrap／isolated-facades／decision-core，
-- 不需要 Room、Engine 或任何一支武將 handler。
local function player(name, alive, fields)
    local view = {object_name = name, alive = alive, dead = not alive, handcard_count = 0,
        hp = 3, max_hp = 4, hujia = 0, max_cards = 3, attack_range = 1,
        equips = {}, judging_area = {}, public_marks = {}, skills = {}}
    for key, value in pairs(fields or {}) do view[key] = value end
    return view
end

local function card(id, name, class_name, kinds)
    return {id = id, effective_id = id, name = name, class_name = class_name,
        kind_of = kinds}
end

local function request()
    return {viewer = "viewer", kind = "activate", world_view = {
        mode_id = "use-plan-fixture", revision = "1", current_player = "viewer",
        self = player("viewer", true, {hp = 4}),
        players = {
            player("low", true, {hp = 1, handcard_count = 2}),
            player("high", true, {hp = 5, handcard_count = 0}),
            player("tie", true, {hp = 1, handcard_count = 2})
        },
        player_order = {"viewer", "low", "high", "tie"},
        alive_player_order = {"viewer", "low", "high", "tie"},
        hand_cards = {
            card(7, "slash", "Slash", {"Slash", "BasicCard", "Card"}),
            card(9, "peach", "Peach", {"Peach", "BasicCard", "Card"})
        },
        -- A final proposal is answerable only when the authority has issued a
        -- live, un-limited ticket with complete target rows for that card.
        card_candidates = {
            {card_id = 7, candidate_id = 7007, available = true, limited = false,
                complete_coverage = true, target_fixed = false,
                feasible_with_no_target = false,
                legal_targets = {"low", "high", "tie"},
                target_combinations = {{"low"}, {"high"}, {"tie"}}},
            {card_id = 9, candidate_id = 7009, available = true, limited = false,
                complete_coverage = true, target_fixed = true,
                feasible_with_no_target = true, legal_targets = {},
                target_combinations = {{}}}
        }
    }}
end

local base = request()
local ai = assert(SmartAIView.new(base))
local room, owner = ai.room, ai.player
local hand = owner:getHandcards()
local slash, peach = hand[1], hand[2]
assert(slash:objectName() == "slash" and slash:getClassName() == "Slash")

local function names(players)
    local result = {}
    for index, entry in ipairs(players) do result[index] = entry:objectName() end
    return table.concat(result, ",")
end

-- 相容版 sort 回副本，快照順序不動；同分用 object name 決勝，不用牆鐘。
local roster = room:getAlivePlayers()
local by_hp = ai:sort(roster, "HP")
assert(getmetatable(by_hp) == AIList and by_hp ~= roster)
assert(names(by_hp) == "low,tie,viewer,high")
assert(names(room:getAlivePlayers()) == "viewer,low,high,tie")
assert(names(ai:sort(roster, "HP", true)) == "high,viewer,low,tie")
assert(names(ai:sort(roster, "handcard")) == "high,viewer,low,tie")
assert(names(ai:sort(roster, "maxhp")) == "high,low,tie,viewer")
assert(names(ai:sort(roster, "card")) == "high,viewer,low,tie")
assert(names(ai:sort(roster, "equip")) == "high,low,tie,viewer")
-- 同一個元素比自己一定是 false，否則 table.sort 會在某些輸入上直接報錯。
local same = AIList.new({owner, owner})
assert(ai:sort(same, "HP"):length() == 2)
-- 快照答不出來的鍵不偷偷改排防禦，回 nil 讓呼叫者知道這個鍵沒有覆蓋。
assert(ai:sort(roster, "chaofeng") == nil)
assert(ai:sort(roster, "no-such-key") == nil)
assert(ai:sort(nil, "HP") == nil and ai:sort(owner, "HP") == nil)
-- 不給鍵就是防禦排序，與舊版預設相同。
assert(ai:sort(roster) ~= nil and getmetatable(ai:sort(roster)) == AIList)

-- 牌排序同樣只動副本，同分用牌 ID 決勝。
local sorted = ai:sortByUseValue(hand)
assert(sorted ~= hand and sorted[1] == peach and sorted[2] == slash)
assert(owner:getHandcards()[1]:getId() == 7)
assert(ai:sortByUseValue(hand, true)[1] == slash)

-- 內建策略若沒有 authority candidate 仍是未覆蓋；這不是 declined。
local spare = CardView.new(card(11, "ex_nihilo", "ExNihilo",
    {"ExNihilo", "TrickCard", "Card"}))
local ok, signal = AIUnsupported.capture(function() return ai:aiUseCard(spare) end)
assert(ok == false and AIUnsupported.is(signal) and signal.key == "ExNihilo")
assert(signal.reason == "this request carries no candidate for the ExNihilo")
local plan, status = ai:tryUseCard(spare)
assert(status == "unsupported" and AIUnsupported.is(plan)
    and plan.key == "ExNihilo")
-- 有策略但把原牌替換成未獲授權的最終牌：最終 proposal 仍須有 authority ticket。
-- 輸入牌本身已有合法 ticket；缺的是 revise 後的 final ticket。
ai_card_use.Slash = function(_, _, use)
    use.card = CardView.new(card(99, "fake_unoffered", "FakeUnoffered",
        {"FakeUnoffered", "Card"}))
end
plan, status = ai:tryUseCard(slash)
assert(status == "unsupported" and plan.key == "candidate")
assert(plan.reason == "proposed card has no authority candidate")
ai_card_use.Slash = nil
plan, status = ai:tryUseCard("slash")
assert(status == "unsupported" and AIUnsupported.is(plan))
plan, status = ai:tryUseCard(owner)
assert(status == "unsupported" and AIUnsupported.is(plan))

-- 註冊策略後就是同一組策略：ai_card_use 與舊寫法 useCardXXX 走同一個入口。
ai_card_use.Slash = function(self, used, use)
    use.card = used
    use.to:append(self.room:findPlayerByObjectName("low"))
    return use
end
plan, status = ai:tryUseCard(slash)
assert(status == "planned" and plan.card == slash and getmetatable(plan.to) == AIList)
assert(plan.to:length() == 1 and plan.to:first():objectName() == "low")
local answer = plan:toAnswer()
assert(answer.kind == "use_card" and answer.card_id == 7)
assert(answer.candidate_id == 7007)
assert(#answer.targets == 1 and answer.targets[1] == "low")

-- 舊呼叫方式拿回同一個 use；呼叫者自己帶 use 進來也可以。
local carried = AIUsePlan.new()
assert(ai:aiUseCard(slash, carried) == carried and carried.card == slash)

-- 有策略但決定不打是 declined，與未覆蓋分開：最外層對這兩者的處置不同。
ai_card_use.Peach = function() return nil end
plan, status = ai:tryUseCard(peach)
assert(status == "declined" and plan.card == nil and plan:toAnswer() == nil)

-- 策略自己丟訊號時一樣往外傳，不會被 tryUseCard 讀成 declined。
ai_card_use.Peach = function() ai_unsupported("needs the dying context", "Peach") end
plan, status = ai:tryUseCard(peach)
assert(status == "unsupported" and plan.reason == "needs the dying context")
-- 策略裡真正的程式錯誤不會被當成未覆蓋吞掉。
ai_card_use.Peach = function() local _ = nil + 1 end
assert(not pcall(function() return ai:tryUseCard(peach) end))
ai_card_use.Peach = nil

-- 查找順序：牌名 → 類別名 → useCard<類別名>，先命中的贏。
function SmartAIView:useCardSlash(used, use)
    use.card = used
    use.to:append(self.room:findPlayerByObjectName("low"))
    use.user_string = "method"
    return use
end
ai_card_use.Slash = nil
plan, status = ai:tryUseCard(slash)
assert(status == "planned" and plan.user_string == "method")
ai_card_use.Slash = function(self, used, use)
    use.card = used; use.to:append(self.room:findPlayerByObjectName("low")); use.user_string = "class"
end
plan = ai:aiUseCard(slash)
assert(plan.user_string == "class")
ai_card_use.slash = function(self, used, use)
    use.card = used; use.to:append(self.room:findPlayerByObjectName("low")); use.user_string = "name"
end
plan = ai:aiUseCard(slash)
assert(plan.user_string == "name")
ai_card_use.slash, ai_card_use.Slash = nil, nil
rawset(SmartAIView, "useCardSlash", nil)

-- 覆蓋率報告照實反映註冊了什麼，不是「試試看」。
assert(not ai_coverage.covers("card_use", "Slash"))
ai_card_use.Slash = function(_, used, use) use.card = used end
assert(ai_coverage.covers("card_use", "Slash"))
ai_card_use.Slash = nil

-- use.to 是純 Lua 集合，怎麼改都動不到快照。
local plan_targets = AIUsePlan.new()
plan_targets.to:append(room:findPlayerByObjectName("low"))
plan_targets.to:append(room:findPlayerByObjectName("high"))
assert(plan_targets.to:length() == 2 and not plan_targets.to:isEmpty())
assert(plan_targets.to:removeOne(room:findPlayerByObjectName("low")))
assert(plan_targets.to:first():objectName() == "high")
assert(names(room:getAlivePlayers()) == "viewer,low,high,tie")
assert(#base.world_view.players == 3)
return true
