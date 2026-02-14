

sgs.ai_skill_invoke.jinbolan = function(self,data)
    return true
end

local jinbolan_skill={}
jinbolan_skill.name="jinbolan_skill"
table.insert(sgs.ai_skills,jinbolan_skill)
jinbolan_skill.getTurnUseCard = function(self)
	if self:isWeak() or self.player:getHp()<2 then return end
    for _,ep in sgs.list(self.room:getOtherPlayers(self.player))do
		if ep:hasSkill("jinbolan") and ep:getMark("jinbolan-PlayClear")<1
		and (not self:isEnemy(ep) or math.random()<0.3) then
			self.jinbolan_to=ep
			return sgs.Card_Parse("@JinBolanSkillCard=.")
		end
	end
end

sgs.ai_skill_use_func["JinBolanSkillCard"] = function(card,use,self)
	use.card = card
	use.to:append(self.jinbolan_to)
end

sgs.ai_use_value.JinBolanSkillCard = 4.4
sgs.ai_use_priority.JinBolanSkillCard = 5.2

function SmartAI:canCanmou(to,use)
	local cu = {from=use.from,card=use.card,to=sgs.SPlayerList()}
	InsertList(cu.to,use.to)
	cu.to:append(to)
	for _,ac in sgs.list(aiConnect(to))do
      	local tr = sgs.ai_target_revises[ac]
       	if type(tr)=="function"
		and tr(to,cu.card,self,cu)
        then return end
    end
	return true
end

sgs.ai_skill_playerchosen.jincanmou = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local use = self.player:getTag("JincanmouData"):toCardUse()
	if use.to:contains(use.from)
	and not use.card:isDamageCard() then
		for _,target in sgs.list(destlist)do
			if self:isFriend(target)
			and self:canCanmou(target,use)
			then return target end
		end
		for _,target in sgs.list(destlist)do
			if not self:isEnemy(target)
			and self:isWeak(target)
			and self:canCanmou(target,use)
			then return target end
		end
	elseif use.card:isDamageCard() then
		for _,target in sgs.list(destlist)do
			if self:isEnemy(target)
			and self:canCanmou(target,use)
			then return target end
		end
		for _,target in sgs.list(destlist)do
			if not self:isFriend(target)
			and not self:isWeak(target)
			and self:canCanmou(target,use)
			then return target end
		end
	elseif not self:isFriend(use.from) then
		for _,to in sgs.list(use.to)do
			if self:isFriend(to) then
				for _,target in sgs.list(destlist)do
					if self:isEnemy(target)
				and self:canCanmou(target,use)
					then return target end
				end
				for _,target in sgs.list(destlist)do
					if not self:isFriend(target)
					and not self:isWeak(target)
					and self:canCanmou(target,use)
					then return target end
				end
			end
		end
	elseif self:isFriend(use.from) then
		for _,to in sgs.list(use.to)do
			if self:isFriend(to) then
				for _,target in sgs.list(destlist)do
					if self:isFriend(target)
					and self:canCanmou(target,use)
					then return target end
				end
				for _,target in sgs.list(destlist)do
					if not self:isEnemy(target)
					and self:isWeak(target)
					and self:canCanmou(target,use)
					then return target end
				end
			elseif self:isEnemy(to) then
				for _,target in sgs.list(destlist)do
					if self:isEnemy(target)
					and self:canCanmou(target,use)
					then return target end
				end
				for _,target in sgs.list(destlist)do
					if not self:isFriend(target)
					and not self:isWeak(target)
					and self:canCanmou(target,use)
					then return target end
				end
			end
		end
	end
end

sgs.ai_skill_invoke.jincongjian = function(self,data)
	local use = self.player:getTag("JincongjianData"):toCardUse()
	self.jcj_can = nil
	if use.to:contains(use.from)
	and not use.card:isDamageCard()
	then return self:canCanmou(self.player,use)
	elseif use.card:isDamageCard()
	then
		self.jcj_can = not self:isWeak()
		return self.jcj_can
		and self:canCanmou(self.player,use)
		and self:canDamageHp(use.from,use.card)
	end
end

sgs.ai_skill_cardask.jincongjian = function(self,data,pattern,prompt)
    local parsed = prompt:split(":")
    if self.jcj_can
	then
    	self.jcj_can = nil
		return false
	end
end

sgs.ai_skill_invoke.jinxiongshu = function(self,data)
	local target = data:toPlayer()
	if target and not target:isKongcheng()
	then
		return self:isEnemy(target)
		or (not self:isFriend(target)) and #self.enemies<2
	end
end

sgs.ai_skill_discard.jinxiongshu = function(self,x,n)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,"j")
   	local target = self.room:getCurrent()
	if n>#cards/2 or self:isFriend(target)
	then return {} end
	local to_cards = {}
   	for _,h in sgs.list(cards)do
   		if #to_cards>=n then break end
     	table.insert(to_cards,h:getEffectiveId())
	end
	return to_cards
end

sgs.ai_skill_invoke.jinxiongshu_guess = function(self,data)
   	local target = self.room:getCurrent()
	local c = target:getTag("JinXiongshuShowCard_"..self.player:objectName()):toInt()-1
	c = sgs.Sanguosha:getCard(c)
	if c:isAvailable(target)
	then
		for _,to in sgs.list(self.room:getAllPlayers())do
			if CanToCard(c,target,to)
			then
				if (c:isDamageCard() or c:isKindOf("SingleTargetTrick"))
				and self:isEnemy(target,to)
				then return true end
			end
		end
		if c:targetFixed()
		and not c:isDamageCard()
		then return true end
	end
end

addAiSkills("jinbingxin").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards)
	local can = #cards==self.player:getHp()
  	for _,c in sgs.list(cards)do
		if c:getColor()==cards[1]:getColor()
		then continue end
		can = false
	end
	if can
	then
		for c,pn in sgs.list(patterns())do
			c = PatternsCard(pn)
			if c and c:getTypeId()==1
			and self.player:getMark("jinbingxin_guhuo_remove_"..pn.."-Clear")<1
			then
				c = dummyCard(pn)
				c:setSkillName("jinbingxin")
				local d = self:aiUseCard(c)
				if c:isAvailable(self.player)
				and d.card and d.to
				then
					self.jinbingxinUse = d
					sgs.ai_use_priority.JinBingxinCard = sgs.ai_use_priority[c:getClassName()]+5
					return sgs.Card_Parse("@JinBingxinCard=.:"..pn)
				end
			end
		end
	end
end

sgs.ai_skill_use_func["JinBingxinCard"] = function(card,use,self)
	if self.jinbingxinUse
	then
		use.card = card
		use.to = self.jinbingxinUse.to
	end
end

sgs.ai_use_value.JinBingxinCard = 3.4
sgs.ai_use_priority.JinBingxinCard = 4.8

sgs.ai_guhuo_card.jinbingxin = function(self,toname,class_name)
	return "@JinBingxinCard=.:"..toname
end

sgs.ai_skill_cardask["@@jinbingxin"] = function(self,data,pattern)
	if(self.jinbingxinUse)
	then
		local tos = {}
		for _,to in sgs.list(self.jinbingxinUse.to)do
			table.insert(tos,to:objectName())
		end
		return self.jinbingxinUse.card:toString().."->"..table.concat(tos,"+")
	end
    return "."
end


















