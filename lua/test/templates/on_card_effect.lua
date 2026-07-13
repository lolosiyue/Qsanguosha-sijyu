-- ============================================================
-- TEMPLATE: on_card_effect 卡片效果相關技能
-- Trigger events: CardEffect, CardEffected, PreCardEffect
-- Common pattern: 在一张牌生效前/后，你可以... / 此牌对你无效...
-- Includes: 无懈可击, 免疫锦囊, 改判, 改目标, 额外效果
-- ============================================================
-- Setup: 2 players, one uses a card on the other, skill modifies it
-- ============================================================

local SKILL_NAME = "[REQUIRED: skill name]"
local TEST_NAME  = "[REQUIRED: test description]"
local TARGET_CARD = "Dismantlement"  -- [CHANGE] card that triggers: Slash/ArcheryAttack/Dismantlement/AOE/...

local runner = sgs.test.create(TEST_NAME)

runner:setup(function(t)
    local players = t:getPlayers()
    local p1 = players[1]   -- skill holder (modifier)
    local p2 = players[2]   -- card user or card target

    -- [SKILL-SPECIFIC] Who has the skill?
    -- If skill nullifies card → holder = target of the card
    -- If skill modifies card → holder = user of the card
    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)

    -- [OPTIONAL] Give p2 a trick card to use
    -- local card = ROOM:getCardFromPile(TARGET_CARD)
    -- if card >= 0 then ROOM:obtainCard(p2, card) end
end)

runner:run(function(t)
    local players = t:getPlayers()
    local p1 = players[1]
    local p2 = players[2]

    -- Force skill invoke
    t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))

    -- [SKILL-SPECIFIC] If skill asks for choice (e.g. nullify or not):
    -- t:registerOverride(p1, "choice", SKILL_NAME, sgs.QVariant("yes"))

    -- [SKILL-SPECIFIC] If skill requires providing a card (e.g. 无懈可击 needs same type):
    -- t:registerOverride(p1, "card", SKILL_NAME, sgs.QVariant(cardId))

    -- [SKILL-SPECIFIC] If skill targets a player:
    -- t:registerOverride(p1, "player_chosen", SKILL_NAME, sgs.QVariant(p2:objectName()))

    -- Skip play phases
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
    -- Don't skip p2 if p2 needs to use the card
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHasSkill(1, SKILL_NAME)

    -- [SKILL-SPECIFIC] If card was nullified → target unaffected:
    -- t:assertHp(2, 4)              -- damage prevented
    -- t:assertHandcardCount(2, 4)   -- card discard prevented

    -- [SKILL-SPECIFIC] If card was modified (extra damage, etc):
    -- t:assertHp(2, 2)              -- extra damage applied
    -- t:assertMark(1, "mark", 1)

    -- [SKILL-SPECIFIC] If card direction was changed:
    -- t:assertHp(2, 4)              -- original target safe
    -- t:assertHp(1, 3)              -- redirected to self or other
end)

return runner
