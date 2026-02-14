
sgs.ai_skill_invoke.yiyong = function(self,data)
	local target = data:toPlayer()
	if target then
		return self:isEnemy(target) or not self:isFriend(target) and not self:isWeak(target)
	end
end

sgs.ai_skill_cardask["@cuijin-discard"] = function(self,data,pattern)
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards) -- 按保留值排序
	local use = data:toCardUse()
   	for _,c in sgs.list(cards)do
    	if self.player:canDiscard(self.player,c:getEffectiveId())
		then
			for _,p in sgs.list(use.to)do
				if getCardsNum("Jink",p,self.player)<1 and self:isEnemy(p)
				then return c:getEffectiveId() end
			end
			for _,p in sgs.list(use.to)do
				if getCardsNum("Jink",p,self.player)>0 and self:isEnemy(use.from)
				then return c:getEffectiveId() end
			end
		end
	end
    return "."
end

sgs.ai_skill_use["@@jueman!"] = function(self,prompt)
	local cn = prompt:split(":")[2]
	cn = dummyCard(cn)
	local tos = {}
	if cn then
		cn:setSkillName("_jueman")
		local d = self:aiUseCard(cn)
		if d.card then
			for _,p in sgs.list(d.to)do
				table.insert(tos,p:objectName())
			end
			return cn:toString().."->"..table.concat(tos,"+")
		end
		if cn:targetFixed() then return cn:toString() end
		local players = self.room:getAlivePlayers()
		self:sort(players,"handcard")
		for _,p in sgs.list(players)do
			if self:isEnemy(p) and cn:isDamageCard()
			and self.player:canUse(cn,p,true)
			then table.insert(tos,p:objectName()) end
		end
		if #tos>0 then
			return cn:toString().."->"..table.concat(tos,"+")
		end
	end
end

addAiSkills("xiaosi").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByUseValue(cards)
  	for _,c in sgs.list(cards)do
		if c:isKindOf("BasicCard")
		and c:isAvailable(self.player)
		then
			local d = self:aiUseCard(c)
			if d.card then
				return sgs.Card_Parse("@XiaosiCard="..c:getEffectiveId())
			end
		end
	end
  	for d,c in sgs.list(cards)do
		if c:isKindOf("BasicCard")
		and c:isAvailable(self.player)
		then
			return sgs.Card_Parse("@XiaosiCard="..c:getEffectiveId())
		end
	end
  	for d,c in sgs.list(cards)do
		if c:isKindOf("BasicCard")
		and self.player:getHandcardNum()>2
		then
			return sgs.Card_Parse("@XiaosiCard="..c:getEffectiveId())
		end
	end
end

sgs.ai_skill_use_func["XiaosiCard"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>0
		and getKnownCard(self.player,ep,"BasicCard")>0
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.friends_noself)do
		local kc = getKnownCards(self.player,ep)
		for _,c in sgs.list(kc)do
			if c:isKindOf("BasicCard")
			and c:isAvailable(self.player)
			then
				d = self:aiUseCard(c)
				if d.card
				then
					use.card = card
					use.to:append(ep)
					return
				end
			end
		end
	end
	for _,ep in sgs.qlist(self.room:getOtherPlayers(self.player))do
		local kc = getKnownCards(self.player,ep)
		for _,c in sgs.list(kc)do
			if c:isKindOf("BasicCard")
			and c:isAvailable(self.player)
			then
				d = self:aiUseCard(c)
				if d.card
				then
					use.card = card
					use.to:append(ep)
					return
				end
			end
		end
	end
	for _,ep in sgs.qlist(self.room:getOtherPlayers(self.player))do
		local kc = getKnownCards(self.player,ep)
		for _,c in sgs.list(kc)do
			if c:isKindOf("BasicCard")
			and c:isAvailable(self.player)
			then
				use.card = card
				use.to:append(ep)
				return
			end
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

sgs.ai_use_value.XiaosiCard = 9.4
sgs.ai_use_priority.XiaosiCard = 7.8

sgs.ai_skill_discard.xiaosi = function(self,max,min,optional)
	local to_cards = {}
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	local to = self.player:getTag("XiaosiFrom"):toPlayer()
   	for _,c in sgs.list(cards)do
   		if #to_cards>=min then break end
		if c:getTypeId()~=1 then continue end
		if self:isFriend(to)
		then
			if c:isAvailable(to)
			then
				table.insert(to_cards,c:getEffectiveId())
			end
		else
			if not c:isAvailable(to)
			then
				table.insert(to_cards,c:getEffectiveId())
			end
		end
	end
   	for _,c in sgs.list(cards)do
   		if #to_cards>=min then break end
		if c:getTypeId()~=1 then continue end
		table.insert(to_cards,c:getEffectiveId())
	end
	return to_cards
end

sgs.ai_skill_use["@@xiaosi"] = function(self,prompt)
	local cn = self.player:property("XiaosiCards"):toString():split("+")
	local tos = {}
	if #cn>0 then
		local cs = {}
		for _,t in sgs.list(cn)do
			table.insert(cs,sgs.Card_Parse(t))
		end
		self:sortByUseValue(cs)
		for _,c in sgs.list(cs)do
			if c:isAvailable(self.player) then
				local d = self:aiUseCard(c)
				if d.card then
					for _,p in sgs.list(d.to)do
						table.insert(tos,p:objectName())
					end
					if c:canRecast() and d.to:length()<1 then continue end
					return c:toString().."->"..table.concat(tos,"+")
				end
			end
		end
	end
end

sgs.ai_skill_invoke.tenyearqingren = function(self,data)
	return true
end

sgs.ai_skill_use["@@xinggu"] = function(self,prompt)
	for _,id in sgs.list(self.player:getPile("xinggu"))do
		local c = sgs.Sanguosha:getCard(id)
		if c:getTypeId()~=3 then continue end
		if #self:poisonCards({c})>0 then
			for _,p in sgs.list(self.enemies)do
				local i = c:getRealCard():toEquipCard():location()
				if p:hasEquipArea(i) then
					return "@XingguCard="..id.."->"..p:objectName()
				end
			end
		else
			for _,p in sgs.list(self.friends_noself)do
				local i = c:getRealCard():toEquipCard():location()
				if p:hasEquipArea(i) then
					i = p:getEquip(i)
					if i and #self:poisonCards({i})<1 then continue end
					return "@XingguCard="..id.."->"..p:objectName()
				end
			end
		end
	end
end

sgs.ai_skill_invoke.hongji = function(self,data)
	local to = BeMan(self.room,data:toString():split(":")[2])
    return to and self:isFriend(to)
end

sgs.ai_skill_invoke.daili = function(self,data)
    if self.player:faceUp()
	then
		return self:canDraw() and (self:isWeak() or self.player:getHandcardNum()<3)
	else
		return self:canDraw()
	end
end

sgs.ai_skill_invoke.pitian = function(self,data)
    return self:canDraw()
end

sgs.ai_fill_skill.qiangzhizh = function(self)
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
	for _,ep in sgs.list(self.enemies)do
		if ep:getHp()<2 and #cards>3
		and self:damageIsEffective(ep,"N",self.player)
		then
			local pc = {}
			for _,c in sgs.list(cards)do
				if #pc>=3 then break end
				table.insert(pc,c:getEffectiveId())
			end
			return sgs.Card_Parse("@QiangzhiZHCard="..table.concat(pc,"+"))
		end
	end
	return #cards>0 and sgs.Card_Parse("@QiangzhiZHCard="..cards[1]:getEffectiveId())
end

sgs.ai_skill_use_func["QiangzhiZHCard"] = function(card,use,self)
	self:sort(self.friends_noself,"card",true)
	for _,ep in sgs.list(self.friends_noself)do
		local pc = self:poisonCards("e",ep)
		if #pc>1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:getHp()<2 and card:subcardsLength()>=3
		and self:damageIsEffective(ep,"N",self.player)
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self:sort(self.room:getOtherPlayers(self.player),"card"))do
		if ep:getCardCount()+card:subcardsLength()>3 and not self:isFriend(ep)
		and self:doDisCard(ep,"he")
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.QiangzhiZHCard = 5.4
sgs.ai_use_priority.QiangzhiZHCard = 5.8

sgs.ai_skill_askforyiji.libang = function(self,card_ids,targets)
	for _,p in sgs.list(targets)do
		if self:isFriend(p)
		then
			local cards = {}
			for c,id in sgs.list(card_ids)do
				table.insert(cards,sgs.Sanguosha:getCard(id))
			end
			self:sortByUseValue(cards) -- 按保留值排序
			return p,cards[1]:getEffectiveId()
		end
	end
	for _,p in sgs.list(targets)do
		if not self:isEnemy(p)
		then
			local cards = {}
			for c,id in sgs.list(card_ids)do
				table.insert(cards,sgs.Sanguosha:getCard(id))
			end
			self:sortByUseValue(cards) -- 按保留值排序
			return p,cards[1]:getEffectiveId()
		end
	end
end

sgs.ai_skill_use["@@libang"] = function(self,prompt)
	local d = dummyCard()
	d:setSkillName("_libang")
	local use = self:aiUseCard(d)
	if use.card and use.to then
		local tos = {}
		for _,p in sgs.list(use.to)do
			table.insert(tos,p:objectName())
		end
		return d:toString()"->"..table.concat(tos,"+")
	end
end

sgs.ai_fill_skill.libang = function(self)
    local cards = self.player:getCards("he")
    cards = self:sortByUseValue(cards,true,true) -- 按保留值排序
	return #cards>0 and sgs.Card_Parse("@LibangCard="..cards[1]:getEffectiveId())
end

sgs.ai_skill_use_func["LibangCard"] = function(card,use,self)
	self:sort(self.friends_noself,"card",true)
	for _,ep in sgs.list(self.friends_noself)do
		if use.to:length()<1
		and self:doDisCard(ep,"e")
		then use.to:append(ep) end
	end
	for _,ep in sgs.list(self:sort(self.room:getOtherPlayers(self.player),"card"))do
		if use.to:length()<2 and not self:isFriend(ep)
		and self:doDisCard(ep,"he")
		then use.to:append(ep) end
	end
	if use.to:length()>1
	then use.card = card end
end

sgs.ai_use_value.LibangCard = 5.4
sgs.ai_use_priority.LibangCard = 5.8

sgs.ai_skill_invoke.huayi = function(self,data)
    return true
end

sgs.ai_fill_skill.caizhuang = function(self)
	local valid = {}
    local cards = self.player:getCards("he")
    cards = self:sortByUseValue(cards,true) -- 按保留值排序
	for i,h in sgs.list(cards)do
		if #valid>#cards/2 then break end
		local can = true
		for _,id in sgs.list(valid)do
			if h:getSuit()==sgs.Sanguosha:getCard(id):getSuit()
			then can = false break end
		end
		if can then
			table.insert(valid,h:getEffectiveId())
		end
	end
	local suits = {}
	for _,h in sgs.list(self.player:getCards("h"))do
		if table.contains(valid,h:getEffectiveId()) or table.contains(suits,h:getSuit()) then continue end
		table.insert(suits,h:getSuit())
	end
	return #valid>#suits and sgs.Card_Parse("@CaizhuangCard="..table.concat(valid,"+"))
end

sgs.ai_skill_use_func["CaizhuangCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.CaizhuangCard = 7.4
sgs.ai_use_priority.CaizhuangCard = 5.8

sgs.ai_fill_skill.jijiao = function(self)
	local ids = self.player:getTag("JijiaoRecord"):toIntList()
	if ids:length()<2 then return end
	return #self.toUse<1 and sgs.Card_Parse("@JijiaoCard=.")
end

sgs.ai_skill_use_func["JijiaoCard"] = function(card,use,self)
	local ids = self.player:getTag("JijiaoRecord"):toIntList()
	local n,xt = 0,0
	for _,id in sgs.list(ids)do
		if self.room:getCardPlace(id)==sgs.Player_Discard then
			n = n+1
			if self:aiUseCard(sgs.Sanguosha:getCard(id)).card
			then xt = xt+1 end
			if xt>ids:length()/2
			and use.to then
				use.card = card
				use.to:append(self.player)
				return
			end
		end
	end
	if n<3 then return end
	self:sort(self.friends,"card",true)
	for _,ep in sgs.list(self.friends)do
		use.card = card
		use.to:append(ep)
		return
	end
end

sgs.ai_use_value.JijiaoCard = 6.4
sgs.ai_use_priority.JijiaoCard = 6.8

sgs.ai_skill_discard.huizhi = function(self,max,min)
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if p:getHandcardNum()<self.player:getHandcardNum() then continue end
		local cards = self.player:getCards("h")
		local to_cards = {}
		for _,h in sgs.list(self:sortByKeepValue(cards))do
			if #to_cards>min or #to_cards>cards:length()/2 then break end
			table.insert(to_cards,h:getEffectiveId())
		end
		return to_cards
	end
	return {}
end

sgs.ai_skill_choice.yuanmo = function(self,choices,data)
	local items = choices:split("+")
	if table.contains(items,"add")
	then
		local n = 0
		for _,p in sgs.list(self.room:getAlivePlayers())do
			if self.player:distanceTo(p)-1==self.player:getAttackRange()
			and self:doDisCard(p,nil,true)
			then
				n = n+1
			end
		end
		if n>1
		or n>0 and self.player:getAttackRange()<2
		then return "add" end
	end
	if table.contains(items,"reduce")
	and self.player:getAttackRange()>1
	then return "reduce" end
	return "cancel"
end

sgs.ai_skill_use["@@jianjiyh"] = function(self,prompt)
	local d = dummyCard()
	d:setSkillName("_jianjiyh")
	local use = self:aiUseCard(d)
	if use.card and use.to then
		local tos = {}
		for _,p in sgs.list(use.to)do
			table.insert(tos,p:objectName())
		end
		return d:toString().."->"..table.concat(tos,"+")
	end
end

sgs.ai_fill_skill.jianjiyh = function(self)
	return sgs.Card_Parse("@JianjiYHCard=.")
end

sgs.ai_skill_use_func["JianjiYHCard"] = function(card,use,self)
	self:sort(self.enemies,"card")
	for _,ep in sgs.list(self.enemies)do
		if ep:getCardCount()>0
		and use.to:length()<self.player:getAttackRange()
		then
			if use.to:length()<1
			then
				use.card = card
				use.to:append(ep)
				continue
			end
			for _,p in sgs.list(use.to)do
				if p:isAdjacentTo(ep)
				then
					use.to:append(ep)
					break
				end
			end
		end
	end
	self:sort(self.friends,"card",true)
	for _,ep in sgs.list(self.friends)do
		if ep:getCardCount()>4 and use.to:length()>0
		and use.to:length()<self.player:getAttackRange()
		then
			for _,p in sgs.list(use.to)do
				if p:isAdjacentTo(ep)
				then
					use.to:append(ep)
					break
				end
			end
		end
	end
	for _,ep in sgs.list(self:sort(self.room:getAlivePlayers(),"card"))do
		if ep:getCardCount()>0 and not self:isFriend(ep)
		and use.to:length()<self.player:getAttackRange()
		and not use.to:contains(ep)
		then
			if use.to:length()<1
			then
				use.card = card
				use.to:append(ep)
				continue
			end
			for _,p in sgs.list(use.to)do
				if p:isAdjacentTo(ep)
				then
					use.to:append(ep)
					break
				end
			end
		end
	end
end

sgs.ai_use_value.JianjiYHCard = 3.4
sgs.ai_use_priority.JianjiYHCard = 9.8

sgs.ai_skill_choice.xiangshuzk = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	if (not self:isFriend(to) or self:doDisCard(to,"ej"))
	and to:getHandcardNum()<=7
	then
		if to:getHandcardNum()>3
		then
			return tostring(to:getHandcardNum()-2)
		else
			return tostring(to:getHandcardNum()-1)
		end
	end
	return "cancel"
end

sgs.ai_skill_askforag.fozong = function(self,card_ids)
	if not self:isEnemy(self.room:getCurrent())
	then
		local cards = {}
		for c,id in sgs.list(card_ids)do
			table.insert(cards,sgs.Sanguosha:getCard(id))
		end
		self:sortByUseValue(cards) -- 按保留值排序
		return cards[1]:getEffectiveId()
	end
	return -1
end

sgs.ai_skill_playerchosen.cansi = function(self,players)
	local destlist = self:sort(players,"card")
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:hasTrickEffective(dummyCard(),target,self.player)
		and self:hasTrickEffective(dummyCard("duel"),target,self.player)
		and self:hasTrickEffective(dummyCard("fire_attack"),target,self.player)
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:hasTrickEffective(dummyCard("duel"),target,self.player)
		and self:hasTrickEffective(dummyCard("fire_attack"),target,self.player)
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:hasTrickEffective(dummyCard("duel"),target,self.player)
		and self:hasTrickEffective(dummyCard("fire_attack"),target,self.player)
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
    return destlist[1]
end

sgs.ai_skill_use["@@saowei"] = function(self,prompt)
	local d = dummyCard()
	d:setSkillName("saowei")
	for _,h in sgs.list(self:sortByUseValue(self.player:getHandcards(),true,true))do
		if h:hasTip("qrasai") then
			d:addSubcard(h)
			break
		end
	end
	local use = self:aiUseCard(d)
	if use.card and use.to then
		local tos = {}
		for _,p in sgs.list(use.to)do
			table.insert(tos,p:objectName())
		end
		return d:toString().."->"..table.concat(tos,"+")
	end
end

sgs.ai_skill_invoke.aishou = function(self,data)
    return self:canDraw()
end

sgs.ai_skill_discard.jinjin = function(self,max,min)
	local to_cards = {}
	local damage = self.player:getTag("JinjinData"):toDamage()
	if self:isEnemy(damage.from)
	and min<self.player:getCardCount()/2
	then
		local cards = self.player:getCards("he")
		for _,hcard in sgs.list(self:sortByKeepValue(cards))do
			if #to_cards>=min then break end
			table.insert(to_cards,hcard:getEffectiveId())
		end
	end
	return to_cards
end

sgs.ai_skill_invoke.jinjin = function(self,data)
    return self.player:getHp()>self.player:getHandcardNum()
end

sgs.ai_skill_discard.haochong = function(self,max,min)
	local to_cards = {}
	if min<2 then
		local cards = self.player:getCards("h")	
		for _,hcard in sgs.list(self:sortByKeepValue(cards))do
			if #to_cards>=min then break end
			table.insert(to_cards,hcard:getEffectiveId())
		end
	end
	return to_cards
end

sgs.ai_skill_invoke.haochong = function(self,data)
    return self:canDraw()
end

sgs.ai_fill_skill.tenyearlingyin = function(self)
	local valid = {}
    local cards = self.player:getCards("he")
    cards = self:sortByUseValue(cards,true,true) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if h:isKindOf("Weapon")
		or h:isKindOf("Armor")
		then
			local d = dummyCard("duel")
			d:setSkillName("tenyearlingyin")
			d:addSubcard(h)
			table.insert(valid,d)
		end
	end
	return #valid>0 and valid
end

sgs.ai_skill_use["@@tenyearlingyin"] = function(self,prompt)
	local valid = {}
    local cards1,cards2 = {},{}
	for _,id in sgs.list(self.player:getPile("tyrjwywang"))do
		local c = sgs.Sanguosha:getCard(id)
		if #cards1<1 or cards1[1]:getColor()==c:getColor()
		then table.insert(cards1,c)
		else table.insert(cards2,c) end
	end
	local n = self.player:getMark("tenyearlingyin_lun-PalyClear")
	if #cards1<=n
	then
		for _,c in sgs.list(cards1)do
			table.insert(valid,c:getEffectiveId())
		end
		self.player:addMark("lingyin_valid",#valid)
		return "@TenyearLingyinCard="..table.concat(valid,"+")
	end
	if #cards2<=n
	and #cards2>0
	then
		for _,c in sgs.list(cards2)do
			table.insert(valid,c:getEffectiveId())
		end
		self.player:addMark("lingyin_valid",#valid)
		return "@TenyearLingyinCard="..table.concat(valid,"+")
	end
	for _,c in sgs.list(cards1)do
		if #valid>=n then break end
		table.insert(valid,c:getEffectiveId())
	end
	self.player:addMark("lingyin_valid",#valid)
	return "@TenyearLingyinCard="..table.concat(valid,"+")
end

sgs.ai_skill_playerchosen.tenyearliying = function(self,players)
	local destlist = self:sort(players,"hp")
	local n = self.player:getMark("lingyin_valid")
	self.player:setMark("lingyin_valid",0)
	if n>1 then return end
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and self:canDraw(target)
		then return target end
	end
end

sgs.ai_skill_invoke.tenyearwangyuan = function(self,data)
    return true
end

sgs.ai_fill_skill.jinjianhe = function(self)
	local valid = {}
    local cards = self.player:getCards("he")
    cards = self:sortByUseValue(cards,true) -- 按保留值排序
	for i,h1 in sgs.list(cards)do
		for n,h2 in sgs.list(cards)do
			if i==n then continue end
			if h1:sameNameWith(h2)
			or h1:getTypeId()==3 and h2:getTypeId()==3 then
				table.insert(valid,h1:getEffectiveId())
				table.insert(valid,h2:getEffectiveId())
				return sgs.Card_Parse("@JinJianheCard="..table.concat(valid,"+"))
			end
		end
	end
end

sgs.ai_skill_use_func["JinJianheCard"] = function(card,use,self)
	self:sort(self.enemies,"card")
	for _,ep in sgs.list(self.enemies)do
		if ep:getMark("jinjianheTarget-PlayClear")<1 then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self:sort(self.room:getAlivePlayers(),"card"))do
		if ep:getMark("jinjianheTarget-PlayClear")<1
		and not self:isFriend(ep) then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.JinJianheCard = 3.4
sgs.ai_use_priority.JinJianheCard = 4.8

sgs.ai_use_revises.jinbihun = function(self,card,use)
	if not card:targetFixed()
	and self.player:getHandcardNum()-1>self.player:getMaxCards() then
		if card:isKindOf("BasicCard")
		or card:isKindOf("SingleTargetTrick") then
			for _,p in sgs.list(self.friends_noself)do
				if CanToCard(card,self.player,p) then
					use.card = card
					use.to:append(p)
					return
				end
			end
			return false
		end
	end
end

sgs.ai_skill_cardchosen.tenyearluochong = function(self,who,flags,method)
	if who:getMark("luochongChosen-Clear")<2
	then
		for _,c in sgs.list(who:getCards("ej"))do
			if self:doDisCard(who,c:getEffectiveId())
			then
				who:addMark("luochongChosen-Clear")
				return c:getEffectiveId()
			end
		end
		for _,p in sgs.list(self.room:getOtherPlayers(who))do
			if p:getMark("luochongChosen-Clear")<1
			and self:doDisCard(p,"ej")
			then return -1 end
		end
		if who:objectName()==self.player:objectName()
		then
			if who:getMark("luochongChosen-Clear")>0
			then return -1
			else
				who:addMark("luochongChosen-Clear")
				return self:getCardRandomly(who,"h")
			end
		end
		if self:doDisCard(who,"h")
		and who:getMark("luochongChosen-Clear")<2
		then
			who:addMark("luochongChosen-Clear")
			return self:getCardRandomly(who,"h")
		end
	end
	return -1
end

sgs.ai_skill_playerchosen.tenyearluochong = function(self,players)
	local destlist = self:sort(players,"hp")	
	for _,target in sgs.list(destlist)do
		if self:isFriend(target) and target:getMark("luochongChosen-Clear")<1
		and self:doDisCard(target,"ej")
		then return target end
	end
	if self.room:getDrawPile():length()>80
	and self.player:hasSkill("tenyearaichen")
	and self.player:getMark("luochongChosen-Clear")<1
	and players:contains(self.player)
	then return self.player end
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target) and target:getMark("luochongChosen-Clear")<1
		and self:doDisCard(target,"ej")
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target) and target:getMark("luochongChosen-Clear")<1
		and self:doDisCard(target,"hej")
		then return target end
	end
end

sgs.ai_skill_discard.tenyearjinjie = function(self,max,min)
	local to_cards = {}
    local to = self.room:getCurrent()
	if self:isEnemy(to)
	and self:hasTrickEffective(dummyCard(),to,self.player)
	then
		local cards = self.player:getCards("h")	
		for _,hcard in sgs.list(self:sortByKeepValue(cards))do
			if #to_cards>=min then break end
			table.insert(to_cards,hcard:getEffectiveId())
		end
	end
	return to_cards
end

sgs.ai_skill_invoke.tenyearsigong = function(self,data)
    local to = self.room:getCurrent()
	if self:isEnemy(to)
	and self:hasTrickEffective(dummyCard(),to,self.player)
	then return true end
end

sgs.ai_guhuo_card.tenyeargue = function(self,cname,class_name)
	local sj = 0
	for _,h in sgs.list(self.player:getCards("h"))do
		if h:isKindOf("Slash")
		or h:isKindOf("Jink")
		then sj = sj+1 end
	end
	return sj<2 and "@TenyearGueCard=.:"..cname
end

sgs.ai_skill_playerschosen.tenyearyuguan = function(self,players,x,n)
	local destlist = players
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
	self:sort(destlist,"hp")
	local tos = {}
	for _,to in sgs.list(destlist)do
		if #tos>=x then break end
		if self:isFriend(to) and to:getMaxHp()>to:getHandcardNum()
		then table.insert(tos,to) end
	end
	for _,to in sgs.list(destlist)do
		if #tos>=x then break end
		if not table.contains(tos,to)
		then table.insert(tos,to) end
	end
	return tos
end

sgs.ai_skill_invoke.tenyearyuguan = function(self,data)
	local lh = self.player:getLostHp()-1
	if lh>0
	then
		local n = math.max(0,(self.player:getMaxHp()-1)-self.player:getHandcardNum())
		if n>0 then lh = lh-1 end
		self:sort(self.friends_noself,"handcard")
		for _,p in sgs.list(self.friends_noself)do
			if lh<1 then break end
			local x = math.max(0,p:getMaxHp()-p:getHandcardNum())
			if x>0 then lh = lh-1 n = n+x end
		end
		return lh<1 and n>2
	end
end

sgs.ai_skill_playerchosen.tenyearxuewei = function(self,players)
	local destlist = self:sort(players,"hp")	
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and self:isWeak(target)
		then return target end
	end
	return self.player
end

sgs.ai_fill_skill.xiangmian = function(self)
	return sgs.Card_Parse("@XiangmianCard=.")
end

sgs.ai_skill_use_func["XiangmianCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard",true)
	for _,ep in sgs.list(self.enemies)do
		if ep:getMark("xiangmianTarget-Keep")<1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.XiangmianCard = 3.4
sgs.ai_use_priority.XiangmianCard = 9.8

sgs.ai_skill_use["@@tenyearjue"] = function(self,prompt)
	local use = dummy(true,99)
	use = self:aiUseCard(dummyCard(),use)
	if use.card and use.to then
		for _,p in sgs.list(use.to)do
			if p:getHp()==p:getMaxHp()
			then return "@TenyearJueCard=.->"..p:objectName() end
		end
	end
end

sgs.ai_skill_discard.tenyearjinjie = function(self,max,min)
	local to_cards = {}
	local dy = self.room:getCurrentDyingPlayer()
	if not (dy and self:isFriend(dy)) then return {} end
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards)
   	for _,h in sgs.list(cards)do
   		if #to_cards>=min then break end
		table.insert(to_cards,h:getEffectiveId())
	end
	return to_cards
end

sgs.ai_skill_invoke.tenyearjinjie = function(self,data)
	local to = BeMan(self.room,data:toString():split(":")[2])
    return to and self:isFriend(to)
end

sgs.ai_skill_use["@@tenyearzhaohan!"] = function(self,prompt)
	local valid,to = {},nil
	for _,p in sgs.list(self.friends_noself)do
		if p:isKongcheng() and not self:needKongcheng(p,true)
		then to = p break end
	end
    local cards = self.player:getCards("h")
    cards = self:sortByUseValue(cards,true) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if #valid>=2 then break end
    	table.insert(valid,h:getEffectiveId())
	end
	if #valid<2 then return end
	return "@TenyearZhaohanCard="..table.concat(valid,"+").."->"..to:objectName()
end

sgs.ai_skill_choice.tenyearzhaohan = function(self,choices,data)
	local items = choices:split("+")
	if table.contains(items,"give")
	then
		for _,p in sgs.list(self.friends_noself)do
			if p:isKongcheng()
			and not self:needKongcheng(p,true)
			then return "give" end
		end
	end
	return "discard"
end

sgs.ai_skill_invoke.tenyearzhaohan = function(self,data)
    return self:canDraw()
end

sgs.ai_skill_choice.juying = function(self,choices,data)
	local items = choices:split("+")
	if table.contains(items,"cishu")
	then return "cishu"
	elseif table.contains(items,"draw")
	and (self.player:getHp()>1 or self:isWeak())
	then return "draw" end
	if table.contains(items,"maxcards")
	and 4-#items<self.player:getHp()
	and self:getOverflow()>0
	then
		return "maxcards"
	end
	return "cancel"
end

sgs.ai_skill_playerchosen.anzhi = function(self,players)
	local destlist = self:sort(players,"card")
	for _,target in sgs.list(destlist)do
		if self:isFriend(target) and self:canDraw(target)
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
	self.player:addMark("ai_anzhi-PlayClear")
end

sgs.ai_fill_skill.anzhi = function(self)
	return (self.player:getMark("&xialei_watch-Clear")>1 or #self.toUse<1)
	and self.player:getMark("ai_anzhi-PlayClear")<1
	and sgs.Card_Parse("@AnzhiCard=.")
end

sgs.ai_skill_use_func["AnzhiCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.AnzhiCard = 3.4
sgs.ai_use_priority.AnzhiCard = 9.8

sgs.ai_skill_invoke.anzhi = function(self,data)
    return self.player:getMark("&xialei_watch-Clear")>1 or #self.toUse<1
end

sgs.ai_skill_invoke.xialei = function(self,data)
    return self:canDraw()
end

sgs.ai_skill_playerchosen.zhanmeng = function(self,players)
	local destlist = self:sort(players,"card")
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

sgs.ai_skill_choice.zhanmeng = function(self,choices,data)
	local items = choices:split("+")
	local use = data:toCardUse()
	if table.contains(items,"last")
	then return "last"
	elseif table.contains(items,"next")
	then
		if self.player:getMark("@extra_turn")>0
		then
			if self:getCardsNum(use.card:getClassName())>0
			then return "next" end
		else
			if getCardsNum(use.card:getClassName(),self.room:getCurrent():getNextAlive(),self.player)>0
			then return "next" end
		end
	end
	if table.contains(items,"discard")
	and #self.enemies>0
	then
		return "discard"
	end
	
	return "cancel"
end

sgs.ai_skill_playerchosen.wumei = function(self,players)
	local destlist = self:sort(players,"handcard",true)	
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_playerschosen.tingxian = function(self,players,x,n)
	local destlist = self:sort(players,"hp")	
	local tos = {}
	for _,to in sgs.list(destlist)do
		if #tos<x and self:isFriend(to)
		and not self:isPriorFriendOfSlash(to,dummyCard())
		then table.insert(tos,to) end
	end
	return tos
end

sgs.ai_skill_invoke.tingxian = function(self,data)
	for _,ep in sgs.list(self.friends_noself)do
		if self.player:inMyAttackRange(ep)
		then return true end
	end
    return self:canDraw()
end

sgs.ai_target_revises.enyu = function(to,card,self,use)
	return to:getMark("enyu_target_"..card:objectName().."-Clear")>0
end

sgs.ai_fill_skill.jingzao = function(self)
	return sgs.Card_Parse("@JingzaoCard=.")
end

sgs.ai_skill_use_func["JingzaoCard"] = function(card,use,self)
	self:sort(self.friends_noself,"handcard",true)
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getMark("jingzao_target-PlayClear")>0 then continue end
		if ep:getHandcardNum()>4
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self:sort(self.room:getOtherPlayers(self.player),"handcard",true))do
		if ep:getMark("jingzao_target-PlayClear")>0 then continue end
		use.card = card
		use.to:append(ep)
		return
	end
end

sgs.ai_use_value.JingzaoCard = 3.4
sgs.ai_use_priority.JingzaoCard = 9.8

sgs.ai_guhuo_card.fengying = function(self,cname,class_name)
	local d = dummyCard(cname)
	if d
	then
		d:setSkillName("fengying")
		local cards = self.player:getCards("h")
		cards = self:sortByKeepValue(cards,nil,true)
		for _,h in sgs.list(cards)do
			if h:getNumber()<=self.player:getMark("&dgrlzjiao")
			then
				d:addSubcard(h)
				return d:toString()
			end
		end
	end
end

sgs.ai_fill_skill.fengying = function(self)
	local record = self.player:property("SkillDescriptionRecord_fengying"):toString():split("+")
	if #record<1 then return end
	local cancs = {}
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards,nil,true)
  	for c,h in sgs.list(cards)do
		if h:getNumber()>self.player:getMark("&dgrlzjiao")
		then else table.insert(cancs,h:getEffectiveId()) break end
	end
	if #cancs<1 then return end
	local records = {}
  	for _,cn in sgs.list(record)do
		local c = dummyCard(cn)
		c:setSkillName("fengying")
		c:addSubcard(cancs[1])
		if c:isAvailable(self.player)
		then
			local uc = self:aiUseCard(c)
			if uc.card then
				self.fengying_to = uc.to
				if c:canRecast() and uc.to:length()<1 then continue end
				sgs.ai_use_priority.FengyingCard = sgs.ai_use_priority[c:getClassName()]
				return sgs.Card_Parse("@FengyingCard="..cancs[1]..":"..cn)
			end
		end
	end
end

sgs.ai_skill_use_func["FengyingCard"] = function(card,use,self)
	use.card = card
	use.to = self.fengying_to
end

sgs.ai_use_value.FengyingCard = 3.4
sgs.ai_use_priority.FengyingCard = 9.8

sgs.ai_skill_playerchosen.lianzhi_shouze = function(self,players)
	local destlist = self:sort(players,"hp",true)	
	for _,to in sgs.list(destlist)do
		if self:isFriend(to)
		then return to end
	end
	for _,to in sgs.list(destlist)do
		if not self:isEnemy(to)
		then return to end
	end
    return destlist[1]
end

sgs.ai_skill_playerchosen.lianzhi = function(self,players)
	local destlist = self:sort(players,"hp",true)	
	for _,to in sgs.list(destlist)do
		if self:isFriend(to)
		then return to end
	end
	for _,to in sgs.list(destlist)do
		if not self:isEnemy(to)
		then return to end
	end
    return destlist[1]
end

sgs.ai_skill_choice.zuojian = function(self,choices,data)
	local items = choices:split("+")
	local maxE,minE = 0,0
	for i,p in sgs.list(self.room:getAlivePlayers())do
		if p:getEquips():length()>self.player:getEquips():length()
		then
			if self:isEnemy(p) then maxE = maxE-1
			else maxE = maxE+1 end
		elseif p:getEquips():length()<self.player:getEquips():length()
		then
			if self:isFriend(p) then maxE = maxE-1
			else maxE = maxE+1 end
		end
	end
	if maxE>0 then return "draw"
	elseif minE>0
	then return "discard" end
	if maxE>=0 then return "draw"
	elseif minE>=0
	then return "discard" end
	return "draw"
end

sgs.ai_skill_invoke.zhengxu = function(self,data)
	local str = data:toString()
	if str:match("draw") then
		return self:canDraw()
	else
		return not self:needToLoseHp()
	end
end

sgs.ai_fill_skill.cuichuan = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		if #cards>2 or self:getOverflow()>0
		then return sgs.Card_Parse("@CuichuanCard="..c:getEffectiveId()) end
	end
end

sgs.ai_skill_use_func["CuichuanCard"] = function(card,use,self)
	self:sort(self.friends,"hp")
	for _,ep in sgs.list(self.friends)do
		if ep:hasEquip()
		and ep:getEquips():length()<4
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.friends)do
		if ep:hasEquipArea()
		and ep:getEquips():length()<4
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self:sort(self.room:getAlivePlayers(),"equip"))do
		if ep:hasEquipArea() and not self:isEnemy(ep)
		and ep:getEquips():length()<4
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.CuichuanCard = 3.4
sgs.ai_use_priority.CuichuanCard = 4.8

sgs.ai_fill_skill.kanji = function(self)
	local suits = {}
  	for _,c in sgs.list(self.player:getCards("h"))do
		if table.contains(suits,c:getSuit()) then return end
		table.insert(suits,c:getSuit())
	end
	return sgs.Card_Parse("@KanjiCard=.")
end

sgs.ai_skill_use_func["KanjiCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.KanjiCard = 3.4
sgs.ai_use_priority.KanjiCard = 9.8


sgs.ai_skill_playerchosen.silve = function(self,players)
	local destlist = self:sort(players,nil,true)	
	for _,to in sgs.list(destlist)do
		if self:isFriend(to)
		then return to end
	end
	for _,to in sgs.list(destlist)do
		if not self:isEnemy(to)
		then return to end
	end
    return destlist[1]
end

sgs.ai_skill_invoke.silve = function(self,data)
	local damage = data:toDamage()
	return self:doDisCard(damage.to,"he",true)
end

sgs.ai_fill_skill.shuaijie = function(self)
	return sgs.Card_Parse("@ShuaijieCard=.")
end

sgs.ai_skill_use_func["ShuaijieCard"] = function(card,use,self)
  	for _,p in sgs.list(self.room:getAlivePlayers())do
		if p:getMark("&silve+#"..self.player:objectName())>0
		and self:isFriend(p) and self:isWeak(p) then return end
	end
	use.card = card
end

sgs.ai_use_value.ShuaijieCard = 3.4
sgs.ai_use_priority.ShuaijieCard = 9.8


sgs.ai_skill_invoke.tenyearfuning = function(self,data)
	local str = data:toString()
	local strs = str:split(":")
	local n = tonumber(strs[2])
	if n<3 or n<(self.player:getCardCount()+2)/2 then
		return self:canDraw()
	end
end

sgs.ai_fill_skill.tenyearbingji = function(self)
	return sgs.Card_Parse("@TenyearBingjiCard=.")
end

sgs.ai_skill_use_func["TenyearBingjiCard"] = function(card,use,self)
  	local dcp = dummyCard("peach")
	dcp:setSkillName("tenyearbingji")
	self:sort(self.friends_noself)
  	for _,p in sgs.list(self.friends_noself)do
		if self:isWeak(p) and self.player:canUse(dcp,p) then
			sgs.ai_skill_choice.tenyearbingji = "peach"
			use.card = card
			return
		end
	end
  	local dc = dummyCard()
	dc:setSkillName("tenyearbingji")
	if not self.player:isLocked(dc) then
		local d = self:aiUseCard(dc)
		if d.card then
			self.tenyearbingjiUse = d
			sgs.ai_skill_choice.tenyearbingji = "slash"
			use.card = card
			return
		end
	end
  	for _,p in sgs.list(self.friends_noself)do
		if self.player:canUse(dcp,p) then
			sgs.ai_skill_choice.tenyearbingji = "peach"
			use.card = card
			return
		end
	end
end

sgs.ai_use_value.TenyearBingjiCard = 3.4
sgs.ai_use_priority.TenyearBingjiCard = 2.8

sgs.ai_skill_use["@@tenyearbingji!"] = function(self,prompt)
	local use = self.tenyearbingjiUse
	if use.card then
		for _,p in sgs.list(use.to)do
			return use.card:toString().."->"..p:objectName()
		end
	end
end

sgs.ai_skill_playerchosen.tenyearbingji = function(self,players)
	local destlist = self:sort(players)	
	for _,to in sgs.list(destlist)do
		if self:isFriend(to)
		then return to end
	end
	for _,to in sgs.list(destlist)do
		if not self:isEnemy(to)
		then return to end
	end
    return destlist[1]
end











sgs.ai_skill_invoke.qianlong = function(self,data)
    return true
end

sgs.ai_skill_use["@@qianlong"] = function(self,prompt)
	self.qianlong_use = false
	local yuqi_help = self.player:getTag("qianlongForAI"):toIntList()
	local n1,n2 = {},{}
	for _,id in sgs.list(yuqi_help)do
		table.insert(n1,sgs.Sanguosha:getCard(id))
	end
	local n = self.player:getLostHp()
	self:sortByKeepValue(n1,true)
	local poisons = self:poisonCards(n1)
	for _,c in sgs.list(n1)do
		if #n2>=n then break end
		if table.contains(poisons,c) or c:isAvailable(self.player)
		then self.qianlong_use = true continue end
		table.insert(n2,c:getEffectiveId())
	end
	for _,c in sgs.list(n1)do
		if #n2>=n then break end
		if table.contains(poisons,c)
		or self.player:getMark("@juetaoMark")>0
		or table.contains(n2,c:getEffectiveId())
		then continue end
		table.insert(n2,c:getEffectiveId())
	end
	if #n2<1 then table.insert(n2,n1[1]:getEffectiveId()) end
	return #n2>0 and ("@QianlongCard="..table.concat(n2,"+"))
end

sgs.ai_skill_playerchosen.fensi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	local dc = dummyCard()
	dc:setSkillName("_fensi")
    for _,target in sgs.list(destlist)do
		if target:canSlash(self.player,dc,false)
		then continue end
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if target:canSlash(self.player,dc,false)
		then continue end
		if not self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	if not self:isWeak()
	then return self.player end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
	return self.player
end

sgs.ai_skill_playerchosen.juetao = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self.player:inMyAttackRange(target)
		and self:isEnemy(target)
		and self:isWeak(target)
		and self.qianlong_use
		then return target end
	end
end

sgs.ai_skill_use["@@juetao!"] = function(self,prompt)
    local c = sgs.Sanguosha:getCard(self.player:getMark("juetao_card_id"))
    local dummy = self:aiUseCard(c)
   	if dummy.card and dummy.to then
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

sgs.ai_skill_playerschosen.zhushi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	local tos = {}
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target) and self:isWeak(target)
		or self:isFriend(target) then table.insert(tos,target) end
	end
	return tos
end

addAiSkills("xiaowu").getTurnUseCard = function(self)
	return sgs.Card_Parse("@XiaowuCard=.")
end

sgs.ai_skill_use_func["XiaowuCard"] = function(card,use,self)
	use.card = card
	sgs.xiaowu_n = 0
end

sgs.ai_use_value.XiaowuCard = 3.4
sgs.ai_use_priority.XiaowuCard = 4.8

sgs.ai_skill_choice.xiaowu = function(self,choices,data)
	local items = choices:split("+")
	if table.contains(items,"xiajia")
	then return "xiajia" end
	if table.contains(items,"shangjia")
	then return "shangjia" end
	local target = data:toPlayer()
	sgs.xiaowu_n = sgs.xiaowu_n or 0
	if sgs.xiaowu_n<1 and self:isWeak()
	or self:isFriend(target)
	then
		sgs.xiaowu_n = sgs.xiaowu_n+1
		return items[1]
	else
		sgs.xiaowu_n = sgs.xiaowu_n-1
		return items[2]
	end
end

sgs.ai_skill_playerchosen.xiaowu = function(self,players)
	return self.player
end

sgs.ai_skill_invoke.huaping = function(self,data)
	local n = 0
	for _,p in sgs.list(self.room:getPlayers())do
		if p:isDead() then n = n+1 end
	end
	return n>1
end

sgs.ai_skill_playerchosen.huaping = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target) and self:isWeak(target)
		then return target end
	end
end

sgs.ai_skill_use["@@shawu"] = function(self,prompt)
	local valid = {}
	local to = self.player:getTag("ShawuTarget"):toPlayer()
	if not self:isEnemy(to) and self:isWeak(to)
	or self:isFriend(to)
	then return end
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if self.player:getMark("&lyexwsha")*2>#cards 
		or #valid>=2  then continue end
    	table.insert(valid,h:getEffectiveId())
	end
	valid = #valid>1 and table.concat(valid,"+") or "."
	return string.format("@ShawuCard=%s",valid)
end

sgs.ai_skill_playerchosen.tenyearxizhen = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		or not target:isWounded()
		then continue end
		local use = {to = sgs.SPlayerList(),card = dummyCard()}
		use.card:setSkillName("_tenyearxizhen")
		use.to:append(target)
		self:targetRevises(use)
		sgs.ai_skill_choice.tenyearxizhen = "slash="..target:objectName()
		if not use.to:contains(target) then return target end
		use.card = dummyCard("duel")
		use.card:setSkillName("_tenyearxizhen")
		self:targetRevises(use)
		sgs.ai_skill_choice.tenyearxizhen = "duel="..target:objectName()
		if not use.to:contains(target)
		and self:isFriend(target)
		and target:isWounded()
		then return target end
	end
	self:sort(destlist,"hp",true)
    for d,target in sgs.list(destlist)do
		local dc = dummyCard()
		dc:setSkillName("_tenyearxizhen")
		d = self:aiUseCard(dc)
		sgs.ai_skill_choice.tenyearxizhen = "slash="..target:objectName()
		if d.to:contains(target)
		then return target end
		dc = dummyCard("duel")
		dc:setSkillName("_tenyearxizhen")
		d = self:aiUseCard(dc)
		sgs.ai_skill_choice.tenyearxizhen = "duel="..target:objectName()
		if d.to:contains(target)
		then return target end
	end
end

sgs.ai_skill_playerchosen.luochong = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if target:objectName()==self.luochong_to:objectName()
		then return target end
	end
end

sgs.ai_skill_choice.luochong = function(self,choices)
	local items = choices:split("+")
	if table.contains(items,"recover")
	then
		self:sort(self.friends,"hp")
		for _,to in sgs.list(self.friends)do
			self.luochong_to = to
			if self:isWeak(to) and to:isWounded()
			then return "recover" end
		end
	end
	if table.contains(items,"lose")
	then
		self:sort(self.enemies,"hp")
		for _,to in sgs.list(self.enemies)do
			self.luochong_to = to
			if self:isWeak(to)
			then return "lose" end
		end
	end
	if table.contains(items,"draw")
	then
		self:sort(self.friends,"handcard")
		for _,to in sgs.list(self.friends)do
			self.luochong_to = to
			if to:getHandcardNum()<4
			then return "draw" end
		end
	end
	if table.contains(items,"discard")
	then
		self:sort(self.enemies,"card")
		for _,to in sgs.list(self.enemies)do
			self.luochong_to = to
			if to:getCardCount()>1
			then return "discard" end
		end
		for _,to in sgs.list(self.friends_noself)do
			self.luochong_to = to
			if self:doDisCard(to,"e")
			then return "recover" end
		end
		for _,to in sgs.list(self.enemies)do
			self.luochong_to = to
			if to:getCardCount()>0
			then return "discard" end
		end
	end
	if table.contains(items,"draw")
	then
		self:sort(self.friends,"handcard")
		for _,to in sgs.list(self.friends)do
			self.luochong_to = to
			return "draw"
		end
	end
	return items[#items]
end

sgs.ai_skill_choice.aicheng = function(self,choices)
	local items = choices:split("+")
	return items[#items]
end

sgs.ai_skill_playerschosen.tongxie = function(self,players)
	local tos = {}
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and #tos<2
		then table.insert(tos,target) end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target) and #tos<2
		and not table.contains(tos,target)
		then table.insert(tos,target) end
	end
	return #tos>1 and tos
end

sgs.ai_skill_invoke.fuping = function(self,data)
	local cn = data:toString()
	cn = cn:split(":")[2]
	if cn~=""
	then
		cn = dummyCard(cn)
		return self:getUseValue(cn)>5
		or cn:isDamageCard() and self:getUseValue(cn)>4
	end
end

addAiSkills("fuping").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,true)
	local record = self.player:property("SkillDescriptionRecord_fuping"):toString()
	record = record:split("+")
  	for _,c in sgs.list(cards)do
		if c:getTypeId()==1 then continue end
		for d,pn in sgs.list(record)do
			if self.player:getMark("fuping_guhuo_remove_"..pn.."-Clear")>0
			then continue end
			d = dummyCard(pn)
			d:addSubcard(c)
			d:setSkillName("fuping")
			local parse = self:aiUseCard(d)
			if d:isAvailable(self.player) and parse.card and parse.to
			and self:getCardsNum(d:getClassName())<1
			then
				if d:canRecast()
				and parse.to:length()<1
				then return end
				self.fuping_to = parse.to
				sgs.ai_use_priority.FupingCard = sgs.ai_use_priority[d:getClassName()]
				return sgs.Card_Parse("@FupingCard="..c:getEffectiveId()..":"..pn)
			end
		end
	end
end

sgs.ai_skill_use_func["FupingCard"] = function(card,use,self)
	if self.fuping_to
	then
		use.card = card
		use.to = self.fuping_to
	end
end

sgs.ai_use_value.FupingCard = 5.4
sgs.ai_use_priority.FupingCard = 4.8

sgs.ai_guhuo_card.fuping = function(self,toname,class_name)
    local cards = self:addHandPile("he")
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
	local record = self.player:property("SkillDescriptionRecord_fuping"):toString()
	record = record:split("+")
	if #cards<1 or not table.contains(record,toname)
	or self.player:getMark("fuping_guhuo_remove_"..toname.."-Clear")>0
	then return end
    local num = self:getCardsNum(class_name)
   	for _,c in sgs.list(cards)do
       	if c:getTypeId()~=1
		and num<1
      	then
           	return "@FupingCard="..c:getEffectiveId()..":"..toname
       	end
   	end
end

addAiSkills("weilie").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,true)
	if self.player:getMark("weilie_used_times")<self.player:getMark("&weilie_time")+1
	and #cards>0
	then
		local parse = sgs.Card_Parse("@WeilieCard="..cards[1]:getEffectiveId())
		assert(parse)
		return parse
	end
end

sgs.ai_skill_use_func["WeilieCard"] = function(card,use,self)
	self:sort(self.friends,"hp")
	for _,fp in sgs.list(self.friends)do
		if self:isWeak(fp)
		then
			use.card = card
			use.to:append(fp)
			return
		end
	end
end

sgs.ai_use_value.WeilieCard = 3.4
sgs.ai_use_priority.WeilieCard = 0.8

addAiSkills("tenyearjinggong").getTurnUseCard = function(self)
	local cards = self:addHandPile("he")
	cards = self:sortByKeepValue(cards,nil,true)
  	for _,c in sgs.list(cards)do
		if c:getTypeId()~=3 then continue end
		d = dummyCard()
		d:addSubcard(c)
		d:setSkillName("tenyearjinggong")
		local parse = self:aiUseCard(d)
		if d:isAvailable(self.player)
		and parse.card and parse.to
		then return d end
	end
end

sgs.ai_guhuo_card.tenyearjinggong = function(self,toname,class_name)
    local cards = self:addHandPile("he")
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
   local num = self:getCardsNum(class_name)
   	for d,c in sgs.list(cards)do
       	if c:getTypeId()==3 and class_name=="Slash"	and num<1
		and sgs.Sanguosha:getCurrentCardUseReason()==sgs.CardUseStruct_CARD_USE_REASON_RESPONSE_USE
      	then
           	d = dummyCard()
			d:addSubcard(c)
			d:setSkillName("tenyearjinggong")
			return d:toString()
       	end
   	end
end

sgs.ai_skill_invoke.tenyearxiaojun = function(self,data)
	local invoke = data:toString():split(":")
	if invoke
	then
		local to = BeMan(self.room,invoke[2])
		return not self:isFriend(to)
	end
end

sgs.ai_skill_invoke.tenyearmingfa = function(self,data)
	local cn = data:toString()
	cn = cn:split(":")[2]
	if cn~=""
	then
		cn = dummyCard(cn)
		cn:setSkillName("tenyearmingfa")
		self.tenyearmingfa_c = cn
		return (#self.enemies>0 or #self.friends_noself<1)
		and (cn:isDamageCard() or self:getUseValue(cn)>5)
	end
end

sgs.ai_skill_invoke.tenyeardeshao = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isFriend(target)
		or target:getHandcardNum()<=self.player:getHandcardNum()
		or self:doDisCard(target,"e")
	end
end

sgs.ai_skill_playerchosen.tenyearmingfa = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and self.tenyearmingfa_c:targetFixed()
		and self.player:canUse(self.tenyearmingfa_c,target,true)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self.player:canUse(self.tenyearmingfa_c,target,true)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target) and not self:isWeak(target)
		and self.player:canUse(self.tenyearmingfa_c,target,true)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and self.player:canUse(self.tenyearmingfa_c,target,true)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self.player:canUse(self.tenyearmingfa_c,target,true)
		then return target end
	end
end

sgs.ai_skill_discard.liejie = function(self,max,min)
	local to_cards = {}
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,true)
   	for _,hcard in sgs.list(cards)do
   		if #to_cards>#cards/2
		or #to_cards>=max
		then break end
		table.insert(to_cards,hcard:getEffectiveId())
	end
	return to_cards
end

sgs.ai_skill_cardchosen.liejie = function(self,who,flags,method)
	if self:isFriend(who) then
		if self:doDisCard(who,"e") then
			for _,e in sgs.list(who:getEquipsId())do
				if self:doDisCard(who,e)
				then return e end
			end
		end
		return -1
	end
end

sgs.ai_skill_invoke.yuanzi = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return self:isFriend(target)
		and #self.enemies>0
	end
	return true
end

sgs.ai_skill_invoke.tongli = function(self,data)
    return true
end

sgs.ai_skill_invoke.shezang = function(self,data)
    return true
end

sgs.ai_skill_playerchosen.tuoxian = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp",true)
	local n = self.player:getChangeSkillState("piaoping")
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and self:doDisCard(target,"ej")
		and self.player:getMark("&piaoping_trigger-Clear")<3
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

sgs.ai_skill_use["@@tuoxian"] = function(self,prompt)
	local n1,n2 = self.player:getCards("hej"),{}
	self:sortByKeepValue(n1)
	for i,c in sgs.list(n1)do
		i = c:getEffectiveId()
		if #n2>=self.player:getMark("tuoxian_discard") then break end
		if self:doDisCard(self.player,i) then table.insert(n2,i) end
	end
	for i,c in sgs.list(n1)do
		i = c:getEffectiveId()
		if #n2>=self.player:getMark("tuoxian_discard") then break end
		if #n2>0 and not table.contains(n2,i)
		then table.insert(n2,i) end
	end
	return #n2>0 and ("@TuoxianCard="..table.concat(n2,"+"))
end

sgs.ai_skill_playerchosen.dunxi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
	local n = self.player:getChangeSkillState("piaoping")
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and target:getMark("&bxdxdun")<1
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and target:getMark("&bxdxdun")<1
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
end

sgs.ai_skill_invoke.tenyearxiecui = function(self,data)
	local cn = data:toString()
	cn = cn:split(":")
	if cn~=""
	then
		local to = BeMan(self.room,cn[3])
		local from = BeMan(self.room,cn[2])
		return self:isEnemy(to)
		or not self:isFriend(to) and self:isFriend(from)
	end
end

sgs.ai_skill_invoke.tenyearyouxu = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return self:isFriend(target)
		and #self.friends_noself>1
		or #self.friends_noself>0
		or not self:isFriend(target)
	end
end

sgs.ai_skill_playerchosen.tenyearyouxu = function(self,players)
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
end

sgs.ai_skill_invoke.zhongjie = function(self,data)
	local target = data:toPlayer()
	if target
	then
		if target:objectName()==self.player:objectName()
		then
			return self:getCardsNum("Peach")+self:getCardsNum("Analeptic")<1
		end
		return self:isFriend(target)
	end
end

sgs.ai_skill_invoke.sushou = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return target:getHandcardNum()>3
		and (not self:isWeak() or self.player:hasSkill("zhongjie") and self.player:getMark("zhongjie_used_lun")<1)
	end
end

sgs.ai_skill_use["@@sushou"] = function(self,prompt)
	local yuqi_help = self.player:getTag("sushouForAI"):toIntList()
	local n1,n2 = {},{}
	for c,id in sgs.list(yuqi_help)do
		table.insert(n1,sgs.Sanguosha:getCard(id))
	end
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards)
   	local target = self.room:getCurrent()
	local n = self.player:getLostHp()*2
	self:sortByKeepValue(n1,true)
	local poisons = self:poisonCards(n1)
	for _,c in sgs.list(n1)do
		if #n2>=n then break end
		for _,h in sgs.list(cards)do
			if #n2>=n then break end
			if table.contains(poisons,c)
			or table.contains(n2,h:getEffectiveId())
			or self:getUseValue(c)>self:getUseValue(h) and self:isFriend(target)
			or self:getKeepValue(c)<=self:getKeepValue(h) then continue end
			table.insert(n2,c:getEffectiveId())
			table.insert(n2,h:getEffectiveId())
			break
		end
	end
	return #n2>1 and ("@SushouCard="..table.concat(n2,"+"))
end

sgs.ai_skill_playerchosen.suizheng = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target~=self.player
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

sgs.ai_skill_use["@@suizheng"] = function(self,prompt)
    local c = dummyCard()
	c:setSkillName("suizheng")
    local dummy = self:aiUseCard(c)
   	local tos = {}
   	if dummy.card
   	and dummy.to
   	then
       	for _,p in sgs.list(dummy.to)do
			if #tos>0 then break end
       		if p:hasFlag("suizheng_target")
			then
				table.insert(tos,p:objectName())
			end
       	end
    end
	local Players = self.room:getOtherPlayers(self.player)
	Players = self:sort(Players,"handcard")
   	for _,p in sgs.list(Players)do
   		if #tos>0 then break end
		if p:hasFlag("suizheng_target")
		and CanToCard(c,self.player,p) and self:isEnemy(p)
		then table.insert(tos,p:objectName()) end
   	end
   	for _,p in sgs.list(Players)do
   		if #tos>0 then break end
		if p:hasFlag("suizheng_target")
		and CanToCard(c,self.player,p) and not self:isFriend(p)
		then table.insert(tos,p:objectName()) end
   	end
	if #tos>0
	then
		return "@SuizhengCard=.->"..table.concat(tos,"+")
	end
end

sgs.ai_skill_playerchosen["suizheng1"] = function(self,players)
    local c = dummyCard()
	c:setSkillName("suizheng")
    local dummy = self:aiUseCard(c)
   	if dummy.card and dummy.to
   	then
       	for _,p in sgs.list(dummy.to)do
       		if players:contains(p)
			then return p end
       	end
    end
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
   	for _,p in sgs.list(destlist)do
		if CanToCard(c,self.player,p) and self:isEnemy(p)
		then return p end
   	end
   	for _,p in sgs.list(destlist)do
		if CanToCard(c,self.player,p) and not self:isFriend(p)
		then return p end
   	end
end

addAiSkills("kaiji").getTurnUseCard = function(self)
	local n = self.player:getChangeSkillState("kaiji")
	sgs.ai_use_priority.KaijiCard = 9.8
	if n<2
	then
		return sgs.Card_Parse("@KaijiCard=.")
	end
	n = {}
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,true)
	for _,c in sgs.list(cards)do
		if #n>#self.enemies or #n>=self.player:getMaxHp()
		or #n>#cards/2 then break end
		table.insert(n,c:getEffectiveId())
	end
	sgs.ai_use_priority.KaijiCard = 0.8
	if #n>0
	then
		return sgs.Card_Parse("@KaijiCard="..table.concat(n,"+"))
	end
end

sgs.ai_skill_use_func["KaijiCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.KaijiCard = 3.4
sgs.ai_use_priority.KaijiCard = 9.8

sgs.ai_skill_playerschosen.pingxi = function(self,players)
	local destlist = self:sort(players,"card")
	local tos,n = {},self.player:getMark("pingxi_discard-Clear")
	for _,target in sgs.list(destlist)do
		if #tos>=n then break end
		if self:isEnemy(target)
		and self:doDisCard(target)
		then table.insert(tos,target) end
	end
	self:sort(destlist,"card",true)
	for _,target in sgs.list(destlist)do
		if #tos>=n then break end
		if self:isFriend(target)
		and self:doDisCard(target,"ej")
		then table.insert(tos,target) end
	end
	self:sort(destlist,"card")
	for _,target in sgs.list(destlist)do
		if #tos>=n then break end
		if not self:isFriend(target)
		and not table.contains(tos,target)
		and self:doDisCard(target)
		then table.insert(tos,target) end
	end
	for _,target in sgs.list(destlist)do
		if #tos>=n then break end
		if not self:isFriend(target)
		and not table.contains(tos,target)
		then table.insert(tos,target) end
	end
	return tos
end

addAiSkills("xunji").getTurnUseCard = function(self)
	local parse = sgs.Card_Parse("@XunjiCard=.")
	assert(parse)
	return parse
end

sgs.ai_skill_use_func["XunjiCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard",true)
	for _,fp in sgs.list(self.enemies)do
		if fp:getHandcardNum()>2
		then
			use.card = card
			use.to:append(fp)
			return
		end
	end
	for _,fp in sgs.list(self.room:getOtherPlayers(self.player))do
		if fp:getHandcardNum()>2
		and not self:isFriend(fp)
		then
			use.card = card
			use.to:append(fp)
			return
		end
	end
end

sgs.ai_use_value.XunjiCard = 3.4
sgs.ai_use_priority.XunjiCard = 3.8

sgs.ai_skill_invoke.fanyin = function(self,data)
    return true
end

sgs.ai_skill_use["@@fanyin"] = function(self,prompt)
    local c = sgs.Sanguosha:getCard(self.player:getMark("fanyin_id"))
	c:setFlags("fanyin_use_card")
    local dummy = self:aiUseCard(c)
   	if dummy.card
   	and dummy.to
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

sgs.ai_skill_invoke.fanyin_targetfixed = function(self,data)
    local c = sgs.Sanguosha:getCard(self.player:getMark("fanyin_id"))
	c:setFlags("fanyin_use_card")
    local dummy = self:aiUseCard(c)
    return dummy.card and dummy.to
end

sgs.ai_skill_playerschosen.fanyin = function(self,players,x,n)
	local tos = {}
	self.player:setTag("yb_zhuzhan2_data",self.player:getTag("fanyinData"))
	while true do
		local to = sgs.ai_skill_playerchosen.yb_zhuzhan2(self,players)
		if to and #tos<x
		then
			table.insert(tos,to)
			players:removeOne(to)
		else break end
	end
	return tos
end

sgs.ai_skill_invoke.peiqi = function(self,data)
	self.peiqiData = {}
	for _,ep in sgs.list(self.friends)do
		if self:doDisCard(ep,"ej",true) then
			self.peiqiData.from = ep
			local ejs = ep:getCards("ej")
			ejs = self:sortByKeepValue(ejs)
			for _,ej in sgs.list(ejs)do
				local i = ej:getEffectiveId()
				if self:doDisCard(ep,i,true) then
					self.peiqiData.cid = i
					for _,fp in sgs.list(self.enemies)do
						self.peiqiData.to = fp
						if ej:getTypeId()==3 then
							local n = ej:getRealCard():toEquipCard():location()
							if not fp:getEquip(n) and fp:hasEquipArea(n)
							then return true end
						else
							if self.player:canUse(ej,fp,true)
							then return true end
						end
					end
				end
			end
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if self:doDisCard(ep,"ej",true) then
			self.peiqiData.from = ep
			local ejs = ep:getCards("ej")
			ejs = self:sortByKeepValue(ejs,true)
			for _,ej in sgs.list(ejs)do
				local i = ej:getEffectiveId()
				if self:doDisCard(ep,i,true) then
					self.peiqiData.cid = i
					for _,fp in sgs.list(self.friends)do
						self.peiqiData.to = fp
						if ej:getTypeId()==3 then
							local n = ej:getRealCard():toEquipCard():location()
							if not fp:getEquip(n) and fp:hasEquipArea(n)
							then return true end
						else
							if self.player:canUse(ej,fp,true)
							then return true end
						end
					end
				end
			end
		end
	end
	for _,ep in sgs.list(self.room:getOtherPlayers(self.player))do
		if self:doDisCard(ep,"ej",true) and not self:isFriend(ep) then
			self.peiqiData.from = ep
			local ejs = ep:getCards("ej")
			ejs = self:sortByKeepValue(ejs,true)
			for _,ej in sgs.list(ejs)do
				local i = ej:getEffectiveId()
				if self:doDisCard(ep,i,true) then
					self.peiqiData.cid = i
					for _,fp in sgs.list(self.friends)do
						self.peiqiData.to = fp
						if ej:getTypeId()==3 then
							local n = ej:getRealCard():toEquipCard():location()
							if not fp:getEquip(n) and fp:hasEquipArea(n)
							then return true end
						else
							if not fp:containsTrick(ej:objectName())
							and self.player:canUse(ej,fp,true)
							then return true end
						end
					end
				end
			end
		end
	end
end

sgs.ai_skill_playerchosen["peiqi_from"] = function(self,players)
	for _,target in sgs.list(players)do
		if target:objectName()==self.peiqiData.from:objectName()
		then return target end
	end
end

sgs.ai_skill_playerchosen["peiqi_to"] = function(self,players)
	for _,target in sgs.list(players)do
		if target:objectName()==self.peiqiData.to:objectName()
		then return target end
	end
end

sgs.ai_skill_cardchosen.peiqi = function(self,who,flags,method)
	for _,e in sgs.list(who:getCards(flags))do
		local id = e:getEffectiveId()
		if id==self.peiqiData.cid
		then return id end
	end
end

sgs.ai_can_damagehp.peiqi = function(self,from,card,to)
	return to:getHp()+self:getAllPeachNum()-self:ajustDamage(from,to,1,card)>0
	and self:canLoseHp(from,card,to)
	and sgs.ai_skill_invoke.peiqi(self)
end

sgs.ai_skill_playerchosen.xiaoxinf = function(self,players)
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
end

sgs.ai_skill_choice.xiaoxinf = function(self,choices)
	local items = choices:split("+")
	for _,to in sgs.list(self.enemies)do
		if self.player:getLostHp()>1
		and self.player:inMyAttackRange(to)
		and table.contains(items,"lose=2")
		then return "lose=2" end
	end
	for dc,item in sgs.list(items)do
		if item:startsWith("slash")
		then
			dc = dummyCard()
			dc:setSkillName("xiaoxinf")
			dc = self:aiUseCard(dc)
			if dc.card and dc.to
			then
				for _,to in sgs.list(dc.to)do
					self.xiaoxinf_to = to
					if self.player:inMyAttackRange(to)
					then return item end
				end
			end
		end
	end
	return items[1]
end

sgs.ai_skill_playerchosen["xiaoxinf_slash"] = function(self,players)
	for _,target in sgs.list(players)do
		if target:objectName()==self.xiaoxinf_to:objectName()
		then return target end
	end
end

sgs.ai_skill_invoke.xiongrao = function(self,data)
    return self.player:getMaxHp()<3
end

sgs.ai_skill_invoke.diting = function(self,data)
	local target = data:toPlayer()
	if target
	then
		self.diting_to = target
		return not self:isFriend(target)
		or target:getHandcardNum()>1
	end
end

sgs.ai_skill_askforag.diting = function(self,ids)
	local cs = ListI2C(ids)
	cs = self:sortByUseValue(cs,self:isEnemy(self.diting_to))
	for _,c in sgs.list(cs)do
		if c:isDamageCard()
		then
			return c:getEffectiveId()
		end
	end
	return cs[1]:getEffectiveId()
end
--[[
sgs.ai_target_revises.diting = function(to,card,self)
    if self.player:getMark("diting_show_"..card:getEffectiveId().."_"..to:objectName().."-PlayClear")>0
	and self:isFriend(to)
	then return true end
end
--]]
sgs.ai_skill_playerchosen.bihuof = function(self,players)
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
end

sgs.ai_skill_playerchosen.bihuof2 = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp",true)
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_invoke.kanpodz = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isFriend(target)
	end
end

addAiSkills("kanpodz").getTurnUseCard = function(self)
	local cards = self:addHandPile()
	cards = self:sortByKeepValue(cards,nil,true)
  	for _,c in sgs.list(cards)do
		if self:getCardsNum("Slash")>0
		then break end
		d = dummyCard()
		d:addSubcard(c)
		d:setSkillName("kanpodz")
		local parse = self:aiUseCard(d)
		if d:isAvailable(self.player)
		and parse.card and parse.to
		then return d end
	end
end

sgs.ai_guhuo_card.kanpodz = function(self,toname,class_name)
    local cards = self:addHandPile()
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
   	for d,c in sgs.list(cards)do
       	if class_name=="Slash"and self:getCardsNum(class_name)<1
		and sgs.Sanguosha:getCurrentCardUseReason()==sgs.CardUseStruct_CARD_USE_REASON_RESPONSE_USE
		and self.player:getMark("kanpodz_used-Clear")<1
      	then
           	d = dummyCard()
			d:addSubcard(c)
			d:setSkillName("kanpodz")
			return d:toString()
       	end
   	end
end

sgs.ai_skill_invoke.gengzhan = function(self,data)
    return true
end

addAiSkills("midu").getTurnUseCard = function(self)
	local parse = sgs.Card_Parse("@MiduCard=.")
	assert(parse)
	return parse
end

sgs.ai_skill_use_func["MiduCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.MiduCard = 3.4
sgs.ai_use_priority.MiduCard = 6.8

sgs.ai_skill_playerchosen.midu = function(self,players)
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
end

sgs.ai_skill_playerschosen.yingyu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	local tos = {}
	for _,target in sgs.list(destlist)do
		if #tos>0 then break end
		if self:isFriend(target)
		then table.insert(tos,target) end
	end
	for _,target in sgs.list(destlist)do
		if #tos>0 then break end
		if not self:isEnemy(target)
		then table.insert(tos,target) end
	end
	for _,target in sgs.list(destlist)do
		if #tos>1 then break end
		if self:isEnemy(target)
		then table.insert(tos,target) end
	end
	for _,target in sgs.list(destlist)do
		if #tos>0 then break end
		if not self:isFriend(target)
		then table.insert(tos,target) end
	end
	return #tos>1 and tos or {}
end

sgs.ai_skill_playerchosen.yingyu = function(self,players)
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
end

addAiSkills("yongbi").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards,nil,true)
	local suits = {}
  	for s,c in sgs.list(cards)do
		s = c:getSuit()
		if table.contains(suits,s)
		then continue end
		table.insert(suits,s)
	end
	return #suits>2 and sgs.Card_Parse("@YongbiCard=.")
end

sgs.ai_skill_use_func["YongbiCard"] = function(card,use,self)
	self:sort(self.friends,"hp",true)
	for _,fp in sgs.list(self.friends)do
		if fp:isMale()
		then
			use.card = card
			use.to:append(fp)
			return
		end
	end
end

sgs.ai_use_value.YongbiCard = 3.4
sgs.ai_use_priority.YongbiCard = 0.8

sgs.ai_skill_cardask["@fenrui"] = function(self,data)
    return self.player:getCardCount()>2
end

sgs.ai_skill_playerchosen.fenrui = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self.player:getEquips():length()-target:getEquips():length()>=target:getHp()
		then return target end
	end
end

sgs.ai_skill_invoke.tenyeartujue = function(self,data)
    return self:getCardsNum("Peach")+self:getCardsNum("Analeptic")<1
end

addAiSkills("tenyearquanjian").getTurnUseCard = function(self)
	return sgs.Card_Parse("@TenyearQuanjianCard=.")
end

sgs.ai_skill_use_func["TenyearQuanjianCard"] = function(card,use,self)
	if self.player:getMark("tenyearquanjian_tiansuan_remove_card-PlayClear")<1 then
		self:sort(self.enemies,"handcard",true)
		for _,ep in sgs.list(self.enemies)do
			if ep:getHandcardNum()>ep:getMaxCards() then
				use.card = sgs.Card_Parse("@TenyearQuanjianCard=.:card")
				use.to:append(ep)
				return
			end
		end
		self:sort(self.friends_noself,"handcard")
		for _,fp in sgs.list(self.friends_noself)do
			if fp:getHandcardNum()<fp:getMaxCards() then
				use.card = sgs.Card_Parse("@TenyearQuanjianCard=.:card")
				use.to:append(fp)
				return
			end
		end
	end
	if self.player:getMark("tenyearquanjian_tiansuan_remove_damage-PlayClear")<1 then
		self:sort(self.enemies,"hp")
		for _,ep in sgs.list(self.enemies)do
			for _,fp in sgs.list(self.friends_noself)do
				if fp:inMyAttackRange(ep)
				and ep:getMark("&tenyearquanjian_debuff-Clear")>0 then
					use.card = sgs.Card_Parse("@TenyearQuanjianCard=.:damage")
					use.to:append(fp)
					use.to:append(ep)
					return
				end
			end
			for _,fp in sgs.list(self.room:getOtherPlayers(self.player))do
				if fp:inMyAttackRange(ep)
				and ep:getMark("&tenyearquanjian_debuff-Clear")>0
				and self:isEnemy(fp) then
					use.card = sgs.Card_Parse("@TenyearQuanjianCard=.:damage")
					use.to:append(fp)
					use.to:append(ep)
					return
				end
			end
		end
		for _,ep in sgs.list(self.enemies)do
			for _,fp in sgs.list(self.friends_noself)do
				if fp:inMyAttackRange(ep) then
					use.card = sgs.Card_Parse("@TenyearQuanjianCard=.:damage")
					use.to:append(fp)
					use.to:append(ep)
					return
				end
			end
			for _,fp in sgs.list(self.room:getOtherPlayers(self.player))do
				if fp:inMyAttackRange(ep)
				and self:isEnemy(fp) then
					use.card = sgs.Card_Parse("@TenyearQuanjianCard=.:damage")
					use.to:append(fp)
					use.to:append(ep)
					return
				end
			end
		end
	end
end

sgs.ai_use_value.TenyearQuanjianCard = 3.4
sgs.ai_use_priority.TenyearQuanjianCard = 8.8

sgs.ai_skill_invoke.tenyearquanjian = function(self,data)
	local invoke = data:toString():split("+")
	if invoke[1]=="dodamage"
	then
		invoke = BeMan(self.room,invoke[2])
		if invoke
		then
			return not self:isFriend(invoke)
			or not self:isWeak(invoke)
		end
	end
    return true
end

sgs.ai_skill_discard.tenyearquanjian = function(self,max,min)
	local to_cards = {}
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
   	for _,hcard in sgs.list(cards)do
   		if #to_cards>=min or min>2 then break end
		table.insert(to_cards,hcard:getEffectiveId())
	end
	return to_cards
end

sgs.ai_skill_invoke.suoliang = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isFriend(target)
	end
end

sgs.ai_skill_invoke.chongyi = function(self,data)
	local invoke = data:toString():split(":")
	if #invoke>1
	then
		invoke = BeMan(self.room,invoke[2])
		if invoke
		then
			return self:isFriend(invoke)
			or self:isWeak(invoke) and not self:isEnemy(invoke)
		end
	end
end

sgs.ai_skill_invoke.yingtu = function(self,data)
	local target = data:toPlayer()
	if target
	then
		local to = self.player:getNextAlive()
		if to==target
		then
			to = self.player:getNextAlive(self.player:getAliveSiblings():length())
		end
		if not self:isFriend(target)
		and not self:isEnemy(to)
		then return true end
		if self:doDisCard(target,"e")
		then return true end
		if self:isFriend(target)
		and self:isFriend(to)
		then return true end
		if not self:isFriend(target)
		and not self:isFriend(to)
		then return true end
	end
end

sgs.ai_skill_cardask["@tenyearpoyuan-discard"] = function(self,data,pattern)
    return self.player:getCardCount()>1
end

sgs.ai_skill_playerchosen.tenyearpoyuan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and self:doDisCard(target,"e")
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:doDisCard(target,"e")
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and self:doDisCard(target,"e")
		then return target end
	end
end

sgs.ai_skill_cardchosen.tenyearpoyuan = function(self,who,flags,method)
	if self:doDisCard(who,flags)
	then
		for i,c in sgs.list(who:getCards(flags))do
			i = c:getEffectiveId()
			if self:doDisCard(who,i)
			then return i end
		end
	end
	return -1
end

addAiSkills("tenyearhuace").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards,nil,true)
	for d,pn in sgs.list(patterns())do
		if self.player:getMark("tenyearhuace_guhuo_remove_"..pn)>0
		or #cards<1 then continue end
		d = PatternsCard(pn)
		if d and d:isNDTrick()
		and d:isDamageCard()
		then
			d = dummyCard(pn)
			d:addSubcard(cards[1])
			d:setSkillName("tenyearhuace")
			local parse = self:aiUseCard(d)
			if d:isAvailable(self.player) and parse.card and parse.to
			and self:getCardsNum(d:getClassName())<1
			then
				self.tenyearhuace_to = parse.to
				if d:canRecast() and parse.to:length()<1 then continue end
				return d
			end
		end
	end
	for d,pn in sgs.list(patterns())do
		if self.player:getMark("tenyearhuace_guhuo_remove_"..pn)>0
		or #cards<1 then continue end
		d = PatternsCard(pn)
		if d and d:isNDTrick()
		then
			d = dummyCard(pn)
			d:addSubcard(cards[1])
			d:setSkillName("tenyearhuace")
			local parse = self:aiUseCard(d)
			if d:isAvailable(self.player) and parse.card and parse.to
			and self:getCardsNum(d:getClassName())<1
			then
				if d:canRecast() and parse.to:length()<1 then continue end
				self.tenyearhuace_to = parse.to
				return d
			end
		end
	end
end

sgs.ai_skill_use_func["TenyearHuaceCard"] = function(card,use,self)
	if self.tenyearhuace_to
	then
		use.card = card
		use.to = self.tenyearhuace_to
	end
end

sgs.ai_use_value.TenyearHuaceCard = 5.4
sgs.ai_use_priority.TenyearHuaceCard = 4.8

sgs.ai_skill_invoke.ruizhan = function(self,data)
	local target = data:toPlayer()
	if target
	then
		local mc = self:getMaxCard()
		return mc and mc:getNumber()>10
		and (self:isEnemy(target) or not (self:isFriend(target) or self:isWeak(target)))
	end
end

addAiSkills("shilie").getTurnUseCard = function(self)
	return sgs.Card_Parse("@ShilieCard=.")
end

sgs.ai_skill_use_func["ShilieCard"] = function(card,use,self)
	local pile = self.player:getPile("shilie")
	if self:isWeak() then
		sgs.ai_skill_choice.shilie = "recover"
		if self.player:getCardCount()>self.player:getHp()
		or self.player:getCardCount()<1
		then use.card = card return end
	elseif pile:length()>1 then
		sgs.ai_skill_choice.shilie = "lose"
		self.shilie_n = sgs.IntList()
		for _,c in sgs.list(ListI2C(pile))do
			if c:isAvailable(self.player) then
				local d = self:aiUseCard(c)
				if d.card and d.to
				then self.shilie_n:append(c:getEffectiveId()) end
			end
		end
		if self.shilie_n:length()>1
		then use.card = card return end
	end
	if self.player:isWounded()
	and self.player:getCardCount()>self.player:getHp()*(math.random()+1) then
		sgs.ai_skill_choice.shilie = "recover"
		use.card = card
	end
end

sgs.ai_use_value.ShilieCard = 3.4
sgs.ai_use_priority.ShilieCard = 5.8

sgs.ai_skill_use["@@shilie!"] = function(self,prompt)
	local n2 = {}
	for i,id in sgs.list(self.player:getPile("shilie"))do
		if self.shilie_n:contains(id) then table.insert(n2,id) end
	end
	return #n2>0 and ("@ShilieGetCard="..table.concat(n2,"+"))
end

sgs.ai_skill_playerchosen.shilie = function(self,players)
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
end

addAiSkills("qiaoli").getTurnUseCard = function(self)
	local cards = self:addHandPile("he")
	cards = self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		if c:getTypeId()~=3
		or (self.player:getMark("qiaoliWeapon-PlayClear")>0 and c:isKindOf("Weapon")
		or self.player:getMark("qiaoliEquip-PlayClear")>0 and not c:isKindOf("Weapon"))
		or self:getCardsNum("Duel")>0
		then continue end
		local dc = dummyCard("duel")
		dc:setSkillName("qiaoli")
		dc:addSubcard(c)
		if dc:isAvailable(self.player)
		and self:aiUseCard(dc).card then
			return dc
		end
	end
end

sgs.ai_skill_invoke.qingliang = function(self,data)
	return self:getCardsNum("Jink")>0
	or self:isWeak()
end

sgs.ai_skill_choice.qingliang = function(self,choices)
	local items = choices:split("+")
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards,nil,true)
	local suits = {}
  	for s,c in sgs.list(cards)do
		s = c:getSuitString()
		if suits[s] then suits[s] = suits[s]+1
		else suits[s] = 1 end
	end
	local function func(a,b)
		return a<b
	end
	table.sort(suits,func)
   	local target = self.room:getCurrent()
	if self:getCardsNum("Jink")>0
	and not self:isEnemy(target)
	then return items[1] end
  	for s,c in sgs.list(cards)do
		s = c:getSuitString()
		if suits[1]==suits[s]
		then
			return "discard="..s
		end
	end
end

sgs.ai_skill_choice.chongwang = function(self,choices)
	local items = choices:split("+")
	if items
	then
		local to = items[1]:split("=")[2]
		to = BeMan(self.room,to)
		if to and self:isEnemy(to)
		then return items[2] end
		if to and self:isFriend(to)
		then return items[1] end
	end
	return items[#items]
end

sgs.ai_skill_playerschosen.huagui = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
	local tos = {}
	for _,target in sgs.list(destlist)do
		if #tos>=x then break end
		if self:isFriend(target)
		then table.insert(tos,target) end
	end
	for _,target in sgs.list(destlist)do
		if #tos>=x then break end
		if self:isEnemy(target)
		then table.insert(tos,target) end
	end
	for _,target in sgs.list(destlist)do
		if #tos>=x then break end
		if not table.contains(tos,target)
		then table.insert(tos,target) end
	end
	return tos
end

sgs.ai_skill_choice.huagui = function(self,choices,data)
	local target = data:toPlayer()
	local items = choices:split("+")
	if self:isFriend(target)
	then return items[1] end
	if not self:isEnemy(target)
	and math.random()>0.3
	then return items[1] end
	return items[2]
end

sgs.ai_skill_invoke.jingjian = function(self,data)
	local target = data:toString()
	target = target:split(":")[2]
	target = BeMan(self.room,target)
	if target then
		local mc = self:getMaxCard()
		return mc and mc:getNumber()>math.random(8,11)
		and (self:isEnemy(target) or not (self:isFriend(target) or self:isWeak(target)))
	end
end

sgs.ai_skill_choice.zhenze = function(self,choices)
	local items = choices:split("+")
	local function canZhenze(to)
		if to:getHandcardNum()>to:getHp()
		then return 1
		elseif to:getHandcardNum()==to:getHp()
		then return 0 end
		return -1
	end
	local n,x = canZhenze(self.player),0
	for _,target in sgs.list(self.room:getAllPlayers())do
		if n~=canZhenze(target)
		and not self:isFriend(target)
		then x = x+1 end
		if n==canZhenze(target)
		and not self:isEnemy(target)
		and target:isWounded()
		then x = x-1 end
	end
	if x>0 then return "lose" end
	if x<0 then return "recover" end
	return items[#items]
end

addAiSkills("anliao").getTurnUseCard = function(self)
	return sgs.Card_Parse("@AnliaoCard=.")
end

sgs.ai_skill_use_func["AnliaoCard"] = function(card,use,self)
	for _,fp in sgs.list(self.friends)do
		if self:doDisCard(fp,"e")
		then
			use.card = card
			use.to:append(fp)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()==1
		and ep:getMark("anliao_to-PlayClear")<1
		then
			use.card = card
			use.to:append(ep)
			ep:addMark("anliao_to-PlayClear")
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if self:doDisCard(ep,"he")
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,c in sgs.list(self.player:getCards("he"))do
		if self:getKeepValue(c)<4
		or self.player:getCardCount()>3
		then
			use.card = card
			use.to:append(self.player)
			return
		end
	end
end

sgs.ai_use_value.AnliaoCard = 3.4
sgs.ai_use_priority.AnliaoCard = 5.8

sgs.ai_skill_invoke.xieshou = function(self,data)
	local target = data:toPlayer()
	return target and self:isFriend(target)
end

sgs.ai_skill_choice.xieshou = function(self,choices)
	if self:isWeak() then return "recover" end
	local items = choices:split("+")
	return items[#items]
end

sgs.ai_skill_invoke.qingyan = function(self,data)
	return true
end

sgs.ai_skill_cardask["@qingyan"] = function(self,data)
    return self.player:getHandcardNum()>self.player:getMaxCards()
end









--度断
sgs.ai_skill_cardask["@duoduan-card"] = function(self,data)
	if self:needToThrowArmor() then return "$"..self.player:getArmor():getEffectiveId() end
	local use = data:toCardUse()
	if use.card:isKindOf("FireSlash") and self:slashIsEffective(use.card,self.player,use.from) and self:damageIsEffective(self.player,sgs.DamageStruct_Fire,use.from)
	and self.player:hasArmorEffect("Vine") and self.player:getArmor() and self.player:getArmor():objectName()=="vine" and
		(self:getCardsNum("Jink")==0 or self:canHit(self.player,use.from)) and not use.from:hasSkills("jueqing|gangzhi") then
		return "$"..self.player:getArmor():getEffectiveId()
	end

	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	if cards[1]:isKindOf("Jink") and self:getCardsNum("Jink")==1 then return "." end
	if cards[1]:isKindOf("Peach") or cards[1]:isKindOf("Analeptic") then return "." end

	local nature = sgs.DamageStruct_Normal
	if use.card:isKindOf("FireSlash") then nature = sgs.DamageStruct_Fire end
	if use.card:isKindOf("ThunderSlash") then nature = sgs.DamageStruct_Thunder end
	if self:getCardsNum("Jink")==0 and self:slashIsEffective(use.card,self.player,use.from) and self:damageIsEffective(self.player,nature,use.from) then return "$"..cards[1]:getEffectiveId() end
	if not self:isValuableCard(cards[1]) then return "$"..cards[1]:getEffectiveId() end
	return "."
end

sgs.ai_skill_discard.duoduan = function(self,discard_num,min_num,optional,include_equip)
	local use = self.player:getTag("duoduanForAI"):toCardUse()
	return self:askForDiscard("dummyreason",1,1,false,true)
end

--共损
sgs.ai_skill_use["@@gongsun"] = function(self,prompt)
	local valid,to = {},nil
    for _,p in sgs.list(self.player:getAliveSiblings())do
      	if self:isEnemy(p) and p:getHandcardNum()>2
    	then to = p break end
	end
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if #valid>=2 or to==nil or #cards<4 then break end
    	table.insert(valid,h:getEffectiveId())
	end
	if #valid<2 or #self.friends_noself<1 then return end
	return string.format("@GongsunCard=%s->%s",table.concat(valid,"+"),to:objectName())
end

sgs.ai_skill_askforag.GongsunCard = function(self,card_ids)
	for c,id in sgs.list(card_ids)do
		c = sgs.Sanguosha:getCard(id)
		if c:isKindOf("Slash")
		then return id end
	end
	for c,id in sgs.list(card_ids)do
		c = sgs.Sanguosha:getCard(id)
		if c:isKindOf("Jink")
		and self:getCardsNum("Slash")>0
		then return id end
	end
	for c,id in sgs.list(card_ids)do
		c = sgs.Sanguosha:getCard(id)
		if c:isDamageCard()
		then return id end
	end
end

sgs.ai_skill_invoke.juanxia_slash = function(self,data)
	local items = data:toString():split(":")
	if items[1]=="juanxia_slash"
	then
        local target = self.room:findPlayerByObjectName(items[2])
    	return self:isEnemy(target)
	end
end

sgs.ai_skill_playerchosen.juanxia = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:isWeak(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:doDisCard(target,"ej")
		and self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_choice.juanxia = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	local cs = {}
	for _,pn in sgs.list(items)do
		local dc = dummyCard(pn)
		if dc then
			dc:setSkillName("_juanxia")
			table.insert(cs,dc)
		end
	end
	self:sortByUseValue(cs)
	for _,c in sgs.list(cs)do
		local d = self:aiUseCard(c)
		if d.card and d.to:contains(target)
		then return c:objectName() end
	end
	if self:isFriend(target) then
		for _,c in sgs.list(cs)do
			if self:doDisCard(target,"ej")
			and (c:isKindOf("Snatch") or c:isKindOf("Dismantlement") or c:isKindOf("Zhujinquyuan"))
			then return c:objectName() end
		end
		for _,c in sgs.list(cs)do
			if c:targetFixed() and not c:isDamageCard()
			then return c:objectName() end
		end
	else
		for _,c in sgs.list(cs)do
			if not c:targetFixed() or c:isDamageCard()
			then return c:objectName() end
		end
	end
	return items[#items]
end

sgs.ai_skill_invoke.tenyearjuanxia_slash = function(self,data)
	local items = data:toString():split(":")
	if items[1]=="tenyearjuanxia_slash"
	then
        local target = self.room:findPlayerByObjectName(items[2])
    	return target and self:isEnemy(target)
	end
end

sgs.ai_skill_playerchosen.tenyearjuanxia = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:isWeak(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:doDisCard(target,"ej")
		and self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_choice.tenyearjuanxia = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	for _,pn in sgs.list(items)do
		local c = dummyCard(pn)
		if not c then continue end
		c:setSkillName("_tenyearjuanxia")
		local d = self:aiUseCard(c)
		if d.card and d.to:contains(target)
		then return pn end
	end
	if self:isFriend(target) then
		for _,pn in sgs.list(items)do
			local c = dummyCard(pn)
			if not c then continue end
			c:setSkillName("_tenyearjuanxia")
			if self:doDisCard(target,"ej")
			and (c:isKindOf("Snatch") or c:isKindOf("Dismantlement") or c:isKindOf("Zhujinquyuan"))
			then return pn end
		end
		for _,pn in sgs.list(items)do
			local c = dummyCard(pn)
			if not c then continue end
			c:setSkillName("_tenyearjuanxia")
			if c:targetFixed() and not c:isDamageCard()
			then return pn end
		end
	else
		for _,pn in sgs.list(items)do
			local c = dummyCard(pn)
			if not c then continue end
			c:setSkillName("_tenyearjuanxia")
			if not c:targetFixed() or c:isDamageCard()
			then return pn end
		end
	end
	return items[#items]
end

--周旋
local zhouxuan_skill = {}
zhouxuan_skill.name = "zhouxuan"
table.insert(sgs.ai_skills,zhouxuan_skill)
zhouxuan_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@ZhouxuanCard=.")
end

sgs.ai_skill_use_func.ZhouxuanCard = function(card,use,self)
	local id = -1
	if self:needToThrowArmor() and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId()) then
		id = self.player:getArmor():getEffectiveId()
	else
		local cards = sgs.QList2Table(self.player:getCards("he"))
		self:sortByKeepValue(cards)
		if cards[1]:isKindOf("Peach") or (cards[1]:isKindOf("Jink") and self:getCardsNum("Jink")==1) then return end
		id = cards[1]:getEffectiveId()
	end
	if id<0 or (self:getOverflow()<=0 and self.room:getCardPlace(id)==sgs.Player_PlaceHand) then return end
	use.card = sgs.Card_Parse("@ZhouxuanCard="..id)
end

sgs.ai_use_priority.ZhouxuanCard = 0
sgs.ai_card_intention.ZhouxuanCard = 0

sgs.ai_skill_playerchosen.zhouxuan = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"handcard")
	
	for _,p in ipairs(targets)do
		local flag = string.format("%s_%s_%s","visible",self.player:objectName(),p:objectName())
		for _,cc in sgs.qlist(p:getHandcards())do
			if (cc:hasFlag("visible") or cc:hasFlag(flag)) and cc:isKindOf("TrickCard") and not cc:isKindOf("Nullification") then
				sgs.ai_skill_choice.zhouxuan = "TrickCard"
				return p
            end
        end
	end
	
	for _,p in ipairs(targets)do
		local flag = string.format("%s_%s_%s","visible",self.player:objectName(),p:objectName())
		for _,cc in sgs.qlist(p:getHandcards())do
			if (cc:hasFlag("visible") or cc:hasFlag(flag)) and cc:isKindOf("EquipCard") and p:canUse(cc) then
				sgs.ai_skill_choice.zhouxuan = "EquipCard"
				return p
            end
        end
	end
	
	for _,p in ipairs(targets)do
		local flag = string.format("%s_%s_%s","visible",self.player:objectName(),p:objectName())
		for _,cc in sgs.qlist(p:getHandcards())do
			if (cc:hasFlag("visible") or cc:hasFlag(flag)) and cc:isKindOf("Peach") and p:canUse(cc) then
				sgs.ai_skill_choice.zhouxuan = "peach"
				return p
            end
        end
	end
	
	for _,p in ipairs(targets)do
		local flag = string.format("%s_%s_%s","visible",self.player:objectName(),p:objectName())
		for _,cc in sgs.qlist(p:getHandcards())do
			if (cc:hasFlag("visible") or cc:hasFlag(flag)) and cc:isKindOf("Slash") and self:canUse(cc,self:getEnemies(p),p) then
				sgs.ai_skill_choice.zhouxuan = cc:objectName()
				return p
            end
        end
	end
	
	for _,p in ipairs(targets)do
		local flag = string.format("%s_%s_%s","visible",self.player:objectName(),p:objectName())
		for _,cc in sgs.qlist(p:getHandcards())do
			if (cc:hasFlag("visible") or cc:hasFlag(flag)) and cc:isKindOf("Analeptic") and p:canUse(cc) then
				sgs.ai_skill_choice.zhouxuan = cc:objectName()
				return p
            end
        end
	end
	
	sgs.ai_skill_choice.zhouxuan = "slash"
	return targets[#targets]
end

sgs.ai_skill_askforyiji.zhouxuan = function(self,card_ids,tos)
	return sgs.ai_skill_askforyiji.nosyiji(self,card_ids,tos)
end

sgs.ai_skill_invoke.wangzu = function(self,data)
	return self.player:getHandcardNum()>1 or self:isWeak()
end

sgs.ai_skill_cardask["@wangzu-discard"] = function(self,data)
    local damage = data:toDamage()
    local cards = self.player:getCards("h")
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
	if self:canDamageHp(damage.from,damage.card,damage.to)
	and damage.damage<2
	then return "." end
	if #cards>0	then return cards[1]:getEffectiveId() end
    return "."
end

addAiSkills("yingrui").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
  	if #cards>0
	then
		return sgs.Card_Parse("@YingruiCard="..cards[1]:getEffectiveId())
	end
end

sgs.ai_skill_use_func["YingruiCard"] = function(card,use,self)
	self:sort(self.enemies,"card",true)
	for _,ep in sgs.list(self.enemies)do
		if ep:getCardCount()>0
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.YingruiCard = 4.4
sgs.ai_use_priority.YingruiCard = 3.8

sgs.ai_skill_discard.yingrui = function(self,x,n)
	local ids = {}
    local cards = sgs.QList2Table(self.player:getCards("he"))
    self:sortByKeepValue(cards) -- 按保留值排序
   	for _,h in sgs.list(cards)do
		if #ids>=n then break end
		if h:isKindOf("EquipCard") and self:isWeak()
		then table.insert(ids,h:getEffectiveId()) end
	end
	return #ids>=n and ids or {}
end

sgs.ai_skill_invoke.fuyuan = function(self,data)
	local target = data:toPlayer()
	if target then
		return not self:isEnemy(target)
	end
end

sgs.ai_skill_choice.olfengji = function(self,choices)
	local items = choices:split("+")
	if table.contains(items,"draw")
	and #self.friends_noself>0
	and self.player:getHandcardNum()>3
	then return "draw" end
	if table.contains(items,"slash")
	and #self.friends_noself>0
	then
		local slash = dummyCard("slash")
		local n = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,slash)
		for _,to in sgs.list(self.enemies)do
			if self.player:canSlash(to)
			and self:isWeak(to)
			then n = n-1 end
		end
		if n>0
		then
			return "slash"
		end
	end
	return items[#items]
end

sgs.ai_skill_playerchosen.olfengji = function(self,players)
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
	return destlist[1]
end




--魅步
sgs.ai_skill_cardask["@tenyearmeibu-dis"] = function(self,data)
	local player = data:toPlayer()
	if self:isEnemy(player)
	then
		local cards = {}
		for _,c in sgs.qlist(self.player:getCards("he"))do
			if self.player:canDiscard(self.player,c:getEffectiveId())
			then table.insert(cards,c) end
		end
		if #cards>0 then
			self:sortByKeepValue(cards)
			for _,c in ipairs(cards)do
				if not self:isValuableCard(c) then return "$"..c:getEffectiveId() end
			end
		end
	end
	if not self:isFriend(player)
	and not player:hasSkill("tenyearzhixi",true)
	then
		if self:needToThrowArmor()
		and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId())
		then return "$"..self.player:getArmor():getEffectiveId() end
	end
	return "."
end

sgs.ai_skill_cardask["@secondtenyearmeibu-dis"] = function(self,data)
	return sgs.ai_skill_cardask["@tenyearmeibu-dis"](self,data)
end

--穆穆
sgs.ai_skill_invoke.tenyearmumu = function(self,data)
	local targets,targets2 = {},{}
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if (self:isFriend(p) and p:hasSkills(sgs.lose_equip_skill)) or (self:isEnemy(p) and self:doDisCard(p,"e")) then
			table.insert(targets,p)
		end
		if not p:getArmor() then continue end
		if (self:isFriend(p) and p:hasSkills(sgs.lose_equip_skill)) or (self:isEnemy(p) and self:doDisCard(p,"e")) then
			table.insert(targets2,p)
		end
	end
	
	if #targets2>0 then
		local will_use_slash = false
		if self:getCardsNum("Slash")>0 then
			local slash = dummyCard("slash")
			local dummy_use = dummy()
			for _,p in sgs.qlist(self.room:getAlivePlayers())do
				if not self:isWeak(p) then
					table.insert(dummy_use.current_targets,p)
				end
			end
			self:useCardSlash(slash,dummy_use)
			if dummy_use.card and dummy_use.to:length()>0 then
				will_use_slash = true
			end
		end
		if not will_use_slash then
			sgs.ai_skill_choice.tenyearmumu = "get"
			self:sort(targets2)
			for _,p in ipairs(targets2)do
				if self:isEnemy(p) then
					sgs.ai_skill_playerchosen.tenyearmumu = p
					return true
				end
			end
			sgs.ai_skill_playerchosen.tenyearmumu = targets2[1]
			return true
		end
	end
	
	if #targets>0 then
		sgs.ai_skill_choice.tenyearmumu = "discard"
		self:sort(targets)
		for _,p in ipairs(targets)do
			if self:isEnemy(p) then
				sgs.ai_skill_playerchosen.tenyearmumu = p
				return true
			end
		end
		sgs.ai_skill_playerchosen.tenyearmumu = targets[1]
		return true
	end
	return false
end

sgs.ai_skill_invoke.SecondTenyearMumu = function(self,data)
	local targets,targets2 = {},{}
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if (self:isFriend(p) and p:hasSkills(sgs.lose_equip_skill)) or (self:isEnemy(p) and self:doDisCard(p,"e")) then
			table.insert(targets,p)
		end
		if not p:hasEquip() then continue end
		if (self:isFriend(p) and p:hasSkills(sgs.lose_equip_skill)) or (self:isEnemy(p) and self:doDisCard(p,"e")) then
			table.insert(targets2,p)
		end
	end
	
	if #targets2>0 then
		local will_use_slash = false
		if self:getCardsNum("Slash")>0 then
			local slash = dummyCard("slash")
			local dummy_use = dummy()
			for _,p in sgs.qlist(self.room:getAlivePlayers())do
				if not self:isWeak(p) then
					table.insert(dummy_use.current_targets,p)
				end
			end
			self:useCardSlash(slash,dummy_use)
			if dummy_use.card and dummy_use.to:length()>0 then
				will_use_slash = true
			end
		end
		if not will_use_slash then
			sgs.ai_skill_choice.secondtenyearmumu = "get"
			self:sort(targets2)
			for _,p in ipairs(targets2)do
				if self:isEnemy(p) then
					sgs.ai_skill_playerchosen.secondtenyearmumu = p
					return true
				end
			end
			sgs.ai_skill_playerchosen.secondtenyearmumu = targets2[1]
			return true
		end
	end
	
	if #targets>0 then
		sgs.ai_skill_choice.secondtenyearmumu = "discard"
		self:sort(targets)
		for _,p in ipairs(targets)do
			if self:isEnemy(p) then
				sgs.ai_skill_playerchosen.secondtenyearmumu = p
				return true
			end
		end
		sgs.ai_skill_playerchosen.secondtenyearmumu = targets[1]
		return true
	end
	return false
end

--止息
sgs.ai_skill_discard.tenyearzhixi = function(self,discard_num,min_num,optional,include_equip)
	return self:askForDiscard("dummyreason",1,1,false,false)
end

--鬻爵
function getYujueTarget(self)
	--[[local enemies = {}   --对敌人  待补充
	for _,p in ipairs(self.enemies)do
		if p:getHandcardNum()~=1 then continue end
		if p:hasSkill("kongcheng") then continue end  --AOE不需要考虑空城
		table.insert(enemies,p)
	end
	for _,p in ipairs(enemies)do]]

	for _,p in ipairs(self.friends_noself)do
		if p:isKongcheng() then continue end
		if self:needToThrowLastHandcard(p) then
			return p
		end
	end
	self:sort(self.friends_noself,"handcard")
	self.friends_noself = sgs.reverse(self.friends_noself)
	for _,p in ipairs(self.friends_noself)do
		if self:doDisCard(p,"h") then
			return p
		end
	end
	for _,p in ipairs(self.friends_noself)do
		if p:isKongcheng() then continue end
		return p
	end
	return nil
end

local yujue_skill = {}
yujue_skill.name = "yujue"
table.insert(sgs.ai_skills,yujue_skill)
yujue_skill.getTurnUseCard = function(self,inclusive)
	sgs.yujue_target = getYujueTarget(self)
	if sgs.yujue_target then
		return sgs.Card_Parse("@YujueCard=.")
	end
end

sgs.ai_skill_use_func.YujueCard = function(card,use,self)
	use.card = card
end

sgs.ai_use_priority.YujueCard = 7
sgs.ai_use_value.YujueCard = 1

sgs.ai_skill_choice.yujue = function(self,choices,data)
	local items = choices:split("+")
	if self:needToThrowArmor() and self.player:hasEquipArea(1) and table.contains(items,"1") then
		return "1"
	elseif self.player:hasEquipArea(4) and not self.player:getTreasure() and table.contains(items,"4") then
		return "4"
	elseif self.player:hasEquipArea(1) and not self.player:getArmor() and table.contains(items,"1") then
		return "1"	
	elseif self.player:hasEquipArea(0) and not self.player:getWeapon() and table.contains(items,"0") then
		return "0"
	elseif self.player:hasEquipArea(3) and not self.player:getOffensiveHorse() and table.contains(items,"3") then
		return "3"	
	elseif self.player:hasEquipArea(2) and not self.player:getDefensiveHorse() and table.contains(items,"2") then
		return "2"
	elseif self.player:hasEquipArea(4) and not self:keepWoodenOx() and table.contains(items,"4") then
		return "4"
	elseif self.player:hasEquipArea(1) and table.contains(items,"1") then
		return "1"	
	elseif self.player:hasEquipArea(0) and table.contains(items,"0") then
		return "0"	
	elseif self.player:hasEquipArea(3) and table.contains(items,"3") then
		return "3"
	elseif self.player:hasEquipArea(2) and table.contains(items,"2") then
		return "2"
	else
		return items[1]
	end
	return items[math.random(1,#items)]
end

sgs.ai_skill_playerchosen.yujue = function(self,targets)
	if sgs.yujue_target and targets:contains(sgs.yujue_target) then return sgs.yujue_target end
	local p = getYujueTarget(self)
	if p then return p end
	return targets:at(math.random(0,targets:length()-1))
end

sgs.ai_skill_discard.yujue = function(self,discard_num,min_num,optional,include_equip)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByUseValue(cards,true)
	return {cards[1]:getEffectiveId()}
end

--二版鬻爵
local secondyujue_skill = {}
secondyujue_skill.name = "secondyujue"
table.insert(sgs.ai_skills,secondyujue_skill)
secondyujue_skill.getTurnUseCard = function(self,inclusive)
	sgs.secondyujue_target = getYujueTarget(self)
	if sgs.secondyujue_target then
		return sgs.Card_Parse("@SecondYujueCard=.")
	end
end

sgs.ai_skill_use_func.SecondYujueCard = function(card,use,self)
	use.card = card
end

sgs.ai_use_priority.SecondYujueCard = sgs.ai_use_priority.YujueCard
sgs.ai_use_value.SecondYujueCard = sgs.ai_use_value.YujueCard

sgs.ai_skill_playerchosen.secondyujue = function(self,targets)
	if sgs.secondyujue_target and targets:contains(sgs.secondyujue_target) then return sgs.secondyujue_target end
	local p = getYujueTarget(self)
	if p then return p end
	return targets:at(math.random(0,targets:length()-1))
end

--逆乱
local spniluan_skill = {}
spniluan_skill.name = "spniluan"
table.insert(sgs.ai_skills,spniluan_skill)
spniluan_skill.getTurnUseCard = function(self,inclusive)
	--if self.player:hasSkill("nuzhan") then sgs.ai_use_priority.SpNiluanCard = sgs.ai_use_priority.Slash+0.05 end
	--return sgs.Card_Parse("@SpNiluanCard=.")
	local allcards = self.player:getCards("he")
	allcards = sgs.QList2Table(allcards)
	for _,id in sgs.qlist(self.player:getHandPile())do
		if sgs.Sanguosha:getCard(id):isBlack() then
			table.insert(allcards,sgs.Sanguosha:getCard(id))
		end
	end
	local cards = {}
	self:sortByUseValue(allcards,true)
	for _,c in ipairs(allcards)do
		if not c:isBlack() then continue end
		local slash = dummyCard()
		slash:setSkillName("spniluan")
		slash:addSubcard(c)
		if slash:isAvailable(self.player) then return slash end
	end
end

sgs.ai_skill_use_func.SpNiluanCard = function(card,use,self)
	local allcards = self.player:getCards("he")
	allcards = sgs.QList2Table(allcards)
	for _,id in sgs.qlist(self.player:getHandPile())do
		if sgs.Sanguosha:getCard(id):isBlack() then
			table.insert(allcards,sgs.Sanguosha:getCard(id))
		end
	end
	local cards = {}
	self:sortByUseValue(allcards,true)
	for _,c in ipairs(allcards)do
		if not c:isBlack() then continue end
		local slash = dummyCard("slash")
		slash:setSkillName("spniluan")
		slash:addSubcard(c)
		if self.player:isLocked(slash) then continue end
		local dummy_use = dummy()
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if p:getHp()<=self.player:getHp() then
				table.insert(dummy_use.current_targets,p)
			end
		end
		self:useCardSlash(slash,dummy_use)
		if dummy_use.card and dummy_use.to:length()>0 then
			table.insert(cards,c)
		end
	end
	if #cards==0 then return end
	local black_card
	local useAll = false
	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if enemy:getHp()==1 and not enemy:hasArmorEffect("EightDiagram") and self.player:distanceTo(enemy)<=self.player:getAttackRange() and self:isWeak(enemy)
			and getCardsNum("Jink",enemy,self.player)+getCardsNum("Peach",enemy,self.player)+getCardsNum("Analeptic",enemy,self.player)==0 then
			useAll = true
			break
		end
	end

	local disCrossbow = false
	if self:getCardsNum("Slash")<2 or self.player:hasSkills("paoxiao|tenyearpaoxiao|olpaoxiao") then
		disCrossbow = true
	end

	local nuzhan_equip = false
	local nuzhan_equip_e = false
	self:sort(self.enemies,"defense")
	if self.player:hasSkill("nuzhan") then
		for _,enemy in ipairs(self.enemies)do
			if not enemy:hasArmorEffect("EightDiagram") and self.player:distanceTo(enemy)<=self.player:getAttackRange()
			and getCardsNum("Jink",enemy)<1 then
				nuzhan_equip_e = true
				break
			end
		end
		for _,card in ipairs(cards)do
			if card:isKindOf("TrickCard") and nuzhan_equip_e then
				nuzhan_equip = true
				break
			end
		end
	end

	local nuzhan_trick = false
	local nuzhan_trick_e = false
	self:sort(self.enemies,"defense")
	if self.player:hasSkill("nuzhan") and not self.player:hasFlag("hasUsedSlash") and self:getCardsNum("Slash")>1 then
		for _,enemy in ipairs(self.enemies)do
			if not enemy:hasArmorEffect("EightDiagram") and self.player:distanceTo(enemy)<=self.player:getAttackRange() then
				nuzhan_trick_e = true
				break
			end
		end
		for _,card in ipairs(cards)do
			if card:isKindOf("TrickCard") and nuzhan_trick_e then
				nuzhan_trick = true
				break
			end
		end
	end

	for _,card in ipairs(cards)do
		local slash = dummyCard("slash")
		if not card:isKindOf("Slash") and not (nuzhan_equip or nuzhan_trick)
			and (not isCard("Peach",card,self.player) and not isCard("ExNihilo",card,self.player) and not useAll)
			and (not isCard("Crossbow",card,self.player) or disCrossbow)
			and (self:getUseValue(card)<sgs.ai_use_value.Slash or sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,slash)>0) then
			black_card = card
			break
		end
	end

	if nuzhan_equip then
		for _,card in ipairs(cards)do
			if card:isKindOf("EquipCard") then
				black_card = card
				break
			end
		end
	end

	if nuzhan_trick then
		for _,card in ipairs(cards)do
			if card:isKindOf("TrickCard")then
				black_card = card
				break
			end
		end
	end

	if black_card then
		local slash = dummyCard("slash")
		slash:setSkillName("spniluan")
		slash:addSubcard(black_card)
		if self.player:isLocked(slash) then return end
		local dummy_use = dummy()
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if p:getHp()<=self.player:getHp() then
				table.insert(dummy_use.current_targets,p)
			end
		end
		self:useCardSlash(slash,dummy_use)
		if dummy_use.card and dummy_use.to:length()>0 then
			use.card = sgs.Card_Parse("@SpNiluanCard="..black_card:getEffectiveId())
			for i = 1,math.min(dummy_use.to:length(),1+sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,slash))do
				use.to:append(dummy_use.to:at(i-1))
			end
		end
	end
end

sgs.ai_use_priority.SpNiluanCard = sgs.ai_use_priority.Slash-0.05

--违忤
local weiwu_skill = {}
weiwu_skill.name = "weiwu"
table.insert(sgs.ai_skills,weiwu_skill)
weiwu_skill.getTurnUseCard = function(self,inclusive)
	if self.player:isNude() and self.player:getHandPile():isEmpty() then return end
	return sgs.Card_Parse("@WeiwuCard=.")
end

sgs.ai_skill_use_func.WeiwuCard = function(card,use,self)
	local disCrossbow = false
	if self:getCardsNum("Slash")<2 or self.player:hasSkills("paoxiao|tenyearpaoxiao|olpaoxiao") then
		disCrossbow = true
	end
	local allcards = {}
	for _,c in sgs.qlist(self.player:getCards("he"))do
		if c:isKindOf("WoodenOx") and not self.player:getPile("wooden_ox"):isEmpty() then continue end
		if c:isRed() and not c:isKindOf("Snatch") and not c:isKindOf("Peach") and not c:isKindOf("ExNihilo") and (not c:isKindOf("Crossbow") or disCrossbow) then
			table.insert(allcards,c)
		end
	end
	for _,id in sgs.qlist(self.player:getHandPile())do
		local c = sgs.Sanguosha:getCard(id)
		if c:isRed() and not c:isKindOf("Snatch") and not c:isKindOf("Peach") and not c:isKindOf("ExNihilo") and (not c:isKindOf("Crossbow") or disCrossbow) then
			table.insert(allcards,c)
		end
	end
	if #allcards==0 then return end
	self:sortByUseValue(allcards,true)
	local cards = {}
	for _,c in ipairs(allcards)do
		local snatch = dummyCard("snatch")
		snatch:setSkillName("weiwu")
		snatch:addSubcard(c)
		if self.player:isLocked(snatch) then continue end
		local dummy_use = dummy()
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if p:getHandcardNum()<=self.player:getHandcardNum() then
				table.insert(dummy_use.current_targets,p)
			end
		end
		self:aiUseCard(snatch,dummy_use)
		if dummy_use.card and dummy_use.to:length()>0 then
			if self:getUseValue(c)<self:getUseValue(snatch) or sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,snatch)>0 then
				table.insert(cards,c)
			end
		end
	end
	if #cards==0 then return end
	local snatch = dummyCard("snatch")
	snatch:setSkillName("weiwu")
	snatch:addSubcard(cards[1])
	if self.player:isLocked(snatch) then return end
	local dummy_use = dummy()
	for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if p:getHandcardNum()<=self.player:getHandcardNum()
		then table.insert(dummy_use.current_targets,p) end
	end
	self:aiUseCard(snatch,dummy_use)
	if dummy_use.card and dummy_use.to:length()>0 then
		use.card = sgs.Card_Parse("@WeiwuCard="..cards[1]:getEffectiveId())
		for i = 1,math.min(dummy_use.to:length(),1+sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,snatch))do
			use.to:append(dummy_use.to:at(i-1))
		end
	end
end

sgs.ai_use_priority.WeiwuCard = sgs.ai_use_priority.Snatch+0.05

--攻坚
function gongjianDoNotDis2(self,to)
	if to:getCardCount(true)<2 then return true end
    local n = 0
	if self:doDisCard(to,"e") then
		for _,c in sgs.qlist(to:getCards("e"))do
			if self.player:canDiscard(to,c:getEffectiveId()) then n = n+1 end
		end
	end
	if n<2 then
		if not self:hasLoseHandcardEffective(to,2-n) then return true end
		if to:getHandcardNum()<=2-n and self:needKongcheng(to) then return true end
	end
	if self:hasSkills(sgs.lose_equip_skill,to) and to:getHandcardNum()<2 then return true end
	if to:getCardCount(true)<=2 and self:needToThrowArmor(to) then return true end
end

sgs.ai_skill_invoke.gongjian = function(self,data)
	local player = data:toPlayer()
	if self:isFriend(player) then return false end
	
	local use = self.player:getTag("gongjianData"):toCardUse()
	if not use then
		--[[local n = player:getHandcardNum()
		for _,c in sgs.qlist(player:getCards("e"))do
			if n>=2 then break end
			if self.player:canDiscard(player,c:getEffectiveId()) then n = n+1 end
		end
		if n<=0 then return false end
		n = math.min(2,n)
		return not self:doNotDiscard(player,"he",nil,n)]]
		return self:doDisCard(player,"he")
	end
	
	if player:objectName()==use.to:last():objectName() then
		--[[local n = player:getHandcardNum()
		for _,c in sgs.qlist(player:getCards("e"))do
			if n>=2 then break end
			if self.player:canDiscard(player,c:getEffectiveId()) then n = n+1 end
		end
		if n==0 then return false end
		n = math.min(2,n)
		return not self:doNotDiscard(player,"he",nil,n)]]
		return self:doDisCard(player,"he")
	end
	
	local players = {}
	local names = self.room:getTag("gongjian_slash_targets"):toStringList()
	for _,p in sgs.qlist(use.to)do
		if self:isFriend(p) or not table.contains(names,p:objectName()) then continue end
		local n = p:getHandcardNum()
		for _,c in sgs.qlist(p:getCards("e"))do
			if n>=2 then break end
			if self.player:canDiscard(p,c:getEffectiveId()) then n = n+1 end
		end
		if n==0 then continue end
		n = math.min(2,n)
		if n==2 and not gongjianDoNotDis2(self,p) then
			table.insert(players,p)
		end
	end
	if #players>0 then
		self:sort(players)
		return player:objectName()==players[1]:objectName()
	end
	for _,p in sgs.qlist(use.to)do
		if self:isFriend(p) or not table.contains(names,p:objectName()) then continue end
		if not self:doDisCard(player,"he") then continue end
		table.insert(players,p)
	end
	if #players>0 then
		self:sort(players)
		return player:objectName()==players[1]:objectName()
	end
	return false
end

sgs.ai_skill_choice.gongjian = function(self,choices,data)
	local player = data:toPlayer()
	if self:isFriend(player) and self:needToThrowLastHandcard(player,2) then return "2" end
	if self:isFriend(player) then return "1" end
	if gongjianDoNotDis2(self,player) then return "1" end
	return "2"
end

--慈孝
sgs.ai_skill_playerchosen.cixiao = function(self,targets)
	if #self.enemies>0 then
		self:sort(self.enemies,"chaofeng")
		local n = math.random(1,100)
		if n<=25 then self.player:speak("伤害不高，但侮辱性极强") end
		return self.enemies[1]
	end
	return nil
end

sgs.ai_skill_use["@@cixiao"] = function(self,prompt,method)
	if #self.enemies>0 then
		self:sort(self.enemies,"chaofeng")
		self.enemies = sgs.reverse(self.enemies)
		local first
		if self.player:getMark("&cxyizi")>0 then first = self.player:objectName() end
		if not first then
			for _,p in ipairs(self.enemies)do
				if p:getMark("&cxyizi")>0 then
					first = p:objectName()
					break
				end
			end
		end
		if not first then return "." end
		local second
		self.enemies = sgs.reverse(self.enemies)
		
		if self:needToThrowArmor() and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId()) then
			for _,p in ipairs(self.enemies)do
				if p:getMark("&cxyizi")==0 and p:objectName()~=first then
					second = p:objectName()
					break
				end
			end
			if second then
				return "@CixiaoCard="..self.player:getArmor():getEffectiveId().."->"..first.."+"..second
			end
			for _,p in ipairs(self.enemies)do
				if p:objectName()~=first then
					second = p:objectName()
					break
				end
			end
			if second then
				return "@CixiaoCard="..self.player:getArmor():getEffectiveId().."->"..first.."+"..second
			end
			if self.player:objectName()~=first then
				return "@CixiaoCard="..self.player:getArmor():getEffectiveId().."->"..first.."+"..self.player:objectName()
			end
			return "."
		end
		
		for _,p in ipairs(self.enemies)do
			if p:objectName()==first then break end
			if p:getMark("&cxyizi")==0 then
				second = p:objectName()
				break
			end
		end
		if not second then return "." end
		local cards = sgs.QList2Table(self.player:getCards("he"))
		self:sortByKeepValue(cards)
		for _,c in ipairs(cards)do
			if not self:isValuableCard(c) and self.player:canDiscard(self.player,c:getEffectiveId()) then
				return "@CixiaoCard="..c:getEffectiveId().."->"..first.."+"..second
			end
		end
	end
	return "."
end

--叛弑
sgs.ai_skill_askforyiji.panshi = function(self,card_ids)
	local cards = {}
	for _,id in ipairs(card_ids)do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
	self:sortByUseValue(cards,true)
	
	local dingyuans,num,fri = {},0,0
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if p:hasFlag("dingyuan") then
			table.insert(dingyuans,p)
			if not self:isEnemy(p) then
				num = num+1
			end
			if self:isFriend(p) then
				fri = fri+1
			end
		end
	end
	self:sort(dingyuans)
	if self.player:getHandcardNum()<=num then
		for _,p in ipairs(dingyuans)do
			if self:isFriend(p) then
				return p,cards[1]:getEffectiveId()
			end
		end
		for _,p in ipairs(dingyuans)do
			if not self:isEnemy(p) then
				return p,cards[1]:getEffectiveId()
			end
		end
		return dingyuans[1],cards[1]:getEffectiveId()
	else
		for _,p in ipairs(dingyuans)do
			if self:isEnemy(p) then
				return p,cards[1]:getEffectiveId()
			end
		end
		
		if self.player:getHandcardNum()<=fri then
			for _,p in ipairs(dingyuans)do
				if self:isFriend(p) then
					return p,cards[1]:getEffectiveId()
				end
			end
			return dingyuans[1],cards[1]:getEffectiveId()
		else
			for _,p in ipairs(dingyuans)do
				if not self:isFriend(p) then
					return p,cards[1]:getEffectiveId()
				end
			end
			return dingyuans[1],cards[1]:getEffectiveId()
		end
	end
	return dingyuans[1],cards[1]:getEffectiveId()
end

--节应
sgs.ai_skill_playerchosen.jieyingh = function(self,targets)
	local slash = dummyCard()
	self:sort(self.enemies,"handcard",true)
	for _,p in ipairs(self.enemies)do
		if self:canUse(slash,self.friends,p)
		and (self:hasCrossbowEffect(p) or p:hasSkills(sgs.double_slash_skill))
		then
			if getCardsNum("Slash",p,self.player)>1
			then return p end
		end
	end
	self:sort(self.friends_noself,"handcard")
	for _,p in ipairs(self.friends_noself)do
		if #self:getEnemies(p)>1
		and getCardsNum("ExNihilo,Snatch,Dismantlement,Duel",p,self.player)>0
		then return p end
	end
	for _,p in ipairs(self.friends_noself)do
		if not self:hasCrossbowEffect(p) then
			if (getCardsNum("Slash",p,self.player)>0 or p:getHandcardNum()>=3)
			and not self:canUse(slash,self:getEnemies(p),p)
			then
				for _,enemy in ipairs(self:getEnemies(p))do
					if p:canSlash(enemy,nil,false) and not p:inMyAttackRange(enemy)
					then return p end
				end
			end
		end
	end
	for _,p in ipairs(self.friends_noself)do
		if (getCardsNum("Slash",p,self.player)>0 or p:getHandcardNum()>=3)
		and not self:canUse(slash,self:getEnemies(p),p)
		then
			for _,enemy in ipairs(self:getEnemies(p))do
				if p:canSlash(enemy,nil,false) and not p:inMyAttackRange(enemy)
				then return p end
			end
		end
	end
	for _,p in ipairs(self.friends_noself)do
		if self:canUse(slash,self:getEnemies(p),p) and #self:getEnemies(p)>1
		and (getCardsNum("Slash",p,self.player)==1 or p:getHandcardNum()>=3)
		then return p end
	end
	for _,p in ipairs(self.friends_noself)do
		if p:getHandcardNum()<3
		then return p end
	end
	self:sort(self.enemies,"handcard")
	if #self.enemies>0 and #self:getEnemies(self.enemies[1])<=1
	then return self.enemies[1] end--]]
end

sgs.ai_skill_use["@@jieyingh"] = function(self,prompt,method)
	if self.player:hasFlag("jieyingh_now_use_collateral")
	then
		local dummy_use = dummy()
		dummy_use.current_targets = self.player:property("extra_collateral"):toString():split("+")
		local card = sgs.Card_Parse(dummy_use.current_targets[1])
		if not card then return "." end
		self:useCardCollateral(card,dummy_use)
		if dummy_use.card and dummy_use.to:length()==2 then
			return "@ExtraCollateralCard=.->"..dummy_use.to:first().."+"..dummy_use.to:last()
		end
	else
		local use = self.player:getTag("jieyinghData"):toCardUse()
		if not use then return "." end
		self.player:setTag("yb_zhuzhan2_data",ToData(use))
		local tos = sgs.SPlayerList()
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if use.to:contains(p) then continue end
			if self.player:canUse(use.card,p,true)
			then tos:append(p) end
		end
		tos = sgs.ai_skill_playerchosen.yb_zhuzhan2(self,tos)
		if tos then return "@JieyinghCard=.->"..tos:objectName() end
	end
	return "."
end

--危迫
sgs.ai_skill_discard.weipo = function(self,discard_num,min_num,optional,include_equip)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByUseValue(cards,true)
	return {cards[1]:getEffectiveId()}
end

--敏思
addAiSkills("minsi").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	local function toids(cs)
		local ids = {}
		for _,c1 in sgs.list(cs)do
			table.insert(ids,c1:getEffectiveId())
		end
		return ids
	end
	local function minsiNumber(cs,c)
		local n = c:getNumber()
		for _,c1 in sgs.list(cs)do
			n = n+c1:getNumber()
		end
		return n==13 and 2 or n<=13 and 1 or 0 
	end
  	for _,c in sgs.list(cards)do
		local cs = {c}
		for i,c1 in sgs.list(cards)do
			if table.contains(cs,c1) then continue end
			i = minsiNumber(cs,c1)
			if i>0
			then
				table.insert(cs,c1)
				if i>1
				then
					cs = toids(cs)
					return sgs.Card_Parse("@MinsiCard="..table.concat(cs,"+"))
				end
				for i,c2 in sgs.list(cards)do
					if table.contains(cs,c2) then continue end
					i = minsiNumber(cs,c2)
					if i>0
					then
						table.insert(cs,c2)
						if i>1
						then
							cs = toids(cs)
							return sgs.Card_Parse("@MinsiCard="..table.concat(cs,"+"))
						end
					end
				end
			end
		end
	end
  	for _,c in sgs.list(cards)do
		if c:getNumber()==13
		then
			return sgs.Card_Parse("@MinsiCard="..c:getEffectiveId())
		end
	end
end

sgs.ai_skill_use_func["MinsiCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.MinsiCard = 9.4
sgs.ai_use_priority.MinsiCard = 4.8

--吉境
sgs.ai_skill_invoke.jijing = function(self,data)
    return self.player:isWounded()
end

sgs.ai_skill_use["@@jijing"] = function(self,prompt)
	local valid,ts = {},sgs.IntList()
	local jn = self.player:property("jijing_judge"):toInt()
	local function toids(cs)
		local ids = {}
		for _,c1 in sgs.list(cs)do
			table.insert(ids,c1:getEffectiveId())
		end
		return ids
	end
	local function minsiNumber(cs,c)
		local n = c:getNumber()
		for _,c1 in sgs.list(cs)do
			n = n+c1:getNumber()
		end
		return n==jn and 2 or n<=jn and 1 or 0 
	end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		if c:getNumber()==jn
		then
			return string.format("@JijingCard=%s",c:getEffectiveId())
		end
	end
  	for _,c in sgs.list(cards)do
		local cs = {c}
		for i,c1 in sgs.list(cards)do
			if table.contains(cs,c1) then continue end
			i = minsiNumber(cs,c1)
			if i>0
			then
				table.insert(cs,c1)
				if i>1
				then
					cs = toids(cs)
					return string.format("@JijingCard=%s",table.concat(cs,"+"))
				end
				for i,c2 in sgs.list(cards)do
					if table.contains(cs,c2) then continue end
					i = minsiNumber(cs,c2)
					if i>0
					then
						table.insert(cs,c2)
						if i>1
						then
							cs = toids(cs)
							return string.format("@JijingCard=%s",table.concat(cs,"+"))
						end
					end
				end
			end
		end
	end
end

--追德
sgs.ai_skill_playerchosen.zhuide = function(self,targets)
	self:sort(self.friends_noself)
	for _,p in ipairs(self.friends_noself)do
		if self:needKongcheng(p,true) or hasManjuanEffect(p) then continue end
		return p
	end
	local jink,ana,peach,slash = false,false,false,false
	for _,id in sgs.qlist(self.room:getDrawPile())do
		local card = sgs.Sanguosha:getCard(id)
		if card:isKindOf("Jink") then jink = true
		elseif card:isKindOf("Analeptic") then ana = true
		elseif card:isKindOf("Peach") then peach = true
		elseif card:isKindOf("Slash") then slash = true end
		if jink and ana and peach and slash then break end
	end
	if (jink and ana) or (jink and peach) or (ana and peach) then
		for _,p in ipairs(self.friends_noself)do
			if hasManjuanEffect(p) then continue end
			return p
		end
	end
	if slash and not jink and not ana and not peach then
		self:sort(self.enemies)
		for _,p in ipairs(self.enemies)do
			if hasManjuanEffect(p) then continue end
			if not self:needKongcheng(p,true) then continue end
			if self:getEnemyNumBySeat(self.player,p,p)>0 then
				return p
			end
		end
	end
	return nil
end

--毒逝
sgs.ai_skill_playerchosen.spdushi = function(self,targets)
	targets = sgs.QList2Table(targets)
	for _,p in ipairs(targets)do
		if self:isEnemy(p) and not p:hasSkill("spdushi",true) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if not self:isFriend(p) and not p:hasSkill("spdushi",true) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if self:isEnemy(p) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if not self:isFriend(p) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if self:isFriend(p) and p:hasSkill("spdushi",true) then
			return p
		end
	end
	targets = sgs.reverse(targets)
	return targets[1]
end

--盗戟
local daoji_skill = {}
daoji_skill.name = "daoji"
table.insert(sgs.ai_skills,daoji_skill)
daoji_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@DaojiCard=.")
end

sgs.ai_skill_use_func.DaojiCard = function(card,use,self)
	local id = -1
	if self:needToThrowArmor() and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId()) then id = self.player:getArmor():getEffectiveId() end
	if id<0 and self.player:getWeapon() and self.player:canDiscard(self.player,self.player:getWeapon():getEffectiveId()) then id = self.player:getWeapon():getEffectiveId() end
	local cards = {}
	for _,c in sgs.qlist(self.player:getCards("he"))do
		if c:isKindOf("BasicCard") or self.player:canDiscard(self.player,c:getEffectiveId()) then continue end
		table.insert(cards,c)
	end
	if #cards>0 then
		self:sortByKeepValue(cards)
		if cards[1]:objectName()~="wooden_ox" or self.player:getPile("wooden_ox"):isEmpty() then
			if id<0 then id = cards[1]:getEffectiveId() end
		end
	end
	
	self:sort(self.enemies,"hp")
	self.daoji_throwcard = nil
	if id>=0 then
		for _,p in ipairs(self.enemies)do
			local damage = self:ajustDamage(self.player,p)
			if damage>=p:getHp() and self:damageIsEffective(p,nil,self.player) and p:getWeapon() then
				local weapon = sgs.Sanguosha:getCard(p:getWeapon():getEffectiveId())
				if self.player:canUse(weapon) then
					self.daoji_throwcard = weapon
					use.card = sgs.Card_Parse("@DaojiCard="..id)
					use.to:append(p)
					return
				end
			end
		end
		for _,p in ipairs(self.enemies)do
			if self:damageIsEffective(p,nil,self.player) and p:getWeapon() and self:doDisCard(p,"e",true) then
				local weapon = sgs.Sanguosha:getCard(p:getWeapon():getEffectiveId())
				if self.player:canUse(weapon) then
					self.daoji_throwcard = weapon
					use.card = sgs.Card_Parse("@DaojiCard="..id)
					use.to:append(p)
					return
				end
			end
		end
	end
	
	self:sort(self.enemies)
	if self.player:getTreasure() and self.player:canDiscard(self.player,self.player:getTreasure():getEffectiveId()) then
		if self.player:getTreasure():objectName()~="wooden_ox" or self.player:getPile("wooden_ox"):isEmpty() then
			for _,p in ipairs(self.enemies)do
				if p:getTreasure() and self:doDisCard(p,"e",true) then
					local treasure = sgs.Sanguosha:getCard(p:getTreasure():getEffectiveId())
					if treasure:objectName()=="wooden_ox" and not p:getPile("wooden_ox"):isEmpty() then
						if self.player:canUse(treasure) then
							self.daoji_throwcard = treasure
							use.card = sgs.Card_Parse("@DaojiCard="..self.player:getTreasure():getEffectiveId())
							use.to:append(p)
							return
						end
					end
				end
			end
		end
	end
	if self.player:getArmor() and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId()) then
		for _,p in ipairs(self.enemies)do
			if p:getArmor() and self:doDisCard(p,"e",true) then
				local armor = sgs.Sanguosha:getCard(p:getArmor():getEffectiveId())
				if self.player:canUse(armor) then
					self.daoji_throwcard = armor
					use.card = sgs.Card_Parse("@DaojiCard="..self.player:getArmor():getEffectiveId())
					use.to:append(p)
					return
				end
			end
		end
	end
	if self.player:getDefensiveHorse() and self.player:canDiscard(self.player,self.player:getDefensiveHorse():getEffectiveId()) then
		for _,p in ipairs(self.enemies)do
			if p:getDefensiveHorse() and self:doDisCard(p,"e",true) then
				local horse = sgs.Sanguosha:getCard(p:getDefensiveHorse():getEffectiveId())
				if self.player:canUse(horse) then
					self.daoji_throwcard = horse
					use.card = sgs.Card_Parse("@DaojiCard="..self.player:getDefensiveHorse():getEffectiveId())
					use.to:append(p)
					return
				end
			end
		end
	end
	if self.player:getOffensiveHorse() and self.player:canDiscard(self.player,self.player:getOffensiveHorse():getEffectiveId()) then
		for _,p in ipairs(self.enemies)do
			if p:getOffensiveHorse() and self:doDisCard(p,"e",true) then
				local horse = sgs.Sanguosha:getCard(p:getOffensiveHorse():getEffectiveId())
				if self.player:canUse(horse) then
					self.daoji_throwcard = horse
					use.card = sgs.Card_Parse("@DaojiCard="..self.player:getOffensiveHorse():getEffectiveId())
					use.to:append(p)
					return
				end
			end
		end
	end
	if self.player:getTreasure() and self.player:canDiscard(self.player,self.player:getTreasure():getEffectiveId()) then
		if self.player:getTreasure():objectName()~="wooden_ox" or self.player:getPile("wooden_ox"):isEmpty() then
			for _,p in ipairs(self.enemies)do
				if p:getTreasure() and self:doDisCard(p,"e",true) then
					local treasure = sgs.Sanguosha:getCard(p:getTreasure():getEffectiveId())
					if self.player:canUse(treasure) then
						self.daoji_throwcard = treasure
						use.card = sgs.Card_Parse("@DaojiCard="..self.player:getTreasure():getEffectiveId())
						use.to:append(p)
						return
					end
				end
			end
		end
	end
	
	if id>=0 then
		for _,p in ipairs(self.enemies)do
			if self:doDisCard(p,"e",true) then
				local c
				if p:getTreasure() and p:getTreasure():objectName()=="wooden_ox" and not p:getPile("wooden_ox"):isEmpty() then
					c = sgs.Sanguosha:getCard(p:getTreasure():getEffectiveId())
				elseif p:getArmor() and not self.player:getArmor() then
					c = sgs.Sanguosha:getCard(p:getArmor():getEffectiveId())
				elseif p:getDefensiveHorse() and not self.player:getDefensiveHorse() then
					c = sgs.Sanguosha:getCard(p:getDefensiveHorse():getEffectiveId())
				elseif p:getOffensiveHorse() and not self.player:getOffensiveHorse() then
					c = sgs.Sanguosha:getCard(p:getOffensiveHorse():getEffectiveId())
				elseif p:getTreasure() and not self.player:getTreasure() then
					c = sgs.Sanguosha:getCard(p:getTreasure():getEffectiveId())
				end
				if c then
					self.daoji_throwcard = treasure
					use.card = sgs.Card_Parse("@DaojiCard="..id)
					use.to:append(p)
					return
				end
			end
		end
		for _,p in ipairs(self.enemies)do
			if self:doDisCard(p,"e",true) then
				use.card = sgs.Card_Parse("@DaojiCard="..id)
				use.to:append(p)
				return
			end
		end
	end
end

sgs.ai_use_priority.DaojiCard = sgs.ai_use_priority.Slash+0.1

sgs.ai_skill_cardchosen.daoji = function(self,who,flags)
	if self.daoji_throwcard and who:getCards("e"):contains(self.daoji_throwcard) then return self.daoji_throwcard end
	if who:getCards("e"):length()==1 then return who:getCards("e"):first() end
	return self:askForCardChosen(who,"e","snatch")
end

--手杀逆乱
sgs.ai_skill_cardask["@mobileniluan"] = function(self,data,pattern,target)
	if target then
		for _,slash in ipairs(self:getCards("Slash"))do
			if self:isFriend(target) and self:slashIsEffective(slash,target) then
				if self:needLeiji(target,self.player) then return slash:toString() end
				if self:needToLoseHp(target,self.player,slash,true) then return slash:toString() end
			end
			if self:isEnemy(target) and self:slashIsEffective(slash,target)
			and not self:needToLoseHp(target,self.player,slash) and not self:needLeiji(target,self.player) 
			then return slash:toString() end
		end
	end
	return "."
end

--骁袭
sgs.ai_view_as.mobilexiaoxi = function(card,player,card_place)
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	if card_place~=sgs.Player_PlaceSpecial and card:isBlack() and not card:isKindOf("Peach") and not card:hasFlag("using") then
		return ("slash:mobilexiaoxi[%s:%s]=%d"):format(suit,number,card_id)
	end
end

local mobilexiaoxi_skill = {}
mobilexiaoxi_skill.name = "mobilexiaoxi"
table.insert(sgs.ai_skills,mobilexiaoxi_skill)
mobilexiaoxi_skill.getTurnUseCard = function(self,inclusive)
	local cards = self:addHandPile("he")
	local red_card
	self:sortByUseValue(cards,true)

	local useAll = false
	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if enemy:getHp()==1 and not enemy:hasArmorEffect("EightDiagram") and self.player:distanceTo(enemy)<=self.player:getAttackRange() and self:isWeak(enemy)
			and getCardsNum("Jink",enemy,self.player)+getCardsNum("Peach",enemy,self.player)+getCardsNum("Analeptic",enemy,self.player)==0 then
			useAll = true
			break
		end
	end

	local disCrossbow = false
	if self:getCardsNum("Slash")<2 or self.player:hasSkills("paoxiao|tenyearpaoxiao|olpaoxiao") then
		disCrossbow = true
	end

	local nuzhan_equip = false
	local nuzhan_equip_e = false
	self:sort(self.enemies,"defense")
	if self.player:hasSkill("nuzhan") then
		for _,enemy in ipairs(self.enemies)do
			if  not enemy:hasArmorEffect("EightDiagram") and self.player:distanceTo(enemy)<=self.player:getAttackRange()
			and getCardsNum("Jink",enemy)<1 then
				nuzhan_equip_e = true
				break
			end
		end
		for _,card in ipairs(cards)do
			if card:isBlack() and card:isKindOf("TrickCard") and nuzhan_equip_e then
				nuzhan_equip = true
				break
			end
		end
	end

	local nuzhan_trick = false
	local nuzhan_trick_e = false
	self:sort(self.enemies,"defense")
	if self.player:hasSkill("nuzhan") and not self.player:hasFlag("hasUsedSlash") and self:getCardsNum("Slash")>1 then
		for _,enemy in ipairs(self.enemies)do
			if  not enemy:hasArmorEffect("EightDiagram") and self.player:distanceTo(enemy)<=self.player:getAttackRange() then
				nuzhan_trick_e = true
				break
			end
		end
		for _,card in ipairs(cards)do
			if card:isBlack() and card:isKindOf("TrickCard") and nuzhan_trick_e then
				nuzhan_trick = true
				break
			end
		end
	end

	for _,card in ipairs(cards)do
		if card:isBlack() and not card:isKindOf("Slash") and not (nuzhan_equip or nuzhan_trick)
			and (not isCard("Peach",card,self.player) and not isCard("ExNihilo",card,self.player) and not useAll)
			and (not isCard("Crossbow",card,self.player) or disCrossbow)
			and (self:getUseValue(card)<sgs.ai_use_value.Slash or inclusive or sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,dummyCard("slash"))>0) then
			red_card = card
			break
		end
	end

	if nuzhan_equip then
		for _,card in ipairs(cards)do
			if card:isBlack() and card:isKindOf("EquipCard") then
				red_card = card
				break
			end
		end
	end

	if nuzhan_trick then
		for _,card in ipairs(cards)do
			if card:isBlack() and card:isKindOf("TrickCard")then
				red_card = card
				break
			end
		end
	end

	if red_card then
		local suit = red_card:getSuitString()
		local number = red_card:getNumberString()
		local card_id = red_card:getEffectiveId()
		return sgs.Card_Parse(("slash:mobilexiaoxi[%s:%s]=%d"):format(suit,number,card_id))
	end
end

function sgs.ai_cardneed.mobilexiaoxi(to,card)
	return to:getHandcardNum()<3 and card:isBlack()
end

--评荐
sgs.ai_skill_invoke.pingjian = function(self,data)
    return true
end

function PingjianSkill(player)
	local pingjian_skills = player:property("pingjian_has_used_skills"):toStringList()
	local sks = canAiSkills()
	for i=1,99 do
		if #sks<1 then break end
		local sk = sks[math.random(1,#sks)]
		if table.contains(pingjian_skills,sk.name) then continue end
		i = sgs.Sanguosha:getSkill(sk.name)
		if i
		then
			i = i:getDescription()
			if string.find(i,"出牌阶段限一次，")
			or string.find(i,"阶段技，")
			or string.find(i,"出牌阶段限一次。")
			or string.find(i,"阶段技。")
			or string.find(i,"出牌阶段限一次")
			or string.find(i,"阶段技")
			then return sk end
		end
	end
end

addAiSkills("pingjian").getTurnUseCard = function(self)
	for i=1,3 do
		local tosk = PingjianSkill(self.player)
		if tosk and sgs.Sanguosha:getViewAsSkill(tosk.name):isEnabledAtPlay(self.player) then
			self.pjsk_to = tosk.ai_fill_skill(self,false)
			if not self.pjsk_to then continue end
			local use = self:aiUseCard(self.pjsk_to)
			if use.card then
				self.pjsk_to = use.to
				sgs.ai_use_priority.PingjianCard = sgs.ai_use_priority[use.card:getClassName()] or 5
				return sgs.Card_Parse("@PingjianCard="..use.card:subcardString()..":"..tosk.name.."|"..use.card:getUserString())
			end
		end
	end
end

sgs.ai_skill_use_func["PingjianCard"] = function(card,use,self)
	if self.pjsk_to then
		use.card = card
		use.to = self.pjsk_to
	end
end

sgs.ai_use_value.PingjianCard = 9.4
sgs.ai_use_priority.PingjianCard = 10.8


--授符
local shoufu_skill = {}
shoufu_skill.name = "shoufu"
table.insert(sgs.ai_skills,shoufu_skill)
shoufu_skill.getTurnUseCard = function(self)
	for _,p in ipairs(self.enemies)do
		if p:getPile("sflu"):isEmpty() then
			return sgs.Card_Parse("@ShoufuCard=.")
		end
	end
end

sgs.ai_skill_use_func.ShoufuCard = function(card,use,self)
	use.card = card
end

sgs.ai_use_priority.ShoufuCard = sgs.ai_use_priority.Slash+1
sgs.ai_use_value.ShoufuCard = 3.4

sgs.ai_skill_use["@@shoufu!"] = function(self,prompt,method)
	self:sort(self.enemies,"handcard")
	self.enemies = sgs.reverse(self.enemies)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	for _,p in ipairs(self.enemies)do
		if p:getPile("sflu"):isEmpty() then
			return "@ShoufuPutCard="..cards[1]:getEffectiveId().."->"..p:objectName()
		end
	end
	return "."
end

--颂词
local tenyearsongci_skill = {}
tenyearsongci_skill.name = "tenyearsongci"
table.insert(sgs.ai_skills,tenyearsongci_skill)
tenyearsongci_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@TenyearSongciCard=.")
end

sgs.ai_skill_use_func.TenyearSongciCard = function(card,use,self)
	self:sort(self.friends,"handcard")
	for _,friend in ipairs(self.friends)do
		if friend:getMark("tenyearsongci"..self.player:objectName())==0 and friend:getHandcardNum()<=friend:getHp() and self:canDraw(friend) then
			use.card = sgs.Card_Parse("@TenyearSongciCard=.")
			use.to:append(friend)
			return
		end
	end

	self:sort(self.enemies,"handcard")
	self.enemies = sgs.reverse(self.enemies)
	for _,enemy in ipairs(self.enemies)do
		if enemy:getMark("tenyearsongci"..self.player:objectName())==0 and enemy:getHandcardNum()>enemy:getHp() and not enemy:isNude()
		and self:doDisCard(enemy,"he",false,2) then
			use.card = sgs.Card_Parse("@TenyearSongciCard=.")
			use.to:append(enemy)
			return
		end
	end
end

sgs.ai_use_value.TenyearSongciCard = sgs.ai_use_value.SongciCard
sgs.ai_use_priority.TenyearSongciCard = sgs.ai_use_priority.SongciCard

--游龙
addAiSkills("youlong").getTurnUseCard = function(self)
	for _,name in sgs.list(patterns())do
        local c = dummyCard(name)
		if c and c:isAvailable(self.player)
		and self.player:getMark("youlong_"..name)<1
		and (c:isKindOf("BasicCard") and self.player:getMark("youlong_basic_lun")<1 and self.player:getChangeSkillState("youlong")>1
		or c:isNDTrick() and self.player:getMark("youlong_trick_lun")<1 and self.player:getChangeSkillState("youlong")<2)
		and self:getCardsNum(c:getClassName())<1
		and self.player:hasEquipArea()
		then
         	local dummy = self:aiUseCard(c)
    		if dummy.card and dummy.to
	     	then
				self.youlong_to = dummy.to
	           	if c:canRecast() and dummy.to:length()<1 then continue end
				sgs.ai_use_priority.YoulongCard = sgs.ai_use_priority[c:getClassName()]
                return sgs.Card_Parse("@YoulongCard=.:"..name)
			end
		end
	end
end

sgs.ai_skill_use_func["YoulongCard"] = function(card,use,self)
	use.card = card
	use.to = self.youlong_to
end

sgs.ai_use_value.YoulongCard = 10.4
sgs.ai_use_priority.YoulongCard = 10.4

sgs.ai_guhuo_card.youlong = function(self,toname,class_name)
	if self:getCardsNum(class_name)<1 and self.player:getMark("youlong_"..toname)<1 then
        return "@YoulongCard=.:"..toname
	end
end

--鸾凤
sgs.ai_skill_invoke.luanfeng = function(self,data)
	local target = data:toPlayer()
	if target:hasSkill("niepan") and target:getMark("@nirvana")>0 then return false end
	if target:hasSkill("mobileniepan") and target:getMark("@mobileniepanMark")>0 then return false end
	if target:hasSkill("olniepan") and target:getMark("@olniepanMark")>0 then return false end
	return self:isFriend(target)
end

--战意
local secondzhanyi_skill = {}
secondzhanyi_skill.name = "secondzhanyi"
table.insert(sgs.ai_skills,secondzhanyi_skill)
secondzhanyi_skill.getTurnUseCard = function(self)
	if not self.player:hasUsed("SecondZhanyiCard") then
		return sgs.Card_Parse("@SecondZhanyiCard=.")
	end
	if self.player:getMark("ViewAsSkill_secondzhanyiEffect-PlayClear")>0 then
		local use_basic = self:ZhanyiUseBasic()
		local cards = self.player:getCards("h")
		cards=sgs.QList2Table(cards)
		self:sortByUseValue(cards,true)
		local BasicCards = {}
		for _,card in ipairs(cards)do
			if card:isKindOf("BasicCard") then
				table.insert(BasicCards,card)
			end
		end
		if use_basic and #BasicCards>0 then
			return sgs.Card_Parse("@SecondZhanyiViewAsBasicCard="..BasicCards[1]:getId()..":"..use_basic)
		end
	end
end

sgs.ai_skill_use_func.SecondZhanyiCard = function(card,use,self)
	local to_discard
	local cards = self.player:getCards("h")
	cards=sgs.QList2Table(cards)
	self:sortByUseValue(cards,true)

	local TrickCards = {}
	for _,card in ipairs(cards)do
		if card:isKindOf("Disaster") or card:isKindOf("GodSalvation") or card:isKindOf("AmazingGrace") or self:getCardsNum("TrickCard")>1 then
			table.insert(TrickCards,card)
		end
	end
	if #TrickCards>0 and (self.player:getHp()>2 or self:getCardsNum("Peach")>0 ) and self.player:getHp()>1 then
		to_discard = TrickCards[1]
	end

	local EquipCards = {}
	if self:needToThrowArmor() and self.player:getArmor() then table.insert(EquipCards,self.player:getArmor()) end
	for _,card in ipairs(cards)do
		if card:isKindOf("EquipCard") then
			table.insert(EquipCards,card)
		end
	end
	if not self:isWeak() and self.player:getDefensiveHorse() then table.insert(EquipCards,self.player:getDefensiveHorse()) end
	if self.player:hasTreasure("wooden_ox") and self.player:getPile("wooden_ox"):length()==0 then table.insert(EquipCards,self.player:getTreasure()) end
	self:sort(self.enemies,"defense")
	if self:getCardsNum("Slash")>0 and
	((self.player:getHp()>2 or self:getCardsNum("Peach")>0 ) and self.player:getHp()>1) then
		for _,enemy in ipairs(self.enemies)do
			if (self:isWeak(enemy)) or (enemy:getCardCount(true)<=4 and enemy:getCardCount(true)>=1)
				and self.player:canSlash(enemy) and self:slashIsEffective(dummyCard("slash"),enemy,self.player)
				and self.player:inMyAttackRange(enemy) and not self:needToThrowArmor(enemy) then
				to_discard = EquipCards[1]
				break
			end
		end
	end

	local BasicCards = {}
	for _,card in ipairs(cards)do
		if card:isKindOf("BasicCard") then
			table.insert(BasicCards,card)
		end
	end
	local use_basic = self:ZhanyiUseBasic()
	if (use_basic=="peach" and self.player:getHp()>1 and #BasicCards>3)
	--or (use_basic=="analeptic" and self.player:getHp()>1 and #BasicCards>2)
	or (use_basic=="slash" and self.player:getHp()>1 and #BasicCards>1)
	then
		to_discard = BasicCards[1]
	end

	if to_discard then
		use.card = sgs.Card_Parse("@SecondZhanyiCard="..to_discard:getEffectiveId())
		return
	end
end

sgs.ai_use_priority.SecondZhanyiCard = sgs.ai_use_priority.ZhanyiCard

sgs.ai_skill_use_func.SecondZhanyiViewAsBasicCard=function(card,use,self)
	local userstring=card:toString()
	userstring=(userstring:split(":"))[3]
	local zhanyicard=dummyCard(userstring,card:getSuit(),card:getNumber())
	zhanyicard:setSkillName("secondzhanyi")
	if zhanyicard:getTypeId()==sgs.Card_TypeBasic then
		if not use.isDummy and use.card and zhanyicard:isKindOf("Slash") and (not use.to or use.to:isEmpty()) then return end
		self:useBasicCard(zhanyicard,use)
	end
	if not use.card then return end
	use.card=card
end

sgs.ai_use_priority.SecondZhanyiViewAsBasicCard = sgs.ai_use_priority.ZhanyiViewAsBasicCard

--偏宠
sgs.ai_skill_invoke.pianchong = function(self,data)
	return self.player:getPile("yiji"):isEmpty()
end

sgs.ai_skill_choice.pianchong = function(self,choices,data)
	local use_red,use_black,red,black = 0,0,0,0
	for _,c in sgs.qlist(self.player:getCards("h"))do
		if not self:willUse(self.player,c,false,false,true) then continue end
		if c:isRed() then use_red = use_red+1
		elseif c:isBlack() then use_black = use_black+1 end
	end
	if use_red>use_black then return "red"
	elseif use_red<use_black then return "black"
	else
		if red>black then return "red"
		elseif red<black then return "black" end
	end
	choices = choices:split("+")
	return choices[math.random(1,#choices)]
end

--尊位
local zunwei_skill = {}
zunwei_skill.name = "zunwei"
table.insert(sgs.ai_skills,zunwei_skill)
zunwei_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@ZunweiCard=.")
end

sgs.ai_skill_use_func.ZunweiCard = function(card,use,self)
	local recover_t,draw_t,equip_t = {},{},{}
	if not self.player:property("zunwei_draw"):toBool() and self:canDraw() then
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if p:getHandcardNum()>self.player:getHandcardNum() then
				table.insert(draw_t,p)
			end
		end
	end
	if not self.player:property("zunwei_recover"):toBool() and self.player:getLostHp()>0 then
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if p:getHp()>self.player:getHp() then
				table.insert(recover_t,p)
			end
		end
	end
	if not self.player:property("zunwei_equip"):toBool() and self.player:hasEquipArea() then
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if p:getEquips():length()>self.player:getEquips():length() then
				table.insert(equip_t,p)
			end
		end
	end
	if #recover_t==0 and #draw_t==0 and #equip_t==0 then return end
	
	if #recover_t>0 then self:sort(recover_t,"hp") recover_t = sgs.reverse(recover_t) end
	if #draw_t>0 then self:sort(draw_t,"handcard") draw_t = sgs.reverse(draw_t) end
	if #equip_t>0 then self:sort(equip_t,"equip") equip_t = sgs.reverse(equip_t) end
	
	if self:isWeak() then
		if #recover_t>0 then
			sgs.ai_use_priority.ZunweiCard = 10
			sgs.ai_skill_choice.zunwei = "recover"
			use.card = card
			use.to:append(recover_t[1])
			return
		end
		
		if #draw_t>0 then
			sgs.ai_skill_choice.zunwei = "draw"
			use.card = card
			use.to:append(draw_t[1])
			return
		end
		
		if #equip_t>0 then
			sgs.ai_skill_choice.zunwei = "equip"
			use.card = card
			use.to:append(equip_t[1])
			return
		end
	end
	
	if #recover_t>0 and recover_t[1]:getHp()-self.player:getHp()>=2 and self.player:getLostHp()>=2 then
		sgs.ai_skill_choice.zunwei = "recover"
		use.card = card
		use.to:append(recover_t[1])
		return
	end
		
	if #draw_t>0 and ((draw_t[1]:getHandcardNum()-self.player:getHandcardNum()>=2 and sgs.Slash_IsAvailable(self.player)) or
	draw_t[1]:getHandcardNum()-self.player:getHandcardNum()>=4) then
		sgs.ai_skill_choice.zunwei = "draw"
		use.card = card
		use.to:append(draw_t[1])
		return
	end
		
	if #equip_t>0 and equip_t[1]:getEquips():length()-self.player:getEquips():length()>=2 then
		sgs.ai_skill_choice.zunwei = "equip"
		use.card = card
		use.to:append(equip_t[1])
		return
	end
end

sgs.ai_use_priority.ZunweiCard = 0

--讨灭
sgs.ai_skill_invoke.taomie = function(self,data)
	local player = data:toPlayer()
	return not self:isFriend(player)
end

sgs.ai_skill_choice.taomie = function(self,choices,data)
	local damage = data:toDamage()
	local to = damage.to
	choices = choices:split("+")
	
	local damage = getChoice(choices,"damage")
	local get = getChoice(choices,"get")
	local all = getChoice(choices,"all")
	
	if #choices==2 then
		table.removeOne(choices,all)
		return choices[1]
	end
	if #choices==3 then
		if self:cantDamageMore(self.player,to) then
			if self:doDisCard(to) then return get end
			return damage
		end
		if self:doDisCard(to) then return get end
		return all
	end
	return choices[1]
end

sgs.ai_skill_askforyiji.taomie = function(self,card_ids,tos)
	return sgs.ai_skill_askforyiji.nosyiji(self,card_ids,tos)
end

--粮营
sgs.ai_skill_invoke.liangying = function(self,data)
	for _,p in ipairs(self.friends)do
		if self:canDraw(p) then return true end
	end
	return false
end

sgs.ai_skill_choice.liangying = function(self,choices,data)
	choices = choices:split("+")
	local n = tonumber(choices[#choices])
	n = math.min(n,#self.friends)
	for _,p in ipairs(self.friends)do
		if not self:canDraw(p) then n = n-1 end
	end
	if n>0 then
		return ""..n
	else
		return choices[1]
	end
end

sgs.ai_skill_askforyiji.liangying = function(self,card_ids)
	local friends = {}
	for _,p in ipairs(self.friends)do
		if p:getMark("liangying-Clear")<=0
		then
			table.insert(friends,p)
		end
	end
	local toGive,allcards = {},{}
	local keep
	for _,id in ipairs(card_ids)do
		local card = sgs.Sanguosha:getCard(id)
		if not keep and (isCard("Jink",card,self.player) or isCard("Analeptic",card,self.player))
		then keep = true else table.insert(toGive,card) end
		table.insert(allcards,card)
	end
	local cards = #toGive>0 and toGive or allcards
	self:sortByKeepValue(cards,true)
	local id = cards[1]:getId()
	local card,friend = self:getCardNeedPlayer(cards,true,friends)
	if card and friend and table.contains(friends,friend) then return friend,card:getId() end
	if #friends>0
	then
		self:sort(friends,"handcard")
		for _,afriend in ipairs(friends)do
			if not self:needKongcheng(afriend,true)
			then return afriend,id end
		end
		self:sort(friends,"defense")
		return friends[1],id
	end
	return nil,-1
end

--把盏
local bazhan_skill = {}
bazhan_skill.name = "bazhan"
table.insert(sgs.ai_skills,bazhan_skill)
bazhan_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@BazhanCard=.")
end

sgs.ai_skill_use_func.BazhanCard = function(card,use,self)
	local n = self.player:getChangeSkillState("bazhan")
	if n~=1 and n~=2 then return end
	if n==1 then
		if self.player:isKongcheng() then return end
		local cards = sgs.QList2Table(self.player:getCards("h"))
		self:sortByUseValue(cards,true)
		local hearts = {}
		for _,c in ipairs(cards)do
			if c:isKindOf("Analeptic") or c:getSuit()==sgs.Card_Heart then
				table.insert(hearts,c)
			end
		end
		if #hearts>0 then
			self:sort(self.friends_noself,"hp")
			for _,p in ipairs(self.friends_noself)do
				if self:isWeak(p) and p:getLostHp()>0 and not (p:isKongcheng() and self:needKongcheng(p,true)) then
					sgs.ai_use_priority.BazhanCard = 7
					use.card = sgs.Card_Parse("@BazhanCard="..hearts[1]:getEffectiveId())
					use.to:append(p)
					return
				end
			end
			
			self:sort(self.friends_noself)
			for _,p in ipairs(self.friends_noself)do
				if not p:faceUp() and not (p:isKongcheng() and self:needKongcheng(p,true)) then
					sgs.ai_use_priority.BazhanCard = 7
					use.card = sgs.Card_Parse("@BazhanCard="..hearts[1]:getEffectiveId())
					use.to:append(p)
					return
				end
			end
			
			local card,friend = self:getCardNeedPlayer({hearts[1]},false)
			if card and friend then
				sgs.ai_use_priority.BazhanCard = 7
				use.card = sgs.Card_Parse("@BazhanCard="..card:getEffectiveId())
				use.to:append(friend)
				return
			end
		end
		
		if self:getOverflow()>0 then
			if not cards[1]:isKindOf("Jink") and not cards[1]:isKindOf("Peach") and not cards[1]:isKindOf("Analeptic") and not cards[1]:isKindOf("ExNihilo") then
				self:sort(self.enemies)
				for _,p in ipairs(self.enemies)do
					if p:isKongcheng() and self:needKongcheng(p,true) then
						use.card = sgs.Card_Parse("@BazhanCard="..cards[1]:getEffectiveId())
						use.to:append(p)
						return
					end
				end
			end
			
			local card,friend = self:getCardNeedPlayer({cards[1]},false)
			if card and friend then
				use.card = sgs.Card_Parse("@BazhanCard="..card:getEffectiveId())
				use.to:append(friend)
				return
			end
			
			self:sort(self.friends_noself,"handcard")
			for _,p in ipairs(self.friends_noself)do
				if not (p:isKongcheng() and self:needKongcheng(p,true)) and not self:willSkipPlayPhase(p) then
					use.card = sgs.Card_Parse("@BazhanCard="..cards[1]:getEffectiveId())
					use.to:append(p)
					return
				end
			end
			
			for _,p in ipairs(self.friends_noself)do
				if not (p:isKongcheng() and self:needKongcheng(p,true)) then
					use.card = sgs.Card_Parse("@BazhanCard="..cards[1]:getEffectiveId())
					use.to:append(p)
					return
				end
			end
		end
	else
		if self.player:isKongcheng() and self:needKongcheng(self.player,true) then return end
		self:sort(self.friends_noself)
		self.friends_noself = sgs.reverse(self.friends_noself)
		for _,p in ipairs(self.friends_noself)do
			if self:needToThrowLastHandcard(p) then
				use.card = sgs.Card_Parse("@BazhanCard=.")
				use.to:append(p)
				return
			end
		end
		
		self:sort(self.friends_noself,"handcard")
		self.friends_noself = sgs.reverse(self.friends_noself)
		for _,p in ipairs(self.friends_noself)do
			if self:doDisCard(p,"h",true) and not self:isWeak(p) then
				use.card = sgs.Card_Parse("@BazhanCard=.")
				use.to:append(p)
				return
			end
		end
		
		self:sort(self.enemies)
		for _,p in ipairs(self.enemies)do
			if self:doDisCard(p,"h",true) then
				sgs.ai_use_priority.BazhanCard = sgs.ai_use_priority.Snatch
				use.card = sgs.Card_Parse("@BazhanCard=.")
				use.to:append(p)
				return
			end
		end
		
		for _,p in ipairs(self.friends_noself)do
			if self:getOverflow(p)>1 and (not self:isWeak(p) or self:willSkipPlayPhase(p)) then
				use.card = sgs.Card_Parse("@BazhanCard=.")
				use.to:append(p)
				return
			end
		end
	end
end

sgs.ai_use_priority.BazhanCard = 0

sgs.ai_skill_choice.bazhan = function(self,choices,data)
	local to = data:toPlayer()
	if not self:isFriend(to) then return "cancel" end
	choices = choices:split("+")
	
	local recover = getChoice(choices,"recover")
	local reset = getChoice(choices,"reset")
	
	if recover then
		if self:isWeak(to) then return recover end
		if not to:faceUp() then return reset end
		if to:isChained() and self:getFinalRetrial(to)==2 then
			if self:hasSkills("leiji|nosleiji|olleiji|",self.enemies) then return reset end
			for _,p in sgs.qlist(self.room:getAlivePlayers())do
				if not p:containsTrick("YanxiaoCard") and p:containsTrick("lightning") then
					return reset
				end
			end
		end
		return recover
	end
	return reset
end

--醮影
sgs.ai_skill_playerchosen.jiaoying = function(self,targets)
	local player = sgs.ai_skill_playerchosen.jieming(self,targets)
	if player then return player end
	return self.player
end

--殃众
sgs.ai_skill_cardask["@yangzhong"] = function(self,data,pattern,target)
	if not target or target:isDead() or self:isFriend(target) then return "." end
	local dis = {}
	if target:getHp()<=1 and not hasBuquEffect(target) then
		dis = self:askForDiscard("dummyreason",2,2,false,true)
		if #dis>=2 then
			return "$"..table.concat(dis,"+")
		else
			return "."
		end
	end
	if target:hasSkill("zhaxiang") and not self:willSkipPlayPhase(target) then return "." end
	dis = self:askForDiscard("dummyreason",2,2,false,true)
	if #dis==2 then
		for _,id in ipairs(dis)do
			if sgs.Sanguosha:getCard(id):isKindOf("Peach") then return "." end
			if sgs.Sanguosha:getCard(id):isKindOf("Analeptic") and self:isWeak() then return "." end
		end
		return "$"..table.concat(dis,"+")
	end
	return "."
end

--礼赂
sgs.ai_skill_invoke.lilu = function(self,data)
	local invoke = false
	for _,p in ipairs(self.friends_noself)do
		if p:isKongcheng() and self:needKongcheng(p,true) then continue end
		invoke = true
		break
	end
	if not invoke then return false end
	local draw = math.min(self.player:getMaxHp(),5)-self.player:getHandcardNum()
	local mark = self.player:getMark("&lilu")
	if draw<=2 then
		if self:getOverflow()+math.max(draw,0)>=mark+1 and ((self.player:isSkipped(sgs.Player_Play) and self:getOverflow()+math.max(draw,0)>0) or
		self:getOverflow()-mark>=1 or mark<=1) then
			return true
		else
			return false
		end
	end
	--[[return draw+self.player:getHandcardNum()-mark>=2 or (self.player:hasSkill("kongcheng") and draw+self.player:getHandcardNum()<=mark) or 
		self.player:isSkipped(sgs.Player_Play)]]
	return true
end

sgs.ai_skill_use["@@lilu!"] = function(self,prompt)
	local mark = self.player:getMark("&lilu")
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByUseValue(cards,true)
	self:sort(self.friends_noself)
	
	local target
	for _,p in ipairs(self.friends_noself)do
		if self:canDraw(p) and not self:willSkipPlayPhase(p) then
			target = p
			break
		end
	end
	if not target then
		for _,p in ipairs(self.friends_noself)do
			if not (p:isKongcheng() and self:needKongcheng(p,true)) and not self:willSkipPlayPhase(p) then
				target = p
				break
			end
		end
	end
	if not target then
		for _,p in ipairs(self.friends_noself)do
			if self:canDraw(p) then
				target = p
				break
			end
		end
	end
	if not target then
		for _,p in ipairs(self.friends_noself)do
			if not (p:isKongcheng() and self:needKongcheng(p,true)) then
				target = p
				break
			end
		end
	end
	if not target then target = self.friends_noself[1] end
	
	local give,hand_num = {},self.player:getHandcardNum()
	if hand_num<mark+1 and target then
		if self:needToThrowLastHandcard(self.player,hand_num) then
			for _,c in ipairs(cards)do
				table.insert(give,c:getEffectiveId())
			end
			return "@LiluCard="..table.concat(give,"+").."->"..target:objectName()
		end
		local card,friend = self:getCardNeedPlayer(cards,false)
		if card and friend then
			return "@LiluCard="..card:getEffectiveId().."->"..friend:objectName()
		end
		return "@LiluCard="..cards[1]:getEffectiveId().."->"..target:objectName()
	end
	
	for i = 1,mark+1 do
		if #cards<i then break end
		table.insert(give,cards[i]:getEffectiveId())
	end
	if #give>0 and target then
		return "@LiluCard="..table.concat(give,"+").."->"..target:objectName()
	end
	return "."
end

--翊正
sgs.ai_skill_playerchosen.yizhengc = function(self,targets)
	if self.player:getMaxHp()<=3 or #self.friends_noself<=0 then return nil end
	local friends = {}
	for _,p in ipairs(self.friends_noself)do
		if p:getMaxHp()<self.player:getMaxHp() and p:getMark("&yizhengc+#"..self.player:objectName())<=0 and not self:willSkipPlayPhase(p) then
			table.insert(friends,p)
		end
	end
	if #friends>0 then
		self:sort(friends,"threat")
		return friends[1]
	end
	
	for _,p in ipairs(self.friends_noself)do
		if p:getMaxHp()<self.player:getMaxHp() and not self:willSkipPlayPhase(p) then
			table.insert(friends,p)
		end
	end
	if #friends>0 then
		self:sort(friends,"threat")
		return friends[1]
	end
	
	for _,p in ipairs(self.friends_noself)do
		if p:getMaxHp()<self.player:getMaxHp() and p:getMark("&yizhengc+#"..self.player:objectName())<=0 then
			table.insert(friends,p)
		end
	end
	if #friends>0 then
		self:sort(friends,"threat")
		return friends[1]
	end
	
	for _,p in ipairs(self.friends_noself)do
		if p:getMaxHp()<self.player:getMaxHp() then
			table.insert(friends,p)
		end
	end
	if #friends>0 then
		self:sort(friends,"threat")
		return friends[1]
	end
	
	self:sort(self.friends_noself,"threat")
	return self.friends_noself[1]
end

--十周年揖让
sgs.ai_skill_playerchosen.tenyearyirang = function(self,targets)
	return sgs.ai_skill_playerchosen.yirang(self,targets)
end

--凤魄
sgs.ai_skill_choice.newfengpo = function(self,choices,data)
	return sgs.ai_skill_choice.fengpo(self,choices,data)
end

--天匠
addAiSkills("tianjiang").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("e"))
	self:sortByKeepValue(cards)
  	for _,e in sgs.list(cards)do
		local equip = e:getRealCard():toEquipCard()
		local x = equip:location()
		for _,h in sgs.list(self.player:getCards("h"))do
			if h:isKindOf("EquipCard") then
				local eq = h:getRealCard():toEquipCard()
				local n = eq:location()
				if x==n then
					n = self.friends_noself
					if self:evaluateArmor(e)<-5
					then n = self.enemies end
					for _,ep in sgs.list(n)do
						if ep:hasEquipArea(x) then
							self.tj_to = ep
							if self:isFriend(ep) then
								if ep:getEquip(x)==nil
								then return sgs.Card_Parse("@TianjiangCard="..e:getEffectiveId()) end
							else
								if ep:getEquip(x)~=nil
								then return sgs.Card_Parse("@TianjiangCard="..e:getEffectiveId()) end
							end
						end
					end
				end
			end
		end
	end
  	for _,e in sgs.list(cards)do
		local equip = e:getRealCard():toEquipCard()
		local x = equip:location()
		for _,h in sgs.list(self.player:getCards("h"))do
			if h:isKindOf("EquipCard") then
				local eq = h:getRealCard():toEquipCard()
				local n = eq:location()
				if x==n then
					n = self.friends_noself
					if self:evaluateArmor(e)<-5
					then n = self.enemies end
					for _,ep in sgs.list(n)do
						if ep:hasEquipArea(x) then
							self.tj_to = ep
							if self:isFriend(ep) then
								if ep:getEquip(x)==nil
								then return sgs.Card_Parse("@TianjiangCard="..e:getEffectiveId()) end
							else return sgs.Card_Parse("@TianjiangCard="..e:getEffectiveId()) end
						end
					end
				end
			end
		end
	end
  	for _,e in sgs.list(cards)do
		local equip = e:getRealCard():toEquipCard()
		local x = equip:location()
		local n = self.friends_noself
		if self:evaluateArmor(e)<-5
		then n = self.enemies end
		for _,ep in sgs.list(n)do
			if ep:hasEquipArea(x) then
				self.tj_to = ep
				if self:isFriend(ep) then
					if ep:getEquip(x)==nil and self:isWeak(ep)
					then return sgs.Card_Parse("@TianjiangCard="..e:getEffectiveId()) end
				else return sgs.Card_Parse("@TianjiangCard="..e:getEffectiveId()) end
			end
		end
	end
  	for _,e in sgs.list(cards)do
		local equip = e:getRealCard():toEquipCard()
		local x = equip:location()
		for _,ep in sgs.list(self.friends_noself)do
			if ep:hasEquipArea(x) then
				self.tj_to = ep
				equip = e:objectName()
				if equip=="_hongduanqiang"
				or equip=="_liecuidao"
				or equip=="_shuibojian"
				or equip=="_hunduwanbi"
				or equip=="_tianleiren"
				then return sgs.Card_Parse("@TianjiangCard="..e:getEffectiveId()) end
			end
		end
	end
end

sgs.ai_skill_use_func["TianjiangCard"] = function(card,use,self)
	use.card = card
	use.to:append(self.tj_to)
end

sgs.ai_use_value.TianjiangCard = 9.4
sgs.ai_use_priority.TianjiangCard = 4.8


--铸刃
addAiSkills("zhuren").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	local toids = {}
  	for _,c in sgs.list(cards)do
		if c:getNumber()>6 and self.player:getWeapon()==nil
		then return sgs.Card_Parse("@ZhurenCard="..c:getEffectiveId()) end
	end
  	for _,c in sgs.list(cards)do
		if c:getNumber()<6
		and self:getCardsNum("Slash")<1
		and sgs.Slash_IsAvailable(self.player)
		then return sgs.Card_Parse("@ZhurenCard="..c:getEffectiveId()) end
	end
end

sgs.ai_skill_use_func["ZhurenCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.ZhurenCard = 9.4
sgs.ai_use_priority.ZhurenCard = 4.8

--短兵
sgs.ai_skill_playerchosen.olduanbing = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

--奋迅
addAiSkills("olfenxun").getTurnUseCard = function(self)
	return sgs.Card_Parse("@OLFenxunCard=.")
end

sgs.ai_skill_use_func["OLFenxunCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,ep in sgs.list(self.enemies)do
		if self.player:distanceTo(ep)>1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.OLFenxunCard = 3.4
sgs.ai_use_priority.OLFenxunCard = 8.8

--筹略
sgs.ai_skill_playerchosen.choulve = function(self,players)
	local name = self.player:property("choulve_damage_card"):toString()
	name = dummyCard(name)
	if not name then return end
	name:setSkillName("_choulve")
	name = self:aiUseCard(name)
	if name.card==nil then return end
	self.cl_to = name.to
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getCardCount()>0
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		and target:getCardCount()>0
		then return target end
	end
end

sgs.ai_skill_discard.choulve = function(self)
	local ids = {}
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
   	local target = self.room:getCurrent()
	if self:isFriend(target)
	and #cards>0
	then
		table.insert(ids,cards[1]:getEffectiveId())
	end
	return ids
end

sgs.ai_skill_use["@@choulve!"] = function(self,prompt)
    local c = self.player:property("choulve_damage_card"):toString()
	c = dummyCard(c)
	c:setSkillName("_choulve")
   	if self.cl_to then
      	local tos = {}
       	for _,p in sgs.list(self.cl_to)do
       		table.insert(tos,p:objectName())
       	end
		self.cl_to = nil
       	return c:toString().."->"..table.concat(tos,"+")
    end
end

--威仪
sgs.ai_skill_invoke.weiyi = function(self,data)
	local p = data:toPlayer()
	return self:isFriend(p) and p:getHp()<=self.player:getHp() and self:isWeak(p)
	or self:isEnemy(p) and p:getHp()>=self.player:getHp()
end

sgs.ai_skill_choice.weiyi = function(self,choices,data)
	local player = data:toPlayer()
	if not player or player:isDead() then return "cancel" end
	choices = choices:split("+")
	local recover = getChoice(choices,"recover")
	local losehp = getChoice(choices,"losehp")
	if self:isFriend(player) and recover then return recover end
	if self:isEnemy(player) and losehp then return losehp end  --不管诈降了
	return "cancel"
end

--锦织

addAiSkills("jinzhi").getTurnUseCard = function(self)
	return sgs.Card_Parse("@JinzhiCard=.")
end

sgs.ai_skill_use_func["JinzhiCard"] = function(card,use,self)
	local toids = {}
	local mark = self.player:getMark("&jinzhi_lun")
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
	if #cards<=mark then return end
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if h:getColor()~=cards[1]:getColor() then continue end
		table.insert(toids,h:getEffectiveId())
		if #toids>mark then break end
	end
	if #toids<=mark or #toids>=#cards/2 then return end
	for _,name in sgs.list(patterns())do
        local c = dummyCard(name)
		if c and c:isKindOf("BasicCard")
		and c:isAvailable(self.player)
		and self:getCardsNum(c:getClassName())<1
		then
         	for _,id in sgs.list(toids)do
				c:addSubcard(id)
			end
			c:setSuit(6)
			c:setNumber(0)
			local dummy = self:aiUseCard(c)
    		if dummy.card and dummy.to
	     	then
				toids = #toids>0 and table.concat(toids,"+") or "."
	           	if c:canRecast() and dummy.to:length()<1 then continue end
				sgs.ai_use_priority.JinzhiCard = sgs.ai_use_priority[c:getClassName()]
				use.card = sgs.Card_Parse("@JinzhiCard="..toids..":"..name)
				use.to = dummy.to
				break
			end
		end
	end
end

sgs.ai_use_value.JinzhiCard = 10.4
sgs.ai_use_priority.JinzhiCard = 10.4

sgs.ai_guhuo_card.jinzhi = function(self,toname,class_name)
	local toids = {}
	local mark = self.player:getMark("&jinzhi_lun")
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
	if #cards<=mark then return end
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if h:getColor()~=cards[1]:getColor() then continue end
		table.insert(toids,h:getEffectiveId())
		if #toids>mark then break end
	end
	if self:getCardsNum(class_name)<1
	and #toids>mark and #toids<3
	then
		toids = #toids>0 and table.concat(toids,"+") or "."
        return "@JinzhiCard="..toids..":"..toname
	end
end

--二版锦织
addAiSkills("secondjinzhi").getTurnUseCard = function(self)
	local toids = {}
	local mark = self.player:getMark("&secondjinzhi_lun")
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	if #cards<1 then return end
	for _,h in sgs.list(cards)do
		if h:getColor()~=cards[1]:getColor() then continue end
		table.insert(toids,h:getEffectiveId())
		if #toids>mark then break end
	end
	if #toids<=mark then return end
	for _,name in sgs.list(patterns())do
        local c = dummyCard(name)
		if c and c:isKindOf("BasicCard")
		and c:isAvailable(self.player)
		and self:getCardsNum(c:getClassName())<1
		then
         	for _,id in sgs.list(toids)do
				c:addSubcard(id)
			end
			c:setSuit(6)
			c:setNumber(0)
         	local dummy = self:aiUseCard(c)
    		if dummy.card and dummy.to
	     	then
				self.secondjinzhi_to = dummy.to
				toids = #toids>0 and table.concat(toids,"+") or "."
	           	if c:canRecast() and dummy.to:length()<1 then continue end
				sgs.ai_use_priority.SecondJinzhiCard = sgs.ai_use_priority[c:getClassName()]
                return sgs.Card_Parse("@SecondJinzhiCard="..toids..":"..name)
			end
		end
	end
end

sgs.ai_skill_use_func["SecondJinzhiCard"] = function(card,use,self)
	use.card = card
	use.to = self.secondjinzhi_to
end

sgs.ai_use_value.SecondJinzhiCard = 10.4
sgs.ai_use_priority.SecondJinzhiCard = 10.4

sgs.ai_guhuo_card.secondjinzhi = function(self,toname,class_name)
	local toids = {}
	local mark = self.player:getMark("&secondjinzhi_lun")
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
	if #cards<1 then return end
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if h:getColor()~=cards[1]:getColor() then continue end
		table.insert(toids,h:getEffectiveId())
		if #toids>mark then break end
	end
	if #toids>mark
	and self:getCardsNum(class_name)<1
	then
		toids = #toids>0 and table.concat(toids,"+") or "."
        return "@SecondJinzhiCard="..toids..":"..toname
	end
end


--兴作
sgs.ai_skill_invoke.xingzuo = function(self,data)
    return self.player:getHandcardNum()>1
end

sgs.ai_skill_use["@@xingzuo"] = function(self,prompt)
	local valid = {}
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	local cidlist = self.player:getTag("xingzuoForAI"):toIntList()
	for _,h in sgs.list(cards)do
		for _,id in sgs.list(cidlist)do
			local c = sgs.Sanguosha:getCard(id)
			if self:getKeepValue(c)>self:getKeepValue(h)
			and not table.contains(valid,h:getEffectiveId())
			and not table.contains(valid,c:getEffectiveId())
			then
				table.insert(valid,h:getEffectiveId())
				table.insert(valid,c:getEffectiveId())
				break
			end
		end
	end
	return #valid>1 and ("@XingzuoCard="..table.concat(valid,"+"))
end

sgs.ai_skill_playerchosen.xingzuo = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()<3
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and target:getHandcardNum()>0
		then return target end
	end
    return destlist[1]
end

--妙弦
addAiSkills("miaoxian").getTurnUseCard = function(self)
	local toids = {}
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	if #cards<1 then return end
	for _,h in sgs.list(cards)do
		if h:isBlack() then table.insert(toids,h:getEffectiveId()) end
	end
	for _,name in sgs.list(patterns())do
        if #toids~=1 then continue end
		local c = dummyCard(name)
		if c and c:isNDTrick()
		and c:isAvailable(self.player)
		and self:getCardsNum(c:getClassName())<1
		then
         	local dummy = self:aiUseCard(c)
    		if dummy.card and dummy.to
	     	then
				self.mx_to = dummy.to
				toids = #toids>0 and table.concat(toids,"+") or "."
	           	if c:canRecast() and dummy.to:length()<1 then continue end
				sgs.ai_use_priority.MiaoxianCard = sgs.ai_use_priority[c:getClassName()]
                return sgs.Card_Parse("@MiaoxianCard="..toids..":"..name)
			end
		end
	end
end

sgs.ai_skill_use_func["MiaoxianCard"] = function(card,use,self)
	use.card = card
	use.to = self.mx_to
end

sgs.ai_use_value.MiaoxianCard = 10.4
sgs.ai_use_priority.MiaoxianCard = 10.4

sgs.ai_guhuo_card.miaoxian = function(self,toname,class_name)
	local toids = {}
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	if #cards<1 then return end
	for _,h in sgs.list(cards)do
		if h:isBlack() then table.insert(toids,h:getEffectiveId()) end
	end
	if self.player:getMark("miaoxian-Clear")<1
	and #toids==1 and self:getCardsNum(class_name)<1
	then
		toids = #toids>0 and table.concat(toids,"+") or "."
        return "@MiaoxianCard="..toids..":"..toname
	end
end

--谋逆
sgs.ai_skill_playerchosen.mouni = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:getCardsNum("Slash")>=target:getHp()
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:getCardsNum("Slash")>0
		and self:isWeak(target)
		then return target end
	end
end


--纵反

sgs.ai_skill_playerchosen.fuzhong = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
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






--美人计
function SmartAI:useCardMeirenji(card,use)
	self:sort(self.enemies,"hp")
	local extraTarget = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,card)
	if use.extra_target then extraTarget = extraTarget+use.extra_target end
	for _,ep in sgs.list(self.enemies)do
		if isCurrent(use,ep) then continue end
		if CanToCard(card,self.player,ep)
		then
	    	use.card = card
			use.to:append(ep)
	    	if use.to:length()>extraTarget
			then return end
		end
	end
end
sgs.ai_use_priority.Meirenji = 6.5
sgs.ai_keep_value.Meirenji = 2
sgs.ai_use_value.Meirenji = 3.7

--笑里藏刀
function SmartAI:useCardXiaolicangdao(card,use)
	self:sort(self.enemies,"hp",true)
	local extraTarget = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,card)
	if use.extra_target then extraTarget = extraTarget+use.extra_target end
	for _,ep in sgs.list(self.enemies)do
		if isCurrent(use,ep) then continue end
		if CanToCard(card,self.player,ep)
		and self:isGoodTarget(ep,self.enemies,card)
		and ep:getHp()<2
		then
	    	use.card = card
			use.to:append(ep)
	    	if use.to:length()>extraTarget
			then return end
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if isCurrent(use,ep) then continue end
		if CanToCard(card,self.player,ep)
		and self:isGoodTarget(ep,self.enemies,card)
		then
	    	use.card = card
			use.to:append(ep)
	    	if use.to:length()>extraTarget
			then return end
		end
	end
	for _,ep in sgs.list(self.friends_noself)do
		if isCurrent(use,ep) then continue end
		if CanToCard(card,self.player,ep)
		and self:ajustDamage(self.player,ep,1,card)~=0
		and ep:getHp()>1 and ep:getLostHp()>1
		then
	    	use.card = card
			use.to:append(ep)
	    	if use.to:length()>extraTarget
			then return end
		end
	end
end
sgs.ai_use_priority.Xiaolicangdao = 5.5
sgs.ai_keep_value.Xiaolicangdao = 1
sgs.ai_use_value.Xiaolicangdao = 3.7

--连计

--矜功
addAiSkills("jingong").getTurnUseCard = function(self)
	local toids = {}
    local cards = self:addHandPile("he")
    self:sortByKeepValue(cards) -- 按保留值排序
	if #cards<1 then return end
	for _,h in sgs.list(cards)do
		if h:isKindOf("Slash")
		or h:isKindOf("EquipCard")
		then table.insert(toids,h:getEffectiveId()) end
	end
	local tricks = self.player:property("jingong_tricks"):toString():split("+")
	for _,name in sgs.list(tricks)do
        local c = dummyCard(name)
		if c and c:isAvailable(self.player)
		and self:getCardsNum(c:getClassName())<1
		and #toids>0
		then
         	c:addSubcard(toids[1])
			c:setSkillName("jingong")
			local dummy = self:aiUseCard(c)
    		if dummy.card
	    	and dummy.to
	     	then
	           	if c:canRecast()
				and dummy.to:length()<1
				then continue end
                return c
			end
		end
	end
end

addAiSkills("tenyearjingong").getTurnUseCard = function(self)
	local toids = {}
    local cards = self:addHandPile("he")
    self:sortByKeepValue(cards) -- 按保留值排序
	if #cards<1 then return end
	for _,h in sgs.list(cards)do
		if h:isKindOf("Slash")
		or h:isKindOf("EquipCard")
		then table.insert(toids,h:getEffectiveId()) end
	end
	local tricks = self.player:property("tenyearjingong_tricks"):toString():split("+")
	for _,name in sgs.list(tricks)do
        local c = dummyCard(name)
		if c and c:isAvailable(self.player)
		and self:getCardsNum(c:getClassName())<1
		and #toids>0
		then
         	c:addSubcard(toids[1])
			c:setSkillName("tenyearjingong")
			local dummy = self:aiUseCard(c)
    		if dummy.card
	    	and dummy.to
	     	then
	           	if c:canRecast()
				and dummy.to:length()<1
				then continue end
                return c
			end
		end
	end
end

--十周年连计
addAiSkills("tenyearlianji").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
  	if #cards<2 then return end
	return sgs.Card_Parse("@TenyearLianjiCard="..cards[1]:getEffectiveId())
end

sgs.ai_skill_use_func["TenyearLianjiCard"] = function(card,use,self)
	self:sort(self.enemies,"hp",true)
	for _,ep in sgs.list(self.enemies)do
		use.card = card
		use.to:append(ep)
		return
	end
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getHandcardNum()>2
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.TenyearLianjiCard = 9.4
sgs.ai_use_priority.TenyearLianjiCard = 3.8

--OL连计
addAiSkills("ollianji").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
  	if #cards<2 then return end
	return sgs.Card_Parse("@OLLianjiCard="..cards[1]:getEffectiveId())
end

sgs.ai_skill_use_func["OLLianjiCard"] = function(card,use,self)
	self:sort(self.enemies,"hp",true)
	for _,ep in sgs.list(self.enemies)do
		use.card = card
		use.to:append(ep)
		return
	end
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getHandcardNum()>2
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.OLLianjiCard = 9.4
sgs.ai_use_priority.OLLianjiCard = 3.8

sgs.ai_skill_playerchosen.ollianji = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
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

sgs.ai_skill_playerchosen.ollianji_give = function(self,players)
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

--手杀连计
addAiSkills("mobilelianji").getTurnUseCard = function(self)
	return sgs.Card_Parse("@MobileLianjiCard=.")
end

sgs.ai_skill_use_func["MobileLianjiCard"] = function(card,use,self)
	self:sort(self.enemies,"hp",true)
	for _,fp in sgs.list(self.friends_noself)do
		if fp:getHandcardNum()>0
		then
			for _,ep in sgs.list(self.enemies)do
				use.card = card
				use.to:append(fp)
				use.to:append(ep)
				return
			end
		end
	end
end

sgs.ai_use_value.MobileLianjiCard = 9.4
sgs.ai_use_priority.MobileLianjiCard = 3.8

--屯储
sgs.ai_skill_invoke.newtunchu = function(self,data)
    return self:getCardsNum("Slash")<1 or #self.friends_noself>0
end

sgs.ai_skill_discard.newtunchu = function(self)
	local cards = {}
    local handcards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByKeepValue(handcards) -- 按保留值排序
   	for _,h in sgs.list(handcards)do
		if #cards>2 or #cards>#handcards/2 then break end
		table.insert(cards,h:getEffectiveId())
	end
	return cards
end

--输粮
sgs.ai_skill_use["@@newshuliang"] = function(self,prompt)
    local ids = self.player:getPile("food")
   	local target = self.room:getCurrent()
	if ids:length()>0
	and self:isFriend(target)
	then
		return string.format("@NewShuliangCard=%s",ids:at(0))
	end
end

--天命
sgs.ai_skill_invoke.newtianming = function(self,data)
	return sgs.ai_skill_invoke.tianming(self,data)
end

sgs.ai_skill_discard.newtianming = function(self,discard_num,min_num,optional,include_equip)
	return sgs.ai_skill_discard.tianming(self,discard_num,min_num,optional,include_equip)
end

--观虚
addAiSkills("guanxu").getTurnUseCard = function(self)
	return sgs.Card_Parse("@GuanxuCard=.")
end

sgs.ai_skill_use_func["GuanxuCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard",true)
	self.guanxu_friends = false
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>2
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getHandcardNum()<4
		and ep:getHandcardNum()>0
		then
			use.card = card
			use.to:append(ep)
			self.guanxu_friends = true
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

sgs.ai_use_value.GuanxuCard = 9.4
sgs.ai_use_priority.GuanxuCard = 6.8

sgs.ai_skill_use["@@guanxu1"] = function(self,prompt)
	local valid = {}
	local guanxuhand = self.player:getTag("guanxuhandForAI"):toIntList()
	local guanxudrawpile = self.player:getTag("guanxudrawpileForAI"):toIntList()
	local n1,n2,suits = {},{},{}
	for _,id in sgs.list(guanxuhand)do
		local c = sgs.Sanguosha:getCard(id)
		table.insert(n1,c)
		if suits[c:getSuitString()]
		then suits[c:getSuitString()] = suits[c:getSuitString()]+1
		else suits[c:getSuitString()] = 1 end
	end
	for _,id in sgs.list(guanxudrawpile)do
		table.insert(n2,sgs.Sanguosha:getCard(id))
	end
	if self.guanxu_friends
	then
		self:sortByKeepValue(n1)
		self:sortByKeepValue(n2,true)
		for _,h in sgs.list(n1)do
			for _,c in sgs.list(n2)do
				if self:getKeepValue(c)>self:getKeepValue(h)
				then
					table.insert(valid,h:getEffectiveId())
					table.insert(valid,c:getEffectiveId())
					return ("@GuanxuChooseCard="..table.concat(valid,"+"))
				end
			end
		end
	end
	self:sortByKeepValue(n1,true)
	self:sortByKeepValue(n2)
	for _,h in sgs.list(n1)do
		for _,c in sgs.list(n2)do
			if self:getKeepValue(c)<self:getKeepValue(h)
			and suits[c:getSuitString()]==2
			then
				table.insert(valid,h:getEffectiveId())
				table.insert(valid,c:getEffectiveId())
				return ("@GuanxuChooseCard="..table.concat(valid,"+"))
			end
		end
	end
	for _,h in sgs.list(n1)do
		for _,c in sgs.list(n2)do
			if suits[c:getSuitString()]==2
			then
				table.insert(valid,h:getEffectiveId())
				table.insert(valid,c:getEffectiveId())
				return ("@GuanxuChooseCard="..table.concat(valid,"+"))
			end
		end
	end
	for _,h in sgs.list(n1)do
		for _,c in sgs.list(n2)do
			if suits[c:getSuitString()]==3
			and c:getSuitString()==h:getSuitString()
			and self:getKeepValue(c)<self:getKeepValue(h)
			then
				table.insert(valid,h:getEffectiveId())
				table.insert(valid,c:getEffectiveId())
				return ("@GuanxuChooseCard="..table.concat(valid,"+"))
			end
		end
	end
	for _,h in sgs.list(n1)do
		for _,c in sgs.list(n2)do
			if suits[c:getSuitString()]==3
			and c:getSuitString()==h:getSuitString()
			then
				table.insert(valid,h:getEffectiveId())
				table.insert(valid,c:getEffectiveId())
				return ("@GuanxuChooseCard="..table.concat(valid,"+"))
			end
		end
	end
	for _,h in sgs.list(n1)do
		for _,c in sgs.list(n2)do
			if self:getKeepValue(c)<self:getKeepValue(h)
			then
				table.insert(valid,h:getEffectiveId())
				table.insert(valid,c:getEffectiveId())
				return ("@GuanxuChooseCard="..table.concat(valid,"+"))
			end
		end
	end
	return #valid>1 and ("@GuanxuChooseCard="..table.concat(valid,"+"))
end

sgs.ai_skill_use["@@guanxu2"] = function(self,prompt)
	local guanxuhand = self.player:getTag("guanxuForAI"):toIntList()
	local n1,n2,suits = {},{},{}
	for c,id in sgs.list(guanxuhand)do
		table.insert(n1,sgs.Sanguosha:getCard(id))
	end
	self:sortByKeepValue(n1,true)
	for _,c in sgs.list(n1)do
		if suits[c:getSuitString()]
		then table.insert(suits[c:getSuitString()],c:getEffectiveId())
		else
			suits[c:getSuitString()] = {}
			table.insert(suits[c:getSuitString()],c:getEffectiveId())
		end
	end
	for _,ids in sgs.list(suits)do
		if #ids>=3
		then
			for i=1,3 do
				table.insert(n2,ids[i])
			end
			return ("@GuanxuDiscardCard="..table.concat(n2,"+"))
		end
	end
end

--雅士
sgs.ai_skill_invoke.yashi = function(self,data)
    return true
end

sgs.ai_skill_choice.yashi = function(self,choices,data)
	local damage = data:toDamage()
	local items = choices:split("+")
	if table.contains(items,"guanxu")
	then
		for _,ep in sgs.list(self.enemies)do
			if ep:getHandcardNum()>2
			then return "guanxu" end
		end
		for _,ep in sgs.list(self.friends_noself)do
			if ep:getHandcardNum()<4
			and ep:getHandcardNum()>0
			then return "guanxu" end
		end
	elseif damage.from
	and not self:isFriend(damage.from)
	then return items[1] end
end

sgs.ai_skill_playerchosen.yashi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
	self.guanxu_friends = false
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and target:getHandcardNum()>2
		then return target end
	end
	self:sort(destlist,"handcard")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()<4
		and target:getHandcardNum()>0
		then
			self.guanxu_friends = true
			return target
		end
	end
	return destlist[1]
end

addAiSkills("tenyearjiezhen").getTurnUseCard = function(self)
	return sgs.Card_Parse("@TenyearJiezhenCard=.")
end

sgs.ai_skill_use_func["TenyearJiezhenCard"] = function(card,use,self)
	self:sort(self.enemies)
	local function JiezhenSkill(p)
		local n = 0
		for _,s in sgs.list(p:getSkillList())do
			if s:isLimitedSkill()
			or s:isAttachedLordSkill()
			or s:isLordSkill()
			or s:getFrequency(p)==sgs.Skill_Compulsory
			or s:getFrequency(p)==sgs.Skill_Wake
			then continue end
			n = n+1
		end
		return n
	end
	for _,ep in sgs.list(self.enemies)do
		if JiezhenSkill(ep)>0
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.room:getOtherPlayers(self.player))do
		if JiezhenSkill(ep)>0
		and not self:isFriend(ep)
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.TenyearJiezhenCard = 2.4
sgs.ai_use_priority.TenyearJiezhenCard = 9.8

sgs.ai_skill_playerchosen.tenyearzecai = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()>3
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()>2
		and self:isWeak()
		then
			return target
		end
	end
end

sgs.ai_can_damagehp.tenyearyinshi = function(self,from,card,to)
	return not (card and (card:isRed() or card:isBlack()))
	and to:getMark("tenyearyinshi_damage-Clear")<1
	and self:canLoseHp(from,card,to)
end

sgs.ai_target_revises.tenyearyinshi = function(to,card,self,use)
	return card and (card:isRed() or card:isBlack())
	and to:getMark("tenyearyinshi_damage-Clear")<1
end

--挫锐
sgs.ai_skill_use["@@spcuorui"] = function(self,prompt)
	local targets = self:findPlayerToDiscard("h",false,false)
	if #targets<=0 then return "." end
	
	local tos = {}
	for i = 1,math.min(self.player:getHp(),#targets)do
		table.insert(tos,targets[i]:objectName())
	end
	return "@SpCuoruiCard=.->"..table.concat(tos,"+")
end

--裂围
sgs.ai_skill_invoke.spliewei = function(self,data)
	return self:canDraw()
end

--挫锐-二版
addAiSkills("secondspcuorui").getTurnUseCard = function(self)
	return sgs.Card_Parse("@SecondSpCuoruiCard=.")
end

sgs.ai_skill_use_func["SecondSpCuoruiCard"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	local n = 0
	for _,ep in sgs.list(self.enemies)do
		if self:isWeak(ep)
		and ep:getHandcardNum()>0
		then n = n+1 end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>0
		and n>=#self.enemies/2
		then
			use.card = card
			use.to:append(ep)
			if use.to:length()>=self.player:getHp()
			then return end
		end
	end
end

sgs.ai_use_value.SecondSpCuoruiCard = 9.4
sgs.ai_use_priority.SecondSpCuoruiCard = 6.8


--裂围-二版
sgs.ai_skill_invoke.secondspliewei = function(self,data)
	return self:canDraw()
end

--天算
function getSpecialMark(special_mark,player)
	player = player or current_self.player
	local num = 0
	for _,mark in ipairs(player:getMarkNames())do
		if mark:startsWith(special_mark) and player:getMark(mark)>0 then num = num+1 end
	end
	return num
end

addAiSkills("tiansuan").getTurnUseCard = function(self)
	return sgs.Card_Parse("@TiansuanCard=.:"..math.random(1,5))
end

sgs.ai_skill_use_func["TiansuanCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.TiansuanCard = 9.4
sgs.ai_use_priority.TiansuanCard = 6.8

sgs.ai_skill_playerchosen.tiansuan0 = function(self,players)--无法知道天算签
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	self:sort(destlist,"handcard")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and #self.enemies>0
		then return target end
	end
end

--掳掠
sgs.ai_skill_playerchosen.lulve = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	self:sort(destlist,"handcard")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and #self.enemies>0
		then return target end
	end
end

sgs.ai_skill_choice.lulve = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if self:isFriend(target) then return items[1]
	else return items[2] end
end

--望归
sgs.ai_skill_playerchosen.wanggui = function(self,targets)
	if targets:first():getKingdom()==self.player:getKingdom() then
		local target = self:findPlayerToDraw(false,1)
		if target then return target end
		if self:canDraw() then return self.player end
	else
		return self:findPlayerToDamage(1,self.player,nil,targets)[1]
	end
end

--息兵
sgs.ai_skill_invoke.xibing = function(self,data)
	local target = data:toPlayer()
	local hand_num = target:getHandcardNum()
	local num = target:getHp()-hand_num
	if num<=0 then return false end
	if self:isFriend(target) then
		if hand_num>2 then return false end
		return true
	elseif self:isEnemy(target) then
		if hand_num<=2 then return false end
		if hand_num>=5 then return true end
	end
	return false
end

--诱言
sgs.ai_skill_invoke.youyan = function(self,data)
	return self:canDraw()
end

--追还
sgs.ai_skill_playerchosen.zhuihuan = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets)
	for _,p in ipairs(targets)do
		if not self:isFriend(p) or p:getMark("&zhuihuan")>0 then continue end
		return p
	end
	for _,p in ipairs(targets)do
		if not self:isFriend(p) then continue end
		return p
	end
	return nil
end

--抗歌
sgs.ai_skill_playerchosen.kangge = function(self,players)
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

sgs.ai_skill_invoke.kangge = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isEnemy(target)
	end
end

--节烈
sgs.ai_skill_invoke.jielie = function(self,data)
    return true
end

--拒关
addAiSkills("juguan").getTurnUseCard = function(self)
	local cards = self:addHandPile()
	self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		local fs = dummyCard("duel")
		fs:setSkillName("juguan")
		fs:addSubcard(c)
		local d = self:aiUseCard(fs)
		self.jg_to = d.to
		sgs.ai_use_priority.JuguanCard = sgs.ai_use_priority.Duel
		if fs:isAvailable(self.player) and d.card and d.to
		then return sgs.Card_Parse("@JuguanCard="..c:getEffectiveId()..":duel") end
	end
  	for _,c in sgs.list(cards)do
		local fs = dummyCard("slash")
		fs:setSkillName("juguan")
		fs:addSubcard(c)
		local d = self:aiUseCard(fs)
		self.jg_to = d.to
		sgs.ai_use_priority.JuguanCard = sgs.ai_use_priority.Slash
		if fs:isAvailable(self.player) and d.card and d.to
		then return sgs.Card_Parse("@JuguanCard="..c:getEffectiveId()..":slash") end
	end
end

sgs.ai_skill_use_func["JuguanCard"] = function(card,use,self)
	use.card = card
	use.to = self.jg_to
end

sgs.ai_use_value.JuguanCard = 9.4
sgs.ai_use_priority.JuguanCard = 4.8

--驱徙
sgs.ai_skill_invoke.quxi = function(self,data)
    return self.player:getHandcardNum()>=self.player:getMaxCards()
	and sgs.ai_skill_use["@@quxi1"](self,"quxi1")
end

sgs.ai_skill_use["@@quxi1"] = function(self,prompt)
	local valid = {}
	local destlist = self.player:getAliveSiblings()
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist,"hp")
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		for _,ep in sgs.list(destlist)do
			if self:isFriend(fp) and self:isEnemy(ep)
			and fp:getHandcardNum()<ep:getHandcardNum()
			then
				table.insert(valid,fp:objectName())
				table.insert(valid,ep:objectName())
				break
			end
		end
	end
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		for _,ep in sgs.list(destlist)do
			if self:isFriend(fp) and not self:isFriend(ep)
			and fp:getHandcardNum()<ep:getHandcardNum()
			then
				table.insert(valid,fp:objectName())
				table.insert(valid,ep:objectName())
				break
			end
		end
	end
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		for _,ep in sgs.list(destlist)do
			if not self:isEnemy(fp) and self:isEnemy(ep)
			and fp:getHandcardNum()<ep:getHandcardNum()
			then
				table.insert(valid,fp:objectName())
				table.insert(valid,ep:objectName())
				break
			end
		end
	end
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		for _,ep in sgs.list(destlist)do
			if not self:isEnemy(fp) and not self:isFriend(ep)
			and fp:getHandcardNum()<ep:getHandcardNum()
			then
				table.insert(valid,fp:objectName())
				table.insert(valid,ep:objectName())
				break
			end
		end
	end
	if #valid>1
	then
    	return string.format("@QuxiCard=.->%s",table.concat(valid,"+"))
	end
end

sgs.ai_skill_use["@@quxi2"] = function(self,prompt)
	local valid = {}
	local destlist = self.player:getAliveSiblings()
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist,"hp")
	local death = self.player:property("QuxiDeathPlayer"):toString()
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		if fp:objectName()~=death then continue end
		for _,ep in sgs.list(destlist)do
			if ep:objectName()==death then continue end
			if fp:getMark("&quxiqian")>0
			and self:isEnemy(ep)
			then
				table.insert(valid,fp:objectName())
				table.insert(valid,ep:objectName())
				break
			end
		end
	end
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		if fp:objectName()~=death then continue end
		for _,ep in sgs.list(destlist)do
			if ep:objectName()==death then continue end
			if fp:getMark("&quxiqian")>0
			and not self:isFriend(ep)
			then
				table.insert(valid,fp:objectName())
				table.insert(valid,ep:objectName())
				break
			end
		end
	end
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		if fp:objectName()~=death then continue end
		for _,ep in sgs.list(destlist)do
			if ep:objectName()==death then continue end
			if fp:getMark("&quxifeng")>0
			and self:isFriend(ep)
			then
				table.insert(valid,fp:objectName())
				table.insert(valid,ep:objectName())
				break
			end
		end
	end
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		if fp:objectName()~=death then continue end
		for _,ep in sgs.list(destlist)do
			if ep:objectName()==death then continue end
			if fp:getMark("&quxifeng")>0
			and not self:isEnemy(ep)
			then
				table.insert(valid,fp:objectName())
				table.insert(valid,ep:objectName())
				break
			end
		end
	end
	if #valid>1
	then
    	return string.format("@QuxiCard=.->%s",table.concat(valid,"+"))
	end
end

sgs.ai_skill_use["@@quxi3"] = function(self,prompt)
	local valid = {}
	local destlist = self.player:getAliveSiblings()
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist,"hp")
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		for _,ep in sgs.list(destlist)do
			if fp:objectName()==ep:objectName() then continue end
			if fp:getMark("&quxiqian")>0
			and not self:isEnemy(fp)
			and self:isEnemy(ep)
			then
				table.insert(valid,fp:objectName())
				table.insert(valid,ep:objectName())
				break
			end
		end
	end
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		for _,ep in sgs.list(destlist)do
			if fp:objectName()==ep:objectName() then continue end
			if fp:getMark("&quxifeng")>0
			and not self:isFriend(fp)
			and self:isFriend(ep)
			then
				table.insert(valid,fp:objectName())
				table.insert(valid,ep:objectName())
				break
			end
		end
	end
	if #valid>1
	then
    	return string.format("@QuxiCard=.->%s",table.concat(valid,"+"))
	end
end

--齐攻
sgs.ai_skill_playerchosen.qigong = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
	if self:getCardsNum("Slash")>0
	then return self.player end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:objectName()~=self.player:objectName()
		and target:getHandcardNum()>0
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		and target:getHandcardNum()>0
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()>0
		then return target end
	end
end

--列侯
addAiSkills("liehou").getTurnUseCard = function(self)
	return sgs.Card_Parse("@LiehouCard=.")
end

sgs.ai_skill_use_func["LiehouCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,ep in sgs.list(self.enemies)do
		if self.player:inMyAttackRange(ep)
		and ep:getHandcardNum()>0
		then
			use.card = card
			use.to:append(ep)
			break
		end
	end
end

sgs.ai_use_value.LiehouCard = 9.4
sgs.ai_use_priority.LiehouCard = 4.8

sgs.ai_skill_askforyiji.liehou = function(self,card_ids)
	local target = self.player:getTag("LiehouTarget"):toPlayer()
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
	   	if self.player:inMyAttackRange(p)
		and p:objectName()~=target:objectName()
		and self:isFriend(p)
		then return p,card_ids[1] end
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
	   	if self.player:inMyAttackRange(p)
		and p:objectName()~=target:objectName()
		and not self:isEnemy(p)
		then return p,card_ids[1] end
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
	   	if self.player:inMyAttackRange(p)
		and p:objectName()~=target:objectName()
		then return p,card_ids[1] end
	end
end

sgs.ai_fill_skill.tenyearshuhe = function(self)
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	local ids = {}
	for _,p in sgs.list(self.room:getAlivePlayers())do
		for _,c in sgs.list(p:getCards("ej"))do
			if self:doDisCard(p,c:getId(),true)
			then table.insert(ids,c:getNumber()) end
		end
	end
	for _,c in sgs.list(cards)do
		if table.contains(ids,c:getNumber())
		then return sgs.Card_Parse("@TenyearShuheCard="..c:getId()) end
	end
	if (#cards>2 or self:getOverflow()>0)
	and #self.friends_noself>0
	then
		return sgs.Card_Parse("@TenyearShuheCard="..cards[1]:getId())
	end
end

sgs.ai_skill_use_func["TenyearShuheCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.TenyearShuheCard = 3.4
sgs.ai_use_priority.TenyearShuheCard = 6.2

sgs.ai_skill_playerchosen.tenyearshuhe = function(self,players)
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
end

sgs.ai_skill_discard.tenyearliehou = function(self,max,min,optional)
	local to_cards = self:poisonCards("e")
   	if #to_cards>=min then return to_cards end
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
   	for _,c in sgs.list(cards)do
   		if #to_cards>=min then break end
     	table.insert(to_cards,c:getEffectiveId())
	end
	if (min<2 or self:isWeak()) and #to_cards>=min
	then return to_cards end
	return {}
end

--狼灭
sgs.ai_skill_invoke.langmie = function(self,data)
    return self:canDraw()
end

sgs.ai_skill_cardask["@langmie-dis"] = function(self,data,pattern,prompt)
   	local target = self.room:getCurrent()
    if self:isEnemy(target) then return true end
	return not self:isFriend(target) and self.player:getCardCount()>4
end

sgs.ai_skill_choice.secondlangmie = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if table.contains(items,"draw")
	and not self:isEnemy(target)
	then return "draw" end
	if self:isEnemy(target)
	or not self:isFriend(target) and self.player:getCardCount()>4
	then return items[#items-1] end
	if table.contains(items,"draw")
	then return "draw" end
end

sgs.ai_skill_cardask["@secondlangmie"] = function(self,data,pattern,prompt)
	local target = data:toPlayer()
    if self:isEnemy(target) then return true end
	return not self:isFriend(target) and self.player:getCardCount()>4 or self:canDraw()
end

sgs.ai_skill_cardask["@secondlangmie-damage"] = function(self,data,pattern,prompt)
	local target = data:toPlayer()
    if self:isEnemy(target) then return true end
	return not self:isFriend(target) and self.player:getCardCount()>4
end

sgs.ai_skill_cardask["@secondlangmie-draw"] = function(self,data,pattern,prompt)
	return self:canDraw()
end

--祸水
sgs.ai_skill_use["@@tenyearhuoshui"] = function(self,prompt)
	local valid = {}
	local destlist = self.player:getAliveSiblings()
    destlist = self:sort(destlist,"hp")
	local n = math.max(self.player:getLostHp(),1)
	for _,fp in sgs.list(destlist)do
		if #valid>=n then break end
		if self:isEnemy(fp) then table.insert(valid,fp:objectName()) end
	end
	for _,fp in sgs.list(destlist)do
		if #valid>=n then break end
		if table.contains(valid,fp:objectName())
		or self:isFriend(fp)
		then continue end
		table.insert(valid,fp:objectName())
	end
	if #valid>0
	then
    	return string.format("@TenyearHuoshuiCard=.->%s",table.concat(valid,"+"))
	end
end

--倾城
addAiSkills("tenyearqingcheng").getTurnUseCard = function(self)
	return sgs.Card_Parse("@TenyearQingchengCard=.")
end

sgs.ai_skill_use_func["TenyearQingchengCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard",true)
	local n = self:getCardsNum("Peach")
	local cs = self:poisonCards()
	for _,ep in sgs.list(self.enemies)do
		if ep:isMale() and #cs>0
		and self.player:getHandcardNum()-ep:getHandcardNum()<=#cs
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:isMale() and n<1
		and ep:getHandcardNum()==self.player:getHandcardNum()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.friends_noself)do
		if ep:isMale()
		and ep:getHandcardNum()<=self.player:getHandcardNum()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.TenyearQingchengCard = 9.4
sgs.ai_use_priority.TenyearQingchengCard = 3.8

--祈禳
sgs.ai_skill_invoke.tenyearqirang = function(self,data)
    return true
end

sgs.ai_skill_playerchosen.tenyearqirang = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,id in sgs.list(self.player:getTag("tenyearqirang_tricks"):toIntList())do
		local c = sgs.Sanguosha:getCard(id)
		if self:isEnemy(target)
		and c:isDamageCard()
		then return target end
	end
end

--寇略
sgs.ai_skill_invoke.koulve = function(self,data)
	local target = data:toPlayer()
	return not self:isFriend(target)
	and target:getHandcardNum()>2
end

sgs.ai_skill_invoke.secondkoulve = function(self,data)
	local target = data:toPlayer()
	return not self:isFriend(target)
	and target:getHandcardNum()>2
end

--随认
sgs.ai_skill_playerchosen.suirenq = function(self,players)
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
end

sgs.ai_skill_invoke.fengshimf = function(self,data)
	local target = data:toPlayer()
	return self:isEnemy(target)
end

--摧坚
addAiSkills("cuijian").getTurnUseCard = function(self)
	return sgs.Card_Parse("@CuijianCard=.")
end

sgs.ai_skill_use_func["CuijianCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard",true)
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>0
		then
			use.card = card
			use.to:append(ep)
			break
		end
	end
end

sgs.ai_use_value.CuijianCard = 9.4
sgs.ai_use_priority.CuijianCard = 4.8

addAiSkills("secondcuijian").getTurnUseCard = function(self)
	return sgs.Card_Parse("@SecondCuijianCard=.")
end

sgs.ai_skill_use_func["SecondCuijianCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard",true)
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>0
		then
			use.card = card
			use.to:append(ep)
			break
		end
	end
end

sgs.ai_use_value.SecondCuijianCard = 9.4
sgs.ai_use_priority.SecondCuijianCard = 4.8

--同援
sgs.ai_skill_playerchosen.secondtongyuan0 = function(self,players)--无法知道牌
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
end


sgs.ai_skill_cardask["@chaofeng-discard"] = function(self,data,pattern,prompt)
    local damage = data:toDamage()
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	for _,c in sgs.list(cards)do
		if damage.card
		and damage.card:getSuit()==c:getSuit()
		then
			if self:isEnemy(damage.to)
			and damage.card:getNumber()==c:getNumber()
			then return c:getEffectiveId() end
		end
	end
	for _,c in sgs.list(cards)do
		if damage.card
		then
			if self:isEnemy(damage.to)
			and damage.card:getNumber()==c:getNumber()
			then return c:getEffectiveId() end
		end
	end
	for _,c in sgs.list(cards)do
		if damage.card
		and damage.card:getSuit()==c:getSuit()
		then return c:getEffectiveId() end
	end
	return #cards>1 and cards[1]:getEffectiveId() or "."
end

sgs.ai_skill_cardask["@secondchaofeng-discard"] = function(self,data,pattern,prompt)
    local damage = data:toDamage()
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	for _,c in sgs.list(cards)do
		if damage.card
		and damage.card:getColor()==c:getColor()
		then
			if self:isEnemy(damage.to)
			and damage.card:getTypeId()==c:getTypeId()
			then return c:getEffectiveId() end
		end
	end
	for _,c in sgs.list(cards)do
		if damage.card
		then
			if self:isEnemy(damage.to)
			and damage.card:getTypeId()==c:getTypeId()
			then return c:getEffectiveId() end
		end
	end
	for _,c in sgs.list(cards)do
		if damage.card
		and damage.card:getColor()==c:getColor()
		then return c:getEffectiveId() end
	end
	return #cards>1 and cards[1]:getEffectiveId() or "."
end

sgs.ai_skill_playerchosen.chuanshu = function(self,players,reason)
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
end

sgs.ai_skill_playerchosen.secondchuanshu = function(self,players,reason)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp",true)
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		and #self.enemies>0
		then return target end
	end
end

sgs.ai_skill_invoke.chuanyun = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isFriend(target)
	end
end

sgs.ai_skill_invoke.xunde = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isEnemy(target)
	end
end

sgs.ai_skill_cardask["@chenjie-card"] = function(self,data)
	local judge = data:toJudge()
	local all_cards = self:addHandPile("he")
	if #all_cards<1 then return "." end
	local cards = {}
	for _,c in sgs.list(all_cards)do
		if c:getSuit()==judge.card:getSuit()
		then table.insert(cards,c) end
	end
	if #cards<1 then return "." end
	if self:needRetrial(judge)
	then
    	local id = self:getRetrialCardId(cards,judge)
    	if id~=-1 then return id end
	else
    	local id = self:getRetrialCardId(cards,judge)
    	if id~=-1 then return id end
	end
    return "."
end

sgs.ai_skill_invoke.jibing = function(self,data)
	if self:getCardsNum("Slash")<3
	and #self.enemies>0 then
		return true
	end
	return self:getCardsNum("Jink")<3
end

function sgs.ai_cardsview.jibing(self,class_name,player)
   	local ids = self.player:getPile("jbbing")
	if class_name=="Jink"
	then return ("jink:jibing[no_suit:0]="..ids:at(0))
	elseif class_name=="Slash"
    then return ("slash:jibing[no_suit:0]="..ids:at(0)) end
end

addAiSkills("jibing").getTurnUseCard = function(self)
  	for _,c in sgs.list(self.player:getPile("jbbing"))do
	   	local fs = dummyCard("slash")
		fs:setSkillName("jibing")
		fs:addSubcard(c)
		if fs:isAvailable(self.player)
	   	then return fs end
	end
end

sgs.ai_skill_playerchosen.binghuo = function(self,players,reason)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_cardask["@huantu-invoke"] = function(self,data,pattern,prompt)
   	local target = self.room:getCurrent()
	local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
    if self:isFriend(target)
	and target:getHandcardNum()<3 or target:getHandcardNum()>4
	then return cards[math.random(1,#cards)]:getEffectiveId() end
    if not self:isEnemy(target)
	then return cards[1]:getEffectiveId() end
	return "."
end

sgs.ai_skill_invoke.huantu = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isEnemy(target)
	end
end

sgs.ai_skill_choice.huantu = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if self:isFriend(target)
	and self:isWeak(target)
	then return items[1] end
	return items[2]
end

sgs.ai_skill_invoke.bihuo = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return self:isFriend(target)
	end
end

sgs.ai_skill_invoke.yachai = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isFriend(target) or target:getHandcardNum()<3
	end
end

sgs.ai_skill_choice.yachai = function(self,choices,data)
	local damage = data:toDamage()
	local items = choices:split("+")
	if self:isFriend(damage.to)
	then return items[2] end
end

sgs.ai_skill_choice.yachai_suit = function(self,choices)
	local items = choices:split("+")
	local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	local suits = {}
	for _,c in sgs.list(cards)do
		if suits[c:getSuitString()]
		then suits[c:getSuitString()]=suits[c:getSuitString()]+1
		else suits[c:getSuitString()]=1 end
	end
    local compare_func = function(a,b)
        return a<b
    end
    table.sort(suits,compare_func)
	for _,s in sgs.list(items)do
		if suits[s]==suits[1]
		then return s end
	end
end

addAiSkills("qingtan").getTurnUseCard = function(self)
	return sgs.Card_Parse("@QingtanCard=.")
end

sgs.ai_skill_use_func["QingtanCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard",true)
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>0
		then
			use.card = card
		end
	end
end

sgs.ai_use_value.QingtanCard = 9.4
sgs.ai_use_priority.QingtanCard = 5.8

sgs.ai_skill_choice.qingtan = function(self,choices)
	local items = choices:split("+")
	table.removeOne(items,items[#items])
	return items[math.random(1,#items)]
end

sgs.ai_skill_invoke.zhukou = function(self,data)
    return true
end

sgs.ai_skill_use["@@zhukou"] = function(self,prompt)
	local valid = {}
	local destlist = self.player:getAliveSiblings()
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist,"hp")
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		if self:isEnemy(fp) then table.insert(valid,fp:objectName()) end
	end
	for _,fp in sgs.list(destlist)do
		if #valid>1 then break end
		if table.contains(valid,fp:objectName())
		or self:isFriend(fp)
		then continue end
		table.insert(valid,fp:objectName())
	end
	if #valid>1
	then
    	return string.format("@ZhukouCard=.->%s",table.concat(valid,"+"))
	end
end

sgs.ai_skill_playerchosen.zhukou = function(self,players,reason)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_choice.yuyun = function(self,choices)
	local items = choices:split("+")
	if table.contains(items,"maxhp")
	and self.player:getLostHp()>1
	then return "maxhp" end
	if table.contains(items,"hp")
	then return "hp" end
	if table.contains(items,"damage")
	then
		for _,fp in sgs.list(self.enemies)do
			if self:isWeak(fp) then return "damage" end
		end
	end
	if table.contains(items,"drawmaxhp")
	then
		for _,fp in sgs.list(self.friends)do
			if fp:getMaxHp()-fp:getHandcardNum()>1
			and fp:getHandcardNum()<5
			then return "drawmaxhp" end
		end
	end
	if table.contains(items,"obtain")
	then
		for _,fp in sgs.list(self.friends_noself)do
			if self:doDisCard(fp,"ej")
			then return "obtain" end
		end
	end
	if table.contains(items,"draw")
	and self.player:getHandcardNum()<=self.player:getMaxCards()
	then return "draw" end
	if table.contains(items,"maxcard")
	and self.player:getHandcardNum()>self.player:getMaxCards()
	then return "maxcard" end
end

sgs.ai_skill_playerchosen.yuyun = function(self,players,reason)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_playerchosen.yuyun_obtain = function(self,players,reason)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and self:doDisCard(target,"ej")
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:doDisCard(target)
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and self:doDisCard(target)
		then return target end
	end
end

sgs.ai_skill_playerchosen.yuyun_drawmaxhp = function(self,players,reason)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
	for _,fp in sgs.list(destlist)do
		if fp:getMaxHp()-fp:getHandcardNum()>1
		and fp:getHandcardNum()<5
		and self:isFriend(fp)
		then return fp end
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

sgs.ai_skill_playerchosen.zhenge = function(self,players,reason)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp",true)
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getMark("&zhenge")<1
		then return target end
	end
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		and target:getMark("&zhenge")<1
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_invoke.zhenge = function(self,data)
	local items = data:toString():split(":")
    target = self.room:findPlayerByObjectName(items[2])
	if target
	then
		return self:isFriend(target)
	end
end

sgs.ai_skill_use["@@zhenge!"] = function(self,prompt)
    local c = dummyCard()
	c:setSkillName("_zhenge")
    local dummy = self:aiUseCard(c)
   	local tos = {}
   	if dummy.card
   	and dummy.to
   	then
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
       	return c:toString().."->"..table.concat(tos,"+")
    end
	dummy.to = sgs.SPlayerList()
    for _,to in sgs.list(self.room:getAllPlayers())do
		if CanToCard(c,self.player,to,dummy.to) and self:isEnemy(to)
		then dummy.to:append(to) table.insert(tos,p:objectName()) end
	end
    for _,to in sgs.list(self.room:getAllPlayers())do
		if dummy.to:contains(to) then continue end
		if CanToCard(c,self.player,to,dummy.to) and not self:isFriend(to)
		then dummy.to:append(to) table.insert(tos,p:objectName()) end
	end
   	return #tos>0 and c:toString().."->"..table.concat(tos,"+")
end

sgs.ai_skill_cardask["@tianze-discard"] = function(self,data,pattern,prompt)
	local use = data:toCardUse()
    if self:isEnemy(use.from)
	then return true end
	return "."
end

sgs.ai_skill_cardask["@difa"] = function(self,data,pattern,prompt)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if table.contains(pattern:split(","),h:toString())
		then
			local d = self:aiUseCard(h)
			if d.card and d.to then continue end
			return h:getEffectiveId()
		end
	end
end

sgs.ai_skill_invoke.zhuangshu = function(self,data)
    return true
end

sgs.ai_skill_cardask["@zhuangshu-discard"] = function(self,data,pattern,prompt)
	local target = data:toPlayer()
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
    if self:isFriend(target)
	and target:getTreasure()==nil and #cards>2
	then return cards[1]:getEffectiveId() end
	return "."
end

sgs.ai_skill_use["@@chuiti"] = function(self,prompt)
	local ids = self.player:getTag("chuitiForAI"):toIntList()
	for _,id in sgs.list(ids)do
		local c = sgs.Sanguosha:getCard(id)
		local d = self:aiUseCard(c)
		if d.card and d.to
		then
			local tos = {}
			for _,p in sgs.list(d.to)do
				table.insert(tos,p:objectName())
			end
			return c:toString().."->"..table.concat(tos,"+")
		end
	end
end

sgs.ai_skill_invoke.wanwei = function(self,data)
    return true
end

sgs.ai_skill_invoke.yuejian = function(self,data)
	local use = data:toCardUse()
	for i,c in sgs.list(self.player:getCards("h"))do
		if c:getSuit()==use.card:getSuit()
		then return end
	end
	return true
end

addAiSkills("zhuning").getTurnUseCard = function(self)
	self.isfriend = nil
	local cards = sgs.QList2Table(self.player:getCards("he"))
	if #cards<2 then return end
	self:sortByKeepValue(cards)
	local toids = {}
  	for _,c in sgs.list(cards)do
		if #self.friends_noself<1
		or #toids>=#cards/2
		then continue end
		table.insert(toids,c:getEffectiveId())
		self.isfriend = true
	end
	if #toids<1 then table.insert(toids,cards[1]:getEffectiveId()) end
	if #toids>0 then return sgs.Card_Parse("@ZhuningCard="..table.concat(toids,"+")) end
end

sgs.ai_skill_use_func["ZhuningCard"] = function(card,use,self)
	if self.isfriend
	then
		self:sort(self.friends_noself,"hp")
		for _,ep in sgs.list(self.friends_noself)do
			use.card = card
			use.to:append(ep)
			return
		end
	end
	self:sort(self.enemies,"hp",true)
	for _,ep in sgs.list(self.enemies)do
		use.card = card
		use.to:append(ep)
		return
	end
end

sgs.ai_use_value.ZhuningCard = 9.4
sgs.ai_use_priority.ZhuningCard = 2.8

sgs.ai_skill_askforag.zhuning = function(self,card_ids)
	for _,id in sgs.list(card_ids)do
		local c = sgs.Sanguosha:getCard(id)
		c = dummyCard(c:objectName())
		c:setSkillName("_zhuning")
		local d = self:aiUseCard(c)
		self.zhuning_d = d
		if d.card and d.to
		then return id end
	end
end

sgs.ai_skill_use["@@zhuning"] = function(self,prompt)
	if self.zhuning_d then
		local tos = {}
		for _,p in sgs.list(self.zhuning_d.to)do
			table.insert(tos,p:objectName())
		end
		return self.zhuning_d.card:toString().."->"..table.concat(tos,"+")
	end
end






addAiSkills("pingxiang").getTurnUseCard = function(self)
	if self.player:getMaxHp()>9
	then
		return sgs.Card_Parse("@PingxiangCard=.")
	end
end

sgs.ai_skill_use_func["PingxiangCard"] = function(card,use,self)
	local fs = dummyCard("fire_slash")
	fs:setSkillName("_pingxiang")
	fs = self:aiUseCard(fs)
	if fs.card and fs.to
	then
		use.card = card
		sgs.ai_use_priority.PingxiangCard = sgs.ai_use_priority.Slash-0.3
	end
end

sgs.ai_use_value.PingxiangCard = 9.4
sgs.ai_use_priority.PingxiangCard = 2.8

sgs.ai_skill_use["@@pingxiang"] = function(self,prompt)
	local fs = dummyCard("fire_slash")
	fs:setSkillName("_pingxiang")
    local dummy = self:aiUseCard(fs)
    local tos = {}
   	if dummy.card
   	and dummy.to
   	then
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
       	return fs:toString().."->"..table.concat(tos,"+")
    end
	dummy = sgs.SPlayerList()
	self:sort(self.enemies,"hp")
	for _,p in sgs.list(self.enemies)do
		if CanToCard(fs,self.player,p,dummy)
		and self:slashIsEffective(fs,p)
		then
			table.insert(tos,p:objectName())
			dummy:append(p)
		end
	end
	local OP = self.room:getOtherPlayers(self.player)
	OP = self:sort(OP,"hp")
	for _,p in sgs.list(OP)do
		if CanToCard(fs,self.player,p,dummy)
		and self:slashIsEffective(fs,p)
		and not self:isFriend(p)
		then
			table.insert(tos,p:objectName())
			dummy:append(p)
		end
	end
   	if #tos>0
   	then
       	return fs:toString().."->"..table.concat(tos,"+")
    end
end

addAiSkills("shouli").getTurnUseCard = function(self)
	local cards = {}
  	for i,p in sgs.list(self.room:getAlivePlayers())do
		for d,c in sgs.list(p:getCards("ej"))do
			if c:isKindOf("OffensiveHorse")
			then table.insert(cards,c) end
		end
	end
	self:sortByKeepValue(cards,nil,"l")
  	for _,c in sgs.list(cards)do
		local dc = dummyCard()
		dc:setSkillName("shouli")
		dc:addSubcard(c)
		local d = self:aiUseCard(dc)
		if d.card and d.to
		and dc:isAvailable(self.player)
		then
			self.shouli_to = d.to
			sgs.ai_use_priority.ShouliCard = sgs.ai_use_priority.Slash+0.6
			return sgs.Card_Parse("@ShouliCard="..c:getEffectiveId()..":slash")
		end
	end
end

sgs.ai_skill_use_func["ShouliCard"] = function(card,use,self)
	if self.shouli_to
	then
		use.card = card
		use.to = self.shouli_to
	end
end

sgs.ai_use_value.ShouliCard = 5.4
sgs.ai_use_priority.ShouliCard = 2.8

sgs.ai_guhuo_card.shouli = function(self,toname,class_name)
	local cards = {}
  	for _,p in sgs.list(self.room:getAlivePlayers())do
		for _,c in sgs.list(p:getCards("ej"))do
			if c:isKindOf("OffensiveHorse") and class_name=="Slash"
			or c:isKindOf("DefensiveHorse") and class_name=="Jink"
			then table.insert(cards,c) end
		end
	end
	self:sortByKeepValue(cards,nil,"l")
  	for _,c in sgs.list(cards)do
		if self:getCardsNum(class_name)>0 then break end
		return "@ShouliCard="..c:getEffectiveId()..":"..toname
	end
end

addAiSkills("shencai").getTurnUseCard = function(self)
	return sgs.Card_Parse("@ShencaiCard=.")
end

sgs.ai_skill_use_func["ShencaiCard"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	for _,ep in sgs.list(self.enemies)do
		if ep:getHp()>=self.player:getHp()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	self:sort(self.enemies,"card",true)
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>=self.player:getHandcardNum()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		use.card = card
		use.to:append(ep)
		return
	end
end

sgs.ai_use_value.ShencaiCard = 9.4
sgs.ai_use_priority.ShencaiCard = 4.8

sgs.ai_skill_playerschosen["#xunshi"] = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card")
	local tos = {}
	local use = {to = sgs.SPlayerList(),card = dummyCard()}
	use.card:setSkillName("xunshi")
	for _,p in sgs.list(self.room:getAllPlayers())do
		if players:contains(p) then continue end
		use.to:append(p)
	end
    for _,target in sgs.list(destlist)do
		if #tos>=x then break end
		if self:isEnemy(target) and self:canCanmou(target,use)
		then table.insert(tos,target) end
	end
    for _,target in sgs.list(destlist)do
		if #tos>=2 then break end
		if self:canCanmou(target,use)
		and not self:isFriend(target)
		and not table.contains(tos,target)
		then table.insert(tos,target) end
	end
	return tos
end

sgs.ai_skill_cardask["@tuoyu1"] = function(self,data,pattern)
	local cards = self.player:getHandcards()
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards) -- 按保留值排序
	local d = dummyCard()
   	for _,c in sgs.list(cards)do
    	if c:hasTip("sdatyfengtian")
		or c:hasTip("sdatyqingqu")
		or c:hasTip("sdatyjunshan")
		then continue end
		if c:isKindOf("Peach")
		or c:isKindOf("SingleTargetTrick")
		then d:addSubcard(c) end
	end
    return d:subcardsLength()>0 and d:toString() or "."
end

sgs.ai_skill_cardask["@tuoyu2"] = function(self,data,pattern)
	local cards = self.player:getHandcards()
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards) -- 按保留值排序
	local d = dummyCard()
   	for _,c in sgs.list(cards)do
    	if c:hasTip("sdatyfengtian")
		or c:hasTip("sdatyqingqu")
		or c:hasTip("sdatyjunshan")
		then continue end
		local late = sgs.Sanguosha:translate(":"..c:objectName())
		if string.find(late,"距离")
		or string.find(late,"攻击范围")
		then d:addSubcard(c) end
	end
    return d:subcardsLength()>0 and d:toString() or "."
end

sgs.ai_skill_cardask["@tuoyu3"] = function(self,data,pattern)
	local cards = self.player:getHandcards()
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards) -- 按保留值排序
	local d = dummyCard()
   	for _,c in sgs.list(cards)do
    	if c:hasTip("sdatyfengtian")
		or c:hasTip("sdatyqingqu")
		or c:hasTip("sdatyjunshan")
		then continue end
		if c:isKindOf("AOE") and not self:isWeak(self.friends)
		or c:isDamageCard() and not c:targetFixed()
		then d:addSubcard(c) end
	end
    return d:subcardsLength()>0 and d:toString() or "."
end

sgs.ai_skill_choice.xianjin = function(self,choices)
	local items = choices:split("+")
	if table.contains(items,"sdatyfengtian")
	and sgs.ai_skill_cardask["@tuoyu1"](self)~="."
	then return "sdatyfengtian" end
	if table.contains(items,"sdatyqingqu")
	and sgs.ai_skill_cardask["@tuoyu2"](self)~="."
	then return "sdatyqingqu" end
	if table.contains(items,"sdatyjunshan")
	and sgs.ai_skill_cardask["@tuoyu3"](self)~="."
	then return "sdatyjunshan" end
end

sgs.ai_skill_playerchosen.qijing = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target) and self:isEnemy(target:getNextAlive())
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target) and not self:isFriend(target:getNextAlive())
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target) and self:isEnemy(target:getNextAlive())
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target) and not self:isFriend(target:getNextAlive())
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target) or not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_invoke.cuixin = function(self,data)
	local items = data:toString():split(":")
    local d = dummyCard(items[2])
	if d then
		d:setSkillName("cuixin")
		local to = self.player:getNextAlive()
		if items:match("shangjia") then to = self.player:getNextAlive(self.player:aliveCount()-1) end
		return self:canCanmou(target,{from=self.player,card=d,to={}})
	end
end

sgs.ai_skill_playerchosen.tenyearyunjiu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and self:canDraw(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
	return destlist[1]
end

sgs.ai_skill_invoke.tenyearmoshou = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_playerchosen.dangzhai = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and self:doDisCard(target,"j",true)
		then return target end
	end
end

sgs.ai_skill_playerchosen.shuangrui = function(self,players)
	local dc = dummyCard()
	dc:setSkillName("shensu")
	local d = self:aiUseCard(dc)
	if d.card then return d.to:first() end
end

addAiSkills("fuxie").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards,nil,"j")
  	for i,c in sgs.list(cards)do
		if i<#cards/2 and c:isKindOf("Weapon") then
			sgs.ai_use_priority.FuxieCard = 5.8
			return sgs.Card_Parse("@FuxieCard="..c:toString())
		end
	end
	local n = 0
	sgs.ai_use_priority.FuxieCard = 0.8
	for i,s in sgs.list(self.player:getVisibleSkillList())do
		if s:isAttachedLordSkill() then continue end
		n = n+1
	end
	if n>2 then
		return sgs.Card_Parse("@FuxieCard=.")
	end
end

sgs.ai_skill_use_func["FuxieCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,ep in sgs.list(self.enemies)do
		if self:doDisCard(ep,"he",true,2) then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.FuxieCard = 9.4
sgs.ai_use_priority.FuxieCard = 0.8

sgs.ai_skill_choice.fuxie = function(self,choices)
	local items = choices:split("+")
	return items[#items]
end

addAiSkills("shouxing").getTurnUseCard = function(self)
	local nps = {}
	for i,p in sgs.list(self.player:getAliveSiblings())do
		if self.player:inMyAttackRange(p)
		then table.insert(nps,p:objectName()) end
	end
	local dc = dummyCard()
	dc:setSkillName("shensu")
	local d = self:aiUseCard(dc,dummy(nil,0,nps))
	if d.card then
		self.shouxingTo = d.to:first()
		local cards = sgs.QList2Table(self.player:getCards("he"))
		self:sortByKeepValue(cards)
		nps = {}
		dc = self.player:distanceTo(self.shouxingTo)
		for i,c in sgs.list(cards)do
			table.insert(nps,c:toString())
			if #nps>=dc then
				return sgs.Card_Parse("@ShouxingCard="..table.concat(nps,"+"))
			end
		end
	end
end

sgs.ai_skill_use_func["ShouxingCard"] = function(card,use,self)
	if self.shouxingTo then
		use.to:append(self.shouxingTo)
		use.card = card
	end
end

sgs.ai_use_value.ShouxingCard = 9.4
sgs.ai_use_priority.ShouxingCard = 1.8

sgs.ai_skill_invoke.shaxue = function(self,data)
	local damage = data:toDamage()
	return self:canDraw() and self.player:distanceTo(damage.to)<(self.player:getCardCount()+2)/2
end

addAiSkills("pingzhi").getTurnUseCard = function(self)
	return sgs.Card_Parse("@PingzhiCard=.")
end

sgs.ai_skill_use_func["PingzhiCard"] = function(card,use,self)
	if self.player:getChangeSkillState("pingzhi")==1 then
		self:sort(self.enemies)
		for _,ep in sgs.list(self.enemies)do
			if ep:getHandcardNum()>0 then
				use.to:append(ep)
				use.card = card
				return
			end
		end
	else
		self:sort(self.friends,nil,true)
		for _,ep in sgs.list(self.friends)do
			if ep:getHandcardNum()>3 then
				use.to:append(ep)
				use.card = card
				return
			end
		end
		self:sort(self.enemies)
		for _,ep in sgs.list(self.enemies)do
			if ep:getHandcardNum()>0 then
				use.to:append(ep)
				use.card = card
				return
			end
		end
	end
end

sgs.ai_use_value.PingzhiCard = 9.4
sgs.ai_use_priority.PingzhiCard = 4.8

sgs.ai_skill_askforag.pingzhi = function(self,card_ids)
	if self.player:getChangeSkillState("pingzhi")==2 then
		local cp = self.room:getCardOwner(card_ids[1])
		local hs = {}
		for _,id in sgs.list(card_ids)do
			table.insert(hs,sgs.Sanguosha:getCard(id))
		end
		if self:isFriend(cp) then
			self:sortByUseValue(hs)
			for _,h in sgs.list(hs)do
				if h:isAvailable(cp) then
					return h:getId()
				end
			end
		end
		self:sortByKeepValue(hs,nil,true)
		for _,h in sgs.list(hs)do
			if h:isAvailable(cp) then
				return h:getId()
			end
		end
	end
end

sgs.ai_skill_use["@@yinlu!"] = function(self,prompt)
	local valid = {}
	local destlist = self.room:getAlivePlayers()
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist)
	for _,fp in sgs.list(destlist)do
		if self:isFriend(fp) then table.insert(valid,fp:objectName()) end
		if #valid>1 then break end
	end
	for _,fp in sgs.list(destlist)do
		if self:isEnemy(fp) or table.contains(valid,fp:objectName()) then continue end
		if #valid>1 then break end
		table.insert(valid,fp:objectName())
	end
	for _,fp in sgs.list(destlist)do
		if #valid==3 then break end
		if self:isEnemy(fp) then table.insert(valid,fp:objectName()) end
	end
	for _,fp in sgs.list(destlist)do
		if self:isFriend(fp) or table.contains(valid,fp:objectName()) then continue end
		if #valid==3 then break end
		table.insert(valid,fp:objectName())
	end
	if #valid>1 then
    	return string.format("@YinluCard=.->%s",table.concat(valid,"+"))
	end
end

sgs.ai_skill_use["@@yinlu1"] = function(self,prompt)
	self:sort(self.enemies)
	self:sort(self.friends)
	for _,p in sgs.list(self.enemies)do
		if p:getMark("&yl_lequan")>0 or p:getMark("&yl_huoxi")>0 then
			local valid = {p:objectName()}
			for _,t in sgs.list(self.friends)do
				table.insert(valid,t:objectName())
				return string.format("@YinluCard=.->%s",table.concat(valid,"+"))
			end
		end
	end
	for _,p in sgs.list(self.friends)do
		if p:getMark("&yl_zhangqi")>0 then
			local valid = {p:objectName()}
			for _,t in sgs.list(self.enemies)do
				table.insert(valid,t:objectName())
				return string.format("@YinluCard=.->%s",table.concat(valid,"+"))
			end
		end
	end
end

sgs.ai_skill_cardask["yl_lequan0"] = function(self,data,pattern,prompt)
    if self:isWeak() or self:getOverflow()>=0
	then return true end
	return "."
end

sgs.ai_skill_cardask["yl_huoxi0"] = function(self,data,pattern,prompt)
    if self:canDraw() and self:getOverflow()>=0
	then return true end
	return "."
end

sgs.ai_skill_cardask["yl_zhangqi0"] = function(self,data,pattern,prompt)
    if self:isWeak() or self:getOverflow()>=0
	then return true end
	return "."
end

sgs.ai_skill_cardask["yl_yunxiang0"] = function(self,data,pattern,prompt)
    if self:isWeak() or self:getOverflow()>=0
	then return true end
	return "."
end

sgs.ai_skill_invoke.yl_yunxiang = function(self,data)
	return self:isWeak()
end

sgs.ai_skill_use["@@thshuliang"] = function(self,prompt)
	local ids = {}
	local tps,valid = {},{}
	for _,p in sgs.list(self.room:getAlivePlayers())do
		if p:isKongcheng() then table.insert(tps,p) end
	end
	local n = #tps
	for i=0,n do
		local c,to = self:getCardNeedPlayer(nil,true,tps)
		if c and to then
			table.removeOne(tps,to)
			table.insert(valid,to:objectName())
			table.insert(ids,c:toString())
		end
	end
	if #valid>0 then
    	return "@ThShuliangCard="..table.concat(ids,"+").."->"..table.concat(valid,"+")
	end
end

sgs.ai_skill_invoke.tanban = function(self,data)
	local n = 0
	for _,h in sgs.list(self.player:getHandcards())do
		if h:hasTip("tanban") then n = n+1 end
	end
	return self.player:getHandcardNum()/2>n
end

sgs.ai_skill_cardask["diou0"] = function(self,data,pattern,prompt)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByUseValue(cards)
	for _,h in sgs.list(self.player:getHandcards())do
		if h:hasTip("tanban") then continue end
		if h:isKindOf("BasicCard") or h:isNDTrick() then
			local dc = dummyCard(h:objectName())
			dc:setSkillName("_diou")
			local d = self:aiUseCard(dc)
			if d.card then
				self.diouUse = d
				return h:toString()
			end
		end
	end
	return "."
end

sgs.ai_skill_use["@@diou"] = function(self,prompt)
	local tps = {}
	for _,p in sgs.list(self.diouUse.to)do
		table.insert(tps,p:objectName())
	end
   	return self.diouUse.card:toString().."->"..table.concat(tps,"+")
end

addAiSkills("pinglu").getTurnUseCard = function(self)
	return sgs.Card_Parse("@PingluCard=.")
end

sgs.ai_skill_use_func["PingluCard"] = function(card,use,self)
	for _,p in sgs.list(self.player:getAliveSiblings())do
		if p:getHandcardNum()>0 and self.player:inMyAttackRange(p)
		then use.card = card break end
	end
end

sgs.ai_use_value.PingluCard = 9.4
sgs.ai_use_priority.PingluCard = 4.8

sgs.ai_skill_invoke.feibai = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_invoke.zhengyue = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_use["@@zhengyue"] = function(self,prompt)
	local cs = {}
	for _,id in sgs.list(self.player:getTag("zhengyueForAI"):toIntList())do
		table.insert(cs,sgs.Sanguosha:getCard(id))
	end
	self:sortByUseValue(cs)
	local ids = {}
	for _,c in sgs.list(cs)do
		table.insert(ids,c:toString())
	end
	return "@ZhengyueCard="..table.concat(ids,"+")
end

sgs.ai_skill_invoke.zhantao = function(self,data)
	local damage = data:toDamage()
	return damage.from and self:isEnemy(damage.from)
end

sgs.ai_skill_playerschosen.anjing = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local tos = {}
	for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then table.insert(tos,target) end
		if #tos>=x then break end
	end
	return tos
end

sgs.ai_skill_invoke.anjing = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_playerchosen.tg_wuyong = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target) and self:damageIsEffective(target,nil,self.player)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target) and self:damageIsEffective(target,nil,self.player)
		then return target end
	end
	return destlist[#destlist]
end

sgs.ai_skill_playerchosen.tg_gangying = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) and p:isWounded()
		then return p end
	end
	return destlist[#destlist]
end

sgs.ai_skill_playerchosen.tg_guojue = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:doDisCard(p)
		then return p end
	end
	return destlist[#destlist]
end

sgs.ai_skill_playerchosen.tg_renzhi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) and self:canDraw(p)
		and p:getHandcardNum()<p:getMaxHp()
		then return p end
	end
	return destlist[#destlist]
end

addAiSkills("wanchan").getTurnUseCard = function(self)
	return sgs.Card_Parse("@WanchanCard=.")
end

sgs.ai_skill_use_func["WanchanCard"] = function(card,use,self)
	self:sort(self.friends_noself)
	for _,ep in sgs.list(self.friends_noself)do
		if self:canDraw(ep) then
			use.to:append(ep)
			use.card = card
			return
		end
	end
end

sgs.ai_use_value.WanchanCard = 9.4
sgs.ai_use_priority.WanchanCard = 4.8

sgs.ai_skill_invoke.jiangzhi = function(self,data)
	return true
end

sgs.ai_skill_playerchosen.jiangzhi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:doDisCard(p,"he",true,2)
		then return p end
	end
	return destlist[#destlist]
end

addAiSkills("thwuyan").getTurnUseCard = function(self)
	return self.thwuyanHs~=self.player:getHandcardNum()
	and sgs.Card_Parse("@ThWuyanCard=.")
end

sgs.ai_skill_use_func["ThWuyanCard"] = function(card,use,self)
	self:sort(self.friends_noself,nil,true)
	self:sort(self.enemies)
	for _,fp in sgs.list(self.friends_noself)do
		local cf = sgs.getDefense(fp)
		if not fp:isMale() or self.thwuyanDp==cf then continue end
		for _,ep in sgs.list(self.enemies)do
			if fp:canSlash(ep) then
				use.to:append(fp)
				use.to:append(ep)
				use.card = card
				self.thwuyanDp = cf
				self.thwuyanHs = self.player:getHandcardNum()
				return
			end
		end
	end
end

sgs.ai_use_value.ThWuyanCard = 9.4
sgs.ai_use_priority.ThWuyanCard = 4.8

sgs.ai_skill_use["@@thwuyan"] = function(self,prompt)
	self:sort(self.enemies)
	self:sort(self.friends_noself,nil,true)
	for _,fp in sgs.list(self.friends_noself)do
		if not fp:isMale() then continue end
		local valid = {fp:objectName()}
		for _,ep in sgs.list(self.enemies)do
			if fp:canSlash(ep) then
				table.insert(valid,ep:objectName())
				return string.format("@ThWuyanCard=.->%s",table.concat(valid,"+"))
			end
		end
	end
end

sgs.ai_skill_invoke.thwuyan = function(self,data)
	local tp = data:toPlayer()
	return not self:isFriend(tp)
end

sgs.ai_skill_cardask["zhanyu0"] = function(self,data,pattern,prompt)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByUseValue(cards)
	return cards[#cards]:toString()
end

addAiSkills("thzhengui").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	local ids = {}
  	for i,c in sgs.list(cards)do
		if table.contains(self.toUse,c) then continue end
		table.insert(ids,c:toString())
		if i>=#cards/2 or #ids>=self.player:getMaxHp() then break end
	end
	if #ids>0 then
		return sgs.Card_Parse("@ThZhenguiCard="..table.concat(ids,"+"))
	end
end

sgs.ai_skill_use_func["ThZhenguiCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,ep in sgs.list(self.enemies)do
		if not self:isWeak(ep) then
			use.to:append(ep)
			use.card = card
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		use.to:append(ep)
		use.card = card
		break
	end
end

sgs.ai_use_value.ThZhenguiCard = 9.4
sgs.ai_use_priority.ThZhenguiCard = 0.8

addAiSkills("jichun").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
  	for i,c in sgs.list(cards)do
		if table.contains(self.toUse,c) then continue end
		return sgs.Card_Parse("@JichunCard="..c:toString())
	end
end

sgs.ai_skill_use_func["JichunCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,p in sgs.list(self.enemies)do
		if p:getHandcardNum()>self.player:getHandcardNum() and self:doDisCard(p,"hej") then
			use.to:append(p)
			use.card = card
			return
		end
	end
	self:sort(self.friends_noself)
	for _,p in sgs.list(self.friends_noself)do
		if p:getHandcardNum()<self.player:getHandcardNum() and self:canDraw(p) then
			use.to:append(p)
			use.card = card
			return
		end
	end
end

sgs.ai_use_value.JichunCard = 9.4
sgs.ai_use_priority.JichunCard = 4.8

sgs.ai_skill_invoke.hanying = function(self,data)
	return true
end

sgs.ai_skill_playerchosen.hanying = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local id = self.player:getMark("hanyingId")
	local has = #self:poisonCards({id},p)>0
    for _,p in sgs.list(destlist)do
		if has then
			if self:isEnemy(p)
			then return p end
		else
			if self:isFriend(p)
			then return p end
		end
	end
    for _,p in sgs.list(destlist)do
		if has then
			if not self:isFriend(p)
			then return p end
		else
			if not self:isEnemy(p)
			then return p end
		end
	end
end

sgs.ai_skill_invoke.jianjiang = function(self,data)
	local damage = data:toDamage()
	if self.player:getMark("jianjiang2-Clear")<1 and self:isWeak(damage.to)
	then return self:isEnemy(damage.to) end
	return self.player:getMark("jianjiang1-Clear")<1
end

sgs.ai_skill_choice.jianjiang = function(self,choices,data)
	local items = choices:split("+")
	local damage = data:toDamage()
	if table.contains(items,"jianjiang2")
	and self:isWeak(damage.to) and self:isEnemy(damage.to)
	then return "jianjiang2" end
	if table.contains(items,"jianjiang1")
	then return "jianjiang1" end
end

sgs.ai_skill_invoke.thjueyan = function(self,data)
	return true
end

addAiSkills("yanzuo").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
  	for i,c in sgs.list(cards)do
		if table.contains(self.toUse,c) then continue end
		for i,id in sgs.list(RandomList(self.player:getPile("yanzuo")))do
			local ct = sgs.Sanguosha:getCard(id)
			if ct:isKindOf("BasicCard") or ct:isNDTrick() then
				local dc = dummyCard(ct:objectName())
				dc:setSkillName("_yanzuo")
				local d = self:aiUseCard(dc)
				if d.card then
					self.yanzuoUse = d
					sgs.ai_skill_choice.yanzuo = ct:objectName()
					return sgs.Card_Parse("@YanzuoCard="..c:toString())
				end
			end
		end
		break
	end
  	for i,c in sgs.list(cards)do
		if c:isKindOf("BasicCard") or c:isNDTrick() then
			local dc = dummyCard(c:objectName())
			dc:setSkillName("_yanzuo")
			local d = self:aiUseCard(dc)
			if d.card then
				self.yanzuoUse = d
				sgs.ai_skill_choice.yanzuo = c:objectName()
				return sgs.Card_Parse("@YanzuoCard="..c:toString())
			end
		end
	end
end

sgs.ai_skill_use_func["YanzuoCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.YanzuoCard = 9.4
sgs.ai_use_priority.YanzuoCard = 11.8

sgs.ai_skill_use["@@yanzuo!"] = function(self,prompt)
	local tps = {}
	for _,p in sgs.list(self.yanzuoUse.to)do
		table.insert(tps,p:objectName())
	end
   	return self.yanzuoUse.card:toString().."->"..table.concat(tps,"+")
end

sgs.ai_skill_playerchosen.pijian = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and self:damageIsEffective(p,nil,self.player)
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p) and self:damageIsEffective(p,nil,self.player)
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p)
		then return p end
	end
	return destlist[#destlist]
end

sgs.ai_skill_invoke.thqixin = function(self,data)
	return self:canDraw()
end

addAiSkills("jiusi").getTurnUseCard = function(self)
  	for i,pn in sgs.list(patterns())do
		local dc = dummyCard(pn)
		if dc and dc:isKindOf("BasicCard") then
			dc:setSkillName("jiusi")
			if dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				if d.card then
					self.jiusiTo = d.to
					sgs.ai_use_priority.JiusiCard = sgs.ai_use_priority[dc:getClassName()]
					return sgs.Card_Parse("@JiusiCard=.:"..pn)
				end
			end
		end
	end
end

sgs.ai_skill_use_func["JiusiCard"] = function(card,use,self)
	if self.jiusiTo then
		use.to = self.jiusiTo
		use.card = card
	end
end

sgs.ai_use_value.JiusiCard = 9.4
sgs.ai_use_priority.JiusiCard = 0.8

sgs.ai_guhuo_card.jiusi = function(self,toname,class_name)
	if self:getCardsNum(class_name)>0 then return end
	return "@JiusiCard=.:"..toname
end

sgs.ai_skill_invoke.chengyan = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_invoke.lianzhan = function(self,data)
	local use = data:toCardUse()
	self.lianzhanUse = use
	return self:isEnemy(use.to:last())
end

sgs.ai_skill_playerchosen.lianzhan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:canCanmou(p,self.lianzhanUse)
		then return p end
	end
end

addAiSkills("manhou").getTurnUseCard = function(self)
	return sgs.Card_Parse("@ManhouCard=.")
end

sgs.ai_skill_use_func["ManhouCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.ManhouCard = 9.4
sgs.ai_use_priority.ManhouCard = 8.8

sgs.ai_skill_choice.manhou = function(self,choices,data)
	local items = choices:split("+")
	return items[#items]
end

addAiSkills("tanluan").getTurnUseCard = function(self)
	return sgs.Card_Parse("@TanluanCard=.")
end

sgs.ai_skill_use_func["TanluanCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.TanluanCard = 9.4
sgs.ai_use_priority.TanluanCard = 0.8

sgs.ai_skill_use["@@tanluan"] = function(self,prompt)
	local cs = {}
	for _,id in sgs.list(self.player:getTag("tanluanForAI"):toIntList())do
		table.insert(cs,sgs.Sanguosha:getCard(id))
	end
	self:sortByUseValue(cs)
	for _,c in sgs.list(cs)do
		if c:isAvailable(self.player) and c:isDamageCard() then
			local d = self:aiUseCard(c)
			if d.card then
				local tps = {}
				for _,p in sgs.list(d.to)do
					table.insert(tps,p:objectName())
				end
				if c:canRecast() and d.to:length()<1 then continue end
				return c:toString().."->"..table.concat(tps,"+")
			end
		end
	end
	for _,c in sgs.list(cs)do
		if c:isAvailable(self.player) then
			local d = self:aiUseCard(c)
			if d.card then
				local tps = {}
				for _,p in sgs.list(d.to)do
					table.insert(tps,p:objectName())
				end
				if c:canRecast() and d.to:length()<1 then continue end
				return c:toString().."->"..table.concat(tps,"+")
			end
		end
	end
end

addAiSkills("weiti").getTurnUseCard = function(self)
	return sgs.Card_Parse("@WeitiCard=.")
end

sgs.ai_skill_use_func["WeitiCard"] = function(card,use,self)
	local destlist = sgs.QList2Table(self.room:getAlivePlayers()) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,p in sgs.list(destlist)do
		use.card = card
		use.to:append(p)
		break;
	end
end

sgs.ai_use_value.WeitiCard = 9.4
sgs.ai_use_priority.WeitiCard = 2.8

sgs.ai_skill_invoke.yuanrong = function(self,data)
	return true
end

sgs.ai_skill_askforag.yuanrong = function(self,card_ids)
    self.yuanrongIds = card_ids
	for _,id in sgs.list(card_ids)do
		local c = sgs.Sanguosha:getCard(id)
		local dc = dummyCard(c:objectName())
		if dc and dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then return id end
		end
	end
end

sgs.ai_skill_use["@@yuanrong!"] = function(self,prompt)
	local ids = self.player:getTag("yuanrongForAI"):toIntList();
	for _,id in sgs.list(self.yuanrongIds)do
		local c = sgs.Sanguosha:getCard(id)
		local dc = dummyCard(c:objectName(),"yuanrong")
		for _,cid in sgs.list(ids)do
			dc:addSubcard(cid)
			if dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				if d.card then
					local tps = {}
					for _,p in sgs.list(d.to)do
						table.insert(tps,p:objectName())
					end
					if not dc:canRecast() or d.to:length()>0 then
						return dc:toString().."->"..table.concat(tps,"+")
					end
				end
			end
			dc:clearSubcards()
		end
	end
end

sgs.ai_skill_invoke.boxuan = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_playerchosen.boxuan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:doDisCard(p,"he")
		then return p end
	end
end

sgs.ai_skill_use["@@boxuan"] = function(self,prompt)
	for _,id in sgs.list(self.player:getTag("boxuanForAI"):toIntList())do
		local c = sgs.Sanguosha:getCard(id)
		if c:isAvailable(self.player) then
			local d = self:aiUseCard(c)
			if d.card then
				local tps = {}
				for _,p in sgs.list(d.to)do
					table.insert(tps,p:objectName())
				end
				if c:canRecast() and d.to:length()<1 then continue end
				return c:toString().."->"..table.concat(tps,"+")
			end
		end
	end
end

sgs.ai_skill_playerschosen.thyizheng = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local tps = {}
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and p:getHandcardNum()>0 and #tps<x
		then table.insert(tps,p) end
	end
	return tps
end

sgs.ai_skill_playerchosen.thyizheng = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) and self:canDraw(p)
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if self:isFriend(p)
		then return p end
	end
end

sgs.ai_skill_use["@@guilin"] = function(self,prompt)
	if self:getCardsNum("Peach,Analeptic")<1 then
		return "@GuilinCard=."
	end
end

sgs.ai_skill_playerschosen.thguying = function(self,players,x)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local tps = {}
    for _,p in sgs.list(destlist)do
		if #tps<x and self:isFriend(p)
		then table.insert(tps,p) end
	end
    for _,p in sgs.list(destlist)do
		if #tps<x and not self:isEnemy(p)
		and not table.contains(tps,p)
		then table.insert(tps,p) end
	end
	return tps
end

addAiSkills("thmuzhen").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	local ids = {}
	local mt = ""
	for i,m in sgs.list(self.player:getMarkNames())do
		if m:contains("&thmuzhen+") then
			mt = m
			break
		end
	end
  	for i,c in sgs.list(cards)do
		if table.contains(self.toUse,c) then continue end
		if mt~="" then
			if mt:contains(c:getType()) or sgs.Sanguosha:getCard(ids[1]):getType()~=c:getType() then break end
			table.insert(ids,c:getId())
			if #ids>=self.player:getMark(mt) then
				return sgs.Card_Parse("@ThMuzhenCard="..table.concat(ids,"+"))
			end
		else
			return sgs.Card_Parse("@ThMuzhenCard="..c:toString())
		end
	end
end

sgs.ai_skill_use_func["ThMuzhenCard"] = function(card,use,self)
	local destlist = sgs.QList2Table(self.room:getAlivePlayers()) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if p~=self.player then
			use.card = card
			use.to:append(p)
			break
		end
	end
end

sgs.ai_use_value.ThMuzhenCard = 9.4
sgs.ai_use_priority.ThMuzhenCard = 4.8

sgs.ai_skill_choice.thmuzhen = function(self,choices,data)
	local items = choices:split("+")
	local tp = data:toPlayer()
	if self:isFriend(tp)
	then return "3" end
end

sgs.ai_skill_invoke.chuanyu = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_use["@@chuanyu"] = function(self,prompt)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	for i=0,9 do
		local c,p = self:getCardNeedPlayer(cards,false)
		if c and p then
			if(c:isAvailable(p) and c:getTypeId()<3)then
				return "@ChuanyuCard="..c:toString().."->"..p:objectName()
			end
			table.removeOne(cards,c)
			if(#cards<1)then break end
		end
	end
	for _,c in sgs.list(cards)do
		for _,p in sgs.list(self.friends_noself)do
			if(c:isAvailable(p) and c:getTypeId()<3)then
				return "@ChuanyuCard="..c:toString().."->"..p:objectName()
			end
		end
		for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
			if(c:isAvailable(p) and c:getTypeId()<3)then
				return "@ChuanyuCard="..c:toString().."->"..p:objectName()
			end
		end
	end
	for _,c in sgs.list(cards)do
		for _,p in sgs.list(self.friends_noself)do
			return "@ChuanyuCard="..c:toString().."->"..p:objectName()
		end
		for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
			return "@ChuanyuCard="..c:toString().."->"..p:objectName()
		end
	end
end

sgs.ai_skill_playerchosen.chuanyu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p)
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if not self:isEnemy(p)
		then return p end
	end
	return destlist[1]
end

sgs.ai_skill_use["@@chuanyu1"] = function(self,prompt)
	self:sort(self.enemies)
	for _,ep in sgs.list(self.enemies)do
		local tps = {ep:objectName()}
		for _,p in sgs.list(self.room:getOtherPlayers(ep))do
			if(p:getMark("chuanyuBf_lun")>0 and p:canSlash(p,false))then
				table.insert(tps,p:objectName())
			end
		end
		if #tps>1 then
			return "@ChuanyuCard=.:@@chuanyu1->"..table.concat(tps,"+")
		end
	end
end

sgs.ai_skill_invoke.yitou = function(self,data)
	local tp = data:toPlayer()
	return self:isFriend(tp) and #self.enemies>0
end

sgs.ai_skill_invoke.gengdu = function(self,data)
	return self:canDraw()
end

addAiSkills("gengdu").getTurnUseCard = function(self)
  	for i,c in sgs.list(self.player:getCards("he"))do
		if table.contains(self.toUse,c) or not c:isRed() then continue end
		return sgs.Card_Parse("@GengduCard=.")
	end
end

sgs.ai_skill_use_func["GengduCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.GengduCard = 9.4
sgs.ai_use_priority.GengduCard = 2.8

sgs.ai_skill_askforag.gengdu = function(self,card_ids)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	for _,id in sgs.list(card_ids)do
		local c = sgs.Sanguosha:getCard(id)
		if not c:isDamageCard() then continue end
		local dc = dummyCard(c:objectName(),"gengdu")
		for i,h in sgs.list(cards)do
			if table.contains(self.toUse,h) or not h:isRed() then continue end
			dc:addSubcard(h)
			if dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				self.gengduUse = d
				if d.card then return id end
			end
			dc:clearSubcards()
		end
	end
	for _,id in sgs.list(card_ids)do
		local c = sgs.Sanguosha:getCard(id)
		local dc = dummyCard(c:objectName(),"gengdu")
		for i,h in sgs.list(cards)do
			if table.contains(self.toUse,h) or not h:isRed() then continue end
			dc:addSubcard(h)
			if dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				self.gengduUse = d
				if d.card then return id end
			end
			dc:clearSubcards()
		end
	end
end

sgs.ai_skill_use["@@gengdu"] = function(self,prompt)
	local d = self.gengduUse
	if d.card then
		local tps = {}
		for _,p in sgs.list(d.to)do
			table.insert(tps,p:objectName())
		end
		return d.card:toString().."->"..table.concat(tps,"+")
	end
end

sgs.ai_skill_invoke.gumai = function(self,data)
	local damage = data:toDamage()
	return self:isFriend(damage.to) or self:isEnemy(damage.to) and self:isWeak(damage.to)
end

sgs.ai_skill_choice.gumai = function(self,choices,data)
	local items = choices:split("+")
	local damage = data:toDamage()
	if self:isFriend(damage.to)
	then return "2" end
	return "1"
end

sgs.ai_skill_cardask["gumai0"] = function(self,data,pattern,prompt)
	return self.player:getHandcardNum()>1
end

sgs.ai_skill_invoke.pigua = function(self,data)
	local tp = data:toPlayer()
	return not self:isFriend(tp)
end

sgs.ai_skill_use["@@jikun"] = function(self,prompt)
	local n = 0
	local destlist = sgs.QList2Table(self.room:getAlivePlayers()) -- 将列表转换为表
	for _,p in sgs.list(destlist)do
		n = math.max(n,p:getHandcardNum())
	end
	self:sort(destlist)
	self:sort(self.friends_noself)
	for _,p in sgs.list(self.friends_noself)do
		if not self:canDraw(p) then continue end
		local tps = {p:objectName()}
		for _,q in sgs.list(self.enemies)do
			if(q:getHandcardNum()>=n)then
				table.insert(tps,q:objectName())
				return "@JikunCard=.->"..table.concat(tps,"+")
			end
		end
		for _,q in sgs.list(destlist)do
			if(q:getHandcardNum()>=n and not self:isFriend(q))then
				table.insert(tps,q:objectName())
				return "@JikunCard=.->"..table.concat(tps,"+")
			end
		end
		for _,q in sgs.list(destlist)do
			if(q:getHandcardNum()>=n and q~=p)then
				table.insert(tps,q:objectName())
				return "@JikunCard=.->"..table.concat(tps,"+")
			end
		end
	end
	for _,p in sgs.list(self.friends_noself)do
		local tps = {p:objectName()}
		for _,q in sgs.list(self.enemies)do
			if(q:getHandcardNum()>=n)then
				table.insert(tps,q:objectName())
				return "@JikunCard=.->"..table.concat(tps,"+")
			end
		end
		for _,q in sgs.list(destlist)do
			if(q:getHandcardNum()>=n and not self:isFriend(q))then
				table.insert(tps,q:objectName())
				return "@JikunCard=.->"..table.concat(tps,"+")
			end
		end
		for _,q in sgs.list(destlist)do
			if(q:getHandcardNum()>=n and q~=p)then
				table.insert(tps,q:objectName())
				return "@JikunCard=.->"..table.concat(tps,"+")
			end
		end
	end
end

sgs.ai_skill_invoke.dujun = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_playerchosen.dujun = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) and self:canDraw(p)
		and self:isWeak(p) and p:getHp()<self.player:getHp()
		then return p end
	end
end











