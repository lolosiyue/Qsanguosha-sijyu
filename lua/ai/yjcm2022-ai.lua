
sgs.ai_skill_invoke.liandui = function(self,data)
	local items = data:toString():split(":")
	if #items>1
	then
        local target = BeMan(self.room,items[2])
    	return target and not self:isEnemy(target)
	end
end

sgs.ai_skill_invoke.lianduiother = function(self,data)
	local items = data:toString():split(":")
	if #items>1
	then
        local target = BeMan(self.room,items[2])
    	return target and self:isFriend(target)
	end
end

addAiSkills("biejun-give").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards)
  	for _,p in sgs.list(self.room:findPlayersBySkillName("biejun"))do
		if #cards<2 or p==self.player then continue end
		self.biejun_to = p
		if self:isFriend(p) then
			if #cards>self.player:getMaxCards() and #self.toUse<1
			then return sgs.Card_Parse("@BiejunCard="..cards[1]:getEffectiveId()) end
		else
			local pcs = self:poisonCards(cards)
			if #pcs>0 then
				return sgs.Card_Parse("@BiejunCard="..pcs[1]:getEffectiveId())
			end
			if self:isEnemy(p) and self:isWeak(p) and p:getHandcardNum()<3
			and #cards>self.player:getMaxCards() and #self.toUse>1
			then return sgs.Card_Parse("@BiejunCard="..cards[1]:getEffectiveId()) end
		end
	end
end

sgs.ai_skill_use_func["BiejunCard"] = function(card,use,self)
	if self.biejun_to then
		use.card = card
		use.to:append(self.biejun_to)
	end
end

sgs.ai_use_value.BiejunCard = 0.4
sgs.ai_use_priority.BiejunCard = 5.8

sgs.ai_skill_invoke.biejun = function(self,data)
	return not self.player:faceUp() or self:isWeak()
end

sgs.ai_can_damagehp.biejun = function(self,from,card,to)
    for _,id in sgs.list(to:handCards())do
		if to:getMark("biejunGetCard_"..id.."-Clear")>0
		then return end
	end
	return to:getHp()+self:getAllPeachNum()-self:ajustDamage(from,to,1,card)>1
	and self:canLoseHp(from,card,to)
	and self:isFriend(to)
	and not to:faceUp()
end

sgs.ai_skill_playerchosen.sangu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		self.sangu_to = target
		if self:isFriend(target)
		and target:getHandcardNum()>1
		then return target end
	end
    for _,target in sgs.list(destlist)do
		self.sangu_to = target
		if self:isEnemy(target)
		and target:getHandcardNum()>1
		then return target end
	end
    for _,target in sgs.list(destlist)do
		self.sangu_to = target
		if not self:isEnemy(target)
		and target:getHandcardNum()>1
		then return target end
	end
end

sgs.ai_skill_askforag.sangu = function(self,card_ids)
    local cards = {}
	for c,id in sgs.list(card_ids)do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
    self:sortByKeepValue(cards,self:isFriend(self.sangu_to))
	for i,c in sgs.list(cards)do
		if self:isEnemy(self.sangu_to) and c:isAvailable(self.sangu_to)
		or self:isFriend(self.sangu_to) and not c:isAvailable(self.sangu_to)
		then continue end
		if self.player:getMark("SanguRecord_"..c:objectName().."-PlayClear")>0
		and i<#cards/3 then return c:getEffectiveId() end
	end
	for i,c in sgs.list(cards)do
		if self.player:getMark("SanguRecord_"..c:objectName().."-PlayClear")>0
		and i<#cards/3 then return c:getEffectiveId() end
	end
	for i,c in sgs.list(cards)do
		if self:isEnemy(self.sangu_to) and c:isAvailable(self.sangu_to)
		or self:isFriend(self.sangu_to) and not c:isAvailable(self.sangu_to)
		then continue end
		return c:getEffectiveId()
	end
	return cards[1]:getEffectiveId()
end

sgs.ai_skill_invoke.bushilk = function(self,data)
	return self.player:getHandcardNum()>0
end

sgs.ai_skill_discard.koujing = function(self,max,min,optional)
	local to_cards = {}
	local cards = self.player:getCards("h")
	cards = self:sortByUseValue(cards)
	local touse = self:getTurnUse()
   	for c,h in sgs.list(cards)do
   		c = dummyCard()
		c:addSubcard(h)
		c:setSkillName("koujing")
		if table.contains(touse,h)
		and self:getUseValue(c)<self:getUseValue(h)
		then continue end
		local d = self:aiUseCard(c)
		if d.card and d.to:length()>0
		and #to_cards<=d.to:at(0):getHp()
		then table.insert(to_cards,h:getEffectiveId()) end
	end
	return to_cards
end

sgs.ai_skill_invoke.koujing = function(self,data)
	local ids = self.player:getTag("KoujingShowCards"):toIntList()
	if ids:length()>0
	then
		return self.player:getHp()+self:getCardsNum("Peach")+self:getCardsNum("Analeptic")<=ids:length()
		or self.player:getHandcardNum()>=ids:length()
	end
end


sgs.ai_skill_invoke.duanwan = function(self,data)
	return self.player:getHp()+self:getAllPeachNum()<1
end

sgs.ai_skill_cardask["diezhang0"] = function(self,data,pattern,to1,to2)
    if to2 and self:isEnemy(to2) and self.player:canSlash(to2,false)
	then return true end
	return "."
end

sgs.ai_skill_invoke.diezhang = function(self,data)
	local to = data:toPlayer()
    if to and not self:isFriend(to) and self.player:canSlash(to,false)
	then return true end
end

sgs.ai_skill_invoke.cibei = function(self,data)
	return true
end

sgs.ai_skill_playerchosen.cibei = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) and self:doDisCard(p,"hej")
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if self:doDisCard(p,"hej")
		then return p end
	end
end

sgs.ai_fill_skill.shujian = function(self)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,c1 in sgs.list(cards)do
		if table.contains(self.toUse,c1) then continue end
		return sgs.Card_Parse("@ShujianCard="..c1:getId())
	end
end

sgs.ai_skill_use_func["ShujianCard"] = function(card,use,self)
	self:sort(self.friends_noself)
	for _,p in sgs.list(self.friends_noself)do
		if self:canDraw(p) then
			use.card = card
			use.to:append(p)
			break
		end
	end
end

sgs.ai_use_value.ShujianCard = 3.4
sgs.ai_use_priority.ShujianCard = 6.2

sgs.ai_skill_use["@@shujian"] = function(self,prompt)
    local c = dummyCard("dismantlement")
	c:setSkillName("_shujian")
    local d = self:aiUseCard(c)
   	if d.card then
      	local tos = {}
       	for _,p in sgs.list(d.to)do
       		table.insert(tos,p:objectName())
       	end
       	return d.card:toString().."->"..table.concat(tos,"+")
    end
end

sgs.ai_skill_choice["shujian"] = function(self,choices,data)
	local items = choices:split("+")
	local p = data:toPlayer()
	if self:isFriend(p) and p:getMark("shujianNum-PlayClear")<2 then
		return items[1]
	end
	if self:aiUseCard(dummyCard("dismantlement")).card then
		return items[2]
	end
	return items[1]
end

sgs.ai_skill_playerschosen.zhitu = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	local use = self.room:getTag("zhituData"):toCardUse()
	local d = self:aiUseCard(use.card,dummy(nil,99,use.to))
	local tos = {}
	if d.card then
		for _,p in sgs.list(d.to)do
			if table.contains(destlist,p)
			then table.insert(tos,p) end
		end
	end
	return tos
end

sgs.ai_fill_skill.fujue = function(self)
	if sgs.ai_skill_invoke.peiqi(self) then
		return sgs.Card_Parse("@FujueCard=.")
	end
end

sgs.ai_skill_use_func["FujueCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.FujueCard = 3.4
sgs.ai_use_priority.FujueCard = 6.2

sgs.ai_skill_playerchosen.fujue_from = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
    for _,p in sgs.list(destlist)do
		if self.peiqiData.from==p
		then return p end
	end
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:doDisCard(p,"ej")
		then return p end
	end
end

sgs.ai_skill_playerchosen.fujue_to = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
    for _,p in sgs.list(destlist)do
		if self.peiqiData.to==p
		then return p end
	end
end

sgs.ai_skill_cardchosen.fujue = function(self,who,flags)
	if self.peiqiData.cid then return self.peiqiData.cid end
end

sgs.ai_fill_skill.beiyu = function(self)
	if self.player:getMaxHp()>self.player:getHandcardNum() then
		return sgs.Card_Parse("@BeiyuCard=.")
	end
end

sgs.ai_skill_use_func["BeiyuCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.BeiyuCard = 3.4
sgs.ai_use_priority.BeiyuCard = 6.2

sgs.ai_skill_invoke.duchi = function(self,data)
	return self:canDraw()
end

sgs.ai_fill_skill.tyqimei = function(self)
    return sgs.Card_Parse("@ThQimeiCard=.")
end

sgs.ai_skill_use_func["ThQimeiCard"] = function(card,use,self)
	self:sort(self.friends_noself)
	for _,p in sgs.list(self.friends_noself)do
		if self:canDraw(p) then
			use.card = card
			use.to:append(p)
			break
		end
	end
end

sgs.ai_use_value.ThQimeiCard = 3.4
sgs.ai_use_priority.ThQimeiCard = 6.2

sgs.ai_skill_use["@@thqimei"] = function(self,prompt)
	local cs = {}
    for _,id in sgs.list(self.player:getTag("thqimeiForAI"):toIntList())do
      	table.insert(cs,sgs.Sanguosha:getCard(id))
	end
    self:sortByUseValue(cs)
	for _,c in sgs.list(cs)do
      	local d = self:aiUseCard(c)
		if d.card then
			local tps = {}
			for _,p in sgs.list(d.to)do
				table.insert(tps,p:objectName())
			end
			return c:toString().."->"..table.concat(tps,"+")
		end
	end
end

sgs.ai_skill_playerchosen.thzhuiji = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p)
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if not self:isEnemy(p)
		then return p end
	end
end

sgs.ai_fill_skill.gongqiao = function(self)
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,c1 in sgs.list(cards)do
		if table.contains(self.toUse,c1) or #cards<3 then continue end
		return sgs.Card_Parse("@GongqiaoCard="..c1:getId())
	end
end

sgs.ai_skill_use_func["GongqiaoCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.GongqiaoCard = 3.4
sgs.ai_use_priority.GongqiaoCard = 6.2

sgs.ai_skill_choice.gongqiao = function(self,choices)
	local items = choices:split("+")
    for _,t in sgs.list(items)do
		local n = tonumber(t:split("ea")[2])
		if self.player:getEquip(n)
		then continue end
		return t
	end
end


function SmartAI:useCardWangmeizhike(card,use)
	self:sort(self.friends_noself)
	for _,p in sgs.list(self.friends_noself)do
		if self:isWeak(p) and getKnownCard(p,self.player,"club",false,"h")>0
	   	and CanToCard(card,self.player,p,use.to)
		then use.card = card end
	end
	self:sort(self.enemies,nil,true)
	for _,p in sgs.list(self.enemies)do
		if not self:isWeak(p) and getKnownCard(p,self.player,"club",false,"h")>1
	   	and CanToCard(card,self.player,p,use.to)
		then use.card = card end
	end
end
sgs.ai_use_priority.Wangmeizhike = 1.4
sgs.ai_keep_value.Wangmeizhike = 4
sgs.ai_use_value.Wangmeizhike = 3.7
sgs.ai_nullification.Wangmeizhike = function(self,trick,from,to,positive)
	if self:getCardsNum("Nullification")>1 then
        return self:isEnemy(to)
    	and to:isWounded()
    	and positive
	else
        return self:isEnemy(to)
    	and self:isWeak(to)
     	and to:isWounded()
    	and positive
	end
end

