-- ============================================================
-- EXAMPLE: on_phase template filled in for s4_cloud_tuxi
-- Shows how an AI Agent fills in [REQUIRED] and [SKILL-SPECIFIC] marks
-- Usage: QSanguosha.exe --lua-test lua/test/templates/EXAMPLE_on_phase_tuxi.lua
-- ============================================================

local SKILL_NAME  = "s4_cloud_tuxi"
local TEST_NAME   = "s4_cloud_tuxi: 出牌階段棄牌/扣血+比較屬性"
local TRIGGER_PHASE = sgs.Player_Play
local runner = sgs.test.create(TEST_NAME)

runner:setup(function(t)
    local players = t:getPlayers()
    if #players < 2 then return end
    local p1 = players[1]
    local p2 = players[2]
    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)
    ROOM:handleAcquireDetachSkills(p2, SKILL_NAME, false)

    -- s4_cloud_tuxi can_trigger: target:isAlive() and target:getPhase() == Play
    -- So it triggers during BOTH players' play phases
end)

runner:run(function(t)
    local players = t:getPlayers()
    if #players < 2 then return end
    local p1 = players[1]
    local p2 = players[2]

    -- When p2's play phase starts, p1 can invoke s4_cloud_tuxi → force yes
    t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))

    -- s4_cloud_tuxi asks: discard OR lose HP
    -- choose "not discard → lose HP" by NOT overriding card (AI auto-fails discard)
    -- then s4_cloud_tuxi asks choice {1,2,3,4} HP values → choose "1"
    t:registerOverride(p1, "choice", SKILL_NAME, sgs.QVariant("1"))

    -- After losing HP, s4_cloud_tuxi compares and steals card → choose card
    t:registerOverride(p1, "card_chosen", SKILL_NAME, sgs.QVariant(1))

    -- Skip both play phases (skill still fires on phase start)
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHasSkill(1, SKILL_NAME)
    t:assertHasSkill(2, SKILL_NAME)
    -- p1 loses 1 HP from skill cost
    t:assertHp(1, 3)
end)

return runner
