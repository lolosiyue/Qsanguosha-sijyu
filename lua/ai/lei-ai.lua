--往烈
sgs.ai_skill_invoke.wanglie = function(self,data)
	local use = data:toCardUse()
	return use.card:isKindOf("Duel") or use.card:isKindOf("SavageAssault") or use.card:isKindOf("ArcheryAttack") or use.card:isKindOf("Slash") or use.card:hasFlag("drank")
end

--罪论
sgs.ai_skill_invoke.zuilun = function(self,data)
	local n = 0
	if self.player:getMark("damage_point_round")>0 then n = n+1 end
	local minhandnum = true
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if p:getHandcardNum()<self.player:getHandcardNum() then
			minhandnum = false
			break
		end
	end
	if minhandnum then n = n+1 end
	if self.player:getMark("zuilun_discard-Clear")<=0 then n = n+1 end
	
	if n==0 then
		local to = self:findPlayerToLoseHp()
		if not to then return false end
		if to:getHp()>self.player:getHp() then return false end
		if (self.player:isLord() or self.player:getRole()=="renegade") and self:isWeak() then return false end
		return true
	end
	if self:hasJieyingyEffect(self.player) then return false end
	return true
end

sgs.ai_skill_playerchosen.zuilun = function(self,targets)
	return self:findPlayerToLoseHp() or targets[1]
end

sgs.ai_playerchosen_intention.zuilun = function(self,from,to)
	if self:needToLoseHp(friend,self.player,false,true) then return end
	if to:hasSkill("zhaxiang") then return end
	sgs.updateIntention(from,to,10)
end

sgs.ai_target_revises.fuyin = function(to,card,self)
    if self:getOverflow()>1 or to:getHandcardNum()>=self.player:getHandcardNum() then
		return
	end
    if card:isKindOf("Slash") then
		if to:getMark("fuyin-Clear")<1
		and self:getCardsNum("Duel")<1
		then return true end
	end
    if card:isKindOf("Duel") then
		if to:getMark("fuyin-Clear")<1
		and self:getCardsNum("Slash,Duel")<2
		then return true end
	end
end

--良姻
sgs.ai_skill_playerchosen.liangyin = function(self,targets)
	local tos = sgs.QList2Table(targets)
	self:sort(tos)
	for _,p in sgs.list(tos)do
		if p:getHandcardNum()>self.player:getHandcardNum() and self:isFriend(p) and self:canDraw(p) then
			return p
		elseif p:getHandcardNum()<self.player:getHandcardNum() and p:canDiscard(p,"he") then
			if self:isEnemy(p) and not self:needToThrowCard(p) and not (not self:hasLoseHandcardEffective(p) and not p:isKongcheng()) then return p end
			if self:isFriend(p) and self:needToThrowCard(p) then return p end
		end
	end
	if tos[1]:getHandcardNum()>self.player:getHandcardNum() then self:noChoice(targets,"draw") end
	if tos[1]:getHandcardNum()<self.player:getHandcardNum() then self:noChoice(targets,"letDis") end
	return nil
end

sgs.ai_playerchosen_intention.liangyin = function(self,from,to)
	if to:getHandcardNum()>from:getHandcardNum() and self:canDraw(to,from) then sgs.updateIntention(from,to,-10) end
	if to:getHandcardNum()<from:getHandcardNum() and not self:needToThrowCard(to) then sgs.updateIntention(from,to,10) end
end

--箜声
sgs.ai_skill_discard.kongsheng = function(self,discard_num,min_num,optional,include_equip)
	local cards = sgs.QList2Table(self.player:getHandcards())
	local to_discard = {}
	self:sortByKeepValue(cards)
	if (self:needKongcheng() and not self.player:isKongcheng()) or self.player:containsTrick("indulgence") then
		for _,card in sgs.list(cards)do
			table.insert(to_discard,card:getId())
		end
	end
	
	local useSlash = false
	local slash,ana,peach = 0,0,0
	for _,card in sgs.list(cards)do
		if (card:isKindOf("Jink") or card:isKindOf("DefensiveHorse")) and not table.contains(to_discard,card:getId()) then
			table.insert(to_discard,card:getId())
		elseif card:isKindOf("Slash") then
			if self:willUse(self.player,card,false,false,true) then
				useSlash = true
				slash = slash+1
			else
				if not table.contains(to_discard,card:getId()) then table.insert(to_discard,card:getId()) end
			end
			if slash>1 and not self:hasCrossbowEffect() and not table.contains(to_discard,card:getId()) then table.insert(to_discard,card:getId()) end
		elseif card:isKindOf("Analeptic") then
			if (not useSlash or ana>0) and not table.contains(to_discard,card:getId()) then table.insert(to_discard,card:getId()) end
			ana = ana+1
		elseif card:isKindOf("TrickCard") and not card:isKindOf("ExNihilo") and not card:isKindOf("Nullification") then
			local dummyuse = dummy()
			self:useTrickCard(card,dummyuse)
			if not dummyuse.card then
				if not table.contains(to_discard,card:getId()) then table.insert(to_discard,card:getId()) end
			end
		elseif card:isKindOf("OffensiveHorse") and self.player:getOffensiveHorse() then
			if not table.contains(to_discard,card:getId()) then table.insert(to_discard,card:getId()) end
		elseif card:isKindOf("Weapon") and self.player:getWeapon() and #to_discard==0 then
			if not table.contains(to_discard,card:getId()) then table.insert(to_discard,card:getId()) end
		elseif card:isKindOf("Peach") then
			local dummy_use = dummy()
			self:useBasicCard(card,dummy_use)
			if dummy_use.card then
				peach = peach+1
			else
				if not table.contains(to_discard,card:getId()) then table.insert(to_discard,card:getId()) end
			end
			if peach>1 and not table.contains(to_discard,card:getId()) then table.insert(to_discard,card:getId()) end
		end
	end
	if self:needToThrowArmor() then table.insert(to_discard,self.player:getArmor():getId()) end
	if self.player:getDefensiveHorse() then table.insert(to_discard,self.player:getDefensiveHorse():getId()) end
	return to_discard
end

sgs.ai_target_revises.qianjie = function(to,card,self,use)
    if card:isKindOf("IronChain")
	then return true end
end

--决堰
local jueyan_skill = {}
jueyan_skill.name = "jueyan"
table.insert(sgs.ai_skills,jueyan_skill)
jueyan_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@JueyanCard=.")
end

sgs.ai_skill_use_func.JueyanCard = function(card,use,self)
	self.jueyan = nil
	if self:needToThrowArmor() and self.player:hasEquipArea(1) then 
		sgs.ai_use_priority.JueyanCard = 10
		self.jueyan = "1"
		use.card = card
		return
	end
	
	local cards = sgs.QList2Table(self.player:getCards("h"))
	local w,h,t = 0,0,0
	for _,c in sgs.list(cards)do
		if c:isKindOf("Slash") then
			if self:willUse(self.player,c,false,true) then w = w+1 end
			if self:slashIsAvailable() and self:willUse(self.player,c,true) then h = h+1 end
		end
		if c:isKindOf("Snatch") or c:isKindOf("SupplyShortage") and self:willUse(self.player,c,true) then h = h+1 end
		if c:isNDTrick() and self:willUse(self.player,c) then t = t+1 end
	end
	if t>0 and self.player:hasEquipArea(4) and not self:keepWoodenOx() then 
		sgs.ai_use_priority.JueyanCard = 10
		self.jueyan = "4"
		use.card = card 
		return
	end
	if w>0 and self.player:hasEquipArea(0) then 
		sgs.ai_use_priority.JueyanCard = sgs.ai_use_priority.Slash-0.1 
		self.jueyan = "0"
		use.card = card 
		return
	end
	if h>0 and (self.player:hasEquipArea(2) or self.player:hasEquipArea(3)) then 
		sgs.ai_use_priority.JueyanCard = sgs.ai_use_priority.Slash-0.1 
		self.jueyan = "23"
		use.card = card 
		return
	end
	if self.player:hasEquipArea(1) and (not self.player:getArmor() or self:isWeak() or self.player:getHandcardNum()<3) then 
		if self.player:isWounded() then sgs.ai_use_priority.JueyanCard = sgs.ai_use_priority.SilverLion-0.1 else sgs.ai_use_priority.JueyanCard = 10 end
		self.jueyan = "1"
		use.card = card 
		return
	end
end

sgs.ai_use_priority.JueyanCard = 10

sgs.ai_skill_choice.jueyan = function(self,choices,data)
	local items = choices:split("+")
	if self.jueyan then
		return self.jueyan
	else
		return items[1]
	end
end

--怀柔
local huairou_skill = {}
huairou_skill.name = "huairou"
table.insert(sgs.ai_skills,huairou_skill)
huairou_skill.getTurnUseCard = function(self,inclusive)	--yun
	if self:needToThrowArmor() then return sgs.Card_Parse("@HuairouCard="..self.player:getArmor():getEffectiveId()) end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	for _,c in sgs.list(cards)do
		if c:isKindOf("EquipCard") then
			local equip = c:getRealCard():toEquipCard()
			local equip_index = equip:location()
			if self.player:isProhibited(self.player,c) or not self.player:hasEquipArea(equip_index) then
				return sgs.Card_Parse("@HuairouCard="..c:getEffectiveId())
			end
			
			if self:willUse(self.player,c) then
				if c:isKindOf("Weapon") and self.player:getWeapon() then 
					return sgs.Card_Parse("@HuairouCard="..self.player:getWeapon():getEffectiveId())
				elseif c:isKindOf("Armor") and self.player:getArmor() then
					return sgs.Card_Parse("@HuairouCard="..self.player:getArmor():getEffectiveId())
				elseif c:isKindOf("DefensiveHorse") and self.player:getDefensiveHorse() then
					return sgs.Card_Parse("@HuairouCard="..self.player:getDefensiveHorse():getEffectiveId())
				elseif c:isKindOf("OffensiveHorse") and self.player:getOffensiveHorse() then
					return sgs.Card_Parse("@HuairouCard="..self.player:getOffensiveHorse())				
				end
			else
				return sgs.Card_Parse("@HuairouCard="..c:getEffectiveId())
			end
		end
	end
	return
end

sgs.ai_skill_use_func.HuairouCard = function(card,use,self)
	use.card = card
end

sgs.ai_use_priority.HuairouCard = 10
sgs.ai_use_value.HuairouCard = 10

--镇骨
sgs.ai_skill_playerchosen.zhengu = function(self,targets)
	local n = self.player:getHandcardNum()
	local f,e = n,n
	
	local dis,drw = 0,0
	local fri,ene,eqf,eqe
	self:sort(self.friends_noself,"handcard")
	for _,who in sgs.list(self.friends_noself)do
		if who:getHandcardNum()<f and not self:needKongcheng(who,true) and not hasManjuanEffect(who) then f = who:getHandcardNum() fri = who end
		if who:getHandcardNum()==n and not eqf and not hasManjuanEffect(who) and n>1 then eqf = who end
	end
	
	drw = n-f
	if drw>0 and n>5 then 
		drw = 5-f
	end
	
	self:sort(self.enemies,"handcard")
	self.enemies = sgs.reverse(self.enemies)
	for _,who in sgs.list(self.enemies)do
		if who:getHandcardNum()>e and not (n==0 and self:needKongcheng(who,true)) then e = who:getHandcardNum() ene = who end
		if who:getHandcardNum()==n and n<2 then eqe = who end
	end
	dis = e-n
	if drw>0 and drw>=dis then return fri end
	if dis>0 and dis>=drw then return ene end
	if drw>0 and drw==dis then
		if n>1 and fri then return fri end
		if n<2 and ene then return ene end
	end
	if eqf then return eqf end
	if eqe then return eqe end
	return nil
end

sgs.ai_playerchosen_intention.zhengu = function(self,from,to)
	if to:getHandcardNum()<from:getHandcardNum() and self:canDraw(to,from) then sgs.updateIntention(from,to,-10) end
	if to:getHandcardNum()>from:getHandcardNum() and not self:needToThrowCard(to,"h") then sgs.updateIntention(from,to,10) end
end

--征荣
sgs.ai_skill_invoke.zhengrong = function(self,data)
	local target = data:toPlayer()
	if self:isFriend(target) then
		if self:getOverflow(target)>2 then return true end
		if self:needToThrowCard(target) then return true end
		if self:needKongcheng(target) and target:getHandcardNum()==1 then return true end
		return false
	end
	if self:isEnemy(target) then
		if not self:doDisCard(target,"he",true) then return false end
		return true
	end
	return true
end

--鸿举
sgs.ai_skill_use["@@hongju"] = function(self,prompt)
    local pile = self.player:getPile("rong")
	local piles = {}
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
	local max_num = math.min(pile:length(),#cards)
	for _,card_id in sgs.qlist(pile)do
		table.insert(piles,sgs.Sanguosha:getCard(card_id))
	end
	
	local exchange_to_pile = {}
	local exchange_to_handcard = {}
	self:sortByCardNeed(cards)
	self:sortByCardNeed(piles)
	for i = 1 ,max_num,1 do
		if self:cardNeed(piles[#piles])>self:cardNeed(cards[1]) then
			table.insert(exchange_to_handcard,piles[#piles])
			table.insert(exchange_to_pile,cards[1])
			table.removeOne(piles,piles[#piles])
			table.removeOne(cards,cards[1])
		else
			break
		end
	end
	if #exchange_to_handcard==0 then return "." end
	local exchange = {}

	for _,c in sgs.list(exchange_to_handcard)do
		table.insert(exchange,c:getId())
	end

	for _,c in sgs.list(exchange_to_pile)do
		table.insert(exchange,c:getId())
	end

	return "@HongjuCard="..table.concat(exchange,"+")
end

--清侧
local qingce_skill = {}
qingce_skill.name = "qingce"
table.insert(sgs.ai_skills,qingce_skill)
qingce_skill.getTurnUseCard = function(self)
	local piles = self.player:getPile("rong")
	return sgs.Card_Parse("@QingceCard="..piles:first())	
end

sgs.ai_skill_use_func.QingceCard = function(card,use,self)	
	local to = self:findPlayerToDiscard("ej",true,false,players)[1]
	if not to then return end
	use.card = card			
	use.to:append(to)
end

sgs.ai_use_priority.QingceCard = 4.2

--OL征荣
sgs.ai_skill_playerchosen.olzhengrong = function(self,targets)
	local to = self:findPlayerToDiscard("he",true,false,targets)[1]
	if to then return to end
	for _,to in sgs.qlist(targets)do
		if not self:isFriend(to) and self:doDisCard(to,"he",true) then return to end
	end
	return nil
end

--OL鸿举
sgs.ai_skill_use["@@olhongju"] = function(self,prompt)
    local pile = self.player:getPile("rong")
	local piles = {}
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
	local max_num = math.min(pile:length(),#cards)
	for _,card_id in sgs.qlist(pile)do
		table.insert(piles,sgs.Sanguosha:getCard(card_id))
	end
	
	local exchange_to_pile = {}
	local exchange_to_handcard = {}
	self:sortByCardNeed(cards)
	self:sortByCardNeed(piles)
	for i = 1 ,max_num,1 do
		if self:cardNeed(piles[#piles])>self:cardNeed(cards[1]) then
			table.insert(exchange_to_handcard,piles[#piles])
			table.insert(exchange_to_pile,cards[1])
			table.removeOne(piles,piles[#piles])
			table.removeOne(cards,cards[1])
		else
			break
		end
	end
	if #exchange_to_handcard==0 then return "." end
	local exchange = {}

	for _,c in sgs.list(exchange_to_handcard)do
		table.insert(exchange,c:getId())
	end

	for _,c in sgs.list(exchange_to_pile)do
		table.insert(exchange,c:getId())
	end

	return "@OLHongjuCard="..table.concat(exchange,"+")
end

--OL清侧
local olqingce_skill = {}
olqingce_skill.name = "olqingce"
table.insert(sgs.ai_skills,olqingce_skill)
olqingce_skill.getTurnUseCard = function(self)
	local hands = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(hands)
	if hands[1]:isKindOf("ExNihilo") then return end
	if self:isWeak() and hands[1]:isKindOf("Peach") then return end
	if self:isWeak() and hands[1]:isKindOf("Jink") and self:getCardsNum("Jink")<2 then return end
	local piles = {}
	for _,id in sgs.qlist(self.player:getPile("rong"))do
		table.insert(piles,sgs.Sanguosha:getCard(id))
	end
	self:sortByUseValue(piles,true)
	local c = {}
	table.insert(c,hands[1]:getId())
	table.insert(c,piles[1]:getId())
	return sgs.Card_Parse("@OLQingceCard="..table.concat(c,"+"))	
end

sgs.ai_skill_use_func.OLQingceCard = function(card,use,self)	
	local to = self:findPlayerToDiscard("ej",true,false,players)[1]
	if not to then return end
	use.card = card			
	use.to:append(to)
end

sgs.ai_use_priority.OLQingceCard = sgs.ai_use_priority.Dismantlement-0.1

--手杀征荣
sgs.ai_skill_playerchosen.mobilezhengrong = function(self,targets)
	local to = self:findPlayerToDiscard("he",false,false,targets)[1]
	if to then return to end
	for _,to in sgs.qlist(targets)do
		if not self:isFriend(to) and self:doDisCard(to) then return to end
	end
	for _,to in sgs.qlist(targets)do
		if not self:isFriend(to) then return to end
	end
	return targets:at(math.random(0,targets:length()-1))
end

--手杀鸿举
sgs.ai_skill_use["@@mobilehongju"] = function(self,prompt)
    local pile = self.player:getPile("rong")
	local piles = {}
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
	local max_num = math.min(pile:length(),#cards)
	for _,card_id in sgs.qlist(pile)do
		table.insert(piles,sgs.Sanguosha:getCard(card_id))
	end
	
	local exchange_to_pile = {}
	local exchange_to_handcard = {}
	self:sortByCardNeed(cards)
	self:sortByCardNeed(piles)
	for i = 1 ,max_num,1 do
		if self:cardNeed(piles[#piles])>self:cardNeed(cards[1]) then
			table.insert(exchange_to_handcard,piles[#piles])
			table.insert(exchange_to_pile,cards[1])
			table.removeOne(piles,piles[#piles])
			table.removeOne(cards,cards[1])
		else
			break
		end
	end
	if #exchange_to_handcard==0 then return "." end
	local exchange = {}

	for _,c in sgs.list(exchange_to_handcard)do
		table.insert(exchange,c:getId())
	end

	for _,c in sgs.list(exchange_to_pile)do
		table.insert(exchange,c:getId())
	end

	return "@MobileHongjuCard="..table.concat(exchange,"+")
end

--伪帝
sgs.ai_skill_askforyiji.leiweidi = function(self,card_ids,targets)
	local friends = {}
	for _,p in sgs.list(targets)do
		if hasManjuanEffect(p) or self:isLihunTarget(p)
		or p:getKingdom()~="qun" or not self:isFriend(p) then continue end
		table.insert(friends,p)
	end
	if #friends<1 then
		for _,p in sgs.list(targets)do
			if hasManjuanEffect(p) or self:isLihunTarget(p)
			or p:getKingdom()~="qun" or self:isEnemy(p) then continue end
			table.insert(friends,p)
		end
	end
	local to,id = sgs.ai_skill_askforyiji.nosyiji(self,card_ids,targets)
	if id and to and targets:contains(to) then
		to:addMark("leiweidi_to-Clear")
		return to,id
	end
	local keep,toGive,allcards = nil,{},{}
	for _,id in sgs.list(card_ids)do
		local card = sgs.Sanguosha:getCard(id)
		if not keep and isCard("Jink,Analeptic",card,self.player)
		then keep = true
		else
			table.insert(toGive,card)
		end
		table.insert(allcards,card)
	end
	local card,friend = self:getCardNeedPlayer(allcards)
	if card and friend and targets:contains(friend) then
		friend:addMark("leiweidi_to-Clear")
		return friend,card:getId()
	end
	allcards = #toGive>0 and toGive or allcards
	self:sortByKeepValue(allcards,true)
	if #friends>0 then
		local id = allcards[1]:getId()
		self:sort(friends,"handcard")
		for _,ap in sgs.list(friends)do
			if self:needKongcheng(ap,true)
			then continue end
			ap:addMark("leiweidi_to-Clear")
			return ap,id
		end
		self:sort(friends,"defense")
		friends[1]:addMark("leiweidi_to-Clear")
		return friends[1],id
	end
	return nil,-1
end

--从谏
sgs.ai_skill_askforyiji.congjian = function(self,card_ids,tos)
	local equips,ces,cs = {},{},{}
	for _,id in sgs.list(card_ids)do
		local equip = sgs.Sanguosha:getCard(id)
		table.insert(cs,equip)
		if not equip:isKindOf("EquipCard")
		or equip:objectName()=="wooden_ox" and self.player:getPile("wooden_ox"):length()>0
		then continue end
		table.insert(ces,equip)
		table.insert(equips,id)
	end
	local c,to = self:getCardNeedPlayer(ces,nil,tos)
	if c and to and to~=self.player then return to,c:getEffectiveId() end
	local c,to = self:getCardNeedPlayer(cs,nil,tos)
	if c and to and to~=self.player then return to,c:getEffectiveId() end
	if #equips>0 then
		local to,id = sgs.ai_skill_askforyiji.nosyiji(self,equips,tos)
		if to and id>0 and to~=self.player then return to,id end
	end
	local to,id = sgs.ai_skill_askforyiji.nosyiji(self,card_ids,tos)
	if to and id>0 and to~=self.player then return to,id end
	return nil,-1
end

--雄乱
local xiongluan_skill = {}
xiongluan_skill.name = "xiongluan"
table.insert(sgs.ai_skills,xiongluan_skill)
xiongluan_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@XiongluanCard=.")
end

sgs.ai_skill_use_func.XiongluanCard = function(card,use,self)
	local slashes = self:getCards("Slash")
	for _,e in sgs.list(self.player:getEquips())do
		for _,slash in sgs.list(table.copyFrom(slashes))do
			if slash:getEffectiveId()==e:getId()
			or slash:getSkillName()==e:objectName()
			or not slash:isAvailable(self.player)
			then table.removeOne(slashes,slash) end
		end
	end
	if #slashes<1 then return end
	self:sort(self.enemies,"hp")
	for _,enemy in sgs.list(self.enemies)do
		if enemy:getHp()<2 or #self.enemies<2
		then continue end
		local n = 0
		for _,slash in sgs.list(slashes)do
			local d = self:aiUseCard(slash)
			if d.card and d.to:contains(enemy)
			and not(self:hasEightDiagramEffect(enemy)) then
				n = n+1
				if n>=enemy:getHp() then
					use.card = card
					use.to:append(enemy)
					return
				end
			end
		end
	end
end

sgs.ai_use_priority.XiongluanCard = sgs.ai_use_priority.Slash+0.5
sgs.ai_use_value.XiongluanCard = 3


--夺锐
sgs.ai_skill_invoke.duorui = function(self,data)
	local player = data:toPlayer()
	if self:isFriend(player) then return false end
	local name = player:getGeneralName()
	local g = sgs.Sanguosha:getGeneral(name)
	if not g then return false end
	--[[if self:isFriend(player) then
		for _,sk in sgs.list(g:getSkillList())do
			if not sk:isVisible() then continue end
			if sk:isLimitedSkill() then continue end
			if sk:getFrequency()==sgs.Skill_Wake then continue end
			if sk:isLordSkill() then continue end
			if string.find(sgs.bad_skills,sk:objectName()) and player:hasSkill(sk) then return true end
		end
		if player:getGeneral2() then
			local name2 = player:getGeneral2Name()
			local g2 = sgs.Sanguosha:getGeneral(name2)
			if g2 then
				for _,sk in sgs.list(g2:getSkillList())do
					if not sk:isVisible() then continue end
					if sk:isLimitedSkill() then continue end
					if sk:getFrequency()==sgs.Skill_Wake then continue end
					if sk:isLordSkill() then continue end
					if string.find(sgs.bad_skills,sk:objectName()) and player:hasSkill(sk) then return true end
				end
			end
		end
	end]]
	if not self:isFriend(player) then
		for _,sk in sgs.list(g:getSkillList())do
			if not sk:isVisible() then continue end
			if sk:isLimitedSkill() then continue end
			if sk:getFrequency()==sgs.Skill_Wake then continue end
			if sk:isLordSkill() then continue end
			if string.find(sgs.bad_skills,sk:objectName()) then continue end
			return true
		end
		if player:getGeneral2() then
			local name2 = player:getGeneral2Name()
			local g2 = sgs.Sanguosha:getGeneral(name2)
			if g2 then
				for _,sk in sgs.list(g2:getSkillList())do
					if not sk:isVisible() then continue end
					if sk:isLimitedSkill() then continue end
					if sk:getFrequency()==sgs.Skill_Wake then continue end
					if sk:isLordSkill() then continue end
					if string.find(sgs.bad_skills,sk:objectName()) then continue end
					return true
				end
			end
		end
	end
	return false
end

sgs.ai_skill_choice.duorui_area = function(self,choices,data)
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
end

sgs.ai_skill_choice.duorui = function(self,choices,data)
	local player = data:toDamage().to
	local skills = choices:split("+")
	--[[if self:isFriend(player) then
		for _,sk in sgs.list(skills)do
			if string.find(sgs.bad_skills,sk) and player:hasSkill(sk) then return sk end
		end
		return skills[1]
	end]]
	
	for _,sk in sgs.list(skills)do
		if self:isValueSkill(sk,player,true) then
			return sk
		end
	end
	
	for _,sk in sgs.list(skills)do
		if self:isValueSkill(sk,player) then
			return sk
		end
	end
	
	local not_bad_skills = {}
	for _,sk in sgs.list(skills)do
		if string.find(sgs.bad_skills,sk) then continue end
		table.insert(not_bad_skills,sk)
	end
	if #not_bad_skills>0 then
		return not_bad_skills[math.random(1,#not_bad_skills)]
	end
	
	return skills[math.random(1,#skills)]
end

--止啼
sgs.ai_skill_choice.zhiti = function(self,choices,data)
	local items = choices:split("+")
	if not self.player:hasEquipArea(1) then
		return "1"
	elseif not self.player:hasEquipArea(0) then
		return "0"
	elseif not self.player:hasEquipArea(2) then
		return "2"
	elseif not self.player:hasEquipArea(3) then
		return "3"
	elseif not self.player:hasEquipArea(4) then
		return "4"
	else
		return items[1]
	end
end

--魄袭
local poxi_skill = {}
poxi_skill.name = "poxi"
table.insert(sgs.ai_skills,poxi_skill)
poxi_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@PoxiCard=.")
end

sgs.ai_skill_use_func.PoxiCard = function(card,use,self)
	if #self.enemies<=0 then return end
	local target = nil
	self:sort(self.enemies,"handcard")
	self.enemies = sgs.reverse(self.enemies)
	for _,p in sgs.list(self.enemies)do
		if self:doDisCard(p,"h")
		then target = p	break end
	end
	if not target then return end
	self.poxi_target = nil
	if target:getHandcardNum()>0 then
		use.card = card
		self.poxi_target = target
		use.to:append(target)
	end
end

sgs.ai_skill_use["@@poxi"] = function(self,prompt)
	if self.poxi_target then
		local target_handcards = sgs.QList2Table(self.poxi_target:getCards("h"))
		self:sortByUseValue(target_handcards,inverse)
		local handcards = sgs.QList2Table(self.player:getCards("h"))
		local discard_cards = {}
		local spade_check = true
		local heart_check = true
		local club_check = true
		local diamond_check = true
		local target_discard_count = 0
		
		for _,c in sgs.list(target_handcards)do
			if spade_check and c:getSuit()==sgs.Card_Spade then
				spade_check = false
				table.insert(discard_cards,c:getEffectiveId())
			elseif heart_check and c:getSuit()==sgs.Card_Heart then
				heart_check = false
				table.insert(discard_cards,c:getEffectiveId())
			elseif club_check and c:getSuit()==sgs.Card_Club then
				club_check = false
				table.insert(discard_cards,c:getEffectiveId())
			elseif diamond_check and c:getSuit()==sgs.Card_Diamond then
				diamond_check = false
				table.insert(discard_cards,c:getEffectiveId())
			end
			target_discard_count = #discard_cards
		end
		
		for _,c in sgs.list(handcards)do
			if not c:isKindOf("Peach")
			and not c:isKindOf("Duel")
			and not c:isKindOf("Indulgence")
			and not c:isKindOf("SupplyShortage")
			and not (self:getCardsNum("Jink")==1 and c:isKindOf("Jink"))
			and not (self:getCardsNum("Analeptic")==1 and c:isKindOf("Analeptic"))
			then
				if spade_check and c:getSuit()==sgs.Card_Spade then
					spade_check = false
					table.insert(discard_cards,c:getEffectiveId())
				elseif heart_check and c:getSuit()==sgs.Card_Heart then
					heart_check = false
					table.insert(discard_cards,c:getEffectiveId())
				elseif club_check and c:getSuit()==sgs.Card_Club then
					club_check = false
					table.insert(discard_cards,c:getEffectiveId())
				elseif diamond_check and c:getSuit()==sgs.Card_Diamond then
					diamond_check = false
					table.insert(discard_cards,c:getEffectiveId())
				end
			end
		end
		
		if target_discard_count==4 and not self.player:isWounded() then return "." end
		if 4-target_discard_count==1 and self.player:getHandcardNum()>self.player:getMaxCards() then return "." end
		
		if #discard_cards==4 then
			return "@PoxiDisCard="..table.concat(discard_cards,"+")
		end
	end
	return "."
end

sgs.ai_use_priority.PoxiCard = 3
sgs.ai_use_value.PoxiCard = 3
sgs.ai_card_intention.PoxiCard = 50

--劫营
sgs.ai_skill_playerchosen.jieyingg = function(self,targets)
	self:sort(self.enemies,"handcard")
	self.enemies = sgs.reverse(self.enemies)
	
	for _,enemy in sgs.list(self.enemies)do
		if enemy:containsTrick("indulgence") then
			return enemy
		end
	end
	local second
	for _,enemy in sgs.list(self.enemies)do
		if enemy:faceUp() and not enemy:hasSkills("tenyearliegong|tieji") and not self:needKongcheng(enemy) 
		and not (enemy:hasSkills("rende|nosrende|olrende|tenyearrende|mingjian|newmingjian|mizhao") and self:findFriendsByType(sgs.Friend_Draw,enemy)) then
			if not enemy:inMyAttackRange(self.player) or enemy:getHandcardNum()>3 or self:hasSkills(sgs.notActive_cardneed_skill,enemy) then return enemy end
			if not second then second = enemy end
		end
	end
	self:sort(self.friends_noself,"handcard")
	for _,friend in sgs.list(self.friends_noself)do
		if self:needKongcheng(friend) or friend:hasSkills("tenyearliegong|tieji|rende|nosrende|olrende|tenyearrende|mingjian|newmingjian|mizhao") then
			return friend
		end
	end
	if second then return second end
	return nil
end


--OL夺锐
sgs.ai_skill_invoke.olduorui = function(self,data)
	local player = data:toPlayer()
	if self:isFriend(player) then return false end
	local name = player:getGeneralName()
	local g = sgs.Sanguosha:getGeneral(name)
	if not g then return false end
	for _,sk in sgs.list(g:getSkillList())do
		if not sk:isVisible() then continue end
		if string.find(sgs.bad_skills,sk:objectName()) then continue end
		return true
	end
	if player:getGeneral2() then
		local name2 = player:getGeneral2Name()
		local g2 = sgs.Sanguosha:getGeneral(name2)
		if g2 then
			for _,sk in sgs.list(g2:getSkillList())do
				if not sk:isVisible() then continue end
				if string.find(sgs.bad_skills,sk:objectName()) then continue end
				return true
			end
		end
	end
	return false
end

sgs.ai_skill_choice.olduorui = function(self,choices,data)
	local player = data:toDamage().to
	local skills = choices:split("+")
	
	for _,sk in sgs.list(skills)do
		if self:isValueSkill(sk,player,true) then
			return sk
		end
	end
	
	for _,sk in sgs.list(skills)do
		if self:isValueSkill(sk,player) then
			return sk
		end
	end
	
	local not_bad_skills = {}
	for _,sk in sgs.list(skills)do
		if string.find(sgs.bad_skills,sk) then continue end
		table.insert(not_bad_skills,sk)
	end
	if #not_bad_skills>0 then
		return not_bad_skills[math.random(1,#not_bad_skills)]
	end
	
	return skills[math.random(1,#skills)]
end

--OL止啼
function getEquipAreaNum(player)
	local num = 0
	if player:hasEquipArea(0) then num = num+1 end
	if player:hasEquipArea(1) then num = num+1 end
	if player:hasEquipArea(2) then num = num+1 end
	if player:hasEquipArea(3) then num = num+1 end
	if player:hasEquipArea(4) then num = num+1 end
	return num
end

function hasOnlyEquipArea(player,area)
	if not area then return false end
	if not player:hasEquipArea(area) then return false end
	for i = 0,4 do
		if i==area then continue end
		if player:hasEquipArea(i) then return false end
	end
	return true
end

function getOnlyEquipArea(player)
	local num = -1
	for i = 0,4 do
		if hasOnlyEquipArea(player,i) then return i end
	end
	return num
end

sgs.ai_skill_playerchosen.olzhiti = function(self,targets)
	local players = {}
	for _,p in sgs.list(targets)do
		if self:isFriend(p) and hasOnlyEquipArea(p,1) and p:getEquip(1) and self:needToThrowArmor(p) then return p end
		if self:isEnemy(p) and getOnlyEquipArea(p)>-1 and p:getEquip(getOnlyEquipArea(p)) and self:doDisCard(p,"e") then
			table.insert(players,p)
		end
	end
	if #players>0 then
		self:sort(players,"defense")
		return players[1]
	end
	
	for _,p in sgs.list(targets)do
		if self:isEnemy(p) and p:hasEquipArea() and p:getEquips():isEmpty() and getOnlyEquipArea(p)>-1 then
			table.insert(players,p)
		end
	end
	if #players>0 then
		self:sort(players,"defense")
		return players[1]
	end
	
	for _,p in sgs.list(targets)do
		if self:isEnemy(p) and p:hasEquipArea() and p:getEquips():isEmpty() and p:hasSkills(sgs.lose_equip_skill) then
			table.insert(players,p)
		end
	end
	if #players>0 then
		self:sort(players,"defense")
		return players[1]
	end
	
	for _,p in sgs.list(targets)do
		if self:isEnemy(p) and p:hasEquipArea() and p:getEquips():isEmpty() then
			table.insert(players,p)
		end
	end
	if #players>0 then
		self:sort(players,"defense")
		return players[1]
	end
	
	for _,p in sgs.list(targets)do
		if self:isEnemy(p) and getEquipAreaNum(p)<=2*p:getEquips():length() and self:doDisCard(p,"e") then
			table.insert(players,p)
		end
	end
	if #players>0 then
		self:sort(players,"defense")
		return players[1]
	end
	
	for _,p in sgs.list(targets)do
		if self:isEnemy(p) and getEquipAreaNum(p)<=2*p:getEquips():length() then
			table.insert(players,p)
		end
	end
	if #players>0 then
		self:sort(players,"defense")
		return players[1]
	end
	
	return nil
end
