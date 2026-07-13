-- ============================================================
-- TEMPLATE: on_discard 棄牌/失去牌時觸發的技能
-- Trigger events: CardsMoveOneTime, EventPhaseStart (Phase_Discard)
-- Common pattern: 当你弃置/失去牌时，你可以... / 弃牌阶段开始时...
-- ============================================================
-- Setup: 2 players, skill holder discards or loses cards
-- ============================================================

local SKILL_NAME = "[REQUIRED: skill name]"
local TEST_NAME  = "[REQUIRED: test description]"

local runner = sgs.test.create(TEST_NAME)

runner:setup(function(t)
    local players = t:getPlayers()
    local p1 = players[1]
    local p2 = players[2]

    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)

    -- [OPTIONAL] Give p1 more cards so they must discard in discard phase
    -- for i = 1, 5 do
    --     local ids = ROOM:getNCards(1)
    --     ROOM:obtainCard(p1, sgs.Sanguosha:getCard(ids[1]))
    -- end
end)

runner:run(function(t)
    local players = t:getPlayers()
    local p1 = players[1]
    local p2 = players[2]

    -- Force skill invoke
    t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))

    -- [SKILL-SPECIFIC] If skill asks for choice (e.g. revive, draw, damage):
    -- t:registerOverride(p1, "choice", SKILL_NAME, sgs.QVariant("option"))

    -- [SKILL-SPECIFIC] If skill needs to provide specific cards to discard:
    -- t:registerOverride(p1, "card", SKILL_NAME, sgs.QVariant(1))

    -- [SKILL-SPECIFIC] If skill chooses cards from someone:
    -- t:registerOverride(p1, "card_chosen", SKILL_NAME, sgs.QVariant(1))

    -- [SKILL-SPECIFIC] If skill chooses players:
    -- t:registerOverride(p1, "player_chosen", SKILL_NAME, sgs.QVariant(p2:objectName()))

    -- Skip play phases
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
    t:registerOverride(p2, "activate", "phase", sgs.QVariant("pass"))
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHasSkill(1, SKILL_NAME)

    -- [SKILL-SPECIFIC] Check discard effects:
    -- t:assertHandcardCount(1, 2)    -- discarded cards, fewer in hand
    -- t:assertHp(2, 3)              -- discard dealt damage
    -- t:assertHp(1, 3)              -- discard caused self-damage
    -- t:assertMark(1, "mark", 1)    -- mark applied
end)

return runner
