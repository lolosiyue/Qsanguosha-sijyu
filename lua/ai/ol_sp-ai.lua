

sgs.ai_skill_discard.zhouxuanz = function(self,x,n)
	local cards = {}
    local handcards = self.player:getCards("h")
    handcards = self:sortByKeepValue(handcards,true) -- 按保留值排序
   	for _,h in sgs.list(handcards)do
		if #handcards-#cards<self.player:getMaxCards()/2 or #cards>=x then break end
		table.insert(cards,h:getEffectiveId())
	end
	return cards
end

sgs.ai_skill_invoke.xianlve = function(self,data)
    return true
end

sgs.ai_skill_askforag.xianlve = function(self,card_ids)
	for _,id in sgs.list(card_ids)do
		local c = sgs.Sanguosha:getCard(id)
		if self:getUseValue(c)>4
		then return id end
	end
end

addAiSkills("zaowang").getTurnUseCard = function(self)
	return sgs.Card_Parse("@ZaowangCard=.")
end

sgs.ai_skill_use_func["ZaowangCard"] = function(card,use,self)
	self:sort(self.friends,"hp",true)
	for _,ep in sgs.list(self.friends)do
		if ep:getHp()>=self.player:getHp()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.ZaowangCard = 9.4
sgs.ai_use_priority.ZaowangCard = 4.8

sgs.ai_skill_invoke.guowu = function(self,data)
    return true
end

sgs.ai_skill_use["@@guowu2"] = function(self,prompt)
	local valid = {}
	local pr = prompt:split(":")
	local n = pr[4]-0
	local destlist = self.room:getAllPlayers()
    destlist = self:sort(destlist,"hp")
	for i=1,n do
		local use = sgs.CardUseStruct()
		use.from = self.player
		use.card = dummyCard(pr[2])
		local tos = sgs.SPlayerList()
		for _,ep in sgs.list(destlist)do
			if ep:hasFlag("guowu_canchoose")
			then tos:append(ep)
			elseif CanToCard(use.card,self.player,ep,use.to)
			then use.to:append(ep) end
		end
		self.player:setTag("yb_zhuzhan2_data",ToData(use))
		tos = sgs.ai_skill_playerchosen.yb_zhuzhan2(self,tos)
		if tos
		then
			table.insert(valid,tos:objectName())
			table.removeOne(destlist,tos)
		end
	end
	if #valid>0
	then
    	return string.format("@GuowuCard=.->%s",table.concat(valid,"+"))
	end
end

sgs.ai_skill_invoke.yuqi = function(self,data)
	local target = data:toPlayer()
	if target then
		self.yuqi_to = target
	end
	return true
end

sgs.ai_skill_use["@@yuqi1"] = function(self,prompt)
	if not self:isFriend(self.yuqi_to)
	then return end
	local yuqi_help = self.player:getTag("yuqiForAI"):toIntList()
	local n1,n2 = {},{}
	for c,id in sgs.list(yuqi_help)do
		table.insert(n1,sgs.Sanguosha:getCard(id))
	end
	local n = self.player:getMark("yuqi_help")
	self:sortByKeepValue(n1,true)
	local poisons = self:poisonCards(n1)
	for _,c in sgs.list(n1)do
		if #n2>=n then break end
		if table.contains(poisons,c) then continue end
		table.insert(n2,c:getEffectiveId())
	end
	return #n2>0 and ("@YuqiCard="..table.concat(n2,"+"))
end

sgs.ai_skill_use["@@yuqi2"] = function(self,prompt)
	local valid = {}
	local yuqi_help = self.player:getTag("yuqiForAI"):toIntList()
	local n1,n2 = {},{}
	for _,id in sgs.list(yuqi_help)do
		table.insert(n1,sgs.Sanguosha:getCard(id))
	end
	local n = self.player:getMark("yuqi_help")
	self:sortByKeepValue(n1,true)
	local poisons = self:poisonCards(n1)
	for _,c in sgs.list(n1)do
		if #n2>=n then break end
		if table.contains(poisons,c) then continue end
		table.insert(n2,c:getEffectiveId())
	end
	return #n2>0 and ("@YuqiCard="..table.concat(n2,"+"))
end

sgs.ai_skill_invoke.shanshen = function(self,data)
    return true
end

sgs.ai_skill_invoke.xianjing = function(self,data)
    return true
end

sgs.ai_can_damagehp.yuqi = function(self,from,card,to)
	if (not card or card:isDamageCard()) and not self:isWeak(to)
	and to:getMark("yuqi-Clear")<2
	and not self:isFriend(from)
	and self:canLoseHp(from,card)
	then return true end
end

sgs.ai_nullification.yuqi = function(self,trick,from,to,positive)
    if to:hasSkill("yuqi")
	and self:isFriend(to)
	and not self:isWeak(to)
	and trick:isDamageCard()
	and to:getMark("yuqi-Clear")<2
   	and self:canLoseHp(from,trick,to)
	then return false end
end

sgs.ai_skill_invoke.huguan = function(self,data)
	local target = data:toPlayer()
	if target then
		return self:isFriend(target)
	end
end

sgs.ai_skill_cardask["@yaopei-discard"] = function(self,data,pattern,prompt)
    local parsed = data:toPlayer()
    if self:isFriend(parsed)
	then return true end
	return "."
end

sgs.ai_skill_choice.yaopei = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if self:isWeak() then return items[1] end
	if self:isWeak(target) then return items[2] end
	if self.player:isWounded() then return items[1] end
	if target:isWounded() then return items[2] end
	if target:getHandcardNum()>=target:getHandcardNum()
	then return items[2] else return items[1] end
end

sgs.ai_skill_use["@@heqia1"] = function(self,prompt)
	local valid,to = {},nil
	if #self.friends_noself<1 then return end
	self:sort(self.friends_noself,"card",true)
    for _,p in sgs.list(self.friends_noself)do
      	if p:getHandcardNum()>4
    	then return string.format("@HeqiaCard=.->%s",p:objectName()) end
	end
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if #valid>=#cards/2 then break end
    	table.insert(valid,h:getEffectiveId())
	end
	to = self.friends_noself[#self.friends_noself]
	if #valid<1 then return end
	return string.format("@HeqiaCard=%s->%s",table.concat(valid,"+"),to:objectName())
end

sgs.ai_skill_discard.heqia = function(self,x,n)
	local to_cards = {}
	local cards = self.player:getCards("h")
	cards = self:sortByUseValue(cards,true)
	if self:isFriend(self.room:getCurrent())
	then
		for _,c in sgs.list(cards)do
			if #to_cards>=#cards/2 then break end
			table.insert(to_cards,c:getEffectiveId())
		end
	end
	for _,c in sgs.list(cards)do
		if #to_cards>=n then break end
		table.insert(to_cards,c:getEffectiveId())
	end
	return to_cards
end

sgs.ai_skill_askforag.heqia = function(self,card_ids)
	local cards = self.player:getCards("h")
	if cards:length()<1 then return end
	for _,id in sgs.list(card_ids)do
		local c = sgs.Sanguosha:getCard(id)
		c = dummyCard(c:objectName())
		c:addSubcard(cards:at(0))
		c:setSkillName("_heqia")
		local d = self:aiUseCard(c)
		self.heqia_use = d
		if d.card and d.to
		then return id end
	end
end

sgs.ai_skill_use["@@heqia2"] = function(self,prompt)
    local dummy = self.heqia_use
   	if dummy.card and dummy.to then
      	local tos = {}
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
		if dummy.card:isKindOf("Peach") then
			for _,p in sgs.list(self.friends_noself)do
				if self.player:isProhibited(p,dummy.card)
				or not p:isWounded()
				or #tos>=self.player:getMark("heqia_get_card")
				then continue end
				table.insert(tos,p:objectName())
			end
		end
       	return dummy.card:toString().."->"..table.concat(tos,"+")
    end
end

sgs.ai_skill_invoke.lanjiang = function(self,data)
    return true
end

sgs.ai_skill_invoke.lanjiang_draw = function(self,data)
   	local target = self.room:getCurrent()
	return self:isFriend(target)
end

sgs.ai_skill_playerchosen.lanjiang = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and target:getHandcardNum()>=self.player:getHandcardNum()
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and target:getHandcardNum()>=self.player:getHandcardNum()
		then return target end
	end
	self:sort(destlist,"hp")
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()<self.player:getHandcardNum()
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		and target:getHandcardNum()<self.player:getHandcardNum()
		then return target end
	end
--	return destlist[1]
end

sgs.ai_skill_cardask["@mingluan-discard"] = function(self,data,pattern,prompt)
    local target = self.room:getCurrent()
    if self.player:getHandcardNum()<target:getHandcardNum()
	and self.player:getHandcardNum()<5
	then return true end
	return "."
end

sgs.ai_skill_playerchosen.bingqing = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	local n = self.player:getMark("bingqing_suit-PlayClear")
	self:sort(destlist,"hp")
	if n<3 then
		for _,target in sgs.list(destlist)do
			if self:isFriend(target)
			then return target end
		end
		for _,target in sgs.list(destlist)do
			if not self:isEnemy(target)
			then return target end
		end
	elseif n<4 then
		for _,target in sgs.list(destlist)do
			if self:isFriend(target)
			and self:doDisCard(target,"ej")
			then return target end
		end
		for _,target in sgs.list(destlist)do
			if self:isEnemy(target)
			then return target end
		end
		for _,target in sgs.list(destlist)do
			if not self:isFriend(target)
			then return target end
		end
	elseif n<5 then
		for _,target in sgs.list(destlist)do
			if self:isEnemy(target)
			then return target end
		end
		for _,target in sgs.list(destlist)do
			if not self:isFriend(target)
			then return target end
		end
	end
--	return destlist[1]
end

sgs.ai_skill_playerchosen.yingfeng = function(self,players)
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

sgs.ai_skill_playerchosen.jixianzl = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and target:getHandcardNum()<3
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and target:getHandcardNum()<3
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
end

addAiSkills("jinhui").getTurnUseCard = function(self)
	return sgs.Card_Parse("@JinhuiCard=.")
end

sgs.ai_skill_use_func["JinhuiCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.JinhuiCard = 9.4
sgs.ai_use_priority.JinhuiCard = 4.8

sgs.ai_skill_use["@@jinhui2!"] = function(self,prompt)
	local ids = self.player:getTag("jinhuiForAI"):toIntList()
	local n1 = {}
	for c,id in sgs.list(ids)do
		table.insert(n1,sgs.Sanguosha:getCard(id))
	end
	self:sortByKeepValue(n1,true)
   	local target = self.room:getCurrent()
	for _,c in sgs.list(n1)do
		if self.player:canUse(c,target,true)
		then
			return ("@JinhuiUseCard="..c:getEffectiveId())
		end
	end
	return ("@JinhuiUseCard="..n1[1]:getEffectiveId())
end

sgs.ai_skill_use["@@jinhui1"] = function(self,prompt)
	local ids = self.player:getTag("jinhuiForAI"):toIntList()
	local n1,n2 = {},{}
	for c,id in sgs.list(ids)do
		table.insert(n1,sgs.Sanguosha:getCard(id))
	end
	self:sortByKeepValue(n1,true)
	return ("@JinhuiUseCard="..n1[1]:getEffectiveId())
end

sgs.ai_skill_playerchosen.jinhui = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
	return destlist[1]
end

sgs.ai_skill_choice.saodi = function(self,choices,data)
	local items = choices:split("+")
	local use = data:toCardUse()
    local to = use.to:at(0)
	local rights,right,lefts,left = 0,0,0,0
    local to1 = self.player:getNextAlive()
    while to1~=to do
        rights = rights+1
		right = self:isFriend(to1) and right+1 or right
        to1 = to1:getNextAlive()
    end
    to1 = to:getNextAlive()
    while to1~=self.player do
        lefts = lefts+1
		left = self:isFriend(to1) and left+1 or left
        to1 = to1:getNextAlive()
    end
	if rights<lefts
	then
		if rights-right>=right
		then return items[1] end
	elseif rights>lefts
	then
		if lefts-left>=left
		then return items[1] end
	else
		if rights-right>=right
		then return items[1] end
		if lefts-left>=left
		then return items[2] end
	end
	return items[2]
end

sgs.ai_skill_playerchosen.zhuitao = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self.player:distanceTo(target)>1
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and self.player:distanceTo(target)>1
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and self.player:distanceTo(target)>1
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_invoke.jiqiaosy = function(self,data)
    return self.player:getMaxHp()>2
end

sgs.ai_skill_use["@@jiqiaosy!"] = function(self,prompt)
	local n1,n2 = {},{}
	n2.isRed = 0
	n2.isBlack = 0
	for _,id in sgs.list(self.player:getPile("jiqiaosy"))do
		local c = sgs.Sanguosha:getCard(id)
		table.insert(n1,c)
		if c:isRed() then n2.isRed = n2.isRed+1
		else n2.isBlack = n2.isBlack+1 end
	end
	self:sortByUseValue(n1,true)
	for _,c in sgs.list(n1)do
		local d = self:aiUseCard(c)
		if d.card and c:isAvailable(self.player)
		then
			if c:isRed() and n2.isRed>n2.isBlack
			then return ("@JiqiaosyCard="..c:getEffectiveId())
			elseif c:isBlack() and n2.isRed<n2.isBlack
			then return ("@JiqiaosyCard="..c:getEffectiveId()) end
		end
	end
	for _,c in sgs.list(n1)do
		if c:isRed() and n2.isRed>n2.isBlack
		then return ("@JiqiaosyCard="..c:getEffectiveId())
		elseif c:isBlack() and n2.isRed<n2.isBlack
		then return ("@JiqiaosyCard="..c:getEffectiveId()) end
	end
	self:sortByKeepValue(n1,true)
	for _,c in sgs.list(n1)do
		local d = self:aiUseCard(c)
		if d.card and c:isAvailable(self.player)
		then
			return ("@JiqiaosyCard="..c:getEffectiveId())
		end
	end
	return ("@JiqiaosyCard="..n1[1]:getEffectiveId())
end

sgs.ai_skill_invoke.xiongyisy = function(self,data)
    local dying = data:toDying()
	return dying.who:objectName()==self.player:objectName()
	and self:getCardsNum("Peach")+self:getCardsNum("Analeptic")<1
end

addAiSkills("xiongmang").getTurnUseCard = function(self)
	local cards = self:addHandPile()
	self:sortByKeepValue(cards,nil,true)
   	local fs = dummyCard("slash")
	fs:setSkillName("xiongmang")
	local suits,n = {},0
   	for i,ep in sgs.list(self.enemies)do
		if self.player:canSlash(ep,fs)
		then n = n+1 end
	end
  	for _,c in sgs.list(cards)do
		if suits[c:getSuitString()]
		or fs:subcardsLength()>=n
		then continue end
		suits[c:getSuitString()]=true
		fs:addSubcard(c)
	end
	if fs:isAvailable(self.player)
	and fs:subcardsLength()>#self.enemies/2
 	then return fs end
end

sgs.ai_skill_use["@@xiongmang"] = function(self,prompt)
	local valid = {}
	local xiongmang_c = self.player:property("xiongmang"):toString()
	xiongmang_c = sgs.Card_Parse(xiongmang_c)
	local destlist = self.player:getAliveSiblings()
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist,"hp")
	for _,ep in sgs.list(destlist)do
		if #valid>=xiongmang_c:subcardsLength()-1 then break end
		if self:isEnemy(ep)
		and self.player:canSlash(ep,xiongmang_c)
		and not ep:hasFlag("xiongmang_target")
		then table.insert(valid,ep:objectName()) end
	end
	for _,ep in sgs.list(destlist)do
		if #valid>=xiongmang_c:subcardsLength()-1 then break end
		if table.contains(valid,ep:objectName()) then continue end
		if not self:isFriend(ep)
		and self.player:canSlash(ep,xiongmang_c)
		and not ep:hasFlag("xiongmang_target")
		then table.insert(valid,ep:objectName()) end
	end
	if #valid>0
	then
    	return string.format("@XiongmangCard=.->%s",table.concat(valid,"+"))
	end
end

sgs.ai_skill_use["@@jianliang"] = function(self,prompt)
	local valid = {}
	local destlist = self.player:getAliveSiblings()
	destlist:append(self.player)
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist,"hp")
	for _,ep in sgs.list(destlist)do
		if #valid>1 then break end
		if self:isFriend(ep) then table.insert(valid,ep:objectName()) end
	end
	for _,ep in sgs.list(destlist)do
		if #valid>1 then break end
		if table.contains(valid,ep:objectName()) then continue end
		if not self:isEnemy(ep) then table.insert(valid,ep:objectName()) end
	end
	if #valid>0
	then
    	return string.format("@JianliangCard=.->%s",table.concat(valid,"+"))
	end
end

addAiSkills("weimeng").getTurnUseCard = function(self)
	if self.player:getHp()>0
	then
		return sgs.Card_Parse("@WeimengCard=.")
	end
end

sgs.ai_skill_use_func["WeimengCard"] = function(card,use,self)
	local destlist = self.room:getOtherPlayers(self.player)
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
   	for i,ep in sgs.list(destlist)do
		if ep:getHandcardNum()>=self.player:getHp()
		and self:isEnemy(ep)
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
   	for i,ep in sgs.list(destlist)do
		if ep:getHandcardNum()>=self.player:getHp()
		and not self:isFriend(ep)
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
   	for i,ep in sgs.list(destlist)do
		if self:isFriend(ep)
		and ep:getHandcardNum()>0
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.WeimengCard = 9.4
sgs.ai_use_priority.WeimengCard = 5.8

sgs.ai_skill_invoke.yusui = function(self,data)
	local target = data:toPlayer()
	if target and self:isEnemy(target)
	and not self:isWeak()
	then
		return target:getHp()>self.player:getHp()
		or target:getHandcardNum()-self.player:getHandcardNum()>1
	end
end

sgs.ai_skill_choice.yusui = function(self,choices,data)
	local target = data:toPlayer()
	local items = choices:split("+")
	if ((target:getHp()-self.player:getHp())*2)>(target:getHandcardNum()-self.player:getHandcardNum())
	then return items[2] end
	return items[1]
end

addAiSkills("boyan").getTurnUseCard = function(self)
	return sgs.Card_Parse("@BoyanCard=.")
end

sgs.ai_skill_use_func["BoyanCard"] = function(card,use,self)
	local destlist = self.room:getOtherPlayers(self.player)
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
	self:sort(destlist,"handcard")
   	for i,ep in sgs.list(destlist)do
		if self:isFriend(ep)
		and ep:getHandcardNum()<5
		and ep:getHandcardNum()<ep:getMaxHp()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
   	for i,ep in sgs.list(destlist)do
		if (ep:getHandcardNum()>=ep:getMaxHp() or ep:getHandcardNum()>=5)
		and self:isEnemy(ep)
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
   	for i,ep in sgs.list(destlist)do
		if ep:getHandcardNum()<5
		and ep:getHandcardNum()<ep:getMaxHp()
		and not self:isEnemy(ep)
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.BoyanCard = 9.4
sgs.ai_use_priority.BoyanCard = 6.8

addAiSkills("juesheng").getTurnUseCard = function(self)
   	local fs = dummyCard("duel")
	fs:setSkillName("juesheng")
	local d = self:aiUseCard(fs)
	if d.card and d.to
	and fs:isAvailable(self.player)
	then
		for _,ep in sgs.list(d.to)do
			local i = ep:property("JueshengSlashNum"):toInt()
			if i>=ep:getHp() then return fs end
		end
	end
end

sgs.ai_skill_choice.zengou = function(self,choices,data)
	local items = choices:split("+")
	local target = self.room:findPlayerByObjectName(items[1]:split("=")[2])
	if target and self:isEnemy(target) then
		if items[1]:startsWith("lose") and self:isWeak() then return "cancel" end
		if items[2]:startsWith("lose") and self:isWeak() then return items[1] end
		return items[1]
	end
	return "cancel"
end

sgs.ai_skill_invoke.zengou = function(self,data)
	local use = data:toCardUse()
	if use.from and self:isEnemy(use.from) then
		return not self:isWeak() or sgs.ai_skill_cardask["@zengou-discard"](self,data)~="."
	end
	local use = data:toCardResponse()
	if use.m_who and self:isFriend(use.m_who) then
		return not self:isWeak() or sgs.ai_skill_cardask["@zengou-discard"](self,data)~="."
	end
end

sgs.ai_skill_cardask["@zengou-discard"] = function(self,data)
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if self.player:isJilei(h)
		and h:getTypeId()==1 then continue end
		return h:getEffectiveId()
	end
    return "."
end

sgs.ai_skill_invoke.zhangji = function(self,data)
	local items = data:toString():split(":")
   	local target = self.room:getCurrent()
	if items[1]=="draw"
	then
		return not self:isEnemy(target)
	else
		return not self:isFriend(target)
	end
end

sgs.ai_skill_invoke.changji = function(self,data)
	return sgs.ai_skill_invoke.zhangji(self,data)
end

sgs.ai_skill_discard.shejian = function(self)
	local cards = {}
   	local target = self.room:getCurrent()
    local handcards = sgs.QList2Table(self.player:getCards("he"))
    self:sortByKeepValue(handcards) -- 按保留值排序
   	for _,h in sgs.list(handcards)do
		if #cards>1 then break end
		if self:isEnemy(target)
		and self:isWeak(target)
		then
			table.insert(cards,h:getEffectiveId())
		end
	end
	return #cards>1 and cards
end

sgs.ai_skill_choice.shejian = function(self,choices,data)
	local items = choices:split("+")
	if string.startsWith(items[1],"damage")
	then return items[2] end
	if string.startsWith(items[2],"damage")
	then return items[2] end
	return items[1]
end

sgs.ai_skill_playerchosen.jinhuaiyuan = function(self,players)
	local destlist = self:sort(players,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_choice.jinhuaiyuan = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if target:getAttackRange()<3
	then return items[2] end
	if target:getMaxCards()<5
	then return items[1] end
	return items[3]
end

addAiSkills("jinchongxin").getTurnUseCard = function(self)
	if self.player:getCardCount()>1
	then
		return sgs.Card_Parse("@JinChongxinCard=.")
	end
end

sgs.ai_skill_use_func["JinChongxinCard"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()==1
		and self.player:inMyAttackRange(ep)
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getHandcardNum()>1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.room:getOtherPlayers(self.player))do
		if ep:getHandcardNum()>0
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.JinChongxinCard = 3.4
sgs.ai_use_priority.JinChongxinCard = 4.8

sgs.ai_skill_cardask["@jinchongxin-recast"] = function(self,data)
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if self.player:isCardLimited(h,sgs.Card_MethodRecast,self.player:getHandcards():contains(h))
    	then continue end
		return h:getEffectiveId()
	end
    return "."
end

sgs.ai_skill_playerchosen.jinweishu = function(self,players)
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

sgs.ai_skill_playerchosen.jinweishu_dis = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and self:doDisCard(target,"e")
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:doDisCard(target,"he")
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and self:doDisCard(target,"he")
		then return target end
	end
	return destlist[1]
end

addAiSkills("channi").getTurnUseCard = function(self)
	local toids = {}
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		if #toids>#cards/2 then break end
		table.insert(toids,c:getEffectiveId())
	end
	if #toids<1 then return end
	return sgs.Card_Parse("@ChanniCard="..table.concat(toids,"+"))
end

sgs.ai_skill_use_func["ChanniCard"] = function(card,use,self)
	self:sort(self.friends_noself,"handcard",true)
	for _,fp in sgs.list(self.friends_noself)do
		local c = dummyCard("duel")
		c:setSkillName("_channi")
		if c:isAvailable(fp)
		then
			use.card = card
			use.to:append(fp)
			return
		end
	end
end

sgs.ai_use_value.ChanniCard = 2.4
sgs.ai_use_priority.ChanniCard = 0.8

sgs.ai_skill_use["@@channi"] = function(self,prompt)
    local n = self.player:getMark("channi_mark-Clear")
	c = dummyCard("duel")
	c:setSkillName("_channi")
    local cards = self.player:getCards("h")
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if c:subcardsLength()>=#cards/2
		or c:subcardsLength()>=n
		then break end
		if h:isKindOf("Slash")
		and math.random()>0.4
		then continue end
		c:addSubcard(h)
	end
    local dummy = self:aiUseCard(c)
   	if dummy.card
   	and dummy.to
   	then
      	local tos = {}
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
       	return c:toString().."->"..table.concat(tos,"+")
    end
end

sgs.ai_skill_invoke.tiqi = function(self,data)
    return true
end

sgs.ai_skill_choice.tiqi = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if self:isFriend(target)
	then return items[1] end
	return items[2]
end

sgs.ai_skill_use["@@baoshu"] = function(self,prompt)
	local valid = {}
	local destlist = self.player:getAliveSiblings()
	destlist:append(self.player)
    destlist = self:sort(destlist,"hp")
	for _,friend in sgs.list(destlist)do
		if #valid>=self.player:getMaxHp() then break end
		if self:isFriend(friend)
		then table.insert(valid,friend:objectName()) end
	end
	if #valid>0
	then
    	return string.format("@BaoshuCard=.->%s",table.concat(valid,"+"))
	end
end

sgs.ai_skill_invoke.tianyun = function(self,data)
    return true
end

sgs.ai_skill_playerchosen.tianyun = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_playerchosen.yuyan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and #self.enemies>#self.friends
		then return target end
		if self:isEnemy(target)
		and #self.enemies<#self.friends
		then return target end
	end
	return destlist[1]
end

sgs.ai_skill_invoke.bingjie = function(self,data)
    local n = 0
    for _,c in sgs.list(self:getTurnUse())do
		if c:getTypeId()==1
		or c:getTypeId()==2
		then n = n+1 end
	end
	return n>1 and self.player:isWounded()
end

sgs.ai_skill_invoke.qibie = function(self,data)
    return self.player:getHandcardNum()>4
	or self.player:isWounded()
end

addAiSkills("yijiao").getTurnUseCard = function(self)
	return sgs.Card_Parse("@YijiaoCard=.")
end

sgs.ai_skill_use_func["YijiaoCard"] = function(card,use,self)
	self:sort(self.friends_noself,"handcard",true)
	sgs.ai_skill_choice.yijiao = ""..math.random(1,2)
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getHandcardNum()>0
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	sgs.ai_skill_choice.yijiao = ""..math.random(3,4)
	self:sort(self.enemies,"handcard")
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>0
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	local tos = self.room:getOtherPlayers(self.player)
	tos = self:sort(tos,"handcard",true)
	sgs.ai_skill_choice.yijiao = ""..math.random(1,4)
	for _,ep in sgs.list(tos)do
		if ep:getHandcardNum()>2
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.YijiaoCard = 1.4
sgs.ai_use_priority.YijiaoCard = 1.8

sgs.ai_skill_use["@@xunli2!"] = function(self,prompt)
	local valid = {}
	local jpxlli = self.player:getTag("xunliForAI"):toIntList()
	jpxlli = ListI2C(jpxlli)
	jpxlli = self:sortByUseValue(jpxlli)
	local put = 9-self.player:getPile("jpxlli"):length()
	for _,c in sgs.list(jpxlli)do
		if #valid>=put then break end
		if c:isAvailable(self.player)
		then
			table.insert(valid,c:getEffectiveId())
		end
	end
	for _,c in sgs.list(jpxlli)do
		if #valid>=put then break end
		if table.contains(valid,c:getEffectiveId())
		then continue end
		table.insert(valid,c:getEffectiveId())
	end
	return #valid>0 and string.format("@XunliPutCard=%s",table.concat(valid,"+"))
end

sgs.ai_skill_use["@@xunli1"] = function(self,prompt)
	local valid = {}
    local cards = self.player:getCards("h")
	local jpxlli = self.player:getPile("jpxlli")
	jpxlli = self:sortByUseValue(ListI2C(jpxlli))
	for _,h in sgs.list(self:sortByKeepValue(cards))do
		if not h:isBlack() then continue end
		for i,c in sgs.list(jpxlli)do
			if self:aiUseCard(c).card then
				if self:aiUseCard(h).card then
					if self:getUseValue(h)<self:getUseValue(c) then
						table.insert(valid,h:getEffectiveId())
						table.insert(valid,c:getEffectiveId())
						table.remove(jpxlli,i)
						break
					end
				else
					table.insert(valid,h:getEffectiveId())
					table.insert(valid,c:getEffectiveId())
					table.remove(jpxlli,i)
					break
				end
			end
		end
	end
	return #valid>1 and string.format("@XunliCard=%s",table.concat(valid,"+"))
end

sgs.ai_skill_playerchosen.zhishi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_use["@@zhishi"] = function(self,prompt)
	local valid = {}
	local jpxlli = self.player:getPile("jpxlli")
	jpxlli = ListI2C(jpxlli)
	jpxlli = self:sortByUseValue(jpxlli)
	local to = self.player:getTag("ZhishiTarget"):toPlayer()
	for _,h in sgs.list(jpxlli)do
		if to:getHandcardNum()+#valid>3 or self:isEnemy(to) then continue end
		table.insert(valid,h:getEffectiveId())
	end
	return #valid>0 and string.format("@ZhishiCard=%s",table.concat(valid,"+"))
end

addAiSkills("lieyi").getTurnUseCard = function(self)
	if #self.toUse<2
	then
		return sgs.Card_Parse("@LieyiCard=.")
	end
end

sgs.ai_skill_use_func["LieyiCard"] = function(card,use,self)
	local jpxlli = self.player:getPile("jpxlli")
	jpxlli = ListI2C(jpxlli)
	self:sort(self.enemies,"handcard")
	for _,ep in sgs.list(self.enemies)do
		local n = 0
		for _,c in sgs.list(jpxlli)do
			local d = self:aiUseCard(c)
			if d.card and d.to:contains(ep)
			then n = n+1 end
		end
		if n>jpxlli:length()/2
		or n>ep:getHp()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	local tos = self.room:getOtherPlayers(self.player)
	tos = self:sort(tos,"handcard")
	for _,ep in sgs.list(tos)do
		local n = 0
		for _,c in sgs.list(jpxlli)do
			local d = self:aiUseCard(c)
			if d.card and d.to:contains(ep)
			then n = n+1 end
		end
		if n>jpxlli:length()/2
		or n>ep:getHp()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.LieyiCard = 1.4
sgs.ai_use_priority.LieyiCard = 0.8

addAiSkills("manwang").getTurnUseCard = function(self)
	local toids = {}
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,true)
	local n = 4-self.player:getMark("manwang_remove_last")
  	for _,c in sgs.list(cards)do
		if #toids>=#cards/2 or #toids>=n then break end
		if self:getUseValue(c)<3.5 then
			table.insert(toids,c:getEffectiveId())
		end
	end
	if #toids<1 or #cards<3 then return end
	if self.player:hasSkill("panqin") and #toids<2 then return end
	if self:isWeak() and #toids<3 then return end
	return sgs.Card_Parse("@ManwangCard="..table.concat(toids,"+"))
end

sgs.ai_skill_use_func["ManwangCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.ManwangCard = 2.4
sgs.ai_use_priority.ManwangCard = 1.8

sgs.ai_skill_invoke.panqin = function(self,data)
    local dc = dummyCard("SavageAssault")
	dc:setSkillName("panqin")
    for _,id in sgs.list(self.player:getTag("PanqinRecord"):toIntList())do
		if self.room:getCardPlace(id)==sgs.Player_DiscardPile
		then dc:addSubcard(id) end
	end
	return self:aiUseCard(dc).card
end

sgs.ai_skill_invoke.jinjian = function(self,data)
	local invoke = data:toString()
	local damage = self.player:getTag("JinjianDamage"):toDamage()
	if invoke=="add" then
		return self:isEnemy(damage.to)
		or not self:isFriend(damage.to) and not self:isWeak(damage.to)
	end
    return self:isFriend(damage.to)
	or self:isWeak(damage.to) and not self:isEnemy(damage.to)
end

sgs.ai_guhuo_card.dunshi = function(self,toname,class_name)
	if class_name=="Slash" then toname = "slash" end
   	local target = self.room:getCurrent()
	if self:getCardsNum(class_name)<1 or self:isFriend(target)
	then return "@DunshiCard=.:"..toname end
end

addAiSkills("dunshi").getTurnUseCard = function(self)
 	for _,pn in sgs.list({"slash","peach","analeptic"})do
		if self.player:getMark("dunshi_used_"..pn)>0
		or pn=="slash" and self.player:getLostHp()<1
		then continue end
		local dc = dummyCard(pn)
		if dc and self:getCardsNum(dc:getClassName())<1 then
			dc:setSkillName("dunshi")
			if dc:isAvailable(self.player) then
				local dummy = self:aiUseCard(dc)
				if dummy.card and dummy.to
				then
					self.dunshi_to = dummy.to
					sgs.ai_use_priority.DunshiCard = sgs.ai_use_priority[dc:getClassName()]
					return sgs.Card_Parse("@DunshiCard=.:"..pn)
				end
			end
		end
	end
end

sgs.ai_skill_use_func["DunshiCard"] = function(card,use,self)
	if self.dunshi_to
	then
		use.card = card
		use.to = self.dunshi_to
	end
end

sgs.ai_use_value.DunshiCard = 2.4
sgs.ai_use_priority.DunshiCard = 6.8

sgs.ai_skill_choice.dunshi = function(self,choices,data)
	local items = choices:split("+")
	local damage = data:toDamage()
	if self:isFriend(self.room:getCurrent())
	then
		if self.player:isWounded() or self.player:getMaxHp()>5 then return items[1] end
	end
	if #items>2
	then
		if self.player:isWounded() or self.player:getMaxHp()>5 then return items[2] end
		if self:isFriend(damage.to) and self:isWeak(damage.to)
		or self:isFriend(damage.from) then return items[1] end
	else
		local cn = items[2]:split("=")[2]
		if cn=="slash" or cn=="analeptic"
		then return items[2] end
	end
	return items[1]
end

sgs.ai_skill_choice.dunshi_chooseskill = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if self:isFriend(target)
	then
		if table.contains(items,"renzheng")
		then return "renzheng" end
		if table.contains(items,"lilu")
		then return "lilu" end
		if table.contains(items,"zhici")
		then return "zhici" end
	end
end

sgs.ai_skill_invoke.chenjian = function(self,data)
    return true
end

sgs.ai_skill_use["@@chenjian1"] = function(self,prompt)
	local valid = sgs.ai_skill_use["@@chenjian3"](self,prompt)
	if valid then return valid end
	return sgs.ai_skill_use["@@chenjian2"](self,prompt)
end

sgs.ai_skill_use["@@chenjian2"] = function(self,prompt)
	local jpxlli = self.player:getTag("chenjianForAI"):toIntList()
	jpxlli = ListI2C(jpxlli)
	local suits = {}
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
	for _,c in sgs.list(jpxlli)do
		local s = c:getSuitString()
		if suits[s] then suits[s] = suits[s]+1
		else suits[s] = 1 end
	end
	local func = function(a,b)
		return a>b
	end
	table.sort(suits,func)
	self:sort(self.friends,"handcard")
	for _,c in sgs.list(cards)do
		local s = c:getSuitString()
		if suits[s] and suits[s]>0 then
			return "@ChenjianCard="..c:getEffectiveId().."->"..self.friends[1]:objectName()
		end
	end
end

sgs.ai_skill_use["@@chenjian3"] = function(self,prompt)
	local valid = {}
	local jpxlli = self.player:getTag("chenjianForAI"):toIntList()
	jpxlli = ListI2C(jpxlli)
	jpxlli = self:sortByKeepValue(jpxlli,true)
	for _,c in sgs.list(jpxlli)do
		if c:isAvailable(self.player) then
			local d = self:aiUseCard(c)
			if d.card and d.to then
				if c:canRecast() and d.to:length()<1
				then continue end
				for _,to in sgs.list(d.to)do
					table.insert(valid,to:objectName())
				end
				return c:toString().."->"..table.concat(valid,"+")
			end
		end
	end
end

addAiSkills("yuanyu").getTurnUseCard = function(self)
	return sgs.Card_Parse("@YuanyuCard=.")
end

sgs.ai_skill_use_func["YuanyuCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard",true)
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>0
		and ep:getMark("&yuanyu+#"..self.player:objectName())<1
		then
			use.card = card
			return
		end
	end
	local tos = self.room:getOtherPlayers(self.player)
	tos = self:sort(tos,"handcard",true)
	for _,ep in sgs.list(tos)do
		if ep:getHandcardNum()>0
		and not self:isFriend(ep)
		then
			use.card = card
			return
		end
	end
end

sgs.ai_use_value.YuanyuCard = 3.4
sgs.ai_use_priority.YuanyuCard = 3.8

sgs.ai_skill_playerchosen.yuanyu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and target:getMark("&yuanyu+#"..self.player:objectName())<1
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and target:getMark("&yuanyu+#"..self.player:objectName())<1
		then return target end
	end
	return destlist[1]
end

sgs.ai_skill_playerchosen.jinzhefu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
	return destlist[1]
end

sgs.ai_skill_discard.jinzhefu = function(self,max,min,optional,include_equip,pattern)
	local to_cards = {}
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
   	for _,h in sgs.list(cards)do
   		if #to_cards>=min then break end
		if self:getKeepValue(h)<6
		and sgs.Sanguosha:matchExpPattern(pattern,self.player,h)
		then table.insert(to_cards,h:getEffectiveId()) end
	end
	return to_cards
end


sgs.ai_skill_invoke.jinyidu = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return self:isEnemy(target)
		or not self:isFriend(target) and not self:isWeak(target)
	end
end

sgs.ai_skill_invoke.xingchong = function(self,data)
    return true
end

sgs.ai_skill_choice.xingchong = function(self,choices,data)
	local items = choices:split("+")
	if #items>1 then return items[math.random(2,#items)] end
	return items[1]
end

sgs.ai_skill_discard.xingchong = function(self,max,min)
	local to_cards = {}
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	local uses = self:getTurnUse()
   	for _,h in sgs.list(cards)do
   		if #to_cards>=max then break end
		if table.contains(uses,h)
		then
         	table.insert(to_cards,h:getEffectiveId())
		end
	end
	return to_cards
end

sgs.ai_skill_playerschosen.lianzhou = function(self,players,n,x)
	local tos = {}
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
    for _,target in sgs.list(destlist)do
		if #tos>=x then break end
		if self:isEnemy(target)
		then table.insert(tos,target) end
	end
    for _,target in sgs.list(destlist)do
		if #tos>1 then break end
		if not self:isFriend(target)
		then table.insert(tos,target) end
	end
	return tos
end

sgs.ai_skill_invoke.choutao = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isFriend(target) and self:getCardsNum("Jink")<1
		or self:isFriend(target) and (self:doDisCard(target,"e") or target==self.player and target:getCardCount()>3)
	end
end

sgs.ai_skill_playerchosen.xiangshu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and self:isWeak(target)
		and self.player:getMark("damage_point_round")>=target:getLostHp()
		then return target end
	end
end

sgs.ai_skill_invoke.zhubi = function(self,data)
   	local target = self.room:getCurrent()
	if target:getPhase()<sgs.Player_Play
	then return self:isFriend(target) end
	target = target:getNextAlive()
	return self:isFriend(target)
end

sgs.ai_skill_playerchosen.guili = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp",true)
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

sgs.ai_skill_playerchosen.caiyi = function(self,players)
	local n = self.player:getChangeSkillState("caiyi")
	self.caiyi_from = self.player
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	if n>1
	then
		for _,target in sgs.list(destlist)do
			if self:isEnemy(target)
			and (self:isWeak(target) or self:isWeak())
			then return target end
		end
	else
		local removes = self.player:property("SkillDescriptionRecord_caiyi"):toString():split("+")
		n = 4-#removes
		for _,target in sgs.list(destlist)do
			if self:isFriend(target)
			then
				if not table.contains(removes,"caiyi_recover") and (target:getLostHp()>=n or self:isWeak(target) and n>0)
				or not table.contains(removes,"caiyi_draw") and target:getHandcardNum()+n<5
				or not table.contains(removes,"caiyi_fuyuan") and not target:faceUp()
				or not table.contains(removes,"caiyi_random1") and self:isWeak(target)
				then return target end
			end
		end
	end
end

sgs.ai_skill_choice.caiyi = function(self,choices,data)
	local caiyi_from = self.caiyi_from or self.room:getCurrent()
	local n = caiyi_from:getChangeSkillState("caiyi")
	if n>1 then n = 4-#caiyi_from:property("SkillDescriptionChoiceRecord1_caiyi"):toString():split("+")
	else n = 4-#caiyi_from:property("SkillDescriptionRecord_caiyi"):toString():split("+") end
	local items = choices:split("+")
	for _,cho in sgs.list(items)do
		if cho:startsWith("recover")
		and (self.player:getLostHp()>=n or self:isWeak() and n>0)
		then return cho end
		if cho:startsWith("draw")
		and self.player:getHandcardNum()+n<5
		then return cho end
		if cho:startsWith("fuyuan")
		and not self.player:faceUp()
		then return cho end
		if cho:startsWith("discard")
		and self.player:getCardCount()-n>1
		then return cho end
		if cho:startsWith("damage")
		and self.player:getHp()-n>1
		then return cho end
	end
end

addAiSkills("shengong").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		if c:isKindOf("EquipCard") then
			local i = c:getRealCard():toEquipCard():location()
			if(self.player:getMark(i.."shengong-PlayClear")>0)then continue end
			for _,p in sgs.list(self.friends)do
				for _,pc in sgs.list(self:poisonCards("e",p))do
					local l = pc:getRealCard():toEquipCard():location()
					if(i==l)then
						self.shengongTo = p
						return sgs.Card_Parse("@ShengongCard="..c:getId())
					end
				end
			end
			for _,p in sgs.list(self.friends)do
				if(p:hasEquipArea(i) and not p:getEquip(i))then
					self.shengongTo = p
					return sgs.Card_Parse("@ShengongCard="..c:getId())
				end
			end
		end
	end
end

sgs.ai_skill_use_func["ShengongCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.ShengongCard = 2.4
sgs.ai_use_priority.ShengongCard = 5.8

sgs.ai_skill_playerchosen.shengong = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
    for _,target in sgs.list(destlist)do
		if self.shengongTo==target
		then return target end
	end
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_choice.shengong = function(self,choices,data)
	local to = data:toPlayer()
	if(self:isFriend(to)) then
		return "shengong1"
	end
	if(self:isEnemy(to)) then
		return "shengong2"
	end
end

sgs.ai_skill_invoke.chishi = function(self,data)
   	local target = data:toPlayer()
	return target and self:isFriend(target) and self:canDraw(target)
end

addAiSkills("weimian").getTurnUseCard = function(self)
	self:sort(self.friends)
	for _,p in sgs.list(self.friends)do
		if self:isWeak(p) or p:getHandcardNum()<2 and self:canDraw(p) then
			self.weimianTo = p
			return sgs.Card_Parse("@WeimianCard=.")
		end
	end
end

sgs.ai_skill_use_func["WeimianCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.WeimianCard = 2.4
sgs.ai_use_priority.WeimianCard = 5.8

sgs.ai_skill_choice.weimian = function(self,choices)
	local items = choices:split("+")
	if table.contains(items,"cancel")
	then return "cancel" end
	for _,t in sgs.list(items)do
		local e = self.player:getEquip(t)
		if e and #self:poisonCards({e})>0 then
			return t
		end
	end
	for _,t in sgs.list(sgs.reverse(items))do
		local e = self.player:getEquip(t)
		if e==nil then
			return t
		end
	end
	return items[#items]
end

sgs.ai_skill_playerchosen.weimian = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
    for _,target in sgs.list(destlist)do
		if self.weimianTo==target
		then return target end
	end
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_choice.weimian_target = function(self,choices)
	local items = choices:split("+")
	if table.contains(items,"weimian3")
	and self.player:getHandcardNum()<2 and self:canDraw()
	then return "weimian3" end
	if table.contains(items,"weimian2")
	and self:isWeak()
	then return "weimian2" end
	return items[1]
end

sgs.ai_skill_choice.qingliu = function(self,choices)
	local items = choices:split("+")
	for _,p in sgs.list(self.friends_noself)do
		if table.contains(items,p:getKingdom())
		then return p:getKingdom() end
	end
end

sgs.ai_skill_playerchosen.yongzu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for i,target in sgs.list(destlist)do
		if i<=#destlist/2 and self:isFriend(target)
		and not target:faceUp()
		then return target end
	end
    for i,target in sgs.list(destlist)do
		if i<=#destlist/2 and self:isFriend(target)
		and target:getKingdom()==self.player:getKingdom()
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
end

sgs.ai_skill_choice.yongzu = function(self,choices)
	local items = choices:split("+")
	if table.contains(items,"yongzu3")
	and not self.player:faceUp()
	then return "yongzu3" end
	if table.contains(items,"yongzu2")
	and self:isWeak()
	then return "yongzu2" end
	if table.contains(items,"yongzu1")
	and self.player:getHandcardNum()<2 and self:canDraw()
	then return "yongzu1" end
	if table.contains(items,"yongzu4")
	and self:getOverflow()>1
	then return "yongzu4" end
	return items[#items]
end

addAiSkills("kouchao").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	local cns = self.player:property("KouchaoCNS"):toString():split("+");
	if #cns<2 then cns = {"slash","fire_attack","dismantlement"} end
  	for _,c in sgs.list(cards)do
		local n = 0
		for _,cn in sgs.list(cns)do
			n = n+1
			if self.player:getMark(cn..n.."KouchaoUse_lun")>0
			or self.player:getMark(n.."KouchaoNum_lun")>0
			then continue end
			local dc = dummyCard(cn)
			dc:setSkillName("kouchao")
			dc:addSubcard(c)
			if dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				if d.card then
					if dc:canRecast() and d.to:length()<1 then continue end
					self.kouchaoUse = d
					sgs.ai_skill_choice.kouchao = cn
					sgs.ai_use_priority.KouchaoCard = sgs.ai_use_priority[dc:getClassName()]
					return sgs.Card_Parse("@KouchaoCard=.")
				end
			end
		end
	end
end

sgs.ai_skill_use_func["KouchaoCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.KouchaoCard = 2.4
sgs.ai_use_priority.KouchaoCard = 5.8

sgs.ai_skill_use["@@kouchao"] = function(self,prompt)
	local valid = {}
	local d = self.kouchaoUse
	if d.card then
		for _,to in sgs.list(d.to)do
			table.insert(valid,to:objectName())
		end
		return d.card:toString().."->"..table.concat(valid,"+")
	end
end

sgs.ai_guhuo_card.kouchao = function(self,toname,class_name)
	if self:getCardsNum(class_name)<1 then
		local cards = self.player:getCards("he")
		cards = self:sortByKeepValue(cards)
		local cns = self.player:property("KouchaoCNS"):toString():split("+");
		if #cns<2 then cns = {"slash","fire_attack","dismantlement"} end
		for _,c in sgs.list(cards)do
			local n = 0
			for _,cn in sgs.list(cns)do
				n = n+1
				if self.player:getMark(cn..n.."KouchaoUse_lun")>0
				or self.player:getMark(n.."KouchaoNum_lun")>0
				or toname~=cn then continue end
				local dc = dummyCard(cn)
				dc:setSkillName("kouchao")
				dc:addSubcard(c)
				return dc:toString()
			end
		end
	end
end

addAiSkills("hunjiang").getTurnUseCard = function(self)
	return sgs.Card_Parse("@HunjiangCard=.")
end

sgs.ai_skill_use_func["HunjiangCard"] = function(card,use,self)
  	for _,p in sgs.list(self.room:getAllPlayers())do
		if self.player:inMyAttackRange(p) then
			use.card = card
			use.to:append(p)
		end
	end
end

sgs.ai_use_value.HunjiangCard = 2.4
sgs.ai_use_priority.HunjiangCard = 5.8

sgs.ai_skill_choice.hunjiang = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	if self:isFriend(to) or self:isWeak() and to:canSlash(self.player)
	and getCardsNum("Slash",to,self.player)>0 and self:isEnemy(to)
	then return items[2] end
	if self:isEnemy(to)
	then return items[1] end
end

sgs.ai_skill_playerschosen.hunjiang = function(self,players,x)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local tos = {}
	local use = self.player:getTag("hunjiangUse"):toCardUse()
    for _,p in sgs.list(destlist)do
		if #tos<x and not self:isFriend(p) and self:slashIsEffective(use.card,p)
		then table.insert(tos,p) end
	end
    return tos
end

addAiSkills("jinlan").getTurnUseCard = function(self)
	return sgs.Card_Parse("@JinlanCard=.")
end

sgs.ai_skill_use_func["JinlanCard"] = function(card,use,self)
  	for _,p in sgs.list(self.room:getAllPlayers())do
		local n = 0
		for _,s in sgs.list(p:getVisibleSkillList())do
			if not s:isAttachedLordSkill() then n = n+1 end
		end
		if n>self.player:getHandcardNum() then
			use.card = card
			break
		end
	end
end

sgs.ai_use_value.HunjiangCard = 6.4
sgs.ai_use_priority.HunjiangCard = 2.8

sgs.ai_skill_choice.jianman = function(self,choices)
	local items = choices:split("+")
  	for _,cn in sgs.list(items)do
		local dc = dummyCard(cn)
		dc:setSkillName("_jianman")
		if not self.player:isLocked(dc) then
			local d = self:aiUseCard(dc)
			if d.card then
				self.jianmanUse = d
				return cn
			end
		end
	end
end

sgs.ai_skill_use["@@jianman"] = function(self,prompt)
	local valid = {}
	local d = self.jianmanUse
	if d.card then
		for _,to in sgs.list(d.to)do
			table.insert(valid,to:objectName())
		end
		return d.card:toString().."->"..table.concat(valid,"+")
	end
end

sgs.ai_skill_playerschosen.liwen = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
	local tos = {}
    for _,p in sgs.list(destlist)do
		if #tos<=self.friends/2 and self:isFriend(p) and p:getHandcardNum()>1
		then table.insert(tos,p) end
	end
    return tos
end

sgs.ai_skill_choice.zhengyi = function(self,choices)
	local items = choices:split("+")
	local n = 0
	local destlist = sgs.QList2Table(self.room:getAlivePlayers()) -- 将列表转换为表
  	for _,p in sgs.list(destlist)do
		if p:getMark("&xianLW")>0 and p:getHp()>n then
			n = p:getHp()
		end
	end
	self:sort(destlist,nil,true)
  	for _,p in sgs.list(destlist)do
		if p:getMark("&xianLW")>0 and p:getHp()>=n then
			if p==self.player then
				return "yes"
			end
			n = 999
		end
	end
	return "no"
end

sgs.ai_skill_invoke.hongtu = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_use["@@hongtu!"] = function(self,prompt)
	local ids = {}
	self:sort(self.friends_noself,nil,true)
	local cards = self.player:getHandcards()
	cards = self:sortByKeepValue(cards)
	for _,p in sgs.list(self.friends_noself)do
		for _,h in sgs.list(sgs.reverse(cards))do
			if #ids<2 and h:isAvailable(p) then
				table.insert(ids,h:getId())
			end
		end
		for _,h in sgs.list(cards)do
			if #ids<3 and not table.contains(ids,h:getId()) then
				table.insert(ids,h:getId())
			end
		end
		return "@HongtuCard="..table.concat(ids,"+").."->"..p:objectName()
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if self:isEnemy(p) then continue end
		for _,h in sgs.list(sgs.reverse(cards))do
			if #ids<2 and h:isAvailable(p) then
				table.insert(ids,h:getId())
			end
		end
		for _,h in sgs.list(cards)do
			if #ids<3 and not table.contains(ids,h:getId()) then
				table.insert(ids,h:getId())
			end
		end
		return "@HongtuCard="..table.concat(ids,"+").."->"..p:objectName()
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		for _,h in sgs.list(cards)do
			if #ids<3 then
				table.insert(ids,h:getId())
			end
		end
		return "@HongtuCard="..table.concat(ids,"+").."->"..p:objectName()
	end
end

sgs.ai_skill_use["@@hongtu"] = function(self,prompt)
	local cards = {}
	for _,id in sgs.list(self.player:getTag("hongtuForAI"):toIntList())do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
	cards = self:sortByUseValue(cards)
	for _,c in sgs.list(cards)do
		if c:isAvailable(self.player) then
			local d = self:aiUseCard(c)
			if d.card then
				if c:canRecast() and d.to:length()<1 then continue end
				local valid = {}
				for _,to in sgs.list(d.to)do
					table.insert(valid,to:objectName())
				end
				return c:toString().."->"..table.concat(valid,"+")
			end
		end
	end
end

sgs.ai_skill_discard.xiwu = function(self,max,min,optional,include_equip,pattern)
	local to_cards = {}
	if sgs.cardEffect and sgs.cardEffect.card and sgs.cardEffect.card:isDamageCard()
	and self:canDamageHp(sgs.cardEffect.from,sgs.cardEffect.card)
	then return to_cards end
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
   	for _,h in sgs.list(cards)do
   		if #to_cards>=min then break end
		if sgs.Sanguosha:matchExpPattern(pattern,self.player,h)
		then table.insert(to_cards,h:getEffectiveId()) end
	end
	return to_cards
end

sgs.ai_skill_invoke.dongdao = function(self,data)
	return self:canDraw()
end

addAiSkills("fushi").getTurnUseCard = function(self)
	local fss = self.player:getPile("fushi")
	local ids = {}
  	for _,id in sgs.list(fss)do
		table.insert(ids,id)
		if #ids>fss:length()/2 then
			return sgs.Card_Parse("@FushiCard="..table.concat(ids,"+"))
		end
	end
end

sgs.ai_skill_use_func["FushiCard"] = function(card,use,self)
  	local dc = dummyCard()
	dc:setSkillName("fushi")
	if dc:isAvailable(self.player) then
		local d = self:aiUseCard(dc)
		if d.card then
			use.card = card
			use.to = d.to
		end
	end
end

sgs.ai_use_value.FushiCard = 5.4
sgs.ai_use_priority.FushiCard = 2.8

function sgs.ai_cardsview.fushi(self,class_name,player)
	local fss = player:getPile("fushi")
	local ids = {}
  	for _,id in sgs.list(fss)do
		table.insert(ids,id)
		if #ids>fss:length()/2 then
			return "@FushiCard="..table.concat(ids,"+")
		end
	end
end

sgs.ai_skill_choice.fushi = function(self,choices,data)
	local items = choices:split("+")
	local use = data:toCardUse()
	if table.contains(items,"fushi1") then
		self:sort(self.enemies)
		local dc = dummyCard()
		self.fushi1To = nil
		for _,p in sgs.list(self.enemies)do
			if self.player:canSlash(p,dc) and self:slashIsEffective(dc,p) then
				self.fushi1To = p
				return "fushi1"
			end
		end
		for _,p in sgs.list(self.friends_noself)do
			if self.player:canSlash(p,dc) and not self:slashIsEffective(dc,p) then
				self.fushi1To = p
				return "fushi1"
			end
		end
	end
	if table.contains(items,"fushi2") then
		local dc = dummyCard()
		self.fushi2To = nil
		for _,p in sgs.list(self.friends_noself)do
			if use.to:contains(p) then
				self.fushi2To = p
				return "fushi2"
			end
		end
	end
	if table.contains(items,"fushi3") then
		self.fushi3To = nil
		for _,p in sgs.list(self.enemies)do
			if use.to:contains(p) then
				self.fushi3To = p
				return "fushi2"
			end
		end
		return "fushi3"
	end
	return items[#items]
end

sgs.ai_skill_playerchosen.fushi1 = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
    for _,target in sgs.list(destlist)do
		if self.fushi1To==target
		then return target end
	end
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_playerchosen.fushi2 = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
    for _,target in sgs.list(destlist)do
		if self.fushi2To==target
		then return target end
	end
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_playerchosen.fushi3 = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
    for _,target in sgs.list(destlist)do
		if self.fushi3To==target
		then return target end
	end
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

addAiSkills("zuolian").getTurnUseCard = function(self)
	return sgs.Card_Parse("@ZuolianCard=.")
end

sgs.ai_skill_use_func["ZuolianCard"] = function(card,use,self)
	self:sort(self.friends_noself,nil,true)
  	for _,p in sgs.list(self.friends_noself)do
		if use.to:length()<self.player:getHp() and p:getHandcardNum()>0 then
			use.card = card
			use.to:append(p)
		end
	end
end

sgs.ai_use_value.ZuolianCard = 2.4
sgs.ai_use_priority.ZuolianCard = 4.8

sgs.ai_skill_invoke.zuolian = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_playerschosen.jingzhou = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
	local tos = {}
    for _,p in sgs.list(destlist)do
		if #tos<self.player:getHp() and self:isFriend(p)
		and p:isChained() and p~=self.player
		then table.insert(tos,p) end
	end
	local damage = self.player:getTag("jingzhouDamage"):toDamage()
	if damage.nature~=sgs.DamageStruct_Normal then
		if #tos<self.player:getHp() and #self.enemies>0
		and not self.player:isChained()
		then table.insert(tos,self.player) end
		for _,p in sgs.list(sgs.reverse(destlist))do
			if #tos<self.player:getHp() and self:isEnemy(p)
			and not p:isChained()
			then table.insert(tos,p) end
		end
	else
		for _,p in sgs.list(sgs.reverse(destlist))do
			if #tos<self.player:getHp() and self:isEnemy(p)
			and not p:isChained()
			then table.insert(tos,p) end
		end
	end
    return tos
end

sgs.ai_skill_playerschosen.pijing = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
	local tos = {}
	local n = math.max(1,self.player:getLostHp())
	local use = self.player:getTag("pijingUse"):toCardUse()
    for _,p in sgs.list(destlist)do
		if #tos<n and self:isFriend(p) and use.to:contains(p)
		and use.card:isDamageCard() and not self:canDamageHp(use.from,use.card)
		then table.insert(tos,p) end
	end
    for _,p in sgs.list(destlist)do
		if #tos<n and self:isEnemy(p) and not use.to:contains(p)
		and (use.card:isDamageCard() or not self:isFriend(use.to:at(0)))
		and self:hasTrickEffective(use.card,p)
		then table.insert(tos,p) end
	end
    return tos
end

sgs.ai_skill_choice.pijing = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	if table.contains(items,"pijing2") and self:isFriend(to) then
		return "pijing2"
	end
	return items[1]
end


addAiSkills("weifu").getTurnUseCard = function(self)
	if self:getOverflow()<1 then return end
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		if self.player:isJilei(c) then continue end
		return sgs.Card_Parse("@WeifuCard="..c:getId())
	end
end

sgs.ai_skill_use_func["WeifuCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.WeifuCard = 2.4
sgs.ai_use_priority.WeifuCard = 1.8

sgs.ai_skill_playerchosen.kuansai = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and self:isWeak()
		then return target end
	end
	return destlist[1]
end

sgs.ai_skill_choice.kuansai = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	if self:isFriend(to) and to:isWounded() then
		return items[2]
	end
	return items[1]
end

sgs.ai_skill_playerchosen.lianju = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and self:canDraw()
		then return target end
	end
end

sgs.ai_skill_askforag.changxin = function(self,card_ids)
    local damage = self.player:getTag("changxinDamage"):toDamage()
	if #card_ids>2 then
		self.changxin_damage = damage.damage
	end
	for _,id in sgs.list(card_ids)do
		if sgs.Sanguosha:getCard(id):getSuit()==2 then
			if self.changxin_damage<1 or self:canDamageHp(damage.from,damage.card) then
				continue
			else
				self.changxin_damage = self.changxin_damage-1
			end
		end
		return id
	end
	return -1
end

sgs.ai_skill_invoke.runwei = function(self,data)
	local to = data:toPlayer()
	local n = self:getOverflow(to)
	if self:isFriend(to) then
		return self:canDraw(to) and n<0 or n>1
	end
	return n<2
end

sgs.ai_skill_choice.runwei = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	if self:isFriend(to) then
		if self:getOverflow(to)<0 then
			return items[1]
		end
	end
	return items[2]
end

function sgs.ai_cardsview.zhongshen(self,class_name,player)
  	for _,c in sgs.list(player:getCards("he"))do
		if c:isRed() and c:hasTip("zhongshen_lun",false) then
			return "jink:zhongshen[no_suit:0]="..c:getId()
		end
	end
end

sgs.ai_skill_invoke.qingzhe = function(self,data)
	return true
end

sgs.ai_skill_use["@@qingzhe"] = function(self,prompt)
	local cards = {}
	for _,id in sgs.list(self.player:getTag("qingzheForAI"):toIntList())do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
	cards = self:sortByUseValue(cards)
	for _,c in sgs.list(cards)do
		if c:isAvailable(self.player) then
			local d = self:aiUseCard(c)
			if d.card then
				if c:canRecast() and d.to:length()<1 then continue end
				local valid = {}
				for _,to in sgs.list(d.to)do
					table.insert(valid,to:objectName())
				end
				return c:toString().."->"..table.concat(valid,"+")
			end
		end
	end
end

addAiSkills("olyicheng").getTurnUseCard = function(self)
	return sgs.Card_Parse("@olYichengCard=.")
end

sgs.ai_skill_use_func["olYichengCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.olYichengCard = 4.4
sgs.ai_use_priority.olYichengCard = 8.8

sgs.ai_skill_use["@@olyicheng"] = function(self,prompt)
	local cards = {}
	for _,id in sgs.list(self.player:getTag("olyichengForAI"):toIntList())do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
	local valid = {}
	cards = self:sortByUseValue(cards)
	local hs = self:sortByUseValue(self.player:getHandcards(),true)
	for _,c in sgs.list(cards)do
		for _,h in sgs.list(hs)do
			if table.contains(valid,h:getId()) then continue end
			if self:getUseValue(c)>self:getUseValue(h) and c:isAvailable(self.player) then
				table.insert(valid,c:getId())
				table.insert(valid,h:getId())
				break
			end
		end
	end
	if #valid>1 then
		return "@olYicheng2Card="..table.concat(valid,"+")
	end
end

addAiSkills("chanshuang").getTurnUseCard = function(self)
	return sgs.Card_Parse("@ChanshuangCard=.")
end

sgs.ai_skill_use_func["ChanshuangCard"] = function(card,use,self)
	self:sort(self.friends_noself,nil,true)
    for _,p in sgs.list(self.friends_noself)do
		if self:canDraw(p) or getCardsNum("Slash",p,self.player)>0 then
			use.card = card
			use.to:append(p)
			return
		end
	end
    for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if not self:isEnemy(p) and (self:canDraw(p) or getCardsNum("Slash",p,self.player)>0) then
			use.card = card
			use.to:append(p)
			return
		end
	end
    for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if not(self:canDraw(p) or getCardsNum("Slash",p,self.player)>0) then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.ChanshuangCard = 4.4
sgs.ai_use_priority.ChanshuangCard = 4.8

sgs.ai_skill_choice.chanshuang = function(self,choices,data)
	local items = choices:split("+")
	if table.contains(items,"chanshuang2") then
		for _,s in sgs.list(self:getCards("Slash"))do
			if self:aiUseCard(s).card then
				return "chanshuang2"
			end
		end
	end
	if table.contains(items,"chanshuang1") then
		return "chanshuang1"
	end
	return items[1]
end

addAiSkills("xuanzhu").getTurnUseCard = function(self)
	local n = self.player:getChangeSkillState("xuanzhu")
	if n==1 then
		for _,pn in sgs.list(patterns())do
			local dc = dummyCard(pn)
			if dc:getTypeId()==1 and dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				if d.card then
					self.xuanzhuUse = d
					return sgs.Card_Parse("@Xuanzhu2Card=.")
				end
			end
		end
	else
		for _,pn in sgs.list(patterns())do
			local dc = dummyCard(pn)
			if dc:getTypeId()==2 and dc:isKindOf("SingleTargetTrick") and dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				if d.card then
					if dc:canRecast() and d.to:length()<1 then continue end
					self.xuanzhuUse = d
					return sgs.Card_Parse("@Xuanzhu2Card=.")
				end
			end
		end
	end
end

sgs.ai_skill_use_func["Xuanzhu2Card"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.Xuanzhu2Card = 4.4
sgs.ai_use_priority.Xuanzhu2Card = 4.8

sgs.ai_skill_use["@@xuanzhu"] = function(self,prompt)
	local cards = self.player:getCards("he")
	cards = self:sortByUseValue(cards,true)
	local d = self.xuanzhuUse
	local valid = {}
	for _,p in sgs.list(d.to)do
		table.insert(valid,p:objectName())
	end
	for i,c in sgs.list(cards)do
		if i<=#cards/2 and c:getTypeId()==3 then
			return "@XuanzhuCard="..c:getId()..":"..d.card:objectName().."->"..table.concat(valid,"+")
		end
	end
	for _,c in sgs.list(cards)do
		return "@XuanzhuCard="..c:getId()..":"..d.card:objectName().."->"..table.concat(valid,"+")
	end
end

sgs.ai_guhuo_card.xuanzhu = function(self,toname,class_name)
	if self:getCardsNum(class_name)>0 then return end
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	for i,c in sgs.list(cards)do
		if i<=#cards/2 and c:getTypeId()==3 then
			return "@XuanzhuCard="..c:getId()..":"..toname
		end
	end
	for _,c in sgs.list(cards)do
		return "@XuanzhuCard="..c:getId()..":"..toname
	end
end

addAiSkills("qushi").getTurnUseCard = function(self)
	return sgs.Card_Parse("@QushiCard=.")
end

sgs.ai_skill_use_func["QushiCard"] = function(card,use,self)
	local tos = sgs.QList2Table(self.room:getOtherPlayers(self.player))
	self:sort(tos,nil,true)
    for i,p in sgs.list(tos)do
		if i<=#tos/2 and p:getPile("qu_shi"):length()<1 then
			use.card = card
			use.to:append(p)
			return
		end
	end
    for _,p in sgs.list(tos)do
		if p:getPile("qu_shi"):length()<1 then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.QushiCard = 4.4
sgs.ai_use_priority.QushiCard = 5.8

sgs.ai_guhuo_card.weijie = function(self,toname)
	sgs.ai_skill_choice.weijie = nil
    for _,p in sgs.list(self.room:getAlivePlayers())do
		if not self:isFriend(p) and self.player:distanceTo(p)==1 then
			for _,c in sgs.list(getKnownCards(p,self.player))do
				if c:getTypeId()==1 then
					self.weijieTo = p
					sgs.ai_skill_choice.weijie = c:objectName()
					return "@WeijieCard=.:"..toname
				end
			end
		end
	end
    for _,p in sgs.list(self.room:getAlivePlayers())do
		if self:isWeak() and self.player:distanceTo(p)==1 then
			for _,c in sgs.list(getKnownCards(p,self.player))do
				if c:getTypeId()==1 then
					self.weijieTo = p
					sgs.ai_skill_choice.weijie = c:objectName()
					return "@WeijieCard=.:"..toname
				end
			end
		end
	end
    for _,p in sgs.list(self.room:getAlivePlayers())do
		if self:isEnemy(p) and self.player:distanceTo(p)==1 and self:doDisCard(p,"h") then
			return "@WeijieCard=.:"..toname
		end
	end
end

sgs.ai_skill_playerchosen.weijie = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
    for _,target in sgs.list(destlist)do
		if self.weijieTo==target
		then return target end
	end
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_playerchosen.shilu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

addAiSkills("miuyan").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	for _,c in sgs.list(cards)do
		if c:isBlack() then
			local dc = dummyCard("fire_attack")
			dc:setSkillName("miuyan")
			dc:addSubcard(c)
			if dc:isAvailable(self.player) then
				return dc
			end
		end
	end
end

sgs.ai_skill_playerschosen.gongjie = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local tos = {}
    for _,p in sgs.list(destlist)do
		if #tos<x and self:isFriend(p)
		and self:canDraw(p)
		then table.insert(tos,p) end
	end
	return tos
end

sgs.ai_skill_invoke.xiangxu = function(self,data)
	local to = self.room:getCurrent()
	local n = to:getHandcardNum()-self.player:getHandcardNum()
	if n>0 then
		return self:canDraw()
	else
		return n==-2 and self:isWeak()
	end
end

sgs.ai_skill_askforyiji.xiangzuo = function(self,card_ids,tos)
	if self:getAllPeachNum()+self.player:getHp()>0
	then return nil,-1 end
	local destlist = sgs.QList2Table(tos) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) and self:canDraw(p)
		and self.player:getMark("gongjie"..p:objectName())>0
		and self.player:getMark("xiangxu"..p:objectName())>0 then
			return p,card_ids[1]
		end
	end
	local p,id = sgs.ai_skill_askforyiji.nosyiji(self,card_ids,tos)
	if p and p~=self.player then
		return p,id
	end
end

sgs.ai_skill_playerchosen.jieyan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	if self.room:getCurrent():getPhase()==sgs.Player_Discard then
		for _,p in sgs.list(destlist)do
			if self:isFriend(p) and self:canDraw(p)
			then return p end
		end
		for _,p in sgs.list(destlist)do
			if self:isFriend(p)
			then return p end
		end
		for _,p in sgs.list(destlist)do
			if not self:isEnemy(p)
			then return p end
		end
	end
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and self:damageIsEffective(p,"N")
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p)
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p)
		then return p end
	end
end

sgs.ai_skill_choice.jieyan = function(self,choices,data)
	local items = choices:split("+")
	if table.contains(items,"jieyan2") and self:isWeak() then
		return "jieyan2"
	end
	return items[1]
end

sgs.ai_skill_invoke.jinghua = function(self,data)
	return #self.enemies>=#self.friends_noself
end

sgs.ai_skill_invoke.shuiyue = function(self,data)
	return #self.enemies>=#self.friends_noself
end

addAiSkills("pingduan").getTurnUseCard = function(self)
	return sgs.Card_Parse("@PingduanCard=.")
end

sgs.ai_skill_use_func["PingduanCard"] = function(card,use,self)
	local tos = sgs.QList2Table(self.room:getAlivePlayers())
	self:sort(tos,nil,true)
    for _,p in sgs.list(tos)do
		if self:isFriend(p) and p:hasEquip() then
			use.card = card
			use.to:append(p)
			return
		end
	end
    for _,p in sgs.list(tos)do
		if self:isFriend(p) and p:getCardCount()>2 then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.PingduanCard = 4.4
sgs.ai_use_priority.PingduanCard = 5.8

sgs.ai_skill_cardask["pingduan2"] = function(self,data,pattern,prompt)
    return true
end

sgs.ai_skill_invoke.pingduan = function(self,data)
	local to = data:toString():split(":")[2]
	to = BeMan(self.room,to)
	return to and not self:isEnemy(to)
end

sgs.ai_skill_choice.fuchao = function(self,choices,data)
	local items = choices:split("+")
	local use = data:toCardUse()
	local from,tocard
	if use and use.card then
		from = use.who
		tocard = use.whocard
	else
		use = data:toCardResponse()
		from = use.m_who
		tocard = use.m_toCard
	end
	use = self.room:getUseStruct(tocard)
	if table.contains(items,"fuchao1") then
		local n=0
		for _,p in sgs.list(self.friends_noself)do
			if use.to:contains(p) and self:playerGetRound(p)>self:playerGetRound(self.player)
			then n=n-1 end
		end
		for _,p in sgs.list(self.enemies)do
			if use.to:contains(p) and self:playerGetRound(p)>self:playerGetRound(self.player)
			then n=n+1 end
		end
		if n>=0 then
			return "fuchao1"
		end
	end
	if table.contains(items,"fuchao2") then
		local n=0
		for _,p in sgs.list(self.friends_noself)do
			if use.to:contains(p) and self:playerGetRound(p)>self:playerGetRound(self.player)
			then n=n-1 end
		end
		for _,p in sgs.list(self.enemies)do
			if use.to:contains(p) and self:playerGetRound(p)>self:playerGetRound(self.player)
			then n=n+1 end
		end
		if n<0 and not self:isWeak() then
			return "fuchao2"
		end
	end
	return items[1]
end

sgs.ai_skill_choice.leiluan = function(self,choices,data)
	local items = choices:split("+")
	for _,pc in sgs.list(items)do
		local dc = dummyCard(pc)
		dc:setSkillName("leiluan")
		if dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then
				if dc:canRecast() and d.to:length()<1
				then continue end
				self.leiluanUse = d
				return pc
			end
		end
	end
end

sgs.ai_skill_use["@@leiluan"] = function(self,prompt)
	local d = self.leiluanUse
	if d.card then
		local valid = {}
		for _,to in sgs.list(d.to)do
			table.insert(valid,to:objectName())
		end
		return d.card:toString().."->"..table.concat(valid,"+")
	end
end

addAiSkills("leiluan").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	local ids = {}
	local n = self.player:getMark("&leiluan")
	n = math.max(1,n)
	for _,c in sgs.list(cards)do
		if #ids<n then
			table.insert(ids,c:getId())
		end
	end
	if #ids<n or #ids>2 then return end
	for _,pc in sgs.list(patterns())do
		if self.player:getMark("leiluan_guhuo_remove_"..pc.."_lun")<1 then
			local dc = dummyCard(pc)
			if dc:getTypeId()~=1
			or self:getCardsNum(dc:getClassName())>0
			then continue end
			dc:setSkillName("leiluan")
			for _,id in sgs.list(ids)do
				dc:addSubcard(id)
			end
			if dc:isAvailable(self.player) and self:aiUseCard(dc).card then
				return dc
			end
		end
	end
end

sgs.ai_guhuo_card.leiluan = function(self,toname,class_name)
	if self.player:getMark("leiluan_guhuo_remove_"..toname.."_lun")>0
	or self:getCardsNum(class_name)>0 then return end
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	local ids = {}
	local n = self.player:getMark("&leiluan")
	n = math.max(1,n)
	for _,c in sgs.list(cards)do
		if #ids<n then
			table.insert(ids,c:getId())
		end
	end
	if #ids<n or #ids>2 then return end
	local dc = dummyCard(toname)
	dc:setSkillName("leiluan")
	for _,id in sgs.list(ids)do
		dc:addSubcard(id)
	end
	return dc:toString()
end

addAiSkills("renxian").getTurnUseCard = function(self)
	local ids,ids2 = {},{}
  	for _,h in sgs.list(self.player:getHandcards())do
		if h:isDamageCard() then
			table.insert(ids,h:getId())
			if not self.player:isJilei(h) then
				table.insert(ids2,h:getId())
			end
		end
	end
	sgs.ai_use_priority.RenxianCard = 2.2
	if #ids==2 and #ids==#ids2 then
		sgs.ai_use_priority.RenxianCard = 11.2
		return sgs.Card_Parse("@RenxianCard="..table.concat(ids2,"+"))
	elseif #ids<1 then
		sgs.ai_use_priority.RenxianCard = 11.2
		return sgs.Card_Parse("@RenxianCard=.")
	else
		return sgs.Card_Parse("@RenxianCard=.")
	end
end

sgs.ai_skill_use_func["RenxianCard"] = function(card,use,self)
  	use.card = card
end

sgs.ai_use_value.RenxianCard = 5.4
sgs.ai_use_priority.RenxianCard = 2.2

addAiSkills("yanliangvs").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	for i,c in sgs.list(cards)do
		if i<=#cards/2 and c:isKindOf("EquipCard") then
			if self:aiUseCard(dummyCard("analeptic")).card then
				return sgs.Card_Parse("@YanliangCard="..c:getId())
			end
		end
	end
end

sgs.ai_skill_use_func["YanliangCard"] = function(card,use,self)
	for i,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if p:hasSkill("yanliang") and not self:isEnemy(p) then
			use.card = card
			use.to:append(p)
			break
		end
	end
end

sgs.ai_use_value.YanliangCard = 5.4
sgs.ai_use_priority.YanliangCard = 3.2

sgs.ai_skill_playerchosen.xiaoshi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local use = self.player:getTag("xiaoshiUse"):toCardUse()
	if use.card:isDamageCard() then
		for _,p in sgs.list(destlist)do
			if self:isEnemy(p) and self:isWeak(p)
			and self:hasTrickEffective(use.card,p,use.from)
			then return p end
		end
		for _,p in sgs.list(destlist)do
			if self:isFriend(p)
			and (not self:isWeak(p) or not self:hasTrickEffective(use.card,p,use.from))
			then return p end
		end
		for _,p in sgs.list(destlist)do
			if not self:isFriend(p) then return p end
		end
	else
		for _,p in sgs.list(destlist)do
			if self:isFriend(p) and not self:hasTrickEffective(use.card,p,use.from)
			then return p end
		end
		for _,p in sgs.list(destlist)do
			if self:isFriend(p)
			then return p end
		end
	end
end

sgs.ai_skill_playerchosen.xiaoshi1 = function(self,players)
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

sgs.ai_skill_invoke.jinming = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_invoke.olmouhulie = function(self,data)
	local use = data:toCardUse()
	return not self:isFriend(use.to:at(0))
end

sgs.ai_skill_playerchosen.olmouhulie = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	for _,p in sgs.list(destlist)do
		if not self:isWeak() and self:getCardsNum("Jink")<1
		then return p end
	end
end

sgs.ai_skill_playerchosen.olmouyipo = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local x = math.max(1, self.player:getLostHp())
	for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and p:getCardCount()/2<x
		then return p end
	end
	for _,p in sgs.list(destlist)do
		if self:isFriend(p) and self:canDraw(p)
		then return p end
	end
end

sgs.ai_skill_choice.olmouyipo = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	if self:isFriend(to) then
		return items[1]
	else
		return items[2]
	end
end

addAiSkills("huiyun").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	for i,c in sgs.list(cards)do
		local dc = dummyCard("fire_attack")
		dc:setSkillName("huiyun")
		dc:addSubcard(c)
		if dc:isAvailable(self.player) then
			return dc
		end
	end
end

sgs.ai_skill_choice.huiyun = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	if self:isFriend(to) then
		return items[#items]
	else
		return items[1]
	end
end






--储元
sgs.ai_skill_invoke.chuyuan = function(self,data)
	local player = data:toPlayer()
	if self.player:getPile("cychu"):length()==2 then
		if self.player:hasSkill("dengji") and self.player:getMark("dengji")<=0 then return self.player:getMaxHp()>1 end
		if self.player:hasSkill("tianxing") and self.player:getMark("tianxing")<=0 then return self.player:getMaxHp()>1 end
	end
	if not self:doDisCard(player,"h",true) then return false end
	return true
end

sgs.ai_skill_discard.chuyuan = function(self,discard_num,min_num,optional,include_equip)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	return {cards[1]:getEffectiveId()}
end

--天行
sgs.ai_skill_choice.tianxing = function(self,choices,data)  --待补充
	local skills = choices:split("+")
	local skills2 = {}
	if table.contains(skills,"tenyearzhiheng") then table.insert(skills2,"tenyearzhiheng") end
	if table.contains(skills,"olluanji") then table.insert(skills2,"olluanji") end
	if #self.friends_noself<=0 and #skills2>0 then
		return skills2[math.random(1,#skills2)]
	end
	return skills[math.random(1,#skills)]
end

--神赋
sgs.ai_skill_playerchosen.shenfu_ji = function(self,targets)
	local targets_table = self:findPlayerToDamage(1,self.player,sgs.DamageStruct_Thunder,targets)
	if #targets_table<=0 then return nil end
	if not self.player:isChained() or not self:isWeak() or not self:damageIsEffective(self.player,sgs.DamageStruct_Thunder,self.player) then return targets_table[1] end
	for _,p in sgs.list(targets_table)do
		if p:isChained() then continue end
		return p
	end
	return nil
end

sgs.ai_skill_playerchosen.shenfu_ou = function(self,targets)
	local targets_table1,targets_table2 = {},{}
	for _,target in sgs.list(targets)do
		if math.abs(target:getHandcardNum()-target:getHp())==1 then
			table.insert(targets_table1,target)
		else
			if self:isFriend(target) or (self:isEnemy(target) and not target:isKongcheng()) or (self:isEnemy(target) and self:needKongcheng(target,true)) then
				table.insert(targets_table2,target)
			end
		end
	end
	if #targets_table1>0 then
		self:sort(targets_table1,"defense")
		return targets_table1[1]
	end
	if #targets_table2>0 then
		self:sort(targets_table2,"defense")
		return targets_table2[1]
	end
	return nil
end

sgs.ai_skill_choice.shenfu = function(self,choices,data)
	local player = data:toPlayer()
	if self:isFriend(player) then
		if player:hasSkills("tuntian|mobiletuntian|oltuntian") and player:getHandcardNum()-player:getHp()==1 then
			return "discard"
		end
		return "draw"
	end
	if self:needKongcheng(player,true) then return "draw" end
	return "discard"
end


sgs.ai_skill_cardask["tianhou0"] = function(self,data)
	local js = self.player:getCards("j")
	local handcards = self.player:getCards("h")
	handcards = self:sortByKeepValue(handcards) -- 按保留值排序
	if js:length()>0 then
		local jt = sgs.ai_judgestring[js:last():objectName()]
		if type(jt)~="table" then
			if type(jt)=="string" then
				jt = {jt,true}
			else
				jt = {jc:getSuitString(),true}
			end
		end
		if jt then
			for _,h in sgs.list(handcards)do
				if sgs.Sanguosha:matchExpPattern(jt[1],self.player,h)==jt[2]
				then return h:toString() end
			end
		end
	end
	local tc = sgs.Sanguosha:getCard(data:toInt())
	for _,h in sgs.list(handcards)do
		if self:getUseValue(tc)>self:getUseValue(h)
		then return h:toString() end
	end
	js = self:poisonCards("h")
	if #js>0 then
		return js[1]:toString()
	end
    return "."
end


sgs.ai_skill_playerchosen.tianhou = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_discard.chenshuo = function(self,discard_num,min_num,optional,include_equip)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	return {cards[math.random(1,#cards)]:getEffectiveId()}
end


sgs.ai_skill_playerchosen.olfengyao = function(self,players)
    for _,p in sgs.list(players)do
		if not self:isFriend(p)
		then return p end
	end
end

sgs.ai_skill_playerchosen.kuangxiang = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) and self:isWeak(p) and p:getHandcardNum()<3
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and p:getHandcardNum()>5
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) and p:getHandcardNum()<3
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and p:getHandcardNum()>4
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if not self:isEnemy(p) and p:getHandcardNum()<4
		then return p end
	end
end

addAiSkills("zonglve").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	for i,c in sgs.list(cards)do
		local dc = dummyCard()
		dc:setSkillName("zonglve")
		dc:addSubcard(c)
		if dc:isAvailable(self.player) then
			return dc
		end
	end
end

sgs.ai_skill_invoke.zonglve = function(self,data)
	local to = data:toPlayer()
	return self:doDisCard(to,"hej",true)
end

addAiSkills("shuzi").getTurnUseCard = function(self)
	local ids = {}
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards)
	local tp
  	for _,h in sgs.list(cards)do
		local c,p = self:getCardNeedPlayer({h},false)
		if c and p then
			if tp==p then
				table.insert(ids,h:getId())
				if #ids==2 then
					self.shuzi_to = p
					return sgs.Card_Parse("@ShuziCard="..table.concat(ids,"+"))
				end
			else
				ids = {h:getId()}
				tp = p
			end
		end
	end
end

sgs.ai_skill_use_func["ShuziCard"] = function(card,use,self)
  	if self.shuzi_to then
		use.card = card
		use.to:append(self.shuzi_to)
	end
end

sgs.ai_use_value.ShuziCard = 5.4
sgs.ai_use_priority.ShuziCard = 1.2

sgs.ai_skill_playerchosen.shuzi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
	local isf = self:isFriend(self.shuzi_to)
	if not isf and self:damageIsEffective(self.shuzi_to,"N") then
		return self.shuzi_to
	end	
    for _,p in sgs.list(destlist)do
		if isf and self:isEnemy(p) and #self:poisonCards("e",p)<p:getEquips():length()
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if not isf and self:isFriend(p) and #self:poisonCards("ej",p)>0
		then return p end
	end
end

sgs.ai_skill_invoke.xianying = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_use["@@xianying0!"] = function(self,prompt)
	local mt = ""
	for _,m in sgs.list(self.player:getMarkNames())do
		if m:startsWith("&xianying+:+") and self.player:getMark(m)>0
		then mt = m end
	end
	local valid = {}
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
    	table.insert(valid,h:getEffectiveId())
		if not mt:contains(tostring(#valid)) then break end
	end
	if #valid<1 then return end
	return "@XianyingCard="..table.concat(valid,"+")
end

sgs.ai_skill_use["@@xianying1"] = function(self,prompt)
    local c = self.player:property("xianyingName"):toString()
	c = dummyCard(c)
	c:setSkillName("_xianying")
    local dummy = self:aiUseCard(c)
   	if dummy.card then
      	local tos = {}
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
		if c:canRecast() and #tos<1 then return end
       	return c:toString().."->"..table.concat(tos,"+")
    end
end

addAiSkills("olliyong").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
  	for _,h in sgs.list(cards)do
		if self.player:getChangeSkillState("olliyong")==1 then
			local mt = ""
			for _,m in sgs.list(self.player:getMarkNames())do
				if m:startsWith("&olliyong+") and m:endsWith("-Clear")
				and self.player:getMark(m)>0 then mt = m end
			end
			if mt:contains(h:getSuitString()) then continue end
			local dc = dummyCard("duel")
			dc:setSkillName("olliyong")
			dc:addSubcard(h)
			if dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				if d.card then
					self.olliyong_to = d.to
					sgs.ai_use_priority.OLLiyongCard = sgs.ai_use_priority.Duel
					return sgs.Card_Parse("@OLLiyongCard="..h:getId())
				end
			end
		else
			if self.player:isJilei(h) then continue end
			for _,p in sgs.list(self.enemies)do
				if getCardsNum("Slash",p,self.player)<self:getCardsNum("Slash")
				and dummyCard("duel"):isAvailable(p) then
					self.olliyong_to = sgs.SPlayerList()
					self.olliyong_to:append(p)
					sgs.ai_use_priority.OLLiyongCard = 1.2
					return sgs.Card_Parse("@OLLiyongCard="..h:getId())
				end
			end
		end
	end
end

sgs.ai_skill_use_func["OLLiyongCard"] = function(card,use,self)
  	if self.olliyong_to then
		use.card = card
		use.to = self.olliyong_to
	end
end

sgs.ai_use_value.OLLiyongCard = 5.4
sgs.ai_use_priority.OLLiyongCard = 1.2

addAiSkills("bojue").getTurnUseCard = function(self)
	return sgs.Card_Parse("@BojueCard=.")
end

sgs.ai_skill_use_func["BojueCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,p in sgs.list(self.enemies)do
		if p:getHandcardNum()>0 then
			use.card = card
			use.to:append(p)
			break
		end
	end
end

sgs.ai_use_value.BojueCard = 5.4
sgs.ai_use_priority.BojueCard = 1.2

sgs.ai_skill_cardask["xiongni0"] = function(self,data,pattern)
	if #self.enemies>#self.friends/2 then return true end
    return "."
end

sgs.ai_skill_cardask["xiongni1"] = function(self,data,pattern)
	local from = data:toPlayer()
	if self:canDamageHp(from) and not self:isWeak() then return "." end
    return true
end

addAiSkills("fengshang").getTurnUseCard = function(self)
	return sgs.Card_Parse("@FengshangCard=.")
end

sgs.ai_skill_use_func["FengshangCard"] = function(card,use,self)
	if sgs.ai_skill_use["@@fengshang"](self,"") then
		use.card = card
	end
end

sgs.ai_use_value.FengshangCard = 5.4
sgs.ai_use_priority.FengshangCard = 9.2

sgs.ai_skill_use["@@fengshang"] = function(self,prompt)
	local sn = {}
	local ids = {}
	local sus = {}
	for _,id in sgs.list(self.room:getDiscardPile())do
		local c = sgs.Sanguosha:getCard(id)
		if self.player:getMark(id.."fengshangId-Clear")>0
		and self.player:getMark(c:getSuitString().."fengshang_lun")<1 then
			local h,p = self:getCardNeedPlayer({c},true)
			sus[c:getSuitString()] = sus[c:getSuitString()] or {}
			table.insert(sus[c:getSuitString()],c:toString())
			if h==c and p then
				sn[c:toString()] = p:objectName()
			elseif #self:poisonCards({c})<1 then
				table.insert(ids,c)
			end
		end
	end
	for s,cs in pairs(sus)do
		if #cs>1 then
			local valid,tos = {},{}
			for _,c in ipairs(cs)do
				if sn[c] and not table.contains(tos,sn[c]) then
					table.insert(valid,c)
					table.insert(tos,sn[c])
					if #valid>=2 then
						return "@FengshangCard="..table.concat(valid,"+").."->"..table.concat(tos,"+")
					end
				end
			end
		end
	end
	self:sortByKeepValue(ids,true)
	self:sort(self.friends)
	for s,cs in pairs(sus)do
		if #cs>1 then
			local valid,tos = {},{}
			for _,c in ipairs(ids)do
				if c:getSuitString()==s and #valid<2 then
					table.insert(valid,c:getId())
				end
			end
			if #valid<2 then continue end
			for _,p in sgs.list(self.friends)do
				if self:canDraw(p) then
					table.insert(tos,p:objectName())
					if #tos>=2 then
						return "@FengshangCard="..table.concat(valid,"+").."->"..table.concat(tos,"+")
					end
				end
			end
		end
	end
	local aps = self:sort(self.room:getAlivePlayers())
	for s,cs in pairs(sus)do
		if #cs>1 then
			local valid,tos = {},{}
			for _,c in ipairs(ids)do
				if c:getSuitString()==s and #valid<2 then
					table.insert(valid,c:getId())
				end
			end
			if #valid<2 then continue end
			for _,p in sgs.list(aps)do
				if self:canDraw(p) and not self:isEnemy(p) then
					table.insert(tos,p:objectName())
					if #tos>=2 then
						return "@FengshangCard="..table.concat(valid,"+").."->"..table.concat(tos,"+")
					end
				end
			end
		end
	end
	for s,cs in pairs(sus)do
		if #cs>1 then
			local valid,tos = {},{}
			for _,c in ipairs(cs)do
				if #self:poisonCards({tonumber(c)})<1 then
					for _,p in sgs.list(aps)do
						if table.contains(tos,p:objectName()) then continue end
						if self:canDraw(p) and not self:isEnemy(p) then
							table.insert(tos,p:objectName())
							table.insert(valid,c)
							break
						end
					end
				else
					for _,p in sgs.list(aps)do
						if table.contains(tos,p:objectName()) then continue end
						if self:isEnemy(p) then
							table.insert(tos,p:objectName())
							table.insert(valid,c)
							break
						end
					end
				end
				if #tos>=2 then
					return "@FengshangCard="..table.concat(valid,"+").."->"..table.concat(tos,"+")
				end
			end
		end
	end
end

sgs.ai_skill_use["@@jigu"] = function(self,prompt)
	local valid,ts = {},{}
	for _,id in sgs.list(self.player:getPile("ji_gu"))do
		table.insert(ts,sgs.Sanguosha:getCard(id))
	end
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,c in sgs.list(ts)do
		for _,h in sgs.list(cards)do
			if table.contains(valid,h:getId())
			or self:getKeepValue(h)>=self:getKeepValue(c)
			and self:getUseValue(h)>=self:getUseValue(c)
			then continue end
			table.insert(valid,h:getId())
			table.insert(valid,c:getId())
			break
		end
	end
	return #valid>1
	and string.format("@JiguCard=%s",table.concat(valid,"+"))
end

sgs.ai_skill_use["@@jiewan"] = function(self,prompt)
	local tos,ts = {},{}
	for _,id in sgs.list(self.player:getPile("ji_gu"))do
		table.insert(ts,id)
	end
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		local dc = dummyCard("snatch")
		dc:setSkillName("jiewan")
		dc:addSubcard(h)
		if dc:isAvailable(self.player) then
			local ids = {h:getId()}
			if #ts>1 then
				table.insert(ids,ts[1])
				table.insert(ids,ts[2])
			elseif self.player:getLostHp()<1 then
				break
			end
			local d = self:aiUseCard(dc)
			if d.card then
				if d.to:length()<1 then return "@JiewanCard="..table.concat(ids,"+") end
				for _,p in sgs.list(d.to)do
					table.insert(tos,p:objectName())
				end
				return "@JiewanCard="..table.concat(ids,"+").."->"..table.concat(tos,"+")
			end
		end
	end
end

sgs.ai_fill_skill.olspxixiang = function(self)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	local ids = {}
	for _,c in sgs.list(cards)do
		if table.contains(self.toUse,c)
		or self:getKeepValue(c)>6 then continue end
		table.insert(ids,c:getId())
		if #ids>1 then return sgs.Card_Parse("#olspxixiangCard:"..table.concat(ids,"+")..":") end
	end
end

sgs.ai_skill_use_func["#olspxixiangCard"] = function(card,use,self)
	for i=1,3 do
		local cs = {"slash","duel"}
		local dc = dummyCard(cs[math.random(1,2)])
		dc:setSkillName("olspxixiang")
		dc:addSubcards(card:getSubcards())
		if self.player:getMark("olspxixiang_juguan_remove_"..dc:objectName().."-PlayClear")<1
		and dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then
				use.card = sgs.Card_Parse("#olspxixiangCard:"..table.concat(sgs.QList2Table(card:getSubcards()),"+")..":"..dc:objectName())
				use.to = d.to
				break
			end
		end
	end
end

sgs.ai_use_value.olspxixiangCard = 3.4
sgs.ai_use_priority.olspxixiangCard = 2.2

sgs.ai_fill_skill.olspzhubei = function(self)
    return not self:isWeak()
	and sgs.Card_Parse("#olspzhubeiCard:.:")
end

sgs.ai_skill_use_func["#olspzhubeiCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,p in sgs.list(self.enemies)do
		if p:getCardCount()>0 then
			use.card = card
			use.to:append(p)
			break
		end
	end
end

sgs.ai_use_value.olspzhubeiCard = 3.4
sgs.ai_use_priority.olspzhubeiCard = 2.2

sgs.ai_skill_invoke.olspzhubei = function(self,data)
	local damage = data:toDamage()
	if damage.to then
		return self:canDraw()
	end
	local to = data:toPlayer()
	return self.player:getHandcardNum()<to:getHandcardNum()
end

sgs.ai_use_revises["&keolranji_ban"] = function(self,card,use)
	if card:isKindOf("Peach") or card:isKindOf("Analeptic") and self.player:getHp()<1 then
		if self.player:getMark("&keolranji_ban")>0 then
			return false
		end
	end
end

sgs.ai_skill_playerchosen.olmoujiaodi = function(self,players)
	return sgs.ai_skill_playerchosen.jincanmou(self,players)
end

addAiSkills("olmoubaojing").getTurnUseCard = function(self)
	return sgs.Card_Parse("#olmoubaojingCard:.:")
end

sgs.ai_skill_use_func["#olmoubaojingCard"] = function(card,use,self)
	self:sort(self.enemies,nil,true)
	for _,p in sgs.list(self.enemies)do
		if p:getAttackRange()>1 then
			use.card = card
			use.to:append(p)
			sgs.ai_skill_choice.olmoubaojing = "2"
			return
		end
	end
	self:sort(self.friends_noself,nil,true)
	for _,p in sgs.list(self.friends_noself)do
		if p:getAttackRange()<3 then
			use.card = card
			use.to:append(p)
			sgs.ai_skill_choice.olmoubaojing = "1"
			return
		end
	end
	for _,p in sgs.list(self.friends_noself)do
		use.card = card
		use.to:append(p)
		sgs.ai_skill_choice.olmoubaojing = "1"
		return
	end
end

sgs.ai_use_value.olmoubaojingCard = 5.4
sgs.ai_use_priority.olmoubaojingCard = 9.2

sgs.ai_skill_invoke.SkillEffect = function(self,data)
	local st = data:toString()
	if st=="s1" or st=="s5" or st=="s9" or st=="s10" or st=="s21" or st=="s22" then
		return self:canDraw()
	end
	if st=="s23" then
		return self:canDraw() and (not self.player:faceUp() or self.player:getHandcardNum()<=self.player:getHp())
	end
	if st=="s26" then
		return self:canDraw() and not self:isWeak()
	end
	if st=="s29" then
		local damage = self.player:getTag("SkillEffect"):toDamage()
		return self:isEnemy(damage.to)
	end
	if st=="s30" then
		local damage = self.player:getTag("SkillEffect"):toDamage()
		return self:isFriend(damage.to) and (self:isWeak(damage.to) or damage.from and self:isFriend(damage.from))
		or self:isEnemy(damage.to) and not self:isWeak(damage.to) and damage.from and self:isFriend(damage.from) and self:canDraw(damage.from)
	end
	return true
end

for i=0,99 do
	sgs.ai_skill_playerchosen["qingshu_tianshu"..i] = function(self,players)
		local str = self.room:getTag("qingshu_tianshu"..i.."SkillEffect"):toString()
		local destlist = self:sort(players)
		if str=="SkillEffect2" then
			for _,p in sgs.list(destlist)do
				if self:doDisCard(p,"hej")
				then return p end
			end
		end
		if str=="SkillEffect7" then
			for _,p in sgs.list(destlist)do
				if self:doDisCard(p,"hej",true)
				then return p end
			end
		end
		if str=="SkillEffect11" then
			for _,p in sgs.list(sgs.reverse(destlist))do
				if p:getMark("skillInvalidity")<1
				and self:isEnemy(p)
				then return p end
			end
			for _,p in sgs.list(sgs.reverse(destlist))do
				if p:getMark("skillInvalidity")<1
				and not self:isFriend(p)
				then return p end
			end
		end
		if str=="SkillEffect12" then
			for _,p in sgs.list(destlist)do
				if not p:faceUp() and self:isFriend(p)
				then return p end
			end
			for _,p in sgs.list(sgs.reverse(destlist))do
				if p:faceUp() and self:isEnemy(p)
				then return p end
			end
			for _,p in sgs.list(sgs.reverse(destlist))do
				if p:faceUp() and not self:isFriend(p)
				then return p end
			end
		end
		if str=="SkillEffect14" then
			for _,p in sgs.list(destlist)do
				if self:damageIsEffective(p,"T",self.player)
				and self:isEnemy(p)
				then return p end
			end
			for _,p in sgs.list(sgs.reverse(destlist))do
				if self:damageIsEffective(p,"T",self.player)
				and not self:isFriend(p)
				then return p end
			end
		end
		if str=="SkillEffect18" then
			for _,p in sgs.list(destlist)do
				if self:doDisCard(p,"he",true)
				and self:isEnemy(p) and p:getCardCount()>2
				then return p end
			end
		end
		if str=="SkillEffect20" then
			for _,p in sgs.list(destlist)do
				if self:isFriend(p)
				then return p end
			end
		end
		if str=="SkillEffect24" then
			for _,p in sgs.list(destlist)do
				if self.player:getMark(p:objectName().."SkillEffect24-SelfClear")<1
				and self:isEnemy(p) and not self.player:inMyAttackRange(p)
				then return p end
			end
			for _,p in sgs.list(destlist)do
				if self.player:getMark(p:objectName().."SkillEffect24-SelfClear")<1
				and self:isEnemy(p)
				then return p end
			end
		end
		if str=="SkillEffect25" then
			for _,p in sgs.list(destlist)do
				if self:isFriend(p) and p:isWounded()
				then return p end
			end
		end
	end
	sgs.ai_skill_playerschosen["qingshu_tianshu"..i] = function(self,players)
		local str = self.room:getTag("qingshu_tianshu"..i.."SkillEffect"):toString()
		local destlist = self:sort(players)
		if str=="SkillEffect27" then
			for _,p in sgs.list(destlist)do
				if self:isFriend(p) and self:canDraw(p) then
					for _,q in sgs.list(sgs.reverse(destlist))do
						if q:getHandcardNum()<=p:getHandcardNum() or self:isFriend(q) then continue end
						return {p,q}
					end
				end
			end
			for _,p in sgs.list(destlist)do
				if not self:isEnemy(p) then
					for _,q in sgs.list(sgs.reverse(destlist))do
						if q:getHandcardNum()<=p:getHandcardNum() or self:isFriend(q) then continue end
						return {p,q}
					end
				end
			end
		end
		if str=="SkillEffect28" then
			for _,p in sgs.list(destlist)do
				if self:isFriend(p) then
					for _,q in sgs.list(sgs.reverse(destlist))do
						if self:isFriend(q)
						or q:getEquips():length()-#self:poisonCards("e",q)<=p:getEquips():length()-#self:poisonCards("e",p)
						then continue end
						return {p,q}
					end
				end
			end
			for _,p in sgs.list(destlist)do
				if not self:isEnemy(p) then
					for _,q in sgs.list(sgs.reverse(destlist))do
						if self:isFriend(q)
						or q:getEquips():length()-#self:poisonCards("e",q)<=p:getEquips():length()-#self:poisonCards("e",p)
						then continue end
						return {p,q}
					end
				end
			end
		end
		if str=="SkillEffect19" then
			local tos = {}
			for _,p in sgs.list(destlist)do
				if self:isFriend(p) and self:canDraw(p) then
					table.insert(tos,p)
					if #tos>1 then break end
				end
			end
			for _,p in sgs.list(destlist)do
				if not table.contains(tos,p) and not self:isEnemy(p) then
					table.insert(tos,p)
					if #tos>1 then break end
				end
			end
			return tos
		end
	end
	sgs.ai_skill_discard["qingshu_tianshu"..i] = function(self)
		local str = self.room:getTag("qingshu_tianshu"..i.."SkillEffect"):toString()
		local cards = {}
		local handcards = sgs.QList2Table(self.player:getCards("he"))
		self:sortByKeepValue(handcards) -- 按保留值排序
		if str=="SkillEffect4" then
			for _,h in sgs.list(handcards)do
				if #cards>#handcards/2 or self:getKeepValue(h)>6 then continue end
				table.insert(cards,h:getEffectiveId())
			end
			return cards
		end
		if str=="SkillEffect25" then
			for _,h in sgs.list(handcards)do
				if #cards>=2 then continue end
				table.insert(cards,h:getEffectiveId())
			end
			if #cards<2 then return {} end
			for _,p in sgs.list(self.friends_noself)do
				if self:isWeak(p) and self.player:isWounded() then
					return cards
				end
			end
		end
	end
end

sgs.ai_skill_use["@@skilleffectp"] = function(self,prompt)
    local c = dummyCard()
	c:setSkillName("qingshu_tianshu0")
    local dummy = self:aiUseCard(c)
   	if dummy.card then
      	local tos = {}
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
       	return c:toString().."->"..table.concat(tos,"+")
    end
end

sgs.ai_fill_skill.olshoushu = function(self)
	return sgs.Card_Parse("#olshoushuCard:.:")
end

sgs.ai_skill_use_func["#olshoushuCard"] = function(card,use,self)
	self:sort(self.friends_noself,nil,true)
	for _,p in sgs.list(self.friends_noself)do
		local has = false
		for _,s in sgs.qlist(p:getVisibleSkillList())do
			if s:objectName():startsWith("qingshu_tianshu")
			then has = true break end
		end
		if has then continue end
		use.card = card
		use.to:append(p)
		break
	end
end

sgs.ai_use_value.olshoushuCard = 3.4
sgs.ai_use_priority.olshoushuCard = 6.2

sgs.ai_skill_cardask["bingcai0"] = function(self,data,pattern)
	local use = data:toCardUse()
	if self:isFriend(use.from) and use.to:length()>0 then
		if not self:isFriend(use.to:at(0)) then
			return true
		end
		if use.card:targetFixed() and use.from:getLostHp()>1 then
			return true
		end
	end
	return "."
end

addAiSkills("lixian").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
  	for _,h in sgs.list(cards)do
		if h:hasTip("lixian") then
			local dc = dummyCard()
			dc:setSkillName("lixian")
			dc:addSubcard(h)
			if dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				if d.card then
					return dc
				end
			end
		end
	end
end

function sgs.ai_cardsview.lixian(self,class_name,player)
  	for _,c in sgs.list(player:getCards("he"))do
		if c:hasTip("lixian") then
			if class_name=="Jink" then
				return "jink:lixian[no_suit:0]="..c:getId()
			else
				return "slash:lixian[no_suit:0]="..c:getId()
			end
		end
	end
end

addAiSkills("wenren").getTurnUseCard = function(self)
	return sgs.Card_Parse("@WenrenCard=.")
end

sgs.ai_skill_use_func["WenrenCard"] = function(card,use,self)
	self:sort(self.friends)
	for _,p in sgs.list(self.friends)do
		if p:getHandcardNum()<=self.player:getHandcardNum() and self:canDraw(p) then
			use.card = card
			use.to:append(p)
		end
	end
end

sgs.ai_use_value.WenrenCard = 5.4
sgs.ai_use_priority.WenrenCard = 9.2

sgs.ai_skill_playerchosen.zongluan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) then
			for _,q in sgs.list(destlist)do
				if p:inMyAttackRange(q) and self:isEnemy(q) and p:canSlash(q) then
					return p
				end
			end
		end
	end
end

sgs.ai_skill_playerschosen.zongluan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local tos = {}
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) then
			table.insert(tos,p)
		end
	end
	return tos
end

sgs.ai_skill_invoke.olxuhe = function(self,data)
	local has = sgs.ai_skill_invoke.xuhe(self,data)
	sgs.ai_skill_choice.olxuhe = sgs.ai_skill_choice.xuhe
	return has
end

sgs.ai_skill_choice.jiaoyu = function(self,choices,data)  --待补充
	local r = self.player:getMark("jiaoyuRed")
	if r>self.player:getEquips():length()/2 then
		return "red"
	end
	return "black"
end

sgs.ai_skill_askforag.siqi = function(self,card_ids)
	return card_ids[#card_ids]
end

sgs.ai_skill_invoke.siqi = function(self,data)
	return true
end

sgs.ai_skill_use["@@siqi"] = function(self,prompt)
	local tos,ts = {},{}
	for _,id in sgs.list(self.player:getTag("siqiForAI"):toIntList())do
		table.insert(ts,sgs.Sanguosha:getCard(id))
	end
    self:sortByUseValue(ts)
	for _,c in sgs.list(ts)do
		if c:isAvailable(self.player)
		and (c:isKindOf("Peach") or c:isKindOf("Analeptic") or c:isKindOf("ExNihilo") or c:isKindOf("EquipCard")) then
			local d = self:aiUseCard(c)
			if d.card then
				if d.to:length()<1 then return "@SiqiCard="..c:toString() end
				for _,p in sgs.list(d.to)do
					table.insert(tos,p:objectName())
				end
				return "@SiqiCard="..c:toString().."->"..table.concat(tos,"+")
			end
		end
	end
	self:sort(self.friends_noself)
	for _,c in sgs.list(ts)do
		for _,p in sgs.list(self.friends_noself)do
			if self.player:isProhibited(p,c) then continue end
			if c:isKindOf("Peach") then
				if c:isWounded() then
					return "@SiqiCard="..c:toString().."->"..p:objectName()
				end
			elseif c:isKindOf("ExNihilo") then
				if self:canDraw(p) then
					return "@SiqiCard="..c:toString().."->"..p:objectName()
				end
			elseif c:isKindOf("EquipCard") then
				local n = c:getRealCard():toEquipCard():location()
				if p:getEquip(n) then
					if #self:poisonCards({p:getEquip(n)},p)>0 then
						return "@SiqiCard="..c:toString().."->"..p:objectName()
					end
				else
					return "@SiqiCard="..c:toString().."->"..p:objectName()
				end
			elseif c:isKindOf("Analeptic") then
				if getCardsNum("Slash",p,self.player)>0 then
					return "@SiqiCard="..c:toString().."->"..p:objectName()
				end
			end
		end
	end
end

addAiSkills("qiaozhi").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
  	for _,h in sgs.list(cards)do
		if self:canDraw() and not self.player:isJilei(h) and self:getKeepValue(h)<6 then
			return sgs.Card_Parse("@QiaozhiCard="..h:toString())
		end
	end
end

sgs.ai_skill_use_func["QiaozhiCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.QiaozhiCard = 5.4
sgs.ai_use_priority.QiaozhiCard = 9.2

sgs.ai_skill_askforag.qiaozhi = function(self,card_ids)
	for _,id in sgs.list(card_ids)do
		local c = sgs.Sanguosha:getCard(id)
		local d = self:aiUseCard(c)
		if d.card then return id end
	end
end

sgs.ai_skill_playerchosen.choulie = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local to = nil
	local dc = dummyCard()
	dc:setFlags("Qinggang")
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and p:getHandcardNum()<=self.player:getCardCount()
		and self.player:canSlash(p,false) and self:slashIsEffective(dc,p) then
			if p:isLord() then
				return p
			end
			to = p
		end
	end
	return to
end

sgs.ai_skill_cardask["choulie1"] = function(self,data,pattern)
	local to = data:toPlayer()
	local dc = dummyCard()
	dc:setFlags("Qinggang")
	if not self:isFriend(to) and self:slashIsEffective(dc,to) then
		return true
	end
end

sgs.ai_skill_cardask["choulie2"] = function(self,data,pattern)
	return true
end

sgs.ai_skill_choice.fengwei = function(self,choices,data)  --待补充
	local r = self.player:getHp()
	if r>0 and r<5 then
		return ""..r
	end
	return "2"
end

addAiSkills("zonghu").getTurnUseCard = function(self)
	return sgs.Card_Parse("@ZonghuCard=.")
end

sgs.ai_skill_use_func["ZonghuCard"] = function(card,use,self)
	local dc = dummyCard()
	if not dc:isAvailable(self.player) then return end
	local n = self.player:getMark("&zonghu_lun")
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	local ids = {}
  	for _,h in sgs.list(cards)do
		if self:getKeepValue(h)<6 then
			table.insert(ids,h:getId())
			if #ids>n then break end
		end
	end
	if #ids<=n then return end
	local d = self:aiUseCard(dc)
	if d.card then use.to = d.to
	else return end
	if #self:poisonCards(ids)>#ids/2 then
		self:sort(self.enemies)
		for _,p in sgs.list(self.enemies)do
			use.card = sgs.Card_Parse("@ZonghuCard="..table.concat(ids,"+")..":slash")
			self.zonghuTo = p
			return
		end
	end
	self:sort(self.friends_noself)
  	for _,p in sgs.list(self.friends_noself)do
		if self:canDraw(p) then
			self.zonghuTo = p
			use.card = sgs.Card_Parse("@ZonghuCard="..table.concat(ids,"+")..":slash")
			return
		end
	end
  	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if self:isEnemy(p) then continue end
		self.zonghuTo = p
		use.card = sgs.Card_Parse("@ZonghuCard="..table.concat(ids,"+")..":slash")
		return
	end
end

sgs.ai_use_value.ZonghuCard = 5.4
sgs.ai_use_priority.ZonghuCard = 1.2

sgs.ai_skill_playerchosen.zonghu = function(self,players)
    for _,p in sgs.list(players)do
		if self.zonghuTo==p then
			return p
		end
	end
end

sgs.ai_guhuo_card.zonghu = function(self,toname,class_name)
	if self:getCardsNum(class_name)>0 then return end
	local n = self.player:getMark("&zonghu_lun")
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	local ids = {}
  	for _,h in sgs.list(cards)do
		if self:getKeepValue(h)<6 then
			table.insert(ids,h:getId())
			if #ids>n then break end
		end
	end
	if #ids<=n then return end
	if #self:poisonCards(ids)>#ids/2 then
		self:sort(self.enemies)
		for _,p in sgs.list(self.enemies)do
			self.zonghuTo = p
			return "@ZonghuCard="..table.concat(ids,"+")..":"..toname
		end
	end
	self:sort(self.friends_noself)
  	for _,p in sgs.list(self.friends_noself)do
		if self:canDraw(p) then
			self.zonghuTo = p
			return "@ZonghuCard="..table.concat(ids,"+")..":"..toname
		end
	end
  	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if self:isEnemy(p) then continue end
		self.zonghuTo = p
		return "@ZonghuCard="..table.concat(ids,"+")..":"..toname
	end
end

sgs.ai_skill_cardask["olbiluan0"] = function(self,data,pattern)
	if self:isWeak() or #self.enemies>0 then
		return true
	end
end

sgs.ai_skill_choice.ollixia = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	if table.contains(items,"ollixia3")
	and self:isFriend(to) and to:isWounded()
	then return "ollixia3" end
	if table.contains(items,"ollixia1")
	and self:canDraw()
	then return "ollixia1" end
	if table.contains(items,"ollixia2")
	and self:isFriend(to) and self:canDraw(to)
	then return "ollixia2" end
	if table.contains(items,"ollixia1")
	then return "ollixia1" end
	return items[#items]
end

sgs.ai_skill_askforag.xutu2 = function(self,card_ids)
	local cs = self:poisonCards(card_ids)
	if #cs>0 then return cs[1]:getId() end
	for _,id in sgs.list(card_ids)do
		table.insert(cs,sgs.Sanguosha:getCard(id))
	end
	self:sortByKeepValue(cs)
	return cs[1]:getId()
end

addAiSkills("lunzhan").getTurnUseCard = function(self)
  	local mt = ""
	for _,m in ipairs(self.player:getMarkNames())do
		if m:startsWith("&lunzhan+:+") then
			mt = m
			break
		end
	end
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	local ids = {}
  	for _,h in sgs.list(cards)do
		if string.find(mt,tostring(#ids)) then
			table.insert(ids,h:getId())
			if #ids>5 then break end
		else
			if #ids<1 then return sgs.Card_Parse("@LunzhanCard=.") end
			return sgs.Card_Parse("@LunzhanCard="..table.concat(ids,"+"))
		end
	end
end

sgs.ai_skill_use_func["LunzhanCard"] = function(card,use,self)
	local dc = dummyCard("Duel")
	dc:setSkillName("lunzhan")
	dc:addSubcards(card:getSubcards())
	if not dc:isAvailable(self.player) then return end
	local nts = {}
  	for _,p in sgs.list(self.room:getAlivePlayers())do
		if p:getMark("lunzhanBan-Clear")>0 then
			table.insert(nts,p)
		end
	end
	local d = self:aiUseCard(dc,dummy(nil,0,nts))
	if d.card then
		use.card = card
		use.to = d.to
	end
end

sgs.ai_use_value.LunzhanCard = 5.4
sgs.ai_use_priority.LunzhanCard = 2.2

sgs.ai_skill_invoke.lunzhan = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_invoke.fulve = function(self,data)
	local use = data:toCardUse()
	if use.to then
		return not self:isFriend(use.to:last())
	end
end

addAiSkills("lucun").getTurnUseCard = function(self)
  	for _,pn in sgs.list(RandomList(patterns()))do
		if self.player:getMark("lucun_guhuo_remove_"..pn.."_lun")>0 then continue end
		local dc = dummyCard(pn,"lucun")
		if (dc:getTypeId()==1 or dc:isNDTrick())
		and dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then
				self.lucunTo = d.to
				sgs.ai_use_priority.LucunCard = sgs.ai_use_priority[dc:getClassName()]
				return sgs.Card_Parse("@LucunCard=.:"..pn)
			end
		end
	end
end

sgs.ai_skill_use_func["LucunCard"] = function(card,use,self)
	if self.lucunTo then
		use.to = self.lucunTo
		use.card = card
	end
end

sgs.ai_use_value.LucunCard = 5.4
sgs.ai_use_priority.LucunCard = 9.2

sgs.ai_guhuo_card.lucun = function(self,toname,class_name)
	if self.player:getMark("lucun_guhuo_remove_"..toname.."_lun")>0
	or self:getCardsNum(class_name)>0 then return end
	return "@LucunCard=.:"..toname
end

sgs.ai_skill_invoke.tuisheng = function(self,data)
	return self:isWeak() and self:getAllPeachNum()<1
end

sgs.ai_skill_choice.tuisheng = function(self,choices,data)
	local items = choices:split("+")
	local to = self.room:getCurrent()
	if table.contains(items,"tuisheng2")
	and not self:isEnemy(to)
	then return "tuisheng2" end
	return items[1]
end

sgs.ai_skill_playerchosen.pengbi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) then
			return p
		end
	end
    for _,p in sgs.list(destlist)do
		if not self:isEnemy(p) then
			return p
		end
	end
end

addAiSkills("dici").getTurnUseCard = function(self)
	return sgs.Card_Parse("@DiciCard=.")
end

sgs.ai_skill_use_func["DiciCard"] = function(card,use,self)
	self:sort(self.friends_noself)
  	for _,p in sgs.list(self.friends_noself)do
		if self:isWeak(p) and p:isWounded() and p:getHandcardNum()>0 then
			use.card = card
			use.to:append(p)
			return
		end
	end
	self:sort(self.enemies)
  	for _,p in sgs.list(self.enemies)do
		if not p:isWounded() and p:getHandcardNum()>0 then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.DiciCard = 5.4
sgs.ai_use_priority.DiciCard = 6.2

sgs.ai_skill_discard.sibing = function(self,discard_num,min_num,optional,include_equip)
	local use = self.room:getTag("sibingData"):toCardUse()
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	local ids = {}
	if use.from==self.player then
		if self:isFriend(use.to:last()) then return {} end
		for i,c in sgs.list(cards)do
			if i<=#cards/2 and c:isRed() then
				table.insert(ids,c:getId())
			end
		end
	else
		for i,c in sgs.list(cards)do
			if i<=#cards/2 and #ids<min_num then
				table.insert(ids,c:getId())
			end
		end
		if #ids<min_num or not self:isWeak() then return {} end
	end
	return ids
end

sgs.ai_skill_use["@@sibing"] = function(self,prompt)
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards,nil,"j") -- 按保留值排序
	for _,h in sgs.list(cards)do
    	if h:isBlack() then
			local dc = dummyCard()
			local d = self:aiUseCard(dc)
			if d.card then
				local tps = {}
				for _,p in sgs.list(d.to)do
					table.insert(tps,p:objectName())
				end
				return "@SibingCard="..h:toString().."->"..table.concat(tps,"+")
			end
			break
		end
	end
end

sgs.ai_skill_invoke.liance = function(self,data)
	return self:canDraw()
end

addAiSkills("ol2shanjia").getTurnUseCard = function(self)
	return sgs.Card_Parse("@OL2ShanjiaCard=.:slash")
end

sgs.ai_skill_use_func["OL2ShanjiaCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.OL2ShanjiaCard = 5.4
sgs.ai_use_priority.OL2ShanjiaCard = 2.2

sgs.ai_skill_use["@@ol2shanjia"] = function(self,prompt)
    local c = dummyCard()
	c:setSkillName("_ol2shanjia")
    local dummy = self:aiUseCard(c)
   	if dummy.card then
      	local tos = {}
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
       	return c:toString().."->"..table.concat(tos,"+")
    end
end

sgs.ai_skill_playerchosen.liantao = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) then
			return p
		end
	end
end

sgs.ai_skill_playerchosen.olmomingcha = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p) then
			return p
		end
	end
	return destlist[#destlist]
end

sgs.ai_skill_use["@@huanhuo"] = function(self,prompt)
	local destlist = sgs.QList2Table(self.room:getAlivePlayers()) -- 将列表转换为表
	self:sort(destlist,nil,true)
	local tps = {}
	for _,p in sgs.list(destlist)do
		if #tps<2 and not self:isFriend(p) then
			table.insert(tps,p:objectName())
		end
	end
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards,nil,"j") -- 按保留值排序
	local ids = {}
	for _,h in sgs.list(cards)do
    	table.insert(ids,h:toString())
		if #ids==#tps then
			return "@HuanhuoCard="..table.concat(ids,"+").."->"..table.concat(tps,"+")
		end
	end
end

sgs.ai_skill_invoke.olqingshi = function(self,data)
	return not self:isWeak() and #self.enemies>0
end

sgs.ai_skill_use["@@olqingshi"] = function(self,prompt)
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards,nil,"j") -- 按保留值排序
	local use = self.player:getTag("olqingshiUse"):toCardUse()
	local bps = {}
	local destlist = sgs.QList2Table(self.room:getAlivePlayers()) -- 将列表转换为表
	for _,p in sgs.list(destlist)do
		if p:hasFlag("olqingshiTo") then continue end
		table.insert(bps,p)
	end
	self:sort(destlist)
	for _,h in sgs.list(cards)do
		local d = self:aiUseCard(use.card,dummy(true,0,bps))
		if d.card and d.to:length()>0 and d.to:first()~=use.to:first() then
			return "@OLQingshiCard="..h:toString().."->"..d.to:first():objectName()
		end
		if self:isEnemy(use.from) and self:isFriend(use.to:first()) then
			for _,p in sgs.list(destlist)do
				if p:hasFlag("olqingshiTo") and self:isEnemy(p) then
					return "@OLQingshiCard="..h:toString().."->"..p:objectName()
				end
			end
			for _,p in sgs.list(destlist)do
				if p:hasFlag("olqingshiTo") and not self:isFriend(p) then
					return "@OLQingshiCard="..h:toString().."->"..p:objectName()
				end
			end
		end
		break
	end
end

addAiSkills("kuanmo").getTurnUseCard = function(self)
	return not self:isWeak() and sgs.Card_Parse("@KuanmoCard=.")
end

sgs.ai_skill_use_func["KuanmoCard"] = function(card,use,self)
	self:sort(self.enemies)
  	for _,p in sgs.list(self.enemies)do
		if self.player:inMyAttackRange(p) then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.KuanmoCard = 5.4
sgs.ai_use_priority.KuanmoCard = 6.2

sgs.ai_skill_use["@@kuanmo!"] = function(self,prompt)
	self:sort(self.enemies)
  	for _,p in sgs.list(self.enemies)do
		if self.player:inMyAttackRange(p) then
			return "@KuanmoCard=.->"..p:objectName()
		end
	end
	local destlist = sgs.QList2Table(self.room:getAlivePlayers()) -- 将列表转换为表
	self:sort(destlist)
	for _,p in sgs.list(destlist)do
		if not self:isFriend(p) then
			return "@KuanmoCard=.->"..p:objectName()
		end
	end
	for _,p in sgs.list(destlist)do
		if p~=self.player then
			return "@KuanmoCard=.->"..p:objectName()
		end
	end
end

addAiSkills("gangqian").getTurnUseCard = function(self)
	if self.player:getMark("gangqianQi")>0 then
		local bps = {}
		for _,p in sgs.list(self.room:getAlivePlayers())do
			if self.player:isAdjacentTo(p) then continue end
			table.insert(bps,p)
		end
		local dc = dummyCard("fire_slash","gangqian")
		local cards = self.player:getCards("he")
		cards = self:sortByKeepValue(cards) -- 按保留值排序
		for _,c in sgs.list(cards)do
			if c:getTypeId()==3 then
				dc:addSubcard(c)
				if dc:isAvailable(self.player) then
					local d = self:aiUseCard(dc,dummy(true,0,bps))
					if d.card then
						self.gangqianUse = d
						return sgs.Card_Parse("@GangqianCard="..c:toString())
					end
				end
				dc:clearSubcards()
			end
		end
		return
	end
    local cards = self.player:getCards("h")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
	local dc = dummyCard("duel","gangqian")
	for _,c in sgs.list(cards)do
		if c:getTypeId()==2 then
			dc:addSubcard(c)
			if dc:isAvailable(self.player) then
				return dc
			end
			dc:clearSubcards()
		end
	end
end

sgs.ai_skill_use_func["GangqianCard"] = function(card,use,self)
	use.card = card
	use.to = self.gangqianUse.to
end

sgs.ai_use_value.GangqianCard = 5.4
sgs.ai_use_priority.GangqianCard = 2.2

addAiSkills("miluo").getTurnUseCard = function(self)
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
	local ids = {}
	local tps = {}
	for i=0,9 do
		local c,p = self:getCardNeedPlayer(cards,false)
		if c and p then
			if not table.contains(tps,p:objectName()) then
				table.insert(ids,c:toString())
				table.insert(tps,p:objectName())
				if #tps>1 then break end
			end
			table.removeOne(cards,c)
			if #cards<1 then break end
		end
	end
	if #tps<1 and #cards>1 then
		local destlist = sgs.QList2Table(self.room:getOtherPlayers(self.player)) -- 将列表转换为表
		self:sort(destlist)
		ids = {cards[1]:toString()}
		tps = {destlist[1]:objectName()}
	end
	if #tps>0 then
		self.miluo_to = tps
		return sgs.Card_Parse("@MiluoCard="..table.concat(ids,"+"))
	end
end

sgs.ai_skill_use_func["MiluoCard"] = function(card,use,self)
	use.card = card
	use.to = self.miluo_to
end

sgs.ai_use_value.MiluoCard = 5.4
sgs.ai_use_priority.MiluoCard = 1.2

sgs.ai_skill_choice.miluo = function(self,choices,data)
	local items = choices:split("+")
	for _,t in sgs.list(items)do
		if t:startsWith("miluo1") then
			for _,p in sgs.list(self.enemies)do
				if t:endsWith(p:objectName()) and self:isWeak(p) then
					return t
				end
			end
		else
			for _,p in sgs.list(self.friends)do
				if t:endsWith(p:objectName()) and self:isWeak(p) then
					return t
				end
			end
		end
	end
	for _,t in sgs.list(items)do
		if t:startsWith("miluo1") then
			for _,p in sgs.list(self.enemies)do
				if t:endsWith(p:objectName()) then
					return t
				end
			end
		else
			for _,p in sgs.list(self.friends)do
				if t:endsWith(p:objectName()) then
					return t
				end
			end
		end
	end
	return items[#items]
end

addAiSkills("oljueyan").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByUseValue(cards) -- 按保留值排序
	for _,c in sgs.list(cards)do
		if c:isNDTrick() or c:getTypeId()==1 and c:getSuit()==2 then
			if self:getCardsNum(c:getClassName())>1 then continue end
			local dc = dummyCard(c:objectName(),"oljueyan")
			if dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				if d.card then
					self.oljueyanUse = d
					sgs.ai_use_priority.OLJueyanCard = sgs.ai_use_priority[c:getClassName()]
					return sgs.Card_Parse("@OLJueyanCard="..c:toString()..":"..c:objectName())
				end
			end
		end
	end
end

sgs.ai_skill_use_func["OLJueyanCard"] = function(card,use,self)
	use.card = card
	use.to = self.oljueyanUse.to
end

sgs.ai_use_value.OLJueyanCard = 5.4
sgs.ai_use_priority.OLJueyanCard = 2.2

sgs.ai_guhuo_card.oljueyan = function(self,toname,class_name)
	if self:getCardsNum(class_name)>1 then return end
	for _,c in sgs.list(self.player:getCards("he"))do
		if c:isKindOf(class_name) then
			return "@OLJueyanCard="..c:toString()..":"..toname
		end
	end
end

addAiSkills("suyi").getTurnUseCard = function(self)
	return sgs.Card_Parse("@SuyiCard=.:slash")
end

sgs.ai_skill_use_func["SuyiCard"] = function(card,use,self)
	local dc = dummyCard()
	local d = self:aiUseCard(dc)
	if d.card then
		use.card = card
		use.to:append(d.to:first())
		return
	end
	self:sort(self.enemies)
    for _,p in sgs.list(self.enemies)do
		if self.player:canSlash(p,false) and self:slashIsEffective(dc,p) then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.SuyiCard = 5.4
sgs.ai_use_priority.SuyiCard = 2.2

addAiSkills("xiewei").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards) -- 按保留值排序
	local dc = dummyCard(nil,"xiewei")
	local ids = {}
	for _,c in sgs.list(cards)do
		if table.contains(self.toUse,c) then continue end
		table.insert(ids,c:toString())
		if #ids==2 and dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then
				self.xieweiUse = d
				return sgs.Card_Parse("@XieweiCard="..table.concat(ids,"+")..":slash")
			end
		end
	end
end

sgs.ai_skill_use_func["XieweiCard"] = function(card,use,self)
	use.card = card
	use.to = self.xieweiUse.to
end

sgs.ai_use_value.XieweiCard = 5.4
sgs.ai_use_priority.XieweiCard = 2.2

function sgs.ai_cardsview.xiewei(self,class_name,player)
	local cards = player:getCards("h")
	cards = self:sortByKeepValue(cards) -- 按保留值排序
	local ids = {}
	for _,c in sgs.list(cards)do
		table.insert(ids,c:toString())
		if #ids==2 then
			if class_name=="Jink" then
				return "@XieweiCard="..table.concat(ids,"+")..":jink"
			else
				return "@XieweiCard="..table.concat(ids,"+")..":slash"
			end
		end
	end
end

addAiSkills("youque").getTurnUseCard = function(self)
	local cards = {}
	for _,id in sgs.list(self.player:getPile("xw_er"))do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
	local n = 99
	for _,c in sgs.list(cards)do
		n = math.min(n,c:getNumber())
	end
	for _,c in sgs.list(cards)do
		if c:getNumber()==n then
			return sgs.Card_Parse("@YouqueCard="..c:toString())
		end
	end
end

sgs.ai_skill_use_func["YouqueCard"] = function(card,use,self)
	self:sort(self.enemies)
    for _,p in sgs.list(self.enemies)do
		if self.player:canPindian(p) and self.player:canSlash(p,false)
		and self:slashIsEffective(dummyCard(),p) then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.YouqueCard = 5.4
sgs.ai_use_priority.YouqueCard = 3.2

sgs.ai_skill_choice.kuangxin = function(self,choices,data)
	local items = choices:split("+")
	return items[math.random(1,2)]
end

sgs.ai_skill_invoke.leishi = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_playerchosen.leishi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and self:damageIsEffective(p,"F") then
			return p
		end
	end
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p) then
			return p
		end
	end
end

