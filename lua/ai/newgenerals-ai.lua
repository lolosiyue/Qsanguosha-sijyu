

function SmartAI:useCardQizhengxiangsheng(card,use)
	self:sort(self.enemies,"hp")
	local extraTarget = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,card)
	if use.extra_target then extraTarget = extraTarget+use.extra_target end
	for _,ep in sgs.list(self.enemies)do
		if isCurrent(use,ep) then continue end
		if CanToCard(card,self.player,ep,use.to)
		and self:ajustDamage(self.player,ep,1,card)~=0
		then
	    	use.card = card
			use.to:append(ep)
	    	if use.to:length()>extraTarget
			then return end
		end
	end
end
sgs.ai_use_priority.Qizhengxiangsheng = 3.4
sgs.ai_keep_value.Qizhengxiangsheng = 4
sgs.ai_use_value.Qizhengxiangsheng = 3.7
sgs.ai_card_intention.Qizhengxiangsheng = 22

sgs.ai_nullification.Qizhengxiangsheng = function(self,trick,from,to,positive)
    if positive then
		return self:isFriend(to)
		and self:isWeak(to)
	else
		return self:isEnemy(to)
		and self:isWeak(to)
	end
end

sgs.ai_skill_choice._qizhengxiangsheng = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if target:getHandcardNum()<3 and math.random()>0.4
	or target:isKongcheng()
	then return items[2] end
	if math.random()<0.4
	then return items[1] end
end

sgs.ai_skill_invoke.zhiren = function(self,data)
    return true
end

sgs.ai_skill_playerchosen.zhiren = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"equip")
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

sgs.ai_skill_playerchosen.zhiren_judge = function(self,players)
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

sgs.ai_skill_invoke.yaner = function(self,data)
	local target = data:toPlayer()
	if target then
		return not self:isEnemy(target)
	end
end

sgs.ai_skill_invoke.quedi = function(self,data)
	local target = data:toPlayer()
	if target then
		return self:isEnemy(target)
	end
end

sgs.ai_skill_choice.quedi = function(self,choices,data)
	local items = choices:split("+")
	local use = data:toCardUse()
	if use.to:at(0):getHandcardNum()>1
	then return items[1] end
	if use.to:at(0):getHandcardNum()<2
	then return items[2] end
end

addAiSkills("chuifeng").getTurnUseCard = function(self)
	--添加限制
	if ((self.player:getMark("usetimeschuifeng-PlayClear") < 2)
	and (self.player:getMark("banchuifeng-Clear") == 0)) then
	--以上
		for _,cn in sgs.list(patterns())do
			local fs = dummyCard(cn)
			if fs and self.player:getKingdom()=="wei"
			and fs:isKindOf("Duel")
			and fs:isAvailable(self.player)
			and not self:isWeak() then
				fs:setSkillName("chuifeng")
				local d = self:aiUseCard(fs)
				sgs.ai_use_priority.chuifeng = sgs.ai_use_priority[fs:getClassName()]
				self.cf_to = d.to
				if d.card and d.to then
					return sgs.Card_Parse("#chuifengCard:.:"..cn) 
				end
			end	
		end
	end
end

sgs.ai_skill_use_func["#chuifengCard"] = function(card,use,self)
	use.card = card
	use.to = self.cf_to
end

sgs.ai_use_value.chuifengCard = 5.4
sgs.ai_use_priority.chuifengCard = 2.8

--[[sgs.ai_guhuo_card.chuifeng = function(self,toname,class_name)
	if (class_name=="Slash" or class_name=="Duel")
	and self.player:getKingdom()=="wei"
	and sgs.Sanguosha:getCurrentCardUseReason()==sgs.CardUseStruct_CARD_USE_REASON_RESPONSE_USE
	then return "#chuifeng:.:"..toname end
end]]


addAiSkills("chongjian").getTurnUseCard = function(self)
	local cards = self:addHandPile("he")
	cards = self:sortByKeepValue(cards,nil,true)
	local toids = {}
  	for _,c in sgs.list(cards)do
		if c:isKindOf("EquipCard")
		then table.insert(toids,c) end
	end
	for _,cn in sgs.list(patterns())do
	   	local fs = dummyCard(cn)
		if fs and self.player:getKingdom()=="wu"
		and (fs:isKindOf("Slash") or fs:isKindOf("Analeptic"))
		and #toids>0
		then
			fs:setSkillName("chuifeng")
			fs:addSubcard(toids[1])
			local d = self:aiUseCard(fs)
			if fs:isAvailable(self.player)
			and d.card and d.to
			then return fs end
		end
	end
end

sgs.ai_guhuo_card.chongjian = function(self,toname,class_name)
	local cards = self:addHandPile("he")
	cards = self:sortByKeepValue(cards,nil,true)
	local toids = {}
  	for _,c in sgs.list(cards)do
		if c:isKindOf("EquipCard")
		then table.insert(toids,c:getEffectiveId()) end
	end
	if #toids>0
	then return "#chongjianCard:"..toids[1]..":"..toname end
end


sgs.ai_skill_invoke.xiuhao = function(self,data)
	local target = data:toPlayer()
	if target then
		return not self:isEnemy(target) or self:isWeak()
	end
end

sgs.ai_skill_playerchosen.sujian = function(self,players)
	players = self:sort(players,"card",true)
    for _,target in sgs.list(players)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(players)do
		if not self:isFriend(target)
		then return target end
	end
	return players[1]
end

sgs.ai_skill_askforyiji.sujian = function(self,card_ids,tos)
    local to,id = sgs.ai_skill_askforyiji.nosyiji(self,card_ids,tos)
	if to and id then return to,id end
    for _,target in sgs.list(tos)do
		if self:isFriend(target)
		then return target,card_ids[1] end
	end
    for _,target in sgs.list(tos)do
		return target,card_ids[1]
	end
end

sgs.ai_skill_choice.sujian = function(self,choices,data)
	local items = choices:split("+")
	local ids = data:toIntList()
    local to,id = sgs.ai_skill_askforyiji.sujian(self,sgs.QList2Table(ids),self.room:getOtherPlayers(self.player))
	if to and id then
		return "give"
	end
	return "discard"
end

addAiSkills("olfuman").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getMark("olfuman_target-PlayClear")<1
		and #cards>=self.player:getMaxCards()
		then
			return sgs.Card_Parse("#olfuman:"..cards[1]:getEffectiveId()..":")
		end
	end
	for _,ep in sgs.list(self.room:getOtherPlayers(self.player))do
		if ep:getMark("olfuman_target-PlayClear")<1
		and #cards>self.player:getMaxCards()
		and not self:isEnemy(ep)
		then
			return sgs.Card_Parse("#olfuman:"..cards[1]:getEffectiveId()..":")
		end
	end
end

sgs.ai_skill_use_func["#olfuman"] = function(card,use,self)
	self:sort(self.friends_noself,"hp")
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getMark("olfuman_target-PlayClear")<1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.room:getOtherPlayers(self.player))do
		if ep:getMark("olfuman_target-PlayClear")<1
		and self.player:getHandcardNum()>self.player:getMaxCards()
		and not self:isEnemy(ep)
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.olfuman = 9.4
sgs.ai_use_priority.olfuman = 4.8

sgs.ai_skill_playerchosen.xianwei = function(self,players)
	players = self:sort(players,"hp")
    for _,target in sgs.list(players)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(players)do
		if not self:isEnemy(target)
		then return target end
	end
	return players[1]
end

sgs.ai_skill_discard.zhiming = function(self)
	local cards = {}
	local js = self.player:getCards("j")
	if js:length()>0
	and self.player:getPhase()==sgs.Player_Start then
		local handcards = self.player:getCards("he")
		handcards = self:sortByKeepValue(handcards) -- 按保留值排序
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
				then table.insert(cards,h:getEffectiveId()) end
				if #cards>0 then return cards end
			end
		end
	end
	js = self:poisonCards("he")
	if self.player:getPhase()~=sgs.Player_Start and #js>0 then
		table.insert(cards,js[1]:getEffectiveId())
	end
	return cards
end

sgs.ai_skill_cardask["@zhiming-put"] = function(self,data,pattern)
	local target = self.room:getCurrent()
    local cards = sgs.ai_skill_discard.zhiming(self)
    return #cards>0 and cards[1] or "."
end

sgs.ai_skill_cardask["@yuyanzy-give"] = function(self,data,pattern)
	local use = data:toCardUse()
    return self:isEnemy(use.to:at(0))
end

sgs.ai_skill_playerchosen.xbwuxinglianzhu = function(self,players)
	players = self:sort(players,"handcard",true)
    for _,target in sgs.list(players)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(players)do
		if not self:isEnemy(target)
		then return target end
	end
	return players[1]
end

sgs.ai_skill_playerchosen.xbfukuangdongzhu = function(self,players)
	players = self:sort(players,"hp")
    for _,target in sgs.list(players)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(players)do
		if not self:isEnemy(target)
		then return target end
	end
	return players[1]
end

sgs.ai_skill_playerchosen.xbyinghuoshouxin = function(self,players)
	players = self:sort(players,"handcard",true)
    for _,target in sgs.list(players)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(players)do
		if not self:isFriend(target)
		then return target end
	end
	return players[1]
end

sgs.ai_skill_invoke.olhaoshi = function(self,data)
	local al = self.player:getAliveSiblings()
	local n,can = 998,false
    for _,target in sgs.list(al)do
		n = math.min(n,target:getHandcardNum())
	end
    for _,target in sgs.list(al)do
		if target:getHandcardNum()<=n
		and not self:isEnemy(target)
		then can = true end
	end
    return self.player:getHandcardNum()+2<=5 or can
end

sgs.ai_skill_cardask["@olhaoshi-give"] = function(self,data,pattern,to)
	local use = data:toCardUse()
	if self:isFriend(to)
	and self:isWeak(to)
	then
		local cs
		if use.card:isKindOf("Slash")
		or use.card:isKindOf("ArcheryAttack")
		then cs = self:getCards("Jink") end
		if use.card:isKindOf("Duel")
		or use.card:isKindOf("SavageAssault")
		then cs = self:getCards("Slash") end
		if cs then return cs[1]:getEffectiveId() end
	end
	return "."
end

addAiSkills("oldimeng").getTurnUseCard = function(self)
	return sgs.Card_Parse("#oldimeng:.:")
end

sgs.ai_skill_use_func["#oldimeng"] = function(card,use,self)
	self:sort(self.enemies,"handcard",true)
	self:sort(self.friends_noself,"handcard")
	for _,ep in sgs.list(self.enemies)do
		for _,fp in sgs.list(self.friends_noself)do
			if ep:getHandcardNum()>fp:getHandcardNum()
			then
				use.card = card
				use.to:append(ep)
				use.to:append(fp)
				return
			end
		end
	end
end

sgs.ai_use_value.oldimeng = 9.4
sgs.ai_use_priority.oldimeng = 5.8

sgs.ai_skill_playerchosen.secondwenji = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card",true)
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
	self:sort(destlist,"card")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	self:sort(destlist,"card",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
	return destlist[1]
end

sgs.ai_skill_playerchosen.secondkangge = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card",true)
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
	self:sort(destlist,"card",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
	self:sort(destlist,"card")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	return destlist[1]
end

sgs.ai_skill_playerchosen.fengjie = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp",true)
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
	return destlist[1]
end

sgs.ai_skill_invoke.secondkangge = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isEnemy(target)
	end
end

sgs.ai_skill_invoke.secondjielie = function(self,data)
    return true
end

sgs.ai_skill_invoke.yise = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return self:isFriend(target)
	end
end

sgs.ai_skill_askforyiji.shunshi = function(self,card_ids)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	local tos = self.room:getOtherPlayers(self.player)
	tos = self:sort(tos,"hp")
	for _,p in sgs.list(tos)do
	   	if p:hasFlag("shunshi")
		then
			if cards[1]:isRed() and self:isFriend(p)
			or cards[1]:isBlack() and self:isEnemy(p)
			then return p,cards[1]:getEffectiveId() end
		end
	end
end

sgs.ai_skill_cardask["@fengzi-discard"] = function(self,data)
    local use = data:toCardUse()
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if h:getTypeId()==use.card:getTypeId()
    	then
			if use.card:isKindOf("Peach")
			and self.player:getLostHp()>1
			then return h:getEffectiveId()
			elseif use.card:isKindOf("Analeptic")
			and not(h:isKindOf("Slash") and table.contains(self.toUse,h))
			then return h:getEffectiveId()
			elseif use.to:contains(self.player)
			then return h:getEffectiveId()
			elseif use.card:isDamageCard()
			or use.card:isKindOf("SingleTargetTrick")
			then
				for _,to in sgs.list(use.to)do
					if self:isFriend(to)
					then return "." end
				end
				return h:getEffectiveId()
			end
		end
	end
    return "."
end

sgs.ai_skill_invoke.jizhanw = function(self,data)
    return true
end

sgs.ai_skill_choice.jizhanw = function(self,choices,data)
	local n = data:toInt()
	local items = choices:split("+")
	if n>6 then return items[2] end
	if n<7 then return items[1] end
	return items[2]
end


sgs.ai_skill_playerchosen.fusong = function(self,players)
	players = self:sort(players,"card",true)
    for _,target in sgs.list(players)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(players)do
		if not self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_invoke.qingjue = function(self,data)
	local use = data:toCardUse()
	if self:isFriend(use.to:at(0))
	then
		if self:isWeak(use.to:at(0))
		and use.card:isDamageCard()
		then return true
		else
			local cards = self.player:getCards("h")
			cards = sgs.QList2Table(cards) -- 将列表转换为表
			self:sortByKeepValue(cards) -- 按保留值排序
			for _,h in sgs.list(cards)do
				if h:getNumber()>9
				then return true end
			end
		end
	end
end

sgs.ai_skill_playerchosen.zhibian = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card")
    for _,target in sgs.list(destlist)do
		local cards = self.player:getCards("h")
		cards = sgs.QList2Table(cards) -- 将列表转换为表
		self:sortByKeepValue(cards) -- 按保留值排序
		for _,h in sgs.list(cards)do
			if h:getNumber()>9
			and self:isEnemy(target)
			then return target end
		end
	end
end

sgs.ai_useto_revises.yuyan = function(self,card,use,p)
	if card:isKindOf("Slash")
	and not card:isVirtualCard()
	then
		sgs.ai_skill_defense.yuyan = 0
		local xc = self:getMaxCard(nil,self:getCards("he"))
		if xc and xc:getNumber()<=card:getNumber()
		then sgs.ai_skill_defense.yuyan = 4 end
	end
end

sgs.ai_skill_invoke.yilie = function(self,data)
    return true
end

addAiSkills("mobilefenming").getTurnUseCard = function(self)
	return sgs.Card_Parse("#mobilefenming:.:")
end

sgs.ai_skill_use_func["#mobilefenming"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	for _,ep in sgs.list(self.enemies)do
		if ep:isChained() and ep:getCardCount()>1
		and ep:getHp()<=self.player:getHp()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:getHp()>self.player:getHp() then continue end
		use.card = card
		use.to:append(ep)
		return
	end
end

sgs.ai_use_value.mobilefenming = 3.4
sgs.ai_use_priority.mobilefenming = 4.8

addAiSkills("jinghe").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("h"))
    if #cards<1 then return end
	self:sortByKeepValue(cards)
	local toids,cs = {},{}
  	for _,c in sgs.list(cards)do
		if #toids>=#self.friends
		or cs[c:objectName()]
		then continue end
		cs[c:objectName()] = true
		table.insert(toids,c:getEffectiveId())
	end
	self.jh_n = #toids
	return #toids>0 and sgs.Card_Parse("#jinghe:"..table.concat(toids,"+")..":")
end

sgs.ai_skill_use_func["#jinghe"] = function(card,use,self)
	self:sort(self.friends,"hp",true)
	for _,ep in sgs.list(self.friends)do
		use.card = card
		if use.to:length()<self.jh_n
		then use.to:append(ep) end
	end
end

sgs.ai_use_value.jinghe = 9.4
sgs.ai_use_priority.jinghe = 6.8

sgs.ai_skill_invoke.gongxiu = function(self,data)
    return true
end


sgs.ai_skill_choice.gongxiu = function(self,choices)
	local items = choices:split("+")
	local draw,discard = 0,0
	for _,p in sgs.list(self.room:getAllPlayers())do
		if p:getMark("jinghe_GetSkill-Clear")>0
		then draw = draw+1
		elseif not p:isKongcheng()
		then discard = discard+1 end
	end
	if draw<discard
	then return "discard"
	else return "draw" end
end

addAiSkills("nhhuoqi").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,true)
	if #cards>0
	then
		return sgs.Card_Parse("#nhhuoqi:"..cards[1]:getEffectiveId()..":")
	end
end

sgs.ai_skill_use_func["#nhhuoqi"] = function(card,use,self)
	self:sort(self.friends,"hp")
	local tos = self.player:getAliveSiblings()
	tos = self:sort(tos,"hp")
	for _,ep in sgs.list(self.friends)do
		if ep:getHp()<=tos[1]:getHp()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.nhhuoqi = 9.4
sgs.ai_use_priority.nhhuoqi = 4.8

sgs.ai_skill_invoke.nhguizhu = function(self,data)
    return true
end

addAiSkills("nhxianshou").getTurnUseCard = function(self)
	return sgs.Card_Parse("#nhxianshou:.:")
end

sgs.ai_skill_use_func["#nhxianshou"] = function(card,use,self)
	self:sort(self.friends,"hp")
	for _,ep in sgs.list(self.friends)do
		if not ep:isWounded()
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
end

sgs.ai_use_value.nhxianshou = 9.4
sgs.ai_use_priority.nhxianshou = 3.8

sgs.ai_skill_invoke.yise = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isFriend(target) and target:getHandcardNum()>self.player:getHandcardNum()
		or target:getHandcardNum()<self.player:getHandcardNum()
	end
end

sgs.ai_skill_invoke.nhguanyue = function(self,data)
    return true
end

sgs.ai_skill_cardask["@nhyanzheng-keep"] = function(self,data)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards,true) -- 按保留值排序
	local n = #cards-1
	if n>#self.enemies/2
	and n<#self.enemies*2
	then
		return cards[1]:getEffectiveId()
	end
    return "."
end

sgs.ai_skill_use["@@nhyanzheng"] = function(self,prompt)
	local valid = {}
	local destlist = self.player:getAliveSiblings()
    destlist = self:sort(destlist,"hp")
	local n = self.player:getMark("nhyanzheng-PlayClear")
	for _,friend in sgs.list(destlist)do
		if #valid>=n then break end
		if self:isEnemy(friend)
		then table.insert(valid,friend:objectName()) end
	end
	for _,friend in sgs.list(destlist)do
		if #valid>=n then break end
		if not self:isFriend(friend)
		and not table.contains(valid,friend:objectName())
		then table.insert(valid,friend:objectName()) end
	end
	if #valid>0
	then
    	return string.format("#nhyanzheng:.:->%s",table.concat(valid,"+"))
	end
end

addAiSkills("nosjinwanyi").getTurnUseCard = function(self)
	local cards = self:addHandPile()
	cards = self:sortByKeepValue(cards,nil,true)
  	for _,c in sgs.list(cards)do
		local eg = sgs.Sanguosha:getEngineCard(c:getEffectiveId())
		if eg:property("YingBianEffects"):toString()=="" then continue end
		for d,cn in sgs.list({"zhujinqiyuan","chuqibuyi","drowning","dongzhuxianji"})do
			local tc = dummyCard(cn)
			if not tc or c:objectName()==cn then continue end
			tc:setSkillName("nosjinwanyi")
			tc:addSubcard(c)
			d = self:aiUseCard(tc)
			sgs.ai_use_priority.nosjinwanyi = sgs.ai_use_priority[tc:getClassName()]
			self.wy_to = d.to
			if d.card and d.to
			and tc:isAvailable(self.player)
			then return sgs.Card_Parse("#nosjinwanyi:"..c:getEffectiveId()..":"..cn) end
		end
	end
end

sgs.ai_skill_use_func["#nosjinwanyi"] = function(card,use,self)
	if self.wy_to
	then
		use.card = card
		use.to = self.wy_to
	end
end

sgs.ai_use_value.nosjinwanyi = 9.4
sgs.ai_use_priority.nosjinwanyi = 7.8


sgs.ai_skill_playerchosen.nosjinxuanbei = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
end



sgs.ai_skill_invoke.jinzhaosong = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isEnemy(target)
	end
end

sgs.ai_skill_invoke.jinzhaosong_lei = function(self,data)
    return true
end

sgs.ai_skill_playerchosen.jinzhaosong = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and self:doDisCard(target,"ej")
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and target:getCardCount()>0
		then return target end
	end
end

sgs.ai_skill_invoke.jinzhaosong_fu = function(self,data)
	local target = self.player:getTag("JinZhaosongDrawer"):toPlayer()
	if target then return not self:isEnemy(target) end
end

sgs.ai_skill_use["@@jinzhaosong"] = function(self,prompt)
	local valid = {}
	local destlist = self.player:getAliveSiblings()
    destlist = self:sort(destlist,"hp")
	for _,friend in sgs.list(destlist)do
		if #valid>1 then break end
		if self:isEnemy(friend)
		and friend:hasFlag("jinzhaosong_can_choose")
		then table.insert(valid,friend:objectName()) end
	end
	for _,friend in sgs.list(destlist)do
		if #valid>1 then break end
		if not self:isFriend(friend)
		and friend:hasFlag("jinzhaosong_can_choose")
		and not table.contains(valid,friend:objectName())
		then table.insert(valid,friend:objectName()) end
	end
	if #valid>0
	then
    	return string.format("#jinzhaosong:.:->%s",table.concat(valid,"+"))
	end
end


sgs.ai_skill_playerchosen.jinlisi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
end


addAiSkills("zuoxing").getTurnUseCard = function(self)
	for _,name in sgs.list(patterns())do
		local poi = dummyCard(name)
		if poi==nil then continue end
		poi:setSkillName("zuoxing")
		if poi:isAvailable(self.player)
		and poi:isNDTrick() and poi:isDamageCard()
		then
			local dummy = self:aiUseCard(poi)
			if dummy.card and dummy.to
			then
				self.zx_to = dummy.to
				sgs.ai_use_priority.zuoxing = sgs.ai_use_priority[poi:getClassName()]
				if poi:canRecast() and dummy.to:length()<1 then continue end
				return sgs.Card_Parse("#zuoxing:.:"..name)
			end
		end
	end
	for _,name in sgs.list(patterns())do
		local poi = dummyCard(name)
		if poi==nil then continue end
		poi:setSkillName("zuoxing")
		if poi:isAvailable(self.player)
		and poi:isNDTrick()
		and name~="amazing_grace"
		and name~="collateral"
		then
			local dummy = self:aiUseCard(poi)
			if dummy.card and dummy.to
			then
				self.zx_to = dummy.to
				sgs.ai_use_priority.zuoxing = sgs.ai_use_priority[poi:getClassName()]
				if poi:canRecast() and dummy.to:length()<1 then continue end
				return sgs.Card_Parse("#zuoxing:.:"..name)
			end
		end
	end
end

sgs.ai_skill_use_func["#zuoxing"] = function(card,use,self)
	use.card = card
	use.to = self.zx_to
end

sgs.ai_use_value.zuoxing = 8.4
sgs.ai_use_priority.zuoxing = 8.4

addAiSkills("huishi").getTurnUseCard = function(self)
    return sgs.Card_Parse("#huishiCard:.:")
end

sgs.ai_skill_use_func["#huishiCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.huishiCard = 8.4
sgs.ai_use_priority.huishiCard = 8.4

sgs.ai_skill_invoke.huishi = function(self,data)
    return true
end

sgs.ai_skill_playerchosen.huishi = function(self,players)
	players = self:sort(players,"handcard")
    for _,target in sgs.list(players)do
    	if self:isFriend(target)
		then
            return target
		end
	end
end

sgs.ai_skill_playerchosen.godtianyi = function(self,players)
	local destlist = players
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
	self:sort(destlist,"maxhp",true)
    for _,target in sgs.list(destlist)do
    	if self:isFriend(target)
		then
            return target
		end
	end
    return destlist[1]
end

sgs.ai_skill_playerchosen.zuoxing = function(self,players)
	local destlist = players
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
	self:sort(destlist,"maxhp")
    for _,target in sgs.list(destlist)do
    	if self:isEnemy(target)
		then
            return target
		end
	end
    return destlist[1]
end

addAiSkills("huishii").getTurnUseCard = function(self)
    return sgs.Card_Parse("#huishii:.:")
end

sgs.ai_skill_use_func["#huishii"] = function(card,use,self)
	self:sort(self.friends,"hp")
	for _,ep in sgs.list(self.friends)do
		local skills = {}
		for _,sk in sgs.list(ep:getVisibleSkillList())do
			if sk:getFrequency(ep)~=sgs.Skill_Wake
			or ep:getMark(sk:objectName())>0
			then continue end
			table.insert(skills,sk:objectName())
		end
		if #skills>0
		and self.player:getMaxHp()>2
		and self.player:getLostHp()>1
		and self.player:getMaxHp()>=self.room:alivePlayerCount()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.friends)do
		if self:isWeak(ep) and self.player:getMaxHp()>2
		and self.player:getLostHp()>1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.huishii = 8.4
sgs.ai_use_priority.huishii = 8.5

sgs.ai_skill_use["@@dangmo"] = function(self,prompt)
	local valid = {}
	local destlist = self.player:getAliveSiblings()
    destlist = self:sort(destlist,"hp")
	local n = self.player:getHp()-1
	for _,friend in sgs.list(destlist)do
		if #valid>=n then break end
		if self:isEnemy(friend)
		and friend:hasFlag("dangmo")
		then table.insert(valid,friend:objectName()) end
	end
	for _,friend in sgs.list(destlist)do
		if #valid>=n then break end
		if not self:isFriend(friend)
		and friend:hasFlag("dangmo")
		and not table.contains(valid,friend:objectName())
		then table.insert(valid,friend:objectName()) end
	end
	if #valid>0
	then
    	return string.format("#dangmo:.:->%s",table.concat(valid,"+"))
	end
end

addAiSkills("yingba").getTurnUseCard = function(self)
   	if self.player:getMaxHp()>2
   	then
        return sgs.Card_Parse("#yingba:.:")
	end
end

sgs.ai_skill_use_func["#yingba"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	for _,ep in sgs.list(self.enemies)do
		if ep:isWounded()
		or ep:getMaxHp()<2
		then continue end
		use.card = card
		use.to:append(ep)
		return
	end
	self:sort(self.enemies,"hp",true)
	for _,ep in sgs.list(self.enemies)do
		if ep:getMaxHp()<2
		then continue end
		use.card = card
		use.to:append(ep)
		return
	end
end

sgs.ai_use_value.yingba = 8.4
sgs.ai_use_priority.yingba = 7.4

sgs.ai_skill_askforyiji.pinghe = function(self,card_ids)
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
	   	if self:isFriend(p) then return p,cards[1]:getEffectiveId() end
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
	   	if self:isEnemy(p) then continue end
		return p,cards[1]:getEffectiveId()
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		return p,cards[1]:getEffectiveId()
	end
end

sgs.ai_use_revises.pinghe = function(self,card)
	if card:isKindOf("Peach")
	then return false end
end

sgs.ai_skill_choice.dinghan = function(self,choices)
	local items = choices:split("+")
	local cns = self.player:property("SkillDescriptionRecord_dinghan"):toString():split("+")
	if table.contains(items,"remove")
	then
		for c,pn in sgs.list(cns)do
			c = dummyCard(pn)
			if c
			then
				self.ai_dinghan_choice = c:objectName()
				if c:isDamageCard() and self:isWeak()
				and self:getRestCardsNum(c:getClassName())>0
				then return "remove" end
				if c:isKindOf("DelayedTrick")
				and self:getRestCardsNum(c:getClassName())>0
				then return "remove" end
			end
		end
	end
	if table.contains(items,"add")
	then
		for d,c in sgs.list(self.player:getHandcards())do
			if table.contains(cns,c:objectName())
			or c:isZhinangCard() then continue end
			if c:targetFixed() and not c:isDamageCard()
			then
				self.ai_dinghan_choice = c:objectName()
				d = self:aiUseCard(c)
				if d.card and d.to
				then return "add" end
			end
		end
		for _,pn in sgs.list(patterns())do
			if table.contains(cns,pn) then continue end
			local c = dummyCard(pn)
			if c then
				if table.contains(sgs.ZhinangClassName,c:getClassName())
				or c:isZhinangCard() then continue end
				self.ai_dinghan_choice = pn
				if c:targetFixed() and not c:isDamageCard()
				and self:getRestCardsNum(c:getClassName())>0
				then return "add" end
			end
		end
		for _,pn in sgs.list(patterns())do
			if table.contains(cns,pn) then continue end
			local c = dummyCard(pn)
			if c then
				if table.contains(sgs.ZhinangClassName,c:getClassName())
				or c:isZhinangCard() then continue end
				self.ai_dinghan_choice = pn
				if c:isKindOf("GlobalEffect") and not c:isDamageCard()
				and self:getRestCardsNum(c:getClassName())>0
				then return "add" end
			end
		end
	end
	return "cancel"
end

sgs.ai_skill_askforag.dinghan = function(self,card_ids)
    for c,id in sgs.list(card_ids)do
        c = sgs.Sanguosha:getCard(id)
		if c:objectName()==self.ai_dinghan_choice
	    then return id end
    end
    for c,id in sgs.list(card_ids)do
        c = sgs.Sanguosha:getCard(id)
		if c:isDamageCard() and self:isWeak()
		and self:getRestCardsNum(c:getClassName())>0
	    then return id end
    end
    for c,id in sgs.list(card_ids)do
        c = sgs.Sanguosha:getCard(id)
		if c:targetFixed() and not c:isDamageCard()
		and self:getRestCardsNum(c:getClassName())>0
	    then return id end
    end
    for c,id in sgs.list(card_ids)do
        c = sgs.Sanguosha:getCard(id)
		if c:isKindOf("GlobalEffect") and not c:isDamageCard()
		and self:getRestCardsNum(c:getClassName())>0
	    then return id end
    end
end

sgs.ai_target_revises.tianzuo = function(to,card,self)
    if card:isKindOf("Qizhengxiangsheng")
	and not self:isFriend(to)
	then return true end
end

sgs.ai_target_revises.dinghan = function(to,card,self)
    if card:getTypeId()==2
	then
		local cns = to:property("SkillDescriptionRecord_dinghan"):toString():split("+")
		if table.contains(cns,card:objectName())
		then return end
		return true
	end
end

sgs.ai_skill_playerchosen.kemobileyijin = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
    	if self:isFriend(target)
		then
            if self.player:getMark("@keyijin_houren")>0 and self:isWeak(target) and target:isWounded()
			then 
				sgs.ai_skill_choice.kemobileyijin = "keyijin_houren"
				return target
			end
            if self.player:getMark("@keyijin_tongshen")>0 and self:isWeak(target)
			then 
				sgs.ai_skill_choice.kemobileyijin = "keyijin_tongshen"
				return target
			end
            if self.player:getMark("@keyijin_wushi")>0 and not self:isWeak(target)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_wushi"
				return target
			end
            if self.player:getMark("@keyijin_jinmi")>0 and target:isSkipped(sgs.Player_Play)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_jinmi"
				return target
			end
		end
	end
    for _,target in sgs.list(destlist)do
    	if self:isEnemy(target)
		then
            if self.player:getMark("@keyijin_guxiong")>0 and self:isWeak(target)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_guxiong"
				return target
			end
            if self.player:getMark("@keyijin_yongbi")>0 and target:getHandcardNum()<4
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_yongbi"
				return target
			end
            if self.player:getMark("@keyijin_jinmi")>0 and self:getOverflow()~=0
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_jinmi"
				return target
			end
		end
	end
    for _,target in sgs.list(destlist)do
    	if not self:isFriend(target)
		then
            if self.player:getMark("@keyijin_guxiong")>0 and self:isWeak(target)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_guxiong"
				return target
			end
            if self.player:getMark("@keyijin_yongbi")>0 and target:getHandcardNum()<4
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_yongbi"
				return target
			end
            if self.player:getMark("@keyijin_jinmi")>0 and self:getOverflow()~=0
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_jinmi"
				return target
			end
		end
	end
    for _,target in sgs.list(destlist)do
    	if not self:isEnemy(target)
		then
            if self.player:getMark("@keyijin_houren")>0 and self:isWeak(target) and target:isWounded()
			then 
				sgs.ai_skill_choice.kemobileyijin = "keyijin_houren"
				return target
			end
            if self.player:getMark("@keyijin_tongshen")>0 and self:isWeak(target)
			then 
				sgs.ai_skill_choice.kemobileyijin = "keyijin_tongshen"
				return target
			end
            if self.player:getMark("@keyijin_wushi")>0 and not self:isWeak(target)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_wushi"
				return target
			end
            if self.player:getMark("@keyijin_jinmi")>0 and target:isSkipped(sgs.Player_Play)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_jinmi"
				return target
			end
		end
	end
end


--周不疑

local kehuiyao_skill = {}
kehuiyao_skill.name="kehuiyao"
table.insert(sgs.ai_skills,kehuiyao_skill)
kehuiyao_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("#kehuiyaoCard:.:")
end

sgs.ai_skill_use_func["#kehuiyaoCard"] = function(card,use,self)
	if not self:isWeak() then
		self:sort(self.enemies)
		for _, ep in ipairs(self.enemies) do
			local can = true
			for _,sk in ipairs(sgs.getPlayerSkillList(ep)) do
				local ts = sgs.Sanguosha:getTriggerSkill(sk:objectName())
				if ts and ts:hasEvent(sgs.Damage) then
					can = false
					break
				end
			end
			if can then
				for _, fp in ipairs(self.friends_noself) do
					for _,sk in ipairs(sgs.getPlayerSkillList(fp)) do
						local ts = sgs.Sanguosha:getTriggerSkill(sk:objectName())
						if ts and ts:hasEvent(sgs.Damaged) then
							use.card = card
							use.to:append(ep)
							use.to:append(fp)
							return
						end
					end
				end
			end
		end
		for _, fp in ipairs(self.friends_noself) do
			for _,sk in ipairs(sgs.getPlayerSkillList(fp)) do
				local ts = sgs.Sanguosha:getTriggerSkill(sk:objectName())
				if ts and ts:hasEvent(sgs.Damage) then
					for _, ep in ipairs(self.enemies) do
						local can = true
						for _,sk in ipairs(sgs.getPlayerSkillList(ep)) do
							local ts = sgs.Sanguosha:getTriggerSkill(sk:objectName())
							if ts and ts:hasEvent(sgs.Damaged) then
								can = false
								break
							end
						end
						if can then
							use.card = card
							use.to:append(fp)
							use.to:append(ep)
							return
						end
					end
				end
			end
		end
		for _, fp in ipairs(self.friends_noself) do
			for _,sk in ipairs(sgs.getPlayerSkillList(fp)) do
				local ts = sgs.Sanguosha:getTriggerSkill(sk:objectName())
				if ts and ts:hasEvent(sgs.Damage) then
					for _, ep in ipairs(self.enemies) do
						use.card = card
						use.to:append(fp)
						use.to:append(ep)
						return
					end
				end
			end
		end
		for _, ep in ipairs(self.enemies) do
			for _, fp in ipairs(self.friends_noself) do
				for _,sk in ipairs(sgs.getPlayerSkillList(fp)) do
					local ts = sgs.Sanguosha:getTriggerSkill(sk:objectName())
					if ts and ts:hasEvent(sgs.Damaged) then
						use.card = card
						use.to:append(ep)
						use.to:append(fp)
						return
					end
				end
			end
			break
		end
		if self.player:hasSkill("kequesong") and self.player:getMark("&kequesong-Clear")<1 then
			self:sort(self.friends_noself,nil,true)
			for _, fp in ipairs(self.friends_noself) do
				for _, ep in ipairs(self.enemies) do
					use.card = card
					use.to:append(fp)
					use.to:append(ep)
					return
				end
			end
			for _, fp in sgs.qlist(self.room:getOtherPlayers(self.player)) do
				for _, ep in ipairs(self.enemies) do
					use.card = card
					use.to:append(fp)
					use.to:append(ep)
					return
				end
			end
			for _, fp in sgs.qlist(self.room:getOtherPlayers(self.player)) do
				for _, ep in sgs.qlist(self.room:getOtherPlayers(fp)) do
					if ep==self.player then continue end
					use.card = card
					use.to:append(fp)
					use.to:append(ep)
					return
				end
			end
		end
	end
end

sgs.ai_skill_playerchosen.kequesong = function(self, targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets)
	for _, p in ipairs(targets) do
		if self:isFriend(p) then
			return p
		end
	end
	return nil
end

sgs.ai_skill_choice.kequesong = function(self,choices,data)
	local items = choices:split("+")
	return self.player:isWounded() and (self:isWeak() or self.player:getHandcardNum()>self.player:getHp()) and items[2]
	or items[1]
end

sgs.ai_skill_playerchosen.kehuiyaoone = function(self, targets)
	targets = sgs.QList2Table(targets)
	local theweak = sgs.SPlayerList()
	local theweaktwo = sgs.SPlayerList()
	for _, p in ipairs(targets) do
		if self:isEnemy(p) then
			theweak:append(p)
		end
	end
	for _,qq in sgs.qlist(theweak) do
		if theweaktwo:isEmpty() then
			theweaktwo:append(qq)
		else
			local inin = 1
			for _,pp in sgs.qlist(theweaktwo) do
				if (pp:getHp() < qq:getHp()) then
					inin = 0
				end
			end
			if (inin == 1) then
				theweaktwo:append(qq)
			end
		end
	end
	if theweaktwo:length() > 0 then
	    return theweaktwo:at(0)
	end
	return nil
end

sgs.ai_skill_playerchosen.kehuiyaotwo = function(self, targets)
	targets = sgs.QList2Table(targets)
	local theweak = sgs.SPlayerList()
	local theweaktwo = sgs.SPlayerList()
	for _, p in ipairs(targets) do
		if self:isFriend(p) then
			theweak:append(p)
		end
	end
	for _,qq in sgs.qlist(theweak) do
		if theweaktwo:isEmpty() then
			theweaktwo:append(qq)
		else
			local inin = 1
			for _,pp in sgs.qlist(theweaktwo) do
				if (pp:getHp() < qq:getHp()) then
					inin = 0
				end
			end
			if (inin == 1) then
				theweaktwo:append(qq)
			end
		end
	end
	if theweaktwo:length() > 0 then
	    return theweaktwo:at(0)
	end
	return nil
end

--ol周处

sgs.ai_skill_choice.keolshanduanmpjd = function(self, choices, data)
	return "shanduanfour"
end

sgs.ai_skill_choice.keolshanduancpgjfw = function(self, choices, data)
	local two = 1
	for _,p in sgs.qlist(self.player:getAliveSiblings()) do
		if self:isEnemy(p) and self.player:inMyAttackRange(p) then
			two = 0
		end
		break
	end	
	if two == 1 then
	    return "shanduantwo"
	else
		return "shanduanone"
	end
end

sgs.ai_skill_choice.keolshanduancpslash = function(self, choices, data)
	local items = choices:split("+")
	return items[#items]
end

sgs.ai_skill_discard.keolhuanfu = function(self, discard_num, min_num, optional, include_equip) 
	local to_discard = {}
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	if self.player:hasFlag("wantusehuanfu") then
	    table.insert(to_discard, cards[1]:getEffectiveId())
		return to_discard
	else
	    return self:askForDiscard("dummyreason", discard_num, min_num, true, true)
	end
end

local keolqingyi_skill = {}
keolqingyi_skill.name = "keolqingyi"
table.insert(sgs.ai_skills, keolqingyi_skill)
keolqingyi_skill.getTurnUseCard = function(self)
	if self.player:hasUsed("keolqingyiCard") then return end
	return sgs.Card_Parse("#keolqingyiCard:.:")
end

sgs.ai_skill_use_func["#keolqingyiCard"] = function(card, use, self)
    if not self.player:hasUsed("#keolqingyiCard") then
		local room = self.room
		local all = room:getOtherPlayers(self.player)
		local enys = sgs.SPlayerList()
		for _, p in sgs.qlist(all) do
			if self:isEnemy(p) and (enys:length() < 2) then
				enys:append(p)
			end
		end
		if (enys:length() > 0) then
			for _, p in sgs.qlist(enys) do
				use.card = card
				use.to:append(p)
			end
		end	
		return
	end
end

sgs.ai_use_value.keolqingyiCard = 8.5
sgs.ai_use_priority.keolqingyiCard = 9.5
sgs.ai_card_intention.keolqingyiCard = 80


sgs.ai_skill_playerchosen.keolzeyue = function(self, targets)
	targets = sgs.QList2Table(targets)
	local theweak = sgs.SPlayerList()
	local theweaktwo = sgs.SPlayerList()
	for _, p in ipairs(targets) do
		if self:isEnemy(p) then
			theweak:append(p)
		end
	end
	for _,qq in sgs.qlist(theweak) do
		if theweaktwo:isEmpty() then
			theweaktwo:append(qq)
		else
			local inin = 1
			for _,pp in sgs.qlist(theweaktwo) do
				if (pp:getHp() < qq:getHp()) then
					inin = 0
				end
			end
			if (inin == 1) then
				theweaktwo:append(qq)
			end
		end
	end
	if theweaktwo:length() > 0 then
	    return theweaktwo:at(0)
	end
	return nil
end

--曹嵩

sgs.ai_skill_playerchosen.kemobileyijin = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
    	if self:isFriend(target)
		then
            if self.player:getMark("@keyijin_houren")>0 and self:isWeak(target) and target:isWounded()
			then 
				sgs.ai_skill_choice.kemobileyijin = "keyijin_houren"
				return target
			end
            if self.player:getMark("@keyijin_tongshen")>0 and self:isWeak(target)
			then 
				sgs.ai_skill_choice.kemobileyijin = "keyijin_tongshen"
				return target
			end
            if self.player:getMark("@keyijin_wushi")>0 and not self:isWeak(target)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_wushi"
				return target
			end
            if self.player:getMark("@keyijin_jinmi")>0 and target:isSkipped(sgs.Player_Play)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_jinmi"
				return target
			end
		end
	end
    for _,target in sgs.list(destlist)do
    	if self:isEnemy(target)
		then
            if self.player:getMark("@keyijin_guxiong")>0 and self:isWeak(target)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_guxiong"
				return target
			end
            if self.player:getMark("@keyijin_yongbi")>0 and target:getHandcardNum()<4
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_yongbi"
				return target
			end
            if self.player:getMark("@keyijin_jinmi")>0 and self:getOverflow()~=0
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_jinmi"
				return target
			end
		end
	end
    for _,target in sgs.list(destlist)do
    	if not self:isFriend(target)
		then
            if self.player:getMark("@keyijin_guxiong")>0 and self:isWeak(target)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_guxiong"
				return target
			end
            if self.player:getMark("@keyijin_yongbi")>0 and target:getHandcardNum()<4
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_yongbi"
				return target
			end
            if self.player:getMark("@keyijin_jinmi")>0 and self:getOverflow()~=0
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_jinmi"
				return target
			end
		end
	end
    for _,target in sgs.list(destlist)do
    	if not self:isEnemy(target)
		then
            if self.player:getMark("@keyijin_houren")>0 and self:isWeak(target) and target:isWounded()
			then 
				sgs.ai_skill_choice.kemobileyijin = "keyijin_houren"
				return target
			end
            if self.player:getMark("@keyijin_tongshen")>0 and self:isWeak(target)
			then 
				sgs.ai_skill_choice.kemobileyijin = "keyijin_tongshen"
				return target
			end
            if self.player:getMark("@keyijin_wushi")>0 and not self:isWeak(target)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_wushi"
				return target
			end
            if self.player:getMark("@keyijin_jinmi")>0 and target:isSkipped(sgs.Player_Play)
			then
				sgs.ai_skill_choice.kemobileyijin = "keyijin_jinmi"
				return target
			end
		end
	end
end

local kemobileguanzong={}
kemobileguanzong.name="kemobileguanzong"
table.insert(sgs.ai_skills,kemobileguanzong)
kemobileguanzong.getTurnUseCard = function(self)
	return sgs.Card_Parse("#kemobileguanzongCard:.:")
end

sgs.ai_skill_use_func["#kemobileguanzongCard"] = function(card,use,self)
	self.gz_to = nil
	for _,fp in sgs.list(self.friends_noself)do
		for _,sk in sgs.list(aiConnect(fp))do
			local tsk = sgs.Sanguosha:getTriggerSkill(sk)
			if tsk and tsk:hasEvent(sgs.Damage)
			then
				use.card = card
				use.to:append(fp)
				return
			end
		end
	end
	for _,ep in sgs.list(self.enemies)do
		local can = true
		for _,sk in sgs.list(aiConnect(ep))do
			local tsk = sgs.Sanguosha:getTriggerSkill(sk)
			if tsk and tsk:hasEvent(sgs.Damage)
			then can = false break end
		end
		if can==false then break end
		for _,fp in sgs.list(self.friends_noself)do
			for _,sk in sgs.list(aiConnect(fp))do
				local tsk = sgs.Sanguosha:getTriggerSkill(sk)
				if tsk and tsk:hasEvent(sgs.Damaged)
				then
					use.card = card
					use.to:append(ep)
					self.gz_to = fp
					return
				end
			end
		end
	end
end

sgs.ai_use_value.kemobileguanzongCard = 3.4
sgs.ai_use_priority.kemobileguanzongCard = 2.7

sgs.ai_skill_playerchosen.kemobileguanzongCard = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card")
    for _,target in sgs.list(destlist)do
		if target==self.gz_to
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_playerchosen.keoltongdu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target) and self:isWeak()
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
	self:sort(destlist,"card",true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()>5
		then return target end
	end
	return destlist[1]
end

local keolzhubi={}
keolzhubi.name="keolzhubi"
table.insert(sgs.ai_skills,keolzhubi)
keolzhubi.getTurnUseCard = function(self)
	return sgs.Card_Parse("#keolzhubiCard:.:")
end

sgs.ai_skill_use_func["#keolzhubiCard"] = function(card,use,self)
	self:sort(self.friends,"card")
	for _,fp in sgs.list(self.friends)do
		if not fp:hasFlag("keolzhubiTo")
		and fp:getCardCount()>1
		then
			use.card = card
			fp:setFlags("keolzhubiTo")
			use.to:append(fp)
			return
		end
	end
	for _,fp in sgs.list(self.enemies)do
		if not fp:hasFlag("keolzhubiTo")
		and fp:getCardCount()==1
		then
			use.card = card
			fp:setFlags("keolzhubiTo")
			use.to:append(fp)
			return
		end
	end
	for _,fp in sgs.list(self.friends)do
		if fp:getCardCount()>2
		then
			use.card = card
			fp:setFlags("keolzhubiTo")
			use.to:append(fp)
			return
		end
	end
end

sgs.ai_use_value.keolzhubiCard = 3.4
sgs.ai_use_priority.keolzhubiCard = 6.2

sgs.ai_skill_use["@@keolzhubi"] = function(self,prompt)
	local valid = {}
	local ids = self.player:getTag("keolzhubiForAI"):toIntList()
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards) -- 按保留值排序
   	for _,c in sgs.list(cards)do
    	if c:hasTip("keolzhubi")
		then
			for _,id in sgs.list(ids)do
				if table.contains(valid,id) then continue end
				local c2 = sgs.Sanguosha:getCard(id)
				if self:cardNeed(c2)>self:cardNeed(c)
				then
					table.insert(valid,c:getId())
					table.insert(valid,id)
					break
				end
			end
		end
	end
	if #valid<2 then return end
	return string.format("#keolzhubiVSCard:%s:",table.concat(valid,"+"))
end

sgs.ai_skill_playerchosen.keolzhuri = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
	local mc = self:getMaxCard()
	if mc then
		for _,target in sgs.list(destlist)do
			if self:isEnemy(target) and self:isWeak(target)
			and mc:getNumber()>9 then return target end
		end
		for _,target in sgs.list(destlist)do
			if self:isEnemy(target)
			and mc:getNumber()>9 then return target end
		end
		for _,target in sgs.list(destlist)do
			if not self:isFriend(target)
			and mc:getNumber()>9 then return target end
		end
		self:sort(destlist,"card",true)
		for _,target in sgs.list(destlist)do
			if self:isFriend(target)
			and mc:getNumber()>6 then return target end
		end
		for _,target in sgs.list(destlist)do
			if self:isEnemy(target) and self.player:getPhase()>sgs.Player_Play
			then return target end
		end
	end
end

sgs.ai_skill_use["@@keolzhuri"] = function(self,prompt)
	local ids = self.player:getTag("keolzhuriForAI"):toIntList()
	for _,id in sgs.list(ids)do
		local c2 = sgs.Sanguosha:getCard(id)
		if c2:isAvailable(self.player) then
			local d = self:aiUseCard(c2)
			if d.card then
				local tos = {}
				for _,p in sgs.list(d.to)do
					table.insert(tos,p:objectName())
				end
				if c2:canRecast() and #tos<1 then continue end
				return id.."->"..table.concat(tos,"+")
			end
		end
	end
end

sgs.ai_skill_choice.keolzhuri = function(self,choices)
	local items = choices:split("+")
	return (self.player:getPhase()>sgs.Player_Play or self:isWeak()) and items[2] or items[1]
end

sgs.ai_skill_invoke.keolranji = function(self,data)
    return self.player:getMark("keolranji_used-Clear") == self.player:getHp()
end

sgs.ai_skill_choice.keolranji = function(self,choices)
	local items = choices:split("+")
	return self:isWeak() and items[1] or items[2]
end

sgs.ai_use_revises.keolranji = function(self,card,use)
    if self.player:getMark("&keolranji_ban")>0 and card:isKindOf("Peach") then
		return false
	end 
end

sgs.ai_skill_invoke.keolbotu = function(self,data)
    return true
end

sgs.ai_skill_use["@@keolxiejuslash"] = function(self,prompt)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
    	local c = dummyCard()
		c:setSkillName("_keolxieju")
		c:addSubcard(h)
		if self.player:isLocked(c) or not h:isBlack() then continue end
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
end

sgs.ai_skill_discard.keolshuangxiong = function(self, discard_num, min_num, optional, include_equip) 
	local cards = self.player:getCards("he")
	if cards:length()>2 then
	    return self:askForDiscard("dummyreason", 1, 1, true, true)
	end
end

local keolshuangxiong = {}
keolshuangxiong.name = "keolshuangxiong"
table.insert(sgs.ai_skills, keolshuangxiong)
keolshuangxiong.getTurnUseCard = function(self)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if self.player:getMark("&keolshuangxiong+"..h:getColorString().."-Clear")>0
		then continue end
    	local c = dummyCard("duel")
		c:setSkillName("keolshuangxiong")
		c:addSubcard(h)
		if c:isAvailable(self.player)
		then return c end
	end
end


sgs.ai_skill_invoke.keolqiejian = function(self,data)
    return true
end

sgs.ai_fill_skill.tyzhuangpo = function(self)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
	    local des = h:getDescription()
		if string.find(des,"【杀】") then
			local c = dummyCard("duel")
			c:setSkillName("tyzhuangpo")
			c:addSubcard(h)
			if c:isAvailable(self.player)
			then return c end
		end
	end
end

sgs.ai_skill_invoke.tyzhuangpo = function(self,data)
    local to = data:toPlayer()
	return self:isEnemy(to)
	and to:getCardCount()>0
end

sgs.ai_skill_choice.tyzhuangpo = function(self,choices)
	local items = choices:split("+")
    local to = data:toPlayer()
	return items[math.min(to:getCardCount(),#items)]
end

sgs.ai_skill_invoke.yongyiyong = function(self,data)
    local to = data:toPlayer()
	return self:isEnemy(to)
	and not self:isFriend(to) and #self.friends_noself<1
end

sgs.ai_fill_skill.yongshanxie = function(self)
    return sgs.Card_Parse("#yongshanxieCard:.:")
end

sgs.ai_skill_use_func["#yongshanxieCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.yongshanxieCard = 3.4
sgs.ai_use_priority.yongshanxieCard = 6.2

sgs.ai_skill_discard.keolhuanfu = function(self,x,n)
	local to_cards = {}
	local use = self.player:getTag("keolhuanfuData"):toCardUse()
	if use.to:contains(self.player) then
		if self:getCardsNum("Jink")<1 then
			local m = self:ajustDamage(use.from,self.player,1,use.card)
			if m>0 and m<self.player:getHp()+self:getCardsNum("Peach,Analeptic") then
				local cards = self.player:getCards("he")
				cards = sgs.QList2Table(cards)
				self:sortByKeepValue(cards)
				for _,c in sgs.list(cards)do
					if self.player:isJilei(c) then continue end
					table.insert(to_cards,c:getEffectiveId())
					if #to_cards>=x or #to_cards>=m then break end
				end
			end
		end
	elseif use.from==self.player then
		if getCardsNum("Jink",use.to:first(),self.player)<1 then
			local m = self:ajustDamage(use.from,use.to:first(),1,use.card)
			if m>0 then
				local cards = self.player:getCards("he")
				cards = sgs.QList2Table(cards)
				self:sortByKeepValue(cards)
				for _,c in sgs.list(cards)do
					if self.player:isJilei(c) then continue end
					table.insert(to_cards,c:getEffectiveId())
					if #to_cards>=x or #to_cards>=m then break end
				end
			end
		end
	end
	return to_cards
end

sgs.ai_fill_skill.keolqingyi = function(self)
    return sgs.Card_Parse("#keolqingyiCard:.:")
end

sgs.ai_skill_use_func["#keolqingyiCard"] = function(card,use,self)
	self:sort(self.enemies,"card")
	for _,p in sgs.list(self.enemies)do
		if p:getCardCount()>0 and use.to:length()<2 then
			use.card = card
			use.to:append(p)
		end
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if p:getCardCount()>0 and use.to:length()<2
		and not(use.to:contains(p) or self:isFriend(p)) then
			use.card = card
			use.to:append(p)
		end
	end
end

sgs.ai_use_value.keolqingyiCard = 3.4
sgs.ai_use_priority.keolqingyiCard = 6.2

sgs.ai_skill_invoke.keolqingyi = function(self,data)
	return true
end


sgs.ai_skill_playerchosen.keolzeyue = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp",true)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and #self.enemies>=self.player:aliveCount()/2
		then return target end
	end
end

sgs.ai_skill_invoke.keolkangrui = function(self,data)
	return true
end

sgs.ai_skill_choice.keolkangrui = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	return self:isFriend(to) and to:isWounded() and items[1]
	or items[2]
end

sgs.ai_skill_invoke.keolqiejian = function(self,data)
    local to = data:toPlayer()
	return self:isFriend(to)
	or #self:poisonCards("ej")>0
	or self:doDisCard(to,"ej")
end

sgs.ai_skill_playerchosen.keolqiejian = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp",true)
    for _,target in sgs.list(destlist)do
		if self:doDisCard(target,"ej")
		then return target end
	end
end

sgs.ai_skill_playerchosen.keolnishou = function(self,players)
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

sgs.ai_skill_invoke.keolguangao = function(self,data)
    local st = data:toString()
	local sts = st:split(":")
	local to = BeMan(self.room,sts[2])
	if self:isFriend(to) then
		return to:getHandcardNum()%2==0
	elseif to:getHandcardNum()%2~=0
	then
		return self:isEnemy(to)
		or #self.friends_noself<1
	end
end

sgs.ai_skill_playerschosen.keolguangao = function(self,players,x,n)
	local tos = {}
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and #tos<x
		then table.insert(tos,target) end
	end
    return tos
end

sgs.ai_fill_skill.keolxieju = function(self)
    return sgs.Card_Parse("#keolxiejuCard:.:")
end

sgs.ai_skill_use_func["#keolxiejuCard"] = function(card,use,self)
	self:sort(self.friends,"card",true)
	for _,p in sgs.list(self.friends)do
		if p:getCardCount()>0 and p:getMark("keolxiejutar-Clear")>0 then
			use.card = card
			use.to:append(p)
		end
	end
end

sgs.ai_use_value.keolxiejuCard = 3.4
sgs.ai_use_priority.keolxiejuCard = 2.2



sgs.ai_fill_skill.keolshandao = function(self)
    return sgs.Card_Parse("#keolshandaoCard:.:")
end

sgs.ai_skill_use_func["#keolshandaoCard"] = function(card,use,self)
	self:sort(self.friends,"card",true)
	for _,p in sgs.list(self.friends)do
		if p:getCardCount()>0 then
			use.card = card
			use.to:append(p)
		end
	end
	for _,p in sgs.list(self.enemies)do
		if p:getCardCount()>0 and self:doDisCard(p,"e") then
			use.card = card
			use.to:append(p)
		end
	end
	for _,p in sgs.list(self.room:getAllPlayers())do
		if not(self:isEnemy(p) or use.to:contains(p))
		and p:getCardCount()>0 and #self.enemies>0 then
			use.card = card
			use.to:append(p)
		end
	end
end

sgs.ai_use_value.keolshandaoCard = 3.4
sgs.ai_use_priority.keolshandaoCard = 4.2

sgs.ai_fill_skill.keolkenshang = function(self)
    local dc = dummyCard()
	dc:setSkillName("keolkenshang")
	local cards = self:addHandPile("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	for i,c in sgs.list(cards)do
		if self.player:isLocked(c) then continue end
		dc:addSubcard(c)
		if dc:subcardsLength()>1 and (i>=#cards/2 or dc:subcardsLength()>=#self.enemies) then break end
	end
	return dc:subcardsLength()>1 and dc
end

function sgs.ai_cardsview.keolkenshang(self,class_name,player)
	local cn = patterns(class_name)
	if cn then
		local dc = dummyCard(cn)
		dc:setSkillName("keolkenshang")
		local cards = self:addHandPile("he")
		cards = sgs.QList2Table(cards)
		self:sortByKeepValue(cards)
		for i,c in sgs.list(cards)do
			if player:isLocked(c) then continue end
			dc:addSubcard(c)
			if i>#cards/2 or dc:subcardsLength()>#self.enemies then break end
		end
		return dc:subcardsLength()>1 and dc:toString()
	end
end



sgs.ai_fill_skill.keolxianbi = function(self)
    return sgs.Card_Parse("#keolxianbiCard:.:")
end

sgs.ai_skill_use_func["#keolxianbiCard"] = function(card,use,self)
	local tos = sgs.QList2Table(self.room:getAllPlayers())
	self:sort(tos,"equip",true)
	for _,p in sgs.list(tos)do
		if p:getEquips():length()>self.player:getHandcardNum() then
			use.card = card
			use.to:append(p)
			return
		end
	end
	for _,p in sgs.list(self.enemies)do
		if p:getEquips():length()<self.player:getHandcardNum() then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.keolxianbiCard = 3.4
sgs.ai_use_priority.keolxianbiCard = 3.2

sgs.ai_skill_playerchosen.keolzenrun = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and #self:poisonCards("he",target)<target:getCardCount()/2
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target) and #self.friends_noself<1
		and #self:poisonCards("he",target)<target:getCardCount()/2
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and #self:poisonCards("he",target)>=target:getCardCount()/2
		then return target end
	end
end

sgs.ai_skill_choice.keolzenrun = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	if self:isFriend(to) or self.player:getHandcardNum()<self.player:getMaxCards()
	then return items[1] end
end

sgs.ai_skill_playerchosen.keolzhuyan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	local function compare_func(a,b)
		return a:getMark("keolzhuyanhp")-a:getHp()>b:getMark("keolzhuyanhp")-b:getHp()
	end
	table.sort(destlist,compare_func)
	sgs.ai_skill_choice.keolzhuyan = "hp"
    for _,target in sgs.list(destlist)do
		if target:getMark("beusedzhuyanhp"..self.player:objectName())<1
		and target:getMark("keolzhuyanhp")-target:getHp()>0
		and self:isFriend(target) and self:isWeak(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if target:getMark("beusedzhuyanhp"..self.player:objectName())<1
		and target:getMark("keolzhuyanhp")-target:getHp()>0
		and self:isFriend(target) and target:isWounded()
		then return target end
	end
	destlist = sgs.reverse(destlist)
    for _,target in sgs.list(destlist)do
		if target:getMark("beusedzhuyanhp"..self.player:objectName())<1
		and self:isEnemy(target) and not self:isWeak(target)
		then return target end
	end
	local function compare_func(a,b)
		return a:getMark("keolzhuyansp")-a:getHandcardNum()>b:getMark("keolzhuyansp")-b:getHandcardNum()
	end
	table.sort(destlist,compare_func)
	sgs.ai_skill_choice.keolzhuyan = "sp"
    for _,target in sgs.list(destlist)do
		if target:getMark("beusedzhuyansp"..self.player:objectName())<1
		and target:getMark("keolzhuyansp")-target:getHandcardNum()>0
		and self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(sgs.reverse(destlist))do
		if target:getMark("beusedzhuyansp"..self.player:objectName())<1
		and target:getMark("keolzhuyansp")-target:getHandcardNum()<0
		and self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_playerchosen.keolleijie = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and target:getHandcardNum()<5
		and self:getFinalRetrial(nil,"lightning")==1
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and self:getFinalRetrial(nil,"lightning")==1
		then return target end
	end
	local c = sgs.Sanguosha:getCard(self.room:getDrawPile():first())
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and c:hasFlag("visible") and c:getSuit()~=0
		and self:getFinalRetrial(nil,"lightning")<2
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and target:getHandcardNum()<5
		and self:getFinalRetrial(nil,"lightning")<2
		and not self:isWeak(target)
		then return target end
	end
end

sgs.ai_skill_use["@@keolsujislash"] = function(self,prompt)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
    	local c = dummyCard()
		c:setSkillName("keolsuji")
		c:addSubcard(h)
		if self.player:isLocked(c) or not h:isBlack() then continue end
		local dummy = self:aiUseCard(c)
		if dummy.card then
			local tos = {}
			for _,p in sgs.list(dummy.to)do
				table.insert(tos,p:objectName())
			end
			return c:toString().."->"..table.concat(tos,"+")
		end
	end
end

sgs.ai_skill_invoke.keollangdao = function(self,data)
	local use = data:toCardUse()
	for _,to in sgs.list(use.to)do
		if self:isEnemy(to) and self:isWeak(to)
		then return true end
	end
end

sgs.ai_skill_playerschosen.keollangdao = function(self,players,x,n)
	local tos = {}
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if (self:isEnemy(target) or #self.friends_noself<1) and #tos<x
		then table.insert(tos,target) end
	end
    return tos
end

sgs.ai_skill_choice.keolgoude = function(self,choices,data)
	local items = choices:split("+")
	if table.contains(items,"slash") then
		local dc = dummyCard()
		dc:setSkillName("_keolgoude")
		local d = self:aiUseCard(dc)
		if d.card then
			self.keolgoudeUse = d
			return "slash"
		end
	end
	if table.contains(items,"dis") then
		for _,p in sgs.list(self.enemies)do
			if p:getHandcardNum()<3 and self:doDisCard(p,"h") then
				return "dis"
			end
		end
	end
	if table.contains(items,"kingdom") then
		local ks = {}
		for _,p in sgs.list(self.room:getAllPlayers())do
			ks[p:getKingdom()] = (ks[p:getKingdom()] or 0)+1
		end
		local x = 0
		for k,n in pairs(ks)do
			if n>x then x = n end
		end
		for k,n in pairs(ks)do
			if n>=x and k~=self.player:getKingdom() then
				sgs.ai_skill_choice.keolgoudekingdom = k
				return "kingdom"
			end
		end
	end
	if table.contains(items,"draw") then
		return "draw"
	end
	if table.contains(items,"dis") then
		for _,p in sgs.list(self.enemies)do
			if self:doDisCard(p,"h") then
				return "dis"
			end
		end
	end
	return "cancel"
end

sgs.ai_skill_use["@@keolgoude"] = function(self,prompt)
	local dummy = self.keolgoudeUse
	if dummy.card then
		local tos = {}
		for _,p in sgs.list(dummy.to)do
			table.insert(tos,p:objectName())
		end
		return dummy.card:toString().."->"..table.concat(tos,"+")
	end
end


sgs.ai_fill_skill.keolzhenying = function(self)
    return sgs.Card_Parse("#keolzhenyingCard:.:")
end

sgs.ai_skill_use_func["#keolzhenyingCard"] = function(card,use,self)
	self:sort(self.friends_noself,"handcard",true)
	for _,p in sgs.list(self.friends_noself)do
		if p:getHandcardNum()<=self.player:getHandcardNum()
		and p:getHandcardNum()<2 and self.player:getHandcardNum()<4 then
			use.card = card
			use.to:append(p)
			self.keolzhenyingTo = p
			return
		end
	end
	self:sort(self.enemies,"handcard",true)
	for _,p in sgs.list(self.enemies)do
		if p:getHandcardNum()<=self.player:getHandcardNum() then
			use.card = card
			use.to:append(p)
			self.keolzhenyingTo = p
			return
		end
	end
end

sgs.ai_skill_choice.keolzhenying = function(self,choices,data)
	local items = choices:split("+")
	local from = data:toPlayer()
	if from==self.player then
		if self:isFriend(self.keolzhenyingTo) then
			return items[#items]
		end
	elseif self:isFriend(from) then
		return items[#items]
	end
end

sgs.ai_skill_choice.keolfengyan = function(self,choices,data)
	local items = choices:split("+")
	local from = data:toPlayer()
	if self:isFriend(from) or #self.enemies>0 and not self:isEnemy(from) then
		return items[1]
	end
	return items[2]
end

sgs.ai_skill_invoke.keolbixin = function(self,data)
	local tr = data:toString()
	for _,cn in sgs.list(tr:split("+"))do
		local dc = dummyCard(cn)
		dc:setSkillName("_keolbixin")
		local d = self:aiUseCard(dc)
		if d.card then
			sgs.ai_skill_choice.keolbixin = cn
			self.keolbixinUse = d
			return true
		end
	end
end

sgs.ai_skill_choice.keolbixintype = function(self,choices,data)
	local items = choices:split("+")
    local cards = self.player:getCards("h")
	local kv = {}
	for _,k in ipairs(items)do
		kv[k] = 0
	end
	for _,h in sgs.list(cards)do
		kv[h:getType()] = (kv[h:getType()] or 0)+1--self:getKeepValue(h)
	end
	local x = 99
	for k,n in pairs(kv)do
		if n<x and table.contains(items,k) then x = n end
	end
	for k,n in pairs(kv)do
		if n<=x and table.contains(items,k) then
			return k
		end
	end
end

sgs.ai_fill_skill.keolbixin = function(self)
    return sgs.Card_Parse("#keolbixinCard:.:")
end

sgs.ai_skill_use_func["#keolbixinCard"] = function(card,use,self)
	for _,p in sgs.list(patterns())do
		local dc = dummyCard(p)
		if dc and dc:getTypeId()==1 and self:getCardsNum(dc:getClassName())<1
		and self.player:getMark("keolbixin_guhuo_remove_"..p)<1 then
			dc:setSkillName("keolbixin")
			if dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				if d.card then
					use.card = sgs.Card_Parse("#keolbixinCard:.:"..p)
					use.to = d.to
					sgs.ai_use_priority.keolbixinCard = sgs.ai_use_priority[dc:getClassName()]
					return
				end
			end
		end
	end
end

sgs.ai_use_value.keolbixinCard = 3.4
sgs.ai_use_priority.keolbixinCard = 3.2

sgs.ai_guhuo_card.keolbixin = function(self,toname,class_name)
	if self:getCardsNum(class_name)<1
	then return "#keolbixinCard:.:"..toname end
end

sgs.ai_fill_skill.keolshizhan = function(self)
    return sgs.Card_Parse("#keolshizhanCard:.:")
end

sgs.ai_skill_use_func["#keolshizhanCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,p in sgs.list(self.enemies)do
		if self:getCardsNum("Slash")>getCardsNum("Slash",p,self.player)  then
			local dc = dummyCard("duel")
			dc:setSkillName("keolshizhan")
			if dc:targetFilter(sgs.PlayerList(),self.player,p) then
				use.card = card
				use.to:append(p)
				return
			end
		end
	end
end

sgs.ai_use_value.keolshizhanCard = 3.4
sgs.ai_use_priority.keolshizhanCard = 2.2


sgs.ai_fill_skill.xin2chuhai = function(self)
    return sgs.Card_Parse("#xin2chuhaiCard:.:")
end

sgs.ai_skill_use_func["#xin2chuhaiCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard")
	local mc = self:getMaxCard()
	if mc and mc:getNumber()>8 then
		for i,c in sgs.list(self.toUse)do
			if i>1 and c:isDamageCard() then
				local d = self:aiUseCard(c)
				if d.card then
					for _,p in sgs.list(d.to)do
						if self:isEnemy(p) and self.player:canPindian(p) then
							use.card = card
							return
						end
					end
				end
			end
		end
	end
end

sgs.ai_use_value.xin2chuhaiCard = 3.4
sgs.ai_use_priority.xin2chuhaiCard = 6.2

sgs.ai_skill_playerchosen.xin2chuhai = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self.player:canPindian(target)
		then return target end
	end
end

sgs.ai_fill_skill.keolyilie = function(self)
	for _,p in sgs.list(patterns())do
		local dc = dummyCard(p)
		if dc and dc:getTypeId()==1 and self:getCardsNum(dc:getClassName())<1
		and self.player:getMark("keolyilie_guhuo_remove_"..p)<1 then
			local cards = self.player:getCards("h")
			if cards:length()<2 then break end
			cards = sgs.QList2Table(cards) -- 将列表转换为表
			self:sortByKeepValue(cards) -- 按保留值排序
			dc:setSkillName("keolyilie")
			for _,h in sgs.list(cards)do
				for _,c in sgs.list(cards)do
					if h:getId()~=c:getId() and h:getColor()==c:getColor() then
						dc:addSubcard(h)
						dc:addSubcard(c)
						if dc:isAvailable(self.player) then
							local d = self:aiUseCard(dc)
							if d.card then
								return dc
							end
						end
						dc:clearSubcards()
						break
					end
				end
			end
		end
	end
end

sgs.ai_guhuo_card.keolyilie = function(self,toname,class_name)
	if self:getCardsNum(class_name)<1 then
		local cards = self.player:getCards("h")
		if cards:length()<2 then return end
		cards = sgs.QList2Table(cards) -- 将列表转换为表
		self:sortByKeepValue(cards) -- 按保留值排序
		local dc = dummyCard(toname)
		dc:setSkillName("keolyilie")
		for _,h in sgs.list(cards)do
			for _,c in sgs.list(cards)do
				if h:getId()~=c:getId() and h:getColor()==c:getColor() then
					dc:addSubcard(h)
					dc:addSubcard(c)
					if not self.player:isLocked(dc) then
						return dc:toString()
					end
					dc:clearSubcards()
					break
				end
			end
		end
	end
end

sgs.ai_fill_skill.ny_ol_yanru = function(self)
    return sgs.Card_Parse("#ny_ol_yanruCard:.:")
end

sgs.ai_skill_use_func["#ny_ol_yanruCard"] = function(card,use,self)
	
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards) -- 将列表转换为表
	if #cards<6 then
		if self.player:getMark("ny_ol_yanru_even-PlayClear")<1 then
			self:sortByKeepValue(cards) -- 按保留值排序
			for i,c in sgs.list(cards)do
				if self.player:isJilei(c)
				or card:subcardsLength()>=#cards/2
				then continue end
				card:addSubcard(c)
			end
			if card:subcardsLength()>=#cards/2 then use.card = card end
		end
		if #cards<4 and self.player:getMark("ny_ol_yanru_odd-PlayClear")<1 then
			card:clearSubcards()
			use.card = card
		end
	end
end

sgs.ai_use_value.ny_ol_yanruCard = 3.4
sgs.ai_use_priority.ny_ol_yanruCard = 3.2

sgs.ai_skill_discard.ny_ol_yanru = function(self,x,n)
	local to_cards = {}
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
   	for _,c in sgs.list(cards)do
		if self.player:isJilei(c) or #to_cards>=n
		or self:aiUseCard(c).card then continue end
		table.insert(to_cards,c:getEffectiveId())
	end
   	for _,c in sgs.list(cards)do
		if self.player:isJilei(c) or #to_cards>=n
		or table.contains(to_cards,c:getEffectiveId()) then continue end
		table.insert(to_cards,c:getEffectiveId())
	end
	return to_cards
end

sgs.ai_skill_invoke.ny_ol_hezhong = function(self,data)
	
	return true
end

sgs.ai_skill_choice.ny_ol_hezhong = function(self,choices,data)
	local items = choices:split("+")
    local n = data:toInt()
	if n>6 then
		return items[2]
	end
	return items[1]
end

sgs.ai_skill_invoke.huamu = function(self,data)
    for _,p in sgs.list(self.friends)do
		if p:hasSkill("liangyuan",true)
		then return true end
	end
end

sgs.ai_fill_skill.liangyuan = function(self)
    return sgs.Card_Parse("#liangyuanCard:.:")
end

sgs.ai_skill_use_func["#liangyuanCard"] = function(card,use,self)
	for _,pc in ipairs({"analeptic","peach"}) do
		local dc = canLyUse(self.player,pc)
		if dc and dc:isAvailable(self.player) and self:getCardsNum(dc:getClassName())<1 then
			local d = self:aiUseCard(dc)
			if d.card then
				use.card = card
				use.to = d.to
				sgs.ai_use_priority.liangyuanCard = sgs.ai_use_priority[dc:getClassName()]
				sgs.ai_skill_choice.liangyuan = pc
				break
			end
		end
	end
end

sgs.ai_use_value.liangyuanCard = 3.4
sgs.ai_use_priority.liangyuanCard = 3.2

sgs.ai_guhuo_card.liangyuan = function(self,toname,class_name)
	if self.player:getMark(toname.."liangyuan_lun")<1 and self:getCardsNum(class_name)<1 then
		sgs.ai_skill_choice.liangyuan = toname
		return "#liangyuanCard:.:"..toname
	end
end

sgs.ai_skill_playerchosen.jisi = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp",true)
	local sks = self.player:getTag("jisi_invoke"):toString():split("+")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and not self:isWeak(target) then
			if self.player:hasSkill("huamu",true) and table.contains(sks,"liangyuan") then
				if self.player:hasSkill("liangyuan",true) and table.contains(sks,"liangyuan") then
					sgs.ai_skill_choice.jisi = "liangyuan"
					return target
				end
				if self.player:hasSkill("qianmeng",true) and table.contains(sks,"qianmeng") then
					sgs.ai_skill_choice.jisi = "qianmeng"
					return target
				end
			end
		end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target) and self:isWeak(target) then
			local dc = dummyCard()
			local d = self:aiUseCard(dc)
			if table.contains(sks,"huamu") then
				sgs.ai_skill_choice.jisi = "huamu"
			end
			if d.card and d.to:contains(target) then
				return target
			end
		end
	end
end


sgs.ai_skill_invoke.keolyangkuang = function(self,data)
	local cur = self.room:getCurrent()
	return self:isFriend(cur) or #self.friends<2
	or #self.enemies>0 and not self:isEnemy(cur)
end

sgs.ai_skill_invoke.keolcihuang = function(self,data)
	local tr = self.player:getTag("keolcihuang"):toString()
	local from = data:toPlayer()
	for _,cn in sgs.list(tr:split("+"))do
		local dc = dummyCard(cn)
		dc:setSkillName("keolcihuang")
		local d = self:aiUseCard(dc)
		if d.card and d.to:contains(from) then
			sgs.ai_skill_choice.keolcihuang = cn
			return true
		end
	end
	for _,cn in sgs.list(tr:split("+"))do
		local dc = dummyCard(cn)
		dc:setSkillName("keolcihuang")
		local d = self:aiUseCard(dc)
		if d.card and d.to:contains(self.player) and self:isFriend(from) then
			sgs.ai_skill_choice.keolcihuang = cn
			return true
		end
	end
end

sgs.ai_skill_discard.keolhetao = function(self,x,n)
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	local use = self.player:getTag("keolhetao"):toCardUse()
   	for _,p in sgs.list(use.to)do
		if use.card:isDamageCard() and self:isEnemy(p) then
			for _,c in sgs.list(cards)do
				if self.player:isJilei(c)
				or use.card:getColor()~=c:getColor()
				then continue end
				self.keolhetaoTo = p
				return {c:getEffectiveId()}
			end
		end
	end
   	for _,p in sgs.list(use.to)do
		if use.to:contains(use.from) and self:isFriend(p)
		and not self:isFriend(use.from) then
			for _,c in sgs.list(cards)do
				if self.player:isJilei(c)
				or use.card:getColor()~=c:getColor()
				then continue end
				self.keolhetaoTo = p
				return {c:getEffectiveId()}
			end
		end
	end
	return {}
end

sgs.ai_skill_playerchosen.keolhetao = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if target==self.keolhetaoTo
		then return target end
	end
end

sgs.ai_skill_invoke.keolshenli = function(self,data)
	local use = data:toCardUse()
	local n = 0
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if self.player:canSlash(p, use.card, false) then
			if self:isFriend(p) then
				n = n-1
			else
				n = n+1
			end
		end
	end
	return n>0
end

sgs.ai_skill_invoke.keolshishou = function(self,data)
	local from = data:toPlayer()
	return self:isFriend(from)
end

function sgs.ai_weapon_value._keolsizhaojian(self,enemy)
	if enemy then return 6-enemy:getHp() end
	return 3
end

sgs.ai_keep_value.Keolsizhaojian = 4.08



sgs.ai_skill_invoke.moujianxiong = function(self,data)
	return true
end

sgs.ai_skill_invoke.mouqingzheng = function(self,data)
	return true
end

sgs.ai_skill_use["@@mouqingzheng"] = function(self,prompt)
	local valid = {}
	local ids = {}
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards) -- 按保留值排序
	local n = self.player:getMark("mouqingzhengSuits")
   	for _,c in sgs.list(cards)do
		if table.contains(ids,c:getSuit()) then continue end
		table.insert(ids,c:getSuit())
	end
	local function func(a,b)
		local an,bn = 0,0
		for _,c in sgs.list(cards)do
			if a==c:getSuit() then an = an+1 end
			if b==c:getSuit() then bn = bn+1 end
		end
		return an<bn
	end
	table.sort(ids,func)
	local ids2 = {}
   	for _,s in sgs.list(ids)do
		table.insert(ids2,s)
		if #ids2>=n then break end
	end
   	for _,c in sgs.list(cards)do
		if table.contains(ids2,c:getSuit())
		then table.insert(valid,c:getId()) end
	end
	if #valid<1 then return end
	self:sort(self.enemies,"handcard",true)
   	for _,p in sgs.list(self.enemies)do
		if p:getHandcardNum()>=#valid or self:isWeak(p) and p:getHandcardNum()>#valid/2 then
			self.mouqingzhengTo = p
			return "#mouqingzhengCard:"..table.concat(valid,"+")..":->"..p:objectName()
		end
	end
end

sgs.ai_skill_askforag.mouqingzheng = function(self,card_ids)
	local cards = {}
    for _,id in sgs.list(card_ids)do
		table.insert(cards,sgs.Sanguosha:getCard(id))
    end
	local ids = {}
   	for _,c in sgs.list(cards)do
		if table.contains(ids,c:getSuit()) then continue end
		table.insert(ids,c:getSuit())
	end
	local function func(a,b)
		local an,bn = 0,0
		for _,c in sgs.list(cards)do
			if a==c:getSuit() then an = an+1 end
			if b==c:getSuit() then bn = bn+1 end
		end
		return an>bn
	end
	table.sort(ids,func)
    self:sortByKeepValue(cards,true) -- 按保留值排序
   	for _,c in sgs.list(cards)do
		if ids[1]==c:getSuit()
		then return c:getId() end
	end
end

sgs.ai_skill_playerchosen.mouhujia = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target) and self:isWeak(target)
		then return target end
	end
	local damage = self.player:getTag("mouhujiaDamage"):toDamage()
    for _,target in sgs.list(sgs.reverse(destlist))do
		if self:isFriend(target) and not self:isWeak(target)
		and self:canDamageHp(damage.from,damage.card,target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target) and self:isWeak()
		then return target end
	end
end

sgs.ai_fill_skill.moufanjian = function(self)
    return sgs.Card_Parse("#moufanjianCard:.:")
end

sgs.ai_skill_use_func["#moufanjianCard"] = function(card,use,self)
	for _,p in ipairs(self.friends_noself) do
		if not p:faceUp() then
			local cards = self.player:getCards("h")
			cards = sgs.QList2Table(cards)
			self:sortByKeepValue(cards) -- 按保留值排序
			for _,c in ipairs(cards) do
				if table.contains(self.toUse,c)
				or self.player:getMark(c:getSuitString().."moufanjianUsed-PlayClear")>0
				then continue end
				use.card = sgs.Card_Parse("#moufanjianCard:"..c:getId()..":")
				use.to:append(p)
				sgs.ai_use_priority.moufanjianCard = 8.8
				return
			end
		end
	end
	self:sort(self.enemies,"hp")
	for _,p in ipairs(self.enemies) do
		if p:faceUp() then
			local cards = self.player:getCards("h")
			cards = sgs.QList2Table(cards)
			self:sortByKeepValue(cards) -- 按保留值排序
			for _,c in ipairs(cards) do
				if table.contains(self.toUse,c)
				or self.player:getMark(c:getSuitString().."moufanjianUsed-PlayClear")>0
				then continue end
				use.card = sgs.Card_Parse("#moufanjianCard:"..c:getId()..":")
				use.to:append(p)
				sgs.ai_use_priority.moufanjianCard = 3.8
				return
			end
		end
	end
end

sgs.ai_use_value.moufanjianCard = 3.4
sgs.ai_use_priority.moufanjianCard = 3.2

sgs.ai_skill_choice.moufanjian = function(self,choices,data)
	local items = choices:split("+")
	if not self.player:faceUp() then
		return items[3]
	end
	return items[math.random(1,2)]
end

sgs.ai_fill_skill.mourende = function(self)
	self.mourendeUse = nil
	for _,p in sgs.list(patterns())do
		local dc = dummyCard(p)
		if dc and dc:getTypeId()==1 and self.player:getMark("mRenWang-Clear")<1
		and self.player:getMark("&mRenWang")>1 and self:getCardsNum(dc:getClassName())<2 then
			dc:setSkillName("_mourende")
			if dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				if d.card then
					self.mourendeUse = d
					sgs.ai_use_priority.mourendeCard = sgs.ai_use_priority[dc:getClassName()]
					return sgs.Card_Parse("#mourendeCard:.:")
				end
			end
		end
	end
	sgs.ai_use_priority.mourendeCard = sgs.ai_use_priority.RendeCard
	return sgs.Card_Parse("#mourendeCard:.:")
end

sgs.ai_skill_use_func["#mourendeCard"] = function(card,use,self)
	if self.mourendeUse then
		use.card = card
		return
	end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByUseValue(cards,true)
	local notFound = false
	for i = 1,#cards do
		local nc,friend = self:getCardNeedPlayer(cards)
		if nc and friend then cards = self:resetCards(cards,nc)
		else notFound = true break end
		if friend:objectName()==self.player:objectName()
		or friend:getMark("mourendeCardGet-PlayClear")>0
		then continue end
		if nc:isAvailable(self.player) then
			if nc:isDamageCard() or nc:isKindOf("SingleTargetTrick") then
				local dummy_use = self:aiUseCard(nc)
				if dummy_use.card and dummy_use.to:length()>0 then
					if dummy_use.to:length()>1 or self:isWeak(dummy_use.to:first())
					or sgs.aiResponse[nc:getClassName()] and getCardsNum(sgs.aiResponse[nc:getClassName()],dummy_use.to:first(),self.player)<1
					then continue end
				end
			elseif nc:isKindOf("DelayedTrick") then
				if self:getEnemyNumBySeat(self.player,friend)>0
				and self:aiUseCard(nc).card then continue end
			end
		end
		local ids = {}
		if friend:hasSkill("enyuan")
		and #cards>=1 and cards[1]~=nc
		then table.insert(ids,cards[1]:getEffectiveId()) end
		table.insert(ids,nc:getEffectiveId())
		use.card = sgs.Card_Parse("#mourendeCard:"..table.concat(ids,"+")..":")
		use.to:append(friend)
		return
	end
	if notFound and self.player:isWounded()
	and self.player:getMark("&mRenWang")<4
	and self.player:getCardCount()>4 then
		cards = sgs.QList2Table(self.player:getCards("he"))
		self:sortByUseValue(cards,true)
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if not self:isFriend(p) and hasManjuanEffect(p)
			and p:getMark("mourendeCardGet-PlayClear")<1 then
				local ids = {}
				for _,c in ipairs(cards)do
					if not isCard("Peach,ExNihilo",c,self.player)
					then table.insert(ids,c:getEffectiveId()) end
					if #ids>1 then break end
				end
				if #ids>0 then
					use.card = sgs.Card_Parse("#mourendeCard:"..table.concat(ids,"+")..":")
					use.to:append(p)
					return
				end
			end
		end
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if not self:isEnemy(p) and p:getMark("mourendeCardGet-PlayClear")<1 then
				local ids = {}
				for _,c in ipairs(cards)do
					if not isCard("Peach,ExNihilo",c,self.player)
					then table.insert(ids,c:getEffectiveId()) end
					if #ids>1 then break end
				end
				if #ids>0 then
					use.card = sgs.Card_Parse("#mourendeCard:"..table.concat(ids,"+")..":")
					use.to:append(p)
					return
				end
			end
		end
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if p:getMark("&mourdTOzw")<1 then
				local ids = {}
				for _,c in ipairs(cards)do
					if not isCard("Peach,ExNihilo",c,self.player)
					then table.insert(ids,c:getEffectiveId()) break end
				end
				if #ids>0 then
					use.card = card
					use.to:append(p)
					return
				end
			end
		end
	end
end

sgs.ai_use_value.mourendeCard = sgs.ai_use_value.RendeCard
sgs.ai_use_priority.mourendeCard = sgs.ai_use_priority.RendeCard

sgs.ai_skill_askforag.mourende = function(self,card_ids)
	local cards = {}
    for _,id in sgs.list(card_ids)do
		table.insert(cards,sgs.Sanguosha:getCard(id))
    end
	if self.mourendeUse then
		for _,c in sgs.list(cards)do
			if self.mourendeUse.card:objectName()==c:objectName()
			then return c:getId() end
		end
	end
    self:sortByKeepValue(cards,true) -- 按保留值排序
   	for _,c in sgs.list(cards)do
		local dc = dummyCard(c:objectName())
		dc:setSkillName("_mourende")
		if dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then
				self.mourendeUse = d
				return c:getId()
			end
		end
	end
end

sgs.ai_skill_use["@@mourende"] = function(self,prompt)
	local dummy = self.mourendeUse
	if dummy.card then
		local tos = {}
		for _,p in sgs.list(dummy.to)do
			table.insert(tos,p:objectName())
		end
		return dummy.card:toString().."->"..table.concat(tos,"+")
	end
end

sgs.ai_guhuo_card.mourende = function(self,toname,class_name)
	if self:getCardsNum(class_name)<1 or self.player:getMark("&mRenWang")>4 then
		local dc = dummyCard(toname)
		dc:setSkillName("_mourende")
		return dc:toString()
	end
end

sgs.ai_fill_skill.mouzhangwu = function(self)
	return self.player:getMark("mRenWang-Clear")>0
	and sgs.Card_Parse("#mouzhangwuCard:.:")
end

sgs.ai_skill_use_func["#mouzhangwuCard"] = function(card,use,self)
	local tos = self.room:getOtherPlayers(self.player)
	local n = 0
	for _,p in sgs.qlist(tos)do
		if p:getMark("&mourdTOzw")>0 then
			n = n+1
		end
	end
	local x = self.room:getTag("TurnLengthCount"):toInt()
	if x>3 and n>tos:length()/2 or x>2 and self:isWeak() and n>=tos:length()/2 then
		use.card = card
	end
end

sgs.ai_use_value.mouzhangwuCard = 4.5
sgs.ai_use_priority.mouzhangwuCard = 7.7

sgs.ai_skill_use["@@moujijiang"] = function(self,prompt)
	local tos = {}
	self:sort(self.enemies,"hp")
	for _,ep in sgs.list(self.enemies)do
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if p:getKingdom()=="shu" and p:getHp()>=self.player:getHp()
			and self:isFriend(p) and p:inMyAttackRange(ep) then
				table.insert(tos,ep:objectName())
				table.insert(tos,p:objectName())
				return "#moujijiangCard:.:->"..table.concat(tos,"+")
			end
		end
	end
	for _,ep in sgs.list(self.enemies)do
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if p:getKingdom()=="shu" and p:getHp()>=self.player:getHp()
			and p:inMyAttackRange(ep) then
				table.insert(tos,ep:objectName())
				table.insert(tos,p:objectName())
				return "#moujijiangCard:.:->"..table.concat(tos,"+")
			end
		end
	end
	for _,ep in sgs.list(self.room:getOtherPlayers(self.player))do
		if self:isFriend(ep) then continue end
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if p:getKingdom()=="shu" and p:getHp()>=self.player:getHp()
			and p:inMyAttackRange(ep) then
				table.insert(tos,ep:objectName())
				table.insert(tos,p:objectName())
				return "#moujijiangCard:.:->"..table.concat(tos,"+")
			end
		end
	end
end

sgs.ai_skill_choice.moujijiang = function(self,choices,data)
	local items = choices:split("+")
	local use = data:toCardUse()
	if self:isFriend(use.to:first()) and self:isWeak(use.to:first()) then
		return items[2]
	end
	return items[1]
end



sgs.ai_skill_invoke.rensongshu = function(self,data)
	local to = data:toPlayer()
	local card_ids = self.room:getTag("ren_pile"):toIntList()
	return self:isEnemy(to) and (card_ids:length()<4 or self:isWeak())
end

sgs.ai_skill_invoke.renyizhu = function(self,data)
	local use = data:toCardUse()
	if self:isEnemy(use.from) then
		return self:isFriend(use.to:first())
		or self:isEnemy(use.to:first())
	end
end

sgs.ai_fill_skill.renluanchou = function(self)
	return sgs.Card_Parse("#renluanchouCard:.:")
end

sgs.ai_skill_use_func["#renluanchouCard"] = function(card,use,self)
	self:sort(self.friends,"hp")
	self:sort(self.enemies,"hp",true)
	for _,ep in sgs.list(self.enemies)do
		for _,p in sgs.list(self.friends)do
			if ep:getHp()>p:getHp() then
				use.card = card
				return
			end
		end
	end
	local tos = self.room:getAllPlayers()
	tos = self:sort(tos,"hp",true)
	for _,ep in sgs.list(tos)do
		for _,p in sgs.list(self.friends)do
			if ep:getHp()>p:getHp() then
				use.card = card
				return
			end
		end
	end
	for _,ep in sgs.list(self.enemies)do
		for _,p in sgs.list(self.friends)do
			if ep:getHp()>=p:getHp() then
				use.card = card
				return
			end
		end
	end
	for _,ep in sgs.list(tos)do
		for _,p in sgs.list(self.friends)do
			if ep~=p and ep:getHp()>=p:getHp() then
				use.card = card
				return
			end
		end
	end
end

sgs.ai_use_value.renluanchouCard = 4.5
sgs.ai_use_priority.renluanchouCard = 6.7

sgs.ai_skill_playerschosen.renluanchou = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	local tos = {}
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) then
			table.insert(tos,target)
			break
		end
	end
    for _,target in sgs.list(sgs.reverse(destlist))do
		if self:isEnemy(target) and #tos==1 and target:getHp()>tos[1]:getHp() then
			table.insert(tos,target)
		end
	end
    for _,target in sgs.list(sgs.reverse(destlist))do
		if self:isEnemy(target) and #tos==1 and target:getHp()>=tos[1]:getHp() then
			table.insert(tos,target)
		end
	end
    for _,target in sgs.list(sgs.reverse(destlist))do
		if #tos==1 and target:getHp()>=tos[1]:getHp() then
			table.insert(tos,target)
		end
	end
	return tos
end

sgs.ai_skill_invoke.renliaoyi = function(self,data)
	local to = data:toPlayer()
	return self:isFriend(to) and to:getHp()>to:getHandcardNum()
	or self:isEnemy(to) and to:getHp()<to:getHandcardNum()
end

sgs.ai_fill_skill.renbinglun = function(self)
	local card_ids = self.room:getTag("ren_pile"):toIntList()
	return card_ids:length()>0
	and sgs.Card_Parse("#renbinglunCard:.:")
end

sgs.ai_skill_use_func["#renbinglunCard"] = function(card,use,self)
	self:sort(self.friends,"hp")
	for _,p in sgs.list(self.friends)do
		if p:isWounded() then
			use.card = card
			use.to:append(p)
			return
		end
	end
	self:sort(self.friends,"handcard")
	for _,p in sgs.list(self.friends)do
		if p:getHandcardNum()<p:getHp() then
			use.card = card
			use.to:append(p)
			return
		end
	end
	use.card = card
	use.to:append(self.player)
end

sgs.ai_use_value.renbinglunCard = 4.5
sgs.ai_use_priority.renbinglunCard = 6.7

sgs.ai_skill_choice.renbinglun = function(self,choices,data)
	local items = choices:split("+")
	if self.player:getMark("&renbinglun")>0
	or self.player:getHandcardNum()<3
	then return items[1] end
	return items[2]
end

sgs.ai_fill_skill.mobile_wuling = function(self)
	return sgs.Card_Parse("#mobile_wulingCard:.:")
end

sgs.ai_skill_use_func["#mobile_wulingCard"] = function(card,use,self)
	self:sort(self.friends,nil,true)
	for _,p in sgs.list(self.friends)do
		if p:getMark("mobile_wuling")<1 then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.mobile_wulingCard = 4.5
sgs.ai_use_priority.mobile_wulingCard = 5.7

sgs.ai_skill_choice.mobile_wuling = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	if self:isFriend(to) then
		if table.contains(items,"wl_hu_mark") and self:isWeak(to) then
			return "wl_hu_mark"
		end
		if table.contains(items,"wl_lu_mark") and to:getCards("j"):length()>0 then
			return "wl_lu_mark"
		end
	end
end

sgs.ai_fill_skill.mobile_youyi = function(self)
	local card_ids = self.room:getTag("ren_pile"):toIntList()
	return card_ids:length()>0 and sgs.Card_Parse("#mobile_youyiCard:.:")
end

sgs.ai_skill_use_func["#mobile_youyiCard"] = function(card,use,self)
	local n = 0
	for _,p in sgs.list(self.room:getAllPlayers())do
		if p:isWounded() then
			if self:isEnemy(p) then
				n = n-1
				if self:isWeak(p)
				then n = n-1 end
			else
				n = n+1
				if self:isFriend(p) and self:isWeak(p)
				then n = n+1 end
			end
		end
	end
	if n>0 then
		use.card = card
	end
end

sgs.ai_use_value.mobile_youyiCard = 4.5
sgs.ai_use_priority.mobile_youyiCard = 1.7

sgs.ai_skill_invoke.mobile_youyi = function(self)
	return true
end

sgs.ai_skill_invoke.mobile_tamo = function(self)
	return true
end

sgs.ai_fill_skill.mobile_dingzou = function(self)
	return sgs.Card_Parse("#mobile_dingzouCard:.:")
end

sgs.ai_skill_use_func["#mobile_dingzouCard"] = function(card,use,self)
	self:sort(self.friends_noself,nil,true)
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards) -- 按保留值排序
	for _,p in sgs.list(self.friends_noself)do
		if #self:poisonCards("ej",p)>p:getEquips():length()/2 then
			local n = p:getCards("ej"):length()
			if n<1 then continue end
			local ids = {}
			for _,c in sgs.list(cards)do
				--card:addSubcard(c)
				table.insert(ids,c:getEffectiveId())
				if #ids>=n then--card:subcardsLength()>=n then
					use.card = sgs.Card_Parse("#mobile_dingzouCard:"..table.concat(ids,"+")..":")
					use.to:append(p)
					return
				end
			end
			--card:clearSubcards()
		end
	end
	for _,p in sgs.list(self.enemies)do
		if #self:poisonCards("ej",p)<p:getEquips():length()/2 then
			local n = p:getCards("ej"):length()
			if n<1 then continue end
			local ids = {}
			for _,c in sgs.list(cards)do
				--card:addSubcard(c)
				table.insert(ids,c:getEffectiveId())
				if #ids>=n then--card:subcardsLength()>=n then
					use.card = sgs.Card_Parse("#mobile_dingzouCard:"..table.concat(ids,"+")..":")
					use.to:append(p)
					return
				end
			end
			--card:clearSubcards()
		end
	end
end

sgs.ai_use_value.mobile_dingzouCard = 4.5
sgs.ai_use_priority.mobile_dingzouCard = 4.7

sgs.ai_skill_playerchosen.mobile_zhimeng = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard",true)
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		and target:getHandcardNum()>self.player:getHandcardNum()
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and self:isWeak(target)
		and target:getHandcardNum()<self.player:getHandcardNum()/2
		then return target end
	end
end


sgs.ai_fill_skill.yanduanbi = function(self)
	return sgs.Card_Parse("#yanduanbiCard:.:")
end

sgs.ai_skill_use_func["#yanduanbiCard"] = function(card,use,self)
	for _,p in sgs.list(self.friends)do
		if self:isWeak(p) and p:getHandcardNum()<3 then
			use.card = card
			break
		end
	end
end

sgs.ai_use_value.yanduanbiCard = 4.5
sgs.ai_use_priority.yanduanbiCard = 5.7

sgs.ai_skill_playerchosen.yanduanbi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_playerchosen.yantongdu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getCardCount()>2
		then return target end
	end
end

sgs.ai_skill_playerchosen.yandiaodu = function(self,players)
	sgs.ai_skill_invoke.peiqi(self)
    for _,target in sgs.list(players)do
		if target==self.peiqiData.from
		then return target end
	end
end

sgs.ai_skill_cardchosen.yandiaodu = function(self,who,flags,method)
	for _,e in sgs.list(who:getCards(flags))do
		local id = e:getEffectiveId()
		if id==self.peiqiData.cid
		then return id end
	end
end

sgs.ai_skill_playerchosen.yandiaodu_to = function(self,players)
    for _,target in sgs.list(players)do
		if target==self.peiqiData.to
		then return target end
	end
end

sgs.ai_skill_invoke.yandiancai = function(self)
	return true
end

function canZhengsu(self,player)
	player = player or self.player
    local handcards = sgs.QList2Table(player:getCards("h"))
    self:sortByUsePriority(handcards)
	local tocs,ns = {},sgs.IntList()
   	for d,h in sgs.list(handcards)do
		if h:isAvailable(player) then
			d = self:aiUseCard(h)
			if d.card and d.to
			then table.insert(tocs,h) end
		end
	end
   	for d,h in sgs.list(tocs)do
		if ns:contains(h:getNumber()) then continue end
		ns:append(h:getNumber())
	end
	if ns:length()>2 then
		self.zhengsu_choice = "zhengsu1"
		return true
	end
   	for d,h in sgs.list(tocs)do
		ns = 0
		for _,h1 in sgs.list(tocs)do
			if h:getSuit()==h1:getSuit()
			then ns = ns+1 end
		end
		if ns>2 then
			self.zhengsu_choice = "zhengsu2"
			return true
		end
	end
	if #handcards-player:getMaxCards()>2
	and #handcards-player:getMaxCards()<5 then
		self.zhengsu_choice = "zhengsu3"
		return true
	end
end

sgs.ai_skill_choice.zhengsu = function(self,choices)
	return self.zhengsu_choice
end

sgs.ai_skill_invoke.yanyanji = function(self)
	return canZhengsu(self)
end

sgs.ai_skill_invoke.yantaoluan = function(self,data)
	local judge = data:toJudge()
	if self:isFriend(judge.who) then
		return judge:isBad()
	elseif self:isEnemy(judge.who) then
		return judge:isGood()
	end
	return self.player:getHandcardNum()<=self.player:getMaxCards()
end

sgs.ai_skill_choice.yantaoluan = function(self,choices,data)
	local items = choices:split("+")
	local judge = data:toJudge()
	if self:isEnemy(judge.who) then
		local dc = dummyCard("fire_slash")
		dc:setSkillName("_"..self:objectName())
		if self.player:canSlash(judge.who,dc,false) then
			return "yantaoluan2"
		end
	end
	return items[1]
end

sgs.ai_skill_invoke.yanshiji = function(self,data)
	local to = data:toPlayer()
	return self:isEnemy(to)
	or not self:isFriend(to) and #self.friends_noself<1
end

sgs.ai_skill_invoke.yanzhengjun = function(self)
	return canZhengsu(self)
end

sgs.ai_skill_playerchosen.yanzhengjun = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) then
			return target
		end
	end
end

sgs.ai_fill_skill.yanyangjie = function(self)
	return sgs.Card_Parse("#yanyangjieCard:.:")
end

sgs.ai_skill_use_func["#yanyangjieCard"] = function(card,use,self)
	local nc = self:getMinCard()
	if nc and nc:getNumber()<5 then
		self:sort(self.enemies)
		self.yanyangjie_card = nc
		for _,p in sgs.list(self.enemies)do
			if self.player:canPindian(p) then
				self.yanyangjieTo = p
				use.card = card
				use.to:append(p)
				break
			end
		end
	end
end

sgs.ai_use_value.yanyangjieCard = 4.5
sgs.ai_use_priority.yanyangjieCard = 2.7

sgs.ai_skill_playerchosen.yanyangjie = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) then
			for _,sk in ipairs(sgs.getPlayerSkillList(target)) do
				local ts = sgs.Sanguosha:getTriggerSkill(sk:objectName())
				if ts and (ts:hasEvent(sgs.DamageCaused) or ts:hasEvent(sgs.Damage)) then
					return target
				end
			end
		end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) then
			local can = true
			for _,sk in ipairs(sgs.getPlayerSkillList(self.yanyangjieTo)) do
				local ts = sgs.Sanguosha:getTriggerSkill(sk:objectName())
				if ts and (ts:hasEvent(sgs.DamageInflicted) or ts:hasEvent(sgs.Damaged)) then
					can = false
					break
				end
			end
			if can then
				return target
			else
				for _,p in sgs.list(destlist)do
					if self:isEnemy(p) then
						return p
					end
				end
			end
		end
	end
	return destlist[1]
end

sgs.ai_skill_invoke.yanjuxiang = function(self,data)
	local to = data:toPlayer()
	return to and self:isEnemy(to)
end

sgs.ai_skill_invoke.yanhoufeng = function(self,data)
	local to = data:toPlayer()
	return canZhengsu(self,to)
end

sgs.ai_skill_use["@@olyuheng!"] = function(self,prompt)
	local valid = {}
	local ids = {}
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards) -- 按保留值排序
	local n = self.player:getMark("mouqingzhengSuits")
   	for _,c in sgs.list(cards)do
		if table.contains(ids,c:getSuit()) then continue end
		table.insert(valid,c:getEffectiveId())
		table.insert(ids,c:getSuit())
	end
	if #valid<1 then return end
   	return "#olyuhengCard:"..table.concat(valid,"+")..":"
end

sgs.ai_skill_choice.oldili = function(self,choices,data)
	local items = choices:split("+")
	local n = data:toInt()
	if n>=3 then
		return items[#items]
	end
	return items[2]
end

sgs.ai_skill_invoke.zhaoluan = function(self,data)
	local to = data:toPlayer()
	return not self:isEnemy(to)
	or #self.friends/2>=#self.enemies
end

sgs.ai_fill_skill.zhaoluan = function(self)
	return sgs.Card_Parse("#zhaoluanCard:.:")
end

sgs.ai_skill_use_func["#zhaoluanCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,p in sgs.list(self.enemies)do
		if p:getMark("zhaoluanDamage"..self.player:objectName())<1 and self:isWeak()
		and self:damageIsEffective(p,nil,self.player) then
			use.card = card
			use.to:append(p)
			break
		end
	end
end

sgs.ai_use_value.zhaoluanCard = 4.5
sgs.ai_use_priority.zhaoluanCard = 1.7

sgs.ai_skill_cardask.mobilejingxie = function(self,data,pattern,prompt)
    return true
end

local jx_equip = {"crossbow","eight_diagram","renwang_shield","silver_lion","vine"}

sgs.ai_fill_skill.mobilejingxie = function(self)
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards,true) -- 按保留值排序
   	for _,c in sgs.list(cards)do
		if table.contains(jx_equip,c:objectName())
		and not self.player:hasFlag("jx_equip"..c:objectName()) and self:aiUseCard(c).card then 
			self.jx_equip_name = c:objectName()
			return sgs.Card_Parse("#mobilejingxieCard:"..c:getId()..":")
		end
	end
end

sgs.ai_skill_use_func["#mobilejingxieCard"] = function(card,use,self)
	self.player:setFlags("jx_equip"..self.jx_equip_name)
	use.card = card
end

sgs.ai_use_value.mobilejingxieCard = 4.5
sgs.ai_use_priority.mobilejingxieCard = 7.7

sgs.ai_fill_skill.mobileqiaosi = function(self)
	sgs.ai_use_priority.mobileqiaosiCard = 8-self.player:getHandcardNum()
	return sgs.Card_Parse("#mobileqiaosiCard:.:")
end

sgs.ai_skill_use_func["#mobileqiaosiCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.mobileqiaosiCard = 4.5
sgs.ai_use_priority.mobileqiaosiCard = 8.7

sgs.ai_skill_choice.qs_shuizhuan = function(self,choices,data)
	local items = choices:split("+")
   	for _,t in sgs.list(table.copyFrom(items))do
		if string.find(t,"100") then table.removeOne(items,t) end
	end
	if #items>0 then
		for _,t in sgs.list(items)do
			if string.find(t,"qs_wang") or string.find(t,"qs_wang")
			then return t end
		end
		for _,t in sgs.list(items)do
			if string.find(t,"qs_shang") or string.find(t,"qs_shi")
			then return t end
		end
		return items[math.random(1,#items)]
	end
end

sgs.ai_skill_use["@@mobileqiaosi!"] = function(self,prompt)
	local valid = {}
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards) -- 按保留值排序
	local n = self.player:getMark("mobileqiaosiNum")
   	for _,c in sgs.list(cards)do
		if #valid>=n or self:aiUseCard(c).card then continue end
		table.insert(valid,c:getEffectiveId())
	end
   	for _,c in sgs.list(cards)do
		if #valid>=n or table.contains(valid,c:getEffectiveId()) then continue end
		table.insert(valid,c:getEffectiveId())
	end
	if #valid<1 then return end
	self:sort(self.friends_noself)
   	for _,p in sgs.list(self.friends_noself)do
		return "#mobileqiaosi2Card:"..table.concat(valid,"+")..":->"..p:objectName()
	end
   	for _,c in sgs.list(cards)do
		if #valid>=n or self:aiUseCard(c).card or self.player:isJilei(c) then continue end
		table.insert(valid,c:getEffectiveId())
	end
   	for _,c in sgs.list(cards)do
		if #valid>=n or table.contains(valid,c:getEffectiveId()) or self.player:isJilei(c) then continue end
		table.insert(valid,c:getEffectiveId())
	end
	if #valid<1 then return end
	return "#mobileqiaosi2Card:"..table.concat(valid,"+")..":"
end

sgs.ai_skill_playerchosen.keoljianxuan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) then
			local h = p:getHandcardNum()+1
			if h==2+self.player:getMark("&keolgangshu_mp")
			or h==self.player:getAttackRange()
			or h==1+sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,dummyCard())
			then return p end
		end
	end
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) then
			local h = p:getHandcardNum()
			if h<2+self.player:getMark("&keolgangshu_mp")
			or h<self.player:getAttackRange()
			or h<1+sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,dummyCard())
			then return p end
		end
	end
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) then
			return p
		end
	end
end

sgs.ai_fill_skill.mobiledamingvs = function(self)
	return sgs.Card_Parse("#mobiledamingCard:.:")
end

sgs.ai_skill_use_func["#mobiledamingCard"] = function(card,use,self)
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	if #cards<1 then return end
    self:sortByKeepValue(cards) -- 按保留值排序
   	for _,p in sgs.list(self.friends_noself)do
		if p:hasSkill("mobiledaming")
		and self.player:getMark(p:objectName().."mobiledamingUse-PlayClear")<1
		then 
			use.card = sgs.Card_Parse("#mobiledamingCard:"..cards[1]:getId()..":")
			use.to:append(p)
			return
		end
	end
	cards = self:poisonCards(cards)
	if #cards<1 then return end
   	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if p:hasSkill("mobiledaming")
		and self.player:getMark(p:objectName().."mobiledamingUse-PlayClear")<1
		then 
			use.card = sgs.Card_Parse("#mobiledamingCard:"..cards[1]:getId()..":")
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.mobiledamingCard = 4.5
sgs.ai_use_priority.mobiledamingCard = 6.7

sgs.ai_fill_skill.mobilexiaoni = function(self)
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	if #cards<1 then return end
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,pn in sgs.list(patterns())do
		local dc = dummyCard(pn)
		if dc and dc:isDamageCard() and self:getCardsNum(dc:getClassName())<1
		and not dc:isKindOf("DelayedTrick") then
			dc:setSkillName("mobilexiaoni")
			for _,c in sgs.list(cards)do
				dc:addSubcard(c)
				if dc:isAvailable(self.player) then
					local d = self:aiUseCard(dc)
					if d.card then return dc end
				end
				dc:clearSubcards()
			end
		end
	end
end

sgs.ai_skill_playerchosen.jiebing = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and p:getCardCount()>0 then
			return p
		end
	end
    for _,p in sgs.list(destlist)do
		if p:getCardCount()>0 then
			return p
		end
	end
end

sgs.ai_fill_skill.hannan = function(self)
	return sgs.Card_Parse("#hannanCard:.:")
end

sgs.ai_skill_use_func["#hannanCard"] = function(card,use,self)
	local mc = self:getMaxCard()
	if not mc or mc:getNumber()<11 then return end
    self.hannan_card = mc
	self:sort(self.enemies)
   	for _,p in sgs.list(self.enemies)do
		if self.player:canPindian(p) then 
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.hannanCard = 4.5
sgs.ai_use_priority.hannanCard = 2.7

sgs.ai_fill_skill.keolsaogu = function(self)
	if self.player:getChangeSkillState("keolsaogu") == 1 then
		local cards = self.player:getCards("he")
		cards = self:sortByKeepValue(cards,nil,"jl") -- 按保留值排序
		if #cards<1 then return end
		local ids = {}
		for _,c in sgs.list(cards)do
			if #ids<2 and c:isKindOf("Slash") and self.player:getMark(c:getSuitString().."keolsaoguSuit-PlayClear")<1 then
				if self.player:hasUsed("Slash") then
					table.insert(ids,c:getEffectiveId())
				end
			end
		end
		for _,c in sgs.list(cards)do
			if #ids==1 and self.player:getMark(c:getSuitString().."keolsaoguSuit-PlayClear")<1
			and c:getEffectiveId()~=ids[1] then
				table.insert(ids,c:getEffectiveId())
			end
		end
		for _,c in sgs.list(cards)do
			if #ids<1 and self.player:getMark(c:getSuitString().."keolsaoguSuit-PlayClear")<1
			and #cards>3 then
				table.insert(ids,c:getEffectiveId())
			end
		end
		if #ids<2 then return end
		sgs.ai_use_priority.keolsaoguCard = 1.7
		return sgs.Card_Parse("#keolsaoguCard:"..table.concat(ids,"+")..":")
	else
		sgs.ai_use_priority.keolsaoguCard = 7.7
		return sgs.Card_Parse("#keolsaoguCard:.:")
	end
end

sgs.ai_skill_use_func["#keolsaoguCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.keolsaoguCard = 4.5
sgs.ai_use_priority.keolsaoguCard = 1.7

sgs.ai_skill_use["@@keolsaogu"] = function(self,prompt)
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards,nil,"j") -- 按保留值排序
	if #cards<2 then return end
	if self.player:getChangeSkillState("keolsaogu") == 1 then
		self:sort(self.friends_noself,nil,true)
		for _,p in sgs.list(self.friends_noself)do
			if p:getCardCount()>2 and (getCardsNum("Slash",p,self.player)>0 or #self:poisonCards("e",p)>0) then
				return "#keolsaogu2Card:"..cards[1]:getId()..":->"..p:objectName()
			end
		end
		self:sort(self.enemies)
		for _,p in sgs.list(self.enemies)do
			if p:getCardCount()>0 and getCardsNum("Slash",p,self.player)<1 and #self:poisonCards("e",p)<1 then
				return "#keolsaogu2Card:"..cards[1]:getId()..":->"..p:objectName()
			end
		end
	else
		self:sort(self.friends_noself)
		for _,p in sgs.list(self.friends_noself)do
			return "#keolsaogu2Card:"..cards[1]:getId()..":->"..p:objectName()
		end
	end
end

sgs.ai_skill_use["@@keolsaoguslash"] = function(self,prompt)
	local ids = self.player:getTag("keolsaoguslashForAI"):toIntList()
	for _,id in sgs.list(ids)do
		local c = sgs.Sanguosha:getCard(id)
		if c:isKindOf("Slash") and not self.player:isLocked(c) then
			local d = self:aiUseCard(c)
			if d.card then
				local tos = {}
				for _,p in sgs.list(d.to)do
					table.insert(tos,p:objectName())
				end
				return id.."->"..table.concat(tos,"+")
			end
		end
	end
end

sgs.ai_skill_discard.keolsaogu = function(self,x,n)
	local to_cards = {}
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards,nil,"j")
   	for _,c in sgs.list(cards)do
		if #to_cards>=n or self.player:getMark(c:getSuitString().."keolsaoguSuit-PlayClear")>0 then continue end
		if c:isKindOf("Slash") and self:aiUseCard(c).card then
			table.insert(to_cards,c:getEffectiveId())
		end
	end
   	for _,c in sgs.list(cards)do
		if #to_cards>=n or table.contains(to_cards,c:getEffectiveId()) then continue end
		if self.player:getMark(c:getSuitString().."keolsaoguSuit-PlayClear")>0 then continue end
		table.insert(to_cards,c:getEffectiveId())
	end
	return to_cards
end

sgs.ai_fill_skill.olxiaofan = function(self)
	local n = 1
	for _,m in sgs.list(self.player:getMarkNames())do
		if m:startsWith("&olxiaofan+:+") and self.player:getMark(m)>0 then
			n = n+#m:split("+")-2
			break
		end
	end
	if n<4 or self.player:getHandcardNum()<2 and #self.toUse<2 then
		if self.player:usedTimes("#olxiaofan0Card")<1 then
			return sgs.Card_Parse("#olxiaofan0Card:.:")
		end
		local pattern = self.player:property("olxiaofanPattern"):toString()
		if pattern~="" or #self.toUse>1 then
			for _,id in sgs.list(pattern:split("+"))do
				local c = sgs.Sanguosha:getCard(id)
				if self:aiUseCard(c).card then
					return sgs.Card_Parse("#olxiaofan0Card:.:")
				end
			end
		end
	end
end

sgs.ai_skill_use_func["#olxiaofan0Card"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.olxiaofan0Card = 4.5
sgs.ai_use_priority.olxiaofan0Card = 9.7

sgs.ai_skill_use["@@olxiaofan1"] = function(self,prompt)
	local pattern = self.player:property("olxiaofanPattern"):toString()
	local cs = {}
	for _,id in sgs.list(pattern:split("+"))do
		table.insert(cs,sgs.Sanguosha:getCard(id))
	end
	self:sortByDynamicUsePriority(cs)
	for _,c in sgs.list(cs)do
		local d = self:aiUseCard(c)
		if d.card then
			local tos = {}
			for _,p in sgs.list(d.to)do
				table.insert(tos,p:objectName())
			end
			return c:toString().."->"..table.concat(tos,"+")
		end
	end
end

sgs.ai_guhuo_card.olxiaofan = function(self,toname,class_name)
	if self.player:getMark("olxiaofanFailed-"..self.player:getPhase().."Clear")>0 then return end
	local n = 1
	for _,m in sgs.list(self.player:getMarkNames())do
		if m:startsWith("&olxiaofan+:+") and self.player:getMark(m)>0 then
			n = n+#m:split("+")-2
			break
		end
	end
	if n<4 or self.player:getHandcardNum()<2 and self:getCardsNum(class_name)<2 then
		self.player:addMark("olxiaofanFailed-"..self.player:getPhase().."Clear")
		return "#olxiaofanCard:.:"..toname
	end
end

sgs.ai_skill_use["@@olxiaofan2"] = function(self,prompt)
	local cs = {}
	for _,id in sgs.list(self.player:getTag("olxiaofanForAI"):toIntList())do
		table.insert(cs,sgs.Sanguosha:getCard(id))
	end
	local pattern = self.player:property("olxiaofanPattern"):toString()
	for _,c in sgs.list(cs)do
		for _,pn in sgs.list(pattern:split("+"))do
			if c:sameNameWith(pn) and not self.player:isLocked(c) then
				return "#olxiaofan0Card:"..c:getId()..":@@olxiaofan2"
			end
		end
	end
end

sgs.ai_skill_invoke.keoldulie = function(self,data)
	local use = data:toCardUse()
	if use.card:isDamageCard() then
		return self.player:getAttackRange()>1 and not self:isWeak()
	elseif self:isFriend(use.from) then
		return self.player:getAttackRange()>0
	end
	return self.player:getAttackRange()>1
end

sgs.ai_fill_skill.keolweilin = function(self)
	local cards = self:addHandPile("he")
	cards = sgs.QList2Table(cards)
	if #cards<1 then return end
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,pn in sgs.list(patterns())do
		local dc = dummyCard(pn)
		if dc and (dc:isKindOf("Analeptic") or dc:isKindOf("Slash"))
		and self:getCardsNum(dc:getClassName())<1 then
			dc:setSkillName("keolweilin")
			for _,c in sgs.list(cards)do
				dc:addSubcard(c)
				if dc:isAvailable(self.player) then
					local d = self:aiUseCard(dc)
					if d.card then return dc end
				end
				dc:clearSubcards()
			end
		end
	end
end

function sgs.ai_cardsview.keolweilin(self,class_name,player)
	local cards = self:addHandPile("he")
	cards = sgs.QList2Table(cards)
	if #cards<1 then return end
	self:sortByKeepValue(cards)
	local dc = dummyCard(class_name)
	dc:setSkillName("keolweilin")
	for i,c in sgs.list(cards)do
		dc:addSubcard(c)
		if not player:isLocked(dc)
		then return dc:toString() end
		dc:clearSubcards()
	end
end

sgs.ai_card_priority.keolweilin = function(self,card,v)
	if card:getSkillName()=="keolweilin" then
		return 0.5
	end
end

sgs.ai_skill_cardask["juqi0"] = function(self,data,pattern,prompt)
    local to = data:toPlayer()
	if self:isFriend(to) then
		local n = to:getChangeSkillState("juqi")
		if n==1 then return true end
	elseif self:isEnemy(to) then
		local n = to:getChangeSkillState("juqi")
		if n==2 and to:inMyAttackRange(self.player) and self:isWeak() then return true end
	end
end

sgs.ai_skill_playerchosen.fengtu = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,p in sgs.list(destlist)do
		if self:isFriend(p) then
			return p
		end
	end
end

sgs.ai_skill_invoke.taishi = function(self,data)
	local cp = self.room:getCurrent()
	return not self:isFriend(cp)
end

sgs.ai_skill_choice.xdtiantong = function(self,choices,data)
	local items = choices:split("+")
	if not self.player:faceUp() then
		return items[1]
	end
end

sgs.ai_fill_skill.xdyifuvs = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards) -- 按保留值排序
	for _,c in sgs.list(cards)do
		if c:getTypeId()==1 then
			for _,p in sgs.list(self.room:getAllPlayers())do
				if p:hasSkill("xdyifu") then
					local n = p:getChangeSkillState("xdyifu")
					local choice = ""
					if n==1 then choice = "lightning"
					elseif n==2 then choice = self.player:property("Suijiyingbian"):toString()
					elseif n==3 then choice = "iron_chain" end
					local dc = dummyCard(choice,"_xdyifu")
					if dc then
						dc:addSubcard(c)
						dc:setCanRecast(false)
						if dc:isAvailable(self.player) then
							local d = self:aiUseCard(dc)
							if d.card then
								self.xdyifuvsTo = p
								self.xdyifuvsUse = d
								return sgs.Card_Parse("#xdyifucard:"..c:getId()..":"..choice)
							end
						end
					end
				end
			end
		end
	end
end

sgs.ai_skill_use_func["#xdyifucard"] = function(card,use,self)
	if self.xdyifuvsTo then
		use.card = card
		use.to:append(self.xdyifuvsTo)
	end
end

sgs.ai_use_value.xdyifucard = 4.5
sgs.ai_use_priority.xdyifucard = 2.7

sgs.ai_skill_use["@@xdyifuvs"] = function(self,prompt)
	local d = self.xdyifuvsUse
	if d.card then
		local tos = {}
		for _,p in sgs.list(d.to)do
			table.insert(tos,p:objectName())
		end
		return d.card:toString().."->"..table.concat(tos,"+")
	end
end

sgs.ai_skill_playerchosen.xdtianjie = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and self:damageIsEffective(p,"T") then
			return p
		end
	end
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p) then
			return p
		end
	end
end

sgs.ai_skill_use["@@xdfangchan"] = function(self,prompt)
	local n = self.player:getMark("xdfangchanId")
	local dc = dummyCard(sgs.Sanguosha:getCard(n):objectName())
	dc:setSkillName("_xdfangchan")
	dc:setCanRecast(false)
	local d = self:aiUseCard(dc)
	if d.card then
		local tos = {}
		for _,p in sgs.list(d.to)do
			table.insert(tos,p:objectName())
		end
		return d.card:toString().."->"..table.concat(tos,"+")
	end
end

sgs.ai_skill_playerschosen._wuqibingfa = function(self,players,x,n)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
	local tos = {}
    for _,p in sgs.list(destlist)do
		if #tos<x and self:isFriend(p) and p:getCardCount()>0 then
			for _,q in sgs.list(self.enemies)do
				if p:canSlash(q) then
					table.insert(tos,p)
					break
				end
			end
		end
	end
    for _,p in sgs.list(destlist)do
		if #tos<x and not table.contains(tos,p) and not self:isEnemy(p) and p:getCardCount()>0 then
			for _,q in sgs.list(self.enemies)do
				if p:canSlash(q) then
					table.insert(tos,p)
					break
				end
			end
		end
	end
	return tos
end

sgs.ai_skill_use["@@_wuqibingfa!"] = function(self,prompt)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards) -- 按保留值排序
	for _,c in sgs.list(cards)do
		local dc = dummyCard()
		dc:setSkillName("_wuqibingfa")
		dc:addSubcard(c)
		if dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then
				local tos = {}
				for _,p in sgs.list(d.to)do
					table.insert(tos,p:objectName())
				end
				return d.card:toString().."->"..table.concat(tos,"+")
			end
		end
	end
	for _,c in sgs.list(cards)do
		local dc = dummyCard()
		dc:setSkillName("_wuqibingfa")
		dc:addSubcard(c)
		if dc:isAvailable(self.player) then
			for _,p in sgs.list(self.enemies)do
				if self.player:canSlash(p,dc) then
					return dc:toString().."->"..p:objectName()
				end
			end
			for _,p in sgs.list(self.room:getAllPlayers())do
				if self.player:canSlash(p,dc) and not self:isFriend(p) then
					return dc:toString().."->"..p:objectName()
				end
			end
			for _,p in sgs.list(self.room:getAllPlayers())do
				if self.player:canSlash(p,dc) then
					return dc:toString().."->"..p:objectName()
				end
			end
		end
	end
end

sgs.ai_skill_invoke.xdfenbo = function(self,data)
	return self:isWeak() or #self.friends<=#self.enemies/2
end

sgs.ai_fill_skill.xiongsuan = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards) -- 按保留值排序
	for _,c in sgs.list(cards)do
		return sgs.Card_Parse("#xiongsuanCard:"..c:getId()..":")
	end
end

sgs.ai_skill_use_func["#xiongsuanCard"] = function(card,use,self)
	self:sort(self.friends,nil,true)
	for _,p in sgs.list(self.friends)do
		for _,s in sgs.qlist(p:getVisibleSkillList()) do
			if p:getFrequency() == sgs.Skill_Limited and p:getMark(s:getLimitMark())<1 then
				use.card = card
				use.to:append(p)
				return
			end
		end
	end
	self:sort(self.enemies)
	for _,p in sgs.list(self.enemies)do
		if p:getHp()<2 and self:damageIsEffective(p) then
			use.card = card
			use.to:append(p)
			return
		end
	end
	if not self:isWeak() then
		use.card = card
		use.to:append(self.player)
	end
end

sgs.ai_use_value.xiongsuanCard = 4.5
sgs.ai_use_priority.xiongsuanCard = 7.7

sgs.ai_skill_choice.xiongsuan = function(self,choices,data)
	local items = choices:split("+")
	local to = data:toPlayer()
	if self:isFriend(to) then
		local skills = {}
		for _,s in sgs.qlist(p:getVisibleSkillList()) do
			if table.contains(items,s:objectName()) and p:getMark(s:getLimitMark())<1 then
				table.insert(skills,s:objectName())
			end
		end
		if table.contains(items,"luanwu") then
			return "luanwu"
		end
		if table.contains(items,"fencheng") then
			return "fencheng"
		end
		if table.contains(items,"xingshuai") and self:isWeak() then
			for _,p in sgs.list(self.friends_noself) do
				if p:getKingdom()=="wei" then
					return "xingshuai"
				end
			end
		end
		if table.contains(items,"xdfenbo") and self:isWeak(self.enemies) then
			return "xdfenbo"
		end
		if table.contains(items,"xiongsuan") then
			return "xiongsuan"
		end
		return items[1]
	else
		return items[#items]
	end
end

sgs.ai_skill_invoke._eight_diagram = function(self,data)
	return sgs.ai_skill_invoke.eight_diagram(self,data)
end

sgs.ai_skill_cardask["mobilejingxie0"] = function(self,data,pattern,prompt)
    return true
end

sgs.ai_fill_skill.olmoguofu = function(self)
	local cards = self:addHandPile("he")
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards) -- 按保留值排序
	local pns = self.player:property("olmoguofuPns"):toString():split("+")
	for _,c in sgs.list(cards)do
		if c:hasTip("olmoguofu") then
			for _,pn in sgs.list(RandomList(pns))do
				if self.player:getMark(pn.."olmoguofuBan-Clear")>0 then continue end
				local dc = dummyCard(pn)
				if dc and self:getCardsNum(dc:getClassName())<1 then
					dc:setSkillName("olmoguofu")
					dc:addSubcard(c)
					if dc:isAvailable(self.player) and self:aiUseCard(dc).card
					then return dc end
				end
			end
		end
	end
end

function sgs.ai_cardsview.olmoguofu(self,class_name,player)
	local cards = self:addHandPile("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	for _,c in sgs.list(cards)do
		if c:hasTip("olmoguofu") then
			local dc = dummyCard(class_name)
			if player:getMark(dc:objectName().."olmoguofuBan-Clear")>0 then continue end
			dc:setSkillName("olmoguofu")
			dc:addSubcard(c)
			return dc:toString()
		end
	end
end

sgs.ai_skill_invoke.olmomoubian = function(self,data)
	return not self:isWeak()
end

sgs.ai_skill_invoke.olmozhouxi = function(self,data)
	local stp = data:toString():split(":")
	local tp = BeMan(self.room,stp[2])
	return tp and self:isEnemy(tp)
end

sgs.ai_skill_use["ol_shengsiyugong0"] = function(self,prompt,method,pattern)
	local dy = self.player:getTag("ol_shengsiyugongData"):toDying()
	if self:isFriend(dy.who) and self:getAllPeachNum()<1 then
		for _,h in sgs.list(self:getCard("Shengsiyugong",true))do
			return h:toString()
		end
	end
end

sgs.ai_skill_use["ol_luojingxiashi0"] = function(self,prompt,method,pattern)
	local dy = self.player:getTag("ol_luojingxiashiData"):toDying()
	if self:isEnemy(dy.who) then
		for _,h in sgs.list(self:getCard("Luojingxiashi",true))do
			return h:toString()
		end
	end
end

function SmartAI:useCardHongyundangtou(card,use)
	self:sort(self.friends_noself,nil,true)
	for _,p in sgs.list(self.friends_noself)do
		if isCurrent(use,p) then continue end
		if self:doDisCard(p,"he") and self:canDraw(p) and CanToCard(card,self.player,p,use.to) then
	    	use.to:append(p)
	    	use.card = card
		end
	end
	for _,p in sgs.list(self.friends_noself)do
		if isCurrent(use,p) or use.to:contains(p) then continue end
		if p:canDiscard(p,"he") and self:canDraw(p) and CanToCard(card,self.player,p,use.to) then
	    	use.to:append(p)
	    	use.card = card
		end
	end
end
sgs.ai_use_priority.Hongyundangtou = 8.4
sgs.ai_keep_value.Hongyundangtou = 4
sgs.ai_use_value.Hongyundangtou = 7.7
sgs.ai_nullification.Hongyundangtou = function(self,trick,from,to,positive)
    return self:isEnemy(to)
	and to:getHandcardNum()<3
	and positive
end

sgs.ai_card_intention.Hongyundangtou = -66

sgs.ai_skill_discard.ol_hongyundangtou = function(self,x,n)
	local to_cards = {}
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards,nil,"j")
   	for i,c in sgs.list(cards)do
		if #to_cards>=x or table.contains(self.toUse,c) or i>#cards/2 then continue end
		table.insert(to_cards,c:getId())
	end
	return to_cards
end

function SmartAI:useCardYounantongdang(card,use)
	if #self.friends<=#self.enemies then
	   	use.card = card
	end
end
sgs.ai_use_priority.Younantongdang = 3.4
sgs.ai_keep_value.Younantongdang = 3
sgs.ai_use_value.Younantongdang = 3.7
sgs.ai_nullification.Younantongdang = function(self,trick,from,to,positive)
    return sgs.ai_nullification.IronChain(self,trick,from,to,positive)
end

function SmartAI:useCardLeigongzhuwo(card,use)
	if #self.friends<=#self.enemies then
	   	use.card = card
	end
end
sgs.ai_use_priority.Leigongzhuwo = 3.4
sgs.ai_keep_value.Leigongzhuwo = 3
sgs.ai_use_value.Leigongzhuwo = 1.7
--[[sgs.ai_nullification.Leigongzhuwo = function(self,trick,from,to,positive)
    return sgs.ai_nullification.Lightning(self,trick,from,to,positive)
end]]

function SmartAI:useCardLiangleichadao(card,use)
	self:sort(self.friends_noself,nil,true)
	for _,p in sgs.list(self.friends_noself)do
		if isCurrent(use,p) then continue end
		if self:canDraw(p) and CanToCard(card,self.player,p,use.to) then
	    	use.to:append(p)
	    	use.card = card
		end
	end
	for _,p in sgs.list(self.friends_noself)do
		if isCurrent(use,p) or use.to:contains(p) then continue end
		if self:canDraw(p) and CanToCard(card,self.player,p,use.to) then
	    	use.to:append(p)
	    	use.card = card
		end
	end
end
sgs.ai_use_priority.Liangleichadao = 8.4
sgs.ai_keep_value.Liangleichadao = 4
sgs.ai_use_value.Liangleichadao = 0.7
sgs.ai_nullification.Liangleichadao = function(self,trick,from,to,positive)
    return self:isEnemy(to) and positive
	and to:getHandcardNum()<3
end

sgs.ai_card_intention.Liangleichadao = -66

function SmartAI:useCardXiongdiqixin(card,use)
	self:sort(self.friends_noself,nil,true)
	for _,p in sgs.list(self.friends_noself)do
		if isCurrent(use,p) then continue end
		if self:canDraw(p) and CanToCard(card,self.player,p,use.to) then
	    	use.to:append(p)
	    	use.card = card
		end
	end
	for _,p in sgs.list(self.friends_noself)do
		if isCurrent(use,p) or use.to:contains(p) then continue end
		if self:canDraw(p) and CanToCard(card,self.player,p,use.to) then
	    	use.to:append(p)
	    	use.card = card
		end
	end
end
sgs.ai_use_priority.Xiongdiqixin = 8.4
sgs.ai_keep_value.Xiongdiqixin = 4
sgs.ai_use_value.Xiongdiqixin = 9.7
sgs.ai_nullification.Xiongdiqixin = function(self,trick,from,to,positive)
    return self:isEnemy(to) and positive and self:isWeak(to)
	and to:getHandcardNum()+from:getHandcardNum()>5
end

sgs.ai_card_intention.Xiongdiqixin = -66

sgs.ai_skill_use["@@ol_xiongdiqixin!"] = function(self,prompt)
	local cs = {}
	for _,id in sgs.list(self.player:getTag("ol_xiongdiqixinForAI"):toIntList())do
		table.insert(cs,sgs.Sanguosha:getCard(id))
	end
	for _,c in sgs.list(self.player:getHandcards())do
		table.insert(cs,c)
	end
	local ids = {}
    self:sortByKeepValue(cs) -- 按保留值排序
	for i,c in sgs.list(cs)do
		if i%2==1 then table.insert(ids,c:toString()) end
	end
	return "#ol_xiongdiqixinCard:"..table.concat(ids,"+")..":"
end

function SmartAI:useCardQianjiu(card,use)
	if #self.friends<=#self.enemies then
	   	use.card = card
	end
end
sgs.ai_use_priority.Qianjiu = 3.4
sgs.ai_keep_value.Qianjiu = 3
sgs.ai_use_value.Qianjiu = 3.7
sgs.ai_nullification.Qianjiu = function(self,trick,from,to,positive)
    return positive and (to==self.player or self:isWeak(to) and self:isFriend(to))
end

sgs.ai_skill_use["@@ol_qianjiu0"] = function(self,prompt)
	local cs = {}
	for _,c in sgs.list(self.player:getCards("he"))do
		table.insert(cs,c)
	end
    self:sortByUseValue(cs)
	for _,c in sgs.list(cs)do
		if c:isKindOf("Analeptic") or c:getNumber()==9 then
			if c:isAvailable(self.player) then
				local d = self:aiUseCard(c)
				if d.card then
					local tos = {}
					for _,p in sgs.list(d.to)do
						table.insert(tos,p:objectName())
					end
					return d.card:toString().."->"..table.concat(tos,"+")
				end
			end
		end
	end
end

function SmartAI:useCardWutianwujie(card,use)
	use.card = card
end
sgs.ai_use_priority.Wutianwujie = 3.4
sgs.ai_keep_value.Wutianwujie = 3
sgs.ai_use_value.Wutianwujie = 8.7
sgs.ai_nullification.Wutianwujie = function(self,trick,from,to,positive)
    return sgs.ai_nullification.ExNihilo(self,trick,from,to,positive)
end

sgs.ai_card_intention.Wutianwujie = -66

sgs.ai_fill_skill.ofsifen = function(self)
	return sgs.Card_Parse("#ofsifenCard:.:")
end

sgs.ai_skill_use_func["#ofsifenCard"] = function(card,use,self)
	local n = self.player:getMark("ofsifenRed-PlayClear")
	if n>0 then
		local cards = self:addHandPile("he")
		cards = sgs.QList2Table(cards)
		self:sortByKeepValue(cards) -- 按保留值排序
		local ids = {}
		local dc = dummyCard("duel","ofsifen")
		for _,c in sgs.list(cards)do
			if c:isRed() and #ids<n then
				table.insert(ids,c:toString())
				dc:addSubcard(c)
			end
		end
		if #ids==n and dc:isAvailable(self.player) then
			local btps = {}
			for _,p in sgs.list(self.room:getAllPlayers())do
				if self.player:getMark(p:objectName().."ofsifenDuel-PlayClear")<1
				then table.insert(btps,p) end
			end
			local d = self:aiUseCard(dc,dummy(nil,0,btps))
			if d.card then
				use.to = d.to
				use.card = sgs.Card_Parse("#ofsifenCard:"..table.concat(ids,"+")..":")
			end
		end
		return
	end
	self:sort(self.enemies)
   	for _,p in sgs.list(self.enemies)do
		if p:getHandcardNum()>0 then 
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.ofsifenCard = 4.5
sgs.ai_use_priority.ofsifenCard = 2.7

sgs.ai_skill_use["@@ofsifen!"] = function(self,prompt)
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
    self:sortByKeepValue(cards) -- 按保留值排序
	local dc = dummyCard("duel","_ofsifen")
   	for _,c in sgs.list(cards)do
		dc:addSubcard(c)
		if dc:subcardsLength()>=#cards/2 then
			if dc:isAvailable(self.player) then
				local d = self:aiUseCard(dc)
				if d.card then
					local tos = {}
					for _,p in sgs.list(d.to)do
						table.insert(tos,p:objectName())
					end
					return dc:toString().."->"..table.concat(tos,"+")
				end
			end
			break
		end
	end
   	for _,c in sgs.list(cards)do
		dc:clearSubcards()
		dc:addSubcard(c)
		if dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then
				local tos = {}
				for _,p in sgs.list(d.to)do
					table.insert(tos,p:objectName())
				end
				return dc:toString().."->"..table.concat(tos,"+")
			end
			for _,p in sgs.list(self.room:getAllPlayers())do
				if CanToCard(dc,self.player,p) then
					return dc:toString().."->"..p:objectName()
				end
			end
			break
		end
	end
end

sgs.ai_skill_invoke.offunan = function(self,data)
	for _,p in sgs.list(self.friends_noself)do
		if p:getKingdom()=="shu" and getCardsNum("Slash",p,self.player)>0 then
			return true
		end
	end
	return not self:isWeak()
end

sgs.ai_fill_skill.offunan = function(self)
	return sgs.ai_skill_invoke.offunan(self)
	and sgs.Card_Parse("#offunanCard:.:")
end

sgs.ai_skill_use_func["#offunanCard"] = function(card,use,self)
	local dc = dummyCard("slash")
	if dc:isAvailable(self.player) then
		for _,p in sgs.list(self:aiUseCard(dc).to)do
			use.card = card
			use.to:append(p)
		end
	end
end

sgs.ai_use_value.offunanCard = 4.5
sgs.ai_use_priority.offunanCard = 2.7

function sgs.ai_cardsview.ofjunwei(self,class_name,player)
	local cards = self:addHandPile("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	for i,c in sgs.list(cards)do
		for n,a in sgs.list(cards)do
			if i<=n then continue end
			local dc = dummyCard("nullification")
			dc:setSkillName("ofjunwei")
			dc:addSubcard(c)
			dc:addSubcard(a)
			return dc:toString()
		end
	end
end

sgs.ai_skill_playerschosen.ofjunwei = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local tps = {}
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) then
			table.insert(tps,p)
		end
	end
    return tps
end

sgs.ai_skill_invoke.ofshezuo = function(self,data)
	return not self:isWeak()
end

sgs.ai_fill_skill.ofshezuo = function(self)
	return #self.enemies>0 and sgs.Card_Parse("#ofshezuoCard:.:")
end

sgs.ai_skill_use_func["#ofshezuoCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.ofshezuoCard = 4.5
sgs.ai_use_priority.ofshezuoCard = 6.7

sgs.ai_skill_playerchosen.ofshezuo = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if self:isEnemy(p) then
			return p
		end
	end
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p) then
			return p
		end
	end
end

sgs.ai_skill_askforag.ofshezuo = function(self,card_ids)
	local cards = {}
    for _,id in sgs.list(card_ids)do
		table.insert(cards,sgs.Sanguosha:getCard(id))
    end
    self:sortByKeepValue(cards,true) -- 按保留值排序
   	for _,c in sgs.list(cards)do
		local dc = dummyCard(c:objectName())
		dc:setSkillName("_mourende")
		if dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then
				self.ofshezuoUse = d
				return c:getId()
			end
		end
	end
end

sgs.ai_skill_use["@@ofshezuo!"] = function(self,prompt)
	local dummy = self.ofshezuoUse
	if dummy.card then
		local tos = {}
		for _,p in sgs.list(dummy.to)do
			table.insert(tos,p:objectName())
		end
		return dummy.card:toString().."->"..table.concat(tos,"+")
	end
end

sgs.ai_fill_skill.ofjixu = function(self)
	return sgs.Card_Parse("#ofjixuCard:.:")
end

sgs.ai_skill_use_func["#ofjixuCard"] = function(card,use,self)
	local tps = self:sort(self.room:getAllPlayers())
   	for _,p in sgs.list(tps)do
		if p:getMark("ofjixuTo-PlayClear")<1 and use.to:length()<2
		and self:isFriend(p) and self:canDraw(p) then 
			use.to:append(p)
		end
	end
   	for _,p in sgs.list(tps)do
		if p:getMark("ofjixuTo-PlayClear")<1 and use.to:length()<2
		and not use.to:contains(p) and self:isFriend(p) then 
			use.to:append(p)
		end
	end
   	for _,p in sgs.list(tps)do
		if p:getMark("ofjixuTo-PlayClear")<1 and use.to:length()<2
		and not use.to:contains(p) and not self:isEnemy(p) then 
			use.to:append(p)
		end
	end
   	for _,p in sgs.list(tps)do
		if p:getMark("ofjixuTo-PlayClear")<1 and use.to:length()==1
		and not use.to:contains(p) then 
			use.to:append(p)
		end
	end
	if use.to:length()==2 then
		use.card = card
	end
end

sgs.ai_use_value.ofjixuCard = 4.5
sgs.ai_use_priority.ofjixuCard = 7.7

sgs.ai_skill_use["@@ofjixu!"] = function(self,prompt)
	local cs = {}
	for _,id in sgs.list(self.player:getTag("ofjixuForAI"):toIntList())do
		table.insert(cs,sgs.Sanguosha:getCard(id))
	end
	local aps = {}
	for _,p in sgs.list(self.room:getAllPlayers())do
		if p:hasFlag("ofjixuTo") then table.insert(aps,p) end
	end
	local ids = {}
	local tps = {}
	for i = 1,#cs do
		local c,p = self:getCardNeedPlayer(cs,nil,aps)
		if c and p then
			table.insert(ids,c:toString())
			table.insert(tps,p:objectName())
			table.removeOne(cs,c)
			table.removeOne(aps,p)
			if #cs<1 or #aps<1 then break end
		end
	end
	self:sort(aps)
	for i,p in sgs.list(aps)do
		table.insert(tps,p:objectName())
	end
    self:sortByKeepValue(cs) -- 按保留值排序
	for i,c in sgs.list(cs)do
		table.insert(ids,c:toString())
	end
	return "#ofjixuCard:"..table.concat(ids,"+")..":->"..table.concat(tps,"+")
end

sgs.ai_fill_skill.ofyouchong = function(self)
	return #self.friends_noself>0
	and sgs.Card_Parse("#ofyouchongCard:.:")
end

sgs.ai_skill_use_func["#ofyouchongCard"] = function(card,use,self)
   	for _,p in sgs.list(patterns())do
		local dc = dummyCard(p)
		if dc and dc:getTypeId()==1 and dc:isAvailable(self.player) then
			local d = self:aiUseCard(dc)
			if d.card then
				use.card = sgs.Card_Parse("#ofyouchongCard:.:"..p)
				use.to = d.to
				break
			end
		end
	end
end

sgs.ai_use_value.ofyouchongCard = 4.5
sgs.ai_use_priority.ofyouchongCard = 7.7

sgs.ai_skill_playerschosen.ofyouchong = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local tps = {}
    for _,p in sgs.list(destlist)do
		if not self:isEnemy(p) then
			table.insert(tps,p)
		end
	end
    return tps
end


sgs.ai_skill_cardask["@@ofyouchong"] = function(self,data)
    local tp = data:toPlayer()
	if self:isEnemy(tp) or #self.enemies<1 then return "." end
	local cn = self.player:property("ofyouchongCN"):toString()
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	local dc = dummyCard(cn,"_ofyouchong")
	for _,h in sgs.list(cards)do
		dc:addSubcard(h)
		if dc:subcardsLength()>2
    	then return dc:toString() end
	end
    return "."
end

sgs.ai_fill_skill.jiechu = function(self)
	return dummyCard("snatch","jiechu")
end

sgs.ai_skill_cardask["jiechu1"] = function(self,data)
	local use = data:toCardUse()
	if self.player:getMark(use.card:getSuitString().."daojueSuit")<1 then return "." end
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		return dc:toString()
	end
    return "."
end

sgs.ai_skill_invoke.tuonan = function(self,data)
	return self:getCardsNum("Peach,Analeptic")+self.player:getHp()<1
end

sgs.ai_skill_playerchosen.yingzhen = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
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

sgs.ai_skill_invoke.yuanjue = function(self,data)
	return self:getOverflow()>0
end

sgs.ai_skill_invoke.aoyong = function(self,data)
	return self:canDraw() or self.player:isWounded()
end

sgs.ai_skill_choice.aoyong = function(self,choices,data)
	local items = choices:split("+")
	if table.contains(items,"aoyong2") and self:isWeak() then
		return "aoyong2"
	end
	if table.contains(items,"aoyong3") then
		for _,c in sgs.list(self.player:getHandcards())do
			if self:aiUseCard(c) then
				return "aoyong3"
			end
		end
	end
	if table.contains(items,"aoyong2") then
		return "aoyong2"
	end
	if table.contains(items,"aoyong1") then
		return "aoyong1"
	end
end

sgs.ai_skill_invoke.tongkai = function(self,data)
	local to = data:toPlayer()
	return not self:isEnemy(to)
end

sgs.ai_skill_invoke.dianmo = function(self,data)
	return self:canDraw() or self.player:isWounded()
end

sgs.ai_fill_skill.zaibi = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards) -- 按保留值排序
	while #cards>0 do
		local cs = {}
		for _,c in sgs.list(cards)do
			if table.contains(self.toUse,c) then continue end
			local has = false
			for _,t in sgs.list(cs)do
				if t:getNumber()==c:getNumber() then
					has = true
					break
				end
			end
			if has then continue end
			if #cs>0 then
				for _,t in sgs.list(cs)do
					if math.abs(t:getNumber()-c:getNumber())==1 then
						has = true
						break
					end
				end
				if not has then continue end
			end
			table.insert(cs,c)
		end
		local ids = {}
		for _,c in sgs.list(cs)do
			table.insert(ids,c:getId())
		end
		if #ids>1 then
			return sgs.Card_Parse("#zaibiCard:"..table.concat(ids,"+")..":")
		end
		table.removeOne(cards,cs[1])
	end
end

sgs.ai_skill_use_func["#zaibiCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.zaibiCard = 4.5
sgs.ai_use_priority.zaibiCard = 7.7

sgs.ai_fill_skill._chunqiubi = function(self)
	return sgs.Card_Parse("#_chunqiubiCard:.:")
end

sgs.ai_skill_use_func["#_chunqiubiCard"] = function(card,use,self)
	local tps = self:sort(self.room:getAllPlayers())
   	for _,p in sgs.list(tps)do
		if not self:isFriend(p) then 
			use.to:append(p)
			use.card = card
			break
		end
	end
end

sgs.ai_use_value._chunqiubiCard = 4.5
sgs.ai_use_priority._chunqiubiCard = 7.7








