function SmartAI:useCardThunderSlash(...)
	self:useCardSlash(...)
end

sgs.ai_card_intention.ThunderSlash = sgs.ai_card_intention.Slash

sgs.card_damage_nature.ThunderSlash = "T"

sgs.ai_use_value.ThunderSlash = 4.55
sgs.ai_keep_value.ThunderSlash = 3.66
sgs.ai_use_priority.ThunderSlash = 2.5

function SmartAI:useCardFireSlash(...)
	self:useCardSlash(...)
end

sgs.card_damage_nature.FireSlash = "F"

sgs.ai_card_intention.FireSlash = sgs.ai_card_intention.Slash

sgs.ai_use_value.FireSlash = 4.6
sgs.ai_keep_value.FireSlash = 3.63
sgs.ai_use_priority.FireSlash = 2.5

sgs.weapon_range.fan = 4
sgs.ai_use_priority.Fan = 2.655
sgs.ai_use_priority.Vine = 0.95

sgs.ai_skill_invoke.fan = function(self,data)
	local use = data:toCardUse()
	for _,target in sgs.list(use.to)do
		local fs = dummyCard("fire_slash")
		fs:setSkillName("fan")
		fs:addSubcard(use.card)
		local ad = self:ajustDamage(self.player,target,1,fs)
		if self:isFriend(target) then
			if ad==0 or target:isChained() and self:isGoodChainTarget(target,fs) then return true end
		else
			if target:isChained() then if self:isGoodChainTarget(target,fs) then return true end
			elseif ad>=self:ajustDamage(self.player,target,1,use.card) then return true end
		end
	end
	return false
end

sgs.ai_view_as.fan = function(card,player,card_place)
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	if card_place~=sgs.Player_PlaceSpecial and card:isKindOf("Slash")
	and not card:isKindOf("NatureSlash")
	then
		return ("fire_slash:fan[%s:%s]=%d"):format(suit,number,card_id)
	end
end

local fan_skill={}
fan_skill.name="fan"
table.insert(sgs.ai_skills,fan_skill)
fan_skill.getTurnUseCard=function(self)
	local cards = self:addHandPile()
	cards = self:sortByKeepValue(cards,nil,"l")
	for d,card in sgs.list(cards)do
		if card:isKindOf("Slash")
		and not card:isKindOf("NatureSlash")
		then
			d = dummyCard("fire_slash")
			d:setSkillName("fan")
			d:addSubcard(card)
			if d:isAvailable(self.player)
			then return d end
		end
	end
end

function sgs.ai_weapon_value.fan(self,enemy)
	if enemy and self:ajustDamage(self.player,enemy,1,nil,"F")>1 then return 6 end
end

function sgs.ai_armor_value.vine(player,self)
	if player:getHandcardNum()==1 and self:needKongcheng(player)
	then return player:hasSkill("kongcheng") and 5 or 3.8 end
	if player:hasSkills(sgs.lose_equip_skill) then return 3.8 end
	if not self:damageIsEffective(player,sgs.DamageStruct_Fire) and not player:getArmor() then return 6 end
	if self.player:hasSkill("sizhan") then return 4.9 end
	if player:getPhase()==sgs.Player_Play and not player:getArmor() then
		if #self:getFriends(player,true)>0 and player:hasSkill("jujian")
		or player:hasSkill("diyyicong")
		then return 3 end
	end
	if player:isChained() and not(self:isGoodChainTarget(player,dummyCard("fire_slash"))
	and self:isGoodChainTarget(player,dummyCard("thunder_slash")))
	then return -2 end
	for _,enemy in sgs.list(self:getEnemies(player))do
		if enemy:canSlash(player) and enemy:hasWeapon("Fan")
		or getKnownCard(enemy,player,"FireSlash,FireAttack,Fan",true)>0
		or enemy:getMark("@flame")>0 and enemy:hasSkill("yeyan")
		or enemy:hasSkills("huoji|longhun|shaoying|zonghuo|wuling")
		then return -2 end
	end
	if #self.enemies<3 and sgs.turncount>2 or player:getHp()<=2 then return 5 end
	if player:getPile("counter"):length()>1 and player:hasSkill("xiansi") then return 3 end
	return 0
end

function SmartAI:useCardAnaleptic(card,use)
	if card:subcardsLength()+self:getOverflow()>1 and not(self.player:hasEquip(card) or self:isWeak() or self:hasLoseHandcardEffective())
	or card:getEffectiveId()>=0 and self.room:getCardOwner(card:getEffectiveId())~=self.player then use.card = card
	elseif sgs.Sanguosha:getCurrentCardUseReason()==sgs.CardUseStruct_CARD_USE_REASON_PLAY then
		if sgs.turncount<=1 and self.role=="renegade" and sgs.isLordHealthy() and self:getOverflow()<2
		or self.player:hasFlag("canshi") and self.player:getHandcardNum()<3
		then return end
		local n,cs = 0,{}
		for _,c in ipairs(self:sortByDynamicUsePriority(self:getCards("Slash")))do
			if c:getSubcards():contains(card:getEffectiveId()) then continue end
			self.player:addHistory("Slash",n)
			local can = c:isAvailable(self.player)
			self.player:addHistory("Slash",-n)
			can = can and self:aiUseCard(c)
			if can and can.card then
				table.insert(cs,can)
				n = n+1
			end
		end
		if #cs<1 then return end
		local tm = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,card,self.player)
		for i,d in ipairs(cs)do
			if #cs-i>tm then return end
			for _,to in sgs.qlist(d.to)do
				if self:isFriend(to)
				or to:getHandcardNum()>0 and self:getOverflow()<0 and to:hasSkill("anxian")
				or to:hasSkills("zhenlie|mobilejxtpjinjiu")
				then return end
				if d.card:hasFlag("Qinggang") and to:hasArmorEffect("SilverLion") then
				else
					local da = self:ajustDamage(self.player,to,2,d.card)
					if da==0 or da==1 then return end
				end
				n = getKnownCard(to,self.player,"Jink",true,"he")
				if to:isFemale() and self.player:hasSkill("roulin")
				or self.player:isFemale() and to:hasSkill("roulin")
				or self.player:hasSkill("wushuang")
				then if n>1 then return end end
				if n>=self.player:getHp() and self.player:hasSkill("kofliegong")
				or (n>=self.player:getHp() or n<=self.player:getAttackRange()) and self.player:hasSkill("liegong")
				then use.card = card return end
				if n>0 and self:getOverflow()<2
				then return end
			end
		end
		use.card = card
	elseif #self.toUse>1 then
		for _,c in ipairs(self.toUse)do
			if c:isKindOf("Slash")
			or c:toString():match("slash")
			then use.card = card break end
		end
	end
end

function SmartAI:searchForAnaleptic(slash,enemy)
	if self:isFriend(enemy)
	or enemy:hasSkills("zhenlie|mobilejxtpjinjiu")
	or enemy:hasSkill("anxian") and enemy:getHandcardNum()>0 and self:getOverflow()<0
	or sgs.turncount<=1 and self.role=="renegade" and sgs.isLordHealthy() and self:getOverflow()<2
	or self.player:hasFlag("canshi") and self.player:getHandcardNum()<3
	then return end
	if enemy:hasArmorEffect("SilverLion") and self.player:hasWeapon("QinggangSword") then
	else
		local d = self:ajustDamage(self.player,enemy,2,slash)
		if d==0 or d==1 then return end
	end
	for _,a in ipairs(self:getCard("Analeptic",true))do
		if a:isKindOf("SkillCard") and not sgs.Analeptic_IsAvailable(player) then continue end
		if a:getEffectiveId()~=slash:getEffectiveId()
		and a:isAvailable(self.player) then
			local sa = {}
			for _,c in ipairs(self.toUse)do
				if c:isKindOf("Slash")
				or c:isKindOf("SkillCard") and c:toString():match("slash")
				then table.insert(sa,c:getEffectiveId()) end
			end
			local n = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,a,self.player)
			if n<1 and sa[#sa]~=slash:getEffectiveId()
			or n>0 and n<#sa then continue end
			n = enemy:getHandcardNum()
			if enemy:hasFlag("dahe")
			or self.player:hasSkill("tieji")
			or self.player:getCardCount()>4 and self.player:hasWeapon("Axe")
			or n>=self.player:getHp() and self.player:hasSkill("kofliegong")
			or (n>=self.player:getHp() or n<=self.player:getAttackRange()) and self.player:hasSkill("liegong")
			then return a end
			n = getKnownCard(enemy,self.player,"Jink",true,"he")
			if enemy:isFemale() and self.player:hasSkill("roulin")
			or self.player:isFemale() and enemy:hasSkill("roulin")
			or self.player:hasSkill("wushuang")
			then if n<2 then return a end end
			if n<1 or self:getOverflow()>1
			then return a else return end
		end
	end
end

sgs.dynamic_value.benefit.Analeptic = true

sgs.ai_use_value.Analeptic = 5.98
sgs.ai_keep_value.Analeptic = 4.1
sgs.ai_use_priority.Analeptic = 3.0

local function handcard_subtract_hp(a,b)
	local diff1 = a:getHandcardNum()-a:getHp()
	local diff2 = b:getHandcardNum()-b:getHp()
	return diff1<diff2
end

function SmartAI:useCardSupplyShortage(card,use)
	local enemies = self:exclude(self.enemies,card)
	if #enemies<1 then return end
	local zhanghe = self.room:findPlayerBySkillName("qiaobian")
	local zhanghe_seat = zhanghe and zhanghe:faceUp() and zhanghe:getHandcardNum()>0 and not self:isFriend(zhanghe) and zhanghe:getSeat() or 0
	local sb_daqiao = self.room:findPlayerBySkillName("yanxiao")
	local yanxiao = sb_daqiao and not self:isFriend(sb_daqiao) and sb_daqiao:faceUp() and
		(getKnownCard(sb_daqiao,self.player,"diamond",nil,"he")>0
		or sb_daqiao:getHandcardNum()+self:ImitateResult_DrawNCards(sb_daqiao,sb_daqiao:getVisibleSkillList(true))>3
		or sb_daqiao:containsTrick("YanxiaoCard"))
	local getvalue = function(enemy)
		if type(enemy)~="userdata"
		or enemy:getMark("juao")>0
		or enemy:containsTrick("YanxiaoCard")
		or enemy:containsTrick("supply_shortage")
		or enemy:getJudgingArea():isEmpty() and enemy:getHandcardNum()>0 and enemy:hasSkill("qiaobian")
		or zhanghe_seat>0 and (self:playerGetRound(zhanghe)<=self:playerGetRound(enemy) and self:enemiesContainsTrick()<=1 or not enemy:faceUp())
		or yanxiao and (self:playerGetRound(sb_daqiao)<=self:playerGetRound(enemy) and self:enemiesContainsTrick(true)<=1 or not enemy:faceUp())
		then return -100 end
		local value = -enemy:getHandcardNum()
		for _,sk in ipairs(sgs.getPlayerSkillList(enemy))do
			if string.find(sk:getDescription(),"摸牌阶段") then value = value+3 end
			if sk:inherits("ViewAsSkill") then value = value+2 end
			local s = sgs.Sanguosha:getTriggerSkill(sk:objectName())
			if s and s:hasEvent(sgs.DrawNCards) then value = value+2 end
			if s and s:hasEvent(sgs.AfterDrawNCards) then value = value+2 end
			if s and s:hasEvent(sgs.AskForRetrial) then value = value-3 end
			if s and s:hasEvent(sgs.FinishJudge) then value = value-2 end
		end
		if enemy:hasSkills("manjuan|beige") then value = value+10 end
		if enemy:hasSkills("qinyin|zhaoxin|toudu|renjie") then value = value+5 end
		if enemy:hasSkills("buyi|chongzhen") then value = value+1 end
		if self:isWeak(enemy) then value = value+5 end
		if enemy:isLord() then value = value+3 end
		if not enemy:faceUp() then value = value-10 end
		if enemy:hasSkills("keji|shensu|qingyi") then value = value-enemy:getHandcardNum() end
		if self:hasGuanxingEffect(enemy) then value = value-5 end
		if not self:isGoodTarget(enemy,self.enemies,card) then value = value-1 end
		if self:needKongcheng(enemy) then value = value-1 end
		if enemy:getMark("@kuiwei")>0 then value = value-2 end
		return value
	end
	local pvs = {}
	for _,ep in ipairs(enemies)do
		pvs[ep:objectName()] = getvalue(ep)
	end
	local function cmp(a,b)
		return pvs[a:objectName()]>pvs[b:objectName()]
	end
	table.sort(enemies,cmp)
	for _,ep in ipairs(enemies)do
		if pvs[ep:objectName()]>-100
		and use.to then
			use.card = card
			use.to:append(ep)
			break
		end
	end
end

sgs.ai_use_value.SupplyShortage = 7
sgs.ai_keep_value.SupplyShortage = 3.48
sgs.ai_use_priority.SupplyShortage = 0.5
sgs.ai_card_intention.SupplyShortage = 120

sgs.dynamic_value.control_usecard.SupplyShortage = true

function SmartAI:getChainedFriends(player)
	player = player or self.player
	local chainedFriends = {}
	for _,friend in sgs.list(self:getFriends(player))do
		if friend:isChained() then
			table.insert(chainedFriends,friend)
		end
	end
	return chainedFriends
end

function SmartAI:getChainedEnemies(player)
	player = player or self.player
	local chainedEnemies = {}
	for _,enemy in sgs.list(self:getEnemies(player))do
		if enemy:isChained() then
			table.insert(chainedEnemies,enemy)
		end
	end
	return chainedEnemies
end

function SmartAI:isGoodChainPartner(player)
	player = player or self.player
	if self:isGoodHp(player)
	and self:needToLoseHp(player)
	then return true end
end

function SmartAI:isGoodChainTarget(who,nature_card,source,damagecount)
	source = source or self.player
	if hasJueqingEffect(source,who) then return not self:isFriend(who) end
	local card = type(nature_card)=="userdata" and nature_card
	local nature = card and "N" or nature_card
	damagecount = self:ajustDamage(source,who,damagecount,card,nature)
	if damagecount<1 then return end
	local kills,good,bad,killlord,the_enemy = 0,0,0,nil,nil
	function getChainedPlayerValue(target)
		local newvalue = -damagecount
		if self:isGoodChainPartner(target) then newvalue = newvalue+2 end
		if self:isWeak(target) then newvalue = newvalue-1 end
		if self:cantbeHurt(target,source,damagecount) then newvalue = newvalue-10 end
		if damagecount>=target:getHp() then
			newvalue = newvalue-4
			if isLord(target) then killlord = true newvalue = newvalue-5 end
			if self:isEnemy(target) then kills = kills+1 end
		else
			if self:isEnemy(target,source)
			and target:hasSkills("ganglie|neoganglie")
			and source:getHandcardNum()+source:getHp()+getCardsNum("Peach",source,self.player)<3
			then newvalue = newvalue+3 end
			if target:hasSkill("vsganglie") then
				for _,t in sgs.list(self:getEnemies(target))do
					if t:getHp()+getCardsNum("Peach",t,self.player)+t:getHandcardNum()<3
					and self:damageIsEffective(t,nil,target) then
						newvalue = newvalue+3
						if isLord(t) then
							newvalue = newvalue+5
							killlord = true
						end
					end
				end
			end
		end
		return newvalue
	end
	local f_num,e_num = 0,0
	if self:isFriend(who) then
		f_num = f_num+1
		good = getChainedPlayerValue(who)
	elseif self:isEnemy(who) then
		e_num = e_num+1
		bad = getChainedPlayerValue(who)
	end
	if nature=="N" then return bad~=0 and good>bad end
	for _,p in sgs.list(self.room:getOtherPlayers(who))do
		if p:isChained() then
			local dn = damagecount
			if self:ajustDamage(source,p,dn,card,nature)>0 then
				if self:isFriend(p) then
					f_num = f_num+1
					good = good+getChainedPlayerValue(p)
				elseif self:isEnemy(p) then
					e_num = e_num+1
					bad = bad+getChainedPlayerValue(p)
					if p:isKongcheng() and p:getHp()<2 then the_enemy = p end
				end
				if killlord then
					killlord = getLord(self.player)
					if self:isFriend(killlord)
					then if self:getCardsNum("Peach")<1 then return end
					elseif self:isEnemy(killlord) then
						if card then self.room:setCardFlag(card,"AIGlobal_KillOff") end
						if not sgs.GetConfig("EnableHegemony",false)
						or sgs.getDefenseSlash(p,self)<2
						then return true end
					end
					killlord = nil
				elseif kills>=#self.enemies then --小概率会玩脱，但是这很刺激 ψ(｀∇´)ψ
					if card then self.room:setCardFlag(card,"AIGlobal_KillOff") end
					return true
				end
			end
		end
	end
	if card and the_enemy then
		for _,c in sgs.list(self:getCards("Slash"))do
			if not(c:isKindOf("NatureSlash") or self:slashProhibit(c,the_enemy,source)) then return end
		end
	end
	return e_num>0 and e_num>=f_num and good>bad
end

sgs.ai_use_revises.yinbing = function(self,card,use)
	if card:isKindOf("IronChain")
	and self.player:getPile("yinbing"):isEmpty()
	and self.player:getHandcardNum()-self:getCardsNum("BasicCard")==1
	and not self:isWeak()--祖茂的引兵：如果手里只有一张铁索，这时最大效率化还是将其保留为好
	then use.card = card end
end

sgs.ai_use_revises.manjuan = function(self,card,use)
	if card:isKindOf("IronChain")
	and self:getOverflow()<=0
	then use.card = card end
end

function SmartAI:useCardIronChain(card,use)
	if card:canRecast() then use.card = card end
	if self.player:isLocked(card) then return end
	local friendtargets,friendtargets2,otherfriends = {},{},{}
	self:sort(self.friends,"defense")
	for _,friend in ipairs(self.friends)do
		if friend:isChained() and not self:isGoodChainPartner(friend) then
			if friend:containsTrick("lightning")
			then table.insert(friendtargets,friend)
			else table.insert(friendtargets2,friend) end
		else table.insert(otherfriends,friend) end
	end
	local extraTarget = 1+sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,card)
	if use.extra_target then extraTarget = extraTarget+use.extra_target end
	InsertList(friendtargets,friendtargets2)
	for _,friend in ipairs(friendtargets)do
		if isCurrent(use,friend) then continue end
		if CanToCard(card,self.player,friend) then
			use.to:append(friend)
			if use.to:length()>extraTarget
			then return end
		end
	end
	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if isCurrent(use,enemy) then continue end
		if CanToCard(card,self.player,enemy) and self:objectiveLevel(enemy)>2
		and not(enemy:isChained() or self:needToLoseHp(enemy))
		and self:isGoodTarget(enemy,self.enemies) then
			use.to:append(enemy)
			if use.to:length()>extraTarget
			then return end
		end
	end
	if not isCurrent(use,self.player)
	and use.to:length()>0 and not use.to:contains(self.player) and CanToCard(card,self.player,self.player)
	and self:needToLoseHp(self.player) and not(self.player:isChained() or hasJueqingEffect(self.player))
	and (self:getCard("NatureSlash") or self:getCard("FireAttack") and self.player:getHandcardNum()>2)
	then use.to:append(self.player) if use.to:length()>extraTarget then return end end
	for _,friend in ipairs(otherfriends)do
		if isCurrent(use,friend) then continue end
		if not use.to:contains(friend)
		and CanToCard(card,self.player,friend)
		and self:hasHuangenEffect(friend) then
			use.to:append(friend)
			if use.to:length()>extraTarget
			then return end
		end
	end
	if use.to:length()<2 then use.to:clear()
	else use.card = card end
end

sgs.ai_card_intention.IronChain = function(self,card,from,tos)
	for _,to in sgs.list(tos)do
		if to:isChained() then
			sgs.updateIntention(from,to,-60)
		else
			local n = 60
			if #tos>1 and (to:hasSkill("danlao") or self:hasHuangenEffect(to))
			then n = -30 end
			sgs.updateIntention(from,to,n)
		end
	end
end

sgs.ai_use_value.IronChain = 5.4
sgs.ai_keep_value.IronChain = 3.34
sgs.ai_use_priority.IronChain = 9.1

sgs.ai_skill_cardask["@fire-attack"] = function(self,data,pattern,target)
	local cards = self.player:getCards("h")
	cards = self:sortByUseValue(cards,true)
	local convert = {[".S"]="spade",[".D"]="diamond",[".H"]="heart",[".C"]="club",[".N"]="no_shit"}
	for i,c in sgs.list(cards)do
		if c:getSuitString()==convert[pattern] then
			if self:isFriend(target) then
				if target:isChained()
				or self:needToLoseHp(target,self.player,sgs.cardEffect.card)
				then else break end
			end
			if self.player:isJilei(c) then continue end
			if i>#cards/2 and isCard("Peach",c,self.player) then
				if target:isChained()
				then if self:isGoodChainTarget(target,sgs.cardEffect.card) then return c:getId() end
				elseif not self:isWeak(nil,false) or self:ajustDamage(self.player,target,1,sgs.cardEffect.card)>1
				then return c:getId() end
			else
				if target:isChained() then
					if self:isGoodChainTarget(target,sgs.cardEffect.card)
					then return c:getId() end
				else return c:getId() end
			end
		end
	end
	return "."
end

function SmartAI:useCardFireAttack(fire_attack,use)
	local lack = {spade=false,club=false,heart=false,diamond=false,no_shit=false}
	local canDis = {}
	for _,h in sgs.list(self.player:getHandcards())do
		if h:getEffectiveId()~=fire_attack:getEffectiveId()
		and self.player:canDiscard(self.player,h:getEffectiveId())
		then
			table.insert(canDis,h)
			lack[h:getSuitString()] = true
		end
	end
	local suitnum = 0
	for suit,islack in pairs(lack)do
		if islack then suitnum = suitnum+1  end
	end
	self:sort(self.enemies,"defense")
	local function can_attack(enemy)
		if self.player:hasFlag("FireAttackFailed_"..enemy:objectName())
		or self:cantbeHurt(enemy,self.player) then return end
		return self:objectiveLevel(enemy)>2
		and self:isGoodTarget(enemy,self.enemies,fire_attack)
		and (hasJueqingEffect(self.player,enemy) or not(enemy:hasSkill("jianxiong") and not self:isWeak(enemy)
		or enemy:isChained() and not self:isGoodChainTarget(enemy,fire_attack)
		or self:needToLoseHp(enemy,self.player,fire_attack)))
	end
	local enemies,targets = {},{}
	for _,enemy in sgs.list(self.enemies)do
		if can_attack(enemy) then table.insert(enemies,enemy) end
	end
	for kc,enemy in sgs.list(enemies)do
		kc = getKnownCards(enemy,self.player)
		if #kc>enemy:getHandcardNum()/2
		then
			local can = 0
			for _,h in sgs.list(kc)do
				if lack[h:getSuitString()]
				then can = can+1 end
			end
			if can>=#kc then table.insert(targets,enemy) end
		end
	end
	local fireAttack_self
	for _,card in sgs.list(canDis)do
		if self:getCardsNum("Peach,Analeptic")>=2
		or not isCard("Peach,Analeptic",card,self.player)
		then fireAttack_self = self.player:isChained() break end
	end
	if self.player:getHandcardNum()>1
	and self.role~="renegade" and fireAttack_self
	and self:isGoodChainTarget(self.player,fire_attack)
	and not self:cantbeHurt(self.player)
	then
		if canNiepan(self.player) or hasBuquEffect(self.player)
		or self:getCardsNum("Peach,Analeptic")+self.player:getHp()>self:ajustDamage(self.player,self.player,1,fire_attack)
		then table.insert(targets,self.player) end
	end
	if (suitnum==2 and lack.diamond or suitnum<=1) and #targets<1
	and self:getOverflow()<=(self.player:hasSkills("jizhi|nosjizhi") and -2 or 0)
	then return end
	for _,enemy in sgs.list(enemies)do
		if table.contains(targets,enemy)
		then else table.insert(targets,enemy) end
	end
	local extraTarget = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,fire_attack)
	if use.extra_target then extraTarget = extraTarget+use.extra_target end
	for _,p in sgs.list(targets)do
		if isCurrent(use,p) then continue end
		if CanToCard(fire_attack,self.player,p)
		and self:ajustDamage(self.player,p,1,fire_attack)~=0
		then
			use.card = fire_attack
			local gs = self:getCard("GodSalvation")
			if gs and gs:getEffectiveId()~=fire_attack:getEffectiveId()
			and p:getLostHp()<1 and self:willUseGodSalvation(gs)
			and self:hasTrickEffective(gs,p,self.player)
			then use.card = gs return end
			use.to:append(p)
			if use.to:length()>extraTarget
			then return end
		end
	end
end

sgs.ai_cardshow.fire_attack = function(self,requestor)
	local cards = self.player:getHandcards()
	cards = self:sortByUseValue(cards,true)
	if requestor:objectName()==self.player:objectName() then return cards[1] end
	local priority = {no_shit=5,heart=4,spade=3,club=2,diamond=1}
	local index,result = -1,cards[1]
	for s,c in sgs.list(cards)do
		s = c:hasSuit() and c:getSuitString() or "no_shit"
		local n = priority[s]
		local cf = CardFilter(c,requestor,sgs.Player_PlaceHand)
		cf = cf:hasSuit() and cf:getSuitString() or "no_shit"
		if cf~=s then n = n+5 end
		if n>index
		then
			result = c
			index = n
		end
	end
	return result
end

sgs.ai_use_value.FireAttack = 4.8
sgs.ai_keep_value.FireAttack = 3.3
sgs.ai_use_priority.FireAttack = sgs.ai_use_priority.Dismantlement+0.1

sgs.card_damage_nature.FireAttack = "F"

sgs.dynamic_value.damage_card.FireAttack = true

sgs.ai_card_intention.FireAttack = 80

sgs.dynamic_value.damage_card.FireAttack = true

