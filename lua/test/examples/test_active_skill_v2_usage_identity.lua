-- Usage: QSanguosha.exe --lua-test lua/test/examples/test_active_skill_v2_usage_identity.lua
-- Verifies Lua factory configuration without registering a permanent extension package.

local context = sgs.SkillContext()

local active = sgs.CreateActiveSkillV2 {
    name = "active_skill_v2_usage_identity_smoke",
    limit_scope = sgs.Skill_Limit_Turn,
    usage_identity = sgs.Skill_Usage_SourceInstance,
    max_usage_limit = 2,
}

local trigger = sgs.CreateTriggerV2Skill {
    name = "trigger_v2_usage_identity_smoke",
    limit_scope = sgs.Skill_Limit_Phase,
    usage_identity = sgs.Skill_Usage_ActivationInstance,
    max_usage_limit = 3,
}

local invalidOk, invalidError = pcall(sgs.CreateActiveSkillV2, {
    name = "active_skill_v2_invalid_usage_identity_smoke",
    usage_identity = -1,
})

local runner = sgs.test.create("ActiveSkillV2 usage identity Lua smoke")

runner:assert(function(t)
    t:addResult(sgs.Skill_Usage_ActivationInstance ~= sgs.Skill_Usage_SourceInstance,
        "usage identity enum constants are distinct")
    t:addResult(active:getLimitScope() == sgs.Skill_Limit_Turn,
        "ActiveSkillV2 factory preserves limit_scope")
    t:addResult(active:getUsageIdentity(context) == sgs.Skill_Usage_SourceInstance,
        "ActiveSkillV2 factory preserves source usage_identity")
    t:addResult(active:getMaxUsageLimit(context) == 2,
        "ActiveSkillV2 factory preserves max_usage_limit")
    t:addResult(trigger:getLimitScope() == sgs.Skill_Limit_Phase,
        "TriggerV2Skill factory preserves limit_scope")
    t:addResult(trigger:getUsageIdentity(context) == sgs.Skill_Usage_ActivationInstance,
        "TriggerV2Skill factory preserves activation usage_identity")
    t:addResult(trigger:getMaxUsageLimit(context) == 3,
        "TriggerV2Skill factory preserves max_usage_limit")
    t:addResult(not invalidOk and type(invalidError) == "string"
            and invalidError:find("usage_identity", 1, true) ~= nil,
        "invalid usage_identity fails closed with a clear error")
end)

return runner
