sgs.ai_skill_invoke.xingshang = true

function SmartAI:toTurnOver(to,n,reason) -- @todo: param of toTurnOver
	if not to then global_room:writeToConsole(debug.traceback()) return end
	n = n or 0
	if self:isEnemy(to) then
		local manchong = self.room:findPlayerBySkillName("junxing")
		if manchong and self:isFriend(to,manchong) and self:playerGetRound(manchong)<self:playerGetRound(to)
		and manchong:faceUp() and not self:willSkipPlayPhase(manchong)
		and not(manchong:isKongcheng() and self:willSkipDrawPhase(manchong))
		then return false end
	end
	if not to:faceUp() and not to:hasFlag("ShenfenUsing")
	and not to:hasFlag("guixinUsing") and not to:hasFlag("newguixinUsing")
	then return false end
	if reason=="fangzhu" and to:getHp()<=1 and self:playerGetRound(to)>self:playerGetRound(self.player)
	and sgs.cardEffect and sgs.cardEffect.card and sgs.cardEffect.card:isKindOf("AOE") then
		if to:isKongcheng() and self:aoeIsEffective(sgs.cardEffect.card,to) then return false end
	end
	if to:getPhase()<=sgs.Player_Play and (not to:hasUsed("ShenfenCard") and to:getMark("&wrath")>=6 or to:hasFlag("ShenfenUsing"))
	or to:hasUsed("ShenfenCard") and to:faceUp() then return false end
	if n>1 then
		if to:getPhase()~=sgs.Player_NotActive and (to:hasSkills(sgs.Active_cardneed_skill) or to:hasWeapon("Crossbow"))
		or to:getPhase()==sgs.Player_NotActive and to:hasSkills(sgs.notActive_cardneed_skill)
		then return false end
	end
	if to:hasSkills("jushou|neojushou|nosjushou|kuiwei") and to:getPhase()<=sgs.Player_Finish
	or to:hasSkill("lihun") and not to:hasUsed("LihunCard") and to:faceUp() and to:getPhase()<=sgs.Player_Play
	then return false end
	return true
end

sgs.ai_skill_playerchosen.fangzhu = function(self,targets)
	self:sort(self.friends_noself,"handcard")
	for _,friend in ipairs(self.friends_noself)do
		if not friend:faceUp() then
			return friend
		end
	end
	local n = self.player:getLostHp()
	for _,friend in ipairs(self.friends_noself)do
		if not self:toTurnOver(friend,n,"fangzhu") then
			return friend
		end
	end
	if n>=3 then
		local target = self:findPlayerToDraw(false,n)
		if target then return target end
		for _,enemy in ipairs(self.enemies)do
			if self:toTurnOver(enemy,n,"fangzhu") and hasManjuanEffect(enemy) then
				return enemy
			end
		end
	else
		self:sort(self.enemies)
		for _,enemy in ipairs(self.enemies)do
			if self:toTurnOver(enemy,n,"fangzhu") and hasManjuanEffect(enemy) then
				return enemy
			end
		end
		for _,enemy in ipairs(self.enemies)do
			if self:toTurnOver(enemy,n,"fangzhu") and enemy:hasSkills(sgs.priority_skill) then
				return enemy
			end
		end
		for _,enemy in ipairs(self.enemies)do
			if self:toTurnOver(enemy,n,"fangzhu") then
				return enemy
			end
		end
	end
end

sgs.ai_skill_playerchosen.songwei = function(self,targets)
	targets = sgs.QList2Table(targets)
	for _,target in ipairs(targets)do
		if self:isFriend(target) then
			return target
		end
	end
end

sgs.ai_playerchosen_intention.songwei = -50

sgs.ai_playerchosen_intention.fangzhu = function(self,from,to)
	if hasManjuanEffect(to) then sgs.updateIntention(from,to,80) end
	local intention = 80/math.max(from:getLostHp(),1)
	if not self:toTurnOver(to,from:getLostHp()) then intention = -intention end
	if from:getLostHp()<3 then
		sgs.updateIntention(from,to,intention)
	else
		sgs.updateIntention(from,to,math.min(intention,-30))
	end
end

sgs.ai_need_damaged.fangzhu = function (self,attacker,player)
	local enemies = self:getEnemies(player)
	if #enemies<1 then return false end
	self:sort(enemies,"defense")
	local n = player:getLostHp()
	for _,enemy in ipairs(enemies)do
		if n<1 and self:toTurnOver(enemy,n+1)
		then return true end
	end
	local friends = self:getFriends(player,true)
	self:sort(friends,"defense")
	for _,friend in ipairs(friends)do
		if not self:toTurnOver(friend,n+1)
		then return true end
	end
	return false
end



duanliang_skill={}
duanliang_skill.name="duanliang"
table.insert(sgs.ai_skills,duanliang_skill)
duanliang_skill.getTurnUseCard=function(self)
	local cards = self:addHandPile("he")
	local card
	self:sortByUseValue(cards,true)
	for _,acard in ipairs(cards)  do
		if (acard:isBlack()) and (acard:isKindOf("BasicCard") or acard:isKindOf("EquipCard")) and (self:getDynamicUsePriority(acard)<sgs.ai_use_value.SupplyShortage)then
			card = acard
			break
		end
	end
	if not card then return nil end
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	return sgs.Card_Parse(("supply_shortage:duanliang[%s:%s]=%d"):format(suit,number,card_id))
end

sgs.ai_cardneed.duanliang = function(to,card,self)
	return card:isBlack() and card:getTypeId()~=sgs.Card_TypeTrick and getKnownCard(to,self.player,"black",false)<2
end

sgs.duanliang_suit_value = {
	spade = 3.9,
	club = 3.9
}



sgs.ai_skill_invoke.zaiqi = function(self,data)
	local lostHp = self.player:hasSkills("rende|nosrende") and 3 or 2
	return self.player:getLostHp()>=lostHp
end

sgs.ai_cardneed.lieren = function(to,card,self)
	return isCard("Slash",card,to) and getKnownCard(to,self.player,"Slash",true)==0
end

sgs.ai_skill_invoke.lieren = function(self,data)
	local damage = data:toDamage()
	if not self:isEnemy(damage.to) then return false end

	if self.player:getHandcardNum()==1 then
		if (self:needKongcheng() or not self:hasLoseHandcardEffective()) and not self:isWeak() then return true end
		local card  = self.player:getHandcards():first()
		if card:isKindOf("Jink") or card:isKindOf("Peach") then return end
	end
	if self:doDisCard(damage.to,"he",true,2) then return true end
end

function sgs.ai_skill_pindian.lieren(minusecard,self,requestor)
	local cards = sgs.QList2Table(self.player:getHandcards())
	self:sortByKeepValue(cards)
	if requestor:objectName()==self.player:objectName() then
		return cards[1]:getId()
	end
	return self:getMaxCard(self.player):getId()
end

sgs.ai_skill_playerchosen.yinghun = function(self,targets)
	if self.player:hasFlag("AI_doNotInvoke_yinghun") then
		self.player:setFlags("-AI_doNotInvoke_yinghun")
		return
	end
	self.yinghunchoice = "d1tx"
	local x = self.player:getLostHp()
	if x==1 and #self.friends==1 then
		for _,enemy in ipairs(self.enemies)do
			if enemy:hasSkill("manjuan") then
				return enemy
			end
		end
		return nil
	end
	local n = x-1

	self.yinghun = nil
	local player = self:AssistTarget()

	if x==1 then
		self:sort(self.friends_noself,"handcard")
		self.friends_noself = sgs.reverse(self.friends_noself)
		for _,friend in ipairs(self.friends_noself)do
			if self:hasSkills(sgs.lose_equip_skill,friend) and friend:getCards("e"):length()>0
			  and not friend:hasSkill("manjuan") then
				self.yinghun = friend
				break
			end
		end
		if not self.yinghun then
			for _,friend in ipairs(self.friends_noself)do
				if friend:hasSkills("tuntian+zaoxian") and not friend:hasSkill("manjuan") then
					self.yinghun = friend
					break
				end
			end
		end
		if not self.yinghun then
			for _,friend in ipairs(self.friends_noself)do
				if self:needToThrowArmor(friend) and not friend:hasSkill("manjuan") then
					self.yinghun = friend
					break
				end
			end
		end
		if not self.yinghun then
			for _,enemy in ipairs(self.enemies)do
				if enemy:hasSkill("manjuan") then
					return enemy
				end
			end
		end

		if not self.yinghun and player and not player:hasSkill("manjuan") and player:getCardCount(true)>0 and not self:needKongcheng(player,true) then
			self.yinghun = player
		end

		if not self.yinghun then
			for _,friend in ipairs(self.friends_noself)do
				if friend:getCards("he"):length()>0 and not friend:hasSkill("manjuan") then
					self.yinghun = friend
					break
				end
			end
		end

		if not self.yinghun then
			for _,friend in ipairs(self.friends_noself)do
				if not friend:hasSkill("manjuan") then
					self.yinghun = friend
					break
				end
			end
		end
	elseif #self.friends>1 then
		self:sort(self.friends_noself,"chaofeng")
		for _,friend in ipairs(self.friends_noself)do
			if self:hasSkills(sgs.lose_equip_skill,friend) and friend:getCards("e"):length()>0
			  and not friend:hasSkill("manjuan") then
				self.yinghun = friend
				break
			end
		end
		if not self.yinghun then
			for _,friend in ipairs(self.friends_noself)do
				if friend:hasSkills("tuntian+zaoxian") and not friend:hasSkill("manjuan") then
					self.yinghun = friend
					break
				end
			end
		end
		if not self.yinghun then
			for _,friend in ipairs(self.friends_noself)do
				if self:needToThrowArmor(friend) and not friend:hasSkill("manjuan") then
					self.yinghun = friend
					break
				end
			end
		end
		if not self.yinghun and #self.enemies>0 then
			local wf
			if self.player:isLord() then
				if self:isWeak() and (self.player:getHp()<2 and self:getCardsNum("Peach")<1) then
					wf = true
				end
			end
			if not wf then
				for _,friend in ipairs(self.friends_noself)do
					if self:isWeak(friend) then
						wf = true
						break
					end
				end
			end

			if not wf then
				self:sort(self.enemies,"chaofeng")
				for _,enemy in ipairs(self.enemies)do
					if enemy:getCards("he"):length()==n
					and self:doDisCard(enemy,"nil",true,n) then
						self.yinghunchoice = "d1tx"
						return enemy
					end
				end
				for _,enemy in ipairs(self.enemies)do
					if enemy:getCards("he"):length()>=n
					and self:doDisCard(enemy,"nil",true,n)
					and self:hasSkills(sgs.cardneed_skill,enemy) then
						self.yinghunchoice = "d1tx"
						return enemy
					end
				end
			end
		end

		if not self.yinghun and player and not player:hasSkill("manjuan") and not self:needKongcheng(player,true) then
			self.yinghun = player
		end

		if not self.yinghun then
			self.yinghun = self:findPlayerToDraw(false,n)
		end
		if not self.yinghun then
			for _,friend in ipairs(self.friends_noself)do
				if not friend:hasSkill("manjuan") then
					self.yinghun = friend
					break
				end
			end
		end
		if self.yinghun then self.yinghunchoice = "dxt1" end
	end
	if not self.yinghun and x>1 and #self.enemies>0 then
		self:sort(self.enemies,"handcard")
		for _,enemy in ipairs(self.enemies)do
			if enemy:getCards("he"):length()>=n
			and self:doDisCard(enemy,"nil",true,n) then
				self.yinghunchoice = "d1tx"
				return enemy
			end
		end
		self.enemies = sgs.reverse(self.enemies)
		for _,enemy in ipairs(self.enemies)do
			if not enemy:isNude()
			and not (self:hasSkills(sgs.lose_equip_skill,enemy) and enemy:getCards("e"):length()>0)
			and not self:needToThrowArmor(enemy)
			and not enemy:hasSkills("tuntian+zaoxian") then
				self.yinghunchoice = "d1tx"
				return enemy
			end
		end
		for _,enemy in ipairs(self.enemies)do
			if not enemy:isNude()
				and not (self:hasSkills(sgs.lose_equip_skill,enemy) and enemy:getCards("e"):length()>0)
				and not self:needToThrowArmor(enemy)
				and not (enemy:hasSkills("tuntian+zaoxian") and x<3 and enemy:getCards("he"):length()<2) then
				self.yinghunchoice = "d1tx"
				return enemy
			end
		end
	end

	return self.yinghun
end

sgs.ai_skill_choice.yinghun = function(self,choices)
	return self.yinghunchoice
end

sgs.ai_playerchosen_intention.yinghun = function(self,from,to)
	if from:getLostHp()>1 then return end
	local intention = -80
	if to:hasSkill("manjuan") then intention = -intention end
	sgs.updateIntention(from,to,intention)
end

sgs.ai_choicemade_filter.skillChoice.yinghun = function(self,player,promptlist)
	local to
	for _,p in sgs.qlist(self.room:getOtherPlayers(player))do
		if p:hasFlag("YinghunTarget") then
			to = p
			break
		end
	end
	local choice = promptlist[#promptlist]
	local intention = (choice=="dxt1") and -80 or 80
	sgs.updateIntention(player,to,intention)
end

local function getLowerBoundOfHandcard(self)
	local least = math.huge
	local players = self.room:getOtherPlayers(self.player)
	for _,player in sgs.qlist(players)do
		least = math.min(player:getHandcardNum(),least)
	end

	return least
end

local function getBeggar(self)
	local least = getLowerBoundOfHandcard(self)

	self:sort(self.friends_noself)
	for _,friend in ipairs(self.friends_noself)do
		if friend:getHandcardNum()==least then
			return friend
		end
	end

	for _,player in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if player:getHandcardNum()==least then
			return player
		end
	end
end

sgs.ai_skill_invoke.haoshi = function(self,data)
	local extra = 0
	if self.player:hasSkill("yongsi") then
		local kingdoms = {}
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			kingdoms[p:getKingdom()] = true
		end
		extra=extra+#kingdoms
	end
	local sk = {["yingzi"]=1,["zishou"]=self.player:getLostHp(),["ayshuijian"]=1+self.player:getEquips():length(),
	["shenwei"]=2,["juejing"]=self.player:getLostHp()}
	for s,n in ipairs(sk)do
		if self.player:hasSkill(s) then
			extra = extra+n
		end
	end
	if self.player:getHandcardNum()+extra<=1 then
		return true
	end

	local beggar = getBeggar(self)
	return self:isFriend(beggar) and not beggar:hasSkill("manjuan")
end

sgs.ai_skill_use["@@haoshi!"] = function(self,prompt)
	local beggar = getBeggar(self)

	local cards = self.player:getHandcards()
	cards = sgs.QList2Table(cards)
	self:sortByUseValue(cards,true)
	local card_ids = {}
	for i=1,math.floor(#cards/2)do
		table.insert(card_ids,cards[i]:getEffectiveId())
	end

	return "@HaoshiCard="..table.concat(card_ids,"+").."->"..beggar:objectName()
end

sgs.ai_card_intention.HaoshiCard = -80

function sgs.ai_cardneed.haoshi(to,card,self)
	return not self:willSkipDrawPhase(to)
end

dimeng_skill = {}
dimeng_skill.name = "dimeng"
table.insert(sgs.ai_skills,dimeng_skill)
dimeng_skill.getTurnUseCard = function(self)
	if self:needBear() then return end
	return sgs.Card_Parse("@DimengCard=.")
end

--要求：mycards是经过sortByKeepValue排序的--
function DimengIsWorth(self,friend,enemy,mycards,myequips)
	local e_hand1,e_hand2 = enemy:getHandcardNum(),enemy:getHandcardNum()-self:getLeastHandcardNum(enemy)
	local f_hand1,f_hand2 = friend:getHandcardNum(),friend:getHandcardNum()-self:getLeastHandcardNum(friend)
	local e_peach,f_peach = getCardsNum("Peach",enemy),getCardsNum("Peach",friend)
	if e_hand1<f_hand1 then
		return false
	elseif e_hand2<=f_hand2 and e_peach<=f_peach then
		return false
	elseif e_peach<f_peach and e_peach<1 then
		return false
	elseif e_hand1==f_hand1 and e_hand1>0 then
		return friend:hasSkills("tuntian+zaoxian")
	end
	local cardNum = #mycards
	local delt = e_hand1-f_hand1 --assert: delt>0
	if delt>cardNum then
		return false
	end
	local equipNum = #myequips
	if equipNum>0 then
		if self.player:hasSkills("xuanfeng|xiaoji|nosxuanfeng") then
			return true
		end
	end
	--now e_hand1>f_hand1 and delt<=cardNum
	local soKeep = 0
	local soUse = 0
	local marker = math.ceil(delt/2)
	for i=1,delt,1 do
		local card = mycards[i]
		local keepValue = self:getKeepValue(card)
		if keepValue>4 then
			soKeep = soKeep+1
		end
		local useValue = self:getUseValue(card)
		if useValue>=6 then
			soUse = soUse+1
		end
	end
	if soKeep>marker then
		return false
	end
	if soUse>marker then
		return false
	end
	return true
end

--缔盟的弃牌策略--
local dimeng_discard = function(self,discard_num,mycards)
	local cards = mycards
	local to_discard = {}

	local aux_func = function(card)
		local place = self.room:getCardPlace(card:getEffectiveId())
		if place==sgs.Player_PlaceEquip then
			if card:isKindOf("SilverLion") and self.player:isWounded() then return -2
			elseif card:isKindOf("OffensiveHorse") then return 1
			elseif card:isKindOf("Weapon") then return 2
			elseif card:isKindOf("DefensiveHorse") then return 3
			elseif card:isKindOf("Armor") then return 4
			end
		elseif self:getUseValue(card)>=6 then return 3 --使用价值高的牌，如顺手牵羊(9),下调至桃
		elseif self:hasSkills(sgs.lose_equip_skill) then return 5
		else return 0
		end
		return 0
	end

	local compare_func = function(a,b)
		if aux_func(a)~=aux_func(b) then
			return aux_func(a)<aux_func(b)
		end
		return self:getKeepValue(a)<self:getKeepValue(b)
	end

	table.sort(cards,compare_func)
	for _,card in ipairs(cards)do
		if not self.player:isJilei(card) then table.insert(to_discard,card:getId()) end
		if #to_discard>=discard_num then break end
	end
	if #to_discard~=discard_num then return {} end
	return to_discard
end

sgs.ai_skill_use_func.DimengCard = function(card,use,self)
	local mycards = {}
	local myequips = {}
	local keepaslash
	for _,c in sgs.qlist(self.player:getHandcards())do
		if not self.player:isJilei(c) then
			local shouldUse
			if not keepaslash and isCard("Slash",c,self.player) then
				local dummy_use = dummy()
				self:useBasicCard(c,dummy_use)
				if dummy_use.card and not dummy_use.to:isEmpty() and (dummy_use.to:length()>1 or dummy_use.to:first():getHp()<=1) then
					shouldUse = true
				end
			end
			if not shouldUse then table.insert(mycards,c) end
		end
	end
	for _,c in sgs.qlist(self.player:getEquips())do
		if not self.player:isJilei(c) then
			table.insert(mycards,c)
			table.insert(myequips,c)
		end
	end
	if #mycards==0 then return end
	self:sortByKeepValue(mycards) --桃的keepValue是5，useValue是6；顺手牵羊的keepValue是1.9，useValue是9

	self:sort(self.enemies,"handcard")
	local friends = {}
	for _,player in ipairs(self.friends_noself)do
		if not player:hasSkill("manjuan") then
			table.insert(friends,player)
		end
	end
	if #friends==0 then return end

	self:sort(friends,"defense")
	local function cmp_HandcardNum(a,b)
		local x = a:getHandcardNum()-self:getLeastHandcardNum(a)
		local y = b:getHandcardNum()-self:getLeastHandcardNum(b)
		return x<y
	end
	table.sort(friends,cmp_HandcardNum)

	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if enemy:hasSkill("manjuan") then
			local e_hand = enemy:getHandcardNum()
			for _,friend in ipairs(friends)do
				local f_peach,f_hand = getCardsNum("Peach",friend),friend:getHandcardNum()
				if (e_hand>f_hand-1) and (e_hand-f_hand)<=#mycards and (f_hand>0 or e_hand>0) and f_peach<=2 then
					if e_hand==f_hand then
						use.card = card
					else
						local discard_num = e_hand-f_hand
						local discards = dimeng_discard(self,discard_num,mycards)
						if #discards>0 then use.card = sgs.Card_Parse("@DimengCard="..table.concat(discards,"+")) end
					end
					if use.card and use.to then
						use.to:append(enemy)
						use.to:append(friend)
					end
					return
				end
			end
		end
	end

	for _,enemy in ipairs(self.enemies)do
		local e_hand = enemy:getHandcardNum()
		for _,friend in ipairs(friends)do
			local f_hand = friend:getHandcardNum()
			if DimengIsWorth(self,friend,enemy,mycards,myequips) and (e_hand>0 or f_hand>0) then
				if e_hand==f_hand then
					use.card = card
				else
					local discard_num = math.abs(e_hand-f_hand)
					local discards = dimeng_discard(self,discard_num,mycards)
					if #discards>0 then use.card = sgs.Card_Parse("@DimengCard="..table.concat(discards,"+")) end
				end
				use.to:append(enemy)
				use.to:append(friend)
				return
			end
		end
	end
end

sgs.ai_card_intention.DimengCard = function(self,card,from,to)
	local compare_func = function(a,b)
		return a:getHandcardNum()<b:getHandcardNum()
	end
	table.sort(to,compare_func)
	if to[1]:getHandcardNum()<to[2]:getHandcardNum() then
		sgs.updateIntention(from,to[1],-80)
	end
end

sgs.ai_use_value.DimengCard = 3.5
sgs.ai_use_priority.DimengCard = 2.8

sgs.dynamic_value.control_card.DimengCard = true



luanwu_skill = {}
luanwu_skill.name = "luanwu"
table.insert(sgs.ai_skills,luanwu_skill)
luanwu_skill.getTurnUseCard = function(self)
	if self.room:getMode()=="_mini_13" then return sgs.Card_Parse("@LuanwuCard=.") end
	local good,bad = 0,0
	local lord = self.room:getLord()
	if lord and self.role~="rebel" and self:isWeak(lord) then return end
	for _,player in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if self:isWeak(player) then
			if self:isFriend(player) then bad = bad+1
			else good = good+1
			end
		end
	end
	if good==0 then return end

	for _,player in sgs.qlist(self.room:getOtherPlayers(self.player))do
		local hp = math.max(player:getHp(),1)
		if getCardsNum("Analeptic",player)>0 then
			if self:isFriend(player) then good = good+1.0/hp
			else bad = bad+1.0/hp
			end
		end

		local has_slash = (getCardsNum("Slash",player)>0)
		local can_slash = false
		if not can_slash then
			for _,p in sgs.qlist(self.room:getOtherPlayers(player))do
				if player:distanceTo(p)<=player:getAttackRange() then can_slash = true break end
			end
		end
		if not has_slash or not can_slash then
			if self:isFriend(player) then good = good+math.max(getCardsNum("Peach",player),1)
			else bad = bad+math.max(getCardsNum("Peach",player),1)
			end
		end

		if getCardsNum("Jink",player)==0 then
			local lost_value = 0
			if self:hasSkills(sgs.masochism_skill,player) then lost_value = player:getHp()/2 end
			local hp = math.max(player:getHp(),1)
			if self:isFriend(player) then bad = bad+(lost_value+1)/hp
			else good = good+(lost_value+1)/hp
			end
		end
	end

	if good>bad then return sgs.Card_Parse("@LuanwuCard=.") end
end

sgs.ai_skill_use_func.LuanwuCard=function(card,use,self)
	use.card = card
end

sgs.dynamic_value.damage_card.LuanwuCard = true

jiuchi_skill={}
jiuchi_skill.name="jiuchi"
table.insert(sgs.ai_skills,jiuchi_skill)
jiuchi_skill.getTurnUseCard=function(self)
	local cards = self:addHandPile()
	local card
	self:sortByUseValue(cards,true)
	for _,acard in ipairs(cards)  do
		if acard:getSuit()==sgs.Card_Spade then
			card = acard
			break
		end
	end
	if not card then return nil end
	local analeptic = sgs.Card_Parse(("analeptic:jiuchi[spade:%s]=%d"):format(card:getNumberString(),card:getEffectiveId()))
	assert(analeptic)
	if sgs.Analeptic_IsAvailable(self.player,analeptic)
	then return analeptic end
end

sgs.ai_view_as.jiuchi = function(card,player,card_place)
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	if card_place==sgs.Player_PlaceHand then
		if card:getSuit()==sgs.Card_Spade then
			return ("analeptic:jiuchi[%s:%s]=%d"):format(suit,number,card_id)
		end
	end
end

function sgs.ai_cardneed.jiuchi(to,card,self)
	return card:getSuit()==sgs.Card_Spade and (getKnownCard(to,self.player,"club",false)+getKnownCard(to,self.player,"spade",false))==0
end

function sgs.ai_cardneed.roulin(to,card,self)
	for _,enemy in ipairs(self.enemies)do
		if card:isKindOf("Slash") and to:canSlash(enemy,nil,true) and self:slashIsEffective(card,enemy)
		and not (enemy:hasSkill("kongcheng") and enemy:isKongcheng())
		and self:isGoodTarget(enemy,self.enemies,card) and not self:slashProhibit(card,enemy) and enemy:isFemale() then
			return getKnownCard(to,self.player,"Slash",true)==0
		end
	end
end

sgs.ai_skill_choice.benghuai = function(self,choices,data)
	for _,friend in ipairs(self.friends)do
		if friend:hasSkill("tianxiang") and (self.player:getHp()>=3 or (self:getCardsNum("Peach")+self:getCardsNum("Analeptic")>0 and self.player:getHp()>1)) then
			return "hp"
		end
	end
	if self.player:getMaxHp()>=self.player:getHp()+2 then
		if self.player:getMaxHp()>5 and (self.player:hasSkills("nosmiji|yinghun|juejing|zaiqi|nosshangshi") or self.player:hasSkill("miji") and self:findPlayerToDraw(false)) then
			local enemy_num = 0
			for _,p in ipairs(self.enemies)do
				if p:inMyAttackRange(self.player) and not self:willSkipPlayPhase(p) then enemy_num = enemy_num+1 end
			end
			local ls = sgs.fangquan_effect and self.room:findPlayerBySkillName("fangquan")
			if ls then
				sgs.fangquan_effect = false
				enemy_num = self:getEnemyNumBySeat(ls,self.player,self.player)
			end
			local least_hp = isLord(self.player) and math.max(2,enemy_num-1) or 1
			if (self:getCardsNum("Peach")+self:getCardsNum("Analeptic")+self.player:getHp()>least_hp) then return "hp" end
		end
		return "maxhp"
	else
		return "hp"
	end
end

sgs.ai_skill_playerchosen.baonue = function(self,targets)
	targets = sgs.QList2Table(targets)
	for _,target in ipairs(targets)do
		if self:isFriend(target) and target:isAlive() then
			if target:isWounded() then
				return target
			end
			local zhangjiao = self.room:findPlayerBySkillName("guidao")
			if zhangjiao and self:isFriend(zhangjiao) then
				return target
			end
		end
	end
	return nil
end

sgs.ai_playerchosen_intention.baonue = -40

sgs.jiuchi_suit_value = {
	spade = 5,
}

sgs.ai_suit_priority.jiuchi= "diamond|heart|club|spade"


function getGuixinValue(self,player)
	if player:isAllNude() then return 0 end
	local card_id = self:askForCardChosen(player,"hej","dummy")
	if self:isEnemy(player) then
		for _,card in sgs.list(player:getJudgingArea())do
			if card:getEffectiveId()==card_id then
				if card:isKindOf("YanxiaoCard") then return 0
				elseif card:isKindOf("Lightning") then
					if self:hasWizard(self.enemies,true) then return 0.8
					elseif self:hasWizard(self.friends,true) then return 0.4
					else return 0.5*(#self.friends)/(#self.friends+#self.enemies) end
				else
					return -0.2
				end
			end
		end
		for i = 0,3 do
			local card = player:getEquip(i)
			if card and card:getEffectiveId()==card_id then
				if card:isKindOf("Armor") and self:needToThrowArmor(player) then return 0 end
				local value = 0
				if self:getDangerousCard(player)==card_id then value = 1.5
				elseif self:getValuableCard(player)==card_id then value = 1.1
				elseif i==1 then value = 1
				elseif i==2 then value = 0.8
				elseif i==0 then value = 0.7
				elseif i==3 then value = 0.5
				end
				if player:hasSkills(sgs.lose_equip_skill) or not self:doDisCard(player,"e") then value = value-0.2 end
				return value
			end
		end
		if self:needKongcheng(player) and player:getHandcardNum()==1 then return 0 end
		if not self:hasLoseHandcardEffective() then return 0.1
		else
			local index = player:hasSkills("jijiu|qingnang|leiji|nosleiji|jieyin|beige|kanpo|liuli|qiaobian|zhiheng|guidao|longhun|xuanfeng|tianxiang|noslijian|lijian") and 0.7 or 0.6
			local value = 0.2+index/(player:getHandcardNum()+1)
			if not self:doDisCard(player,"h") then value = value-0.1 end
			return value
		end
	elseif self:isFriend(player) then
		for _,card in sgs.list(player:getJudgingArea())do
			if card:getEffectiveId()==card_id then
				if card:isKindOf("YanxiaoCard") then return 0
				elseif card:isKindOf("Lightning") then
					if self:hasWizard(self.enemies,true) then return 1
					elseif self:hasWizard(self.friends,true) then return 0.8
					else return 0.4*(#self.enemies)/(#self.friends+#self.enemies) end
				else
					return 1.5
				end
			end
		end
		for i = 0,3 do
			local card = player:getEquip(i)
			if card and card:getEffectiveId()==card_id then
				if card:isKindOf("Armor") and self:needToThrowArmor(player) then return 0.9 end
				local value = 0
				if i==1 then value = 0.1
				elseif i==2 then value = 0.2
				elseif i==0 then value = 0.25
				elseif i==3 then value = 0.25
				end
				if player:hasSkills(sgs.lose_equip_skill) then value = value+0.1 end
				if player:hasSkills("tuntian+zaoxian") then value = value+0.1 end
				return value
			end
		end
		if self:needKongcheng(player,true) and player:getHandcardNum()==1 then return 0.5
		elseif self:needKongcheng(player) and player:getHandcardNum()==1 then return 0.3 end
		if not self:hasLoseHandcardEffective() then return 0.2
		else
			local index = player:hasSkills("jijiu|qingnang|leiji|nosleiji|jieyin|beige|kanpo|liuli|qiaobian|zhiheng|guidao|longhun|xuanfeng|tianxiang|noslijian|lijian") and 0.5 or 0.4
			local value = 0.2-index/(player:getHandcardNum()+1)
			if player:hasSkills("tuntian+zaoxian") then value = value+0.1 end
			return value
		end
	end
	return 0.3
end

sgs.ai_skill_invoke.guixin = function(self,data)
	local damage = data:toDamage()
	local diaochans = self.room:findPlayersBySkillName("lihun")
	local lihun_eff = false
	for _,diaochan in sgs.list(diaochans)do
		if self:isEnemy(diaochan) then
			lihun_eff = true
			break
		end
	end
	local manjuan_eff = hasManjuanEffect(self.player)
	if lihun_eff and not manjuan_eff then return false end
	if not self.player:faceUp() then return true
	else
		if manjuan_eff then return false end
		local value = 0
		for _,player in sgs.list(self.room:getOtherPlayers(self.player))do
			value = value+getGuixinValue(self,player)
		end
		local left_num = damage.damage-self.player:getMark("guixinTimes")
		return value>=1.3 or left_num>0
	end
end

sgs.ai_need_damaged.guixin = function(self,attacker,player)
	if self.room:alivePlayerCount()<=3 or player:hasSkill("manjuan") then return false end
	local diaochan = self.room:findPlayerBySkillName("lihun")
	local drawcards = 0
	for _,aplayer in sgs.list(self.room:getOtherPlayers(player))do
		if aplayer:getCards("hej"):length()>0 then drawcards = drawcards+1 end
	end
	return not self:isLihunTarget(player,drawcards)
end

sgs.ai_skill_invoke.newguixin = function(self,data)
	local damage = data:toDamage()
	local diaochans = self.room:findPlayersBySkillName("lihun")
	local lihun_eff = false
	for _,diaochan in sgs.list(diaochans)do
		if self:isEnemy(diaochan) then
			lihun_eff = true
			break
		end
	end
	local manjuan_eff = hasManjuanEffect(self.player)
	if lihun_eff and not manjuan_eff then return false end
	if not self.player:faceUp() then return true
	else
		if manjuan_eff then return false end
		local value = 0
		for _,player in sgs.list(self.room:getOtherPlayers(self.player))do
			value = value+getGuixinValue(self,player)
		end
		local left_num = damage.damage-self.player:getMark("newguixinTimes")
		return value>=1.3 or left_num>0
	end
end

sgs.ai_need_damaged.newguixin = function(self,attacker,player)
	return sgs.ai_need_damaged.guixin(self,attacker,player)
end

sgs.ai_skill_choice.wumou = function(self,choices)
        if self.player:getMark("&wrath")>6 then return "discard" end
	if self.player:getHp()+self:getCardsNum("Peach")>3 then return "losehp"
	else return "discard"
	end
end

sgs.ai_use_revises.wumou = function(self,card,use)
	if card:isNDTrick() and self.player:getMark("&wrath")<7
	then
        if not (card:isKindOf("AOE") or card:isKindOf("IronChain") or card:isKindOf("Drowning"))
        and not (card:isKindOf("Duel") and self.player:getMark("&wrath")>0)
		then return false end
	end
end

local wuqian_skill = {}
wuqian_skill.name = "wuqian"
table.insert(sgs.ai_skills,wuqian_skill)
wuqian_skill.getTurnUseCard = function(self)
    if self.player:getMark("&wrath")<2 then return end
	return sgs.Card_Parse("@WuqianCard=.")
end

sgs.ai_skill_use_func.WuqianCard = function(wuqiancard,use,self)
	if self:getCardsNum("Slash")>0 and not self.player:hasSkill("wushuang") then
		for _,card in sgs.list(self.player:getHandcards())do
			if isCard("Duel",card,self.player) then
				local dummy_use = dummy()
				dummy_use.isWuqian = true
				local duel = dummyCard("duel")
				self:useCardDuel(duel,dummy_use)
				if dummy_use.card and dummy_use.to:length()>0 and (self:isWeak(dummy_use.to:first()) and dummy_use.to:first():getHp()==1 or dummy_use.to:length()>1) then
					use.card = wuqiancard
					use.to:append(dummy_use.to:first())
					return
				end
			end
		end
	end
end

sgs.ai_use_value.WuqianCard = 5
sgs.ai_use_priority.WuqianCard = 10
sgs.ai_card_intention.WuqianCard = 80

local shenfen_skill = {}
shenfen_skill.name = "shenfen"
table.insert(sgs.ai_skills,shenfen_skill)
shenfen_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@ShenfenCard=.")
end

function SmartAI:canSaveSelf(player)
	if hasBuquEffect(player) then return true end
	if getCardsNum("Analeptic",player,self.player)>0 then return true end
	if player:hasSkills("jiushi|mobilejiushi") and player:faceUp() then return true end
	if player:hasSkills("jiuchi|mobilejiuchi|oljiuchi") then
		for _,c in sgs.list(player:getHandcards())do
			if c:getSuit()==sgs.Card_Spade then return true end
		end
	end
	return false
end

local function getShenfenUseValueOfHECards(self,to)
	local value = 0
	-- value of handcards
	local value_h = 0
	local hcard = to:getHandcardNum()
	if to:hasSkill("lianying") then
		hcard = hcard-0.9
	elseif to:hasSkills("shangshi|nosshangshi") then
		hcard = hcard-0.9*to:getLostHp()
	else
		local jwfy = self.room:findPlayerBySkillName("shoucheng")
		if jwfy and self:isFriend(jwfy,to) and (not self:isWeak(jwfy) or jwfy:getHp()>1) then hcard = hcard-0.9 end
	end
	value_h = (hcard>4) and 16/hcard or hcard
	if to:hasSkills("tuntian+zaoxian") then value = value*0.95 end
	if (to:hasSkill("kongcheng") or (to:hasSkill("zhiji") and to:getHp()>2 and to:getMark("zhiji")==0)) and not to:isKongcheng() then value_h = value_h*0.7 end
	if to:hasSkills("jijiu|qingnang|leiji|nosleiji|jieyin|beige|kanpo|liuli|qiaobian|zhiheng|guidao|longhun|xuanfeng|tianxiang|noslijian|lijian") then value_h = value_h*0.95 end
	value = value+value_h

	-- value of equips
	local value_e = 0
	local equip_num = to:getEquips():length()
	if to:hasArmorEffect("SilverLion") and to:isWounded() then equip_num = equip_num-1.1 end
	value_e = equip_num*1.1
	if to:hasSkills("kofxiaoji|xiaoji") then value_e = value_e*0.7 end
	if to:hasSkill("nosxuanfeng") then value_e = value_e*0.85 end
	if to:hasSkills("bazhen|yizhong") and to:getArmor() then value_e = value_e-1 end
	value = value+value_e

	return value
end

local function getDangerousShenGuanYu(self)
	local most = -100
	local target
	for _,player in sgs.list(self.room:getAllPlayers())do
                local nm_mark = player:getMark("&nightmare")
		if player:objectName()==self.player:objectName() then nm_mark = nm_mark+1 end
		if nm_mark>0 and nm_mark>most or (nm_mark==most and self:isEnemy(player)) then
			most = nm_mark
			target = player
		end
	end
	if target and self:isEnemy(target) then return true end
	return false
end

sgs.ai_skill_use_func.ShenfenCard = function(card,use,self)
	if (self.role=="loyalist" or self.role=="renegade") and self.room:getLord() and self:isWeak(self.room:getLord()) and not self.player:isLord() then return end
	local benefit = 0
	for _,player in sgs.list(self.room:getOtherPlayers(self.player))do
		if self:isFriend(player) then benefit = benefit-getShenfenUseValueOfHECards(self,player) end
		if self:isFriend(player) then benefit = benefit+getShenfenUseValueOfHECards(self,player) end
	end
	local friend_save_num = self:getSaveNum(true)
	local enemy_save_num = self:getSaveNum(false)
	local others = 0
	for _,player in sgs.list(self.room:getOtherPlayers(self.player))do
		if self:damageIsEffective(player,sgs.DamageStruct_Normal) then
			others = others+1
			local value_d = 3.5/math.max(player:getHp(),1)
			if player:getHp()<=1 then
				if player:hasSkill("wuhun") then
					local can_use = getDangerousShenGuanYu(self)
					if not can_use then return else value_d = value_d*0.1 end
				end
				if self:canSaveSelf(player) then
					value_d = value_d*0.9
				elseif self:isFriend(player) and friend_save_num>0 then
					friend_save_num = friend_save_num-1
					value_d = value_d*0.9
				elseif self:isEnemy(player) and enemy_save_num>0 then
					enemy_save_num = enemy_save_num-1
					value_d = value_d*0.9
				end
			end
			if player:hasSkill("fankui") then value_d = value_d*0.8 end
			if player:hasSkill("guixin") then
				if not player:faceUp() then
					value_d = value_d*0.4
				else
					value_d = value_d*0.8*(1.05-self.room:alivePlayerCount()/15)
				end
			end
			if self:needToLoseHp(player,self.player) or getBestHp(player)==player:getHp()-1 then value_d = value_d*0.8 end
			if self:isFriend(player) then benefit = benefit-value_d end
			if self:isEnemy(player) then benefit = benefit+value_d end
		end
	end
	if not self.player:faceUp() or self.player:hasSkills("jushou|nosjushou|neojushou|kuiwei") then
		benefit = benefit+1
	else
		local help_friend = false
		for _,friend in sgs.list(self.friends_noself)do
			if self:hasSkills("fangzhu|jilve",friend) then
				help_friend = true
				benefit = benefit+1
				break
			end
		end
		if not help_friend then benefit = benefit-0.5 end
	end
	if self.player:getKingdom()=="qun" then
		for _,player in sgs.list(self.room:getOtherPlayers(self.player))do
			if player:hasLordSkill("baonue") and self:isFriend(player) then
				benefit = benefit+0.2*self.room:alivePlayerCount()
				break
			end
		end
	end
	benefit = benefit+(others-7)*0.05
	if benefit>0 then
		use.card = card
	end
end

sgs.ai_use_value.ShenfenCard = 8
sgs.ai_use_priority.ShenfenCard = 5.3

sgs.dynamic_value.damage_card.ShenfenCard = true
sgs.dynamic_value.control_card.ShenfenCard = true
