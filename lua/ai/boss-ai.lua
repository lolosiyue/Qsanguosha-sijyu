sgs.ai_skill_playerchosen.bossdidong = function(self,targets)
	self:sort(self.enemies)
	for _,enemy in sgs.list(self.enemies)do
		if enemy:faceUp() then return enemy end
	end
end

sgs.ai_skill_playerchosen.bossluolei = function(self,targets)
	self:sort(self.enemies)
	for _,enemy in sgs.list(self.enemies)do
		if self:canAttack(enemy,self.player,sgs.DamageStruct_Thunder) then
			return enemy
		end
	end
end

sgs.ai_skill_playerchosen.bossguihuo = function(self,targets)
	self:sort(self.enemies)
	for _,enemy in sgs.list(self.enemies)do
		if self:canAttack(enemy,self.player,sgs.DamageStruct_Fire)
        and (enemy:hasArmorEffect("Vine") or enemy:getMark("&kuangfeng")>0)
		then return enemy end
	end
	for _,enemy in sgs.list(self.enemies)do
		if self:canAttack(enemy,self.player,sgs.DamageStruct_Fire)
		then return enemy end
	end
end

sgs.ai_skill_playerchosen.bossxiaoshou = function(self,targets)
	self:sort(self.enemies)
	for _,enemy in sgs.list(self.enemies)do
		if enemy:getHp()>self.player:getHp() and self:canAttack(enemy,self.player) then
			return enemy
		end
	end
end

sgs.ai_armor_value.bossmanjia = function(player,self,card)
	if not card then return sgs.ai_armor_value.vine(player,self) end
end

sgs.ai_skill_invoke.bosslianyu = function(self,data)
	local value,avail = 0,0
	for _,enemy in sgs.list(self.enemies)do
		if not self:damageIsEffective(enemy,sgs.DamageStruct_Fire,self.player) then continue end
		avail = avail+1
		if self:canAttack(enemy,self.player,sgs.DamageStruct_Fire) then
			value = value+1
			if enemy:hasArmorEffect("Vine") or enemy:getMark("&kuangfeng")>0 then
				value = value+1
			end
		end
	end
	return avail>0 and value/avail>=2/3
end

sgs.ai_skill_invoke.bosssuoming = function(self,data)
	local value = 0
	for _,enemy in sgs.list(self.enemies)do
		if self:isGoodTarget(enemy,self.enemies) then
			value = value+1
		end
	end
	return value/#self.enemies>=2/3
end

sgs.ai_skill_playerchosen.bossxixing = function(self,targets)
	self:sort(self.enemies)
	for _,enemy in sgs.list(self.enemies)do
		if enemy:isChained() and self:canAttack(enemy,self.player,sgs.DamageStruct_Thunder) then
			return enemy
		end
	end
end

sgs.ai_skill_invoke.bossqiangzheng = function(self,data)
	local value = 0
	for _,enemy in sgs.list(self.enemies)do
		if enemy:getHandcardNum()==1 and (enemy:hasSkill("kongcheng") or (enemy:hasSkill("zhiji") and enemy:getMark("zhiji")==0)) then
			value = value+1
		end
	end
	return value/#self.enemies<2/3
end

sgs.ai_skill_invoke.bossqushou = function(self,data)
	local sa = dummyCard("savage_assault")
	local dummy_use = dummy()
	self:useTrickCard(sa,dummy_use)
	return (dummy_use.card~=nil)
end

sgs.ai_skill_invoke.bossmojian = function(self,data)
	local aa = dummyCard("archery_attack")
	local dummy_use = dummy()
	self:useTrickCard(aa,dummy_use)
	return (dummy_use.card~=nil)
end

sgs.ai_skill_invoke.bossdanshu = function(self,data)
	if not self.player:isWounded() then return false end
	local zj = self.room:findPlayerBySkillName("guidao")
	if self.player:getHp()/self.player:getMaxHp()>=0.5 and zj and self:isEnemy(zj) and self:canRetrial(zj) then return false end
	return true
end

local kuangxi_skill = {}
kuangxi_skill.name= "kuangxi"
table.insert(sgs.ai_skills,kuangxi_skill)
kuangxi_skill.getTurnUseCard=function(self)
	if not self.player:hasFlag("KuangxiEnterDying") then
		return sgs.Card_Parse("@KuangxiCard=.")
	end
end

sgs.ai_skill_use_func.KuangxiCard = function(card, use, self)
	self:sort(self.enemies, "hp")
	local can_invoke = false
	for _, friend in ipairs(self.friends) do
		if friend:hasSkill("baoying") and friend:getMark("@baoying") > 0 then
			can_invoke = true
		end
	end
	for _, enemy in ipairs(self.enemies) do
		if self:objectiveLevel(enemy) > 3 and not self:cantbeHurt(enemy) and self:damageIsEffective(enemy) then
			if self.player:getHp() >= enemy:getHp() and (self.player:getHp() > 1 or can_invoke) then
				use.card = sgs.Card_Parse("@KuangxiCard=.")
				use.to:append(enemy)
				return
			end
		end
	end
end

sgs.ai_skill_invoke.baoying = function(self, data)
	local dying = data:toDying()
	local peaches = 1 - dying.who:getHp()
	return (peaches > 1  or self:getCardsNum("Peach") + self:getCardsNum("Analeptic") < peaches) and self:isFriend(dying.who)
end

sgs.ai_use_value.KuangxiCard = 2.5
sgs.ai_card_intention.KuangxiCard = 80
sgs.dynamic_value.damage_card.KuangxiCard = true

sgs.ai_skill_askforyiji.huying = function(self,card_ids)
	return self.room:getLord(),card_ids[1]
end

function SmartAI:useCardGodNihilo(card, use)
	if CanToCard(card,self.player,self.player) then
		local xiahou = self.room:findPlayerBySkillName("yanyu")
		if xiahou and self:isEnemy(xiahou) and xiahou:getMark("YanyuDiscard2")>0 then return end
		if self.player:getKingdom()=="god" then
		elseif self.player:hasCard(card) then
			if self.player:getHandcardNum()>math.min(5,self.player:getMaxHp())
			then return end
		elseif self.player:getHandcardNum()>=math.min(5,self.player:getMaxHp())
		then return end
		use.card = card
		use.to:append(self.player)
		for _,p in sgs.list(self.friends_noself)do
			if CanToCard(card,self.player,p,use.to)
			and self:canDraw(p) and p:getHandcardNum()<math.min(5,p:getMaxHp())
			then use.to:append(p) end
		end
	end
end

sgs.ai_card_intention.GodNihilo = -80

sgs.ai_keep_value.GodNihilo = 5
sgs.ai_use_value.GodNihilo = 10
sgs.ai_use_priority.GodNihilo = 1

sgs.dynamic_value.benefit.GodNihilo = true

function SmartAI:useCardGodFlower(card,use)
	local extraTarget = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,card)
	if use.extra_target then extraTarget = extraTarget+use.extra_target end
	local function addTarget(to,ft)
		if isCurrent(use,to) then return end
		if not use.to:contains(to) and self:doDisCard(to,ft,true,2) then
			use.card = card
			use.to:append(to)
			if use.to:length()>extraTarget then return true end
		end
	end
	local players = self:exclude(self.room:getOtherPlayers(self.player),card)
	local dummy = self:getCard("Slash")
	if dummy and dummy:isAvailable(self.player) then
		dummy = self:aiUseCard(dummy)
		if dummy.card then
			for _,to in sgs.qlist(dummy.to)do
				if table.contains(players,to) and to:getHp()<=2
				and to:getHandcardNum()<=to:getHp()
				and addTarget(to,"h") then return end
			end
		end
	end
	self:sort(players,"defense")
	for _,enemy in ipairs(players)do
		if enemy:hasEquip() and addTarget(enemy,"he")
		then return end
	end
	for _,enemy in ipairs(players)do
		local n = 0
		for _,h in sgs.qlist(enemy:getHandcards())do
			if (h:hasFlag("visible") or h:hasFlag("visible_"..self.player:objectName().."_"..enemy:objectName()))
			and isCard("Peach,Analeptic",h,enemy) then
				n = n+1
				if n>=enemy:getHandcardNum()/2 and addTarget(enemy,"h")
				then return end
			end
		end
	end
	for _,enemy in ipairs(players)do
		if enemy:getHandcardNum()>0 and enemy:hasSkills(sgs.cardneed_skill)
		and addTarget(enemy,"h")
		then return end
	end
	for _,enemy in ipairs(players)do
		if enemy:getHandcardNum()>0 and self:isEnemy(enemy)
		and addTarget(enemy,"h") then return end
	end
	for _,enemy in ipairs(players)do
		if enemy:getHandcardNum()>0 and not self:isFriend(enemy)
		and addTarget(enemy,"h") then return end
	end
	for _,enemy in ipairs(players)do
		if enemy:getHandcardNum()>0
		and addTarget(enemy,"h") then return end
	end
end

sgs.ai_use_value.GodFlower = 9
sgs.ai_use_priority.GodFlower = 4.3
sgs.ai_keep_value.GodFlower = 3.46
sgs.dynamic_value.control_card.GodFlower = true

sgs.ai_use_value.GodBlade = 5
sgs.ai_use_priority.GodBlade = 3
sgs.ai_use_value.GodDiagram = 8
sgs.ai_use_priority.GodDiagram = 4
sgs.ai_use_value.GodQin = 2
sgs.ai_use_value.GodPao = 8
sgs.ai_use_priority.GodPao = 5
sgs.ai_use_value.GodHalberd = 8
sgs.ai_use_priority.GodHalberd = 5
sgs.ai_use_value.GodHat = 6
sgs.ai_use_value.GodSword = 9

sgs.ai_skill_invoke.god_double_sword = function(self,data)
	local target = data:toPlayer()
	if target then
		return not self:isFriend(target)
	end
end
sgs.ai_use_value.GodDoubleSword = 4
sgs.ai_use_priority.GodDoubleSword = 3

sgs.ai_skill_playerchosen.god_bow = function(self,players)
    for _,target in sgs.list(players)do
		if self:doDisCard(target,"he",false,2)
		then return target end
	end
end
sgs.ai_use_value.GodBow = 4.4
sgs.ai_use_priority.GodBow = 6.6

sgs.ai_skill_cardask["god_axe0"] = function(self,data)
    local use = data:toCardUse()
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- ���б�ת��Ϊ��
    self:sortByKeepValue(cards) -- ������ֵ����
	local to = use.to:first()
	if #cards>2 and to:getMark("god_axeArmorNullified-Clear")<1 and use.card:isDamageCard()
	and (to:getCardCount()>0 and self:isWeak(to) or to:getHandcardNum()>0 or to:hasArmorEffect(nil))
	and self:isEnemy(to) then
		local dc = dummyCard()
		for _,c in sgs.list(cards)do
			if c:objectName()~="god_axe"
			then dc:addSubcard(c) end
			if dc:subcardsLength()>1
			then return dc:toString() end
		end
	end
    return "."
end
sgs.ai_use_value.GodAxe = 5.4
sgs.ai_use_priority.GodAxe = 6.6
sgs.ai_use_revises.god_axe = function(self,card,use)
	if card:isDamageCard() and use.to:length()==1 and self.player:getCardCount()>3
	then card:setFlags("Qinggang") end
end

sgs.ai_skill_invoke.god_edict = function(self,data)
	local target = data:toPlayer()
	if self:isFriend(target) then
		return #self:poisonCards("he",target)>0 or self:isWeak(target)
	end
	return #self:poisonCards("he",target)<1
end
sgs.ai_use_value.GodEdict = 3
sgs.ai_use_priority.GodEdict = 2

sgs.ai_skill_cardask["god_edict0"] = function(self,data)
	local target = data:toPlayer()
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- ���б�ת��Ϊ��
    self:sortByKeepValue(cards) -- ������ֵ����
	if self:isFriend(target) then
		local pc = self:poisonCards("e")
		if #pc>0 then return pc[1]:getId() end
		if #cards>target:getCardCount() then
			for i,c in sgs.list(cards)do
				if i>#cards/2 and c:objectName()~="god_edict"
				then return c:getId() end
			end
		end
	else
		cards = self:poisonCards("h")
		if #cards>0 then return cards[1]:getId() end
	end
    return "."
end

sgs.ai_skill_invoke.god_headdress = function(self,data)
	return self:getOverflow()~=9
end
sgs.ai_use_value.GodEdict = 3
sgs.ai_use_priority.GodEdict = 2

sgs.ai_skill_cardask["god_headdress0"] = function(self,data)
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- ���б�ת��Ϊ��
    self:sortByKeepValue(cards) -- ������ֵ����
	local n = self.player:getMaxCards()
	if self.player:hasSkill("shenfu")
  	then
		if self:getOverflow()<0
		then
			if #cards%2==0
			then
				if #self.enemies>0
				then return "." end
			end
		elseif n%2==1
		then
			return "."
		else
			return cards[n]:getId()
		end
	end
	if self:getOverflow()>0
	then
		n = n+1
		if self:getKeepValue(cards[n])>6
		then return cards[n]:getId() end
	end
    return "."
end

sgs.ai_skill_invoke.GodDamage = function(self,data)
	local items = data:toString():split(":")
    local target = self.room:findPlayerByObjectName(items[2])
	if target and self:isEnemy(target)
	then
		return target:getLostHp()<1 or not self:isWeak(target)
	end
end

function SmartAI:useCardGodSpeel(card,use)
	self:sort(self.enemies,"skill",false)
	for _,ep in sgs.list(self.enemies)do
		if isCurrent(use,ep) then continue end
		if #ep:getTag("god_speelSkills"):toStringList()<1
		and ep:getVisibleSkillList():length()>0
	   	and CanToCard(card,self.player,ep,use.to) then
	    	use.card = card
	    	use.to:append(ep)
		end
	end
end
sgs.ai_use_priority.GodSpeel = 5.4
sgs.ai_keep_value.GodSpeel = 4
sgs.ai_use_value.GodSpeel = 3.7

sgs.ai_nullification.GodSpeel = function(self,trick,from,to,positive)
    return self:isFriend(to) and (to:getHandcardNum()>3 or to==self.player)
	and positive
end

sgs.ai_card_intention.GodSpeel = 80

sgs.ai_ajustdamage_from.god_qin = function(self,from,to,card,nature)
	nature = "F"
end

sgs.ai_ajustdamage_from.god_deer = function(self,from,to,card,nature)
	if nature ~= "N" then
		return 1
	end
end

sgs.ai_target_revises.yl_remen = function(to,card)
    if to:hasEquipArea() and to:getArmor()==nil then
		if card:isKindOf("SavageAssault")
		or card:isKindOf("ArcheryAttack")
		or card:objectName()=="slash"
		then return true end
	end
end


sgs.ai_skill_playerchosen.god_ship = function(self,players)
    for _,target in sgs.list(players)do
		if self:doDisCard(target,"ej",false)
		then return target end
	end
end


sgs.ai_skill_invoke.yl_shiyou = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_invoke.yl_dayuan = function(self,data)
   	local judge = data:toJudge()
	local dc = dummyCard(judge.card:objectName())
	for s=3,0,-1 do
		sgs.ai_skill_suit.yl_dayuan = s
		dc:setSuit(s)
		for n=13,1,-1 do
			dc:setNumber(n)
			sgs.ai_skill_choice.yl_dayuan = n..""
			if self:isFriend(judge.who) and judge:isGood(dc)
			or self:isEnemy(judge.who) and not judge:isGood(dc) then
				return self:needRetrial(judge)
			end
		end
	end
end

sgs.ai_fill_skill.yl_diting = function(self)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- ���б�ת��Ϊ��
    self:sortByKeepValue(cards) -- ������ֵ����
	for _,c in sgs.list(cards)do
		if c:isKindOf("Horse") and not self.player:isCardLimited(c,sgs.Card_MethodRecast) then
			return sgs.Card_Parse("#yl_ditingCard:"..c:getId()..":")
		end
	end
end

sgs.ai_skill_use_func["#yl_ditingCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.yl_ditingCard = 3.4
sgs.ai_use_priority.yl_ditingCard = 9.2

function SmartAI:useCardHuihun(card,use)
	for _,p in sgs.list(self.friends)do
		if p:getTag("yl_wanghunSkill"):toString()~="" then
	    	use.card = card
		end
	end
end
sgs.ai_use_priority.Huihun = 7.4
sgs.ai_keep_value.Huihun = 2
sgs.ai_use_value.Huihun = 3.7

sgs.ai_nullification.Huihun = function(self,trick,from,to,positive)
    return self:isEnemy(to) and positive
end

sgs.ai_card_intention.Huihun = 80







