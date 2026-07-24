-- Usage: QSanguosha.exe --lua-test lua/test/examples/test_correct_skill_v2.lua
-- Factory and callback-contract smoke for the four CorrectSkillV2 families.

local context = sgs.CorrectSkillContext()

local distance = sgs.CreateDistanceSkillV2 {
	name = "correct_v2_distance_smoke",
	base_amount = -1,
	holder_selector = sgs.CorrectSkill_Participants,
	correct_func = function(skill, ctx)
		return true
	end,
	fixed_func = function(skill, ctx)
		return 4
	end,
}

local maxcards = sgs.CreateMaxCardsSkillV2 {
	name = "correct_v2_maxcards_smoke",
	base_amount = 0,
	holder_selector = sgs.CorrectSkill_Secondary,
	correct_func = function(skill, ctx)
		return 0
	end,
}

local targetmod = sgs.CreateTargetModSkillV2 {
	name = "correct_v2_targetmod_smoke",
	pattern = "Slash",
	base_amount = -1,
	holder_selector = sgs.CorrectSkill_AllHolders,
	correct_func = function(skill, ctx)
		return -1
	end,
}

local attackrange = sgs.CreateAttackRangeSkillV2 {
	name = "correct_v2_attackrange_smoke",
	base_amount = 2,
	holder_selector = sgs.CorrectSkill_System,
	correct_func = function(skill, ctx)
		return nil
	end,
	fixed_func = function(skill, ctx)
		error("intentional fail-closed smoke")
	end,
}

local runner = sgs.test.create("CorrectSkillV2 Lua contract smoke"):factoryOnly()

runner:assert(function(t)
	local distanceResult = distance:getCorrection(context)
	t:addResult(distance:getBaseAmount() == -1,
		"DistanceSkillV2 preserves a negative base amount")
	t:addResult(distance:getHolderSelector() == sgs.CorrectSkill_Participants,
		"DistanceSkillV2 preserves Participants selector")
	t:addResult(distanceResult.applies and distanceResult.value == 0,
		"Lua true uses the current per-instance amount")

	local fixedResult = distance:getFixedValue(context)
	t:addResult(fixedResult.applies and fixedResult.value == 4,
		"fixed callback returns an applicable explicit value")

	local maxCardsResult = maxcards:getCorrection(context)
	t:addResult(maxCardsResult.applies and maxCardsResult.value == 0,
		"Lua numeric zero remains an applicable contribution")

	local targetModResult = targetmod:getCorrection(context)
	t:addResult(targetModResult.applies and targetModResult.value == -1,
		"TargetMod Residue preserves the -1 unlimited sentinel")

	local attackResult = attackrange:getCorrection(context)
	t:addResult(not attackResult.applies,
		"Lua nil means this instance does not apply")
	local attackFixed = attackrange:getFixedValue(context)
	t:addResult(not attackFixed.applies,
		"Lua callback errors fail closed without a contribution")
	t:addResult(attackrange:getHolderSelector() == sgs.CorrectSkill_System,
		"AttackRangeSkillV2 preserves System selector")
end)

return runner
