-- Usage: QSanguosha.exe --lua-test lua/test/examples/test_correct_skill_v2_room.lua
-- Headless Room fixture for per-instance aggregation, selectors and amount/state APIs.

local names = {
	primary = "#correct_v2_distance_primary_test",
	secondary = "#correct_v2_distance_secondary_test",
	participants = "#correct_v2_distance_participants_test",
	allholders = "#correct_v2_distance_allholders_test",
	fixed = "#correct_v2_distance_fixed_test",
	maxcards = "#correct_v2_maxcards_test",
	targetmod = "#correct_v2_targetmod_test",
	attackrange = "#correct_v2_attackrange_test",
}

local state = {}
local runner = sgs.test.create("CorrectSkillV2 Room integration"):factoryOnly()

local function ref(player, skillName, instanceId)
	return sgs.SkillInstanceRef(player:objectName(), sgs.SkillInstanceKey(skillName, instanceId))
end

runner:setup(function(t)
	state.p1 = t:getPlayer(1)
	state.p2 = t:getPlayer(2)
	local p1, p2 = state.p1, state.p2

	state.primary1 = ROOM:acquireSkill(p1, names.primary)
	state.primary2 = ROOM:acquireSkill(p1, names.primary)
	ROOM:acquireSkill(p2, names.primary)
	ROOM:acquireSkill(p2, names.secondary)
	ROOM:acquireSkill(p1, names.participants)
	ROOM:acquireSkill(p2, names.participants)
	ROOM:acquireSkill(p1, names.allholders)
	ROOM:acquireSkill(p2, names.allholders)
	state.fixed = ROOM:acquireSkill(p1, names.fixed)

	state.max1 = ROOM:acquireSkill(p1, names.maxcards)
	ROOM:acquireSkill(p1, names.maxcards)
	ROOM:acquireSkill(p2, names.maxcards)

	state.target1 = ROOM:acquireSkill(p1, names.targetmod)
	ROOM:acquireSkill(p1, names.targetmod)
	ROOM:acquireSkill(p2, names.targetmod)

	ROOM:acquireSkill(p1, names.attackrange)
	ROOM:acquireSkill(p1, names.attackrange)
	ROOM:acquireSkill(p2, names.attackrange)

	state.primaryRef = ref(p1, names.primary, state.primary1)
	state.fixedRef = ref(p1, names.fixed, state.fixed)
	state.targetRef = ref(p1, names.targetmod, state.target1)
	state.slash = sgs.Sanguosha:cloneCard("slash")
end)

runner:assert(function(t)
	local p1, p2 = state.p1, state.p2
	local baseDistance = sgs.Sanguosha:correctDistance(p1, p2, false)
	t:addResult(baseDistance == -7,
		"Primary, Secondary, Participants and AllHolders aggregate per valid instance")

	ROOM:setPlayerMark(p1, "correct_v2_system_enabled", 1)
	t:addResult(sgs.Sanguosha:correctDistance(p1, p2, false) == -8,
		"System selector contributes once without a holder")
	ROOM:setPlayerMark(p1, "correct_v2_system_enabled", 0)

	t:addResult(ROOM:setSkillInstanceAmount(p1, state.primaryRef, -5, "correct_v2_modify"),
		"Changing event accepts an amount update")
	t:addResult(ROOM:getSkillInstanceAmount(state.primaryRef) == -3,
		"Changing event may rewrite newAmount")
	t:addResult(not ROOM:setSkillInstanceAmount(p1, state.primaryRef, 9, "correct_v2_cancel"),
		"Changing event may cancel the update")
	t:addResult(ROOM:getSkillInstanceAmount(state.primaryRef) == -3,
		"canceled update preserves the previous current amount")
	t:addResult(ROOM:addSkillInstanceAmount(p1, state.primaryRef, 2, "correct_v2_add"),
		"addSkillInstanceAmount applies a signed delta")
	t:addResult(ROOM:getSkillInstanceAmount(state.primaryRef) == -1,
		"signed delta preserves negative values")
	t:addResult(ROOM:setSkillInstanceAmount(p1, state.primaryRef, 0, "correct_v2_zero"),
		"explicit zero is a valid current amount")
	t:addResult(ROOM:getSkillInstanceAmount(state.primaryRef) == 0,
		"current amount returns explicit zero")
	t:addResult(ROOM:resetSkillInstanceAmount(p1, state.primaryRef, "correct_v2_reset"),
		"reset removes the per-instance override")
	t:addResult(ROOM:getSkillInstanceAmount(state.primaryRef) == -1,
		"reset restores the shared base amount")

	ROOM:setSkillInstanceAmount(p1, state.primaryRef, -2, "correct_v2_recurse")
	t:addResult(p1:getMark("correct_v2_recursion_rejected") == 1,
		"same-ref recursive amount changes are rejected")
	ROOM:resetSkillInstanceAmount(p1, state.primaryRef, "correct_v2_reset")

	ROOM:setSkillInstanceCorrectState(p1, state.primaryRef, "enabled", false)
	t:addResult(sgs.Sanguosha:correctDistance(p1, p2, false) == -6,
		"correctState disables only the selected instance")
	ROOM:removeSkillInstanceCorrectState(p1, state.primaryRef, "enabled")
	t:addResult(sgs.Sanguosha:correctDistance(p1, p2, false) == -7,
		"removing one correctState key restores that instance")

	ROOM:addSkillInvalidity(p1, names.primary, "correct_v2_fixture", "exact", state.primary1)
	t:addResult(sgs.Sanguosha:correctDistance(p1, p2, false) == -6,
		"instance-scoped invalidity excludes exactly one instance")
	ROOM:removeSkillInvalidity(p1, names.primary, "correct_v2_fixture", "exact", state.primary1)

	ROOM:setSkillInstanceAmount(p1, state.fixedRef, 4, "correct_v2_fixed")
	t:addResult(sgs.Sanguosha:correctDistance(p1, p2, true) == 4,
		"applicable fixed values use the maximum")
	t:addResult(sgs.Sanguosha:correctMaxCards(p1, false) == 2,
		"MaxCards V2 stacks two local instances and ignores another holder")
	t:addResult(sgs.Sanguosha:correctAttackRange(p1, true, false) == 2,
		"AttackRange V2 stacks two local instances and ignores another holder")
	t:addResult(sgs.Sanguosha:correctCardTarget(
		sgs.TargetModSkill_Residue, p1, state.slash, p2) == 2,
		"TargetMod V2 stacks two local instances and ignores another holder")
	ROOM:setSkillInstanceAmount(p1, state.targetRef, -1, "correct_v2_unlimited")
	t:addResult(sgs.Sanguosha:hasResidueUnlimited(p1, state.slash, p2),
		"only TargetMod Residue treats -1 as unlimited")
	t:addResult(p1:getMark("correct_v2_changed_count") >= 7,
		"Changed notification fires only for committed amount changes")
end)

return runner
