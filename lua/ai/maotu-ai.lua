
sgs.ai_skill_choice.mtwenqi = function(self,choices,data)
	local items = choices:split("+")
	if not self.player:faceUp()
	then return items[2] end
	local to = data:toPlayer()
	local suits = {}
	for c,id in sgs.list(to:getPile("yhjyye"))do
		c = sgs.Sanguosha:getCard(id)
		if not table.contains(suits,c:getSuit())
		then table.insert(suits,c:getSuit()) end
	end
	suits = 4-#suits
	if to:objectName()==self.player:objectName()
	and suits>1 then return items[2] end
	if suits<2 then return items[1] end
end

sgs.ai_guhuo_card.mtzhihe = function(self,cname,class_name)
	local cs = sgs.IntList()
	local suits = {}
	for c,id in sgs.list(self.player:getPile("yhjyye"))do
		c = sgs.Sanguosha:getCard(id)
		if not table.contains(suits,c:getSuit())
		then table.insert(suits,c:getSuit()) end
	end
	suits = 4-#suits
	if suits<1 then suits = 1 end
	for d,h in sgs.list(self:sortByKeepValue(self.player:getCards("h"),nil,"l"))do
		if cs:length()<1 or h:getSuit()==sgs.Sanguosha:getCard(cs:at(0)):getSuit()
		then cs:append(h:getEffectiveId()) end
		if cs:length()>=suits
		then
			d = dummyCard(cname)
			d:setSkillName("mtzhihe")
			d:addSubcards(cs)
			return d
		end
	end
end

sgs.ai_fill_skill.mtzhihe = function(self)
    local cards = {}
	local suits = {}
	for c,id in sgs.list(self.player:getPile("yhjyye"))do
		c = sgs.Sanguosha:getCard(id)
		if not table.contains(suits,c:getSuit())
		then table.insert(suits,c:getSuit()) end
		if c:isKindOf("BasicCard") or c:isNDTrick()
		then table.insert(cards,c) end
	end
	if #cards<1 then return end
	suits = 4-#suits
	local cs = sgs.IntList()
	if suits<1 then suits = 1 end
	for d,h in sgs.list(self:sortByKeepValue(self.player:getCards("h"),nil,"l"))do
		if cs:length()<1 or h:getSuit()==sgs.Sanguosha:getCard(cs:at(0)):getSuit()
		then cs:append(h:getEffectiveId()) end
		if cs:length()>=suits then break end
	end
	if cs:length()<suits then return end
	suits = {}
	for d,c in sgs.list(cards)do
		d = dummyCard(c:objectName())
		d:setSkillName("mtzhihe")
		d:addSubcards(cs)
		local dc = self:aiUseCard(d)
		if dc.card
		then
			if dc.to:length()<1 and d:canRecast()
			then continue end
			return d
		end
	end
end

sgs.ai_skill_use["@@mtjiye"] = function(self,prompt)
    local cs = self.player:getCards("he")
    cs = self:sortByUseValue(cs)
    local tocs = {}
	local yhjyye = self.player:getPile("yhjyye")
	for can,c in sgs.list(self:poisonCards("e"))do
    	if #tocs<#cs/2
		then
			for d,id in sgs.list(yhjyye)do
				d = sgs.Sanguosha:getCard(id)
				if d:getSuit()==c:getSuit()
				then can = false break end
			end
			for d,id in sgs.list(tocs)do
				d = sgs.Sanguosha:getCard(id)
				if d:getSuit()==c:getSuit()
				then can = false break end
			end
			if can
			then
				table.insert(tocs,c:getEffectiveId())
			end
		end
	end
	for can,c in sgs.list(cs)do
    	if #tocs<#cs/2
		and (c:isKindOf("BasicCard") or c:isNDTrick())
		then
			for d,id in sgs.list(yhjyye)do
				d = sgs.Sanguosha:getCard(id)
				if d:getSuit()==c:getSuit()
				or d:objectName()==c:objectName()
				then can = false break end
			end
			for d,id in sgs.list(tocs)do
				d = sgs.Sanguosha:getCard(id)
				if d:getSuit()==c:getSuit()
				or d:objectName()==c:objectName()
				then can = false break end
			end
			if can
			then
				table.insert(tocs,c:getEffectiveId())
			end
		end
	end
	if #tocs>0
	then
    	return "@MTJiyeCard="..table.concat(tocs,"+")
	end
end

sgs.ai_skill_cardask["@mtlunhuan-discard"] = function(self,data,pattern,prompt)
	local record = self.player:property("MTLunhuanSuits"):toString():split("+")
    local d = dummyCard()
	d:setSkillName("_mtlunhuan")
	local to = data:toPlayer()
	for _,c in sgs.list(self:sortByKeepValue(self.player:getCards("he")))do
		if table.contains(record,c:getSuitString())
		then
			d:addSubcard(c)
			table.removeOne(record,c:getSuitString())
		end
	end
    if d:subcardsLength()>=to:getHp()
	and d:subcardsLength()>=#record
	and d:subcardsLength()-to:getHp()<2
	and self:isEnemy(to) then return d:toString() end
	return "."
end

sgs.ai_skill_playerchosen.mtlunhuan = function(self,players)
	local destlist = self:sort(players,"hp")	
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and target:getHandcardNum()>=target:getHp()
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and target:getHandcardNum()>=target:getHp()
		then return target end
	end
end

sgs.ai_skill_cardask["@mtguqu-discard4"] = function(self,data,pattern,prompt)
	return sgs.ai_skill_cardask["@mtguqu-discard1"](self,data,pattern,prompt)
end

sgs.ai_skill_cardask["@mtguqu-discard3"] = function(self,data,pattern,prompt)
	return sgs.ai_skill_cardask["@mtguqu-discard1"](self,data,pattern,prompt)
end

sgs.ai_skill_cardask["@mtguqu-discard2"] = function(self,data,pattern,prompt)
	return sgs.ai_skill_cardask["@mtguqu-discard1"](self,data,pattern,prompt)
end

sgs.ai_skill_cardask["@mtguqu-discard1"] = function(self,data,pattern,prompt)
	local record = self.player:property("MTGuquSuits"):toString():split("+")
    local d = dummyCard()
	d:setSkillName("_mtguqu")
	local cs = {}
	for _,c in sgs.list(self:sortByKeepValue(self.player:getCards("he")))do
		if table.contains(record,c:getSuitString())
		then
			local can = true
			for _,d in sgs.list(cs)do
				if d:getSuit()==c:getSuit()
				then can = false break end
			end
			if can
			then
				table.insert(cs,c)
				d:addSubcard(c)
			end
		end
	end
    if #cs<3 and #record==#cs then return d:toString() end
	return "."
end

sgs.ai_fill_skill.mtguzhao = function(self)
	return sgs.Card_Parse("@MTGuzhaoCard=.")
end

sgs.ai_skill_use_func["MTGuzhaoCard"] = function(card,use,self)
	local mc = self:getMaxCard()
	if not (mc and mc:getNumber()>9) then return end
	self:sort(self.enemies,"hp")
	for _,p in sgs.list(self.enemies)do
		if self.player:canPindian(p)
		then
			if use.to:length()<1
			then
				use.card = card
				use.to:append(p)
			else
				for _,to in sgs.list(use.to)do
					if p:isAdjacentTo(to)
					then use.to:append(p) break end
				end
			end
			if use.to:length()>2
			then return end
		end
	end
end

sgs.ai_use_value.MTGuzhaoCard = 5.4
sgs.ai_use_priority.MTGuzhaoCard = 3.8

sgs.ai_skill_invoke.mtjiawei = function(self,data)
	local str = data:toString():split(":")
	local to = BeMan(self.room,str[2])
	if to and self:isFriend(to)
	then
		return self.player:getHandcardNum()>to:getHandcardNum()
	end
end

sgs.ai_skill_playerchosen.mtjiawei = function(self,players)
	local destlist = self:sort(players,"hp")	
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()>self.player:getHandcardNum()
		then return target end
	end
end

sgs.ai_skill_playerchosen.mtfengshang = function(self,players)
	local destlist = self:sort(players,"hp")	
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()<=self.player:getHandcardNum()
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		and target:getHandcardNum()<=self.player:getHandcardNum()
		then return target end
	end
end

sgs.ai_skill_invoke.mtrenyu_jin = function(self,data)
	local use = self.player:getTag("MTRenyuData"):toCardUse()
    if self:isFriend(use.from)
	then return true
	elseif self:isWeak()
	and use.card:isDamageCard()
	then return true end
end

sgs.ai_skill_invoke.mtrenyu = function(self,data)
	local to = data:toPlayer()
    if self:isFriend(to)
	then
		return self:doDisCard(to,"e") or self:isWeak(to)
	end
end

sgs.ai_skill_playerchosen.mtfuzhan = function(self,players)
	local destlist = self:sort(players,"hp")
	local mc = self:getMaxCard()
	if not (mc and mc:getNumber()>9)
	then return end
	for _,to in sgs.list(destlist)do
		if self:isEnemy(to)
		then return to end
	end
	for _,to in sgs.list(destlist)do
		if not self:isFriend(to)
		then return to end
	end
end

sgs.ai_skill_cardask["@mtfeiren"] = function(self,data)
	local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	local use = data:toCardUse()
   	for _,h in sgs.list(cards)do
   		if self:getUseValue(h)<self:getUseValue(use.card) and use.card:isAvailable(self.player)
		or use.card:subcardsLength()>1 then return h:getEffectiveId() end
	end
end

sgs.ai_view_as.mtfeiren = function(card,player,card_place,class_name)
	if card:isKindOf("EquipCard")
	and card_place~=sgs.Player_PlaceSpecial
	then
    	return("slash:mtfeiren[no_suit:0]="..card:getEffectiveId())
	end
end

sgs.ai_skill_use["@@mtrenyi2"] = function(self,prompt)
	local d = self.player:getMark("mtrenyi_id-Clear")-1
	d = sgs.Sanguosha:getCard(d)
    local c = dummyCard(d:objectName())
	c:setSkillName("_mtrenyi")
    local dummy = self:aiUseCard(c)
   	if dummy.card and dummy.to
   	then
      	local tos = {}
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
       	return c:toString().."->"..table.concat(tos,"+")
    end
end

sgs.ai_skill_invoke.mtrenyi = function(self,data)
    return true
end

sgs.ai_skill_askforag.mtrenyi = function(self,card_ids)
	for c,id in sgs.list(card_ids)do
		c = sgs.Sanguosha:getCard(id)
		c = dummyCard(c:objectName())
		if c and self:aiUseCard(c).card
		then return id end
	end
end

sgs.ai_skill_use["@@mtrenyi1"] = function(self,prompt)
    local cs = self.player:getCards("he")
    self:sortByUseValue(cs,true)
    local tocs = {}
	local n = self.player:getMark("mtrenyiShowNum-Clear")
	for _,c in sgs.list(cs)do
    	if #tocs<n
		then
			table.insert(tocs,c:getEffectiveId())
		end
	end
	cs = {}
	for _,p in sgs.list(self.friends_noself)do
		if #cs<n
		then
			table.insert(cs,p:objectName())
		end
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if #cs<n and not self:isEnemy(p) and #tocs<self.player:getCardCount()/2
		and not table.contains(cs,p:objectName())
		then table.insert(cs,p:objectName()) end
	end
	if #cs==#tocs
	and #tocs==n
	then
    	return "@MTRenyiCard="..table.concat(tocs,"+").."->"..table.concat(cs,"+")
	end
end

sgs.ai_skill_discard.mtdianpei = function(self,x,n)
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	local to_cards = {}
   	for _,h in sgs.list(cards)do
   		if #to_cards<n
		then
         	table.insert(to_cards,h:getEffectiveId())
		end
	end
	local to = self.room:getCurrent()
	if #to_cards<#cards/2 and self:isEnemy(to)
	and to:getHp()<3 then return to_cards end
	return {}
end

sgs.ai_skill_playerchosen.mtdianpei = function(self,players)
	local destlist = self:sort(players,"card")	
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self.player:getHandcardNum()<self.player:getHp()
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_choice.mtzhilie = function(self,choices,data)
	local items = choices:split("+")
	for d,c in sgs.list(items)do
		d = c:split("=")
		if c:startsWith("damage")
		and self:damageIsEffective(BeMan(self.room,d[3]),"N",self.player)
		then return c end
	end
	for d,c in sgs.list(items)do
		d = c:split("=")
		if c:startsWith("use")
		and self:hasTrickEffective(dummyCard(d[3]),BeMan(self.room,d[2]),self.player)
		then return c end
	end
	return items[1]
end

sgs.ai_fill_skill.mtzhilie = function(self)
	return sgs.Card_Parse("@MTZhilieCard=.")
end

sgs.ai_skill_use_func["MTZhilieCard"] = function(card,use,self)
	for _,p in sgs.list(self.enemies)do
		if p:getHandcardNum()>0
		and use.to
		then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.MTZhilieCard = 5.4
sgs.ai_use_priority.MTZhilieCard = 4.8

sgs.ai_skill_invoke.mtguanda = function(self,data)
    return true
end

sgs.ai_skill_invoke.mtweiqie = function(self,data)
    return true
end

sgs.ai_skill_use["@@mtweiqie"] = function(self,prompt)
    local cs = {}
    local tocs = {}
	for _,c in sgs.list(self.player:getTag("mtweiqieForAI"):toIntList())do
		table.insert(cs,sgs.Sanguosha:getCard(c))
	end
    self:sortByUseValue(cs)
	function Judge(a,b)
		if b==0 then return a
		else return Judge(b,a%b) end
	end
	for can,c in sgs.list(cs)do
		for _,d in sgs.list(tocs)do
			if Judge(c:getNumber(),d:getNumber())~=1
			then can = false break end
		end
    	if can
		then
			table.insert(tocs,c)
		end
	end
	cs = {}
	for _,c in sgs.list(tocs)do
		table.insert(cs,c:getEffectiveId())
	end
	if #cs>0
	then
    	return "@MTWeiqieCard="..table.concat(cs,"+")
	end
end

sgs.ai_skill_invoke.mtzhongyi = function(self,data)
	local str = data:toString():split(":")
	local to = BeMan(self.room,str[2])
	if to and str[1]=="draw"
	then
		return self:isFriend(to)
	end
	if to and str[1]=="current"
	then
		return not self:isFriend(to) or self:doDisCard(to,"e")
	end
end

sgs.ai_fill_skill.mtjieli = function(self)
    local cards = self.player:getCards("h")
	local cs = {}
	for _,h in sgs.list(self:sortByUseValue(cards,nil,"l"))do
		if #cs<1
		or h:getColor()==sgs.Sanguosha:getCard(cs[1]):getColor()
		then table.insert(cs,h:getEffectiveId()) end
	end
	if #cs<1 then return end
	return sgs.Card_Parse("@MTJieliCard="..table.concat(cs,"+"))
end

sgs.ai_skill_use_func["MTJieliCard"] = function(card,use,self)
	local d = dummyCard("duel")
	d:setSkillName("mtjieli")
	d:addSubcards(card:getSubcards())
	d = self:aiUseCard(d)
	if d.card
	and use.to
	then
		use.card = card
		use.to = d.to
	end
end

sgs.ai_use_value.MTJieliCard = 5.4
sgs.ai_use_priority.MTJieliCard = 2.8

sgs.ai_skill_choice.mtliaoshi = function(self,choices,data)
	local items = choices:split("+")
	return "recover"
end

sgs.ai_skill_playerchosen.mtnianchou = function(self,players)
	local destlist = sgs.QList2Table(players)
	self:sort(destlist,"card")
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self.player:distanceTo(target)>1
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_cardchosen.mtxianzheng = function(self,who,flags,method)
	return self.mtxianzheng_cid
end

sgs.ai_skill_playerchosen.mtxianzheng_to = function(self,players)
    return self.mtxianzheng_to
end

sgs.ai_skill_playerchosen.mtxianzheng_from = function(self,players)
    return sgs.QList2Table(players)[1]
end

sgs.ai_skill_invoke.mtxianzheng = function(self,data)
	local to = BeMan(self.room,data:toString():split(":")[2])
	self.mtxianzheng_cid = nil
	self.mtxianzheng_to = nil
	if to
	then
		local p,c = self:card_for_qiaobian(to)
		if p and c
		then
			self.mtxianzheng_to = p
			self.mtxianzheng_cid = c:getEffectiveId()
			return true
		end
	end
end

sgs.ai_skill_use["@@mtxianzheng"] = function(self,prompt)
	local cards = self:addHandPile("he")
	cards = self:sortByKeepValue(cards,nil,"l")
    local c = dummyCard()
	c:setSkillName("mtxianzheng")
	c:addSubcard(cards[1])
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

sgs.ai_skill_playerchosen.mttongyi = function(self,players)
    return self.mttongyi_to
end

sgs.ai_skill_choice.mtliaoshi = function(self,choices,data)
	local items = choices:split("+")
	self.mttongyi_to = nil
	if table.contains(items,"lose")
	and self.player:hasSkill("mttongyi")
	and not self:isWeak()
	then
		for _,ep in sgs.list(self.enemies)do
			if ep:getHp()<2
			and ep:getMark("mttongyi_target-Keep")<1
			then
				self.mttongyi_to = ep
				return "lose"
			end
		end
	end
	if table.contains(items,"discard")
	and self.player:hasSkill("mttongyi")
	and self.player:getCardCount()>2
	then
		for _,ep in sgs.list(self.enemies)do
			if ep:getCardCount()==2
			and ep:getMark("mttongyi_target-Keep")<1
			then
				self.mttongyi_to = ep
				return "discard"
			end
		end
	end
	if table.contains(items,"recover")
	and self.player:hasSkill("mttongyi")
	and self.player:getCardCount()>2
	then
		for _,ep in sgs.list(self.friends_noself)do
			if self:isWeak(ep) and ep:getLostHp()>0
			and ep:getMark("mttongyi_target-Keep")<1
			then
				self.mttongyi_to = ep
				return "recover"
			end
		end
	end
	if table.contains(items,"recover")
	and self:isWeak()
	then
		return "recover"
	end
	return "draw"
end

