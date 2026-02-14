--不臣
sgs.ai_skill_invoke.jinbuchen = function(self,data)
	local player = data:toPlayer()
	if self:needKongcheng(self.player,true) then return false end
	if self:doDisCard(player,"he") then return true end
end

sgs.ai_can_damagehp.jinbuchen = function(self,from,card,to)
	return self:doDisCard(self.room:getCurrent(),"he",true)
	and to:inYinniState()
end

--雄志
local jinxiongzhi_skill = {}
jinxiongzhi_skill.name = "jinxiongzhi"
table.insert(sgs.ai_skills,jinxiongzhi_skill)
jinxiongzhi_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@JinXiongzhiCard=.")
end

sgs.ai_skill_use_func.JinXiongzhiCard = function(card,use,self)
	local list = self.room:getNCards(self.player:getMaxHp(),false)
	self.room:returnToTopDrawPile(list)
	local use_num = 0
	for _,id in sgs.qlist(list)do
		local card = sgs.Sanguosha:getCard(id)
		if not self.player:canUse(card) then break end
		if self:willUse(self.player,card)
		then use_num = use_num+1
		else break end
	end
	if use_num>=2 then
		use.card = card
	end
end

sgs.ai_use_priority.JinXiongzhiCard = 0

sgs.ai_skill_use["@@jinxiongzhi!"] = function(self,prompt,method)
	local id = self.player:getMark("jinxiongzhi_id-PlayClear")
    local card = sgs.Sanguosha:getCard(id)
	local dummy = self:aiUseCard(card)
	if dummy.card and dummy.to then
		local tos = {}
		for _,p in sgs.qlist(dummy.to)do
			table.insert(tos,p:objectName())
		end
		return id.."->"..table.concat(tos,"+")
	end
	return "."
end

--权变
sgs.ai_skill_invoke.jinquanbian = function(self,data)
	return self:canDraw()
end

--二版权变
sgs.ai_skill_invoke.secondjinquanbian = function(self,data)
	return self:canDraw()
end

--慧识
sgs.ai_skill_invoke.jinhuishi = function(self,data)
	local num = data:toString():split(":")[2]
	num = tonumber(num)
	num = math.floor(num/2)
	return num>=2
end

--清冷
sgs.ai_skill_use["@@jinqingleng"] = function(self,prompt,method)
	local cards = self:addHandPile("he")
	self:sortByUseValue(cards,true)
	
	local name = self.player:property("jinqingleng_now_target"):toString()
	local to = self.room:findPlayerByObjectName(name)
	if not to or to:isDead() then return "." end
	
	local slashs = {}
	for _,c in ipairs(cards)do
		local slash = dummyCard()
        slash:addSubcard(c)
        slash:setSkillName("jinqingleng")
		if self.player:canSlash(to,slash,false) then
			self.player:setFlags("slashNoDistanceLimit")
			local dummy_use = dummy()
			for _,p in sgs.qlist(self.room:getAlivePlayers())do
				if p:objectName()~=name then
					table.insert(dummy_use.current_targets,p)
				end
			end
			self:useCardSlash(slash,dummy_use)
			self.player:setFlags("-slashNoDistanceLimit")
			if dummy_use.card and dummy_use.to and dummy_use.to:length()>0 then
				table.insert(slashs,c)
			end
		end
	end
	if #slashs==0 then return "." end
	
	for _,c in ipairs(slashs)do
		if c:isKindOf("Peach") then continue end
		if c:isKindOf("Jink") and self:getCardsNum("Jink")<2 then continue end
		if c:isKindOf("Analeptic") and self:isWeak() then continue end
		if c:isKindOf("ExNihilo") then continue end
		return "@JinQinglengCard="..c:getEffectiveId()
	end
	return "."
end

sgs.ai_can_damagehp.jinxuanmu = function(self,from,card,to)
	return self:isEnemy(self.room:getCurrent())
	and to:inYinniState()
end

--巧言
sgs.ai_skill_discard.qiaoyan = function(self,discard_num,min_num,optional,include_equip)
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	return {cards[1]:getEffectiveId()}
end

--献珠
sgs.ai_skill_playerchosen.xianzhu = function(self,targets)
	local cards = {}
	for _,id in sgs.qlist(self.player:getPile("qyzhu"))do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
	local card,friend = self:getCardNeedPlayer(cards,true)
	if card and friend then return friend end
	
	self:sort(self.friends_noself)
	
	for _,p in ipairs(self.friends_noself)do
		if self:canDraw(p) and not self:willSkipPlayPhase(p)
		then return p end
	end
	for _,p in ipairs(self.friends_noself)do
		if not (p:isKongcheng() and self:needKongcheng(p,true))
		and not self:willSkipPlayPhase(p)
		then return p end
	end
	for _,p in ipairs(self.friends_noself)do
		if self:canDraw(p)
		then return p end
	end
	for _,p in ipairs(self.friends_noself)do
		if not (p:isKongcheng() and self:needKongcheng(p,true))
		then return p end
	end
	
	self:sort(self.enemies)
	for _,p in ipairs(self.enemies)do
		if not hasManjuanEffect(p) then continue end
		local slash = dummyCard()
		slash:setSkillName("_xianzhu")
		if p:isLocked(slash) then continue end
		for _,enemy in ipairs(self.enemies)do
			if self.player:inMyAttackRange(enemy)
			and p:canSlash(enemy,slash,false)
			then return p end
		end
	end
	
	if #cards==1
	then
		if not cards[1]:isKindOf("Peach")
		and not cards[1]:isKindOf("Jink")
		and not cards[1]:isKindOf("Analeptic")
		and not cards[1]:isKindOf("ExNihilo")
		then
			for _,p in ipairs(self.enemies)do
				if self:canDraw(p) then continue end
				local slash = dummyCard()
				slash:setSkillName("_xianzhu")
				if p:isLocked(slash) then continue end
				for _,enemy in ipairs(self.enemies)do
					if self.player:inMyAttackRange(enemy)
					and p:canSlash(enemy,slash,false)
					then return p end
				end
			end
		end
	end
	
	return self.player
end

sgs.ai_skill_playerchosen.xianzhu_target = function(self,targets)
	local from = self.player:getTag("xianzhu_slash_from"):toPlayer()
	local enemies,zhongli,friends = {},{},{}
	local slash = dummyCard()
	slash:setSkillName("_xianzhu")	
	for _,p in sgs.qlist(targets)do
		if self:slashIsEffective(slash,p,from)
		and not self:slashProhibit(slash,p,from)
		then
			if self:isEnemy(p)
			then table.insert(enemies,p)
			elseif self:isFriend(p)
			then table.insert(friends,p)
			else table.insert(zhongli,p) end
		end
	end
	if #enemies>0 then
		self:sort(enemies)
		for _,p in ipairs(enemies)do
			if (self:isEnemy(from) and not self:isGoodTarget(p,enemies,slash))
			or (self:isFriend(from) and self:isGoodTarget(p,enemies,slash))
			then return p end
		end
		for _,p in ipairs(enemies)do
			if (self:isEnemy(from) and not self:isGoodTarget(p,enemies,slash))
			or (self:isFriend(from) and self:isGoodTarget(p,enemies,slash))
			then return p end
		end
		for _,p in ipairs(enemies)do
			if self:slashIsEffective(slash,p,from)
			then return p end
		end
		return enemies[1]
	end
	
	if #zhongli>0
	then
		self:sort(zhongli)
		zhongli = sgs.reverse(zhongli)
		for _,p in ipairs(zhongli)do
			if (self:isEnemy(from) and not self:isGoodTarget(p,zhongli,slash))
			or (self:isFriend(from) and self:isGoodTarget(p,zhongli,slash))
			then return p end
		end
		for _,p in ipairs(zhongli)do
			if (self:isEnemy(from) and not self:isGoodTarget(p,zhongli,slash))
			or (self:isFriend(from) and self:isGoodTarget(p,zhongli,slash))
			then return p end
		end
		return zhongli[1]
	end
	
	if #friends>0
	then
		self:sort(friends)
		friends = sgs.reverse(friends)
		for _,p in ipairs(friends)do
			if (self:isEnemy(from) and not self:isGoodTarget(p,friends,slash))
			or (self:isFriend(from) and self:isGoodTarget(p,friends,slash))
			then return p end
		end
		for _,p in ipairs(friends)do
			if (self:isEnemy(from) and not self:isGoodTarget(p,friends,slash))
			or (self:isFriend(from) and self:isGoodTarget(p,friends,slash))
			then return p end
		end
		return friends[1]
	end
end

--才望
sgs.ai_skill_invoke.jincaiwang = function(self,data)
	local name = data:toString():split(":")[2]
	local to = self.room:findPlayerByObjectName(name)
	if not to then return false end
	return self:isEnemy(to) and self:doDisCard(to,"he")
end

addAiSkills("secondjincaiwang").getTurnUseCard = function(self)
	local cards = self.player:getCards("j")
	cards = self:sortByKeepValue(cards,nil,"l")
  	for d,c in sgs.list(cards)do
		if #cards<2
		then
			local slash = dummyCard("Slash")
			slash:setSkillName("secondjincaiwang")
			slash:addSubcard(c)
			if slash:isAvailable(self.player)
			then return slash end
		end
	end
end

sgs.ai_guhuo_card.secondjincaiwang = function(self,toname,class_name)
    local cards = self.player:getCards("h")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
	if #cards==1
	and class_name=="Jink"
	then
		local slash = dummyCard("Jink")
		slash:setSkillName("secondjincaiwang")
		slash:addSubcard(cards[1])
		return slash:toString()
	end
	cards = self.player:getCards("e")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
	if #cards==1
	and class_name=="Nullification"
	then
		local slash = dummyCard("Nullification")
		slash:setSkillName("secondjincaiwang")
		slash:addSubcard(cards[1])
		return slash:toString()
	end
	cards = self.player:getCards("j")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
	if #cards==1
	and class_name=="Slash"
	then
		local slash = dummyCard()
		slash:setSkillName("secondjincaiwang")
		slash:addSubcard(cards[1])
		return slash:toString()
	end
end

--车悬
sgs.ai_skill_invoke.chexuan = function(self,data)
	return true
end

local chexuan={}
chexuan.name="chexuan"
table.insert(sgs.ai_skills,chexuan)
chexuan.getTurnUseCard = function(self)
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if h:isBlack()
		then
            return sgs.Card_Parse("@ChexuanCard="..h:getEffectiveId())
		end
	end
end

sgs.ai_skill_use_func["ChexuanCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.ChexuanCard = 4.4
sgs.ai_use_priority.ChexuanCard = 0.4


--草诏
local caozhao={}
caozhao.name="caozhao"
table.insert(sgs.ai_skills,caozhao)
caozhao.getTurnUseCard = function(self)
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	if #cards<1 then return end
    local names = self.player:property("CaozhaoNames"):toStringList()
	for _,name in sgs.list(patterns())do
        if table.contains(names,name) then continue end
		local card = dummyCard(name)
		if not card or card:isKindOf("DelayedTrick") then continue end
		card:addSubcard(cards[1])
		card:setSkillName("caozhao")
		if self:getUseValue(card)>5
		and math.random()>0.6
		then
			return sgs.Card_Parse("@CaozhaoCard="..cards[1]:getEffectiveId()..":"..name)
		end
	end
end

sgs.ai_skill_use_func["CaozhaoCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.CaozhaoCard = 4.4
sgs.ai_use_priority.CaozhaoCard = 0.4

sgs.ai_skill_playerchosen.caozhao = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
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

sgs.ai_skill_playerchosen.caozhao_give = function(self,players)
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

sgs.ai_skill_choice.caozhao = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	local cn = items[1]:split("=")[2]
	local c = dummyCard(cn)
	if self:isFriend(target) or self:isWeak()
	then return items[1] end
	if self:isEnemy(target) and not self:isWeak()
	and self:getUseValue(c)>3
	then return items[2] end
end

--息兵
sgs.ai_skill_invoke.olxibing = function(self,data)
	local dama = data:toDamage()
	if dama.from
	then
		if self:isFriend(dama.from)
		then
			if self.player:getHandcardNum()-dama.from:getHandcardNum()>2
			or dama.from:getHandcardNum()-self.player:getHandcardNum()>2
			then return true end
		else
			return true
		end
	end
end

sgs.ai_skill_choice.olxibing = function(self,choices,data)
	local items = choices:split("+")
	local dama = data:toDamage()
	if table.contains(items,"discard_self")
	then
		if self.player:getHandcardNum()-dama.from:getHandcardNum()<2
		then return "discard_self" end
	end
    for _,item in sgs.list(items)do
		if item~="discard_self"
		then return item end
	end
end
