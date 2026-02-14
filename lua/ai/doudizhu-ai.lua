--飞扬
sgs.ai_skill_use["@@feiyang"] = function(self,prompt)
	local disaster,indulgence,supply_shortage = -1,-1,-1
	for _,card in sgs.list(self.player:getJudgingArea())do
		if card:isKindOf("Disaster") then disaster = card:getId() end
		if card:isKindOf("Indulgence") then indulgence = card:getId() end
		if card:isKindOf("SupplyShortage") then supply_shortage = card:getId() end
	end
	
	local handcards = {}
	for _,id in sgs.list(self.player:handCards())do
		if self.player:canDiscard(self.player,id) then
			table.insert(handcards,sgs.Sanguosha:getCard(id))
		end
	end
	if #handcards<2 then return "." end
	self:sortByKeepValue(handcards)
	
	local discard = {}
	
	table.insert(discard,handcards[1]:getId())
	table.insert(discard,handcards[2]:getId())
	if disaster>-1 and self:hasSkills(sgs.wizard_skill,self.enemies) then
		table.insert(discard,disaster)
		return "@FeiyangCard="..table.concat(discard,"+")
	end
	
	if indulgence>-1 and self.player:hasSkill("keji") and supply_shortage>-1 then
		table.insert(discard,supply_shortage)
		return "@FeiyangCard="..table.concat(discard,"+")
	end
	
	if indulgence>-1 and self:getCardsNum("Peach")>1 and self:isWeak() then
		table.insert(discard,indulgence)
		return "@FeiyangCard="..table.concat(discard,"+")
	end
	
	if indulgence>-1 and self:getOverflow(self.player)>1 and (not self:isWeak() or not handcards[1]:isKindOf("Peach")) then
		table.insert(discard,indulgence)
		return "@FeiyangCard="..table.concat(discard,"+")
	end
	
	if supply_shortage>-1 and (not self:isWeak() or not handcards[1]:isKindOf("Peach")) then
		table.insert(discard,supply_shortage)
		return "@FeiyangCard="..table.concat(discard,"+")
	end
	
	if not self:isWeak() or not handcards[1]:isKindOf("Peach") then
		table.insert(discard,self.player:getJudgingAreaID():first())
		return "@FeiyangCard="..table.concat(discard,"+")
	end
	
	return "."
end

--队友死亡摸牌
sgs.ai_skill_invoke.doudizhu_reward = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_choice.doudizhu = function(self,choices)
	local items = choices:split("+")
	if table.contains(items,"recover")
	and self:isWeak()
	then return "recover" end
	if table.contains(items,"draw")
	and self:canDraw()
	then return "draw" end
	if table.contains(items,"recover")
	then return "recover" end
end

sgs.ai_skill_invoke.ddzcibei = function(self,data)
	local target = data:toPlayer()
	if target then
		return self:isFriend(target) or not self:isWeak(target)
	end
end

sgs.ai_skill_invoke.longgong = function(self,data)
	local damage = data:toDamage()
	if damage.from then
		return self:isFriend(damage.from) or self:isWeak()
	end
	return true
end

sgs.ai_fill_skill.sitian = function(self)
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards,nil,"j") -- 按保留值排序
	for _,c1 in sgs.list(cards)do
		if self:getOverflow()<0 or #self.enemies<1 then break end
		local ids2 = {}
		table.insert(ids2,c1:getId())
		for _,c2 in sgs.list(cards)do
			if c1:getSuit()~=c2:getSuit() then
				table.insert(ids2,c2:getId())
				return sgs.Card_Parse("@SitianCard="..table.concat(ids2,"+"))
			end
		end
	end
end

sgs.ai_skill_use_func["SitianCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.SitianCard = 3.4
sgs.ai_use_priority.SitianCard = 1.2

sgs.ai_fill_skill.jiuxian = function(self)
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,c in sgs.list(cards)do
		if c:isNDTrick() and not c:isSingleTargetCard() then
			local dc = dummyCard("analeptic")
			dc:setSkillName("jiuxian")
			dc:addSubcard(c)
			if dc:isAvailable(self.player) then
				return dc
			end
		end
	end
end

function sgs.ai_cardsview.jiuxian(self,class_name,player)
   	local cards = sgs.QList2Table(player:getCards("h"))
    self:sortByKeepValue(cards)
	for _,c in sgs.list(cards)do
       	if c:isNDTrick() and not c:isSingleTargetCard() then
	   		return ("analeptic:jiuxian[no_suit:0]="..c:getEffectiveId())
	   	end
	end
end

sgs.ai_skill_invoke.shixian = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_askforag.faqi = function(self,card_ids)
    for _,id in sgs.list(card_ids)do
		local dc = dummyCard(sgs.Sanguosha:getCard(id):objectName())
		dc:setSkillName("faqi")
		if dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then
				if dc:canRecast() and d.to:isEmpty()
				then continue end
				self.faqiUse = d
				return id
			end
		end
	end
end

sgs.ai_skill_use["@@faqi"] = function(self,prompt)
    local dummy = self.faqiUse
   	if dummy.card and dummy.to then
      	local tos = {}
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
       	return dummy.card:toString().."->"..table.concat(tos,"+")
    end
end

sgs.ai_skill_invoke.zhanjian = function(self,data)
	local target = data:toPlayer()
	if target then
		return not self:isFriend(target) and self:canDraw()
	end
end

sgs.ai_skill_playerchosen.ddzbenxi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and self:damageIsEffective(p)
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p)
		then return p end
	end
end

sgs.ai_fill_skill.lisao = function(self)
	return sgs.Card_Parse("@LisaoCard=.")
end

sgs.ai_skill_use_func["LisaoCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,ep in sgs.list(self.enemies)do
		if use.to:length()<2 then
			use.card = card
			use.to:append(ep)
		end
	end
end

sgs.ai_use_value.LisaoCard = 3.4
sgs.ai_use_priority.LisaoCard = 9.2

sgs.ai_skill_invoke.juanlv = function(self,data)
	return sgs.ai_skill_invoke.double_sword(self,data)
end

sgs.ai_fill_skill.qixin = function(self)
	for i,c in sgs.list(self.toUse)do
		if c:isKindOf("QixinCard") then return end
	end
	for i,c in sgs.list(self.toUse)do
		if i>1 and c:getTypeId()>0 then
			local d = self:aiUseCard(c)
			for _,p in sgs.list(d.to)do
				if p~=self.player and p:getGender()==self.player:getGender() then
					return sgs.Card_Parse("@QixinCard=.")
				end
			end
			break
		end
	end
	if #self.toUse<2 then
		local c = self.player:getTag("qixinCaojieHp"):toInt()
		local l = self.player:getTag("qixinLiuxieHp"):toInt()
		if c>l and self.player:isMale() or l>c and self.player:isFemale() then
			return sgs.Card_Parse("@QixinCard=.")
		end
	end
end

sgs.ai_skill_use_func["QixinCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.QixinCard = 3.4
sgs.ai_use_priority.QixinCard = 9.2



sgs.ai_skill_invoke.zhinang = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_invoke.huyi = function(self,data)
	return self.player:getVisibleSkillList():length()>3
end

sgs.ai_skill_playerchosen.fengzhu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_playerchosen.yuyu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_fill_skill.jiejiu = function(self)
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,c in sgs.list(cards)do
		if c:isKindOf("Analeptic") then
			for _,pn in sgs.list(patterns())do
				local dc = dummyCard(pn)
				dc:setSkillName("jiejiu")
				dc:addSubcard(c)
				if dc:getTypeId()==1 and dc:isAvailable(self.player) then
					if self:aiUseCard(dc).card then
						return dc
					end
				end
			end
		end
	end
end

sgs.ai_skill_invoke.dingxi = function(self,data)
	local to = data:toPlayer()
	return self:isEnemy(to)
end

sgs.ai_skill_askforag.dingxi = function(self,card_ids)
    for _,id in sgs.list(card_ids)do
		local dc = sgs.Sanguosha:getCard(id)
		if dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then
				return id
			end
		end
	end
end

sgs.ai_skill_invoke.huiwan = function(self,data)
	return self:canDraw() and #self.enemies>0
end

sgs.ai_skill_askforag.huiwan = function(self,card_ids)
    local cs = {}
	for _,id in sgs.list(card_ids)do
		table.insert(cs,sgs.Sanguosha:getCard(id))
	end
	self:sortByKeepValue(cs,true)
	for _,dc in sgs.list(cs)do
		if dc:isAvailable(self.player) and self:getCardsNum(dc:getClassName())<1 then
			local d = self:aiUseCard(dc)
			if d.card then
				return dc:getId()
			end
		end
	end
	return -1
end

sgs.ai_skill_playerchosen.huanli = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,true)
    for i,target in sgs.list(destlist)do
		if self:isEnemy(target) and i<#destlist/2
		then return target end
	end
    for _,target in sgs.list(sgs.reverse(destlist))do
		return target
	end
end

sgs.ai_skill_invoke.huanli = function(self,data)
	local to = data:toPlayer()
	return not self:isEnemy(to) and to:getMaxHp()>3
end

sgs.ai_skill_askforyiji.ddzfulu = function(self,card_ids,tos)
    local cs = ListI2C(card_ids)
	cs = self:sortByKeepValue(cs)
	for _,p in sgs.list(tos)do
		if p:isAlive() and self:doDisCard(p,"h",true,2) then
			return p,cs[1]:getId()
		end
	end
	local t,id = sgs.ai_skill_askforyiji.nosyiji(self,card_ids,tos)
	if t and t:isAlive() then
		return t,id
	end
end

sgs.ai_skill_discard.ddzfulu = function(self)
    local handcards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByKeepValue(handcards) -- 按保留值排序
	return {handcards[1]:getId()}
end

sgs.ai_skill_invoke.pimi = function(self,data)
	local use = data:toCardUse()
	if self:isFriend(use.from) then
		return self:doDisCard(use.from,"e") or not use.card:isDamageCard()
	end
	return self:doDisCard(use.from,"he") and not use.card:isDamageCard()
end





