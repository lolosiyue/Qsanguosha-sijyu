-- ============================================================
-- TEMPLATE: on_phase 摸牌/出牌/棄牌階段觸發的技能
-- Trigger events: EventPhaseStart
-- Common trigger phases: Player_Draw, Player_Play, Player_Discard
-- Common pattern: 摸牌阶段开始时/出牌阶段开始时/弃牌阶段开始时，你可以...
-- ============================================================
-- Setup: 2 players, skill holder's turn triggers the skill
-- ============================================================

local SKILL_NAME  = "[REQUIRED: skill name]"
local TEST_NAME   = "[REQUIRED: test description]"
local TRIGGER_PHASE = sgs.Player_Draw  -- [CHANGE] Player_Draw / Player_Play / Player_Discard

local runner = sgs.test.create(TEST_NAME)

runner:setup(function(t)
    local players = t:getPlayers()
    local p1 = players[1]   -- lord: skill holder
    local p2 = players[2]   -- rebel: target (optional)

    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)

    -- [OPTIONAL] Give p1 specific cards for the skill
    -- local cardIds = ROOM:getNCards(3)
    -- for _, id in sgs.qlist(cardIds) do
    --     ROOM:obtainCard(p1, id)
    -- end

    -- [OPTIONAL] Set up marks/flags needed by the skill
    -- ROOM:setPlayerMark(p1, "some_mark", 1)
end)

runner:run(function(t)
    local players = t:getPlayers()
    local p1 = players[1]
    local p2 = players[2]

    -- Force skill invoke when the phase triggers
    t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))

    -- [SKILL-SPECIFIC] If skill asks for choice (e.g. draw vs discard):
    -- t:registerOverride(p1, "choice", SKILL_NAME, sgs.QVariant("option_name"))

    -- [SKILL-SPECIFIC] If skill asks to discard cards:
    -- t:registerOverride(p1, "card", SKILL_NAME, sgs.QVariant(1))

    -- [SKILL-SPECIFIC] If skill asks to choose a card from someone:
    -- t:registerOverride(p1, "card_chosen", SKILL_NAME, sgs.QVariant(1))

    -- [SKILL-SPECIFIC] If skill asks to choose a player:
    -- t:registerOverride(p1, "player_chosen", SKILL_NAME, sgs.QVariant(p2:objectName()))

    -- Skip play phases to speed up the test
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
    t:registerOverride(p2, "activate", "phase", sgs.QVariant("pass"))
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHasSkill(1, SKILL_NAME)

    -- [SKILL-SPECIFIC] Check phase-trigger effects:

    -- Draw phase trigger: check handcard change
    -- t:assertHandcardCount(1, 6)   -- drew extra cards
    -- t:assertHandcardCount(2, 3)   -- target lost card

    -- Play phase trigger: check damage/marks
    -- t:assertHp(2, 3)              -- took damage
    -- t:assertMark(1, "mark", 1)    -- mark applied

    -- Discard phase trigger: check discard effects
    -- t:assertHandcardCount(1, 3)   -- discarded cards

    -- [SKILL-SPECIFIC] Negative test (skill should NOT have fired):
    -- t:assertHp(2, 4)              -- HP unchanged → skill didn't deal damage
    -- t:assertHandcardCount(2, 4)   -- handcard unchanged
end)

return runner
