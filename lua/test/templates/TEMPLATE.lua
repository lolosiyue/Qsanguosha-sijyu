-- ============================================================
-- BASE TEMPLATE for Lua skill tests
-- Usage: Copy → replace placeholders → run
--        QSanguosha.exe --lua-test-verbose --lua-test your_test.lua
-- ============================================================
-- SKILL_NAME:   [REQUIRED] skill objectName
-- GENERAL:      [OPTIONAL] general to attach skill to (default: sujiang)
-- TEST_DESC:    [REQUIRED] short test description
-- EVENT:        [OPTIONAL] trigger event description (for logging)
-- ============================================================
-- QUERY TYPE REFERENCE (for registerOverride):
--   "skill_invoke"  → QVariant(bool)
--   "choice"        → QVariant(string option)
--   "card"          → QVariant(int cardId) or QVariant("CardName:suit:number")
--   "card_chosen"   → QVariant(int cardId)
--   "player_chosen" → QVariant(string playerObjName)
--   "activate"      → QVariant("pass" to skip play phase)
-- ============================================================
-- ASSERTION HELPERS (on runner):
--   t:assertHp(playerIndex, expected)
--   t:assertAlive(playerIndex)
--   t:assertHasSkill(playerIndex, skillName)
--   t:assertHandcardCount(playerIndex, expected)
--   t:assertMark(playerIndex, markName, expected)
--   t:assertNotKongcheng(playerIndex)
-- ============================================================

local SKILL_NAME = "[REQUIRED: your skill name]"
local TEST_NAME  = "[REQUIRED: test description]"

local runner = sgs.test.create(TEST_NAME)

-- ============================================================
-- SETUP: configure players, skills, initial state
-- ============================================================
runner:setup(function(t)
    local players = t:getPlayers()
    if #players < 2 then return end

    local p1 = players[1]   -- lord (has the skill)
    local p2 = players[2]   -- rebel (target)

    -- [SKILL-SPECIFIC] Assign the skill to a player
    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)

    -- [OPTIONAL] Assign additional skills or marks
    -- ROOM:handleAcquireDetachSkills(p2, "other_skill", false)
    -- ROOM:setPlayerMark(p1, "some_mark", 1)

    -- [OPTIONAL] Configure custom hand cards
    -- local cardIds = ROOM:getNCards(4)
    -- for _, id in ipairs(sgs.list(cardIds)) do
    --     ROOM:obtainCard(p1, id)
    -- end
end)

-- ============================================================
-- RUN: register overrides to control AI decisions
-- ============================================================
runner:run(function(t)
    local players = t:getPlayers()
    if #players < 2 then return end

    local p1 = players[1]
    local p2 = players[2]

    -- [SKILL-SPECIFIC] Override skill invoke decision
    -- t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))

    -- [SKILL-SPECIFIC] Override choice decisions
    -- t:registerOverride(p1, "choice", SKILL_NAME, sgs.QVariant("option1"))

    -- [SKILL-SPECIFIC] Override card provision
    -- t:registerOverride(p1, "card", SKILL_NAME, sgs.QVariant(42))

    -- [OPTIONAL] Skip play phase to speed up test
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
    t:registerOverride(p2, "activate", "phase", sgs.QVariant("pass"))
end)

-- ============================================================
-- ASSERT: verify game state after completion
-- ============================================================
runner:assert(function(t)
    -- Basic assertions (always check these)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHp(1, 4)
    t:assertHp(2, 4)

    -- [SKILL-SPECIFIC] Skill ownership
    -- t:assertHasSkill(1, SKILL_NAME)

    -- [SKILL-SPECIFIC] State changes caused by the skill
    -- t:assertHp(2, 3)               -- target took 1 damage
    -- t:assertHandcardCount(2, 3)    -- target lost 1 card
    -- t:assertMark(1, "some_mark", 1)

    -- [SKILL-SPECIFIC] Verify skill did NOT fire (negative test)
    -- Check that hp did NOT change, mark NOT set, etc.
end)

return runner
