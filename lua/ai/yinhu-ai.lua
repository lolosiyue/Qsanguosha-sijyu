
addAiSkills("yhshecuo").getTurnUseCard = function(self)
	return sgs.Card_Parse("@YHShecuoCard=.")
end

sgs.ai_skill_use_func["YHShecuoCard"] = function(card,use,self)
	for _,ep in sgs.list(self.enemies)do
		if ep:getHp()>=self.player:getHp()
		then
			use.card = card
			use.to:append(ep) 
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>=self.player:getHandcardNum()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>0
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.YHShecuoCard = 2.4
sgs.ai_use_priority.YHShecuoCard = 1.8

sgs.ai_skill_playerchosen.yhyingfu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	self.yf_to = nil
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:isWeak(target)
		then
			self.yf_to = target
			return target
		end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and not self:isWeak(target)
		then
			self.yf_to = target
			return target
		end
	end
	self.yf_to = destlist[1]
    return destlist[1]
end

sgs.ai_skill_cardask.yhyingfu = function(self,data,pattern,prompt)
    local parsed = prompt:split(":")
    if self.yf_to
	and not self:isFriend(self.yf_to)
	then
    	if parsed[1]=="slash-jink"
		then
	    	parsed = data:toCardEffect()
			if self:canLoseHp(parsed.from,parsed.slash)
			then return false end
		else
	    	parsed = data:toCardEffect()
			local card = parsed.card
			if card and card:isDamageCard()
			and self:canLoseHp(parsed.from,parsed.card)
			then return false end
		end
	end
end

sgs.ai_nullification.yhyingfu = function(self,trick,from,to,positive)
    if to:hasSkill("yhyingfu")
	and self:isFriend(to)
	and self.yf_to
	and not self:isFriend(self.yf_to)
	and trick:isDamageCard()
   	and self:canLoseHp(from,trick,to)
	then return false end
end

sgs.ai_skill_playerchosen.yhnabi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card",true)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and not self:isWeak(target)
		then return target end
	end
	return self:isWeak() and destlist[1]
end

addAiSkills("yhyijie").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	for _,c in sgs.list(cards)do
		if c:getSuit()==2
		then return sgs.Card_Parse("@YHYijieCard="..c:getEffectiveId()) end
	end
end

sgs.ai_skill_use_func["YHYijieCard"] = function(card,use,self)
	for _,ep in sgs.list(self.friends)do
		if self:isWeak(ep)
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.friends)do
		if ep:isWounded()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.friends)do
		use.card = card
		use.to:append(ep)
		return
	end
end

sgs.ai_use_value.YHYijieCard = 6.4
sgs.ai_use_priority.YHYijieCard = 4.8

sgs.ai_skill_askforyiji.yhxinghan = function(self,card_ids)
	local cards = {}
	for d,id in sgs.list(card_ids)do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
	self:sortByKeepValue(cards)
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
	   	if not self:isFriend(p) then continue end
		return p,cards[1]:getEffectiveId()
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
	   	if self:isEnemy(p) then continue end
		return p,cards[1]:getEffectiveId()
	end
end

addAiSkills("yhxinghan").getTurnUseCard = function(self)
	local cards = self:addHandPile("he")
	self:sortByKeepValue(cards)
  	for d,c in sgs.list(cards)do
	   	local fs = dummyCard("slash")
		fs:setSkillName("yhxinghan")
		fs:addSubcard(c)
		if fs:isAvailable(self.player)
		and self.player:getMark("yhxinghan-PlayClear")==1
		and c:getTypeId()~=1
	   	then return fs end
	end
end

addAiSkills("yhbianzhan").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	self.yhbianzhan_c = nil
  	for _,c in sgs.list(cards)do
		if c:getNumber()>9
		or #cards>self.player:getMaxCards()
		then
			self.yhbianzhan_c = c
			return sgs.Card_Parse("@YHBianzhanCard=.")
		end
	end
end

sgs.ai_skill_use_func["YHBianzhanCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>self.player:getHandcardNum()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.YHBianzhanCard = 9.4
sgs.ai_use_priority.YHBianzhanCard = 4.4

sgs.ai_skill_discard.yhjifeng = function(self)
	local cards = {}
    local handcards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByKeepValue(handcards) -- 按保留值排序
   	for _,h in sgs.list(handcards)do
		if #cards>1 then break end
		table.insert(cards,h:getEffectiveId())
	end
	if self.yhbianzhan_c
	and self.yhbianzhan_c:getSuit()==3
	and self.yhbianzhan_c:getNumber()>9
	then return cards end
end

sgs.ai_skill_playerchosen.yhjifeng = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and target:getHandcardNum()>0
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and target:getHandcardNum()>0
		then return target end
	end
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	return destlist[1]
end

addAiSkills("yhhuntian").getTurnUseCard = function(self)
	return sgs.Card_Parse("@YHHuntianCard=.")
end

sgs.ai_skill_use_func["YHHuntianCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.YHHuntianCard = 2.4
sgs.ai_use_priority.YHHuntianCard = 1.8

sgs.ai_skill_invoke.yhhuntian = function(self,data)
    return true
end


sgs.ai_skill_playerchosen.yhceri = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_invoke.yhsancai = function(self,data)
    return true
end

sgs.ai_skill_askforyiji.yhsancai = function(self,card_ids)
	local cards = {}
	for d,id in sgs.list(card_ids)do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
	self:sortByKeepValue(cards)
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
	   	if p:hasFlag("yhsancai_give")
		or not self:isFriend(p)
		then continue end
		return p,cards[1]:getEffectiveId()
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
	   	if p:hasFlag("yhsancai_give")
		or self:isEnemy(p)
		then continue end
		return p,cards[1]:getEffectiveId()
	end
end

sgs.ai_skill_discard.yhjuyi = function(self)
	local cards = {}
    local handcards = sgs.QList2Table(self.player:getCards("he"))
    self:sortByKeepValue(handcards) -- 按保留值排序
   	for _,h in sgs.list(handcards)do
		if #cards>0 then break end
		table.insert(cards,h:getEffectiveId())
	end
   	local target = self.room:getCurrent()
	if not self:isEnemy(target)
	then return {} end
	return #handcards>3 and cards or {}
end

sgs.ai_skill_invoke.yhhanjie = function(self,data)
	local target = self.player
	local use = data:toCardUse()
	return not self:isFriend(use.from)
	and use.card:getNumber()>8
end

addAiSkills("yhjuxian").getTurnUseCard = function(self)
	if #self.friends_noself>0
	then
		return sgs.Card_Parse("@YHJuxianCard=.")
	end
end

sgs.ai_skill_use_func["YHJuxianCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.YHJuxianCard = 3.4
sgs.ai_use_priority.YHJuxianCard = 1.8

sgs.ai_skill_playerchosen.yhjuxian = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
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

sgs.ai_skill_playerchosen.yhdujian = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	return destlist[1]
end

addAiSkills("yhbuque_put").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards)
	if #cards<1 then return end
    for _,p in sgs.list(self.room:findPlayersBySkillName("yhbuque"))do
		if not self:isEnemy(yhbuque) and p:getMark("yhbuque_put-PlayClear")<1 and #cards>self.player:getMaxCards()
		then return sgs.Card_Parse("@YHBuquePutCard="..cards[1]:getEffectiveId()) end
	end
	cards = self:poisonCards("e")
    for _,p in sgs.list(self.room:findPlayersBySkillName("yhbuque"))do
		if p:getMark("yhbuque_put-PlayClear")<1 and #cards>0
		then return sgs.Card_Parse("@YHBuquePutCard="..cards[1]:getEffectiveId()) end
	end
end

sgs.ai_skill_use_func["YHBuquePutCard"] = function(card,use,self)
	use.card = card
    for _,p in sgs.list(self.room:findPlayersBySkillName("yhbuque"))do
		if not self:isEnemy(p) and p:getMark("yhbuque_put-PlayClear")<1 and use.to
		then use.to:append(p) return end
	end
    for _,p in sgs.list(self.room:findPlayersBySkillName("yhbuque"))do
		if p:getMark("yhbuque_put-PlayClear")<1 and use.to
		then use.to:append(p) return end
	end
end

sgs.ai_use_value.YHBuquePutCard = 9.4
sgs.ai_use_priority.YHBuquePutCard = -0.4

addAiSkills("yhbuque").getTurnUseCard = function(self)
	local cs = {}
    for c,id in sgs.list(self.room:getDrawPile())do
		c = sgs.Sanguosha:getCard(id)
		if c:hasFlag("visible")
		then table.insert(cs,c) end
	end
	return #cs>0 and cs
end

sgs.ai_guhuo_card.yhbuque = function(self,toname,class_name)
    for c,id in sgs.list(self.room:getDrawPile())do
		c = sgs.Sanguosha:getCard(id)
		if c:hasFlag("visible") and c:isKindOf(class_name)
		then return c:toString() end
	end
end

sgs.ai_skill_invoke.yhchenzhen = function(self,data)
    return self.player:getPhase()<=sgs.Player_Play
	and not self:willSkipPlayPhase()
	and self.player:getMaxHp()>1
	and #self.enemies>0
end

sgs.ai_skill_choice.yhchenzhen = function(self,choices)
	local items = choices:split("+")
	return items[1]
end

sgs.ai_skill_choice.yhchenzhen_num = function(self,choices)
	local items = choices:split("+")
	return items[#items]
end

sgs.ai_skill_playerchosen.yhsigong = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	return destlist[1]
end

addAiSkills("yhxijian_give").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards)
	self.ispc = nil
	local yhxijian = self.room:findPlayerBySkillName("yhxijian")
	if yhxijian and not self:isEnemy(yhxijian) and #cards>self.player:getMaxCards()
	then return sgs.Card_Parse("@YHXijianGiveCard="..cards[1]:getEffectiveId()) end
	cards = self:poisonCards()
	if #cards>0
	then
		self.ispc = true
		return sgs.Card_Parse("@YHXijianGiveCard="..cards[1]:getEffectiveId())
	end
end

sgs.ai_skill_use_func["YHXijianGiveCard"] = function(card,use,self)
	if self.ispc
	then
		for _,ep in sgs.list(self.enemies)do
			if ep:hasSkill("yhxijian")
			and ep:getMark("yhxijian_give-PlayClear")<1
			then
				use.card = card
				use.to:append(ep)
				return
			end
		end
	else
		for _,ep in sgs.list(self.friends_noself)do
			if ep:hasSkill("yhxijian")
			and ep:getMark("yhxijian_give-PlayClear")<1
			then
				use.card = card
				use.to:append(ep)
				return
			end
		end
	end
end

sgs.ai_use_value.YHXijianGiveCard = 9.4
sgs.ai_use_priority.YHXijianGiveCard = -0.4

sgs.ai_skill_playerchosen.yhxijian = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_invoke.yhboben = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return self:isEnemy(target)
	end
end

sgs.ai_skill_discard.yhboben = function(self,x,n)
	local cards = {}
    local handcards = sgs.QList2Table(self.player:getCards("he"))
    self:sortByKeepValue(handcards) -- 按保留值排序
   	for _,h in sgs.list(handcards)do
		if #cards>=n then break end
		table.insert(cards,h:getEffectiveId())
	end
   	local target = self.room:getCurrent()
	if self:isEnemy(target)
	then return cards end
	return {}
end

sgs.ai_skill_playerchosen.yhjuantu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

addAiSkills("yhquanwang").getTurnUseCard = function(self)
	if self.player:getHandcardNum()>0
	and #self.friends_noself>0
	and self:isWeak()
	then
		return sgs.Card_Parse("@YHQuanwangCard=.")
	end
end

sgs.ai_skill_use_func["YHQuanwangCard"] = function(card,use,self)
	self:sort(self.friends_noself,"handcard")
	for _,ep in sgs.list(self.friends_noself)do
		use.card = card
		use.to:append(ep)
		return
	end
end

sgs.ai_use_value.YHQuanwangCard = 3.4
sgs.ai_use_priority.YHQuanwangCard = -1.8

sgs.ai_skill_choice.yhchenwen = function(self,choices)
	local items = choices:split("+")
	local n = self.player:getLostHp()
	n = n>2 and math.random(2,3) or n
	if n>0 and items[1]:startsWith("yhchenwen")
	then return items[n] end
	if table.contains(items,"yhpodi3")
	then return "yhpodi3" end
	if table.contains(items,"yhpodi2")
	then return "yhpodi2" end
	if table.contains(items,"yhshouzhuang")
	then return "yhshouzhuang" end
	if table.contains(items,"yhpodi4")
	then return "yhpodi4" end
	return items[#items]
end

sgs.ai_skill_discard.yhchenwen = function(self,max,min)
	local to_cards = {}
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards,nil,true)
   	for _,hcard in sgs.list(cards)do
   		if #to_cards>#cards-self.player:getMaxCards()
		or #to_cards>=3 then break end
		table.insert(to_cards,hcard:getEffectiveId())
	end
	return to_cards
end

sgs.ai_skill_invoke.yhshouzhuang = function(self,data)
	local invoke = data:toPlayer()
	if invoke
	then
		return self:doDisCard(invoke,"he")
		or not self:isFriend(invoke)
	end
	invoke = data:toString()
	if invoke and invoke~=""
	then
		invoke = invoke:split(":")[2]
		invoke = BeMan(self.room,invoke)
		return invoke and (self:doDisCard(invoke,"he") or not self:isFriend(invoke))
	end
end

sgs.ai_skill_choice.yhshouzhuang = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	local n = items[1]:split("=")[3]
	n = n-0
	if self:isFriend(to)
	then
		if n<=to:getEquips():length()
		then return items[1]
		else return items[2] end
	else
		if n>to:getEquips():length()
		then return items[1]
		else return items[2] end
	end
end

addAiSkills("yhpodi").getTurnUseCard = function(self)
	return sgs.Card_Parse("@YHPodiCard=.")
end

sgs.ai_skill_use_func["YHPodiCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>1
		and self.player:isWounded()
		and ep:getMark("yhpodi_to-PlayClear")<1
		then
			use.card = card
			use.to:append(ep)
			ep:addMark("yhpodi_to-PlayClear")
			return
		end
	end
end

sgs.ai_use_value.YHPodiCard = 4.4
sgs.ai_use_priority.YHPodiCard = 6.8

sgs.ai_skill_choice.yhpodi = function(self,choices)
	local items = choices:split("+")
	if #items>4 then return items[math.random(2,4)] end
	if #items>3 then return items[math.random(2,3)] end
	if #items>2 then return items[2] end
	return items[#items-1]
end

addAiSkills("yhduwei").getTurnUseCard = function(self)
  	local name = self.player:property("yhduwei_damage_card"):toString()
	local cards = self:addHandPile("he")
	self:sortByKeepValue(cards,nil,true)
	for _,c in sgs.list(cards)do
	   	if name=="" then break end
		local fs = dummyCard(name)
		fs:setSkillName("yhduwei")
		fs:addSubcard(c)
		if self:getCardsNum(fs:getClassName())<2
	   	then return fs end
	end
end

sgs.ai_skill_use["@@yhduwei"] = function(self,prompt)
  	local name = self.player:property("yhduwei_damage_card"):toString()
	local cards = self:addHandPile("he")
	self:sortByKeepValue(cards,nil,true)
	for dc,c in sgs.list(cards)do
	   	if name=="" then break end
		dc = dummyCard(name)
	   	if dc==nil then break end
		dc:setSkillName("yhduwei")
		dc:addSubcard(c)
		local dummy = self:aiUseCard(dc)
		if dummy.card and dummy.to
		and dc:isAvailable(self.player)
		then
			if dc:canRecast()
			and dummy.to:length()<1
			then return end
			local tos = {}
			for _,p in sgs.list(dummy.to)do
				table.insert(tos,p:objectName())
			end
			return dc:toString().."->"..table.concat(tos,"+")
		end
    end
end

sgs.ai_skill_invoke.yhsiku = function(self,data)
	local invoke = data:toString()
	if invoke~=""
	then
		invoke = invoke:split(":")[2]
		invoke = BeMan(self.room,invoke)
		return self:isEnemy(invoke) and (not self:isWeak(invoke) or #self.enemies<2 or self.player:isDead())
	end
end

sgs.ai_skill_invoke.yhqingsi = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return self:isFriend(target) and not self:isWeak()
	end
end

sgs.ai_skill_playerchosen.yhchuzhu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	if self:getCardsNum("Peach")+self:getCardsNum("Analeptic")>0
	then return end
	local n = self.player:getTag("YHQingsiNum"):toInt()
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getLostHp()>=n
		then return target end
	end
end

sgs.ai_skill_cardask["@yhhuairen-put"] = function(self,data,pattern)
	return true
end

addAiSkills("yhyuren").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,true)
	local tocs = {}
  	for d,c in sgs.list(cards)do
		if c:isKindOf("Slash") then table.insert(tocs,c) end
	end
	if #tocs<1 then return end
	for dc,pn in sgs.list(patterns())do
		dc = dummyCard(pn)
		if dc and dc:isKindOf("BasicCard")
		and dc:isAvailable(self.player)
		and self:getCardsNum(dc:getClassName())<1
		then
			dc:addSubcard(tocs[1])
			dc:setSkillName("yhyuren")
			d = self:aiUseCard(dc)
			if d.card and d.to
			then
				self.yhyuren_to = d.to
				if dc:canRecast() and d.to:length()<1 then continue end
				sgs.ai_use_priority.YHYurenCard = sgs.ai_use_priority[dc:getClassName()]-0.3
				return sgs.Card_Parse("@YHYurenCard="..tocs[1]:getEffectiveId()..":"..pn)
			end
		end
	end
end

sgs.ai_skill_use_func["YHYurenCard"] = function(card,use,self)
	if self.yhyuren_to
	then
		use.card = card
		use.to = self.yhyuren_to
	end
end

sgs.ai_use_value.YHYurenCard = 5.4
sgs.ai_use_priority.YHYurenCard = 2.8

sgs.ai_guhuo_card.yhyuren = function(self,toname,class_name)
	local cards = self:addHandPile()
	cards = self:sortByKeepValue(cards,nil,true)
  	for d,c in sgs.list(cards)do
		d = dummyCard(toname)
		if c:isKindOf("Slash")
		and d and d:isKindOf("BasicCard")
		and self:getCardsNum(d:getClassName())<2
		and sgs.Sanguosha:getCurrentCardUseReason()==sgs.CardUseStruct_CARD_USE_REASON_RESPONSE_USE
		then return "@YHYurenCard="..c:getEffectiveId()..":"..toname end
	end
end

sgs.ai_skill_playerchosen.yhrangwei = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:isWounded()
		and self.player:isWounded()
		then return target end
	end
end

sgs.ai_skill_playerchosen.yhxiaoyun = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and self:doDisCard(target,"ej")
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:doDisCard(target,"ej")
		then return target end
	end
end

addAiSkills("yhmeiying").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards,nil,true)
	self:sort(self.enemies,"hp")
  	for _,c in sgs.list(cards)do
		if c:isAvailable(self.player) then continue end
		for _,fp in sgs.list(self.friends_noself)do
			if c:isAvailable(fp)
			then
				local d = sgs.ais[fp:objectName()]:aiUseCard(c)
				if d.card and d.to
				then
					self.yhmeiying_to = fp
					return sgs.Card_Parse("@YHMeiyingCard="..c:getEffectiveId())
				end
			end
		end
		for _,ep in sgs.list(self.enemies)do
			if c:isAvailable(ep) then continue end
			self.yhmeiying_to = ep
			return sgs.Card_Parse("@YHMeiyingCard="..c:getEffectiveId())
		end
	end
end

sgs.ai_skill_use_func["YHMeiyingCard"] = function(card,use,self)
	if self.yhmeiying_to
	then
		use.card = card
		use.to:append(self.yhmeiying_to)
	end
end

sgs.ai_use_value.YHMeiyingCard = 5.4
sgs.ai_use_priority.YHMeiyingCard = 2.8

sgs.ai_skill_use["@@yhmeiying"] = function(self,prompt)
  	local c = self.player:getMark("yhmeiying_id-PlayClear")
	c = sgs.Sanguosha:getCard(c)
	local dummy = self:aiUseCard(c)
	if dummy.card and dummy.to
	then
		if c:canRecast()
		and dummy.to:length()<1
		then return end
		local tos = {}
		for _,p in sgs.list(dummy.to)do
			table.insert(tos,p:objectName())
		end
		return c:toString().."->"..table.concat(tos,"+")
    end
end

sgs.ai_skill_choice.yhkudu = function(self,choices)
	local items = choices:split("+")
	self.yhkudu_to = nil
	if table.contains(items,"self=judge")
	and self:doDisCard(self.player,"j")
	then return "self=judge" end
	if table.contains(items,"self=discard")
	and self.player:getHandcardNum()>=self.player:getMaxCards()
	then return "self=discard" end
	if table.contains(items,"self=discard")
	then return "self=discard" end
	if table.contains(items,"self=judge")
	then return "self=judge" end
	if table.contains(items,"self=draw")
	and self.player:getHandcardNum()>3
	then return "self=draw" end
	if table.contains(items,"self=play")
	and self.player:getHandcardNum()<3
	then return "self=play" end
	if table.contains(items,"self=draw")
	then return "self=draw" end
	if table.contains(items,"self=play")
	then return "self=play" end
	
	if table.contains(items,"other=play")
	then
		self:sort(self.enemies,"handcard",true)
		for i,ep in sgs.list(self.enemies)do
			self.yhkudu_to = ep
			if ep:getHandcardNum()>2
			then
				return "other=play"
			end
		end
	end
	if table.contains(items,"other=draw")
	then
		self:sort(self.enemies,"handcard")
		for i,ep in sgs.list(self.enemies)do
			self.yhkudu_to = ep
			if ep:getHandcardNum()<3
			then
				return "other=draw"
			end
		end
	end
	if table.contains(items,"other=discard")
	then
		self:sort(self.friends_noself,"handcard",true)
		for i,ep in sgs.list(self.enemies)do
			self.yhkudu_to = ep
			if ep:getHandcardNum()>3
			then
				return "other=discard"
			end
		end
	end
	if table.contains(items,"other=judge")
	then
		self:sort(self.friends_noself,"handcard")
		for i,ep in sgs.list(self.enemies)do
			self.yhkudu_to = ep
			if self:doDisCard(ep,"j")
			then
				return "other=judge"
			end
		end
	end
	
	if table.contains(items,"other=play")
	then
		self:sort(self.enemies,"handcard",true)
		for i,ep in sgs.list(self.enemies)do
			self.yhkudu_to = ep
			return "other=play"
		end
	end
	if table.contains(items,"other=draw")
	then
		self:sort(self.enemies,"handcard")
		for i,ep in sgs.list(self.enemies)do
			self.yhkudu_to = ep
			return "other=draw"
		end
	end
	if table.contains(items,"other=discard")
	then
		self:sort(self.friends_noself,"handcard",true)
		for i,ep in sgs.list(self.enemies)do
			self.yhkudu_to = ep
			return "other=discard"
		end
	end
	if table.contains(items,"other=judge")
	then
		self:sort(self.friends_noself,"handcard")
		for i,ep in sgs.list(self.enemies)do
			self.yhkudu_to = ep
			return "other=judge"
		end
	end
	
	
end

sgs.ai_skill_invoke.yhkudu = function(self,data)
    local choices = "other=play+other=draw+other=discard+other=judge"
	return sgs.ai_skill_choice.yhkudu(self,choices)
end

sgs.ai_skill_playerchosen.yhkudu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if target==self.yhkudu_to
		then return target end
	end
end

sgs.ai_skill_invoke.yhkunmo = function(self,data)
    return true
end

sgs.ai_skill_use["@@yhkunmo"] = function(self,prompt)
	local cards = self.player:getCards("h")
	cards = self:sortByUseValue(cards,true,true)
  	for d,c in sgs.list(cards)do
		if c:isAvailable(self.player)
		then
			local dummy = self:aiUseCard(c)
			if dummy.card and dummy.to
			then
				if c:canRecast()
				and dummy.to:length()<1
				then return end
				local tos = {}
				for _,p in sgs.list(dummy.to)do
					table.insert(tos,p:objectName())
				end
				return "@YHKunmoCard="..c:getEffectiveId().."->"..table.concat(tos,"+")
			end
		end
    end
end

sgs.ai_skill_playerchosen.yhtanyou = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"maxhp",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:isWounded()
		then return target end
	end
end

sgs.ai_skill_use["@@yhbozhi"] = function(self,prompt)
  	local c = self.player:property("YHBozhiRecordTrick"):toString()
	c = dummyCard(c)
	c:setSkillName("yhbozhi")
	local dummy = self:aiUseCard(c)
	if dummy.card and dummy.to
	and c:isAvailable(self.player)
	then
		if c:canRecast()
		and dummy.to:length()<1
		then return end
		local tos = {}
		for _,p in sgs.list(dummy.to)do
			table.insert(tos,p:objectName())
		end
		return c:toString().."->"..table.concat(tos,"+")
    end
end

sgs.ai_skill_invoke.yhbozhi = function(self,data)
    return sgs.ai_skill_use["@@yhbozhi"](self,"yhbozhi0")
end

sgs.ai_skill_invoke.yhjixiang = function(self,data)
	for _,fp in sgs.list(self.room:getOtherPlayers(self.player))do
		if not self:isEnemy(fp)
		then return true end
	end
end

sgs.ai_skill_askforyiji.yhjixiang = function(self,card_ids,tos)
    local to,id = sgs.ai_skill_askforyiji.nosyiji(self,card_ids,tos)
	if to and to:objectName()~=self.player:objectName()
	then return to,id end
	self:sort(self.friends_noself,"handcard")
	for _,fp in sgs.list(self.friends_noself)do
		return fp,card_ids[1]
	end
	to = self.room:getOtherPlayers(self.player)
	to = self:sort(to,"handcard")
	for _,fp in sgs.list(to)do
		if not self:isEnemy(fp)
		then
			return fp,card_ids[1]
		end
	end
	for _,fp in sgs.list(to)do
		return fp,card_ids[1]
	end
end

sgs.ai_skill_playerchosen.yhguidu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not (self:isFriend(target) or self:isWeak(target))
		then return target end
	end
end

sgs.ai_skill_use["@@yhshanhai"] = function(self,prompt)
  	local ids = self.player:property("YHShanhaiGetIds"):toString():split("+")
	for c,id in sgs.list(self.player:handCards())do
		if not table.contains(ids,""..id)
		then continue end
		c = dummyCard()
		c:addSubcard(id)
		c:setSkillName("yhshanhai")
		local dummy = self:aiUseCard(c)
		if dummy.card and dummy.to
		and c:isAvailable(self.player)
		then
			if c:canRecast()
			and dummy.to:length()<1
			then return end
			local tos = {}
			for _,p in sgs.list(dummy.to)do
				table.insert(tos,p:objectName())
			end
			return c:toString().."->"..table.concat(tos,"+")
		end
	end
end

sgs.ai_skill_choice.yhyanglian = function(self,choices)
	local items = choices:split("+")
	return items[math.random(1,#items-1)]
end

sgs.ai_skill_choice.yhxiandao = function(self,choices)
	local items = choices:split("+")
	if self.player:getHandcardNum()<4
	then return items[2] end
	return items[#items]
end

sgs.ai_skill_playerchosen.yhqizu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:isWeak(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and not self:isWeak()
		then return target end
	end
end

sgs.ai_skill_playerchosen.yhshoulu_from = function(self,players)
    if sgs.ai_skill_invoke.peiqi(self,ToData())
	then
		for _,target in sgs.list(players)do
			if target:objectName()==self.peiqiData.from:objectName()
			then return target end
		end
	end
end

sgs.ai_skill_playerchosen.yhshoulu_to = function(self,players)
	for _,target in sgs.list(players)do
		if target:objectName()==self.peiqiData.to:objectName()
		then return target end
	end
end

sgs.ai_skill_cardchosen.yhshoulu = function(self,who,flags,method)
	for i,c in sgs.list(who:getCards(flags))do
		i = c:getEffectiveId()
		if i==self.peiqiData.cid
		then return i end
	end
end

sgs.ai_skill_invoke.yhyange = function(self,data)
    return true
end

sgs.ai_skill_choice.yhqupai = function(self,choices)
	local items = choices:split("+")
	if math.random()>0.3
	then return "2" end
	if math.random()>0.6
	then return "1" end
	return "cancel"
end









