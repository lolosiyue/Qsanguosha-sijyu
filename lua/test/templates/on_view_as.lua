-- ============================================================
-- TEMPLATE: view_as_skill 轉換技 (ViewAsSkill)
-- Pattern: 將A牌當B牌使用 / 重鑄 (recast)
-- Trigger flow: useSkill → askForCard (pick card to convert)
-- Common: 武聖(Wusheng)、龙胆(Longdan)、奇袭(Qixi)
-- ============================================================
-- Setup: 2 players, skill holder has cards to convert
-- ============================================================
-- NOTE: ViewAsSkill is harder to test because activate() needs to
-- return a CardUseStruct containing the ViewAsSkill card.
-- The override mechanism CAN override activate() by pushing a
-- full CardUseStruct, but this is complex.
-- For V1, test the individual sub-decisions:
--   1. askForCard → provide the card to convert
--   2. skill_invoke → force the skill on
-- ============================================================

local SKILL_NAME = "[REQUIRED: view-as skill name]"
local TEST_NAME  = "[REQUIRED: test description]"
local OUTPUT_CARD = "slash"  -- [CHANGE] what the skill produces: slash/jink/peach/dismantlement/...

local runner = sgs.test.create(TEST_NAME)

runner:setup(function(t)
    local players = t:getPlayers()
    local p1 = players[1]   -- skill holder
    local p2 = players[2]   -- target

    ROOM:handleAcquireDetachSkills(p1, SKILL_NAME, false)

    -- [OPTIONAL] Give p1 specific cards that can be converted
    -- (e.g. red cards for wusheng → slash)
    -- local cardIds = ROOM:getNCards(2)
    -- for _, id in sgs.qlist(cardIds) do
    --     local card = sgs.Sanguosha:getCard(id)
    --     if card:isRed() then ROOM:obtainCard(p1, id) end
    -- end
end)

runner:run(function(t)
    local players = t:getPlayers()
    local p1 = players[1]
    local p2 = players[2]

    -- Force skill invoke
    t:registerOverride(p1, "skill_invoke", SKILL_NAME, sgs.QVariant(true))

    -- [SKILL-SPECIFIC] Provide the card to convert (by effective ID)
    -- t:registerOverride(p1, "card", SKILL_NAME, sgs.QVariant(handCardId))

    -- [SKILL-SPECIFIC] If the converted card needs a target:
    -- t:registerOverride(p1, "player_chosen", OUTPUT_CARD, sgs.QVariant(p2:objectName()))

    -- Skip standard play phase (skill handling is separate)
    t:registerOverride(p1, "activate", "phase", sgs.QVariant("pass"))
end)

runner:assert(function(t)
    t:assertAlive(1)
    t:assertAlive(2)
    t:assertHasSkill(1, SKILL_NAME)

    -- [SKILL-SPECIFIC] view-as slash hit target:
    -- t:assertHp(2, 3)              -- converted slash dealt damage

    -- [SKILL-SPECIFIC] view-as skill consumed the card:
    -- t:assertHandcardCount(1, 3)   -- one less card in hand

    -- [SKILL-SPECIFIC] view-as peach healed:
    -- t:assertHp(1, 4)              -- healed to full
end)

return runner
