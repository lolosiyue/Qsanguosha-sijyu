
addAiSkills("jinxuanbei").getTurnUseCard = function(self)
	return sgs.Card_Parse("@JinXuanbeiCard=.")
end

sgs.ai_skill_use_func["JinXuanbeiCard"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	local can = self:getCardsNum("Jink")>0 or not self:isWeak()
	for _,ep in sgs.list(self.enemies)do
		if self:doDisCard(ep,"e")
		and can
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.friends_noself)do
		if self:doDisCard(ep,"ej")
		and can
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if self:doDisCard(ep,"he")
		and can
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.room:getOtherPlayers(self.player))do
		if self:doDisCard(ep,"he")
		and not self:isFriend(ep)
		and can
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.bf_zhenliangCard = 4.4
sgs.ai_use_priority.bf_zhenliangCard = 3.8

addAiSkills("jinxianwan").getTurnUseCard = function(self)
	local tc = dummyCard()
	tc:setSkillName("jinxianwan")
	if self.player:isChained()
	and tc:isAvailable(self.player) then
		local d = self:aiUseCard(tc)
		sgs.ai_use_priority.jinxianwan = sgs.ai_use_priority[tc:getClassName()]
		self.xw_to = d.to
		if d.card and d.to
		then return sgs.Card_Parse("@JinXianwanCard=.:slash") end
	end
end

sgs.ai_skill_use_func["JinXianwanCard"] = function(card,use,self)
	if self.xw_to then
		use.card = card
		use.to = self.xw_to
	end
end

sgs.ai_use_value.JinXianwanCard = 9.4
sgs.ai_use_priority.JinXianwanCard = 7.8

sgs.ai_guhuo_card.jinxianwan = function(self,toname,class_name)
	return "@JinXianwanCard=.:"..toname
end

sgs.ai_skill_invoke.jinwanyi = function(self,data)
	local target = data:toPlayer()
	if target then
		return self:isFriend(target) and self:doDisCard(target,"ej")
		or self:doDisCard(target,"hej")
	end
end

sgs.ai_skill_playerchosen.jinwanyi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
	return destlist[1]
end

sgs.ai_skill_invoke.jinmaihuo = function(self,data)
	local target = data:toCardUse().from
	if target
	then
		return not self:isFriend(target)
	end
end













