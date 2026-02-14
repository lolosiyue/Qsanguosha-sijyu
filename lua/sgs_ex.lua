-- this script file defines all functions written by Lua

-- trigger skills
function sgs.CreateTriggerSkill(spec)
	assert(type(spec.name)=="string")
	--assert(type(spec.on_trigger)=="function")
	if spec.frequency then assert(type(spec.frequency)=="number") end
	if spec.limit_mark then assert(type(spec.limit_mark)=="string") end
    if spec.change_skill then assert(type(spec.change_skill)=="boolean") end
	if spec.limited_skill then assert(type(spec.limited_skill)=="boolean") end
	if spec.hide_skill then assert(type(spec.hide_skill)=="boolean") end
	if spec.shiming_skill then assert(type(spec.shiming_skill)=="boolean") end
	if spec.waked_skills then assert(type(spec.waked_skills)=="string") end
	local frequency = spec.frequency or sgs.Skill_NotFrequent
	local limit_mark = spec.limit_mark or ""
    local change_skill = spec.change_skill or false
	local limited_skill = spec.limited_skill or false
	local hide_skill = spec.hide_skill or false
	local shiming_skill = spec.shiming_skill or false
    local skill = sgs.LuaTriggerSkill(spec.name,frequency,limit_mark,change_skill,limited_skill,hide_skill,shiming_skill,spec.waked_skills or "")
	if type(spec.guhuo_type)=="string" and spec.guhuo_type~="" then skill:setGuhuoDialog(spec.guhuo_type) end
	if type(spec.juguan_type)=="string" and spec.juguan_type~="" then skill:setJuguanDialog(spec.juguan_type) end
	if type(spec.tiansuan_type)=="string" and spec.tiansuan_type~="" then skill:setTiansuanDialog(spec.tiansuan_type) end
	if type(spec.events)=="number" then
		skill:addEvent(spec.events)
	elseif type(spec.events)=="table" then
		for _,event in ipairs(spec.events)do
			skill:addEvent(event)
		end
	end
	if type(spec.global)=="boolean" then skill:setGlobal(spec.global) end
	--if type(spec.on_trigger)=="function" then
		skill.on_trigger = spec.on_trigger
	--end
	--if type(spec.can_trigger)=="function" then
		skill.can_trigger = spec.can_trigger
	--end
	--if type(spec.can_wake)=="function" then
		skill.can_wake = spec.can_wake
	--end
	if spec.view_as_skill then
		skill:setViewAsSkill(spec.view_as_skill)
	end
	if type(spec.priority)=="number" then
		skill.priority = spec.priority
	elseif type(spec.priority)=="table" then
		if type(spec.events)=="table" then
			for i = 1,#spec.events do
				if i>#spec.priority then break end
				skill:insertPriorityTable(spec.events[i],spec.priority[i])
			end
		elseif type(spec.events)=="number" then
			skill:insertPriorityTable(spec.events,spec.priority[1])
		end
	end
	--if type(spec.dynamic_frequency)=="function" then
		skill.dynamic_frequency = spec.dynamic_frequency
	--end
	return skill
end

function sgs.CreateScenarioRule(spec)
	--assert(type(spec.on_trigger)=="function")
	assert(spec.scenario)
    local rule = sgs.LuaScenarioRule(spec.scenario)
	if type(spec.events)=="number" then
		rule:addEvent(spec.events)
	elseif type(spec.events)=="table" then
		for _,event in ipairs(spec.events)do
			rule:addEvent(event)
		end
	end
	--if type(spec.can_trigger)=="function" then
		rule.can_trigger = spec.can_trigger
	--end
	rule:setGlobal(spec.global~=false)
	rule.on_trigger = spec.on_trigger
	spec.priority = spec.priority or 1
	if type(spec.priority)=="number" then
		rule.priority = spec.priority
	elseif type(spec.priority)=="table" then
		if type(spec.events)=="table" then
			for i = 1,#spec.events do
				if i>#spec.priority then break end
				rule:insertPriorityTable(spec.events[i],spec.priority[i])
			end
		elseif type(spec.events)=="number" then
			rule:insertPriorityTable(spec.events,spec.priority[1])
		end
	end
	return rule
end

function sgs.CreateScenario(spec)
	assert(type(spec.name)=="string")
	assert(spec.roles)
	local scenario = sgs.LuaScenario(spec.name)
	if type(spec.expose)=="boolean"
	then scenario:setExposeRoles(spec.expose) end
	if spec.rule then
		scenario:setRule(spec.rule)
	end
	for r,g in pairs(spec.roles)do
		if type(g)~="string" then g = "sujiang" end
		if r:match("lord") then
			scenario:setScenarioLord(g)
		elseif r:match("loyalist") then
			scenario:addScenarioLoyalists(g)
		elseif r:match("rebel") then
			scenario:addScenarioRebels(g)
		elseif r:match("renegade") then
			scenario:addScenarioRenegades(g)
		end
	end
	--[[if spec.on_assign then
		function scenario:on_assign(...)
			return spec.on_assign(self,...)
		end
	end]]
	return scenario
end

function sgs.CreateProhibitSkill(spec)
	assert(type(spec.name)=="string")
  	local skill = sgs.LuaProhibitSkill(spec.name,spec.frequency or sgs.Skill_Compulsory)
	--if type(spec.is_prohibited)=="function" then
		skill.is_prohibited = spec.is_prohibited
	--end
	return skill
end

function sgs.CreateProhibitPindianSkill(spec)
	assert(type(spec.name)=="string")
	local skill = sgs.LuaProhibitPindianSkill(spec.name,spec.frequency or sgs.Skill_Compulsory)
	--if type(spec.is_pindianprohibited)=="function" then
		skill.is_pindianprohibited = spec.is_pindianprohibited
	--end
	return skill
end

function sgs.CreateFilterSkill(spec)
	assert(type(spec.name)=="string")
	local skill = sgs.LuaFilterSkill(spec.name,spec.frequency or sgs.Skill_Compulsory)
	--if type(spec.view_filter)=="function" then
		skill.view_filter = spec.view_filter
	--end
	--if type(spec.view_as)=="function" then
		skill.view_as = spec.view_as
	--end
	return skill
end

function sgs.CreateDistanceSkill(spec)
	assert(type(spec.name)=="string")
	local skill = sgs.LuaDistanceSkill(spec.name,spec.frequency or sgs.Skill_Compulsory)
	--if type(spec.correct_func)=="function" then
		skill.correct_func = spec.correct_func
	--end
	if type(spec.fixed_func)=="function" then
		function skill.fixed_func(...)
			return spec.fixed_func(...) or -1
		end
		--skill.fixed_func = spec.fixed_func
	end
	return skill
end

function sgs.CreateMaxCardsSkill(spec)
	assert(type(spec.name)=="string")
	local skill = sgs.LuaMaxCardsSkill(spec.name,spec.frequency or sgs.Skill_Compulsory)
	--if type(spec.extra_func)=="function" then
		skill.extra_func = spec.extra_func
	--end
	if type(spec.fixed_func)=="function" then
		function skill.fixed_func(...)
			return spec.fixed_func(...) or -1
		end
		--skill.fixed_func = spec.fixed_func
	end
	return skill
end

function sgs.CreateTargetModSkill(spec)
	assert(type(spec.name)=="string")
	if spec.pattern then assert(type(spec.pattern)=="string") end
	local skill = sgs.LuaTargetModSkill(spec.name,spec.pattern or "Slash",spec.frequency or sgs.Skill_Compulsory)
	--if type(spec.residue_func)=="function" then
		skill.residue_func = spec.residue_func
	--end
	--if type(spec.distance_limit_func)=="function" then
		skill.distance_limit_func = spec.distance_limit_func
	--end
	--if type(spec.extra_target_func)=="function" then
		skill.extra_target_func = spec.extra_target_func
	--end
	return skill
end

function sgs.CreateInvaliditySkill(spec)
	assert(type(spec.name)=="string")
	local skill = sgs.LuaInvaliditySkill(spec.name,spec.frequency or sgs.Skill_Compulsory)
	--if type(spec.skill_valid)=="function" then
		skill.skill_valid = spec.skill_valid
	--end
	return skill
end

function sgs.CreateAttackRangeSkill(spec)
	assert(type(spec.name)=="string")
	local skill = sgs.LuaAttackRangeSkill(spec.name,spec.frequency or sgs.Skill_Compulsory)
	--if type(spec.extra_func)=="function" then
		skill.extra_func = spec.extra_func
	--end
	if type(spec.fixed_func)=="function" then
		function skill.fixed_func(...)
			return spec.fixed_func(...) or -1
		end
		--skill.fixed_func = spec.fixed_func
	end
	return skill
end

function sgs.CreateViewAsEquipSkill(spec)
	assert(type(spec.name)=="string")
	local skill = sgs.LuaViewAsEquipSkill(spec.name,spec.frequency or sgs.Skill_Compulsory)
	--if type(spec.view_as_equip)=="function" then
		skill.view_as_equip = spec.view_as_equip
	--end
	return skill
end

function sgs.CreateCardLimitSkill(spec)
	assert(type(spec.name)=="string")
	local skill = sgs.LuaCardLimitSkill(spec.name,spec.frequency or sgs.Skill_Compulsory)
	--if type(spec.limit_list)=="function" then
		skill.limit_list = spec.limit_list
	--end
	--if type(spec.limit_pattern)=="function" then
		skill.limit_pattern = spec.limit_pattern
	--end
	return skill
end

function sgs.CreateMasochismSkill(spec)
	assert(type(spec.on_damaged)=="function")
	spec.events = sgs.Damaged
	function spec.on_trigger(skill,event,player,data,room)
		local damage = data:toDamage()
		spec.on_damaged(skill,player,damage,room)
		return false
	end
	return sgs.CreateTriggerSkill(spec)
end

function sgs.CreatePhaseChangeSkill(spec)
	assert(type(spec.on_phasechange)=="function")
	spec.events = sgs.EventPhaseStart
	function spec.on_trigger(skill,event,player,data,room)
		return spec.on_phasechange(skill,player,room)
	end
	return sgs.CreateTriggerSkill(spec)
end

function sgs.CreateDrawCardsSkill(spec)
	assert(type(spec.draw_num_func)=="function")
	spec.events = sgs.DrawNCards
	function spec.on_trigger(skill,event,player,data,room)
		local draw = data:toDraw()
		if spec.is_initial then
			if draw.reason~="InitialHandCards" then return false end
		elseif draw.reason~="draw_phase" then return false end
		draw.num = spec.draw_num_func(skill,player,draw.num,room)
		data:setValue(draw)
		return false
	end
	return sgs.CreateTriggerSkill(spec)
end

function sgs.CreateGameStartSkill(spec)
	assert(type(spec.on_gamestart)=="function")
	spec.events = sgs.GameStart
	function spec.on_trigger(skill,event,player,data,room)
		spec.on_gamestart(skill,player,room)
		return false
	end
	return sgs.CreateTriggerSkill(spec)
end

function sgs.CreateRetrialSkill(spec)
	assert(type(spec.on_retrial)=="function")
	assert(type(spec.exchange)=="boolean")
	spec.events = sgs.AskForRetrial
	function spec.on_trigger(skill,event,player,data,room)
		local judge = data:toJudge()
        local card = spec.on_retrial(skill,player,judge,room)
		if not card then return false end
		room:retrial(card,player,judge,skill:objectName(),spec.exchange)
		return false
	end
	return sgs.CreateTriggerSkill(spec)
end

--------------------------------------------

-- skill cards

function sgs.CreateSkillCard(spec)
	assert(spec.name)
	if spec.skill_name then assert(type(spec.skill_name)=="string") end
	local card = sgs.LuaSkillCard(spec.name,spec.skill_name)
	if type(spec.will_throw)=="boolean" then
		card:setWillThrow(spec.will_throw)
		if not spec.will_throw then
			card:setHandlingMethod(sgs.Card_MethodNone)
		end
	end
	if type(spec.can_recast)=="boolean" then
		card:setCanRecast(spec.can_recast)
	end
	if type(spec.handling_method)=="number" then
		card:setHandlingMethod(spec.handling_method)
	end
	if type(spec.mute)=="boolean" then
		card:setMute(spec.mute)
	end
	if type(spec.target_fixed)=="boolean"
	then card:setTargetFixed(spec.target_fixed)end
	if type(spec.filter)=="function" then
		function card.filter(...)
			local result,vote = spec.filter(...)
			if type(result)=="number" then
				vote = result
				result = result>0
			elseif type(vote)~="number" then
				vote = result and 1 or 0
			end
			return (result~=false and result~=nil),vote
		end
	end
	card.feasible = spec.feasible
	card.about_to_use = spec.about_to_use
	card.on_use = spec.on_use
	card.on_effect = spec.on_effect
	card.on_validate = spec.on_validate
	card.on_validate_in_response = spec.on_validate_in_response
	return card
end

function sgs.CreateBasicCard(spec)
	assert(type(spec.name)=="string" or type(spec.class_name)=="string")
	spec.name = spec.name or spec.class_name
	spec.class_name = spec.class_name or spec.name
	if spec.suit then assert(type(spec.suit)=="number") end
	if spec.number then assert(type(spec.number)=="number") end
	if spec.subtype then assert(type(spec.subtype)=="string") end
	local card = sgs.LuaBasicCard(spec.suit or sgs.Card_SuitToBeDecided,spec.number or 0,spec.name,spec.class_name,spec.subtype or "BasicCard")
	if type(spec.target_fixed)=="boolean"
	then card:setTargetFixed(spec.target_fixed)end
	if type(spec.can_recast)=="boolean" then
		card:setCanRecast(spec.can_recast)
	end
	if type(spec.damage_card)=="boolean" then
		card:setDamageCard(spec.damage_card)
	end
	if type(spec.is_gift)=="boolean" then
		card:setGift(spec.is_gift)
	end
	if type(spec.single_target)=="boolean" then
		card:setSingleTargetCard(spec.single_target)
	end
	if type(spec.filter)=="function" then
		function card.filter(...)
			local result,vote = spec.filter(...)
			if type(result)=="number" then
				vote = result
				result = result>0
			elseif type(vote)~="number" then
				vote = result and 1 or 0
			end
			return (result~=false and result~=nil),vote
		end
	end
	card.feasible = spec.feasible
	card.available = spec.available
	card.about_to_use = spec.about_to_use
	card.on_use = spec.on_use
	card.on_effect = spec.on_effect
	card.on_validate = spec.on_validate
	card.on_validate_in_response = spec.on_validate_in_response
	return card
end

--============================================
-- default functions for Trick cards

function isAvailable_AOE(self,player)
	for _,p in sgs.qlist(player:getAliveSiblings())do
		if player:isProhibited(p,self) then continue end
		return self:cardIsAvailable(player)
	end
end

function onUse_AOE(self,room,card_use)
	if card_use.to:isEmpty() then
		card_use.to = room:getOtherPlayers(card_use.from)
	end
	local tos = sgs.SPlayerList()
	for _,to in sgs.qlist(card_use.to)do
		tos:append(to)
	end
	local skillBans = {}
	for _,to in sgs.qlist(tos)do
		local skill = room:isProhibited(card_use.from,to,self)
		if skill then
			if skill:isVisible() then
				skillBans[skill:objectName()] = skillBans[skill:objectName()] or sgs.SPlayerList()
				skillBans[skill:objectName()]:append(to)
			else
				skill = sgs.Sanguosha:getMainSkill(skill:objectName())
				skillBans[skill:objectName()] = skillBans[skill:objectName()] or sgs.SPlayerList()
				skillBans[skill:objectName()]:append(to)
			end
			card_use.to:removeOne(to)
		end
	end
	for skill,tos in pairs(skillBans)do
		local log = sgs.LogMessage()
		log.type = "#SkillAvoidFrom"
		log.from = room:findPlayerBySkillName(skill)
		log.to = tos
		log.arg = skill
		log.arg2 = self:objectName()
		room:broadcastSkillInvoke(skill)
		if log.from then
			if tos:contains(log.from) and tos:length()<2
			then log.type = "#SkillAvoid" end
			room:notifySkillInvoked(log.from,skill)
			room:sendLog(log)
		else
			log.type = "#SkillAvoid"
			for _,to in sgs.qlist(tos)do
				log.from = to
				room:sendLog(log)
			end
		end
	end
	self:cardOnUse(room,card_use)
end

function isAvailable_GlobalEffect(self,player)
	local players = player:getAliveSiblings()
	players:append(player)
	for _,p in sgs.qlist(players)do
		if player:isProhibited(p,self) then continue end
		return self:cardIsAvailable(player)
	end
end

function onUse_GlobalEffect(self,room,card_use)
	if card_use.to:isEmpty() then
		card_use.to = room:getAllPlayers()
	end
	local tos = sgs.SPlayerList()
	for _,to in sgs.qlist(card_use.to)do
		tos:append(to)
	end
	local skillBans = {}
	for _,to in sgs.qlist(tos)do
		local skill = room:isProhibited(card_use.from,to,self)
		if skill then
			if skill:isVisible() then
				skillBans[skill:objectName()] = skillBans[skill:objectName()] or sgs.SPlayerList()
				skillBans[skill:objectName()]:append(to)
			else
				skill = sgs.Sanguosha:getMainSkill(skill:objectName())
				skillBans[skill:objectName()] = skillBans[skill:objectName()] or sgs.SPlayerList()
				skillBans[skill:objectName()]:append(to)
			end
			card_use.to:removeOne(to)
		end
	end
	for skill,tos in pairs(skillBans)do
		local log = sgs.LogMessage()
		log.type = "#SkillAvoidFrom"
		log.from = room:findPlayerBySkillName(skill)
		log.to = tos
		log.arg = skill
		log.arg2 = self:objectName()
		room:broadcastSkillInvoke(skill)
		if log.from then
			if tos:contains(log.from) and tos:length()<2
			then log.type = "#SkillAvoid" end
			room:notifySkillInvoked(log.from,skill)
			room:sendLog(log)
		else
			log.type = "#SkillAvoid"
			for _,to in sgs.qlist(tos)do
				log.from = to
				room:sendLog(log)
			end
		end
	end
	self:cardOnUse(room,card_use)
end

function onUse_DelayedTrick(self,room,card_use)--不启用
	local data = sgs.QVariant()
	data:setValue(card_use)
	local thread = room:getThread()
	thread:trigger(sgs.PreCardUsed,room,card_use.from,data)
	card_use = data:toCardUse()
	local log = sgs.LogMessage()
	log.from = card_use.from
	log.to = card_use.to
	log.type = "#UseCard"
	log.card_str = self:toString()
	room:sendLog(log)
	local reason = sgs.CardMoveReason(sgs.CardMoveReason_S_REASON_USE,card_use.from:objectName(),self:getSkillName(),"")
	if card_use.to:size()==1 then reason.m_targetId = card_use.to:first():objectName() end
	reason.m_extraData:setValue(card_use.card)
	reason.m_useStruct = card_use
	room:moveCardTo(self,nil,sgs.Player_PlaceTable,reason,true)
	thread:trigger(sgs.CardUsed,room,card_use.from,data)
	if room:CardInTable(self) then
		room:moveCardTo(self,nil,sgs.Player_DiscardPile,reason,true)
	end
	card_use = data:toCardUse()
	thread:trigger(sgs.CardFinished,room,card_use.from,data)
end

function use_DelayedTrick(self,room,source,targets)
	if not room:CardInTable(self) then return end
	local use = room:getTag("UseHistory"..self:toString()):toCardUse()
	local reason = sgs.CardMoveReason(sgs.CardMoveReason_S_REASON_USE,source:objectName(),self:getSkillName(),"")
	reason.m_extraData:setValue(self:getRealCard())
	for _,p in ipairs(targets)do
		if p:isDead() or p:containsTrick(self:objectName()) then continue end
		if table.contains(use.nullified_list,"_ALL_TARGETS") or table.contains(use.nullified_list,p:objectName()) then
			local log = sgs.LogMessage()
			log.type = "#CardNullified"
			log.card_str = self:toString()
			log.from = p
			room:sendLog(log)
			room:setEmotion(p, "skill_nullify");
			continue
		end
		if p:hasJudgeArea() then
			local wrapped = sgs.Sanguosha:getWrappedCard(self:getEffectiveId())
			if self:isVirtualCard(true) then
				wrapped:takeOver(sgs.Sanguosha:cloneCard(self))
				room:broadcastUpdateCard(room:getPlayers(), wrapped:getId(), wrapped)
			end
			reason.m_targetId = p:objectName()
			reason.m_useStruct = sgs.CardUseStruct(wrapped,source,use.to)
			room:moveCardTo(wrapped,p,sgs.Player_PlaceDelayedTrick,reason,true)
			return
		end
	end
	reason.m_useStruct = sgs.CardUseStruct(self,source,use.to)
	room:moveCardTo(self,nil,sgs.Player_DiscardPile,reason,true)
end

function onNullified_DelayedTrick_movable(self,source,targets)
	if self:getEffectiveId()<0 then return end
	local room = source:getRoom()
	targets = targets or room:getOtherPlayers(source)
	if not targets:contains(source) then targets:append(source) end
	for _,target in sgs.qlist(targets)do
		if target:containsTrick(self:objectName()) then continue end
		local log = sgs.LogMessage()
		if not target:hasJudgeArea() then
			log.type = "#NoJudgeAreaAvoid"
			log.from = target
			log.arg = self:objectName()
			room:sendLog(log)
			continue
		end
		local skill = room:isProhibited(source,target,self)
		if skill then
			if not skill:isVisible() then
				skill = sgs.Sanguosha:getMainSkill(skill:objectName())
			end
			log.arg = skill:objectName()
			log.arg2 = self:objectName()
			log.type = "#SkillAvoidFrom"
			if target:hasSkill(skill) then
				log.type = "#SkillAvoid"
				log.from = target
				room:sendLog(log)
				room:broadcastSkillInvoke(log.arg)
				room:notifySkillInvoked(target,log.arg)
			else
				for _,owner in sgs.list(room:getAllPlayers())do
					if owner:hasSkill(skill) then
						log.from = owner
						log.to:append(target)
						room:sendLog(log)
						room:broadcastSkillInvoke(log.arg)
						room:notifySkillInvoked(owner,log.arg)
						break
					end
				end
			end
			continue
		end--[[
		local wrapped = sgs.Sanguosha:getWrappedCard(self:getEffectiveId())
		if self:isVirtualCard(true) then
			wrapped:takeOver(sgs.Sanguosha:cloneCard(self))
			room:broadcastUpdateCard(room:getPlayers(), wrapped:getId(), wrapped)
		end--]]
		local reason = sgs.CardMoveReason(sgs.CardMoveReason_S_REASON_TRANSFER,source:objectName(),self:getSkillName(),"")
		reason.m_extraData:setValue(self:getRealCard())
		reason.m_useStruct = sgs.CardUseStruct(self,nil,target)
		if target ~= source then
			local data = sgs.QVariant()
			data:setValue(reason.m_useStruct)
			local thread = room:getThread()
			thread:trigger(sgs.TargetConfirming,room,target,data)
			reason.m_useStruct = data:toCardUse()
			if reason.m_useStruct.to:isEmpty() then continue end--self:on_nullified(target)
			for _,p in sgs.qlist(room:getAllPlayers())do
				thread:trigger(sgs.TargetConfirmed,room,p,data)
			end
		end
		for _,p in sgs.qlist(reason.m_useStruct.to)do
			reason.m_targetId = p:objectName()
			room:moveCardTo(self,source,p,sgs.Player_PlaceDelayedTrick,reason,true)
			return
		end
	end
	onNullified_DelayedTrick_unmovable(self,source)
end

function onNullified_DelayedTrick_unmovable(self,source)
	local reason = sgs.CardMoveReason(sgs.CardMoveReason_S_REASON_NATURAL_ENTER,source:objectName())
	reason.m_extraData:setValue(self:getRealCard())
	source:getRoom():throwCard(self,reason,nil)
end

--============================================

function sgs.CreateTrickCard(spec)
	assert(type(spec.name)=="string" or type(spec.class_name)=="string")
	spec.name = spec.name or spec.class_name
	spec.class_name = spec.class_name or spec.name
	spec.suit = spec.suit or sgs.Card_SuitToBeDecided
	spec.number = spec.number or 0
	assert(type(spec.suit)=="number")
	assert(type(spec.number)=="number")
	if type(spec.subtype)~="string" then
		local subtype_table = {"TrickCard","single_target_trick","delayed_trick","aoe","global_effect"}
		spec.subtype = subtype_table[(spec.subclass or 0)+1]
	end
	local card = sgs.LuaTrickCard(spec.suit,spec.number,spec.name,spec.class_name,spec.subtype)
	if type(spec.target_fixed)=="boolean" then card:setTargetFixed(spec.target_fixed)end
	if type(spec.can_recast)=="boolean" then card:setCanRecast(spec.can_recast) end
	if type(spec.damage_card)=="boolean" then card:setDamageCard(spec.damage_card) end
	if type(spec.is_gift)=="boolean" then card:setGift(spec.is_gift) end
	if type(spec.single_target)=="boolean" then card:setSingleTargetCard(spec.single_target) end
	if type(spec.subclass)=="number" then card:setSubClass(spec.subclass)
	else card:setSubClass(sgs.LuaTrickCard_TypeNormal) end
	if spec.subclass==sgs.LuaTrickCard_TypeDelayedTrick then
--		if not spec.about_to_use then spec.about_to_use = onUse_DelayedTrick end
		if not spec.on_use then spec.on_use = use_DelayedTrick end
		if not spec.on_nullified then
			if spec.movable then spec.on_nullified = onNullified_DelayedTrick_movable
			else spec.on_nullified = onNullified_DelayedTrick_unmovable end
		end
	elseif spec.subclass==sgs.LuaTrickCard_TypeAOE then
		if not spec.available then spec.available = isAvailable_AOE end
		if not spec.about_to_use then spec.about_to_use = onUse_AOE end
		if not spec.target_fixed then card:setTargetFixed(true) end
	elseif spec.subclass==sgs.LuaTrickCard_TypeGlobalEffect then
		if not spec.available then spec.available = isAvailable_GlobalEffect end
		if not spec.about_to_use then spec.about_to_use = onUse_GlobalEffect end
		if not spec.target_fixed then card:setTargetFixed(true) end
	end
	if type(spec.filter)=="function" then
		function card.filter(...)
			local result,vote = spec.filter(...)
			if type(result)=="number" then
				vote = result
				result = result>0
			elseif type(vote)~="number" then
				vote = result and 1 or 0
			end
			return (result~=false and result~=nil),vote
		end
	end
	card.feasible = spec.feasible
	card.available = spec.available
	card.is_cancelable = spec.is_cancelable
	card.on_nullified = spec.on_nullified
	card.about_to_use = spec.about_to_use
	card.on_use = spec.on_use
	card.on_effect = spec.on_effect
	card.on_validate = spec.on_validate
	card.on_validate_in_response = spec.on_validate_in_response
	return card
end

function sgs.CreateViewAsSkill(spec)
	assert(type(spec.name)=="string")
	if spec.response_pattern then assert(type(spec.response_pattern)=="string") end
	if spec.frequency then assert(type(spec.frequency)=="number") end
	if spec.limit_mark then assert(type(spec.limit_mark)=="string") end
	--spec.response_pattern = spec.response_pattern or "@@"..spec.name
	local response_or_use = spec.response_or_use or false
	local frequency = spec.frequency or sgs.Skill_NotFrequent
	if spec.expand_pile then assert(type(spec.expand_pile)=="string") end
	local skill = sgs.LuaViewAsSkill(spec.name,spec.response_pattern,response_or_use,spec.expand_pile or "",frequency,spec.limit_mark or "")
	function skill:view_as(cards)
		return spec.view_as(self,cards)
	end
	spec.n = spec.n or 0
	function skill:view_filter(selected,to_select)
		if #selected>=spec.n then return false end
		return spec.view_filter(self,selected,to_select)
	end
	if type(spec.guhuo_type)=="string" and spec.guhuo_type~="" then skill:setGuhuoDialog(spec.guhuo_type) end
	if type(spec.juguan_type)=="string" and spec.juguan_type~="" then skill:setJuguanDialog(spec.juguan_type) end
	if type(spec.tiansuan_type)=="string" and spec.tiansuan_type~="" then skill:setTiansuanDialog(spec.tiansuan_type) end
	skill.should_be_visible = spec.should_be_visible
	skill.enabled_at_play = spec.enabled_at_play
	skill.enabled_at_response = spec.enabled_at_response
	skill.enabled_at_nullification = spec.enabled_at_nullification
	return skill
end

function sgs.CreateOneCardViewAsSkill(spec)
	local skill = sgs.CreateViewAsSkill(spec)
	function skill:view_as(cards)
		if #cards~=1 then return nil end
		return spec.view_as(self,cards[1])
	end
	function skill:view_filter(selected,to_select)
		if #selected>=1 or to_select:hasFlag("using") then return false end
		if spec.view_filter then return spec.view_filter(self,to_select) end
		if spec.filter_pattern then
			local pat = spec.filter_pattern
			if string.endsWith(pat,"!") then
				if sgs.Self:isJilei(to_select) then return false end
				pat = string.sub(pat,1,-2)
			end
			return sgs.Sanguosha:matchExpPattern(pat,sgs.Self,to_select)
		end
	end
	return skill
end

function sgs.CreateZeroCardViewAsSkill(spec)
	local skill = sgs.CreateViewAsSkill(spec)
	function skill:view_as(cards)
		if #cards>0 then return nil end
		return spec.view_as(self)
	end
	function skill:view_filter(selected,to_select)
		return false
	end
	return skill
end

function sgs.CreateEquipCard(spec)
	assert(type(spec.location)=="number")
	assert(type(spec.name)=="string" or type(spec.class_name)=="string")
	spec.name = spec.name or spec.class_name
	spec.class_name = spec.class_name or spec.name
	spec.suit = spec.suit or sgs.Card_SuitToBeDecided
	spec.number = spec.number or 0
	local card
	if spec.location==sgs.EquipCard_WeaponLocation
	then card = sgs.LuaWeapon(spec.suit,spec.number,spec.range or 1,spec.name,spec.class_name)
	elseif spec.location==sgs.EquipCard_ArmorLocation
	then card = sgs.LuaArmor(spec.suit,spec.number,spec.name,spec.class_name)
	elseif spec.location==sgs.EquipCard_OffensiveHorseLocation
	then card = sgs.LuaOffensiveHorse(spec.suit,spec.number,spec.correct or -1,spec.name,spec.class_name)
	elseif spec.location==sgs.EquipCard_DefensiveHorseLocation
	then card = sgs.LuaDefensiveHorse(spec.suit,spec.number,spec.correct or 1,spec.name,spec.class_name)
	elseif spec.location==sgs.EquipCard_TreasureLocation
	then card = sgs.LuaTreasure(spec.suit,spec.number,spec.name,spec.class_name) end
	assert(card)
	if spec.equip_skill then addToSkills(spec.equip_skill) end
	if type(spec.is_gift)=="boolean" then card:setGift(spec.is_gift) end
	if type(spec.target_fixed)=="boolean" then card:setTargetFixed(spec.target_fixed)end
	--if type(spec.feasible)=="function" then
		card.feasible = spec.feasible
	--end
	if type(spec.filter)=="function" then
		function card.filter(...)
			local result,vote = spec.filter(...)
			if type(result)=="number" then
				vote = result
				result = result>0
			elseif type(vote)~="number" then
				vote = result and 1 or 0
			end
			return (result~=false and result~=nil),vote
		end
	end
	card.available = spec.available
	card.on_install = spec.on_install
	card.on_uninstall = spec.on_uninstall
	return card
end

function sgs.CreateWeapon(spec)
	spec.location = sgs.EquipCard_WeaponLocation
	return sgs.CreateEquipCard(spec)
end

function sgs.CreateArmor(spec)
	spec.location = sgs.EquipCard_ArmorLocation	
	return sgs.CreateEquipCard(spec)
end

function sgs.CreateOffensiveHorse(spec)
	spec.location = sgs.EquipCard_OffensiveHorseLocation
	return sgs.CreateEquipCard(spec)
end

function sgs.CreateDefensiveHorse(spec)
	spec.location = sgs.EquipCard_DefensiveHorseLocation
	return sgs.CreateEquipCard(spec)
end

function sgs.CreateTreasure(spec)
	spec.location = sgs.EquipCard_TreasureLocation
	return sgs.CreateEquipCard(spec)
end

function sgs.LoadTranslationTable(t)
	for key,value in pairs(t)do
		sgs.AddTranslationEntry(key,value)
	end
end