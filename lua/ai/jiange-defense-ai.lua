--[[********************************************************************
	Copyright (c) 2013-2014-QSanguosha-Rara

  This file is part of QSanguosha-Hegemony.

  This game is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 3.0
  of the License,or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  See the LICENSE file for more details.

  QSanguosha-Rara
*********************************************************************]]

sgs.ai_skill_invoke.jglingfeng = true

sgs.ai_skill_playerchosen.jglingfeng = function(self,targets)
	self:sort(self.enemies,"hp")
	for _,enemy in ipairs(self.enemies)do
		return enemy
	end
end

sgs.ai_playerchosen_intention.jglingfeng = 80

sgs.ai_skill_invoke.jgbiantian = true

sgs.ai_slash_prohibit.jgbiantian = function(self,from,enemy,card)
        if enemy:getMark("&dawu")>0 and not card:isKindOf("ThunderSlash") then return false end
	return true
end

sgs.ai_skill_choice.jggongshen = function(self,choice)
	self:sort(self.friends_noself)
	local target = nil
	for _,enemy in ipairs(self.enemies)do
		if string.find(enemy:getGeneral():objectName(),"machine") then
                        if enemy:hasArmorEffect("Vine") or enemy:getMark("&kuangfeng")>0 or enemy:getHp()==1 then
				return "damage"
			end
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if string.find(friend:getGeneral():objectName(),"machine") and friend:getLostHp()>0 then
			if self:isWeak(friend) then
				return "recover"
			end
		end
	end
	return "damage"
end

sgs.ai_skill_invoke.jgzhinang = true

sgs.ai_skill_playerchosen.jgzhinang = function(self,targets)
	for _,friend in ipairs(self.friends_noself)do
		if friend:faceUp() and not self:isWeak(friend) then
			if not friend:getWeapon() or friend:hasSkills("rende|jizhi") then
				return friend
			end
		end
	end
	return self.player
end

sgs.ai_playerchosen_intention.jgzhinang = function(self,from,to)
	if not self:needKongcheng(to,true)  then sgs.updateIntention(from,to,-50) end
end

sgs.ai_skill_invoke.jgjingmiao = true

sgs.ai_skill_invoke.jgqiwu = true

sgs.ai_skill_playerchosen.jgqiwu = function(self,targets)
	local target
	self:sort(self.friends,"hp")
	for _,friend in ipairs(self.friends)do
		if  friend:getLostHp()>0 then
			target = friend
		end
	end
	return target
end

sgs.ai_skill_playerchosen.jgtianyun = function(self,targets)
	local target = nil
	local chained = 0
	self:sort(self.enemies,"hp")
	for _,enemy in ipairs(self.enemies)do
        if enemy:getCards("e"):length()>=2 or enemy:getHp()<=1
		or self:ajustDamage(self.player,enemy,1,nil,"F")>1
		then target = enemy break end
	end
	if not target then
		for _,enemy in ipairs(self.enemies)do
			if self:isGoodChainTarget(enemy,"F")
			then target = enemy break end
		end
	end
	if not target then
		for _,enemy in ipairs(self.enemies)do
			if self:damageIsEffective(enemy,sgs.DamageStruct_Fire,self.player)
			then target = enemy break end
		end
	end
	return target
end

sgs.ai_playerchosen_intention.jgtianyun = 80

function sgs.ai_skill_invoke.jglingyu(self,data)
	local weak = 0
	for _,friend in ipairs(self.friends)do
		if friend:getLostHp()>0 then
			weak = weak+1
			if self:isWeak(friend) then
				weak = weak+1
			end
		end
	end
	if not self.player:faceUp() then return true end
	for _,friend in ipairs(self.friends)do
		if friend:hasSkills("fangzhu") then return true end
	end
	return weak>=2
end

sgs.ai_skill_playerchosen.jgleili = function(self,targets)
	self:sort(self.enemies,"hp")
	local target
	for _,enemy in ipairs(self.enemies)do
		if self:isGoodChainTarget(enemy,"T")
		then
			target = enemy
			break
		end
	end
	if not target then
		for _,enemy in ipairs(self.enemies)do
			if self:damageIsEffective(enemy,sgs.DamageStruct_Thunder,self.player)
			then
				target = enemy
				break
			end
		end
	end
	if not target then
		for _,enemy in ipairs(self.enemies)do
			target = enemy
			break
		end
	end
	return target
end

sgs.ai_playerchosen_intention.jgleili = 80

sgs.ai_skill_playerchosen.jgchuanyun = function(self,targets)
	local target
	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if  enemy:getHp()>self.player:getHp() then
			target = enemy
			break
		end
	end
	return target
end

sgs.ai_playerchosen_intention.jgchuanyun = 80

sgs.ai_skill_playerchosen.jgfengxing =  sgs.ai_skill_playerchosen.zero_card_as_slash

sgs.ai_playerchosen_intention.jgfengxing = 80

sgs.ai_skill_playerchosen.jghuodi = function(self,targets)
	local target
	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if enemy:hasSkills("jgtianyu|jgtianyun") and not enemy:faceUp() then
			target = enemy
			break
		end
	end
	if not target then
		self:sort(self.enemies)
		for _,enemy in ipairs(self.enemies)do
			if self:toTurnOver(enemy) and enemy:hasSkills(sgs.priority_skill)
                        and not (enemy:getMark("&dawu")>0 and enemy:hasSkill("jgbiantian")) then
				target = enemy
				break
			end
		end
		if not target then
			for _,enemy in ipairs(self.enemies)do
				if self:toTurnOver(enemy)
                                and not (enemy:getMark("&dawu")>0 and enemy:hasSkill("jgbiantian")) then
					target = enemy
					break
				end
			end
		end
	end
	return target
end

sgs.ai_playerchosen_intention.jghuodi = 80

sgs.ai_skill_invoke.jgjueji = true

sgs.ai_skill_playerchosen.jgdidong = function(self,targets)
	local target
	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if enemy:hasSkills("jgtianyu|jgtianyun") and not self:isWeak(enemy) and not enemy:faceUp() then
			target = enemy
			break
		end
	end
	if not target then
		self:sort(self.enemies)
		for _,enemy in ipairs(self.enemies)do
                        if self:toTurnOver(enemy) and enemy:hasSkills(sgs.priority_skill) and not (enemy:getMark("&dawu")>0 and enemy:hasSkill("jgbiantian")) then
				target = enemy
				break
			end
		end
		if not target then
			for _,enemy in ipairs(self.enemies)do
                                if self:toTurnOver(enemy) and not (enemy:getMark("&dawu")>0 and enemy:hasSkill("jgbiantian")) then
					target = enemy
					break
				end
			end
		end
	end
	return target
end

sgs.ai_playerchosen_intention.jgdidong = 80

sgs.ai_skill_invoke.jglianyu = true

function sgs.ai_skill_invoke.jgdixian(self,data)
	local throw = 0
	for _,enemy in ipairs(self.enemies)do
		throw = throw+enemy:getCards("e"):length()
	end
	if not self.player:faceUp() then return true end
	for _,friend in ipairs(self.friends)do
		if friend:hasSkills("fangzhu") then return true end
	end
	return throw>=3
end

sgs.ai_skill_invoke.jgkonghun = true


sgs.ai_fill_skill.jgjiaoxie = function(self)
    return sgs.Card_Parse("@JGJiaoxieCard=.")
end

sgs.ai_skill_use_func["JGJiaoxieCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,p in sgs.list(self.enemies)do
		if use.to:length()<2 and p:getGeneralName():contains("jg_machine_") and p:getCardCount()>0 then
			use.card = card
			use.to:append(p)
		end
	end
end

sgs.ai_use_value.JGJiaoxieCard = 3.4
sgs.ai_use_priority.JGJiaoxieCard = 9.2

sgs.ai_skill_invoke.jgbashi = function(self,data)
	local use = data:toCardUse()
	if use.card:isKindOf("Slash") then
		return self:getCardsNum("Jink")<1 and self:isWeak()
	end
	if use.card:isDamageCard() then
		local tcn = sgs.aiResponse[use.card:getClassName()] or "Nullification"
		return self:getCardsNum(tcn)<1 and self:isWeak()
	end
end

sgs.ai_skill_invoke.jgdanjing = function(self,data)
	return self:getAllPeachNum()<1
end

sgs.ai_fill_skill.jgyingji = function(self)
    return sgs.Card_Parse("@JGYingjiCard=.")
end

sgs.ai_skill_use_func["JGYingjiCard"] = function(card,use,self)
	local dc = dummyCard()
	local d = self:aiUseCard(dc)
	if d.card then
		use.card = card
		use.to = d.to
	end
end

sgs.ai_use_value.JGYingjiCard = 3.4
sgs.ai_use_priority.JGYingjiCard = 3.7

sgs.ai_skill_cardask.jgweizhu = function(self,data,pattern,prompt)
    local damage = data:toDamage()
    if self:isFriend(damage.to) and self:isWeak(damage.to)
	then return true end
	return "."
end

sgs.ai_fill_skill.jghanjun = function(self)
    return sgs.Card_Parse("@JGHanjunCard=.")
end

sgs.ai_skill_use_func["JGHanjunCard"] = function(card,use,self)
	local n = 0
	for _,p in sgs.list(self.enemies)do
		if p:getCardCount()>0 then
			n = n+1
		end
	end
	if n>=#self.enemies/2 then
		use.card = card
	end
end

sgs.ai_use_value.JGHanjunCard = 3.4
sgs.ai_use_priority.JGHanjunCard = 9.2

sgs.ai_skill_use["@@jgkeding"] = function(self,prompt)
	local user = self.player:property("jgkedingUser"):toString():split("+");
	local dc = sgs.Card_Parse(user[1])
	local valid,ids = {},{}
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- ���б�ת��Ϊ��
    self:sortByKeepValue(cards) -- ������ֵ����
	for _,h in sgs.list(cards)do
		if self:getKeepValue(h)<7 then table.insert(ids,h:toString()) end
	end
	local d = self:aiUseCard(dc,dummy(nil,99,{user[2]}))
    for _,p in sgs.list(d.to)do
    	if #ids>#valid then
			table.insert(valid,p:objectName())
		end
	end
	while #ids>#valid do
		table.removeOne(ids,ids[#ids])
	end
	if #valid<1 then return end
	return string.format("@JGKedingCard=%s:->%s",table.concat(ids,"+"),table.concat(valid,"+"))
end

sgs.ai_skill_invoke.jglongwei = function(self,data)
	return self:getAllPeachNum()<1
end




