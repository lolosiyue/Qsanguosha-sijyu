--推弑
sgs.ai_skill_playerchosen.jintuishi = function(self,targets)
	local from = self.player:getTag("jintuishi_from"):toPlayer()
	if self:isFriend(from) then return nil end
	targets = self:sort(targets)
	local slash = dummyCard()
	
	for _,p in sgs.list(targets)do
		if self:isEnemy(from,p) and self:slashIsEffective(slash,p,from) and self:needLeiji(p,from) then
			return p
		end
	end
	
	for _,p in sgs.list(targets)do
		if self:isFriend(from,p) and self:slashIsEffective(slash,p,from) then
			return p
		end
	end
	
	for _,p in sgs.list(targets)do
		if self:isFriend(from,p) then
			return p
		end
	end
	
	for _,p in sgs.list(targets)do
		if not self:isFriend(p) then
			return p
		end
	end
	
	if getCardsNum("Slash",from,self.player)<1 then
		targets = sgs.reverse(targets)
		return targets[1]
	end
end

sgs.ai_skill_cardask["@jintuishi_slash"] = function(self,data,pattern,target)
	return sgs.ai_skill_cardask["@mobileniluan"](self,data,pattern,target)
end

sgs.ai_can_damagehp.jintuishi = function(self,from,card,to)
	local gc = self.room:getCurrent()
	for _,p in sgs.list(gc:getAliveSiblings())do
		if gc:inMyAttackRange(p)
		and to:inYinniState()
		then
			if self:isEnemy(p) and not self:isFriend(gc)
			or not self:isFriend(p) and self:isEnemy(gc)
			then return true end
		end
	end
end

--筹伐
local jinchoufa_skill = {}
jinchoufa_skill.name = "jinchoufa"
table.insert(sgs.ai_skills,jinchoufa_skill)
jinchoufa_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@JinChoufaCard=.")
end

sgs.ai_skill_use_func.JinChoufaCard = function(card,use,self)
	self:sort(self.friends_noself,"handcard")
	self.friends_noself = sgs.reverse(self.friends_noself)
	for _,p in sgs.list(self.friends_noself)do
		if p:isKongcheng() then continue end
		if self:hasCrossbowEffect(p) then
			use.card = card
			use.to:append(p)
			return
		end
	end
	self:sort(self.enemies,"handcard")
	self.enemies = sgs.reverse(self.enemies)
	for _,p in sgs.list(self.enemies)do
		if p:isKongcheng() or self:hasCrossbowEffect(p) then continue end
		use.card = card
		use.to:append(p)
		return
	end
end

sgs.ai_use_value.JinChoufaCard = 10

--昭然
sgs.ai_skill_invoke.jinzhaoran = true

sgs.ai_skill_playerchosen.jinzhaoran = function(self,targets)
	return self:findPlayerToDiscard("he",false,true,targets)[1]
end

--识人
sgs.ai_skill_invoke.jinshiren = function(self,data)
	local current = data:toPlayer()
	if self:isFriend(current) then
		return self:doDisCard(current,"h",true)
		or self:needToThrowLastHandcard(current)
		or self:getOverflow(current)>2
	elseif self:isEnemy(current) then
		return self:doDisCard(current,"h",true)
		or not self:needToThrowLastHandcard(current)
	end
	return true
end

sgs.ai_can_damagehp.jinshiren = function(self,from,card,to)
	return (self:isEnemy(self.room:getCurrent()) or #self.friends_noself<1)
	and to:inYinniState()
end

--宴戏
local jinyanxi_skill = {}
jinyanxi_skill.name = "jinyanxi"
table.insert(sgs.ai_skills,jinyanxi_skill)
jinyanxi_skill.getTurnUseCard = function(self)
	if #self.enemies<=0 then return end
	return sgs.Card_Parse("@JinYanxiCard=.")
end

sgs.ai_skill_use_func.JinYanxiCard = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,p in sgs.list(self.enemies)do
		if self:doDisCard(p,"h",true) then
			use.card = card
			use.to:append(p)
			break
		end
	end
end

sgs.ai_use_value.JinYanxiCard = 10
sgs.ai_card_intention.JinYanxiCard = 50

--三陈
local jinsanchen_skill = {}
jinsanchen_skill.name = "jinsanchen"
table.insert(sgs.ai_skills,jinsanchen_skill)
jinsanchen_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@JinSanchenCard=.")
end

sgs.ai_skill_use_func.JinSanchenCard = function(card,use,self)
	self:sort(self.friends,"handcard")
	self.friends = sgs.reverse(self.friends)
	for _,p in sgs.list(self.friends)do
		if not self:doDisCard(p,"h") and p:getMark("jinsanchen_target-Clear")==0 then
			use.card = card
			use.to:append(p)
			return
		end
	end
	for _,p in sgs.list(self.friends)do
		if p:getMark("jinsanchen_target-Clear")>0 then continue end
		use.card = card
		use.to:append(p)
		return
	end
end

sgs.ai_use_value.JinSanchenCard = 10
sgs.ai_card_intention.JinSanchenCard = -50

sgs.ai_skill_discard.jinsanchen = function(self,discard_num,min_num,optional,include_equip)
	return self:askForDiscard("dummyreason",discard_num,min_num,false,include_equip)
end

--破竹
local jinpozhu_skill = {}
jinpozhu_skill.name = "jinpozhu"
table.insert(sgs.ai_skills,jinpozhu_skill)
jinpozhu_skill.getTurnUseCard = function(self)
	local cards = self:addHandPile()
	self:sortByUseValue(cards,true,"l")
	if #cards<1 then return end
	if self:getUseValue(cards[1])>sgs.ai_use_value.Chuqibuyi then return end
	local suit = cards[1]:getSuitString()
	local number = cards[1]:getNumberString()
	local card_id = cards[1]:getEffectiveId()
	local card_str = ("chuqibuyi:jinpozhu[%s:%s]=%d"):format(suit,number,card_id)
	local chuqibuyi = sgs.Card_Parse(card_str)
	assert(chuqibuyi)
	return chuqibuyi
end

sgs.ai_skill_cardask["@jinshenpin-card"] = function(self,data)
	local judge = data:toJudge()
	local all_cards = self.player:getCards("he")
	for _,id in sgs.list(self.player:getPile("wooden_ox"))do
		all_cards:prepend(sgs.Sanguosha:getCard(id))
	end
	if all_cards:isEmpty() then return "." end
	local cards = {}
	for _,c in sgs.list(all_cards)do
		if c:getColor()~=judge.card:getColor()
		then table.insert(cards,c) end
	end
	if #cards<1 then return "." end
    self:sortByUseValue(cards) -- 按保留值排序
	if self:needRetrial(judge)
	then
    	local id = self:getRetrialCardId(cards,judge)
    	if id~=-1 then return id end
	end
    return "."
end

sgs.ai_skill_choice.jinzhongyun = function(self,choices)
	local items = choices:split("+")
	if table.contains(items,"discard")
	then
		for _,ep in sgs.list(self.enemies)do
			if self:isWeak(ep) or ep:hasEquip()
			then return "discard" end
		end
	end
	if table.contains(items,"draw")
	then return "draw" end
end

sgs.ai_skill_playerchosen.jinzhongyun = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and (self:isWeak(target) or target:hasEquip())
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
	return destlist[1]
end


sgs.ai_skill_cardask["@jinshenpin-card"] = function(self,data)
	local judge = data:toJudge()
	local all_cards = self:addHandPile("he")
	if #all_cards<1 then return "." end
	local cards = {}
	for _,c in sgs.list(all_cards)do
		if c:getColor()~=judge.card:getColor()
		then table.insert(cards,c) end
	end
	if #cards<1 then return "." end
    self:sortByUseValue(cards) -- 按保留值排序
	if self:needRetrial(judge)
	then
    	local id = self:getRetrialCardId(cards,judge)
    	if id~=-1 then return id end
	end
    return "."
end

sgs.ai_skill_choice.jinzhongyun = function(self,choices)
	local items = choices:split("+")
	if table.contains(items,"discard")
	then
		for _,ep in sgs.list(self.enemies)do
			if self:isWeak(ep) or ep:hasEquip()
			then return "discard" end
		end
	end
	if table.contains(items,"draw")
	then return "draw" end
end

sgs.ai_skill_playerchosen.jinzhongyun = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and (self:isWeak(target) or target:hasEquip())
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
	return destlist[1]
end

sgs.ai_skill_playerchosen.jingaoling = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:isWounded()
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		and target:isWounded()
		then return target end
	end
--	return destlist[1]
end

sgs.ai_can_damagehp.jingaoling = function(self,from,card,to)
    for _,p in sgs.list(self.friends)do
		if p:isWounded() and not self:isFriend(self.room:getCurrent())
		or self:isWeak(p) then return to:inYinniState() end
	end
end

sgs.ai_skill_playerchosen.jinqimei = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:isWounded()
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		and target:isWounded()
		then return target end
	end
--	return destlist[1]
end

sgs.ai_skill_invoke.jinzhuiji = function(self,data)
    return self:isWeak() or self.player:getHandcardNum()<5 and not self:isWeak()
end

sgs.ai_skill_choice.jinzhuiji = function(self,choices)
	local items = choices:split("+")
	if table.contains(items,"draw")
	and self.player:getHandcardNum()<4 and not self:isWeak()
	then return "draw" end
	if table.contains(items,"recover")
	and self:isWeak()
	then return "recover" end
	return "recover"
end












