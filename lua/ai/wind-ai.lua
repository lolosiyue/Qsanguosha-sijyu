sgs.ai_skill_use["@@shensu1"]=function(self,prompt)
	self:sort(self.enemies,"defense")
	if self.player:containsTrick("lightning") and self.player:getCards("j"):length()==1
	and self:hasWizard(self.friends) and not self:hasWizard(self.enemies,true)
	then return "." end

	if self:needBear() then return "." end

	local slash = dummyCard()
	slash:setSkillName("shensu")
	local dummy = self:aiUseCard(slash)
	if dummy.card then
		local tos = {}
		for _,p in sgs.list(dummy.to)do
			table.insert(tos,p:objectName())
		end
		return "@ShensuCard=.->"..table.concat(tos,"+")
	end
	return "."
end

sgs.ai_get_cardType = function(card)
	return card:getRealCard():toEquipCard():location()+1
end

sgs.ai_skill_use["@@shensu2"] = function(self,prompt,method)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,true)
	local eCard
	local hasCard = {}
	if self:needToThrowArmor() then
		eCard = self.player:getArmor()
	end
	for i,card in ipairs(cards)do
		if card:isKindOf("EquipCard") then
			i = sgs.ai_get_cardType(card)
			hasCard[i] = (hasCard[i] or 0)+1
		end
	end
	for _,card in ipairs(cards)do
		if eCard then break end
		if card:isKindOf("EquipCard") and hasCard[sgs.ai_get_cardType(card)]>1
		then eCard = card end
	end
	for _,card in ipairs(cards)do
		if eCard then break end
		if card:isKindOf("EquipCard") and sgs.ai_get_cardType(card)>3
		then eCard = card end
	end
	for _,card in ipairs(cards)do
		if eCard then break end
		if card:isKindOf("EquipCard") and not card:isKindOf("Armor")
		then eCard = card end
	end
	if not eCard then return "." end
	local slash = dummyCard()
	slash:setSkillName("shensu")
	local dummy = self:aiUseCard(slash)
	if dummy.card then
		local tos = {}
		for _,p in sgs.list(dummy.to)do
			table.insert(tos,p:objectName())
		end
		return "@ShensuCard="..eCard:getEffectiveId().."->"..table.concat(tos,"+")
	end
	return "."
end

sgs.ai_cardneed.shensu = function(to,card,self)
	return card:getTypeId()==sgs.Card_TypeEquip and getKnownCard(to,self.player,"EquipCard",false)<2
end

sgs.ai_card_intention.ShensuCard = sgs.ai_card_intention.Slash

sgs.shensu_keep_value = sgs.xiaoji_keep_value

function sgs.ai_skill_invoke.jushou(self,data)
	if not self.player:faceUp() then return true end
	for _,friend in ipairs(self.friends)do
		if friend:hasSkills("fangzhu|jilve") and not self:isWeak(friend) then return true end
		if friend:hasSkill("junxing") and friend:faceUp() and not self:willSkipPlayPhase(friend)
			and not (friend:isKongcheng() and self:willSkipDrawPhase(friend)) then
			return true
		end
	end
	if not self.player:hasSkill("jiewei") then return false end
	for _,card in sgs.qlist(self.player:getHandcards())do
		if card:getTypeId()==sgs.Card_TypeTrick and not card:isKindOf("Nullification") then
			local dummy_use = dummy()
			self:useTrickCard(card,dummy_use)
			if dummy_use.card then return true end
		elseif card:getTypeId()==sgs.Card_TypeEquip then
			local dummy_use = dummy()
			self:useEquipCard(card,dummy_use)
			if dummy_use.card then return true end
		end
	end
	local Rate = math.random()+self.player:getCardCount()/10+self.player:getHp()/10
	if Rate>1.1 then return true end
	return false
end

sgs.ai_skill_invoke.jiewei = true

sgs.ai_skill_use["TrickCard+^Nullification,EquipCard|.|.|hand"] = function(self,prompt,method)
	local cards = sgs.QList2Table(self.player:getHandcards())
	self:sortByUseValue(cards)
	for _,card in ipairs(cards)do
		if card:getTypeId()==sgs.Card_TypeTrick and not card:isKindOf("Nullification") then
			local dummy_use = dummy()
			self:useTrickCard(card,dummy_use)
			if dummy_use.card then
				local tos = {}
				for _,p in sgs.qlist(dummy_use.to)do
					table.insert(tos,p:objectName())
				end
				return card:toString().."->"..table.concat(tos,"+")
			end
		elseif card:getTypeId()==sgs.Card_TypeEquip then
			local dummy_use = dummy()
			self:useEquipCard(card,dummy_use)
			if dummy_use.card then
				self.jiewei_type = sgs.Card_TypeEquip
				return card:toString()
			end
		end
	end
	return "."
end

sgs.ai_skill_playerchosen.jiewei = function(self,targets)
	if self.jiewei_type==sgs.Card_TypeTrick
	then return self:findPlayerToDiscard("j",true,true,targets)[1]
	elseif self.jiewei_type==sgs.Card_TypeEquip
	then return self:findPlayerToDiscard("e",true,true,targets)[1] end
end

sgs.ai_skill_invoke.liegong = function(self,data)
	local target = data:toPlayer()
	return not self:isFriend(target)
end

function SmartAI:canLiegong(to,from)
	from = from or self.room:getCurrent()
	to = to or self.player
	if not from then return false end
	if from:hasSkill("liegong") and from:getPhase()==sgs.Player_Play and (to:getHandcardNum()>=from:getHp() or to:getHandcardNum()<=from:getAttackRange()) then return true end
	if from:hasSkill("kofliegong") and from:getPhase()==sgs.Player_Play and to:getHandcardNum()>=from:getHp() then return true end
	if from:hasSkill("tenyearliegong") and to:getHandcardNum()<=from:getHandcardNum() then return true end
	return false
end




sgs.ai_skill_cardask["@guidao-card"]=function(self,data)
	local all_cards = self:addHandPile("he")
	if #all_cards<1 then return "." end
	local judge = data:toJudge()
	local needTokeep = judge.card:getSuit()~=sgs.Card_Spade and (not self.player:hasSkill("leiji") or judge.card:getSuit()~=sgs.Card_Club)
		and self:findLeijiTarget(self.player,50) and (self:getCardsNum("Jink")>0 or self:hasEightDiagramEffect()) and self:getFinalRetrial()==1
	if not needTokeep and judge.who:getPhase()<=sgs.Player_Judge
	and judge.who:containsTrick("lightning") and judge.reason~="lightning"
	then needTokeep = true end
	local keptspade = 0
	if needTokeep and self.player:hasSkills("nosleiji|tenyearleiji")
	then keptspade = 2 end
	local cards = {}
	for _,card in sgs.list(all_cards)do
		if card:isBlack() and not card:hasFlag("using") then
			if card:getSuit()==sgs.Card_Spade then keptspade = keptspade-1 end
			table.insert(cards,card)
		end
	end
	if #cards<1 or keptspade==1 then return "." end
	local card_id = self:getRetrialCardId(cards,judge,nil,true)
	if card_id<0 then return "." end
	if self:needRetrial(judge)
	or self:getUseValue(judge.card)>self:getUseValue(sgs.Sanguosha:getCard(card_id))
	then return card_id end
	return "."
end

function sgs.ai_cardneed.guidao(to,card,self)
	for _,p in sgs.qlist(self.room:getAllPlayers())do
		if self:getFinalRetrial(to)==1 then
			if p:containsTrick("lightning") and not p:containsTrick("YanxiaoCard") then
				return card:getSuit()==sgs.Card_Spade and card:getNumber()>=2 and card:getNumber()<=9 and not self:hasSkills("hongyan|olhongyan|wuyan")
			end
			if self:isFriend(p) and self:willSkipDrawPhase(p) then
				return card:getSuit()==sgs.Card_Club and self:hasSuit("club",true,to)
			end
		end
	end
	if self:getFinalRetrial(to)==1 then
		if to:hasSkills("nosleiji|tenyearleiji") then
			return card:getSuit()==sgs.Card_Spade
		end
		if to:hasSkills("leiji|olleiji|tenyearleiji") then
			return card:isBlack()
		end
	end
end

function SmartAI:findLeijiTarget(player,leiji_value,slasher,latest_version)
	if not latest_version then
		return self:findLeijiTarget(player,leiji_value,slasher,1)
		or self:findLeijiTarget(player,leiji_value,slasher,-1)
	end
	if not player:hasSkills(latest_version==1 and "leiji|olleiji|tenyearleiji" or "nosleiji") then return end
	if slasher then
		if not self:slashIsEffective(dummyCard(),player,slasher,slasher:hasWeapon("qinggang_sword")) then return end
		if slasher:hasSkill("liegong") and slasher:getPhase()==sgs.Player_Play and self:isEnemy(player,slasher)
		and (player:getHandcardNum()>=slasher:getHp() or player:getHandcardNum()<=slasher:getAttackRange())
		then return end
		if slasher:hasSkill("kofliegong") and slasher:getPhase()==sgs.Player_Play
		and self:isEnemy(player,slasher) and player:getHandcardNum()>=slasher:getHp()
		then return end
		if latest_version then
			if not self:hasSuit("black",true,player) and player:getHandcardNum()<2 then return end
		else
			if not self:hasSuit("spade",true,player) and player:getHandcardNum()<3 then return end
		end
		if not(getKnownCard(player,self.player,"Jink",true)>0
		or player:getHandcardNum()>=4 and getCardsNum("Jink",player,self.player)>=1
		or not self:isWeak(player) and self:hasEightDiagramEffect(player) and not slasher:hasWeapon("qinggang_sword") and getCardsNum("Jink",player,self.player)>=1)
		then return end
	end
	local function getCmpValue(enemy)
		if not self:damageIsEffective(enemy,sgs.DamageStruct_Thunder,player) then return 99 end
		local value = 0
		if enemy:hasSkills("hongyan|olhongyan") then
			if latest_version==-1 then return 99
			elseif not self:hasSuit("club",true,player) and player:getHandcardNum()<3
			then value = value+80
			else value = value+70 end
		end
		if self:cantbeHurt(enemy,player,latest_version==1 and 1 or 2)
		or self:objectiveLevel(enemy)<3
		or (enemy:isChained() and not self:isGoodChainTarget(enemy,sgs.DamageStruct_Thunder,player,latest_version==1 and 1 or 2))
		then return 100 end
		if not latest_version and enemy:hasArmorEffect("SilverLion") then value = value+20 end
		if enemy:hasSkills(sgs.exclusive_skill) then value = value+10 end
		if enemy:hasSkills(sgs.masochism_skill) then value = value+5 end
		if enemy:isChained() and self:isGoodChainTarget(enemy,sgs.DamageStruct_Thunder,player,latest_version==1 and 1 or 2)
		and #self:getChainedEnemies(player)>1 then value = value-25 end
		if enemy:isLord() then value = value-5 end
		value = value+enemy:getHp()+self:getDefenseSlash(enemy)*0.01
		if latest_version and player:isWounded() and not self:needToLoseHp(player) then value = value+15 end
		return value
	end
	local bcv = {}
	local enemies = self:getEnemies(player)
	for _,enemy in ipairs(enemies)do
		bcv[enemy:objectName()] = getCmpValue(enemy)
	end
	local function cmp(a,b)
		return bcv[a:objectName()]<bcv[b:objectName()]
	end
	table.sort(enemies,cmp)
	for _,enemy in ipairs(enemies)do
		if getCmpValue(enemy)<leiji_value then return enemy end
	end
end

sgs.ai_skill_playerchosen.leiji = function(self,targets)
	local mode = self.room:getMode()
	if mode:find("_mini_17") or mode:find("_mini_19") or mode:find("_mini_20") or mode:find("_mini_26") then
		for _,p in sgs.qlist(self.room:getAllPlayers())do
			if p:getState()~="robot" then
				return p
			end
		end
	end
	return self:findLeijiTarget(self.player,100,nil,1)
end

function SmartAI:needLeiji(to,from)
	return self:findLeijiTarget(to,50,from,-1)
end

sgs.ai_playerchosen_intention.leiji = 80

function sgs.ai_slash_prohibit.leiji(self,from,to,card) -- @todo: Qianxi flag name
	if self:isFriend(to) then return false end
	if to:hasFlag("QianxiTarget") and (not self:hasEightDiagramEffect(to) or self.player:hasWeapon("qinggang_sword")) then return false end
	if not from then from = self.room:getCurrent() end
	local hcard = to:getHandcardNum()
	if self:canLiegong(to,from) then return false end
	if from:getRole()=="rebel" and to:isLord() then
		local other_rebel
		for _,player in sgs.qlist(self.room:getOtherPlayers(from))do
			if sgs.ai_role[player:objectName()]=="rebel"
			or self:compareRoleEvaluation(player,"rebel","loyalist")=="rebel" then
				other_rebel = player
				break
			end
		end
		if not other_rebel and (self:hasSkills("hongyan") or self.player:getHp()>=4)
		and (self:getCardsNum("Peach")>0  or self.player:hasSkills("hongyan|ganglie|neoganglie")) then
			return false
		end
	end
	if getKnownCard(to,self.player,"Jink",true)>=1
	or (self:hasSuit("spade",true,to) and hcard>=2)
	or hcard>=4 then return true end
	if self:hasEightDiagramEffect(to) and not IgnoreArmor(from,to) then return true end
end

sgs.ai_skill_playerchosen.tenyearleiji = function(self,targets)
	return sgs.ai_skill_playerchosen.leiji(self,targets)
end

sgs.ai_playerchosen_intention.tenyearleiji = sgs.ai_playerchosen_intention.leiji

function sgs.ai_slash_prohibit.tenyearleiji(self,from,to,card)
	return sgs.ai_slash_prohibit.leiji(self,from,to,card)
end

local huangtianv_skill = {}
huangtianv_skill.name = "huangtian_attach"
table.insert(sgs.ai_skills,huangtianv_skill)
huangtianv_skill.getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
	self:sortByUseValue(cards,true)
	for _,acard in ipairs(cards)  do
		if acard:isKindOf("Jink") then
			return sgs.Card_Parse("@HuangtianCard="..acard:getEffectiveId())
		end
	end
end

sgs.ai_skill_use_func.HuangtianCard = function(card,use,self)
	if self:needBear() or self:getCardsNum("Jink","h")<=1 then
		return "."
	end
	local targets = {}
	for _,friend in ipairs(self.friends_noself)do
		if friend:hasLordSkill("huangtian") then
			if not friend:hasFlag("HuangtianInvoked") then
				if not hasManjuanEffect(friend) then
					table.insert(targets,friend)
				end
			end
		end
	end
	if #targets>0 then --黄天己方
		use.card = card
		self:sort(targets,"defense")
		use.to:append(targets[1])
	elseif self:getCardsNum("Slash","he")>=2 then --黄天对方
		for _,enemy in ipairs(self.enemies)do
			if enemy:hasLordSkill("huangtian") then
				if not enemy:hasFlag("HuangtianInvoked") then
					if not hasManjuanEffect(enemy,true) then
						if enemy:isKongcheng() and not enemy:hasSkill("kongcheng") and not hasTuntianEffect(enemy,true) then --必须保证对方空城，以保证天义/陷阵的拼点成功
							table.insert(targets,enemy)
						end
					end
				end
			end
		end
		if #targets>0 then
			local flag = false
			if self.player:hasSkill("tianyi") and not self.player:hasUsed("TianyiCard") then
				flag = true
			elseif self.player:hasSkill("xianzhen") and not self.player:hasUsed("XianzhenCard") then
				flag = true
			elseif self.player:hasSkill("tenyearxianzhen") and not self.player:hasUsed("TenyearXianzhenCard") then
				flag = true
			elseif self.player:hasSkill("mobilexianzhen") and not self.player:hasUsed("MobileXianzhenCard") then
				flag = true
			end
			if flag then
				local maxCard = self:getMaxCard(self.player) --最大点数的手牌
				if maxCard:getNumber()>card:getNumber() then --可以保证拼点成功
					self:sort(targets,"defense",true)
					for _,enemy in ipairs(targets)do
						if self.player:canSlash(enemy,nil,false,0) then --可以发动天义或陷阵
							use.card = card
							enemy:setFlags("AI_HuangtianPindian")
							use.to:append(enemy)
							break
						end
					end
				end
			end
		end
	end
end

sgs.ai_card_intention.HuangtianCard = function(self,card,from,tos)
	if tos[1]:isKongcheng() and ((from:hasSkill("tianyi") and not from:hasUsed("TianyiCard")) or (from:hasSkill("xianzhen") and not from:hasUsed("XianzhenCard")) or
		(from:hasSkill("tenyearxianzhen") and not from:hasUsed("TenyearXianzhenCard")) or (from:hasSkill("mobilexianzhen") and not from:hasUsed("MobileXianzhenCard"))) then
	else
		sgs.updateIntention(from,tos[1],-80)
	end
end

sgs.ai_use_priority.HuangtianCard = 10
sgs.ai_use_value.HuangtianCard = 8.5

sgs.guidao_suit_value = {
	spade = 3.9,
	club = 2.7
}

sgs.ai_skill_invoke.fenji = function(self,data)
	local move = data:toMoveOneTime()
	local from = self.room:findPlayerByObjectName(move.from:objectName())
	if self:isWeak() or not from or not self:isFriend(from)
		or hasManjuanEffect(from)
		or self:needKongcheng(from,true) then return false end
	local skill_name = move.reason.m_skillName
	if skill_name=="rende" or skill_name=="nosrende" then return true end
	return from:getHandcardNum()<(self.player:getHp()<=1 and 3 or 5)
end

sgs.ai_choicemade_filter.skillInvoke.fenji = function(self,player,promptlist)
	if sgs.ai_fenji_target then
		if promptlist[3]=="yes" then
			sgs.updateIntention(player,sgs.ai_fenji_target,-10)
		end
		sgs.ai_fenji_target = nil
	end
end

sgs.ai_skill_use["@@tianxiang"] = function(self,data,method)
	if not method then method = sgs.Card_MethodDiscard end
	local friend_lost_hp = 10
	local friend_hp = 0
	local card_id
	local target
	local cant_use_skill
	local dmg

	if data=="@tianxiang-card" then
		dmg = self.player:getTag("TianxiangDamage"):toDamage()
	else
		dmg = data
	end

	if not dmg then self.room:writeToConsole(debug.traceback()) return "." end

	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
	self:sortByUseValue(cards,true)
	for _,card in ipairs(cards)do
		if not self.player:isCardLimited(card,method) and card:getSuit()==sgs.Card_Heart and not card:isKindOf("Peach") then
			card_id = card:getId()
			break
		end
	end
	if not card_id then return "." end

	self:sort(self.enemies,"hp")

	for _,enemy in ipairs(self.enemies)do
		if (enemy:getHp()<=dmg.damage and enemy:isAlive() and enemy:getLostHp()+dmg.damage<3) then
			if (enemy:getHandcardNum()<=2 or enemy:hasSkills("guose|leiji|ganglie|enyuan|qingguo|wuyan|kongcheng") or enemy:containsTrick("indulgence"))
				and self:canAttack(enemy,dmg.from or self.room:getCurrent(),dmg.nature)
				and not (dmg.card and dmg.card:getTypeId()==sgs.Card_TypeTrick and enemy:hasSkill("wuyan")) then
				return "@TianxiangCard="..card_id.."->"..enemy:objectName()
			end
		end
	end

	for _,friend in ipairs(self.friends_noself)do
		if friend:getLostHp()+dmg.damage>1 and friend:isAlive()
		then
			if friend:isChained() and dmg.nature~=sgs.DamageStruct_Normal
			and not self:isGoodChainTarget(friend,dmg.card or dmg.nature,dmg.from,dmg.damage)
			then
			elseif friend:getHp()>=2 and dmg.damage<2
			and (friend:hasSkills("yiji|buqu|nosbuqu|shuangxiong|zaiqi|yinghun|jianxiong|fangzhu")
				or self:needToLoseHp(friend)
				or (friend:getHandcardNum()<3 and (friend:hasSkill("nosrende") or (friend:hasSkill("rende") and not friend:hasUsed("RendeCard")))))
			then return "@TianxiangCard="..card_id.."->"..friend:objectName()
			elseif dmg.card and dmg.card:getTypeId()==sgs.Card_TypeTrick
			and friend:hasSkill("wuyan") and friend:getLostHp()>1
			then return "@TianxiangCard="..card_id.."->"..friend:objectName()
			elseif hasBuquEffect(friend)
			then return "@TianxiangCard="..card_id.."->"..friend:objectName() end
		end
	end

	for _,enemy in ipairs(self.enemies)do
		if (enemy:getLostHp()<=1 or dmg.damage>1) and enemy:isAlive() and enemy:getLostHp()+dmg.damage<4 then
			if (enemy:getHandcardNum()<=2)
				or enemy:containsTrick("indulgence") or enemy:hasSkills("guose|leiji|vsganglie|ganglie|enyuan|qingguo|wuyan|kongcheng")
				and self:canAttack(enemy,(dmg.from or self.room:getCurrent()),dmg.nature)
				and not (dmg.card and dmg.card:getTypeId()==sgs.Card_TypeTrick and enemy:hasSkill("wuyan")) then
				return "@TianxiangCard="..card_id.."->"..enemy:objectName() end
		end
	end

	for i = #self.enemies,1,-1 do
		local enemy = self.enemies[i]
		if not enemy:isWounded() and not self:hasSkills(sgs.masochism_skill,enemy) and enemy:isAlive()
			and self:canAttack(enemy,dmg.from or self.room:getCurrent(),dmg.nature)
			and (not (dmg.card and dmg.card:getTypeId()==sgs.Card_TypeTrick and enemy:hasSkill("wuyan") and enemy:getLostHp()>0) or self:isWeak()) then
			return "@TianxiangCard="..card_id.."->"..enemy:objectName()
		end
	end

	return "."
end

sgs.ai_card_intention.TianxiangCard = function(self,card,from,tos)
	local to = tos[1]
	if self:needToLoseHp(to) then return end
	local intention = 10
	if hasBuquEffect(to) then intention = 0
	elseif (to:getHp()>=2 and to:hasSkills("yiji|shuangxiong|zaiqi|yinghun|jianxiong|fangzhu"))
		or (to:getHandcardNum()<3 and (to:hasSkill("nosrende") or (to:hasSkill("rende") and not to:hasUsed("RendeCard")))) then
		intention = 0
	end
	sgs.updateIntention(from,to,intention)
end

function sgs.ai_slash_prohibit.tianxiang(self,from,to)
	if hasJueqingEffect(from,to) or (from:hasSkill("nosqianxi") and from:distanceTo(to)==1) then return false end
	if from:hasFlag("NosJiefanUsed") then return false end
	if self:isFriend(to,from) then return false end
	return self:cantbeHurt(to,from)
end

sgs.tianxiang_suit_value = {
	heart = 4.9
}

function sgs.ai_cardneed.tianxiang(to,card,self)
	return (card:getSuit()==sgs.Card_Heart or (to:hasSkill("hongyan") and card:getSuit()==sgs.Card_Spade))
	and (getKnownCard(to,self.player,"heart",false)+getKnownCard(to,self.player,"spade",false))<2
end


sgs.ai_skill_choice.guhuo = function(self,choices)
	local yuji = self.room:findPlayerBySkillName("guhuo")
	if not self:isEnemy(yuji) then return "noquestion" end
	local guhuoname = self.room:getTag("GuhuoType"):toString()
	if guhuoname=="peach+analeptic" then guhuoname = "peach" end
	if guhuoname=="normal_slash" then guhuoname = "slash" end
	local guhuocard = dummyCard(guhuoname)
	local guhuotype = guhuocard:getClassName()
	if guhuotype and self:getRestCardsNum(guhuotype,yuji)==0 and self.player:getHp()>0 then return "question" end
	if guhuotype and guhuotype=="AmazingGrace" then return "noquestion" end
	if self.player:hasSkill("hunzi") and self.player:getMark("hunzi")==0 and math.random(1,15)~=1 then return "noquestion" end
	if guhuotype:match("Slash") then
		if yuji:getState()~="robot" and math.random(1,8)==1 then return "question" end
		if not self:hasCrossbowEffect(yuji) then return "noquestion" end
	end
	local x = 5
	if guhuoname=="peach" or guhuoname=="ex_nihilo" then
		x = 2
		if getKnownCard(yuji,self.player,guhuotype,false)>0 then x = x*3 end
	end
	return math.random(1,x)==1 and "question" or "noquestion"
end

local guhuo_skill = {}
guhuo_skill.name = "guhuo"
table.insert(sgs.ai_skills,guhuo_skill)
guhuo_skill.getTurnUseCard = function(self)
    local cards = self.player:getCards("h")
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
	if #cards<1 then return end
	local Guhuo_str = {}
	for _,card in ipairs(cards)do
		if card:isNDTrick() or card:isKindOf("BasicCard") then
			table.insert(Guhuo_str,"@GuhuoCard="..card:getId()..":"..card:objectName())
		end
	end
	local peach = sgs.ai_guhuo_card.guhuo(self,"peach","Peach")
	if peach then table.insert(Guhuo_str,peach) end
	local question = #self.enemies
	for _,enemy in ipairs(self.enemies)do
		if enemy:hasSkill("chanyuan")
		or enemy:hasSkill("hunzi") and enemy:getMark("hunzi")<1
		then question = question-1 end
	end
	local ratio = question<1 and 100 or #self.enemies/question
	if #Guhuo_str>0 and ratio<100 then
		for i=1,5 do
			local guhuo_str = Guhuo_str[math.random(1,#Guhuo_str)]
			local user = dummyCard(guhuo_str:split(":")[2])
			if not user then continue end
			user:setSkillName("guhuo")
			guhuo_str = sgs.Card_Parse(guhuo_str)
			user:addSubcards(guhuo_str:getSubcards())
			if user:isAvailable(self.player) then
				local dummy = self:aiUseCard(user)
				if dummy.card then
					if user:canRecast()
					and dummy.to:length()<1
					then continue end
					self.guhuo_to = dummy.to
					if user:targetFixed() then
						if math.random(1,3)<=ratio
						then return guhuo_str end
					elseif math.random(1,4)<=ratio
					then return guhuo_str end
				end
			end
		end
	end
	if math.random(1,5)<=3*ratio then
		for _,pn in ipairs(patterns())do
			local c = dummyCard(pn)
			if c and (c:getTypeId()==1 or c:isNDTrick())
			and self:getRestCardsNum(c:getClassName())>0
			and c:isDamageCard() then
				c:setSkillName("guhuo") 
				c:addSubcard(cards[1])
				if c:isAvailable(self.player) then
					local dummy = self:aiUseCard(c)
					if dummy.card then
						if c:canRecast()
						and dummy.to:length()<1
						then continue end
						self.guhuo_to = dummy.to
						return sgs.Card_Parse("@GuhuoCard="..cards[1]:getId()..":"..c:objectName())
					end
				end
			end
		end
		for _,pn in ipairs(patterns())do
			local c = dummyCard(pn)
			if c and (c:getTypeId()==1 or c:isNDTrick())
			and self:getRestCardsNum(c:getClassName())>0 then
				c:setSkillName("guhuo") 
				c:addSubcard(cards[1])
				if c:isAvailable(self.player) then
					local dummy = self:aiUseCard(c)
					if dummy.card then
						if c:canRecast()
						and dummy.to:length()<1
						then continue end
						self.guhuo_to = dummy.to
						return sgs.Card_Parse("@GuhuoCard="..cards[1]:getId()..":"..c:objectName())
					end
				end
			end
		end
	end
	if self:isWeak() then
		ratio = sgs.ai_guhuo_card.guhuo(self,"peach","Peach")
		if ratio then
			local dummy = dummyCard("peach")
			if dummy:isAvailable(self.player) then
				local dummy = self:aiUseCard(dummy)
				if dummy.card and dummy.to then
					self.guhuo_to = dummy.to
					return sgs.Card_Parse(ratio)
				end
			end
		end
	end
	ratio = sgs.ai_guhuo_card.guhuo(self,"slash","Slash")
	if ratio and self:slashIsAvailable() then
		local dummy = self:aiUseCard(dummyCard())
		if dummy.card then
	       	self.guhuo_to = dummy.to
		   	return sgs.Card_Parse(ratio)
		end
	end
end

sgs.ai_skill_use_func.GuhuoCard = function(card,use,self)
	use.card = card
	use.to = self.guhuo_to
end

sgs.ai_use_priority.GuhuoCard = 10

sgs.guhuo_suit_value = {
	heart = 5,
}

sgs.ai_skill_choice.guhuo_saveself = function(self,choices)
	if self:getCard("Peach") or not self:getCard("Analeptic") then return "peach" else return "analeptic" end
end

sgs.ai_suit_priority.guidao= "diamond|heart|club|spade"
sgs.ai_suit_priority.hongyan= "club|diamond|spade|heart"
sgs.ai_skill_choice.guhuo_slash = function(self,choices)
	return "slash"
end

function sgs.ai_cardneed.kuanggu(to,card,self)
	return card:isKindOf("OffensiveHorse") and not (to:getOffensiveHorse() or getKnownCard(to,self.player,"OffensiveHorse",false)>0)
end

sgs.ai_skill_playerchosen.wuhun = function(self,targets)
	local targetlist=self:sort(targets,"hp")
	local target
	local lord
	for _,player in sgs.list(targetlist)do
		if player:isLord() then lord = player end
		if self:isEnemy(player) and (not target or target:getHp()<player:getHp()) then
			target = player
		end
	end
	if self.role=="rebel" and lord then return lord end
	if target then return target end
	
	if self.player:getRole()=="loyalist" and targetlist[1]:isLord() then return targetlist[2] end
	return targetlist[1]
end

function SmartAI:getWuhunRevengeTargets(to)
	local targets = {}
	local maxcount = 0
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		local count = p:getMark("&nightmare+#"..to:objectName())
		if count>maxcount then
			targets = {p}
			maxcount = count
		elseif count==maxcount and maxcount>0 then
			table.insert(targets,p)
		end
	end
	return targets
end

function sgs.ai_slash_prohibit.wuhun(self,from,to)
	if from:hasSkill("jueqing") then return false end
	if from:hasFlag("NosJiefanUsed") then return false end
	local damageNum = self:ajustDamage(from,to,1,dummyCard())

	local maxfriendmark = 0
	local maxenemymark = 0
	for _,friend in sgs.list(self:getFriends(from))do
		local friendmark = friend:getMark("&nightmare+#"..to:objectName())
		if friendmark>maxfriendmark then maxfriendmark = friendmark end
	end
	for _,enemy in sgs.list(self:getEnemies(from))do
		local enemymark = enemy:getMark("&nightmare+#"..to:objectName())
		if enemymark>maxenemymark and enemy~=to then maxenemymark = enemymark end
	end
	if self:isEnemy(to,from) and not (to:isLord() and from:getRole()=="rebel") then
		if (maxfriendmark+damageNum>=maxenemymark) and not (#(self:getEnemies(from))==1 and #(self:getFriends(from))+#(self:getEnemies(from))==self.room:alivePlayerCount()) then
			if not(from:getMark("&nightmare+#"..to:objectName())==maxfriendmark and from:getRole()=="loyalist")
			then return true end
		end
	end
end

function SmartAI:cantbeHurt(player,from,damageNum)
	from = from or self.player
	if hasJueqingEffect(from,player) then return false end
	damageNum = damageNum or 1
	if player:hasSkill("wuhun") and not player:isLord() and #self:getFriends(player,true)>0 then
		local maxfriendmark,maxenemymark = 0,0
		for friendmark,friend in sgs.list(self:getFriends(from))do
            friendmark = friend:getMark("&nightmare+#"..player:objectName())
			if friendmark>maxfriendmark then maxfriendmark = friendmark end
		end
		for enemymark,enemy in sgs.list(self:getEnemies(from))do
            enemymark = enemy:getMark("&nightmare+#"..player:objectName())
			if enemymark>maxenemymark and enemy~=player
			then maxenemymark = enemymark end
		end
		if self:isEnemy(player,from) then
			if maxfriendmark+damageNum-player:getHp()/2>=maxenemymark
			and not (#self:getEnemies(from)==1 and #self:getFriends(from)+#self:getEnemies(from)==self.room:alivePlayerCount())
            and not (from:getMark("&nightmare+#"..player:objectName())==maxfriendmark and from:getRole()=="loyalist")
			then return true end
		elseif maxfriendmark+damageNum-player:getHp()/2>maxenemymark
		then return true end
	end
	if player:hasSkill("duanchang") and not player:isLord()
	and #self:getFriends(player,true)>0 and player:getHp()<=1 then
		if not (from:getMaxHp()==3 and from:getArmor() and from:getDefensiveHorse()) then
			if from:getMaxHp()<=3 or from:isLord() and self:isWeak(from) then return true end
			if from:getMaxHp()<=3 or self.room:getLord() and from:getRole()=="renegade" then return true end
		end
	end
	if player:hasSkill("tianxiang")
	and getKnownCard(player,from,"diamond,club",false)<player:getHandcardNum() then
		for _,friend in sgs.list(self:getFriends(from))do
			if friend:getHp()+getCardsNum("Peach",from,self.player)<2
			and player:getHandcardNum()>0
			then return true end
		end
	end
	return false
end

function SmartAI:needDeath(player)
	player = player or self.player
	if player:hasSkill("wuhun") and #self:getFriends(player,true)>0 then
		local maxfriendmark,maxenemymark = 0,0
		for _,ap in sgs.list(self.room:getAlivePlayers())do
            local m = ap:getMark("&nightmare+#"..player:objectName())
			if self:isFriend(player,ap) and player~=ap
			and m>maxfriendmark then maxfriendmark = m end
			if self:isEnemy(player,ap) and m>maxenemymark
			then maxenemymark = m end
			if maxfriendmark>maxenemymark
			or maxenemymark<1 then return
			else return true end
		end
	end
	if player:getMark("&mobilezhixi")>0 and player:getCardCount()<2 then
		for _,ap in sgs.list(self.room:getAlivePlayers())do
			if ap:hasSkill("mobilezhishanxi")
			then return true end
		end
	end
	if player==self.player and player:getChangeSkillState("fengliao")==2 and player:hasSkill("fengliao") then
		return self:getCardsNum("Peach,Analeptic")+player:getHp()<2
	end
	return player:getMark("&ov_xijun+no_recover-Clear")>0
	or player:getMark("&keolranji_ban")>0
end

function SmartAI:doNotSave(player)
	if (player:hasSkill("niepan") and player:getMark("@nirvana")>0 and player:getCards("e"):length()<2)
		or (player:hasSkill("fuli") and player:getMark("@laoji")>0 and player:getCards("e"):length()<2) then
		return true
	end
	if player:hasFlag("AI_doNotSave") then return true end
	return false
end




sgs.ai_skill_invoke.shelie = true

local gongxin_skill={}
gongxin_skill.name="gongxin"
table.insert(sgs.ai_skills,gongxin_skill)
gongxin_skill.getTurnUseCard=function(self)
	return sgs.Card_Parse("@GongxinCard=.")
end

sgs.ai_skill_use_func.GongxinCard=function(card,use,self)
	self:sort(self.enemies,"handcard")
	self.enemies = sgs.reverse(self.enemies)

	for _,enemy in sgs.list(self.enemies)do
		if not enemy:isKongcheng() and self:objectiveLevel(enemy)>0
			and (self:hasSuit("heart",false,enemy) or self:getKnownNum(eneny)~=enemy:getHandcardNum()) then
			use.card = card
			use.to:append(enemy)
			return
		end
	end
end

sgs.ai_skill_askforag.gongxin = function(self,card_ids)
	self.gongxinchoice = nil
	local target = self.player:getTag("gongxin"):toPlayer()
	if not target or self:isFriend(target) then return -1 end
	local nextAlive = self.player
	repeat
		nextAlive = nextAlive:getNextAlive()
	until nextAlive:faceUp()

	local peach,ex_nihilo,jink,nullification,slash
	local valuable
	for _,id in sgs.list(card_ids)do
		local card = sgs.Sanguosha:getCard(id)
		if card:isKindOf("Peach") then peach = id end
		if card:isKindOf("ExNihilo") then ex_nihilo = id end
		if card:isKindOf("Jink") then jink = id end
		if card:isKindOf("Nullification") then nullification = id end
		if card:isKindOf("Slash") then slash = id end
	end
	valuable = peach or ex_nihilo or jink or nullification or slash or card_ids[1]
	local card = sgs.Sanguosha:getCard(valuable)
	if self:isEnemy(target) and target:hasSkill("tuntian") then
		local zhangjiao = self.room:findPlayerBySkillName("guidao")
		if zhangjiao and self:isFriend(zhangjiao,target) and self:canRetrial(zhangjiao,target) and self:isValuableCard(card,zhangjiao) then
			self.gongxinchoice = "discard"
		else
			self.gongxinchoice = "put"
		end
		return valuable
	end

	local willUseExNihilo,willRecast
	if self:getCardsNum("ExNihilo")>0 then
		local ex_nihilo = self:getCard("ExNihilo")
		if ex_nihilo then
			local dummy_use = dummy()
			self:useTrickCard(ex_nihilo,dummy_use)
			if dummy_use.card then willUseExNihilo = true end
		end
	elseif self:getCardsNum("IronChain")>0 then
		local iron_chain = self:getCard("IronChain")
		if iron_chain then
			local dummy_use = dummy()
			self:useTrickCard(iron_chain,dummy_use)
			if dummy_use.card and dummy_use.to:isEmpty() then willRecast = true end
		end
	end
	if willUseExNihilo or willRecast then
		local card = sgs.Sanguosha:getCard(valuable)
		if card:isKindOf("Peach") then
			self.gongxinchoice = "put"
			return valuable
		end
		if card:isKindOf("TrickCard") or card:isKindOf("Indulgence") or card:isKindOf("SupplyShortage") then
			local dummy_use = dummy()
			self:useTrickCard(card,dummy_use)
			if dummy_use.card then
				self.gongxinchoice = "put"
				return valuable
			end
		end
		if card:isKindOf("Jink") and self:getCardsNum("Jink")==0 then
			self.gongxinchoice = "put"
			return valuable
		end
		if card:isKindOf("Nullification") and self:getCardsNum("Nullification")==0 then
			self.gongxinchoice = "put"
			return valuable
		end
		if card:isKindOf("Slash") and self:slashIsAvailable() then
			local dummy_use = dummy()
			self:useBasicCard(card,dummy_use)
			if dummy_use.card then
				self.gongxinchoice = "put"
				return valuable
			end
		end
		self.gongxinchoice = "discard"
		return valuable
	end

	local hasLightning,hasIndulgence,hasSupplyShortage
	local tricks = nextAlive:getJudgingArea()
	if not tricks:isEmpty() and not nextAlive:containsTrick("YanxiaoCard") then
		local trick = tricks:at(tricks:length()-1)
		if self:hasTrickEffective(trick,nextAlive) then
			if trick:isKindOf("Lightning") then hasLightning = true
			elseif trick:isKindOf("Indulgence") then hasIndulgence = true
			elseif trick:isKindOf("SupplyShortage") then hasSupplyShortage = true
			end
		end
	end

	if self:isEnemy(nextAlive) and nextAlive:hasSkill("luoshen") and valuable then
		self.gongxinchoice = "put"
		return valuable
	end
	if nextAlive:hasSkill("yinghun") and nextAlive:isWounded() then
		self.gongxinchoice = self:isFriend(nextAlive) and "put" or "discard"
		return valuable
	end
	if target:hasSkill("hongyan") and hasLightning and self:isEnemy(nextAlive) and not (nextAlive:hasSkill("qiaobian") and nextAlive:getHandcardNum()>0) then
		for _,id in sgs.list(card_ids)do
			local card = sgs.Sanguosha:getEngineCard(id)
			if card:getSuit()==sgs.Card_Spade and card:getNumber()>=2 and card:getNumber()<=9 then
				self.gongxinchoice = "put"
				return id
			end
		end
	end
	if hasIndulgence and self:isFriend(nextAlive) then
		self.gongxinchoice = "put"
		return valuable
	end
	if hasSupplyShortage and self:isEnemy(nextAlive) and not (nextAlive:hasSkill("qiaobian") and nextAlive:getHandcardNum()>0) then
		local enemy_null = 0
		for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
			if self:isFriend(p) then enemy_null = enemy_null-getCardsNum("Nullification",p) end
			if self:isEnemy(p) then enemy_null = enemy_null+getCardsNum("Nullification",p) end
		end
		enemy_null = enemy_null-self:getCardsNum("Nullification")
		if enemy_null<0.8 then
			self.gongxinchoice = "put"
			return valuable
		end
	end

	if self:isFriend(nextAlive) and not self:willSkipDrawPhase(nextAlive) and not self:willSkipPlayPhase(nextAlive)
		and not nextAlive:hasSkill("luoshen")
		and not nextAlive:hasSkill("tuxi") and not (nextAlive:hasSkill("qiaobian") and nextAlive:getHandcardNum()>0) then
		if (peach and valuable==peach) or (ex_nihilo and valuable==ex_nihilo) then
			self.gongxinchoice = "put"
			return valuable
		end
		if jink and valuable==jink and getCardsNum("Jink",nextAlive)<1 then
			self.gongxinchoice = "put"
			return valuable
		end
		if nullification and valuable==nullification and getCardsNum("Nullification",nextAlive)<1 then
			self.gongxinchoice = "put"
			return valuable
		end
		if slash and valuable==slash and self:hasCrossbowEffect(nextAlive) then
			self.gongxinchoice = "put"
			return valuable
		end
	end

	local card = sgs.Sanguosha:getCard(valuable)
	local keep = false
	if card:isKindOf("Slash") or card:isKindOf("Jink")
		or card:isKindOf("EquipCard")
		or card:isKindOf("Disaster") or card:isKindOf("GlobalEffect") or card:isKindOf("Nullification")
		or target:isLocked(card) then
		keep = true
	end
	self.gongxinchoice = (target:objectName()==nextAlive:objectName() and keep) and "put" or "discard"
	return valuable
end

sgs.ai_skill_choice.gongxin = function(self,choices)
	return self.gongxinchoice or "discard"
end

sgs.ai_use_value.GongxinCard = 8.5
sgs.ai_use_priority.GongxinCard = 9.5
sgs.ai_card_intention.GongxinCard = 80
