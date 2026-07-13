-- ============================================================
-- TEMPLATE: on_slash 使用/打出殺時觸發的技能
-- Trigger events: CardUsed, PreCardUsed, CardResponded, TargetChosen
-- Card type: Slash
-- Common pattern: 当你使用/成为【杀】的目标时/使用【杀】指定目标后，你可以...
-- ============================================================
-- Setup: 2 players, skill holder uses or receives a slash
-- ============================================================

local SKILL_NAME = "[REQUIRED: skill name]"
local TEST_NAME  = "[REQUIRED: test description]"

local runner = sgs.test.create(TEST_NAME)

runner:setup(function(t)
    local players = t:getPlayers()
    local p1 = players[1]   -- lord: skill holder (user or target)
    local p2 = players[2]   -- rebel: the other party

    -- Assign the skill to the relevant player
    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)
    -- ROOM:handleAcquireDetachSkills(p2, SKILL_NAME, false)

    -- [SKILL-SPECIFIC]
    -- If skill triggers when YOU use slash → skill goes on p1 (attacker)
    -- If skill triggers when YOU are TARGET of slash → skill goes on p1 (target)
    -- If skill triggers when ANY slash is used → skill goes on p1 (observer)
end)

runner:run(function(t)
    local players = t:getPlayers()
    local p1 = players[1]
    local p2 = players[2]

    -- [CASE A] Skill triggers when p1 USES a slash
    -- Force skill invoke when asked
    t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))

    -- [CASE B] Skill triggers when p1 IS TARGETED by slash
    -- t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))
    -- (p2 auto-uses slash → targets p1 → skill fires)

    -- [CASE C] Skill triggers on slash Damage negotiation (add damage, prevent, etc.)
    -- t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))

    -- [SKILL-SPECIFIC] Choice override (if skill has options like "draw" / "discard"):
    -- t:registerOverride(p1, "choice", SKILL_NAME, sgs.QVariant("option1"))

    -- [SKILL-SPECIFIC] Card override (if skill needs extra card like Jink):
    -- t:registerOverride(p2, "card", SKILL_NAME, sgs.QVariant(1))

    -- [SKILL-SPECIFIC] Card chosen override (steal, obtain from target):
    -- t:registerOverride(p1, "card_chosen", SKILL_NAME, sgs.QVariant(1))

    -- [SKILL-SPECIFIC] Player chosen override:
    -- t:registerOverride(p1, "player_chosen", SKILL_NAME, sgs.QVariant(p2:objectName()))

    -- Skip play phases → auto slash use by AI
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
    -- Don't skip p2 if p2 needs to use slash as attacker
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHasSkill(1, SKILL_NAME)

    -- [SKILL-SPECIFIC] If slash dealt damage:
    -- t:assertHp(2, 3)              -- target took slash damage
    -- t:assertHp(1, 3)              -- holder took slash damage

    -- [SKILL-SPECIFIC] If skill modified slash effects:
    -- t:assertHp(2, 3)              -- extra damage applied
    -- t:assertHp(2, 4)              -- damage prevented → no HP change

    -- [SKILL-SPECIFIC] If skill drew/gave cards:
    -- t:assertHandcardCount(1, 5)   -- drew card from skill
    -- t:assertHandcardCount(2, 3)   -- lost card to skill

    -- [SKILL-SPECIFIC] If skill applied marks:
    -- t:assertMark(1, "mark_name", expected)
    -- t:assertMark(2, "mark_name", 0)
end)

return runner
