local quhu_skill = {}
quhu_skill.name = "quhu"
table.insert(sgs.ai_skills,quhu_skill)
quhu_skill.getTurnUseCard = function(self)
	if self:needBear() then return end
	local max_card = self:getMaxCard()
	return sgs.Card_Parse("@QuhuCard="..max_card:getEffectiveId())
end

sgs.ai_skill_use_func.QuhuCard = function(card,use,self)
	local max_card = self:getMaxCard()
	local max_point = max_card:getNumber()

	if #self.enemies==0 then return end
	self:sort(self.enemies,"handcard")

	for _,enemy in sgs.list(self.enemies)do
		if enemy:getHp()>self.player:getHp() and self.player:canPindian(enemy) then
			local enemy_max_card = self:getMaxCard(enemy)
			local allknown = 0
			if self:getKnownNum(enemy)==enemy:getHandcardNum() then
				allknown = allknown+1
			end
			if (enemy_max_card and max_point>enemy_max_card:getNumber() and allknown>0)
				or (enemy_max_card and max_point>enemy_max_card:getNumber() and allknown<1 and max_point>10)
				or (not enemy_max_card and max_point>10) then
				for _,enemy2 in sgs.list(self.enemies)do
					if (enemy:objectName()~=enemy2:objectName())
						and enemy:distanceTo(enemy2)<=enemy:getAttackRange() then
						self.quhu_card = max_card:getEffectiveId()
						use.card = sgs.Card_Parse("@QuhuCard=.")
						use.to:append(enemy)
						return
					end
				end
			end
		end
	end
	if (not self.player:isWounded() or (self.player:getHp()==1 and self:getCardsNum("Analeptic")>0 and self.player:getHandcardNum()>=2))
	  and self.player:hasSkill("jieming") then
		local use_quhu
		for _,friend in sgs.list(self.friends)do
			if math.min(5,friend:getMaxHp())-friend:getHandcardNum()>=2 then
				self:sort(self.enemies,"handcard")
				if self.player:canPindian(self.enemies[#self.enemies]) then use_quhu = true break end
			end
		end
		if use_quhu then
			for _,enemy in sgs.list(self.enemies)do
				if self.player:canPindian(enemy) and self.player:getHp()<enemy:getHp() and not enemy:hasSkill("jueqing") then
					local cards = self.player:getHandcards()
					cards = sgs.QList2Table(cards)
					self:sortByUseValue(cards,true)
					self.quhu_card = cards[1]:getEffectiveId()
					use.card = sgs.Card_Parse("@QuhuCard=.")
					use.to:append(enemy)
					return
				end
			end
		end
	end
end

sgs.ai_choicemade_filter.QuhuCard = function(self,player,carduse)
	sgs.ai_quhu_effect = true
end

sgs.ai_cardneed.quhu = sgs.ai_cardneed.bignumber
sgs.ai_skill_playerchosen.quhu = sgs.ai_skill_playerchosen.damage
sgs.ai_playerchosen_intention.quhu = 80

sgs.ai_card_intention.QuhuCard = 0
sgs.dynamic_value.control_card.QuhuCard = true

sgs.ai_skill_playerchosen.jieming = function(self,targets)
	local friends = {}
	for _,player in sgs.list(self.friends)do
		if player:isAlive() and not hasManjuanEffect(player) then
			table.insert(friends,player)
		end
	end
	self:sort(friends)

	local max_x = 0
	local target
	local Shenfen_user
	for _,player in sgs.qlist(self.room:getAlivePlayers())do
		if player:hasFlag("ShenfenUsing") then
			Shenfen_user = player
			break
		end
	end
	if Shenfen_user then
		local y,weak_friend = 3,nil
		for _,friend in sgs.list(friends)do
			local x = math.min(friend:getMaxHp(),5)-friend:getHandcardNum()
			if friend:hasSkill("manjuan") and x>0 then x = x+1 end
			if friend:getMaxHp()>=5 and x>max_x and friend:isAlive() then
				max_x = x
				target = friend
			end

			if self:playerGetRound(friend,Shenfen_user)>self:playerGetRound(self.player,Shenfen_user) and x>=y
				and friend:getHp()==1 and getCardsNum("Peach",friend,self.player)<1 then
				y = x
				weak_friend = friend
			end
		end

		if weak_friend and ((getCardsNum("Peach",Shenfen_user,self.player)<1) or (math.min(Shenfen_user:getMaxHp(),5)-Shenfen_user:getHandcardNum()<=1)) then
			return weak_friend
		end
		if self:isFriend(Shenfen_user) and math.min(Shenfen_user:getMaxHp(),5)>Shenfen_user:getHandcardNum() then
			return Shenfen_user
		end
		if target then return target end
	end

	local CP = self.room:getCurrent()
	local max_x = 0
	local AssistTarget = self:AssistTarget()
	for _,friend in sgs.list(friends)do
		local x = math.min(friend:getMaxHp(),5)-friend:getHandcardNum()
		if friend:hasSkill("manjuan") then x = x+1 end
		if self:hasCrossbowEffect(CP) then x = x+1 end
		if AssistTarget and friend:objectName()==AssistTarget:objectName() then x = x+0.5 end

		if x>max_x and friend:isAlive() then
			max_x = x
			target = friend
		end
	end

	return target
end

sgs.ai_need_damaged.jieming = function (self,attacker,player)
	return player:hasSkill("jieming") and self:getJiemingChaofeng(player)<=-6
end

sgs.ai_playerchosen_intention.jieming = function(self,from,to)
	if to:getHandcardNum()<math.min(5,to:getMaxHp()) then
		sgs.updateIntention(from,to,-80)
	end
end

sgs.ai_can_damagehp.jieming = function(self,from,card,to)
	if not self:isWeak()
	and self:canLoseHp(from,card,to)
	and to:getHp()+self:getAllPeachNum()-self:ajustDamage(from,to,1,card)>0
	then
		for _,fp in sgs.list(self.friends)do
			if fp:getMaxHp()-fp:getHandcardNum()>1
			then return true end
		end
	end
end


local qiangxi_skill = {}
qiangxi_skill.name= "qiangxi"
table.insert(sgs.ai_skills,qiangxi_skill)
qiangxi_skill.getTurnUseCard=function(self)
	return sgs.Card_Parse("@QiangxiCard=.")
end

sgs.ai_skill_use_func.QiangxiCard = function(card,use,self)
	local weapon = self.player:getWeapon()
	if weapon then
		local hand_weapon,cards
		cards = self.player:getHandcards()
		for _,card in sgs.qlist(cards)do
			if card:isKindOf("Weapon") then
				hand_weapon = card
				break
			end
		end
		self:sort(self.enemies)
		self.equipsToDec = hand_weapon and 0 or 1
		for _,enemy in sgs.list(self.enemies)do
			if self:objectiveLevel(enemy)>3 and not self:cantbeHurt(enemy) and self:damageIsEffective(enemy)
			then
				if hand_weapon and self.player:distanceTo(enemy)<=self.player:getAttackRange()
				 then
					use.card = sgs.Card_Parse("@QiangxiCard="..hand_weapon:getId())
					use.to:append(enemy)
					break
				end
				if self.player:distanceTo(enemy)<=1
				 then
					use.card = sgs.Card_Parse("@QiangxiCard="..weapon:getId())
					use.to:append(enemy)
					return
				end
			end
		end
		self.equipsToDec = 0
	else
		self:sort(self.enemies,"hp")
		for _,enemy in sgs.list(self.enemies)do
			if self:objectiveLevel(enemy)>3 and not self:cantbeHurt(enemy) and self:damageIsEffective(enemy)
			and self.player:distanceTo(enemy)<=self.player:getAttackRange() and self.player:getHp()>enemy:getHp() and self.player:getHp()>1
			then
				use.card = sgs.Card_Parse("@QiangxiCard=.")
				use.to:append(enemy)
				return
			end
		end
	end
end

sgs.ai_use_value.QiangxiCard = 2.5
sgs.ai_card_intention.QiangxiCard = 80
sgs.dynamic_value.damage_card.QiangxiCard = true
sgs.ai_cardneed.qiangxi = sgs.ai_cardneed.weapon

sgs.qiangxi_keep_value = {
	Peach = 6,
	Jink = 5.1,
	Weapon = 5
}



local huoji_skill={}
huoji_skill.name="huoji"
table.insert(sgs.ai_skills,huoji_skill)
huoji_skill.getTurnUseCard=function(self)
	local cards = self:addHandPile()
	self:sortByUseValue(cards,true)
	local card
	for _,acard in sgs.list(cards)do
		if acard:isRed() and not acard:isKindOf("Peach") and (self:getDynamicUsePriority(acard)<sgs.ai_use_value.FireAttack or self:getOverflow()>0) then
			if acard:isKindOf("Slash") and self:getCardsNum("Slash")==1 then
				local keep
				local dummy_use = dummy()
				self:useBasicCard(acard,dummy_use)
				if dummy_use.card and dummy_use.to and dummy_use.to:length()>0 then
					for _,p in sgs.qlist(dummy_use.to)do
						if p:getHp()<=1 then keep = true break end
					end
					if dummy_use.to:length()>1 then keep = true end
				end
				if keep then sgs.ai_use_priority.Slash = sgs.ai_use_priority.FireAttack+0.1
				else
					sgs.ai_use_priority.Slash = 2.6
					card = acard
					break
				end
			else
				card = acard
				break
			end
		end
	end

	if not card then return nil end
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	local card_str = ("fire_attack:huoji[%s:%s]=%d"):format(suit,number,card_id)
	local skillcard = sgs.Card_Parse(card_str)

	assert(skillcard)

	return skillcard
end

sgs.ai_cardneed.huoji = function(to,card,self)
	return to:getHandcardNum()>=2 and card:isRed()
end

sgs.ai_view_as.kanpo = function(card,player,card_place)
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	if card_place==sgs.Player_PlaceHand then
		if card:isBlack() then
			return ("nullification:kanpo[%s:%s]=%d"):format(suit,number,card_id)
		end
	end
end

sgs.ai_cardneed.kanpo = function(to,card,self)
	return card:isBlack()
end

function sgs.ai_armor_value.bazhen(p,s,card)
	if not card then return 4 end
end

sgs.kanpo_suit_value = {
	spade = 3.9,
	club = 3.9
}

local lianhuan_skill={}
lianhuan_skill.name="lianhuan"
table.insert(sgs.ai_skills,lianhuan_skill)
lianhuan_skill.getTurnUseCard = function(self)
	local cards = self:addHandPile()
	self:sortByUseValue(cards,true)
	local card
	local slash = self:getCard("FireSlash") or self:getCard("ThunderSlash") or self:getCard("Slash")
	if slash then
		local dummy_use = dummy()
		self:useBasicCard(slash,dummy_use)
		if not dummy_use.card then slash = nil end
	end

	for _,acard in sgs.list(cards)do
		if acard:getSuit()==sgs.Card_Club then
			local shouldUse = true
			if self:getUseValue(acard)>sgs.ai_use_value.IronChain and acard:getTypeId()==sgs.Card_TypeTrick then
				local dummy_use = dummy()
				self:useTrickCard(acard,dummy_use)
				if dummy_use.card then shouldUse = false end
			end
			if acard:getTypeId()==sgs.Card_TypeEquip then
				local dummy_use = dummy()
				self:useEquipCard(acard,dummy_use)
				if dummy_use.card then shouldUse = false end
			end
			if shouldUse and (not slash or slash:getEffectiveId()~=acard:getEffectiveId()) then
				card = acard
				break
			end
		end
	end

	if not card then return nil end
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	local card_str = ("iron_chain:lianhuan[club:%s]=%d"):format(number,card_id)
	local skillcard = sgs.Card_Parse(card_str)
	assert(skillcard)
	return skillcard
end

sgs.ai_cardneed.lianhuan = function(to,card)
	return card:getSuit()==sgs.Card_Club and to:getHandcardNum()<=2
end

sgs.ai_skill_invoke.niepan = function(self,data)
	local dying = data:toDying()
	local peaches = 1-dying.who:getHp()

	return self:getCardsNum("Peach")+self:getCardsNum("Analeptic")<peaches
end



local tianyi_skill = {}
tianyi_skill.name = "tianyi"
table.insert(sgs.ai_skills,tianyi_skill)
tianyi_skill.getTurnUseCard = function(self)
	if self:needBear() then return end
	return sgs.Card_Parse("@TianyiCard=.")
end

sgs.ai_skill_use_func.TianyiCard = function(card,use,self)
	self:sort(self.enemies,"handcard")
	local cards = sgs.CardList()
	local peach = 0
	for _,c in sgs.qlist(self.player:getHandcards())do
		if isCard("Peach",c,self.player) and peach<2 then
			peach = peach+1
		else
			cards:append(c)
		end
	end
	local max_card = self:getMaxCard(self.player,cards)
	if not max_card then return end
	local max_point = max_card:getNumber()
	local slashcount = self:getCardsNum("Slash")
	if isCard("Slash",max_card,self.player) then slashcount = slashcount-1 end
	if self.player:hasSkill("kongcheng") and self.player:getHandcardNum()==1 then
		for _,enemy in sgs.list(self.enemies)do
			if self.player:canPindian(enemy) and self:doDisCard(enemy,"h") then
				sgs.ai_use_priority.TianyiCard = 1.2
				self.tianyi_card = max_card:getId()
				use.card = sgs.Card_Parse("@TianyiCard=.")
				use.to:append(enemy)
				return
			end
		end
	end
	for _,enemy in sgs.list(self.enemies)do
		if enemy:hasFlag("AI_HuangtianPindian") and enemy:getHandcardNum()==1 and self.player:canPindian(enemy) then
			sgs.ai_use_priority.TianyiCard = 7.2
			self.tianyi_card = max_card:getId()
			use.card = sgs.Card_Parse("@TianyiCard=.")
			use.to:append(enemy)
			enemy:setFlags("-AI_HuangtianPindian")
			return
		end
	end
	local zhugeliang = self.room:findPlayerBySkillName("kongcheng")

	local slash = self:getCard("Slash")
	local dummy_use = dummy(true,1)
	self.player:setFlags("slashNoDistanceLimit")
	if slash then self:useBasicCard(slash,dummy_use) end
	self.player:setFlags("-slashNoDistanceLimit")

	sgs.ai_use_priority.TianyiCard = (slashcount>=1 and dummy_use.card) and 7.2 or 1.2
	if slashcount>=1 and slash and dummy_use.card  then
		for _,enemy in sgs.list(self.enemies)do
			if not (enemy:hasSkill("kongcheng") and enemy:getHandcardNum()==1) and self.player:canPindian(enemy) then
				local enemy_max_card = self:getMaxCard(enemy)
				local enemy_max_point = enemy_max_card and enemy_max_card:getNumber() or 100
				if max_point>enemy_max_point then
					self.tianyi_card = max_card:getId()
					use.card = sgs.Card_Parse("@TianyiCard=.")
					use.to:append(enemy)
					return
				end
			end
		end
		for _,enemy in sgs.list(self.enemies)do
			if not (enemy:hasSkill("kongcheng") and enemy:getHandcardNum()==1) and self.player:canPindian(enemy) then
				if max_point>=10 then
					self.tianyi_card = max_card:getId()
					use.card = sgs.Card_Parse("@TianyiCard=.")
					use.to:append(enemy)
					return
				end
			end
		end
		if #self.enemies<1 then return end
		if dummy_use.to:length()>1 then
			self:sort(self.friends_noself,"handcard")
			for index = #self.friends_noself,1,-1 do
				local friend = self.friends_noself[index]
				if self.player:canPindian(friend) then
					local friend_min_card = self:getMinCard(friend)
					local friend_min_point = friend_min_card and friend_min_card:getNumber() or 100
					if max_point>friend_min_point then
						self.tianyi_card = max_card:getId()
						use.card = sgs.Card_Parse("@TianyiCard=.")
						use.to:append(friend)
						return
					end
				end
			end
		end

		if zhugeliang and self:isFriend(zhugeliang) and zhugeliang:getHandcardNum()==1 and zhugeliang:objectName()~=self.player:objectName()
			and self.player:canPindian(zhugeliang) then
			if max_point>=7 then
				self.tianyi_card = max_card:getId()
				use.card = sgs.Card_Parse("@TianyiCard=.")
				use.to:append(zhugeliang)
				return
			end
		end

		if dummy_use.to:length()>1 then
			for index = #self.friends_noself,1,-1 do
				local friend = self.friends_noself[index]
				if self.player:canPindian(friend) then
					if max_point>=7 then
						self.tianyi_card = max_card:getId()
						use.card = sgs.Card_Parse("@TianyiCard=.")
						use.to:append(friend)
						return
					end
				end
			end
		end
	end

	cards = sgs.QList2Table(cards)
	self:sortByUseValue(cards,true)
	if zhugeliang and self:isFriend(zhugeliang) and zhugeliang:getHandcardNum()==1
		and zhugeliang:objectName()~=self.player:objectName() and self:getEnemyNumBySeat(self.player,zhugeliang)>=1 and self.player:canPindian(zhugeliang) then
		if isCard("Jink",cards[1],self.player) and self:getCardsNum("Jink")==1 then return end
		self.tianyi_card = cards[1]:getId()
		use.card = sgs.Card_Parse("@TianyiCard=.")
		use.to:append(zhugeliang)
		return
	end

	if self:getOverflow()>0 then
		for _,enemy in sgs.list(self.enemies)do
			if self:doDisCard(enemy,"h") and self.player:canPindian(enemy) then
				self.tianyi_card = cards[1]:getId()
				use.card = sgs.Card_Parse("@TianyiCard=.")
				use.to:append(enemy)
				return
			end
		end
	end
	return nil
end

function sgs.ai_skill_pindian.tianyi(minusecard,self,requestor)
	if requestor:getHandcardNum()==1 then
		local cards = sgs.QList2Table(self.player:getHandcards())
		self:sortByKeepValue(cards)
		return cards[1]
	end
	local maxcard = self:getMaxCard()
	return self:isFriend(requestor) and self:getMinCard() or ( maxcard:getNumber()<6 and  minusecard or maxcard )
end

sgs.ai_cardneed.tianyi = function(to,card,self)
	local cards = to:getHandcards()
	local has_big = false
	for _,c in sgs.qlist(cards)do
		local flag = string.format("%s_%s_%s","visible",self.room:getCurrent():objectName(),to:objectName())
		if c:hasFlag("visible") or c:hasFlag(flag) then
			if c:getNumber()>10 then
				has_big = true
				break
			end
		end
	end
	if not has_big then
		return card:getNumber()>10
	else
		return card:isKindOf("Slash") or card:isKindOf("Analeptic")
	end
end

sgs.ai_card_intention.TianyiCard = 0
sgs.dynamic_value.control_card.TianyiCard = true

sgs.ai_use_value.TianyiCard = 8.5



local luanji_skill = {}
luanji_skill.name = "luanji"
table.insert(sgs.ai_skills,luanji_skill)
luanji_skill.getTurnUseCard = function(self)
	local first_found,second_found = false,false
	local first_card,second_card
	if self.player:getHandcardNum()>=2
	then
		local cards = self:addHandPile()
		local same_suit = false
		self:sortByKeepValue(cards)
		local useAll = false
		for _,enemy in sgs.list(self.enemies)do
			if enemy:getHp()==1 and not enemy:hasArmorEffect("Vine")
			and not self:hasEightDiagramEffect(enemy) and self:damageIsEffective(enemy,nil,self.player) and self:isWeak(enemy)
			and getCardsNum("Jink",enemy,self.player)+getCardsNum("Peach",enemy,self.player)+getCardsNum("Analeptic",enemy,self.player)<1
			then useAll = true end
		end
		for _,fcard in sgs.list(cards)do
			local fvalueCard = (isCard("Peach",fcard,self.player) or isCard("ExNihilo",fcard,self.player) or isCard("ArcheryAttack",fcard,self.player))
			if useAll then fvalueCard = isCard("ArcheryAttack",fcard,self.player) end
			if not fvalueCard then
				first_card = fcard
				first_found = true
				for _,scard in sgs.list(cards)do
					local svalueCard = (isCard("Peach",scard,self.player) or isCard("ExNihilo",scard,self.player) or isCard("ArcheryAttack",scard,self.player))
					if useAll then svalueCard = (isCard("ArcheryAttack",scard,self.player)) end
					if first_card~=scard and scard:getSuit()==first_card:getSuit()
						and not svalueCard then

						local card_str = ("archery_attack:luanji[%s:%s]=%d+%d"):format("to_be_decided",0,first_card:getId(),scard:getId())
						local archeryattack = sgs.Card_Parse(card_str)

						assert(archeryattack)

						local dummy_use = dummy()
						self:useTrickCard(archeryattack,dummy_use)
						if dummy_use.card then
							second_card = scard
							second_found = true
							break
						end
					end
				end
				if second_card then break end
			end
		end
	end

	if first_found and second_found then
		local first_id = first_card:getId()
		local second_id = second_card:getId()
		local card_str = ("archery_attack:luanji[%s:%s]=%d+%d"):format("to_be_decided",0,first_id,second_id)
		local archeryattack = sgs.Card_Parse(card_str)
		assert(archeryattack)
		return archeryattack
	end
end



sgs.ai_skill_invoke.shuangxiong=function(self,data)
	if self:needBear() then return false end
	if self.player:isSkipped(sgs.Player_Play) or (self.player:getHp()<2 and not (self:getCardsNum("Slash")>1 and self.player:getHandcardNum()>=3)) or #self.enemies==0 then
		return false
	end
	local duel = dummyCard("duel")

	local dummy_use = dummy()
	self:useTrickCard(duel,dummy_use)

	return self.player:getHandcardNum()>=3 and dummy_use.card
end

sgs.ai_cardneed.shuangxiong=function(to,card,self)
	return not self:willSkipDrawPhase(to)
end

local shuangxiong_skill={}
shuangxiong_skill.name="shuangxiong"
table.insert(sgs.ai_skills,shuangxiong_skill)
shuangxiong_skill.getTurnUseCard=function(self)
	local mark = self.player:getMark("shuangxiong")

	local cards = self:addHandPile()
	self:sortByUseValue(cards,true)

	local card
	for _,acard in sgs.list(cards)  do
		if (acard:isRed() and mark==2) or (acard:isBlack() and mark==1) then
			card = acard
			break
		end
	end

	if not card then return nil end
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	local card_str = ("duel:shuangxiong[%s:%s]=%d"):format(suit,number,card_id)
	local skillcard = sgs.Card_Parse(card_str)
	assert(skillcard)
	return skillcard

end



sgs.ai_skill_invoke.mengjin = function(self,data)
	local effect = data:toCardEffect()
	if self:isEnemy(effect.to) then
		if self:doDisCard(effect.to) then
			return true
		end
	end
	if self:isFriend(effect.to) then
		return self:needToThrowArmor(effect.to) or self:doDisCard(effect.to)
	end
	return not self:isFriend(effect.to)
end

sgs.ai_suit_priority.lianhuan= "club|diamond|heart|spade"
sgs.ai_suit_priority.kanpo= "diamond|heart|club|spade"


sgs.ai_skill_invoke.qinyin = function(self,data)
	self:sort(self.friends,"hp")
	self:sort(self.enemies,"hp")
	local up = 0
	local down = 0

	for _,friend in sgs.list(self.friends)do
		down = down-10
		up = up+(friend:isWounded() and 10 or 0)
		if self:hasSkills(sgs.masochism_skill,friend) then
			down = down-5
			if friend:isWounded() then up = up+5 end
		end
		if self:needToLoseHp(friend,nil,nil,true) then down = down+5 end
		if self:needToLoseHp(friend,nil,nil,true,true) and friend:isWounded() then up = up-5 end

		if self:isWeak(friend) then
			if friend:isWounded() then up = up+10+(friend:isLord() and 20 or 0) end
			down = down-10-(friend:isLord() and 40 or 0)
			if friend:getHp()<=1 and not friend:hasSkill("buqu") or friend:getPile("buqu"):length()>4 then
				down = down-20-(friend:isLord() and 40 or 0)
			end
		end
	end

	for _,enemy in sgs.list(self.enemies)do
		down = down+10
		up = up-(enemy:isWounded() and 10 or 0)
		if self:hasSkills(sgs.masochism_skill,enemy) then
			down = down+10
			if enemy:isWounded() then up = up-10 end
		end
		if self:needToLoseHp(enemy,nil,nil,true) then down = down-5 end
		if self:needToLoseHp(enemy,nil,nil,true,true) and enemy:isWounded() then up = up-5 end

		if self:isWeak(enemy) then
			if enemy:isWounded() then up = up-10 end
			down = down+10
			if enemy:getHp()<=1 and not enemy:hasSkill("buqu") then
				down = down+10+((enemy:isLord() and #self.enemies>1) and 20 or 0)
			end
		end
	end

	if down>0 then
		sgs.ai_skill_choice.qinyin = "down"
		return true
	elseif up>0 then
		sgs.ai_skill_choice.qinyin = "up"
		return true
	end
	return false
end

local yeyan_skill = {}
yeyan_skill.name = "yeyan"
table.insert(sgs.ai_skills,yeyan_skill)
yeyan_skill.getTurnUseCard = function(self)
	if self.player:getHandcardNum()>=4 then
		local spade,club,heart,diamond
		for _,card in sgs.list(self.player:getHandcards())do
			if card:getSuit()==sgs.Card_Spade then spade = true
			elseif card:getSuit()==sgs.Card_Club then club = true
			elseif card:getSuit()==sgs.Card_Heart then heart = true
			elseif card:getSuit()==sgs.Card_Diamond then diamond = true
			end
		end
		if spade and club and diamond and heart then
			self:sort(self.enemies,"hp")
			local target_num = 0
			for _,enemy in sgs.list(self.enemies)do
				if ((enemy:hasArmorEffect("Vine") or enemy:getHp()<=3) and not enemy:isChained())
				or (enemy:isChained() and self:isGoodChainTarget(enemy,nil,nil,3))
				then target_num = target_num+1 end
			end
			if target_num>=1 then
				return sgs.Card_Parse("@GreatYeyanCard=.")
			end
		end
	end

	self.yeyanchained = false
	if self.player:getHp()+self:getCardsNum("Peach")+self:getCardsNum("Analeptic")<=2 then
		return sgs.Card_Parse("@SmallYeyanCard=.")
	end
	local target_num = 0
	local chained = 0
	for _,enemy in sgs.list(self.enemies)do
                if ((enemy:hasArmorEffect("Vine") or enemy:getMark("&kuangfeng")>0) or enemy:getHp()<=1)
			and not (self.role=="renegade" and enemy:isLord()) then
			target_num = target_num+1
		end
	end
	for _,enemy in sgs.list(self.enemies)do
		if enemy:isChained() and self:isGoodChainTarget(enemy)
		then
			if chained==0 then target_num = target_num +1 end
			chained = chained+1
		end
	end
	self.yeyanchained = (chained>1)
	if target_num>2 or (target_num>1 and self.yeyanchained) or
	(#self.enemies+1==self.room:alivePlayerCount() and self.room:alivePlayerCount()<sgs.Sanguosha:getPlayerCount(self.room:getMode())) then
		return sgs.Card_Parse("@SmallYeyanCard=.")
	end
end

sgs.ai_skill_use_func.GreatYeyanCard = function(card,use,self)
	if self.role=="lord" and (sgs.turncount<=1 or sgs.playerRoles["rebel"]>#self:getChainedEnemies() or self:getAllPeachNum()<4-self.player:getHp()) then
		return
	end
	if self.role=="renegade" and self.player:aliveCount()>2 and self:getCardsNum("Peach")<3-self.player:getHp() then return end
	local cards = self.player:getHandcards()
	cards = sgs.QList2Table(cards)
	self:sortByUseValue(cards,true)
	local need_cards = {}
	local spade,club,heart,diamond
	for _,card in sgs.list(cards)do
		if card:getSuit()==sgs.Card_Spade and not spade then spade = true table.insert(need_cards,card:getId())
		elseif card:getSuit()==sgs.Card_Club and not club then club = true table.insert(need_cards,card:getId())
		elseif card:getSuit()==sgs.Card_Heart and not heart then heart = true table.insert(need_cards,card:getId())
		elseif card:getSuit()==sgs.Card_Diamond and not diamond then diamond = true table.insert(need_cards,card:getId())
		end
	end
	if #need_cards<4 then return end
	local greatyeyan = sgs.Card_Parse("@GreatYeyanCard="..table.concat(need_cards,"+"))
	assert(greatyeyan)

	local first
	self:sort(self.enemies,"hp")
	for _,enemy in sgs.list(self.enemies)do
		if not enemy:hasArmorEffect("SilverLion") and self:objectiveLevel(enemy)>3
		and self:damageIsEffective(enemy,sgs.DamageStruct_Fire)
		and not (enemy:hasSkill("tianxiang") and enemy:getHandcardNum()>0)
		and enemy:isChained() and self:isGoodChainTarget(enemy,nil,nil,3)
		then
            if enemy:hasArmorEffect("Vine") or enemy:getMark("&kuangfeng")>0
			then
				use.card = greatyeyan
				use.to:append(enemy)
				use.to:append(enemy)
				use.to:append(enemy)
				return
			elseif not first then first = enemy end
		end
	end
	if first then
		use.card = greatyeyan
		use.to:append(first)
		use.to:append(first)
		use.to:append(first)
		return
	end

	local second
	for _,enemy in sgs.list(self.enemies)do
		if not enemy:hasArmorEffect("SilverLion") and self:objectiveLevel(enemy)>3 and self:damageIsEffective(enemy,sgs.DamageStruct_Fire)
		and not (enemy:hasSkill("tianxiang") and enemy:getHandcardNum()>0) and not enemy:isChained()
		then
            if enemy:hasArmorEffect("Vine") or enemy:getMark("&kuangfeng")>0
			then
				use.card = greatyeyan
				use.to:append(enemy)
				use.to:append(enemy)
				use.to:append(enemy)
				return
			elseif not second then second = enemy end
		end
	end
	if second then
		use.card = greatyeyan
		use.to:append(second)
		use.to:append(second)
		use.to:append(second)
		return
	end
end

sgs.ai_use_value.GreatYeyanCard = 8
sgs.ai_use_priority.GreatYeyanCard = 9

sgs.ai_card_intention.GreatYeyanCard = 200

sgs.ai_skill_use_func.SmallYeyanCard = function(card,use,self)
	if self.player:getMark("@flame")==0 then return end
	local targets = sgs.SPlayerList()
	self:sort(self.enemies,"hp")
	for _,enemy in sgs.list(self.enemies)do
		if not (enemy:hasSkill("tianxiang") and enemy:getHandcardNum()>0) and self:damageIsEffective(enemy,sgs.DamageStruct_Fire)
        and enemy:isChained() and self:isGoodChainTarget(enemy) and (enemy:hasArmorEffect("Vine") or enemy:getMark("&kuangfeng")>0)
		then
			targets:append(enemy)
			if targets:length()>=3 then break end
		end
	end
	if targets:length()<3 then
		for _,enemy in sgs.list(self.enemies)do
			if not targets:contains(enemy)
			and not (enemy:hasSkill("tianxiang") and enemy:getHandcardNum()>0)
			and self:damageIsEffective(enemy,sgs.DamageStruct_Fire)
			and enemy:isChained() and self:isGoodChainTarget(enemy)
			then
				targets:append(enemy)
				if targets:length()>=3 then break end
			end
		end
	end
	if targets:length()<3 then
		for _,enemy in sgs.list(self.enemies)do
			if not targets:contains(enemy)
				and not (enemy:hasSkill("tianxiang") and enemy:getHandcardNum()>0) and self:damageIsEffective(enemy,sgs.DamageStruct_Fire)
                                and not enemy:isChained() and (enemy:hasArmorEffect("Vine") or enemy:getMark("&kuangfeng")>0) then
				targets:append(enemy)
				if targets:length()>=3 then break end
			end
		end
	end
	if targets:length()<3 then
		for _,enemy in sgs.list(self.enemies)do
			if not targets:contains(enemy)
				and not (enemy:hasSkill("tianxiang") and enemy:getHandcardNum()>0) and self:damageIsEffective(enemy,sgs.DamageStruct_Fire)
				and not enemy:isChained() then
				targets:append(enemy)
				if targets:length()>=3 then break end
			end
		end
	end
	if targets:length()>0 then
		use.card = card
		use.to = targets
	end
end

sgs.ai_card_intention.SmallYeyanCard = 80
sgs.ai_use_priority.SmallYeyanCard = 2.3

sgs.ai_skill_discard.qixing = function(self,discard_num,optional,include_equip)
	local cards = sgs.QList2Table(self.player:getHandcards())
	local to_discard = {}
	local compare_func = function(a,b)
		return self:getKeepValue(a)<self:getKeepValue(b)
	end
	table.sort(cards,compare_func)
	for _,card in sgs.list(cards)do
		if #to_discard>=discard_num then break end
		table.insert(to_discard,card:getId())
	end

	return to_discard
end
sgs.ai_skill_use["@@qixing"] = function(self,prompt)
	local pile = self.player:getPile("stars")
	local piles = {}
	local cards = self.player:getHandcards()
	cards = sgs.QList2Table(cards)
	local max_num = math.min(pile:length(),#cards)
	if pile:isEmpty() or (#cards==0) then
		return "."
	end
	for _,card_id in sgs.list(pile)do
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

	return "@QixingCard="..table.concat(exchange,"+")
end

sgs.ai_skill_use["@@kuangfeng"] = function(self,prompt)
	local friendly_fire
	for _,friend in sgs.list(self.friends_noself)do
                if friend:getMark("&kuangfeng")==0 and self:damageIsEffective(friend,sgs.DamageStruct_Fire) and friend:faceUp() and not self:willSkipPlayPhase(friend)
			and (friend:hasSkill("huoji") or friend:hasWeapon("Fan") or (friend:hasSkill("yeyan") and friend:getMark("@flame")>0)) then
			friendly_fire = true
			break
		end
	end

	local is_chained = 0
	local target = {}
	for _,enemy in sgs.list(self.enemies)do
                if enemy:getMark("&kuangfeng")==0 and self:damageIsEffective(enemy,sgs.DamageStruct_Fire) then
			if enemy:isChained() then
				is_chained = is_chained+1
				table.insert(target,enemy)
			elseif enemy:hasArmorEffect("Vine") then
				table.insert(target,1,enemy)
				break
			end
		end
	end
	local usecard=false
	if friendly_fire and is_chained>1 then usecard=true end
	self:sort(self.friends,"hp")
	if target[1] and not self:isWeak(self.friends[1]) then
		if target[1]:hasArmorEffect("Vine") and friendly_fire then usecard = true end
	end
	if usecard then
		if not target[1] then table.insert(target,self.enemies[1]) end
		if target[1] then return "@KuangfengCard="..self.player:getPile("stars"):first().."->"..target[1]:objectName() else return "." end
	else
		return "."
	end
end

sgs.ai_card_intention.KuangfengCard = 80

sgs.ai_skill_use["@@dawu"] = function(self,prompt)
	self:sort(self.friends_noself,"hp")
	local targets = {}
	local lord = self.room:getLord()
	self:sort(self.friends_noself,"defense")
        if lord and lord:getMark("&dawu")==0 and self:isFriend(lord) and not sgs.isLordHealthy() and not self.player:isLord() and not lord:hasSkill("buqu")
		and not (lord:hasSkill("hunzi") and lord:getMark("hunzi")==0 and lord:getHp()>1) then
			table.insert(targets,lord:objectName())
	else
		for _,friend in sgs.list(self.friends_noself)do
                        if friend:getMark("&dawu")==0 and self:isWeak(friend) and not friend:hasSkill("buqu")
				and not (friend:hasSkill("hunzi") and friend:getMark("hunzi")==0 and friend:getHp()>1) then
					table.insert(targets,friend:objectName())
					break
			end
		end
	end
	if self.player:getPile("stars"):length()>#targets and self:isWeak() then table.insert(targets,self.player:objectName()) end
	if #targets>0 then
		local s = sgs.QList2Table(self.player:getPile("stars"))
		local length = #targets
		for i = 1,#s-length do
			table.remove(s,#s)
		end
		return "@DawuCard="..table.concat(s,"+").."->"..table.concat(targets,"+")
	end
	return "."
end

sgs.ai_card_intention.DawuCard = -70
