

sgs.ai_skill_invoke.yikuxin = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_playerchosen.yikuxin = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
	local has = false
	local ids = self.player:getTag("yikuxinIds"):toIntList()
	for _,id in sgs.list(ids)do
		if sgs.Sanguosha:getCard(id):getSuit()==2 then has = true end
	end
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p)
		and p:getHandcardNum()>ids:length()
		and getKnownCard(p,self.player,"heart","h")>0
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p) and not has
		and getKnownCard(p,self.player,"heart","h")>0
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and not has
		then return p end
	end
end

sgs.ai_fill_skill.yisigu = function(self)
	return sgs.Card_Parse("#yisiguCard:.:")
end

sgs.ai_skill_use_func["#yisiguCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,ep in sgs.list(self.enemies)do
		if self:damageIsEffective(ep,"N",self.player) then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	self:sort(self.friends_noself,nil,true)
	for i,p in sgs.list(self.friends_noself)do
		if i<#self.friends/2 and not self:isWeak(p)
		and self:damageIsEffective(p,"N",self.player) then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.yisiguCard = 3.4
sgs.ai_use_priority.yisiguCard = 0.2

sgs.ai_fill_skill.yimiehai = function(self)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	local dc = dummyCard("yj_stabs_slash","yimiehai")
	for i,c1 in sgs.list(cards)do
		if table.contains(self.toUse,c1)
		or self:getKeepValue(c1)>6 then continue end
		for n,c2 in sgs.list(cards)do
			if table.contains(self.toUse,c1)
			or self:getKeepValue(c1)>6 then continue end
			if i>n then
				dc:addSubcard(c1)
				dc:addSubcard(c2)
				if dc:isAvailable(self.player)
				then return dc end
				dc:clearSubcards()
			end
		end
	end
end

function sgs.ai_cardsview.yimiehai(self,class_name,player)
    local cards = player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	local dc = dummyCard("yj_stabs_slash","yimiehai")
	for i,c1 in sgs.list(cards)do
		for n,c2 in sgs.list(cards)do
			if i>n then
				dc:addSubcard(c1)
				dc:addSubcard(c2)
				if dc:isAvailable(player)
				then return dc:toString() end
				dc:clearSubcards()
			end
		end
	end
end

sgs.ai_skill_choice.shefu = function(self,choices)
	local items = choices:split("+")
	return items[1]
end

sgs.ai_skill_playerchosen.yiqingjun = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p)
		then return p end
	end
end

sgs.ai_skill_invoke.yijugu = function(self,data)
	return #self.enemies>0
end

sgs.ai_skill_playerchosen.yijugu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:doDisCard(p,"he",true)
		then return p end
	end
end

sgs.ai_skill_invoke.yichengming = function(self,data)
	return #self.enemies>0
end

sgs.ai_fill_skill.yizhengsi = function(self)
	return sgs.Card_Parse("#yizhengsiCard:.:")
end

sgs.ai_skill_use_func["#yizhengsiCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>0 and use.to:isEmpty()
		then use.to:append(ep) end
	end
	if use.to:length()==1 then
		use.to:append(self.player)
	end
	for i,p in sgs.list(self.room:getAlivePlayers())do
		if not self:isFriend(p) and not use.to:contains(p)
		and use.to:length()==2 and p:getHandcardNum()>0 then
			use.to:append(p)
			use.card = card
		end
	end
end

sgs.ai_use_value.yizhengsiCard = 3.4
sgs.ai_use_priority.yizhengsiCard = 4.2

sgs.ai_fill_skill.yihuice = function(self)
	return sgs.Card_Parse("#yihuiceCard:.:")
end

sgs.ai_skill_use_func["#yihuiceCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,p in sgs.list(self.enemies)do
		if self.player:canPindian(p) and use.to:length()<2
		then use.to:append(p) end
	end
	for i,p in sgs.list(self.room:getAlivePlayers())do
		if not self:isFriend(p) and not use.to:contains(p)
		and use.to:length()<2 and self.player:canPindian(p) then
			use.to:append(p)
		end
	end
	if use.to:length()==2 then
		use.card = card
	end
end

sgs.ai_use_value.yihuiceCard = 3.4
sgs.ai_use_priority.yihuiceCard = 4.2

sgs.ai_skill_invoke.yiqianliu = function(self,data)
	return #self.enemies>0
end

sgs.ai_skill_playerchosen.yimitu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_playerschosen.yimitu = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local tps = {}
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) and self:canDraw(p)
		and #tps<x and #tps<self.player:getLostHp()
		and #self.enemies>0 then table.insert(tps,p) end
	end
	return tps
end

sgs.ai_skill_invoke.yimitu = function(self,data)
	local ts = data:toString():split(":")
	return self:isEnemy(BeMan(self.room,ts[2]))
end

sgs.ai_skill_playerchosen.yichengbian = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local dc = dummyCard("duel","_yichengbian")
	local d = self:aiUseCard(dc)
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and d.to:contains(p)
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and self:hasTrickEffective(dc,p,self.player)
		then return p end
	end
end

sgs.ai_skill_discard.yichengbian = function(self)
	if self:getCardsNum("Slash")>0 then return {} end
	local cards = {}
    local handcards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByKeepValue(handcards) -- 按保留值排序
   	for _,h in sgs.list(handcards)do
		table.insert(cards,h:getEffectiveId())
		if #cards>=#handcards/2 then break end
	end
	return cards
end

sgs.ai_skill_invoke.yiduibian = function(self,data)
	return #self.enemies>0
end

sgs.ai_skill_invoke.yiduibian = function(self,data)
	local ts = data:toString():split(":")
	return self:isEnemy(BeMan(self.room,ts[2]))
end

sgs.ai_skill_playerschosen.yizongheng = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
	local tps = {}
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p) and #tps<x
		then table.insert(tps,p) end
	end
	return #tps>1 and tps
end







