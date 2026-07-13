-- ============================================================
-- TEMPLATE: on_turn 回合相關技能
-- Trigger events: EventPhaseStart (RoundStart), EventTurnStart
-- Common pattern: 回合开始时/每个回合开始时/你的回合开始时/出牌阶段开始时，你可以...
-- ============================================================

local SKILL_NAME = "[REQUIRED: skill name]"
local TEST_NAME  = "[REQUIRED: test description]"

local runner = sgs.test.create(TEST_NAME)

runner:setup(function(t)
    local players = t:getPlayers()
    local p1 = players[1]   -- skill holder
    local p2 = players[2]

    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)

    -- [OPTIONAL] Set up specific starting conditions
    -- ROOM:setPlayerMark(p1, "mark_name", 1)
end)

runner:run(function(t)
    local players = t:getPlayers()
    local p1 = players[1]
    local p2 = players[2]

    -- Force skill invoke
    t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))

    -- [SKILL-SPECIFIC] Choice override:
    -- t:registerOverride(p1, "choice", SKILL_NAME, sgs.QVariant("option"))

    -- [SKILL-SPECIFIC] Card chosen override:
    -- t:registerOverride(p1, "card_chosen", SKILL_NAME, sgs.QVariant(1))

    -- [SKILL-SPECIFIC] Player chosen override:
    -- t:registerOverride(p1, "player_chosen", SKILL_NAME, sgs.QVariant(p2:objectName()))

    -- Skip play phases
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
    t:registerOverride(p2, "activate", "phase", sgs.QVariant("pass"))
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHasSkill(1, SKILL_NAME)

    -- [SKILL-SPECIFIC] Turn start effects:
    -- t:assertHandcardCount(1, 6)   -- drew extra cards
    -- t:assertHp(2, 3)              -- dealt damage
    -- t:assertMark(1, "mark", 1)    -- set a mark

    -- [SKILL-SPECIFIC] Mandatory skill → should always fire:
    -- t:assertNotEqual(handcard_before, handcard_after)
end)

return runner
