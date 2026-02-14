--清俭
sgs.ai_skill_discard.mobileqingjian = function(self,discard_num,min_num,optional,include_equip)
	if #self.friends_noself==0 then return {} end
	local put = {}
	if self:needToThrowLastHandcard(self.player,self.player:getHandcardNum()) then
		for _,c in sgs.qlist(self.player:getCards("h"))do
			table.insert(put,c:getEffectiveId())
		end
		return put
	end
	
	local n = self:getOverflow()
	if n<=0 then return {} end
	
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	for _,c in ipairs(cards)do
		if self:isValuableCard(c) then continue end
		table.insert(put,c:getEffectiveId())
		if #put-1>=n then break end
	end
	return put
end

sgs.ai_skill_use["@@mobileqingjian!"] = function(self,prompt,method)
	local card_ids = {}
	for _,id in sgs.qlist(self.player:getPile("mobileqingjian"))do
		table.insert(card_ids,id)
	end
	if #self.friends_noself>0 then
		local target,id = sgs.ai_skill_askforyiji.miji(self,card_ids)
		if target and id then
			return "@MobileQingjianCard="..id.."->"..target:objectName()
		end
	end
	
	for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if not self:isEnemy(p) then
			return "@MobileQingjianCard="..table.concat(card_ids,"+").."->"..p:objectName()
		end
	end
	
	return "@MobileQingjianCard="..table.concat(card_ids,"+").."->"..self.enemies[1]:objectName()
end

--奋激
sgs.ai_skill_invoke.mobilefenji = function(self,data)
	if self.player:getHp()>0
	or hasBuquEffect(self.player)
	or self:getSaveNum(true)>0
	then
		local target = self.room:getCurrent()
		if self:isFriend(target)
		and self:canDraw(target)
		then return true end
	end
	return false
end

--强袭
local mobileqiangxi_skill = {}
mobileqiangxi_skill.name= "mobileqiangxi"
table.insert(sgs.ai_skills,mobileqiangxi_skill)
mobileqiangxi_skill.getTurnUseCard=function(self)
	return sgs.Card_Parse("@MobileQiangxiCard=.")
end

sgs.ai_skill_use_func.MobileQiangxiCard = function(card,use,self)
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
		for _,enemy in ipairs(self.enemies)do
			if self:objectiveLevel(enemy)>3 and not self:cantbeHurt(enemy) and self:damageIsEffective(enemy) and enemy:getMark("mobileqiangxi_used-PlayClear")<=0 then
				if hand_weapon and self.player:distanceTo(enemy)<=self.player:getAttackRange() then
					use.card = sgs.Card_Parse("@MobileQiangxiCard="..hand_weapon:getId())
					use.to:append(enemy)
					break
				end
				if self.player:distanceTo(enemy)<=1 and enemy:getMark("mobileqiangxi_used-PlayClear")<=0 then
					use.card = sgs.Card_Parse("@MobileQiangxiCard="..weapon:getId())
					use.to:append(enemy)
					return
				end
			end
		end
		self.equipsToDec = 0
	else
		self:sort(self.enemies,"hp")
		for _,enemy in ipairs(self.enemies)do
			if self:objectiveLevel(enemy)>3 and not self:cantbeHurt(enemy) and self:damageIsEffective(enemy) and enemy:getMark("mobileqiangxi_used-PlayClear")<=0 then
				if self.player:distanceTo(enemy)<=self.player:getAttackRange() and self.player:getHp()>enemy:getHp() and self.player:getHp()>1 then
					use.card = sgs.Card_Parse("@MobileQiangxiCard=.")
					use.to:append(enemy)
					return
				end
			end
		end
	end
end

sgs.ai_use_value.MobileQiangxiCard = sgs.ai_use_value.QiangxiCard
sgs.ai_card_intention.MobileQiangxiCard = sgs.ai_card_intention.QiangxiCard
sgs.dynamic_value.damage_card.MobileQiangxiCard = sgs.dynamic_value.damage_card.QiangxiCard
sgs.ai_cardneed.mobileqiangxi = sgs.ai_cardneed.qiangxi
sgs.mobileqiangxi_keep_value = sgs.qiangxi_keep_value

--节命
sgs.ai_skill_playerchosen.mobilejieming = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"handcard")
	if self:canDraw() then
		for _,p in ipairs(targets)do
			if self:isFriend(p) and self:canDraw(p) and p:getHandcardNum()+2+ZishuEffect(p)<p:getMaxHp() and ZishuEffect(p)>0 then
			----清俭待补充----
				return p
			end
		end
		for _,p in ipairs(targets)do
			if self:isFriend(p) and self:canDraw(p) and p:getHandcardNum()+2<p:getMaxHp() then
				return p
			end
		end
	end
	
	if not self:canDraw() then
		for _,p in ipairs(targets)do
			if self:isFriend(p) and self:canDraw(p) and p:getHandcardNum()+2+ZishuEffect(p)>=p:getMaxHp() and ZishuEffect(p)>0 then
			----清俭待补充----
				return p
			end
		end
		for _,p in ipairs(targets)do
			if self:isFriend(p) and self:canDraw(p) and p:getHandcardNum()+2>=p:getMaxHp() then
				return p
			end
		end
	end
	
	for _,p in ipairs(targets)do
		if self:isFriend(p) and self:canDraw(p) and ZishuEffect(p)>0 then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if self:isFriend(p) and self:canDraw(p) then
			return p
		end
	end
	
	return nil
end

sgs.ai_playerchosen_intention.mobilejieming = function(self,from,to)
	if self:canDraw(to) then
		sgs.updateIntention(from,to,-80)
	end
end

--涅槃
sgs.ai_skill_invoke.mobileniepan = function(self,data)
	return sgs.ai_skill_invoke.niepan(self,data)
end
----出牌阶段主动使用的待补充----

--双雄
sgs.ai_skill_invoke.mobileshuangxiong = function(self,data)
	if data:toString():startsWith("mobileshuangxiong_invoke")
	then return self:canDraw() end
	return sgs.ai_skill_invoke.shuangxiong(self,data)
end

sgs.ai_cardneed.mobileshuangxiong = function(to,card,self)
	return sgs.ai_cardneed.shuangxiong(to,card,self)
end

sgs.ai_skill_askforag.mobileshuangxiong = function(self,card_ids)
	local fcard,scard = sgs.Sanguosha:getCard(card_ids[1]),sgs.Sanguosha:getCard(card_ids[2])
	if fcard:sameColorWith(scard) then
		return sgs.ai_skill_askforag.amazing_grace(self,card_ids)
	end
	
	local red,black = 0,0
	for _,c in sgs.qlist(self.player:getCards("h"))do  --如果这张牌是【杀】，数量不加1，待补充
		if c:isRed() then red = red+1
		elseif c:isBlack() then black = black+1 end
	end
	
	--有“武圣”等技能需要改变选牌策略，待补充--
	if red==black then
		return sgs.ai_skill_askforag.amazing_grace(self,card_ids)
	elseif red>black then
		if fcard:isBlack() then return fcard:getEffectiveId() end
		if scard:isBlack() then return scard:getEffectiveId() end
	else
		if fcard:isRed() then return fcard:getEffectiveId() end
		if scard:isRed() then return scard:getEffectiveId() end
	end
	return sgs.ai_skill_askforag.amazing_grace(self,card_ids)
end

local mobileshuangxiong_skill={}
mobileshuangxiong_skill.name="mobileshuangxiong"
table.insert(sgs.ai_skills,mobileshuangxiong_skill)
mobileshuangxiong_skill.getTurnUseCard=function(self)
	local red = self.player:getMark("mobileshuangxiong_red-Clear")
	local black = self.player:getMark("mobileshuangxiong_black-Clear")	
	local cards = self:addHandPile()
	self:sortByUseValue(cards,true)
	for dc,acard in ipairs(cards)  do
		if not acard:isRed() and red>0
		or not acard:isBlack() and black>0
		then
			dc = dummyCard("duel")
			dc:setSkillName("mobileshuangxiong")
			dc:addSubcard(acard)
			if dc:isAvailable(self.player)
			then return dc end
		end
	end
end

--乱击
local mobileluanji_skill = {}
mobileluanji_skill.name = "mobileluanji"
table.insert(sgs.ai_skills,mobileluanji_skill)
mobileluanji_skill.getTurnUseCard = function(self)
	local cards = self:addHandPile()
	if #cards>=2
	then
		self:sortByKeepValue(cards)
		local useAll = #cards>4
		for _,enemy in ipairs(self.enemies)do
			if enemy:getHp()==1
			and not enemy:hasArmorEffect("Vine")
			and not self:hasEightDiagramEffect(enemy)
			and self:damageIsEffective(enemy)
			and self:isWeak(enemy) and getCardsNum("Jink",enemy,self.player)+getCardsNum("Peach",enemy,self.player)+getCardsNum("Analeptic",enemy,self.player)<1
			then useAll = true end
		end
		if not useAll then return end
		local suits = self.player:property("mobileluanji_suitstring"):toString():split("+")
		for _,fcard in ipairs(cards)do
			if table.contains(suits,fcard:getSuitString())
			or isCard("ArcheryAttack",fcard,self.player)
			or isCard("ArcheryAttack",fcard,self.player)
			or isCard("ExNihilo",fcard,self.player)
			or isCard("Peach",fcard,self.player)
			then continue end
			for _,scard in ipairs(cards)do
				if table.contains(suits,scard:getSuitString())
				or isCard("ArcheryAttack",scard,self.player)
				or isCard("ArcheryAttack",scard,self.player)
				or isCard("ExNihilo",scard,self.player)
				or isCard("Peach",scard,self.player)
				or fcard==scard
				then continue end
				local archery = dummyCard("archery_attack")
				archery:setSkillName("mobileluanji")
				archery:addSubcard(fcard)
				archery:addSubcard(scard)
				local dummy_use = self:aiUseCard(archery)
				if dummy_use.card then return archery end
			end
		end
	end
end

--再起
sgs.ai_skill_use["@@mobilezaiqi"] = function(self,prompt)
	local n = self.player:getMark("mobilezaiqi-Clear")
	n = math.min(n,#self.friends)
	if n<=0 then return "." end
	
	local lost = self.player:getMaxHp()-self.player:getHp()
	self:sort(self.friends,"handcard")
	local friends = {}
	
	for i = 1,n do
		lost = lost-1
		if lost<=0 and self:canDraw(self.friends[i]) then
			table.insert(friends,self.friends[i]:objectName())
		elseif lost>0 then
			table.insert(friends,self.friends[i]:objectName())
		end
	end
	if #friends>0 then
		return "@MobileZaiqiCard=.->"..table.concat(friends,"+")
	end
	return "."
end

sgs.ai_skill_choice.mobilezaiqi = function(self,choices,data)
	local to = data:toPlayer()
	choices = choices:split("+")
	if not self:isFriend(to) then
		if self.player:isKongcheng() and self:needKongcheng() and table.contains(choices,"recover") then return "recover" end
		return "draw"
	else
		if table.contains(choices,"recover") then return "recover" end
		return "draw"
	end
end

--烈刃
sgs.ai_cardneed.mobilelieren = function(to,card,self)
	return sgs.ai_cardneed.lieren(to,card,self)
end

sgs.ai_skill_invoke.mobilelieren = function(self,data)
	local to = data:toPlayer()
	if not self:isEnemy(to) then return false end
	
	local max_card = self:getMaxCard()
	if not max_card then return false end
	local point = max_card:getNumber()
	if self.player:hasSkill("tianbian") and max_card:getSuit()==sgs.Card_Heart then point = 13 end
	
	if self.player:getHandcardNum()==1 then
		if (self:needKongcheng() or not self:hasLoseHandcardEffective()) and not self:isWeak() then
			self.mobilelieren_card = max_card:getEffectiveId()
			return true
		end
		local card  = self.player:getHandcards():first()
		if card:isKindOf("Jink") or card:isKindOf("Peach") then return false end
	end
	
	if self:doDisCard(to,"he",true) then
		self.mobilelieren_card = max_card:getEffectiveId()
		return true
	end
end

function sgs.ai_skill_pindian.mobilelieren(minusecard,self,requestor)
	return sgs.ai_skill_pindian.lieren(minusecard,self,requestor)
end

--行殇
sgs.ai_skill_invoke.mobilexingshang = function(self,data)
	return self:canDraw() or self.player:getLostHp()>0
end

sgs.ai_skill_choice.mobilexingshang = function(self,choices,data)
	local choices = choices:split("+")
	local who = data:toDeath().who
	if not self:canDraw() and self.player:getLostHp()>0 then return "recover" end
	if self.player:getLostHp()>0 and self:willSkipPlayPhase() then return "recover" end
	if self:canDraw() then
		local peach = false
		for _,c in sgs.qlist(who:getCards("h"))do  --做个弊
			if isCard("Peach",c,self.player) or isCard("Analeptic",c,self.player) then
				peach = true
				break
			end
		end
		if peach then return "get" end
		if self.player:getLostHp()>0 and self.player:getHp()<2 and not hasBuquEffect(self.player) then return "recover" end
		if (self.player:getHp()>=2 or hasBuquEffect(self.player)) and who:getHandcardNum()>=3 then return "get" end  --应该判断有有价值的卡牌且不虚弱，待补充
		if not peach and self.player:getLostHp()>0 then return "recover" end
	else
		if self.player:getLostHp()>0 then return "recover" end
	end
	return "get"
end

--放逐
sgs.ai_skill_playerchosen.mobilefangzhu = function(self,targets)
	return sgs.ai_skill_playerchosen.fangzhu(self,targets)
end

sgs.ai_skill_discard.mobilefangzhu = function(self,discard_num,min_num,optional,include_equip)
	if not self.player:faceUp() or self:needBear() then return {} end
	if self.player:getCardCount()-discard_num<=2 and self.player:getHp()<=2 then return {} end
	if self.player:getHp()>1 or hasBuquEffect(self.player) or self:getSaveNum(true)>0 then
		return self:askForDiscard("dummy",discard_num,min_num,false,include_equip)
	end
	return {}
end

sgs.ai_playerchosen_intention.mobilefangzhu = function(self,from,to)
	return sgs.ai_playerchosen_intention.fangzhu(self,from,to)
end

sgs.ai_need_damaged.mobilefangzhu = function (self,attacker,player)
	if not player:hasSkill("mobilefangzhu") then return false end
	local enemies = self:getEnemies(player)
	if #enemies<1 then return false end
	self:sort(enemies,"defense")
	for _,enemy in ipairs(enemies)do
		if player:getLostHp()<1 and self:toTurnOver(enemy,player:getLostHp()+1) then
			return true
		end
	end
	local friends = self:getFriends(player,true)
	self:sort(friends,"defense")
	for _,friend in ipairs(friends)do
		if not self:toTurnOver(friend,player:getLostHp()+1) then return true end
	end
	return false
end

--破虏
sgs.ai_skill_use["@@mobilepolu"] = function(self,prompt,method)
	local n = self.player:getMark("mobilepolu_usedtimes")+1
	local friends = self.friends_noself
	if self.player:isAlive() then friends = self.friends end
	
	local targets = {}
	for _,p in ipairs(friends)do
		if not self:canDraw(p) or p:isDead() then continue end
		table.insert(targets,p:objectName())
	end
	
	if n==1 then
		for _,p in ipairs(self.enemies)do
			if not p:isKongcheng() or not self:needKongcheng(p) or p:isDead() then continue end
			table.insert(targets,p:objectName())
		end
	end
	return "@MobilePoluCard=.->"..table.concat(targets,"+")
end

sgs.ai_card_intention.MobilePoluCard = function(self,card,from,tos)
	for _,to in ipairs(tos)do
		if hasManjuanEffect(to) then continue end
		local intention = -80
		if to:isKongcheng() and self:needKongcheng(to)
		then intention = 80 end
		sgs.updateIntention(from,to,intention)
	end
end

--酒池
local mobilejiuchi_skill={}
mobilejiuchi_skill.name="mobilejiuchi"
table.insert(sgs.ai_skills,mobilejiuchi_skill)
mobilejiuchi_skill.getTurnUseCard=function(self)
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
	local analeptic = sgs.Card_Parse("analeptic:mobilejiuchi[spade:0]="..card:getEffectiveId())
	if sgs.Analeptic_IsAvailable(self.player,analeptic)
	then return analeptic end
end

sgs.ai_view_as.mobilejiuchi = function(card,player,card_place)
	local str = sgs.ai_view_as.jiuchi(card,player,card_place)
	if not str or str=="" or str==nil then return end
	return string.gsub(str,"jiuchi","mobilejiuchi")
end

function sgs.ai_cardneed.mobilejiuchi(to,card,self)
	return sgs.ai_cardneed.jiuchi(to,card,self)
end

--屯田
sgs.ai_skill_invoke.mobiletuntian = function(self,data)
	return sgs.ai_skill_invoke.tuntian(self,data)
end

--挑衅
local mobiletiaoxin_skill = {}
mobiletiaoxin_skill.name = "mobiletiaoxin"
table.insert(sgs.ai_skills,mobiletiaoxin_skill)
mobiletiaoxin_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@MobileTiaoxinCard=.")
end

sgs.ai_skill_use_func.MobileTiaoxinCard = function(card,use,self)
	local distance = use.DefHorse and 1 or 0
	local targets = {}
	for _,enemy in ipairs(self.enemies)do
		if self:doDisCard(enemy,"he") and self:isTiaoxinTarget(enemy)
		then table.insert(targets,enemy) end
	end

	if #targets==0 then return end

	sgs.ai_use_priority.MobileTiaoxinCard = 8
	if not self.player:getArmor() and not self.player:isKongcheng() then
		for _,card in sgs.qlist(self.player:getCards("h"))do
			if card:isKindOf("Armor") and self:evaluateArmor(card)>3 then
				sgs.ai_use_priority.MobileTiaoxinCard = 5.9
				break
			end
		end
	end

	self:sort(targets,"defenseSlash")
	use.to:append(targets[1])
	use.card = sgs.Card_Parse("@MobileTiaoxinCard=.")
end

sgs.ai_card_intention.MobileTiaoxinCard = sgs.ai_card_intention.TiaoxinCard
sgs.ai_use_priority.MobileTiaoxinCard = sgs.ai_use_priority.TiaoxinCard

sgs.ai_skill_cardask["@mobiletiaoxin-slash"] = function(self,data,pattern,target)
	return sgs.ai_skill_cardask["@tiaoxin-slash"](self,data,pattern,target)
end

--志继
sgs.ai_skill_choice.mobilezhiji = function(self,choice)
	return sgs.ai_skill_choice.zhiji(self,choice)
end

--悲歌
sgs.ai_skill_cardask["@mobilebeige"] = function(self,data)
	return sgs.ai_skill_cardask["@beige"](self,data)
end

--直谏
local mobilezhijian_skill = {}
mobilezhijian_skill.name = "mobilezhijian"
table.insert(sgs.ai_skills,mobilezhijian_skill)
mobilezhijian_skill.getTurnUseCard = function(self)
	local equips = {}
	for _,card in sgs.qlist(self.player:getHandcards())do
		if card:getTypeId()==sgs.Card_TypeEquip then
			table.insert(equips,card)
		end
	end
	if #equips==0 then return end

	return sgs.Card_Parse("@MobileZhijianCard=.")
end

sgs.ai_skill_use_func.MobileZhijianCard = function(card,use,self)
	local equips = {}
	for _,card in sgs.qlist(self.player:getHandcards())do
		if card:isKindOf("Armor") or card:isKindOf("Weapon") then
			if not self:getSameEquip(card) then
			elseif card:isKindOf("GudingBlade") and self:getCardsNum("Slash")>0 then
				local HeavyDamage
				local slash = self:getCard("Slash")
				for _,enemy in ipairs(self.enemies)do
					if self.player:canSlash(enemy,slash,true) and not self:slashProhibit(slash,enemy) and
						self:slashIsEffective(slash,enemy) and not hasJueqingEffect(self.player,enemy) and enemy:isKongcheng() then
							HeavyDamage = true
							break
					end
				end
				if not HeavyDamage then table.insert(equips,card) end
			else
				table.insert(equips,card)
			end
		elseif card:getTypeId()==sgs.Card_TypeEquip then
			table.insert(equips,card)
		end
	end

	if #equips==0 then return end

	local select_equip,target
	for _,friend in ipairs(self.friends_noself)do
		for _,equip in ipairs(equips)do
			local index = equip:getRealCard():toEquipCard():location()
			if not friend:hasEquipArea(index) then continue end
			if not self:getSameEquip(equip,friend) and self:hasSkills(sgs.need_equip_skill.."|"..sgs.lose_equip_skill,friend) then
				target = friend
				select_equip = equip
				break
			end
		end
		if target then break end
		for _,equip in ipairs(equips)do
			local index = equip:getRealCard():toEquipCard():location()
			if not friend:hasEquipArea(index) then continue end
			if not self:getSameEquip(equip,friend) then
				target = friend
				select_equip = equip
				break
			end
		end
		if target then break end
	end

	if not target then return end
	use.to:append(target)
	local zhijian = sgs.Card_Parse("@MobileZhijianCard="..select_equip:getId())
	use.card = zhijian
end

sgs.ai_card_intention.MobileZhijianCard = sgs.ai_card_intention.ZhijianCard
sgs.ai_use_priority.MobileZhijianCard = sgs.ai_use_priority.ZhijianCard
sgs.ai_cardneed.mobilezhijian = sgs.ai_cardneed.zhijian

--放权
sgs.ai_skill_invoke.mobilefangquan = function(self,data)
	return sgs.ai_skill_invoke.fangquan(self,data)
end

sgs.ai_skill_use["@@mobilefangquan"] = function(self,prompt)
	local str = sgs.ai_skill_use["@@fangquan"](self,prompt)
	if not str or str=="" or str==nil then return "." end
	return string.gsub(str,"FangquanCard","MobileFangquanCard")
end

--破军
sgs.ai_skill_invoke.mobilepojun = function(self,data)
    local target = data:toPlayer()
    if not self:isFriend(target) then return true end
    return false
end

sgs.ai_skill_choice.mobilepojun_num = function(self,choices,data)
	local items = choices:split("+")
	return items[#items]
end

sgs.ai_use_revises.mobilepojun = function(self,card,use)
	if card:isKindOf("Slash") then
		card:setFlags("Qinggang")
	end
end

--甘露
local mobileganlu_skill = {}
mobileganlu_skill.name = "mobileganlu"
table.insert(sgs.ai_skills,mobileganlu_skill)
mobileganlu_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@MobileGanluCard=.")
end

sgs.ai_skill_use_func.MobileGanluCard = function(card,use,self)
	local lost_hp = self.player:getLostHp()
	local target,min_friend,max_enemy

	local compare_func = function(a,b)
		return a:getEquips():length()>b:getEquips():length()
	end
	table.sort(self.enemies,compare_func)
	table.sort(self.friends,compare_func)

	self.friends = sgs.reverse(self.friends)

	for _,friend in ipairs(self.friends)do
		for _,enemy in ipairs(self.enemies)do
			if not enemy:hasSkills(sgs.lose_equip_skill)
			and not hasTuntianEffect(enemy,true)
			and friend:getPile("wooden_ox"):isEmpty() then
				local ee = enemy:getEquips():length()
				local fe = friend:getEquips():length()
				local value = self:evaluateArmor(enemy:getArmor(),friend)-self:evaluateArmor(friend:getArmor(),enemy)-self:evaluateArmor(nil,friend)+self:evaluateArmor(nil,enemy)
				if (math.abs(ee-fe)<=lost_hp or friend==self.player) and ee>0 and (ee>fe or ee==fe and value>0) then
					if friend:hasSkills(sgs.lose_equip_skill) then
						use.card = card
						use.to:append(friend)
						use.to:append(enemy)
						return
					elseif not min_friend and not max_enemy then
						min_friend = friend
						max_enemy = enemy
					end
				end
			end
		end
	end
	if min_friend and max_enemy then
		use.card = card
		use.to:append(min_friend)
		use.to:append(max_enemy)
		return
	end

	for _,friend in ipairs(self.friends)do
		if self:needToThrowArmor(friend)
		or ((friend:hasSkills(sgs.lose_equip_skill) or hasTuntianEffect(friend,true)) and friend:getEquips():length()>0) then
			target = friend
			break
		end
	end
	if not target then return end
	for _,friend in ipairs(self.friends)do
		if friend~=target and ((friend==self.player or target==self.player)
			or math.abs(friend:getEquips():length()-target:getEquips():length())<=lost_hp) then
			use.card = card
			use.to:append(friend)
			use.to:append(target)
			return
		end
	end
end

sgs.ai_use_priority.MobileGanluCard = sgs.ai_use_priority.GanluCard
sgs.dynamic_value.control_card.MobileGanluCard = true

sgs.ai_card_intention.MobileGanluCard = function(self,card,from,to)
	local compare_func = function(a,b)
		return a:getEquips():length()<b:getEquips():length()
	end
	table.sort(to,compare_func)
	for i = 1,2,1 do
		if to[i]:hasArmorEffect("SilverLion") then
			sgs.updateIntention(from,to[i],-20)
			break
		end
	end
	if to[1]:getEquips():length()<to[2]:getEquips():length() then
		sgs.updateIntention(from,to[1],-80)
	end
end

--节钺
sgs.ai_skill_askforyiji.mobilejieyue = function(self,card_ids)
    local cards = {}
	for c,id in sgs.list(card_ids)do
		c = sgs.Sanguosha:getCard(id)
		table.insert(cards,c)
	end
    self:sortByKeepValue(cards) -- 按保留值排序
	local tos = self.room:getOtherPlayers(self.player)
	tos = self:sort(tos,"card")
	for _,p in sgs.list(tos)do
	   	if self:isFriend(p)
		then return p,cards[1]:getEffectiveId() end
	end
	self:sort(tos,"card",true)
	for _,p in sgs.list(tos)do
	   	if not self:isEnemy(p)
		then return p,cards[1]:getEffectiveId() end
	end
	for _,p in sgs.list(tos)do
	   	if self:isEnemy(p)
		and #cards>2
		then return p,cards[1]:getEffectiveId() end
	end
end

sgs.ai_skill_use["@@mobilejieyue"] = function(self,prompt)
	local valid = {}
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if #cards-#valid<=1 then break end
    	table.insert(valid,h:getEffectiveId())
	end
    cards = self.player:getCards("e")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
   	local target = self.room:getCurrent()
	local n = 0
	for _,h in sgs.list(cards)do
		if #cards-n<=1 then break end
    	table.insert(valid,h:getEffectiveId())
		n = n+1
	end
	if self:isEnemy(target)
	and #valid<4
	then
		return string.format("@MobileJieyueCard=%s",table.concat(valid,"+"))
	end
end

--伏枥
sgs.ai_skill_invoke.mobilefuli = true

--安恤
local mobileanxu_skill = {}
mobileanxu_skill.name = "mobileanxu"
table.insert(sgs.ai_skills,mobileanxu_skill)
mobileanxu_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@MobileAnxuCard=.")
end

sgs.ai_skill_use_func.MobileAnxuCard = function(card,use,self)
	if #self.enemies==0 then return end
	self:sort(self.friends_noself,"handcard")
	
	--friend获得enemy的牌
	for _,friend in ipairs(self.friends_noself)do
		if not self:canDraw(friend) or ZishuEffect(friend)<=0 then continue end
		local n = 2+ZishuEffect(friend)
		for _,enemy in ipairs(self.enemies)do
			if not self:doDisCard(enemy,"he",true) or friend:getHandcardNum()+n>enemy:getHandcardNum() then continue end
			use.card = card
			use.to:append(friend)
			use.to:append(enemy)
			return
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if not self:canDraw(friend) then continue end
		for _,enemy in ipairs(self.enemies)do
			if not self:doDisCard(enemy,"he",true) or friend:getHandcardNum()+2>enemy:getHandcardNum() then continue end
			use.card = card
			use.to:append(friend)
			use.to:append(enemy)
			return
		end
	end
	
	--friend获得中立角色的牌
	for _,friend in ipairs(self.friends_noself)do
		if not self:canDraw(friend) or ZishuEffect(friend)<=0 then continue end
		local n = 2+ZishuEffect(friend)
		for _,p in sgs.qlist(self.room:getOtherPlayers(friend))do
			if self:isFriend(p) or self:isEnemy(p) or not self:doDisCard(p,"he",true) or friend:getHandcardNum()+n>p:getHandcardNum() then continue end
			use.card = card
			use.to:append(friend)
			use.to:append(p)
			return
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if not self:canDraw(friend) then continue end
		for _,p in sgs.qlist(self.room:getOtherPlayers(friend))do
			if self:isFriend(p) or self:isEnemy(p) or self:doDisCard(p,"he",true) or friend:getHandcardNum()+2>p:getHandcardNum() then continue end
			use.card = card
			use.to:append(friend)
			use.to:append(p)
			return
		end
	end
	
	--friend获得friend的牌
	local friends = self.friends_noself
	friends = sgs.reverse(friends)
	
	for _,friend in ipairs(self.friends_noself)do
		if not self:canDraw(friend) or ZishuEffect(friend)<=0 then continue end
		local n = 2+ZishuEffect(friend)
		for _,fri in ipairs(friends)do
			if friend:objectName()==fri:objectName() then continue end
			if self:doDisCard(fri,"he",true) and friend:getHandcardNum()+n>fri:getHandcardNum() then
				use.card = card
				use.to:append(friend)
				use.to:append(fri)
				return
			end
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if not self:canDraw(friend) then continue end
		for _,fri in ipairs(friends)do
			if friend:objectName()==fri:objectName() then continue end
			if self:doDisCard(fri,"he",true) and friend:getHandcardNum()+2>fri:getHandcardNum() then
				use.card = card
				use.to:append(friend)
				use.to:append(fri)
				return
			end
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if not self:canDraw(friend) or ZishuEffect(friend)<=0 then continue end
		local n = 2+ZishuEffect(friend)
		for _,fri in ipairs(friends)do
			if friend:objectName()==fri:objectName() then continue end
			if friend:getHandcardNum()+n>fri:getHandcardNum() then
				use.card = card
				use.to:append(friend)
				use.to:append(fri)
				return
			end
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if not self:canDraw(friend) then continue end
		for _,fri in ipairs(friends)do
			if friend:objectName()==fri:objectName() then continue end
			if friend:getHandcardNum()+2>fri:getHandcardNum() then
				use.card = card
				use.to:append(friend)
				use.to:append(fri)
				return
			end
		end
	end
	
	--friend获得enemy的牌
	for _,friend in ipairs(self.friends_noself)do
		if not self:canDraw(friend) then continue end
		for _,enemy in ipairs(self.enemies)do
			if not self:doDisCard(enemy,"he",true) then continue end
			use.card = card
			use.to:append(friend)
			use.to:append(enemy)
			return
		end
	end
	
	--friend获得中立角色的牌
	for _,friend in ipairs(self.friends_noself)do
		if not self:canDraw(friend) then continue end
		for _,p in sgs.qlist(self.room:getOtherPlayers(friend))do
			if p==self.player or not self:doDisCard(p,"he",true) then continue end
			use.card = card
			use.to:append(friend)
			use.to:append(p)
			return
		end
	end
	
	--enemy获得enemy来破坏kongcheng，待补充
end

sgs.ai_card_intention.MobileAnxuCard = sgs.ai_card_intention.AnxuCard
sgs.ai_use_priority.MobileAnxuCard = sgs.ai_use_priority.AnxuCard

sgs.ai_skill_invoke.mobileanxu = function(self,data)
	local player = data:toPlayer()
	return self:isFriend(player) and self:canDraw(player)
end

--旋风
sgs.ai_skill_invoke.mobilexuanfeng = function(self,data)
	local from,card,to = self:moveField(nil,"e",self.room:getOtherPlayers(self.player),self.room:getOtherPlayers(self.player))
	if from and card and to then
		sgs.ai_skill_choice.mobilexuanfeng = "move"
		sgs.ai_skill_playerchosen.mobilexuanfeng_from = from
		sgs.ai_skill_cardchosen.mobilexuanfeng = card:getId()
		sgs.ai_skill_playerchosen.mobilexuanfeng_to = to
		return true
	end
	if sgs.ai_skill_invoke.xuanfeng(self,data) then
		sgs.ai_skill_choice.mobilexuanfeng = "discard"
		return true
	end
	return false
end

sgs.ai_skill_playerchosen.mobilexuanfeng = function(self,targets)
	return sgs.ai_skill_playerchosen.xuanfeng(self,targets)
end

sgs.ai_skill_cardchosen.mobilexuanfeng = function(self,who,flags)
	return sgs.ai_skill_cardchosen.xuanfeng(self,who,flags)
end

sgs.mobilexuanfeng_keep_value = sgs.xuanfeng_keep_value
sgs.ai_cardneed.mobilexuanfeng = sgs.ai_cardneed.xuanfeng

--酒诗
function sgs.ai_cardsview.mobilejiushi(self,class_name,player)
	if player:faceUp()
	then
		return ("analeptic:mobilejiushi[no_suit:0]=.")
	end
end

sgs.ai_skill_invoke.mobilejiushi = true

--弓骑
local mobilegongqi_skill = {}
mobilegongqi_skill.name = "mobilegongqi"
table.insert(sgs.ai_skills,mobilegongqi_skill)
mobilegongqi_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@MobileGongqiCard=.")
end

sgs.ai_skill_use_func.MobileGongqiCard = function(card,use,self)
	local to = self:findPlayerToDiscard("he",false,true)[1]
	if not to then return end
	
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	
	local dis = {}
	for _,c in ipairs(cards)do
		if c:isKindOf("BasicCard") then continue end
		table.insert(dis,c)
	end
	if #dis<=0 then return end
	
	for _,c in ipairs(dis)do
		if (c:isKindOf("Dismantlement") or c:isKindOf("Snatch")) and self.player:canUse(c)
		then
			if self:aiUseCard(c).card
			then continue end
		end
		use.card = sgs.Card_Parse("@MobileGongqiCard="..c:getEffectiveId())
		use.to:append(to)
		return
	end
end

sgs.ai_use_priority.MobileGongqiCard = sgs.ai_use_priority.Dismantlement

--权计
sgs.ai_skill_invoke.mobilequanji = function(self,data)
	return sgs.ai_skill_invoke.quanji(self,data)
end

sgs.ai_skill_discard.mobilequanji = function(self)
	return sgs.ai_skill_discard.quanji(self)
end

--疠火
function sgs.ai_cardneed.mobilelihuo(to,card,self)
	local slash = card:isKindOf("Slash") and not (card:isKindOf("FireSlash") or card:isKindOf("ThunderSlash"))
	return slash and getKnownCard(to,self.player,"Slash",false)==0
end

sgs.ai_skill_invoke.mobilelihuo = function(self,data)
	local use = data:toCardUse()
	local slash = use.card
	if slash:isVirtualCard() and slash:subcardsLength()>0 then
		for _,player in sgs.qlist(use.to)do
			if self:isEnemy(player) and self:isGoodTarget(player,self.enemies,slash)
			then
				if player:isChained() then return self:isGoodChainTarget(player,"F") end
				if player:hasArmorEffect("Vine") then return true end
			end
		end
	else
		return sgs.ai_skill_invoke.lihuo(self,data)
	end
	return false
end

--纵玄
local mobilezongxuan_skill = {}
mobilezongxuan_skill.name = "mobilezongxuan"
table.insert(sgs.ai_skills,mobilezongxuan_skill)
mobilezongxuan_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@MobileZongxuanCard=.")
end

sgs.ai_skill_use_func.MobileZongxuanCard = function(card,use,self)
	use.card = card
end

sgs.ai_card_intention.MobileZongxuanCard = 10

sgs.ai_skill_discard.mobilezongxuan = function(self,discard_num,min_num,optional,include_equip)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	return {cards[1]:getEffectiveId()}
end

sgs.ai_skill_use["@@mobilezongxuan"] = function(self,prompt)
	if self.player:getPhase()>=sgs.Player_Finish then return "." end
	local valuable
	for _,id in sgs.list(self.player:getTag("mobilezongxuanForAI"):toIntList())do
		local card = sgs.Sanguosha:getCard(id)
		if card:isKindOf("EquipCard") then
			for _,friend in ipairs(self.friends)do
				if not (card:isKindOf("Armor") and not friend:getArmor() and friend:hasSkills("bazhen|yizhong"))
				and (not self:getSameEquip(card,friend) or card:isKindOf("DefensiveHorse") or card:isKindOf("OffensiveHorse")
					or (card:isKindOf("Weapon") and self:evaluateWeapon(card)>self:evaluateWeapon(friend:getWeapon())-1)) then
					return "@MobileZongxuanPutCard="..id
				end
			end
		elseif self:isValuableCard(card) and not valuable then
			valuable = id
		end
	end
	if valuable then
		return "@MobileZongxuanPutCard="..valuable
	end
	return "."
end

--峻刑
addAiSkills("mobilejunxing").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards,nil,"j")
  	if #cards>0 then
		return sgs.Card_Parse("@MobileJunxingCard="..cards[1]:getEffectiveId())
	end
end

sgs.ai_skill_use_func["MobileJunxingCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,ep in sgs.list(self.enemies)do
		use.card = card
		use.to:append(ep)
		return
	end
end

sgs.ai_use_value.MobileJunxingCard = 9.4
sgs.ai_use_priority.MobileJunxingCard = 0.8

sgs.ai_skill_discard.mobilejunxing = function(self,max,min)
	if not self.player:faceUp() then return {} end
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,"j")
	local to_cards = {}
   	for _,h in sgs.list(cards)do
		if not self:isWeak() then
         	table.insert(to_cards,h:getEffectiveId())
			if #to_cards>=min then return to_cards end
		end
	end
	return {}
end

--绝策
sgs.ai_skill_playerchosen.mobilejuece = function(self,targets)
	return sgs.ai_skill_playerchosen.juece(self,targets)
end

sgs.ai_playerchosen_intention.mobilejuece = function(self,from,to)
	return sgs.ai_playerchosen_intention.juece(self,from,to)
end

--灭计
addAiSkills("mobilemieji").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		if c:isBlack()
		and c:isKindOf("TrickCard")
		then
			return sgs.Card_Parse("@MobileMiejiCard="..c:getEffectiveId())
		end
	end
end

sgs.ai_skill_use_func["MobileMiejiCard"] = function(card,use,self)
	self:sort(self.enemies,"card",true)
	for _,ep in sgs.list(self.enemies)do
		if ep:getCardCount()>1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:getCardCount()>0
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.MobileMiejiCard = 9.4
sgs.ai_use_priority.MobileMiejiCard = 3.8

sgs.ai_skill_use["@@mobilemieji!"] = function(self,prompt)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	local ids = {}
	for _,c in ipairs(cards)do
		if c:isKindOf("TrickCard")
		then
			return "@MobileMiejiDiscardCard="..c:getEffectiveId()
		end
		if #ids>1
		then
			return "@MobileMiejiDiscardCard="..table.concat(ids,"+")
		end
		table.insert(ids,c:getId())
	end
	return "."
end

--陷阵
local mobilexianzhen_skill = {}
mobilexianzhen_skill.name = "mobilexianzhen"
table.insert(sgs.ai_skills,mobilexianzhen_skill)
mobilexianzhen_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@MobileXianzhenCard=.")
end

sgs.ai_skill_use_func.MobileXianzhenCard = function(card,use,self)
	self:sort(self.enemies,"handcard")
	local max_card = self:getMaxCard()
	local max_point = max_card:getNumber()
	local slashcount = self:getCardsNum("Slash")
	if max_card:isKindOf("Slash") then slashcount = slashcount-1 end

	if slashcount>0  then
		for _,enemy in ipairs(self.enemies)do
			if enemy:hasFlag("AI_HuangtianPindian") and enemy:getHandcardNum()==1 and self.player:canPindian(enemy) then
				self.mobilexianzhen_card = max_card:getId()
				use.card = sgs.Card_Parse("@MobileXianzhenCard=.")
				use.to:append(enemy)
				enemy:setFlags("-AI_HuangtianPindian")
				return
			end
		end

		--[[local slash = self:getCard("Slash")
		assert(slash)
		local dummy_use = dummy()
		self:useBasicCard(slash,dummy_use)--]]

		for _,enemy in ipairs(self.enemies)do
			if not (enemy:hasSkill("kongcheng") and enemy:getHandcardNum()==1) and self.player:canPindian(enemy) and self:canAttack(enemy,self.player)
				and not self:canLiuli(enemy,self.friends_noself) and not self:findLeijiTarget(enemy,50,self.player) then
				local enemy_max_card = self:getMaxCard(enemy)
				local enemy_max_point =enemy_max_card and enemy_max_card:getNumber() or 100
				if max_point>enemy_max_point then
					self.mobilexianzhen_card = max_card:getId()
					use.card = sgs.Card_Parse("@MobileXianzhenCard=.")
					use.to:append(enemy)
					return
				end
			end
		end
		for _,enemy in ipairs(self.enemies)do
			if not (enemy:hasSkill("kongcheng") and enemy:getHandcardNum()==1) and self.player:canPindian(enemy) and self:canAttack(enemy,self.player)
				and not self:canLiuli(enemy,self.friends_noself) and not self:findLeijiTarget(enemy,50,self.player) then
				if max_point>=10 then
					self.mobilexianzhen_card = max_card:getId()
					use.card = sgs.Card_Parse("@MobileXianzhenCard=.")
					use.to:append(enemy)
					return
				end
			end
		end
	end
	local cards = sgs.QList2Table(self.player:getHandcards())
	self:sortByUseValue(cards,true)
	if (self:getUseValue(cards[1])<6 and self:getKeepValue(cards[1])<6) or self:getOverflow()>0 then
		for _,enemy in ipairs(self.enemies)do
			if not (enemy:hasSkill("kongcheng") and enemy:getHandcardNum()==1) and self.player:canPindian(enemy) and not enemy:hasSkills("tuntian+zaoxian") then
				self.mobilexianzhen_card = cards[1]:getId()
				use.card = sgs.Card_Parse("@MobileXianzhenCard=.")
				use.to:append(enemy)
				return
			end
		end
	end
end

sgs.ai_cardneed.mobilexianzhen = function(to,card,self)
	return sgs.ai_cardneed.xianzhen(to,card,self)
end

function sgs.ai_skill_pindian.mobilexianzhen(minusecard,self,requestor)
	return sgs.ai_skill_pindian.xianzhen(minusecard,self,requestor)
end

sgs.ai_card_intention.MobileXianzhenCard = sgs.ai_card_intention.XianzhenCard
sgs.dynamic_value.control_card.MobileXianzhenCard = true
sgs.ai_use_value.MobileXianzhenCard = sgs.ai_use_value.XianzhenCard
sgs.ai_use_priority.MobileXianzhenCard = sgs.ai_use_priority.XianzhenCard

--巧说
local mobileqiaoshui_skill = {}
mobileqiaoshui_skill.name = "mobileqiaoshui"
table.insert(sgs.ai_skills,mobileqiaoshui_skill)
mobileqiaoshui_skill.getTurnUseCard = function(self)
	if self:needBear() then return end
	return sgs.Card_Parse("@MobileQiaoshuiCard=.")
end

sgs.ai_skill_use_func.MobileQiaoshuiCard = function(card,use,self)
	local trick_num = 0
	for _,card in sgs.qlist(self.player:getHandcards())do
		if card:isNDTrick() and not card:isKindOf("Nullification") then trick_num = trick_num+1 end
	end
	self:sort(self.enemies,"handcard")
	local max_card = self:getMaxCard()
	if not max_card then return end
	local max_point = max_card:getNumber()
	if self.player:hasSkill("tianbian") and max_card:getSuit()==sgs.Card_Heart then max_point = 13 end

	for _,enemy in ipairs(self.enemies)do
		if not (enemy:hasSkill("kongcheng") and enemy:getHandcardNum()==1) and self.player:canPindian(enemy) then
			local enemy_max_card = self:getMaxCard(enemy)
			local enemy_max_point = enemy_max_card and enemy_max_card:getNumber() or 100
			if max_point>enemy_max_point then
				self.mobileqiaoshui_card = max_card:getEffectiveId()
				use.card = card
				use.to:append(enemy)
				return
			end
		end
	end
	for _,enemy in ipairs(self.enemies)do
		if not (enemy:hasSkill("kongcheng") and enemy:getHandcardNum()==1) and self.player:canPindian(enemy) then
			if max_point>=10 then
				self.mobileqiaoshui_card = max_card:getEffectiveId()
				use.card = card
				use.to:append(enemy)
				return
			end
		end
	end

	self:sort(self.friends_noself,"handcard")
	for index = #self.friends_noself,1,-1 do
		local friend = self.friends_noself[index]
		if self.player:canPindian(friend) then
			local friend_min_card = self:getMinCard(friend)
			local friend_min_point = friend_min_card and friend_min_card:getNumber() or 100
			if max_point>friend_min_point then
				self.mobileqiaoshui_card = max_card:getEffectiveId()
				use.card = card
				use.to:append(friend)
				return
			end
		end
	end

	local zhugeliang = self.room:findPlayerBySkillName("kongcheng")
	if zhugeliang and self:isFriend(zhugeliang) and zhugeliang:getHandcardNum()==1 and zhugeliang:objectName()~=self.player:objectName()
		and self.player:canPindian(zhugeliang) then
		if max_point>=7 then
			self.mobileqiaoshui_card = max_card:getEffectiveId()
			use.card = card
			use.to:append(zhugeliang)
			return
		end
	end

	for index = #self.friends_noself,1,-1 do
		local friend = self.friends_noself[index]
		if self.player:canPindian(friend) then
			if max_point>=7 then
				self.mobileqiaoshui_card = max_card:getEffectiveId()
				use.card = card
				use.to:append(friend)
				return
			end
		end
	end

	if trick_num==0 or (trick_num<=2 and self.player:hasSkill("zongshih")) and not self:isValuableCard(max_card) then
		for _,enemy in ipairs(self.enemies)do
			if not (enemy:hasSkill("kongcheng") and enemy:getHandcardNum()==1) and self.player:canPindian(enemy) and self:hasLoseHandcardEffective(enemy) then
				self.mobileqiaoshui_card = max_card:getEffectiveId()
				use.card = card
				use.to:append(enemy)
				return
			end
		end
	end
	return "."
end

sgs.ai_use_priority.MobileQiaoshuiCard = 8
sgs.ai_card_intention.MobileQiaoshuiCard = 0

sgs.ai_skill_choice.mobileqiaoshui = function(self,choices,data)
	local use = data:toCardUse()
	if use.card:isKindOf("Collateral") then
		local dummy_use = dummy(true,0,use.to)
		self:useCardCollateral(use.card,dummy_use)
		if dummy_use.card and dummy_use.to:length()==2 then
			local first = dummy_use.to:at(0):objectName()
			local second = dummy_use.to:at(1):objectName()
			self.mobileqiaoshui_collateral = { first,second }
			return "add"
		else
			self.mobileqiaoshui_collateral = nil
		end
	elseif use.card:isKindOf("Analeptic") then
	elseif use.card:isKindOf("Peach") then
		self:sort(self.friends_noself,"hp")
		for _,friend in ipairs(self.friends_noself)do
			if friend:isWounded() and friend:getHp()<getBestHp(friend) then
				self.mobileqiaoshui_extra_target = friend
				return "add"
			end
		end
	elseif use.card:isKindOf("ExNihilo") then
		local friend = self:findPlayerToDraw(false,2)
		if friend then
			self.mobileqiaoshui_extra_target = friend
			return "add"
		end
	elseif use.card:isKindOf("GodSalvation") then
		self:sort(self.enemies,"hp")
		for _,enemy in ipairs(self.enemies)do
			if enemy:isWounded() and self:hasTrickEffective(use.card,enemy,self.player) then
				self.mobileqiaoshui_remove_target = enemy
				return "remove"
			end
		end
	elseif use.card:isKindOf("AmazingGrace") then
		self:sort(self.enemies)
		for _,enemy in ipairs(self.enemies)do
			if self:hasTrickEffective(use.card,enemy,self.player) and not hasManjuanEffect(enemy)
				and not self:needKongcheng(enemy,true) then
				self.mobileqiaoshui_remove_target = enemy
				return "remove"
			end
		end
	elseif use.card:isKindOf("AOE") then
		self:sort(self.friends_noself)
		local lord = self.room:getLord()
		if lord and lord:objectName()~=self.player:objectName() and self:isFriend(lord) and self:isWeak(lord) then
			self.mobileqiaoshui_remove_target = lord
			return "remove"
		end
		for _,friend in ipairs(self.friends_noself)do
			if self:hasTrickEffective(use.card,friend,self.player) then
				self.mobileqiaoshui_remove_target = friend
				return "remove"
			end
		end
	elseif use.card:isKindOf("Snatch") or use.card:isKindOf("Dismantlement") then
		local trick = dummyCard(use.card:objectName())
		trick:setSkillName("qiaoshui")
		local dummy_use = dummy(true,0,use.to)
		self:useCardDismantlement(trick,dummy_use)
		if dummy_use.card and dummy_use.to:length()>0 then
			self.mobileqiaoshui_extra_target = dummy_use.to:first()
			return "add"
		end
	elseif use.card:isKindOf("Slash") then
		local slash = dummyCard(use.card:objectName())
		slash:setSkillName("qiaoshui")
		local dummy_use = dummy(true,0,use.to)
		self:useCardSlash(slash,dummy_use)
		if dummy_use.card and dummy_use.to:length()>0 then
			self.mobileqiaoshui_extra_target = dummy_use.to:first()
			return "add"
		end
	else
		local dummy_use = dummy(true,0,use.to)
		self:useCardByClassName(use.card,dummy_use)
		if dummy_use.card and dummy_use.to:length()>0 then
			self.mobileqiaoshui_extra_target = dummy_use.to:first()
			return "add"
		end
	end
	self.mobileqiaoshui_extra_target = nil
	self.mobileqiaoshui_remove_target = nil
	return "cancel"
end

sgs.ai_skill_playerchosen.mobileqiaoshui = function(self,targets)
	if not self.mobileqiaoshui_extra_target and not self.mobileqiaoshui_remove_target then self.room:writeToConsole("MobileQiaoshui player chosen error!!") end
	return self.mobileqiaoshui_extra_target or self.mobileqiaoshui_remove_target
end

sgs.ai_skill_use["@@mobileqiaoshui!"] = function(self,prompt) -- extra target for Collateral
	if not self.mobileqiaoshui_collateral then self.room:writeToConsole("MobileQiaoshui player chosen error!!") end
	return "@ExtraCollateralCard=.->"..self.mobileqiaoshui_collateral[1].."+"..self.mobileqiaoshui_collateral[2]
end

--纵适
sgs.ai_skill_invoke.mobilezongshih = true

sgs.ai_skill_use["@@mobilezongshih"] = function(self,prompt,method)
	local cards = {}
	local draw = self.player:getTag("mobilezongshih_drawForAI"):toIntList()
	local pdlist = self.player:getTag("mobilezongshih_pindianForAI"):toIntList()
	for _,id in sgs.qlist(draw)do
		local c = sgs.Sanguosha:getCard(id)
		if self:canDraw() then
			table.insert(cards,c)
		else
			if self:willUse(self.player,c) then
				table.insert(cards,c)
			end
		end
	end
	for _,id in sgs.qlist(pdlist)do
		local c = sgs.Sanguosha:getCard(id)
		if self:canDraw() then
			table.insert(cards,c)
		else
			if self:willUse(self.player,c) then
				table.insert(cards,c)
			end
		end
	end
	if #cards<=0 then return "." end
	self:sortByCardNeed(cards,true)
	return "@MobileZongshihCard="..cards[1]:getEffectiveId()
end

--胆守
sgs.ai_skill_discard.mobiledanshou = function(self,discard_num,min_num,optional,include_equip)
	local target = self.player:getTag("mobiledanshou_target"):toPlayer()
	if not target or target:isDead() then return {} end
	if not self:damageIsEffective(target,sgs.DamageStruct_Normal) then return {} end
	if self:isFriend(target) then
		if not self:needToLoseHp(target,self.player) then return {} end
		if self:getOverflow()>=min_num then
			local ids = self:askForDiscard("dummyreason",discard_num,min_num,false,include_equip)
			for _,id in ipairs(ids)do
				if sgs.Sanguosha:getCard(id):isKindOf("Peach") then return {} end
			end
			return ids
		end
	end
	
	if self:isEnemy(target) then
		if self.player:getCardCount()-discard_num<=2 and self.player:getHp()<=2 then
			if target:getHp()>self:ajustDamage(self.player,target) or hasBuquEffect(target) then
				return {}
			end
		end
		local ids = self:askForDiscard("dummyreason",discard_num,min_num,false,include_equip)
		if target:getHp()>=self:ajustDamage(self.player,target) then
			for _,id in ipairs(ids)do
				if sgs.Sanguosha:getCard(id):isKindOf("Peach") then return {} end
			end
		end
		return ids
	end
	return {}
end

--夺刀
sgs.ai_skill_invoke.mobileduodao = function(self,data)
	local from = data:toPlayer()
	if self:isFriend(from) then
		if from:hasSkills("kofxiaoji|xiaoji") and self:isWeak(damage.from) then return true
		else
			if self:getCardsNum("Slash")==0 or self:willSkipPlayPhase() then return "." end
			local invoke = false
			local range = sgs.weapon_range[from:getWeapon():getClassName()] or 0
			if self.player:hasSkills("anjian|mobileanjian") then
				for _,enemy in ipairs(self.enemies)do
					if not enemy:inMyAttackRange(self.player) and not self.player:inMyAttackRange(enemy) and self.player:distanceTo(enemy)<=range then
						invoke = true
						break
					end
				end
			end
			if not invoke and self:evaluateWeapon(from:getWeapon())>8 then invoke = true end
			if invoke then
				return true
			end
		end
	else
		if from:hasSkill("nosxuanfeng") then
			for _,friend in ipairs(self.friends)do
				if self:isWeak(friend) then return "." end
			end
		else
			return not (self:needKongcheng(self.player,true) and self.player:isKongcheng())
		end
	end
	return false
end

--暗箭
sgs.ai_skill_choice.mobileanjian = function(self,choices,data)
	local target = data:toPlayer()
	local use = self.player:getTag("mobileanjian_usedata"):toCardUse()
	local no_respond_list = use.no_respond_list
	local no_offset_list = use.no_offset_list
	if table.contains(no_respond_list,target:objectName()) or table.contains(no_respond_list,"_ALL_TARGETS") then return "damage" end
	if table.contains(no_offset_list,target:objectName()) or table.contains(no_offset_list,"_ALL_TARGETS") then return "damage" end
	if target:hasArmorEffect("EightDiagram") or getCardsNum("Jink",target,self.player)>0 or target:getHandcardNum()>=3 then return "noresponse" end
	return "damage"
end

--惴恐
sgs.ai_skill_invoke.mobilezhuikong = function(self,data)
	if self.player:getHandcardNum()<=(self:isWeak() and 2 or 1) then return false end
	local current = self.room:getCurrent()
	if not current or self:isFriend(current) then return false end

	local max_card = self:getMaxCard()
	local max_point = max_card:getNumber()
	if self.player:hasSkill("yingyang") then max_point = math.min(max_point+3,13) end
	if not (current:hasSkill("zhiji") and current:getMark("zhiji")==0 and current:getHandcardNum()==1) and not
		(current:hasSkill("mobilezhiji") and current:getMark("mobilezhiji")==0 and current:getHandcardNum()==1) then
		local enemy_max_card = self:getMaxCard(current)
		local enemy_max_point = enemy_max_card and enemy_max_card:getNumber() or 100
		if enemy_max_card and current:hasSkill("yingyang") then enemy_max_point = math.min(enemy_max_point+3,13) end
		if max_point>enemy_max_point or max_point>10 then
			self.mobilezhuikong_card = max_card:getEffectiveId()
			return true
		end
	end
	if current:distanceTo(self.player)==1 and not self:isValuableCard(max_card) then
		self.mobilezhuikong_card = max_card:getEffectiveId()
		return true
	end
	return false
end

--求援
sgs.ai_skill_playerchosen.mobileqiuyuan = function(self,targets)
	return sgs.ai_skill_playerchosen.qiuyuan(self,targets)
end

sgs.ai_skill_cardask["@mobileqiuyuan-give"] = function(self,data,pattern,target)
	return sgs.ai_skill_cardask["@tenyearqiuyuan-give"](self,data,pattern,target)
end

--精策
sgs.ai_skill_invoke.mobilejingce = function(self,data)
	return self:canDraw()
end

addAiSkills("mobiledingpin").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		if self.player:getMark("dingpin_"..c:getType().."-Clear")>0
		or #cards<3 then continue end
		return sgs.Card_Parse("@MobileDingpinCard="..c:getEffectiveId())
	end
end

sgs.ai_skill_use_func["MobileDingpinCard"] = function(card,use,self)
	self:sort(self.friends,"hp")
	if not self.player:faceUp()
	then
		use.card = card
		use.to:append(self.player)
		return
	end
	for _,ep in sgs.list(self.friends)do
		if ep:getHp()<=self.player:getHp()
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.MobileDingpinCard = 9.4
sgs.ai_use_priority.MobileDingpinCard = 4.8

sgs.ai_skill_invoke.mobilezhongyong = function(self,data)
    return true
end

sgs.ai_skill_playerchosen.mobilezhongyong = function(self,players)
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

addAiSkills("mobileshenxing").getTurnUseCard = function(self)
	if self.player:usedTimes("MobileShenxingCard")>=self.player:getHp()
	then return end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	local toids = {}
	local color
  	for _,c in sgs.list(cards)do
		if #toids>=2 then break end
		if table.contains(self.toUse,c)
		or color and color~=c:getColor()
		then continue end
		if self:getKeepValue(c)<4
		then
			color = c:getColor()
			table.insert(toids,c:getEffectiveId())
		end
	end
  	for _,c in sgs.list(cards)do
		if #toids>=2 then break end
		if table.contains(self.toUse,c)
		or table.contains(toids,c:getEffectiveId())
		then continue end
		if self:getKeepValue(c)<4
		then
			table.insert(toids,c:getEffectiveId())
		end
	end
	local ids = #toids>0 and table.concat(toids,"+") or "."
	if #toids>=2 then return sgs.Card_Parse("@MobileShenxingCard="..ids) end
end

sgs.ai_skill_use_func["MobileShenxingCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.MobileShenxingCard = 4.4
sgs.ai_use_priority.MobileShenxingCard = 2.8

sgs.ai_skill_use["@@mobilebingyi"] = function(self,prompt)
	local valid,n = {},0
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
	for _,h in sgs.list(cards)do
		if h:getColor()~=cards[1]:getColor()
		then return end
    	n = n+1
	end
    table.insert(valid,self.player:objectName())
	for _,p in sgs.list(self.player:getAliveSiblings())do
      	if self:isFriend(p) and #valid<n
    	then table.insert(valid,p:objectName()) end
	end
	if #valid<1 then return end
	return ("@MobileBingyiCard=.->"..table.concat(valid,"+"))
end

sgs.ai_skill_invoke.mobileqieting = function(self,data)
    return true
end

sgs.ai_skill_choice.mobileqieting = function(self,choices,data)
	local target = data:toPlayer()
	local items = choices:split("+")
	if table.contains(items,"draw")
	and self:isFriend(target)
	then return "draw" end
	if target:getHandcardNum()>=2
	then return items[1] end
	if target:hasEquip()
	then return items[2] end
	if table.contains(items,"draw")
	then return "draw" end
end

addAiSkills("mobilejianying").getTurnUseCard = function(self)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	if #cards<1 then return end
	for _,name in sgs.list(patterns())do
		local c = dummyCard(name)
		local card = self.player:getTag("mobilejianying"):toCard()
		if card then c = dummyCard(name,card:getSuit()) end
		if c==nil or self:getCardsNum(c:getClassName())>0
		or c:getTypeId()~=1 then continue end
		c:addSubcard(cards[1])
		if c:isAvailable(self.player) then
         	local dummy = self:aiUseCard(c)
    		if dummy.card and dummy.to then
	           	if c:canRecast()
				and dummy.to:length()<1
				then continue end
				self.jy_to = dummy.to
				sgs.ai_use_priority.MobileJianyingCard = sgs.ai_use_priority[c:getClassName()]
                return sgs.Card_Parse("@MobileJianyingCard="..cards[1]:getEffectiveId()..":"..name)
			end
		end
	end
end

sgs.ai_skill_use_func["MobileJianyingCard"] = function(card,use,self)
	use.card = card
	use.to = self.jy_to
end

sgs.ai_use_value.JinzhiCard = 10.4
sgs.ai_use_priority.JinzhiCard = 10.4

sgs.ai_use_revises.mobilejianying = function(self,card,use)
	local card = self.player:getTag("mobilejianying"):toCard()
	if #self.toUse>0 and card then
		for _,c in sgs.list(self.toUse)do
			if c:getSuit()==card:getSuit()
			then card = c return end
		end
		for _,c in sgs.list(self.toUse)do
			if c:getNumber()==card:getNumber()
			then card = c return end
		end
	end
end

sgs.ai_skill_invoke.mobilejianying = function(self,data)
	return sgs.ai_skill_invoke.jianying(self,data)
end

addAiSkills("mobileyanzhu").getTurnUseCard = function(self)
	return sgs.Card_Parse("@MobileYanzhuCard=.")
end

sgs.ai_skill_use_func["MobileYanzhuCard"] = function(card,use,self)
	for _,ep in sgs.list(self.friends_noself)do
		if self:doDisCard(ep,"ej")
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	self:sort(self.enemies,"equip")
	for _,ep in sgs.list(self.enemies)do
		if ep:getEquips():length()>1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:getCardCount()>1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.MobileYanzhuCard = 9.4
sgs.ai_use_priority.MobileYanzhuCard = 5.8

sgs.ai_skill_use["@@mobilexingxue"] = function(self,prompt)
	local valid = {}
	local destlist = self.room:getAllPlayers()
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist,"hp")
	local xingxue = self.player:property("MobileXingxueLevelUp"):toBool()
    local n = xingxue and self.player:getMaxHp() or self.player:getHp()
	for _,friend in sgs.list(destlist)do
		if #valid>=n then break end
		if self:isFriend(friend) then table.insert(valid,friend:objectName()) end
	end
	for _,friend in sgs.list(destlist)do
		if #valid>=n then break end
		if table.contains(valid,friend:objectName()) then continue end
		if not self:isEnemy(friend) then table.insert(valid,friend:objectName()) end
	end
	if #valid>0
	then
    	return string.format("@MobileXingxueCard=.->%s",table.concat(valid,"+"))
	end
end

sgs.ai_skill_playerchosen.mobileanguo = function(self,players)
    for _,p in sgs.list(self.room:getAllPlayers())do
		if p:getMark("&mobileanguo")>0 and self:isFriend(p) and not self:isWeak(p)
		then return end
	end
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_choice.mobileanguo = function(self,choices,data)
	return "hp"
end

sgs.ai_skill_discard.mobilebenxi = function(self,max,min)
	local useds = self:getTurnUse()
	if #useds<1 then return {} end
   	for _,h in sgs.list(useds)do
		if h:getTypeId()>0 then
			if h:isDamageCard() or h:isNDTrick() then break end
			return {}
		end
	end
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,"j")
	local to_cards = {}
   	for _,h in sgs.list(cards)do
		if #to_cards<=#self.enemies and self:getKeepValue(h)<6 and not table.contains(useds,h) then
         	table.insert(to_cards,h:getEffectiveId())
		end
	end
	return to_cards
end

sgs.ai_skill_playerschosen.mobilebenxi = function(self,players,x,n)
	self.player:setTag("JincanmouData",self.player:getTag("mobilebenxiUse"))
	local tos = {}
    for i=0,x do
		local to = sgs.ai_skill_playerchosen.jincanmou(self,players)
		if to then
			table.insert(tos,p)
			players:removeOne(p)
		else
			break
		end
	end
    return tos
end

sgs.ai_skill_playerschosen.mobilepingkou = function(self,players,x,n)
	local tos = {}
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,p in sgs.list(destlist)do
		if #tos<x and self:isEnemy(p)
		then table.insert(tos,p) end
	end
    for _,p in sgs.list(destlist)do
		if #tos<x and not table.contains(tos,p) and not self:isFriend(p)
		then table.insert(tos,p) end
	end
    return tos
end

addAiSkills("mobilefurong").getTurnUseCard = function(self)
	return sgs.Card_Parse("@MobileFurongCard=.")
end

sgs.ai_skill_use_func["MobileFurongCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,ep in sgs.list(self.enemies)do
		if ep:getCardCount()>1 then
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

sgs.ai_use_value.MobileFurongCard = 9.4
sgs.ai_use_priority.MobileFurongCard = 5.8

addAiSkills("mobileyaoming").getTurnUseCard = function(self)
	return sgs.Card_Parse("@MobileYaomingCard=.")
end

sgs.ai_skill_use_func["MobileYaomingCard"] = function(card,use,self)
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getHandcardNum()>=self.player:getHandcardNum() and self:doDisCard(ep,"he") then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	self:sort(self.enemies)
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>=self.player:getHandcardNum() and self:doDisCard(ep,"he") then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	self:sort(self.friends_noself)
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getHandcardNum()<=self.player:getHandcardNum() and self:canDraw(ep) and self:getOverflow(ep)<1 then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.MobileYaomingCard = 9.4
sgs.ai_use_priority.MobileYaomingCard = 5.8

sgs.ai_skill_use["@@mobileyaoming"] = function(self,prompt)
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getHandcardNum()>=self.player:getHandcardNum() and self:doDisCard(ep,"he") then
			return string.format("@MobileYaomingCard=.->%s",ep:objectName())
		end
	end
	self:sort(self.enemies)
	for _,ep in sgs.list(self.enemies)do
		if ep:getHandcardNum()>=self.player:getHandcardNum() and self:doDisCard(ep,"he") then
			return string.format("@MobileYaomingCard=.->%s",ep:objectName())
		end
	end
	self:sort(self.friends_noself)
	for _,ep in sgs.list(self.friends_noself)do
		if ep:getHandcardNum()<=self.player:getHandcardNum() and self:canDraw(ep) and self:getOverflow(ep)<1 then
			return string.format("@MobileYaomingCard=.->%s",ep:objectName())
		end
	end
end

sgs.ai_skill_choice.mobileyaoming = function(self,choices,data)
	local target = data:toPlayer()
	local items = choices:split("+")
	if table.contains(items,"discard")
	and self:doDisCard(target,"he")
	then return "discard" end
end

sgs.ai_skill_use["@@mobilesidi"] = function(self,prompt)
	local tos = self.room:getAllPlayers()
	tos = self:sort(tos,nil,true)
	for _,p in sgs.list(tos)do
		if p~=self.player and p:getMark("mobilesidiFrom"..self.player:objectName())<1 and self:getOverflow()>0 then
			for _,q in sgs.list(tos)do
				if p~=q and self:isEnemy(p,q) then
					return string.format("@MobileSidiCard=.->%s+%s",p:objectName(),q:objectName())
				end
			end
		end
	end
end

sgs.ai_skill_choice.mobilesidi = function(self,choices,data)
	local use = data:toCardUse()
	local items = choices:split("+")
	if table.contains(items,"mobilesidi1")
	and use.card:isDamageCard()
	and self:isFriend(use.to:first())
	then return "mobilesidi1" end
	return "mobilesidi2"
end

sgs.ai_skill_invoke.mobileqingxi = function(self,data)
    local to = data:toPlayer()
	return not self:isFriend(to)
end

sgs.ai_skill_discard.mobileqingxi = function(self,max,min)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards,nil,"j")
	local to_cards = {}
   	for _,h in sgs.list(cards)do
		if self:getKeepValue(h)<7 then
         	table.insert(to_cards,h:getEffectiveId())
			if #to_cards>=min then return to_cards end
		end
	end
	return {}
end

sgs.ai_skill_invoke.mobilezhengzi = function(self,data)
	return self.player:isWounded() or self.player:isChained() or not self.player:faceUp()
end

sgs.ai_skill_invoke.mobileshensu = function(self,data)
	local dc = dummyCard()
	dc:setSkillName("shensu")
	return self:aiUseCard(dc).card~=nil
end

sgs.ai_skill_choice.mobileshensu = function(self,choices,data)
	local items = choices:split("+")
	local dc = dummyCard()
	dc:setSkillName("shensu")
	local bts = {}
   	for _,p in sgs.list(self.room:getAllPlayers())do
		if p:getMark("mobileshensuBan-Clear")>0 then
			table.insert(bts,p)
		end
	end
	local d = self:aiUseCard(dc,dummy(nil,0,bts))
	if d.card then
		self.mobileshensu_to = d.to
		return items[1]
	end
	return items[#items]
end

sgs.ai_skill_use["@@mobileshensu"] = function(self,prompt)
	local tos = {}
	for _,p in sgs.list(self.mobileshensu_to)do
		table.insert(tos,p:objectName())
	end
	return string.format("@MobileShensuCard=.->%s",table.concat(tos,"+"))
end







