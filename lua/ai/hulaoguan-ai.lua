sgs.ai_skill_invoke["weapon_recast"] = function(self,data)
	if self:hasSkills(sgs.lose_equip_skill,self.player) then return false end
	if self.player:isLord() then
		local card_use = data:toCardUse()
		if card_use.card:objectName()~="Crossbow" then return true end
	else
		if self.player:getWeapon() then return true end
	end
end

sgs.ai_skill_invoke["draw_1v3"] = function(self,data)
	return not self:needKongcheng(self.player,true)
end

sgs.ai_skill_choice.Hulaopass = "recover"

sgs.ai_skill_cardask["@xiuluo"] = function(self,data,pattern)
	if self.player:containsTrick("YanxiaoCard") then return "." end
	if not self.player:containsTrick("indulgence") and not self.player:containsTrick("supply_shortage")
		and not (self.player:containsTrick("lightning") and not self:hasWizard(self.enemies)) then return "." end
	local indul_suit,ss_suit,lightning_suit = nil,nil,nil
	for _,card in sgs.qlist(self.player:getJudgingArea())do
		if card:isKindOf("Indulgence") then indul_suit = card:getSuit() end
		if card:isKindOf("SupplyShortage") then ss_suit = card:getSuit() end
		if card:isKindOf("Lightning") then ss_suit = card:getSuit() end
	end
	if ss_suit then
		for _,card in sgs.qlist(self.player:getHandcards())do
			if card:getSuit()==ss_suit then return "$"..card:getEffectiveId() end
		end
	elseif indul_suit then
		for _,card in sgs.qlist(self.player:getHandcards())do
			if card:getSuit()==indul_suit and not isCard("Peach",self.player,card) then return "$"..card:getEffectiveId() end
		end
	elseif lightning_suit then
		for _,card in sgs.qlist(self.player:getHandcards())do
			if card:getSuit()==lightning_suit and not isCard("Peach",self.player,card) then return "$"..card:getEffectiveId() end
		end
	end
	return "."
end

sgs.ai_skill_askforag.xiuluo = function(self,card_ids)
	for _,id in ipairs(card_ids)do
		local card = sgs.Sanguosha:getCard(id)
		if card:isKindOf("SupplyShortage") then return id end
	end
	for _,id in ipairs(card_ids)do
		local card = sgs.Sanguosha:getCard(id)
		if card:isKindOf("Indulgence") then return id end
	end
	if self:hasWizard(self.enemies) and self.player:containsTrick("lightning") then
		for _,id in ipairs(card_ids)do
			local card = sgs.Sanguosha:getCard(id)
			if card:isKindOf("Lightning") then return id end
		end
	end
	return card_ids[1]
end

sgs.ai_skill_choice.hulaopass_shenlvbu = function(self,choices,data)  --待补充
	local lvbus = choices:split("+")
	return lvbus[math.random(1,#lvbus)]
end


--神躯
sgs.ai_skill_choice.shenqu = function(self,choices,data)
	return true
end

--极武
function CanUsejiwu(self)
	if self:needBear() and self.player:getHandcardNum()>self.player:getMaxCards() then return false end
	
	if not self.player:hasSkill("xuanfeng",true) and self.player:getEquips():length()>0 then return true end
	if not self.player:hasSkill("lieren",true) and self:getCardsNum("Slash")>0 and
		(self.player:getHandcardNum()-self:getCardsNum("Peach")-self:getCardsNum("Slash")>0) then return true end
	local candis = false
	for _,c in sgs.list(self.player:getCards("he"))do
		if c:isKindOf("Weapon") and self.player:canDiscard(self.player,c:getEffectiveId()) then
			candis = true
			break
		end
	end
	if not self.player:hasSkill("qiangxi",true) and (self.player:getHp()>3 or self:getCardsNum("Peach")>0 or candis) then return true end
	self:sort(self.enemies,"hp")
	if not self.player:hasSkill("wansha",true) then
		for _,enemy in sgs.list(self.enemies)do
			if not (enemy:hasSkill("kongcheng") and enemy:isKongcheng()) and self:isWeak(enemy) and self:damageMinusHp(enemy,1)>0 and #self.enemies>1 then
				return true
			end
			if self.player:hasSkill("qiangxi") and self:isWeak(enemy) and self:damageMinusHp(enemy,1)>0 then
				return true
			end
		end
	end
	
	if self:needToThrowArmor() and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId()) then return true end
	
	local num = 0
	if not self.player:hasSkill("xuanfeng",true) then num = num+1 end
	if not self.player:hasSkill("lieren",true) then num = num+1 end
	if not self.player:hasSkill("qiangxi",true) then num = num+1 end
	if not self.player:hasSkill("wansha",true) then num = num+1 end
	if self:needKongcheng(self.player,true) and self.player:getHandcardNum()<=num and num>0 then return true end
	
	return false
end

local jiwu_skill = {}
jiwu_skill.name = "jiwu"
table.insert(sgs.ai_skills,jiwu_skill)
jiwu_skill.getTurnUseCard = function(self,inclusive)
	if not CanUsejiwu(self) then return end
	return sgs.Card_Parse("@JiwuCard=.")
end

sgs.ai_skill_use_func.JiwuCard = function(card,use,self)
	local usable_cards = self.player:getCards("h")
	local use_card = {}
	for _,c in sgs.list(usable_cards)do
		if not c:isKindOf("Peach") and not c:isKindOf("ExNihilo") then
			table.insert(use_card,c)
		end
	end
	if #use_card==0 then return end
	self:sortByKeepValue(use_card)
	use.card = sgs.Card_Parse("@JiwuCard="..use_card[1]:getEffectiveId())
end

sgs.ai_skill_choice.jiwu = function(self,choices,data)
	if self.player:getHandcardNum()-self:getCardsNum("Peach")>0 then
		if not self.player:hasSkill("xuanfeng",true) and self.player:getEquips():length()>0 then
			return "xuanfeng"
		end
		if not self.player:hasSkill("lieren",true) and self:getCardsNum("Slash")>0 and (self.player:getHandcardNum()-self:getCardsNum("Peach")-self:getCardsNum("Slash")>0) then
			return "lieren"
		end
		
		local candis = false
		for _,c in sgs.list(self.player:getCards("he"))do
			if c:isKindOf("Weapon") and self.player:canDiscard(self.player,c:getEffectiveId()) then
				candis = true
				break
			end
		end
		if not self.player:hasSkill("qiangxi",true) and (self.player:getHp()>3 or self:getCardsNum("Peach")>0 or candis) then
			return "qiangxi"
		end
		
		self:sort(self.enemies,"hp")
		if not self.player:hasSkill("wansha",true) then
			for _,enemy in sgs.list(self.enemies)do
				if not (enemy:hasSkill("kongcheng") and enemy:isKongcheng()) and self:isWeak(enemy) and self:damageMinusHp(enemy,1)>0 and #self.enemies>1 then
					return "wansha"
				end
				if self.player:hasSkill("qiangxi") and self:isWeak(enemy) and self:damageMinusHp(enemy,1)>0 then
					return "wansha"
				end
			end
		end
	else
		if not self.player:hasSkill("xuanfeng",true) then
			return "xuanfeng"
		end
		if not self.player:hasSkill("lieren",true) then
			return "lieren"
		end
		if not self.player:hasSkill("qiangxi",true) then
			return "qiangxi"
		end
		if not self.player:hasSkill("wansha",true) then
			return "wansha"
		end
	end
	return choices:split("+")[1]
end

sgs.ai_use_priority.JiwuCard = sgs.ai_use_priority.Slash+0.1
sgs.ai_use_value.JiwuCard = 3

sgs.ai_skill_invoke.wushuangji = function(self,data)
	local target = data:toPlayer()
	if target then
		return self:doDisCard(target,"he") or self:canDraw()
	end
end

sgs.ai_skill_cardchosen.wushuangji = function(self,who,flags,method)
	if self:doDisCard(who,"he")
	then return end
    return -1
end

sgs.ai_use_value.Wushuangji = 3
sgs.ai_keep_value.Wushuangji = 3.9

sgs.ai_skill_invoke.shimandai = function(self,data)
	local use = data:toCardUse()
	if use.card:isDamageCard() then
		if self:canDamageHp(use.from,use.card) then
			return false
		end
	end
	return not self:isFriend(use.from)
end

sgs.ai_use_value.Shimandai = 4.4
sgs.ai_keep_value.Shimandai = 3.9

sgs.ai_target_revises.baihuapao = function(to,card,self)
    if card:isKindOf("NatureSlash") then
		return not hasJueqingEffect(self.player,to)
	end
end

sgs.ai_ajustdamage_to.baihuapao = function(self,from,to,card,nature)
	if nature~="N" and not hasJueqingEffect(from,to) then
		return -99
	end
end

sgs.ai_use_value.Baohuapao = 5.4
sgs.ai_keep_value.Baohuapao = 3.9

sgs.ai_skill_playerchosen.zijinguan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p)
		and self:damageIsEffective(p,"N")
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p)
		and self:damageIsEffective(p,"N")
		then return p end
	end
end

sgs.ai_use_value.Zijinguan = 5.4
sgs.ai_keep_value.Zijinguan = 3.9

function SmartAI:useCardLianjunshengyan(card,use)
	local n = 0
	for _,p in sgs.list(self.room:getAlivePlayers())do
		if self.player:canUse(card,p) then
			if self:isEnemy(p) then
				n = n-1
				if self:isWeak(p) then
					n = n-1
				end
			else
				n = n+1
				if self:isFriend(p) and self:isWeak(p) then
					n = n+1
				end
			end
		end
	end
	if n>=0 then
		use.card = card
	end
end
sgs.ai_use_priority.Lianjunshengyan = 1.4
sgs.ai_keep_value.Lianjunshengyan = 4
sgs.ai_use_value.Lianjunshengyan = 2.7





