-- Opt-in package example; require this file from an extension package loader.
-- Each acquired assembly instance draws 1 after damage and adds 1 max hand card.
local Assembly = require("lua.skill_assembly")
local extension = sgs.Package("assembly_resilience_example")
local resilience = Assembly.create {
    name = "assembly_resilience",
    effects = {
        {
            id = "draw", kind = "trigger",
            frequency = sgs.Skill_Compulsory,
            events = {sgs.Damaged},
            base_amount = 1,
            can_trigger = function(self, event, room, player, data)
                if player and player:isAlive() and player:hasSkill(self:objectName()) then
                    return self:objectName(), player
                end
                return ""
            end,
            on_effect = function(self, event, room, player, ctx)
                local n = self:getEffectiveAmount(ctx)
                if n > 0 then player:drawCards(n, "assembly_resilience") end
                return false
            end,
        },
        {
            id = "hand_limit", kind = "maxcards",
            base_amount = 1,
            correct_func = function(self, ctx)
                return true -- native per-helper current amount, including override
            end,
        },
    },
}
local general = sgs.General(extension, "assembly_resilience_general", "qun", 4)
resilience:register(extension, general)

sgs.LoadTranslationTable {
    ["assembly_resilience_example"] = "複合技能範例",
    ["assembly_resilience_general"] = "堅毅示例",
    ["assembly_resilience"] = "堅毅",
    [":assembly_resilience"] = "鎖定技，當你受到傷害後，你摸1張牌；你的手牌上限+1。",
    ["#assembly_resilience__hand_limit"] = "堅毅·手牌上限",
}

-- A future three-choice reward keeps the chosen root instance ID, then uses:
-- local ref = resilience:effectRef(player, chosenRootID, "draw")
-- if ref then room:addSkillInstanceAmount(player, ref, 1, "reward_draw") end
-- Or choose "hand_limit" to improve only that root's correction helper.
-- Gain another whole copy with room:acquireSkill(player, "assembly_resilience").
-- Remove exactly one root (and its helpers) with:
-- room:detachSkillFromPlayer(player, "assembly_resilience#" .. chosenRootID)
-- Never call setBaseAmount during play: that changes the shared definition.
return extension
