--宝箧
sgs.ai_skill_invoke.jinbaoqie = function(self,data)
	return not self.player:getTreasure()
end

sgs.ai_can_damagehp.jinbaoqie = function(self,from,card,to)
	return to:inYinniState()
end

--宜室
sgs.ai_skill_use["@@jinyishi"] = function(self,data)
	local card_ids = self.player:getTag("jinyishiForAI"):toIntList()
	card_ids = sgs.QList2Table(card_ids)
	local who = self.player:getTag("jinyishi_from"):toPlayer()

	if self:isLihunTarget(self.player,#card_ids-1) then return "." end
	local invoke = (self:isFriend(who) and not (who:hasSkill("kongcheng") and who:isKongcheng()))
					or (#card_ids>=3 and not self.player:hasSkill("manjuan"))
					or (#card_ids==2 and not self:hasSkills(sgs.cardneed_skill,who) and not self.player:hasSkill("manjuan"))
					or (self:isEnemy(who) and who:hasSkill("kongcheng") and who:isKongcheng())
	if not invoke then return "." end

	local wulaotai = self.room:findPlayerBySkillName("buyi")
	local Need_buyi = wulaotai and who:getHp()==1 and self:isFriend(who,wulaotai)

	local cards,except_Equip,except_Key = {},{},{}
	for _,card_id in sgs.list(card_ids)do
		local card = sgs.Sanguosha:getCard(card_id)
		if self.player:hasSkills("zhijian|mobilezhijianzhan") and not card:isKindOf("EquipCard") then
			table.insert(except_Equip,card)
		end
		if not card:isKindOf("Peach") and not card:isKindOf("Jink") and not card:isKindOf("Analeptic") and
			not card:isKindOf("Nullification") and not (card:isKindOf("EquipCard") and self.player:hasSkills("zhijian|mobilezhijianzhan")) then
			table.insert(except_Key,card)
		end
		table.insert(cards,card)
	end

	if self:isFriend(who) then

		if Need_buyi then
			local buyicard1,buyicard2
			self:sortByKeepValue(cards)
			for _,card in sgs.list(cards)do
				if card:isKindOf("TrickCard") and not buyicard1 then
					buyicard1 = card:getEffectiveId()
				end
				if not card:isKindOf("BasicCard") and not buyicard2 then
					buyicard2 = card:getEffectiveId()
				end
				if buyicard1 then break end
			end
			if buyicard1 then
				return "@JinYishiCard="..buyicard1
			elseif buyicard2 then
				return "@JinYishiCard="..buyicard2
			end
		end

		local peach_num,peach,jink,analeptic,slash = 0,nil,nil,nil,nil
		for _,card in sgs.list(cards)do
			if card:isKindOf("Peach") then peach = card:getEffectiveId() peach_num = peach_num+1 end
			if card:isKindOf("Jink") then jink = card:getEffectiveId() end
			if card:isKindOf("Analeptic") then analeptic = card:getEffectiveId() end
			if card:isKindOf("Slash") then slash = card:getEffectiveId() end
		end
		if peach then
			if peach_num>1
				or (self:getCardsNum("Peach")>=self.player:getMaxCards())
				or (who:getHp()<getBestHp(who) and who:getHp()<self.player:getHp()) then
					return "@JinYishiCard="..peach
			end
		end
		if self:isWeak(who) and (jink or analeptic) then
			if jink then
				return "@JinYishiCard="..jink
			elseif analeptic then
				return "@JinYishiCard="..analeptic
			end
		end

		for _,card in sgs.list(cards)do
			if not card:isKindOf("EquipCard") then
				for _,askill in sgs.qlist(who:getVisibleSkillList(true))do
					local callback = sgs.ai_cardneed[askill:objectName()]
					if type(callback)=="function" and callback(who,card,self) then
						return "@JinYishiCard="..card:getEffectiveId()
					end
				end
			end
		end

		if jink or analeptic or slash then
			if jink then
				return "@JinYishiCard="..jink
			elseif analeptic then
				return "@JinYishiCard="..analeptic
			elseif slash then
				return "@JinYishiCard="..slash
			end
		end

		for _,card in sgs.list(cards)do
			if not card:isKindOf("EquipCard") and not card:isKindOf("Peach") then
				return "@JinYishiCard="..card:getEffectiveId()
			end
		end

	else

		if Need_buyi then
			for _,card in sgs.list(cards)do
				if card:isKindOf("Slash") then
					return "@JinYishiCard="..card:getEffectiveId()
				end
			end
		end

		for _,card in sgs.list(cards)do
			if card:isKindOf("EquipCard") and self.player:hasSkills("zhijian|mobilezhijianzhan") then
				local Cant_Zhijian = true
				for _,friend in sgs.list(self.friends)do
					if not self:getSameEquip(card,friend) then
						Cant_Zhijian = false
					end
				end
				if Cant_Zhijian then
					return "@JinYishiCard="..card:getEffectiveId()
				end
			end
		end

		local new_cards = (#except_Key>0 and except_Key) or (#except_Equip>0 and except_Equip) or cards

		self:sortByKeepValue(new_cards)
		local valueless,slash
		for _,card in sgs.list (new_cards)do
			if card:isKindOf("Lightning") and not self:hasSkills(sgs.wizard_harm_skill,who) then
				return "@JinYishiCard="..card:getEffectiveId()
			end

			if card:isKindOf("Slash") then slash = card:getEffectiveId() end

			if not valueless and not card:isKindOf("Peach") then
				for _,askill in sgs.qlist(who:getVisibleSkillList(true))do
					local callback = sgs.ai_cardneed[askill:objectName()]
					if (type(callback)=="function" and not callback(who,card,self)) or not callback then
						valueless = card:getEffectiveId()
						break
					end
				end
			end
		end

		if slash or valueless then
			if slash then
				return "@JinYishiCard="..slash
			elseif valueless then
				return "@JinYishiCard="..valueless
			end
		end

		return "@JinYishiCard="..new_cards[1]:getEffectiveId()
	end
end

--识度
local jinshidu_skill = {}
jinshidu_skill.name = "jinshidu"
table.insert(sgs.ai_skills,jinshidu_skill)
jinshidu_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@JinShiduCard=.")
end

sgs.ai_skill_use_func.JinShiduCard = function(card,use,self)
	local max_card = self:getMaxCard()
	if max_card then
		local number = max_card:getNumber()
		if self.player:hasSkill("tianbian") and max_card:getSuit()==sgs.Card_Heart then number = 13 end
		if number>=7 then
			self:sort(self.enemies,"handcard")
			self.enemies = sgs.reverse(self.enemies)
			for _,p in sgs.list(self.enemies)do
				if math.floor((p:getHandcardNum()+self.player:getHandcardNum())/2)<self.player:getHandcardNum() then continue end
				if self:doDisCard(p,"h",true) and self.player:canPindian(p)
				then
					use.card = card
					self.jinshidu_card = max_card
					use.to:append(p)
					return
				end
			end
			
			self:sort(self.friends_noself,"handcard")
			for _,p in sgs.list(self.friends_noself)do
				if math.floor((p:getHandcardNum()+self.player:getHandcardNum())/2)<p:getHandcardNum()
				or p:hasSkill("kongcheng") and p:getHandcardNum()==1 then continue end
				if self:doDisCard(p,"h",true) and self.player:canPindian(p)
				then
					sgs.ai_use_priority.JinShiduCard = 5
					use.card = card
					self.jinshidu_card = max_card
					use.to:append(p)
					return
				end
			end
			for _,p in sgs.list(self.friends_noself)do
				if not self.player:canPindian(p) then continue end
				if math.floor((p:getHandcardNum()+self.player:getHandcardNum())/2)<p:getHandcardNum() then continue end
				--if self:needKongcheng(p,true) then continue end
				if p:hasSkill("kongcheng") and p:getHandcardNum()==1 then continue end
				sgs.ai_use_priority.JinShiduCard = 5
				use.card = card
				self.jinshidu_card = max_card
				use.to:append(p)
				return
			end
		end
	end
	
	local min_card = self:getMinCard()
	if min_card then
		local number = min_card:getNumber()
		if number<7 then
			self:sort(self.friends_noself,"handcard")
			for _,p in sgs.list(self.friends_noself)do
				if not self.player:canPindian(p) then continue end
				if not self:needToThrowLastHandcard(p) then continue end
				sgs.ai_use_priority.JinShiduCard = 5
				use.card = card
				self.jinshidu_card = min_card
				use.to:append(p)
				return
			end
			self.friends_noself = sgs.reverse(self.friends_noself)
			for _,p in sgs.list(self.friends_noself)do
				if not self.player:canPindian(p) then continue end
				if self:doDisCard(p,"h",true) and (not self:isWeak(p) or self:getEnemyNumBySeat(self.player,p,p)==0)
				then
					sgs.ai_use_priority.JinShiduCard = 5
					use.card = card
					self.jinshidu_card = min_card
					use.to:append(p)
					return
				end
			end
			if self:getOverflow(self.player,true)>0
			then
				self:sort(self.enemies,"handcard")
				for _,p in sgs.list(self.enemies)do
					if self:doDisCard(p,"h") and self.player:canPindian(p)
					then
						sgs.ai_use_priority.JinShiduCard = 0
						use.card = card
						self.jinshidu_card = min_card
						use.to:append(p)
						return
					end
				end
			end
		end
	end
end

sgs.ai_use_priority.JinShiduCard = 10.1

function sgs.ai_skill_pindian.jinshidu(minusecard,self,requestor)
	local maxcard = self:getMaxCard()
	return self:isFriend(requestor) and self:getMinCard() or ( maxcard:getNumber()<6 and  minusecard or maxcard )
end

sgs.ai_skill_discard.jinshidu = function(self,discard_num,min_num,optional,include_equip)
	local cards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByUseValue(cards,true)
	local dis = {}
	for _,c in sgs.list(cards)do
		if #dis>=min_num then break end
		table.insert(dis,c:getEffectiveId())
	end
	return dis
end

--韬隐
sgs.ai_skill_invoke.jintaoyin = function(self,data)
	local current = data:toPlayer()
	return self:isEnemy(current)
end

sgs.ai_can_damagehp.jintaoyin = function(self,from,card,to)
	return self:isEnemy(self.room:getCurrent())
	and to:inYinniState()
end

--夷灭
sgs.ai_skill_invoke.jinyimie = function(self,data)
	local str = data:toString():split(":")
	local to = self.room:findPlayerByObjectName(str[2])
	if not to or to:isDead() or not self:isEnemy(to) or tonumber(str[4])<1 then return false end
	if self:cantDamageMore(self.player,to) then return false end
	return true
end

--睿略
local jinruilve_give_skill = {}
jinruilve_give_skill.name = "jinruilve_give"
table.insert(sgs.ai_skills,jinruilve_give_skill)
jinruilve_give_skill.getTurnUseCard = function(self)
	if self:getOverflow(self.player,true)<=0 then return end
	return sgs.Card_Parse("@JinRuilveGiveCard=.")
end

sgs.ai_skill_use_func.JinRuilveGiveCard = function(card,use,self)
	local cards = {}
	for _,c in sgs.qlist(self.player:getCards("h"))do
		if c:isKindOf("Slash") or (c:isDamageCard() and c:isKindOf("TrickCard")) then
			table.insert(cards,c)
		end
	end
	if #cards==0 then return end
	self:sortByUseValue(cards,true)
	self:sort(self.friends_noself,"handcard")
	for _,p in sgs.list(self.friends_noself)do
		if not self:canDraw(p) or not p:hasLordSkill("jinruilve") or p:getMark("jinruilve-PlayClear")>0 then continue end
		use.card = sgs.Card_Parse("@JinRuilveGiveCard="..cards[1]:getEffectiveId())
		use.to:append(p)
		return
	end
end

sgs.ai_use_priority.JinRuilveGiveCard = 0

--慧容
sgs.ai_skill_playerchosen.jinhuirong = function(self,targets)
	local targets = sgs.QList2Table(targets)
	self:sort(targets,"handcard")
	for _,p in sgs.list(targets)do
		if self:canDraw(p) and self:isWeak(p)
		and self:isFriend(p)
		and p:getHandcardNum()<p:getHp()
		then return p end
	end
	local new_targets = {}
	for _,p in sgs.list(targets)do
		if self:isFriend(p) or self:isEnemy(p) then
			table.insert(new_targets,p)
		end
	end
	if #new_targets>0
	then
		local numsort = function(a,b)
			local c1,c2 = a:getHandcardNum()-a:getHp(),b:getHandcardNum()-b:getHp()
			if a:getHp()>a:getHandcardNum() then
				c1 = math.min(a:getHp(),5)-a:getHandcardNum()
			end
			if b:getHp()>b:getHandcardNum() then
				c2 = math.min(b:getHp(),5)-b:getHandcardNum()
			end
			c1 = math.abs(c1)
			c2 = math.abs(c2)
			return c1>c2
		end
		self:sort(targets)
		table.sort(targets,numsort)
		if self:isFriend(targets[1]) and targets[1]:getHandcardNum()<=targets[1]:getHp() then return targets[1] end
		if self:isEnemy(targets[1]) and targets[1]:getHandcardNum()>=targets[1]:getHp() then return targets[1] end
	end
	if self.player:getHp()>=self.player:getHandcardNum()
	then return self.player end
	for _,p in sgs.list(targets)do
		if self:isFriend(p) or self:isEnemy(p) then continue end
		return p
	end
	return self.player
end

sgs.ai_can_damagehp.jinhuirong = function(self,from,card,to)
	for _,p in sgs.list(self.friends)do
		if p:getHandcardNum()<p:getHp()
		and to:inYinniState()
		then return true end
	end
	for _,p in sgs.list(self.enemies)do
		if p:getHandcardNum()>p:getHp()
		and to:inYinniState()
		then return true end
	end
end

--慈威
sgs.ai_skill_cardask["@jinciwei-discard"] = function(self,data)
	local player = self.player:getTag("jinciwei-player"):toPlayer()
	if self:isEnemy(player) then
		local to_discard = self:askForDiscard("dummyreason",1,1,false,true)
		if #to_discard>0 then return "$"..to_discard[1] end
	end
	return "."
end

sgs.ai_skill_invoke.jinzhuosheng = function(self,data)
	return true
end

sgs.ai_skill_choice.jinzhuosheng = function(self,choices,data)
	local items = choices:split("+")
	local use = data:toCardUse()
	self.js_use = use
	if table.contains(items,"add")
	then
		if use.card:isDamageCard()
		or #self.friends_noself>0 and use.to:contains(self.player)
		then return "add" end
	end
	if table.contains(items,"remove")
	and use.card:isDamageCard()
	then
		for _,to in sgs.list(use.to)do
			if self:isFriend(to)
			then return "remove" end
		end
	end
	return "cancel"
end

sgs.ai_skill_playerchosen.jinzhuosheng = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
	if self.js_use.card:isDamageCard()
	then
		for _,target in sgs.list(destlist)do
			if self:isFriend(target)
			and self.js_use.to:contains(target)
			then return target end
		end
		for _,target in sgs.list(destlist)do
			if self:isEnemy(target)
			and not self.js_use.to:contains(target)
			and self:canCanmou(target,self.js_use)
			then return target end
		end
	else
		for _,target in sgs.list(destlist)do
			if self:isFriend(target)
			and self:canCanmou(target,self.js_use)
			then return target end
		end
	end
	return destlist[1]
end

sgs.ai_fill_skill.tousui = function(self)
    local cards = self.player:getCards("he")
	local dc = dummyCard()
	dc:setSkillName("tousui")
	if dc:isAvailable(self.player) then
		local d = self:aiUseCard(dc)
		if d.card then
			self.tousuiUse = d
			cards = self:sortByKeepValue(cards) -- 按保留值排序
			local ids = {}
			for i,c in sgs.list(cards)do
				if table.contains(self.toUse,c) and not c:isKindOf("Slash")
				or #ids>#cards/2 then continue end
				table.insert(ids,c:getId())
				if #ids>getCardsNum("Jink",d.to:at(0),self.player)
				then return sgs.Card_Parse("@TousuiCard="..table.concat(ids,"+")) end
			end
		end
	end
end

sgs.ai_skill_use_func["TousuiCard"] = function(card,use,self)
	use.card = card
	use.to = self.tousuiUse.to
end

sgs.ai_use_value.TousuiCard = 2.4
sgs.ai_use_priority.TousuiCard = 6.2

function sgs.ai_cardsview.tousui(self,class_name,player)
    local cards = player:getCards("he")
	cards = self:sortByKeepValue(cards) -- 按保留值排序
	if #cards>0 then
		return "@TousuiCard="..cards[1]:getId()
	end
end

sgs.ai_skill_choice.chuming = function(self,choices)
	local items = choices:split("+")
	self.chumingUse = nil
	local subcards = self.player:property("chumingSubcard"):toString():split("+");
	for _,pc in sgs.list(items)do
		local dc = dummyCard(pc)
		dc:setSkillName("_chuming")
		for _,id in sgs.list(subcards)do
			dc:addSubcard(id)
		end
		local d = self:aiUseCard(dc)
		if d.card then
			self.chumingUse = d
			return pc
		end
	end
end

sgs.ai_skill_use["@@chuming!"] = function(self,prompt)
    local d = self.chumingUse
   	if d and d.card then
      	local tos = {}
       	for _,p in sgs.list(d.to)do
       		table.insert(tos,p:objectName())
       	end
       	return d.card:toString().."->"..table.concat(tos,"+")
    end
end








