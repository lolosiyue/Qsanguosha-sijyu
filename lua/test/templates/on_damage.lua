-- ============================================================
-- TEMPLATE: on_damage 受到傷害時觸發的技能
-- Trigger events: Damaged, DamageInflicted, DamageDone, DamageComplete
-- Common pattern: 每当你受到/造成伤害后/时，发动...
-- ============================================================
-- Setup: 2 players, lord takes damage from rebel's attack
-- ============================================================

local SKILL_NAME = "[REQUIRED: skill name]"
local TEST_NAME  = "[REQUIRED: test description]"

local runner = sgs.test.create(TEST_NAME)

runner:setup(function(t)
    local players = t:getPlayers()
    local p1 = players[1]   -- lord: the one who HAS the skill
    local p2 = players[2]   -- rebel: the one who deals damage / is targeted

    -- Assign the skill (usually to the one who RECEIVES or DEALS damage)
    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)
    -- ROOM:handleAcquireDetachSkills(p2, SKILL_NAME, false)

    -- [SKILL-SPECIFIC] Skill typically goes on:
    --   p1 (holder)  if the skill triggers when p1 IS DAMAGED
    --   p2 (attacker) if the skill triggers when p2 DEALS damage

    -- [OPTIONAL] Lower someone's HP to make damage more likely to kill
    -- p2:setHp(1)
end)

runner:run(function(t)
    local players = t:getPlayers()
    local p1 = players[1]
    local p2 = players[2]

    -- Force the player who HAS the skill to invoke it when asked
    t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))

    -- Force the player to use a slash (trigger damage)
    -- This override will cause the skill to fire during combat
    -- Note: the skill_invoke override above handles the "do you want to use the skill?" question
    -- that pops up when the damage trigger fires.

    -- [SKILL-SPECIFIC] If the skill asks for a choice after triggering:
    -- t:registerOverride(p1, "choice", SKILL_NAME, sgs.QVariant("option1"))

    -- [SKILL-SPECIFIC] If the skill asks the player to discard:
    -- t:registerOverride(p1, "card", SKILL_NAME, sgs.QVariant(1))

    -- [SKILL-SPECIFIC] If the skill does card_chosen (steal/obtain a card):
    -- t:registerOverride(p1, "card_chosen", SKILL_NAME, sgs.QVariant(1))

    -- [SKILL-SPECIFIC] If the skill asks for a target player:
    -- t:registerOverride(p1, "player_chosen", SKILL_NAME, sgs.QVariant(p2:objectName()))

    -- Skip play phases to speed up (AI will still use slash automatically)
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
    -- DON'T skip p2's phase if p2 is dealing the damage:
    -- (AI will auto-use slash, which triggers the damage → skill)
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)

    -- [SKILL-SPECIFIC] Check damage effects
    -- t:assertHp(1, 3)    -- p1 took damage → HP decreased
    -- t:assertHp(2, 3)    -- p2 took damage → HP decreased

    -- [SKILL-SPECIFIC] Check skill side-effects
    -- t:assertHandcardCount(1, 3)    -- p1 lost/drew cards
    -- t:assertHandcardCount(2, 3)    -- p2 lost/drew cards
    -- t:assertMark(1, "mark_name", 1)
    -- t:assertHasSkill(1, SKILL_NAME)
end)

return runner
