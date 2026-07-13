-- Example: Skill test with override
-- Usage: QSanguosha.exe --lua-test lua/test/examples/test_override.lua
--
-- Demonstrates how to:
-- 1. Add a specific skill to a player
-- 2. Use registerTestOverride() to control AI decisions
-- 3. Assert outcomes after the game
--
-- Players are pre-created (2 sujiang, lord+rebel).
-- The game ends after lord's first turn.

local SKILL_NAME = "test_skill"

local runner = sgs.test.create("Override 技能測試")

runner:setup(function(t)
    local players = t:getPlayers()
    if #players < 2 then
        return
    end

    local p1 = players[1]

    -- Assign the skill to player 1 (lord)
    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)
    print("[SETUP] Added skill: " .. SKILL_NAME .. " to player 1")
end)

runner:run(function(t)
    local players = t:getPlayers()
    if #players < 2 then
        return
    end

    local p1 = players[1]
    local p2 = players[2]

    if not p1:hasSkill(SKILL_NAME) then
        print("[WARN] Player 1 does not have skill: " .. SKILL_NAME)
        print("[WARN] Check that the extension is loaded and skill name is correct")
        return
    end

    -- Register overrides BEFORE the game starts
    -- Format: t:registerOverride(player, queryType, key, answer)
    -- queryType values:
    --   "skill_invoke"  - asks for skill invoke → QVariant(bool)
    --   "choice"        - asks for choice       → QVariant(string)
    --   "card"          - asks for card         → QVariant(int cardId) or QVariant(string parse)
    --   "card_chosen"   - asks for card chosen  → QVariant(int cardId)
    --   "player_chosen" - asks for player       → QVariant(string objectName)
    --   "activate"      - play phase            → QVariant("pass" to skip)

    -- Skip play phase for player 1 (don't use cards, just pass)
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
    print("[OVERRIDE] Player 1: skip play phase (pass)")
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHp(1, 4)
    t:assertHp(2, 4)
end)

return runner
