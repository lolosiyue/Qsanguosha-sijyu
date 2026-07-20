-- Usage: QSanguosha.exe --lua-test lua/test/examples/test_active_skill_v2_usage_ref.lua
-- Verifies usage-reference callbacks without registering a permanent extension package.

local context = sgs.SkillContext()
local sourceRef = sgs.SkillInstanceRef("root_owner", sgs.SkillInstanceKey("root_skill", 7))
local triggerRef = sgs.SkillInstanceRef("trigger_owner", sgs.SkillInstanceKey("trigger_skill", 3))

local active = sgs.CreateActiveSkillV2 {
	name = "active_skill_v2_usage_ref_smoke",
	limit_scope = sgs.Skill_Limit_Turn,
	get_usage_ref = function(skill, ctx)
		return sourceRef
	end,
	max_usage_limit = 2,
}

local trigger = sgs.CreateTriggerV2Skill {
	name = "trigger_v2_usage_ref_smoke",
	limit_scope = sgs.Skill_Limit_Phase,
	get_usage_ref = function(skill, ctx)
		return triggerRef
	end,
	max_usage_limit = 3,
}

local legacyOk, legacyError = pcall(sgs.CreateActiveSkillV2, {
	name = "active_skill_v2_legacy_usage_identity_smoke",
	usage_identity = 0,
})

local invalid = sgs.CreateActiveSkillV2 {
	name = "active_skill_v2_invalid_usage_ref_smoke",
	get_usage_ref = function(skill, ctx)
		return "not a SkillInstanceRef"
	end,
}

local runner = sgs.test.create("ActiveSkillV2 usage reference Lua smoke")

runner:assert(function(t)
	t:addResult(active:getLimitScope() == sgs.Skill_Limit_Turn,
		"ActiveSkillV2 factory preserves limit_scope")
	local activeRef = active:getUsageRef(context)
	t:addResult(activeRef.ownerObjectName == sourceRef.ownerObjectName
			and activeRef.key.skillName == sourceRef.key.skillName
			and activeRef.key.instanceID == sourceRef.key.instanceID,
		"ActiveSkillV2 get_usage_ref selects the source quota")
	t:addResult(active:getMaxUsageLimit(context) == 2,
		"ActiveSkillV2 factory preserves max_usage_limit")
	t:addResult(trigger:getLimitScope() == sgs.Skill_Limit_Phase,
		"TriggerV2Skill factory preserves limit_scope")
	local actualTriggerRef = trigger:getUsageRef(context)
	t:addResult(actualTriggerRef.ownerObjectName == triggerRef.ownerObjectName
			and actualTriggerRef.key.skillName == triggerRef.key.skillName
			and actualTriggerRef.key.instanceID == triggerRef.key.instanceID,
		"TriggerV2Skill get_usage_ref selects its quota reference")
	t:addResult(trigger:getMaxUsageLimit(context) == 3,
		"TriggerV2Skill factory preserves max_usage_limit")
	t:addResult(not legacyOk and type(legacyError) == "string"
			and legacyError:find("get_usage_ref", 1, true) ~= nil,
		"removed usage_identity fails with a migration hint")
	t:addResult(not invalid:getUsageRef(context):isValid(),
		"invalid get_usage_ref result fails closed")
end)

return runner
