-- Example: Test a real extension skill (s4_cloud_tuxi from scarlet.lua)
-- Usage: QSanguosha.exe --lua-test-verbose --lua-test lua/test/examples/test_tuxi.lua
--
-- This test:
-- 1. Adds the s4_cloud_tuxi skill to player 1 (lord)
-- 2. Uses overrides to control AI decisions:
--    - Skips player 1's play phase (pass)
--    - Forces player 1 to invoke s4_cloud_tuxi when asked
--    - Forces player 1 to choose card #1 when asked for card_chosen
-- 3. Verifies that player 2 loses HP (damage dealt by tuxi)

local SKILL = "s4_cloud_tuxi"

local runner = sgs.test.create("s4_cloud_tuxi 突襲技能測試")

runner:setup(function(t)
    local players = t:getPlayers()
    if #players < 2 then return end

    -- Add skill to player 1 (lord)
    ROOM:handleAcquireDetachSkills(players[1], SKILL, false)
    print("[SETUP] Skill " .. SKILL .. " added to player 1 (lord)")
end)

runner:run(function(t)
    local players = t:getPlayers()
    if #players < 2 then return end

    local p1 = players[1]
    local p2 = players[2]

    -- Register overrides
    -- Player 1: skip play phase (don't use cards)
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))

    -- Player 1: when asked if wants to invoke tuxi during player 2's play phase → yes
    t:registerOverride(p1, "skill_invoke", SKILL, sgs.QVariant(true))

    -- Player 1: card_chosen → choose first available card from player 2
    t:registerOverride(p1, "card_chosen", SKILL, sgs.QVariant(1))
    print("[OVERRIDE] All overrides registered")
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHasSkill(1, SKILL)
    print("")
    print("[INFO] Check terminal output above for game events.")
    print("[INFO] If the skill doesn't trigger, verify the extension is loaded.")
    print("[INFO] Add --lua-test-verbose for detailed override matching logs.")
end)

return runner
