sgs.weapon_range.SPMoonSpear = 3

sgs.ai_skill_playerchosen.sp_moonspear = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"defense")
	for _,target in ipairs(targets)do
		if self:isEnemy(target) and self:damageIsEffective(target) and self:isGoodTarget(target,targets) then
			return target
		end
	end
	return nil
end

sgs.ai_playerchosen_intention.sp_moonspear = 80

function sgs.ai_slash_prohibit.weidi(self,from,to,card)
	local lord = self.room:getLord()
	if not lord then return false end
	if to:isLord() then return false end
	for _,askill in sgs.qlist(lord:getVisibleSkillList(true))do
		if askill:objectName()~="weidi" and askill:isLordSkill() then
			local filter = sgs.ai_slash_prohibit[askill:objectName()]
			if type(filter)=="function" and filter(self,from,to,card) then return true end
		end
	end
end

sgs.ai_skill_use["@jijiang"] = function(self,prompt)
	if self.player:hasFlag("Global_JijiangFailed") then return "." end
	local card = sgs.Card_Parse("@JijiangCard=.")
	local dummy_use = dummy()
	self:useSkillCard(card,dummy_use)
	if dummy_use.card then
		local jijiang = {}
		if sgs.jijiangtarget then
			for _,p in ipairs(sgs.jijiangtarget)do
				table.insert(jijiang,p:objectName())
			end
			return "@JijiangCard=.->"..table.concat(jijiang,"+")
		end
	end
	return "."
end

sgs.ai_skill_use["@oljijiang"] = function(self,prompt)
	if self.player:hasFlag("Global_JijiangFailed") then return "." end
	local card = sgs.Card_Parse("@OLJijiangCard=.")
	local dummy_use = dummy()
	self:useSkillCard(card,dummy_use)
	if dummy_use.card then
		local jijiang = {}
		if sgs.oljijiangtarget then
			for _,p in ipairs(sgs.oljijiangtarget)do
				table.insert(jijiang,p:objectName())
			end
			return "@OLJijiangCard=.->"..table.concat(jijiang,"+")
		end
	end
	return "."
end

--[[
	技能：庸肆（弃牌部分）
	备注：为了解决场上有古锭刀时弃白银狮子的问题而重写此弃牌方案。
]]--
sgs.ai_skill_discard.yongsi = function(self,discard_num,min_num,optional,include_equip)
	if optional then
		return {}
	end
	local flag = "h"
	local equips = self.player:getEquips()
	if include_equip and not (equips:isEmpty() or self.player:isJilei(equips:first())) then flag = flag.."e" end
	local cards = self.player:getCards(flag)
	local to_discard = {}
	cards = sgs.QList2Table(cards)
	local aux_func = function(card)
		local place = self.room:getCardPlace(card:getEffectiveId())
		if place==sgs.Player_PlaceEquip then
			if card:isKindOf("SilverLion") then
				local players = self.room:getOtherPlayers(self.player)
				for _,p in sgs.qlist(players)do
					local blade = p:getWeapon()
					if blade and blade:isKindOf("GudingBlade") then
						if p:inMyAttackRange(self.player) then
							if self:isEnemy(p,self.player) then
								return 6
							end
						else
							break --因为只有一把古锭刀，检测到有人装备了，其他人就不会再装备了，此时可跳出检测。
						end
					end
				end
				if self.player:isWounded() then
					return -2
				end
			elseif card:isKindOf("Weapon") and self.player:getHandcardNum()<discard_num+2 and not self:needKongcheng() then return 0
			elseif card:isKindOf("OffensiveHorse") and self.player:getHandcardNum()<discard_num+2 and not self:needKongcheng() then return 0
			elseif card:isKindOf("OffensiveHorse") then return 1
			elseif card:isKindOf("Weapon") then return 2
			elseif card:isKindOf("DefensiveHorse") then return 3
			elseif self:hasSkills("bazhen|yizhong") and card:isKindOf("Armor") then return 0
			elseif card:isKindOf("Armor") then
				return 4
			end
		elseif self:hasSkills(sgs.lose_equip_skill) then
			return 5
		else
			return 0
		end
		return 0
	end
	local compare_func = function(a,b)
		if aux_func(a)~=aux_func(b) then return aux_func(a)<aux_func(b) end
		return self:getKeepValue(a)<self:getKeepValue(b)
	end

	table.sort(cards,compare_func)
	local least = min_num
	if discard_num-min_num>1 then
		least = discard_num -1
	end
	for _,card in ipairs(cards)do
		if not self.player:isJilei(card) then
			table.insert(to_discard,card:getId())
		end
		if (self.player:hasSkill("qinyin") and #to_discard>=least) or #to_discard>=discard_num then
			break
		end
	end
	return to_discard
end

sgs.ai_skill_invoke.danlao = function(self,data)
	local effect = data:toCardUse()
	local current = self.room:getCurrent()
	if effect.card:isKindOf("GodSalvation") and self.player:isWounded() or effect.card:isKindOf("ExNihilo") then
		return false
	elseif effect.card:isKindOf("AmazingGrace") and
		(self.player:getSeat()-current:getSeat()) % (global_room:alivePlayerCount())<global_room:alivePlayerCount()/2 then
		return false
	else
		return true
	end
end

sgs.ai_skill_invoke.jilei = function(self,data)
	local damage = data:toDamage()
	if not damage then return false end
	self.jilei_source = damage.from
	return self:isEnemy(damage.from)
end

sgs.ai_skill_choice.jilei = function(self,choices)
	local tmptrick = dummyCard("ex_nihilo")
	if (self:hasCrossbowEffect(self.jilei_source) and self.jilei_source:inMyAttackRange(self.player))
		or self.jilei_source:isCardLimited(tmptrick,sgs.Card_MethodUse,true) then
		return "BasicCard"
	else
		return "TrickCard"
	end
end

local function yuanhu_validate(self,equip_type,is_handcard)
	local is_SilverLion = false
	if equip_type=="SilverLion" then
		equip_type = "Armor"
		is_SilverLion = true
	end
	local targets
	if is_handcard then targets = self.friends else targets = self.friends_noself end
	if equip_type~="Weapon" then
		if equip_type=="DefensiveHorse" or equip_type=="OffensiveHorse" then self:sort(targets,"hp") end
		if equip_type=="Armor" then self:sort(targets,"handcard") end
		if is_SilverLion then
			for _,enemy in ipairs(self.enemies)do
				if enemy:hasSkill("kongcheng") and enemy:isKongcheng() then
					local seat_diff = enemy:getSeat()-self.player:getSeat()
					local alive_count = self.room:alivePlayerCount()
					if seat_diff<0 then seat_diff = seat_diff+alive_count end
					if seat_diff>alive_count/2.5+1 then return enemy  end
				end
			end
			for _,enemy in ipairs(self.enemies)do
				if self:hasSkills("bazhen|yizhong",enemy) then
					return enemy
				end
			end
		end
		for _,friend in ipairs(targets)do
			local has_equip = false
			for _,equip in sgs.qlist(friend:getEquips())do
				if equip:isKindOf(equip_type) then
					has_equip = true
					break
				end
			end
			if not has_equip then
				if equip_type=="Armor" then
					if not self:needKongcheng(friend,true) and not self:hasSkills("bazhen|yizhong",friend) then return friend end
				else
					if friend:isWounded() and not (friend:hasSkill("longhun") and friend:getCardCount(true)>=3) then return friend end
				end
			end
		end
	else
		for _,friend in ipairs(targets)do
			local has_equip = false
			for _,equip in sgs.qlist(friend:getEquips())do
				if equip:isKindOf(equip_type) then
					has_equip = true
					break
				end
			end
			if not has_equip then
				for _,aplayer in sgs.qlist(self.room:getAllPlayers())do
					if friend:distanceTo(aplayer)==1 then
						if self:isFriend(aplayer) and not aplayer:containsTrick("YanxiaoCard")
							and (aplayer:containsTrick("indulgence") or aplayer:containsTrick("supply_shortage")
								or (aplayer:containsTrick("lightning") and self:hasWizard(self.enemies))) then
							aplayer:setFlags("AI_YuanhuToChoose")
							return friend
						end
					end
				end
				self:sort(self.enemies,"defense")
				for _,enemy in ipairs(self.enemies)do
					if friend:distanceTo(enemy)==1 and self.player:canDiscard(enemy,"he") then
						enemy:setFlags("AI_YuanhuToChoose")
						return friend
					end
				end
			end
		end
	end
	return nil
end

sgs.ai_skill_use["@@yuanhu"] = function(self,prompt)
	local cards = self.player:getHandcards()
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	if self.player:hasArmorEffect("SilverLion") then
		local player = yuanhu_validate(self,"SilverLion",false)
		if player then return "@YuanhuCard="..self.player:getArmor():getEffectiveId().."->"..player:objectName() end
	end
	if self.player:getOffensiveHorse() then
		local player = yuanhu_validate(self,"OffensiveHorse",false)
		if player then return "@YuanhuCard="..self.player:getOffensiveHorse():getEffectiveId().."->"..player:objectName() end
	end
	if self.player:getWeapon() then
		local player = yuanhu_validate(self,"Weapon",false)
		if player then return "@YuanhuCard="..self.player:getWeapon():getEffectiveId().."->"..player:objectName() end
	end
	if self.player:getArmor() and self.player:getLostHp()<=1 and self.player:getHandcardNum()>=3 then
		local player = yuanhu_validate(self,"Armor",false)
		if player then return "@YuanhuCard="..self.player:getArmor():getEffectiveId().."->"..player:objectName() end
	end
	for _,card in ipairs(cards)do
		if card:isKindOf("DefensiveHorse") then
			local player = yuanhu_validate(self,"DefensiveHorse",true)
			if player then return "@YuanhuCard="..card:getEffectiveId().."->"..player:objectName() end
		end
	end
	for _,card in ipairs(cards)do
		if card:isKindOf("OffensiveHorse") then
			local player = yuanhu_validate(self,"OffensiveHorse",true)
			if player then return "@YuanhuCard="..card:getEffectiveId().."->"..player:objectName() end
		end
	end
	for _,card in ipairs(cards)do
		if card:isKindOf("Weapon") then
			local player = yuanhu_validate(self,"Weapon",true)
			if player then return "@YuanhuCard="..card:getEffectiveId().."->"..player:objectName() end
		end
	end
	for _,card in ipairs(cards)do
		if card:isKindOf("SilverLion") then
			local player = yuanhu_validate(self,"SilverLion",true)
			if player then return "@YuanhuCard="..card:getEffectiveId().."->"..player:objectName() end
		end
		if card:isKindOf("Armor") and yuanhu_validate(self,"Armor",true) then
			local player = yuanhu_validate(self,"Armor",true)
			if player then return "@YuanhuCard="..card:getEffectiveId().."->"..player:objectName() end
		end
	end
end

sgs.ai_skill_playerchosen.yuanhu = function(self,targets)
	targets = sgs.QList2Table(targets)
	for _,p in ipairs(targets)do
		if p:hasFlag("AI_YuanhuToChoose") then
			p:setFlags("-AI_YuanhuToChoose")
			return p
		end
	end
	return targets[1]
end

sgs.ai_card_intention.YuanhuCard = function(self,card,from,to)
	if to[1]:hasSkill("bazhen") or to[1]:hasSkill("yizhong") or (to[1]:hasSkill("kongcheng") and to[1]:isKongcheng()) then
		if sgs.Sanguosha:getCard(card:getEffectiveId()):isKindOf("SilverLion") then
			sgs.updateIntention(from,to[1],10)
			return
		end
	end
	sgs.updateIntention(from,to[1],-50)
end

sgs.ai_cardneed.yuanhu = sgs.ai_cardneed.equip

sgs.yuanhu_keep_value = {
	Peach = 6,
	Jink = 5.1,
	Weapon = 4.7,
	Armor = 4.8,
	Horse = 4.9
}

sgs.ai_cardneed.xueji = function(to,card)
	return to:getHandcardNum()<3 and card:isRed()
end

local xueji_skill = {}
xueji_skill.name = "xueji"
table.insert(sgs.ai_skills,xueji_skill)
xueji_skill.getTurnUseCard = function(self)
	if not self.player:isWounded() then return end

	local card
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByUseValue(cards,true)

	for _,acard in ipairs(cards)do
		if acard:isRed() then
			card = acard
			break
		end
	end
	if card then
		card = sgs.Card_Parse("@XuejiCard="..card:getEffectiveId())
		return card
	end

	return nil
end

local function can_be_selected_as_target_xueji(self,card,who)
	-- validation of rule
	if self.player:getWeapon() and self.player:getWeapon():getEffectiveId()==card:getEffectiveId() then
		if self.player:distanceTo(who,sgs.weapon_range[self.player:getWeapon():getClassName()]-self.player:getAttackRange(false))>self.player:getAttackRange() then return false end
	elseif self.player:getOffensiveHorse() and self.player:getOffensiveHorse():getEffectiveId()==card:getEffectiveId() then
		if self.player:distanceTo(who,1)>self.player:getAttackRange() then return false end
	elseif self.player:distanceTo(who)>self.player:getAttackRange() then
		return false
	end
	-- validation of strategy
	if self:isEnemy(who) and self:damageIsEffective(who) and not self:cantbeHurt(who) and not self:needToLoseHp(who) then
		if not self.player:hasSkills("jueqing|gangzhi") then
			if who:hasSkill("guixin") and (self.room:getAliveCount()>=4 or not who:faceUp()) and not who:hasSkill("manjuan") then return false end
			if (who:hasSkill("ganglie") or who:hasSkill("neoganglie")) and (self.player:getHp()==1 and self.player:getHandcardNum()<=2) then return false end
			if who:hasSkill("jieming") then
				for _,enemy in ipairs(self.enemies)do
					if enemy:getHandcardNum()<=enemy:getMaxHp()-2 and not enemy:hasSkill("manjuan") then return false end
				end
			end
			if who:hasSkill("fangzhu") then
				for _,enemy in ipairs(self.enemies)do
					if not enemy:faceUp() then return false end
				end
			end
			if who:hasSkill("yiji") then
				local huatuo = self.room:findPlayerBySkillName("jijiu")
				if huatuo and self:isEnemy(huatuo) and huatuo:getHandcardNum()>=3 then
					return false
				end
			end
		end
		return true
	elseif self:isFriend(who) then
		if who:hasSkill("yiji") and not self.player:hasSkills("jueqing|gangzhi") then
			local huatuo = self.room:findPlayerBySkillName("jijiu")
			if (huatuo and self:isFriend(huatuo) and huatuo:getHandcardNum()>=3 and huatuo~=self.player)
				or (who:getLostHp()==0 and who:getMaxHp()>=3) then
				return true
			end
		end
		if who:hasSkill("hunzi") and who:getMark("hunzi")==0
		  and who:objectName()==self.player:getNextAlive():objectName() and who:getHp()==2 then
			return true
		end
		if self:cantbeHurt(who) and not self:damageIsEffective(who) and not (who:hasSkill("manjuan") and who:getPhase()==sgs.Player_NotActive)
		  and not (who:hasSkill("kongcheng") and who:isKongcheng()) then
			return true
		end
		return false
	end
	return false
end

sgs.ai_skill_use_func.XuejiCard = function(card,use,self)
	if self.player:getLostHp()==0 then return end
	self:sort(self.enemies)
	local to_use = false
	for _,enemy in ipairs(self.enemies)do
		if can_be_selected_as_target_xueji(self,card,enemy) then
			to_use = true
			break
		end
	end
	if not to_use then
		for _,friend in ipairs(self.friends_noself)do
			if can_be_selected_as_target_xueji(self,card,friend) then
				to_use = true
				break
			end
		end
	end
	if to_use then
		use.card = card
		for _,enemy in ipairs(self.enemies)do
			if can_be_selected_as_target_xueji(self,card,enemy) then
				use.to:append(enemy)
				if use.to:length()==self.player:getLostHp() then return end
			end
		end
		for _,friend in ipairs(self.friends_noself)do
			if can_be_selected_as_target_xueji(self,card,friend) then
				use.to:append(friend)
				if use.to:length()==self.player:getLostHp() then return end
			end
		end
	end
end

sgs.ai_card_intention.XuejiCard = function(self,card,from,tos)
	local room = from:getRoom()
	local huatuo = room:findPlayerBySkillName("jijiu")
	for _,to in ipairs(tos)do
		local intention = 60
		if to:hasSkill("yiji") and not from:hasSkill("jueqing") then
			if (huatuo and self:isFriend(huatuo) and huatuo:getHandcardNum()>=3 and huatuo:objectName()~=from:objectName()) then
				intention = -30
			end
			if to:getLostHp()==0 and to:getMaxHp()>=3 then
				intention = -10
			end
		end
		if to:hasSkill("hunzi") and to:getMark("hunzi")==0 then
			if to:objectName()==from:getNextAlive():objectName() and to:getHp()==2 then
				intention = -20
			end
		end
		if self:cantbeHurt(to) and not self:damageIsEffective(to) then intention = -20 end
		sgs.updateIntention(from,to,intention)
	end
end

sgs.ai_use_value.XuejiCard = 3
sgs.ai_use_priority.XuejiCard = 2.35

sgs.ai_skill_use["@@bifa"] = function(self,prompt)
	local cards = self.player:getHandcards()
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	self:sort(self.enemies,"hp")
	if #self.enemies<0 then return "." end
	for _,enemy in ipairs(self.enemies)do
	if enemy:getPile("bifa"):length()>0 then continue end
		if not (self:needToLoseHp(enemy) and not self:hasSkills(sgs.masochism_skill,enemy)) then
			for _,c in ipairs(cards)do
				if c:isKindOf("EquipCard") then return "@BifaCard="..c:getEffectiveId().."->"..enemy:objectName() end
			end
			for _,c in ipairs(cards)do
				if c:isKindOf("TrickCard") and not (c:isKindOf("Nullification") and self:getCardsNum("Nullification")==1) then
					return "@BifaCard="..c:getEffectiveId().."->"..enemy:objectName()
				end
			end
			for _,c in ipairs(cards)do
				if c:isKindOf("Slash") then
					return "@BifaCard="..c:getEffectiveId().."->"..enemy:objectName()
				end
			end
		end
	end
end

sgs.ai_skill_cardask["@bifa-give"] = function(self,data)
	local card_type = data:toString()
	local cards = self.player:getHandcards()
	cards = sgs.QList2Table(cards)
	if self:needToLoseHp() and not self:hasSkills(sgs.masochism_skill) then return "." end
	self:sortByUseValue(cards)
	for _,c in ipairs(cards)do
		if c:isKindOf(card_type) and not isCard("Peach",c,self.player) and not isCard("ExNihilo",c,self.player) then
			return "$"..c:getEffectiveId()
		end
	end
	return "."
end

sgs.ai_card_intention.BifaCard = 30

sgs.bifa_keep_value = {
	Peach = 6,
	Jink = 5.1,
	Nullification = 5,
	EquipCard = 4.9,
	TrickCard = 4.8
}

local songci_skill = {}
songci_skill.name = "songci"
table.insert(sgs.ai_skills,songci_skill)
songci_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@SongciCard=.")
end

sgs.ai_skill_use_func.SongciCard = function(card,use,self)
	self:sort(self.friends,"handcard")
	for _,friend in ipairs(self.friends)do
		if friend:getMark("songci"..self.player:objectName())==0 and friend:getHandcardNum()<friend:getHp() and self:canDraw(friend) then
			use.card = sgs.Card_Parse("@SongciCard=.")
			use.to:append(friend)
			return
		end
	end

	self:sort(self.enemies,"handcard")
	self.enemies = sgs.reverse(self.enemies)
	for _,enemy in ipairs(self.enemies)do
		if enemy:getMark("songci"..self.player:objectName())==0 and enemy:getHandcardNum()>enemy:getHp() and not enemy:isNude()
			and self:doDisCard(enemy,"he",true,2) then
			use.card = sgs.Card_Parse("@SongciCard=.")
			use.to:append(enemy)
			return
		end
	end
end

sgs.ai_use_value.SongciCard = 3
sgs.ai_use_priority.SongciCard = 3

sgs.ai_card_intention.SongciCard = function(self,card,from,to)
	sgs.updateIntention(from,to[1],to[1]:getHandcardNum()>to[1]:getHp() and 80 or -80)
end

sgs.ai_skill_cardask["@xingwu"] = function(self,data)
	local cards = sgs.QList2Table(self.player:getHandcards())
	if #cards<=1 and self.player:getPile("xingwu"):length()==1 then return "." end

	local good_enemies = {}
	for _,enemy in ipairs(self.enemies)do
		if enemy:isMale() and ((self:damageIsEffective(enemy) and not self:cantbeHurt(enemy,self.player,2))
								or (not self:damageIsEffective(enemy) and not enemy:getEquips():isEmpty()
									and not (enemy:getEquips():length()==1 and enemy:getArmor() and self:needToThrowArmor(enemy)))) then
			table.insert(good_enemies,enemy)
		end
	end
	if #good_enemies==0 and (not self.player:getPile("xingwu"):isEmpty() or not self.player:hasSkills("luoyan|olluoyan")) then return "." end

	local red_avail,black_avail
	local n = self.player:getMark("xingwu")
	if bit32.band(n,2)==0 then red_avail = true end
	if bit32.band(n,1)==0 then black_avail = true end

	self:sortByKeepValue(cards)
	local xwcard = nil
	local heart = 0
	local to_save = 0
	for _,card in ipairs(cards)do
		if self.player:hasSkills("tianxiang|oltianxiang") and card:getSuit()==sgs.Card_Heart and heart<math.min(self.player:getHp(),2) then
			heart = heart+1
		elseif isCard("Jink",card,self.player) then
			if self.player:hasSkill("liuli") and self.room:alivePlayerCount()>2 then
				for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
					if self:canLiuli(self.player,p) then
						xwcard = card
						break
					end
				end
			end
			if not xwcard and self:getCardsNum("Jink")>=2 then
				xwcard = card
			end
		elseif to_save>self.player:getMaxCards()
				or (not isCard("Peach",card,self.player) and not (self:isWeak() and isCard("Analeptic",card,self.player))) then
			xwcard = card
		else
			to_save = to_save+1
		end
		if xwcard then
			if (red_avail and xwcard:isRed()) or (black_avail and xwcard:isBlack()) then
				break
			else
				xwcard = nil
				to_save = to_save+1
			end
		end
	end
	if xwcard then return "$"..xwcard:getEffectiveId() else return "." end
end

sgs.ai_skill_playerchosen.xingwu = function(self,targets)
	local good_enemies = {}
	for _,enemy in ipairs(self.enemies)do
		if enemy:isMale() then
			table.insert(good_enemies,enemy)
		end
	end
	if #good_enemies==0 then return targets:first() end

	local getCmpValue = function(enemy)
		local value = 0
		if self:damageIsEffective(enemy) then
			local dmg = enemy:hasArmorEffect("SilverLion") and 1 or 2
			if enemy:getHp()<=dmg then value = 5 else value = value+enemy:getHp()/(enemy:getHp()-dmg) end
			if not self:isGoodTarget(enemy,self.enemies) then value = value-2 end
			if self:cantbeHurt(enemy,self.player,dmg) then value = value-5 end
			if enemy:isLord() then value = value+2 end
			if enemy:hasArmorEffect("SilverLion") then value = value-1.5 end
			if self:hasSkills(sgs.exclusive_skill,enemy) then value = value-1 end
			if self:hasSkills(sgs.masochism_skill,enemy) then value = value-0.5 end
		end
		if not enemy:getEquips():isEmpty() then
			local len = enemy:getEquips():length()
			if enemy:hasSkills(sgs.lose_equip_skill) then value = value-0.6*len end
			if enemy:getArmor() and self:needToThrowArmor() then value = value-1.5 end
			if enemy:hasArmorEffect("SilverLion") then value = value-0.5 end

			if enemy:getWeapon() then value = value+0.8 end
			if enemy:getArmor() then value = value+1 end
			if enemy:getDefensiveHorse() then value = value+0.9 end
			if enemy:getOffensiveHorse() then value = value+0.7 end
			if self:getDangerousCard(enemy) then value = value+0.3 end
			if self:getValuableCard(enemy) then value = value+0.15 end
		end
		return value
	end
	local pvs = {}
	for _,p in ipairs(good_enemies)do
		pvs[p:objectName()] = getCmpValue(p)
	end

	local cmp = function(a,b)
		return pvs[a:objectName()]>pvs[b:objectName()]
	end
	table.sort(good_enemies,cmp)
	return good_enemies[1]
end

sgs.ai_playerchosen_intention.xingwu = 80

sgs.ai_skill_cardask["@tenyearxingwu-card"] = function(self,data)
	return sgs.ai_skill_cardask["@xingwu"](self,data)
end

sgs.ai_skill_use["@@tenyearxingwu"] = function(self,prompt)
	local tp = sgs.ai_skill_playerchosen.xingwu(self,self.room:getOtherPlayers(self.player))
	if not tp then return "." end
	local ids = {}
	for _,id in sgs.qlist(self.player:getPile("xingwu"))do
		table.insert(ids,id)
		if #ids>2 then
			return "@TenyearXingwuCard="..table.concat(ids,"+").."->"..tp:objectName()
		end
	end
end

sgs.ai_playerchosen_intention.tenyearxingwu = 80

sgs.ai_skill_cardask["@olxingwu-card"] = function(self,data)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	if #cards<=1 and self.player:getPile("xingwu"):length()==1 then return "." end

	local good_enemies = {}
	for _,enemy in ipairs(self.enemies)do
		if enemy:isMale() and ((self:damageIsEffective(enemy) and not self:cantbeHurt(enemy,self.player,2))
								or (not self:damageIsEffective(enemy) and not enemy:getEquips():isEmpty()
									and not (enemy:getEquips():length()==1 and enemy:getArmor() and self:needToThrowArmor(enemy)))) then
			table.insert(good_enemies,enemy)
		end
	end
	if #good_enemies==0 and (not self.player:getPile("xingwu"):isEmpty() or not self.player:hasSkills("luoyan|olluoyan")) then return "." end

	local red_avail,black_avail
	local n = self.player:getMark("xingwu")
	if bit32.band(n,2)==0 then red_avail = true end
	if bit32.band(n,1)==0 then black_avail = true end

	self:sortByKeepValue(cards)
	local xwcard = nil
	local heart = 0
	local to_save = 0
	for _,card in ipairs(cards)do
		if self.player:hasSkills("tianxiang|oltianxiang") and card:getSuit()==sgs.Card_Heart and heart<math.min(self.player:getHp(),2) then
			heart = heart+1
		elseif isCard("Jink",card,self.player) then
			if self.player:hasSkill("liuli") and self.room:alivePlayerCount()>2 then
				for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
					if self:canLiuli(self.player,p) then
						xwcard = card
						break
					end
				end
			end
			if not xwcard and self:getCardsNum("Jink")>=2 then
				xwcard = card
			end
		elseif to_save>self.player:getMaxCards()
				or (not isCard("Peach",card,self.player) and not (self:isWeak() and isCard("Analeptic",card,self.player))) then
			xwcard = card
		else
			to_save = to_save+1
		end
		if xwcard then
			if (red_avail and xwcard:isRed()) or (black_avail and xwcard:isBlack()) then
				break
			else
				xwcard = nil
				to_save = to_save+1
			end
		end
	end
	if xwcard then return "$"..xwcard:getEffectiveId() else return "." end
end

sgs.ai_playerchosen_intention.olxingwu = 80

sgs.ai_skill_use["@@olxingwu"] = function(self,prompt)
	local tp = sgs.ai_skill_playerchosen.xingwu(self,self.room:getOtherPlayers(self.player))
	if not tp then return "." end
	local ids = {}
	for _,id in sgs.qlist(self.player:getPile("xingwu"))do
		table.insert(ids,id)
		if #ids>2 then
			return "@OLXingwuCard="..table.concat(ids,"+").."->"..tp:objectName()
		end
	end
	local cards = sgs.QList2Table(self.player:getHandcards())
	if #cards>1 and (self:isEnemy(tp) and self:isWeak(tp) or not self.player:faceUp()) then
		self:sortByKeepValue(cards,nil,"j")
		ids = {}
		for _,c in sgs.list(cards)do
			table.insert(ids,c:getId())
			if #ids>1 then
				return "@OLXingwuCard="..table.concat(ids,"+").."->"..tp:objectName()
			end
		end
	end
end

sgs.ai_skill_cardask["@yanyu-discard"] = function(self,data)
	if self.player:getHandcardNum()<3 and self.player:getPhase()~=sgs.Player_Play then
		if self:needToThrowArmor() then return "$"..self.player:getArmor():getEffectiveId()
		elseif self:needKongcheng(self.player,true) and self.player:getHandcardNum()==1 then return "$"..self.player:handCards():first()
		else return "." end
	end
	local current = self.room:getCurrent()
	local cards = sgs.QList2Table(self.player:getHandcards())
	self:sortByKeepValue(cards)
	if current:objectName()==self.player:objectName() then
		local ex_nihilo,savage_assault,archery_attack
		for _,card in ipairs(cards)do
			if card:isKindOf("ExNihilo") then ex_nihilo = card
			elseif card:isKindOf("SavageAssault") and not current:hasSkills("noswuyan|wuyan") then savage_assault = card
			elseif card:isKindOf("ArcheryAttack") and not current:hasSkills("noswuyan|wuyan") then archery_attack = card
			end
		end
		if savage_assault and self:getAoeValue(savage_assault)<=0 then savage_assault = nil end
		if archery_attack and self:getAoeValue(archery_attack)<=0 then archery_attack = nil end
		local aoe = archery_attack or savage_assault
		if ex_nihilo then
			for _,card in ipairs(cards)do
				if card:getTypeId()==sgs.Card_TypeTrick and not card:isKindOf("ExNihilo") and card:getEffectiveId()~=ex_nihilo:getEffectiveId() then
					return "$"..card:getEffectiveId()
				end
			end
		end
		if self.player:isWounded() then
			local peach
			for _,card in ipairs(cards)do
				if card:isKindOf("Peach") then
					peach = card
					break
				end
			end
			local dummy_use = dummy()
			self:useCardPeach(peach,dummy_use)
			if dummy_use.card and dummy_use.card:isKindOf("Peach") then
				for _,card in ipairs(cards)do
					if card:getTypeId()==sgs.Card_TypeBasic and card:getEffectiveId()~=peach:getEffectiveId() then
						return "$"..card:getEffectiveId()
					end
				end
			end
		end
		if aoe then
			for _,card in ipairs(cards)do
				if card:getTypeId()==sgs.Card_TypeTrick and card:getEffectiveId()~=aoe:getEffectiveId() then
					return "$"..card:getEffectiveId()
				end
			end
		end
		if self:getCardsNum("Slash")>1 then
			for _,card in ipairs(cards)do
				if card:objectName()=="slash" then
					return "$"..card:getEffectiveId()
				end
			end
		end
	else
		local throw_trick
		local aoe_type
		if getCardsNum("ArcheryAttack",current,self.player)>=1 and not current:hasSkills("noswuyan|wuyan") then aoe_type = "archery_attack" end
		if getCardsNum("SavageAssault",current,self.player)>=1 and not current:hasSkills("noswuyan|wuyan") then aoe_type = "savage_assault" end
		if aoe_type then
			local aoe = dummyCard(aoe_type)
			if self:getAoeValue(aoe,current)>0 then throw_trick = true end
		end
		if getCardsNum("ExNihilo",current,self.player)>0 then throw_trick = true end
		if throw_trick then
			for _,card in ipairs(cards)do
				if card:getTypeId()==sgs.Card_TypeTrick and not isCard("ExNihilo",card,self.player) then
					return "$"..card:getEffectiveId()
				end
			end
		end
		if self:getCardsNum("Slash")>1 then
			for _,card in ipairs(cards)do
				if card:objectName()=="slash" then
					return "$"..card:getEffectiveId()
				end
			end
		end
		if self:getCardsNum("Jink")>1 then
			for _,card in ipairs(cards)do
				if card:isKindOf("Jink") then
					return "$"..card:getEffectiveId()
				end
			end
		end
		if self.player:getHp()>=3 and (self.player:getHandcardNum()>3 or self:getCardsNum("Peach")>0) then
			for _,card in ipairs(cards)do
				if card:isKindOf("Slash") then
					return "$"..card:getEffectiveId()
				end
			end
		end
		if getCardsNum("TrickCard",current,self.player)-getCardsNum("Nullification",current,self.player)>0 then
			for _,card in ipairs(cards)do
				if card:getTypeId()==sgs.Card_TypeTrick and not isCard("ExNihilo",card,self.player) then
					return "$"..card:getEffectiveId()
				end
			end
		end
	end
	if self:needToThrowArmor() then return "$"..self.player:getArmor():getEffectiveId() else return "." end
end

sgs.ai_skill_askforag.yanyu = function(self,card_ids)
	local cards = {}
	for _,id in ipairs(card_ids)do
		table.insert(cards,sgs.Sanguosha:getEngineCard(id))
	end
	self.yanyu_need_player = nil
	local card,player = self:getCardNeedPlayer(cards,true)
	if card and player then
		self.yanyu_need_player = player
		return card:getEffectiveId()
	end
	return card_ids[1]
end

sgs.ai_skill_playerchosen.yanyu = function(self,targets)
	local only_id = self.player:getMark("YanyuOnlyId")-1
	if only_id<0 then
		return self.yanyu_need_player
	else
		local card = sgs.Sanguosha:getEngineCard(only_id)
		if card:getTypeId()==sgs.Card_TypeTrick and not card:isKindOf("Nullification") then
			return self.player
		end
		local c,player = self:getCardNeedPlayer({ card },true)
		if player then
			return player
		end
	end
	targets = sgs.QList2Table(targets)
	self:sort(targets,"defense")
	for _,p in ipairs(targets)do
		if self:isFriend(p) and self:canDraw(p) then return p end
	end
end

sgs.ai_playerchosen_intention.yanyu = function(self,from,to)
	if hasManjuanEffect(to) then return end
	local intention = -60
	if self:needKongcheng(to,true) then intention = 10 end
	sgs.updateIntention(from,to,intention)
end

sgs.ai_skill_invoke.xiaode = function(self,data)
	local round = self:playerGetRound(self.player)
	local xiaode_skill = sgs.ai_skill_choice.huashen(self,table.concat(data:toStringList(),"+"),nil,math.random(1-round,7-round))
	if xiaode_skill then
		sgs.xiaode_choice = xiaode_skill
		return true
	else
		sgs.xiaode_choice = nil
		return false
	end
end

sgs.ai_skill_choice.xiaode = function(self,choices)
	return sgs.xiaode_choice
end

function sgs.ai_cardsview_valuable.aocai(self,class_name,player)
	if class_name=="Slash"
	then return "@AocaiCard=.:slash"
	elseif class_name=="Peach" or class_name=="Analeptic"
	then
		local dying = self.room:getCurrentDyingPlayer()
		if dying and dying:objectName()==player:objectName()
		then
			local user_string = "peach+analeptic"
			if player:getMark("Global_PreventPeach")>0 then user_string = "analeptic" end
			return "@AocaiCard=.:"..user_string
		else
			local user_string
			if class_name=="Analeptic" then user_string = "analeptic" else user_string = "peach" end
			return "@AocaiCard=.:"..user_string
		end
	end
end

sgs.ai_skill_invoke.aocai = function(self,data)
	local asked = data:toStringList()
	local pattern = asked[1]
	local prompt = asked[2]
	return self:askForCard(pattern,prompt,1)~="."
end

sgs.ai_skill_askforag.aocai = function(self,card_ids)
	local card = sgs.Sanguosha:getCard(card_ids[1])
	if card:isKindOf("Jink") and self.player:hasFlag("dahe") then
		for _,id in ipairs(card_ids)do
			if sgs.Sanguosha:getCard(id):getSuit()==sgs.Card_Heart then return id end
		end
		return -1
	end
	return card_ids[1]
end

function SmartAI:getSaveNum(isFriend)
	local num = 0
	for _,player in sgs.qlist(self.room:getAllPlayers())do
		if (isFriend and self:isFriend(player)) or (not isFriend and self:isEnemy(player)) then
			if not (self.room:hasCurrent(true) and self.room:getCurrent():hasSkill("wansha")) or self.player:hasSkill("spdushi") or
			player:objectName()==self.player:objectName() then
				if player:hasSkill("jijiu") then
					num = num+self:getSuitNum("heart",true,player)
					num = num+self:getSuitNum("diamond",true,player)
					num = num+player:getHandcardNum()*0.4
				end
				if player:hasSkill("nosjiefan") and getCardsNum("Slash",player,self.player)>0 then
					if self:isFriend(player) or self:getCardsNum("Jink")==0 then num = num+getCardsNum("Slash",player,self.player) end
				end
			end
			if player:hasSkill("mobilezhiyuejian") then
				local can_dis = 0
				for _,c in sgs.qlist(player:getCards("he"))do
					if player:canDiscard(player,c:getEffectiveId()) then
						can_dis = can_dis+1
						if can_dis>=2 then break end
					end
				end
				if can_dis>=2 then num = num+1 end
				if player:objectName()==self.player:objectName() then
					num = num+getCardsNum("Peach",player,self.player)
				end
			end
			if player:hasSkill("buyi") and not player:isKongcheng() then num = num+0.3 end
			if player:hasSkills("chunlao|tenyearchunlao|secondtenyearchunlao") and not player:getPile("wine"):isEmpty() then num = num+player:getPile("wine"):length() end
			if player:hasSkill("jiuzhu") and player:getHp()>1 and not player:isNude() then
				num = num+0.9*math.max(0,math.min(player:getHp()-1,player:getCardCount(true)))
			end
			if player:hasSkill("nosrenxin") and player:objectName()~=self.player:objectName() and not player:isKongcheng() then num = num+1 end
			if player:hasSkill("luanfeng") and player:getMark("@luanfengMark")>0 and self.player:getMaxHp()>=player:getMaxHp() then
				num = num+math.min(3,self.player:getMaxHp())-self.player:getHp()
			end
		end
	end
	return num
end

local duwu_skill = {}
duwu_skill.name = "duwu"
table.insert(sgs.ai_skills,duwu_skill)
duwu_skill.getTurnUseCard = function(self,inclusive)
	if #self.enemies==0 then return end
	return sgs.Card_Parse("@DuwuCard=.")
end

sgs.ai_skill_use_func.DuwuCard = function(card,use,self)
	local cmp = function(a,b)
		if a:getHp()<b:getHp() then
			if a:getHp()==1 and b:getHp()==2 then return false else return true end
		end
		return false
	end
	local enemies = {}
	for _,enemy in ipairs(self.enemies)do
		if self:canAttack(enemy,self.player) and self.player:inMyAttackRange(enemy) then table.insert(enemies,enemy) end
	end
	if #enemies==0 then return end
	table.sort(enemies,cmp)
	if enemies[1]:getHp()<=0
	and self:damageIsEffective(enemies[1],card)
	then
		use.card = sgs.Card_Parse("@DuwuCard=.")
		use.to:append(enemies[1])
		return
	end

	-- find cards
	local card_ids = {}
	if self:needToThrowArmor() then table.insert(card_ids,self.player:getArmor():getEffectiveId()) end

	local zcards = self.player:getHandcards()
	local use_slash,keep_jink,keep_analeptic = false,false,false
	for _,zcard in sgs.qlist(zcards)do
		if not isCard("Peach",zcard,self.player) and not isCard("ExNihilo",zcard,self.player) then
			local shouldUse = true
			if zcard:getTypeId()==sgs.Card_TypeTrick then
				local dummy_use = dummy()
				self:useTrickCard(zcard,dummy_use)
				if dummy_use.card then shouldUse = false end
			end
			if zcard:getTypeId()==sgs.Card_TypeEquip and not self.player:hasEquip(zcard) then
				local dummy_use = dummy()
				self:useEquipCard(zcard,dummy_use)
				if dummy_use.card then shouldUse = false end
			end
			if isCard("Jink",zcard,self.player) and not keep_jink then
				keep_jink = true
				shouldUse = false
			end
			if self.player:getHp()==1 and isCard("Analeptic",zcard,self.player) and not keep_analeptic then
				keep_analeptic = true
				shouldUse = false
			end
			if shouldUse then table.insert(card_ids,zcard:getId()) end
		end
	end
	local hc_num = #card_ids
	local eq_num = 0
	if self.player:getOffensiveHorse() then
		table.insert(card_ids,self.player:getOffensiveHorse():getEffectiveId())
		eq_num = eq_num+1
	end
	if self.player:getWeapon() and self:evaluateWeapon(self.player:getWeapon())<5 then
		table.insert(card_ids,self.player:getWeapon():getEffectiveId())
		eq_num = eq_num+2
	end

	local function getRangefix(index)
		if index<=hc_num then return 0
		elseif index==hc_num+1 then
			if eq_num==2 then
				return sgs.weapon_range[self.player:getWeapon():getClassName()]-self.player:getAttackRange(false)
			else
				return 1
			end
		elseif index==hc_num+2 then
			return sgs.weapon_range[self.player:getWeapon():getClassName()]
		end
	end

	for _,enemy in ipairs(enemies)do
		if enemy:getHp()>#card_ids then continue end
		if enemy:getHp()<=0
		and self:damageIsEffective(enemy,card)
		then
			use.card = sgs.Card_Parse("@DuwuCard=.")
			use.to:append(enemy)
			return
		elseif enemy:getHp()>1
		and self:damageIsEffective(enemy,card)
		then
			local hp_ids = {}
			if self.player:distanceTo(enemy,getRangefix(enemy:getHp()))<=self.player:getAttackRange() then
				for _,id in ipairs(card_ids)do
					table.insert(hp_ids,id)
					if #hp_ids==enemy:getHp() then break end
				end
				use.card = sgs.Card_Parse("@DuwuCard="..table.concat(hp_ids,"+"))
				use.to:append(enemy)
				return
			end
		else
			if not self:isWeak() or self:getSaveNum(true)>=1
			and self:damageIsEffective(enemy,card)
			then
				if self.player:distanceTo(enemy,getRangefix(1))<=self.player:getAttackRange() then
					use.card = sgs.Card_Parse("@DuwuCard="..card_ids[1])
					use.to:append(enemy)
					return
				end
			end
		end
	end
end

sgs.ai_use_priority.DuwuCard = 0.6
sgs.ai_use_value.DuwuCard = 2.45
sgs.dynamic_value.damage_card.DuwuCard = true
sgs.ai_card_intention.DuwuCard = 80

function getNextJudgeReason(self,player)
	if self:playerGetRound(player)>2 then
		if player:hasSkills("ganglie|vsganglie") then return end
		local caiwenji = self.room:findPlayerBySkillName("beige")
		if caiwenji and caiwenji:canDiscard(caiwenji,"he") and self:isFriend(caiwenji,player) then return end
		if player:hasArmorEffect("EightDiagram") or player:hasSkill("bazhen") then
			if self:playerGetRound(player)>3 and self:isEnemy(player) then return "EightDiagram"
			else return end
		end
	end
	if self:isFriend(player) and player:hasSkill("luoshen") then return "luoshen" end
	if not player:getJudgingArea():isEmpty() and not player:containsTrick("YanxiaoCard") then
		return player:getJudgingArea():last():objectName()
	end
	if player:hasSkill("qianxi") then return "qianxi" end
	if player:hasSkill("nosmiji") and player:getLostHp()>0 then return "nosmiji" end
	if player:hasSkill("tuntian") then return "tuntian" end
	if player:hasSkill("tieji") then return "tieji" end
	if player:hasSkill("nosqianxi") then return "nosqianxi" end
	if player:hasSkill("caizhaoji_hujia") then return "caizhaoji_hujia" end
end

local zhoufu_skill = {}
zhoufu_skill.name = "zhoufu"
table.insert(sgs.ai_skills,zhoufu_skill)
zhoufu_skill.getTurnUseCard = function(self)
	if self.player:isKongcheng() then return end
	return sgs.Card_Parse("@ZhoufuCard=.")
end

sgs.ai_skill_use_func.ZhoufuCard = function(card,use,self)
	local cards = {}
	for _,card in sgs.qlist(self.player:getHandcards())do
		table.insert(cards,sgs.Sanguosha:getEngineCard(card:getEffectiveId()))
	end
	self:sortByKeepValue(cards)
	self:sort(self.friends_noself)
	local zhenji
	for _,friend in ipairs(self.friends_noself)do
		if friend:getPile("incantation"):length()>0 then continue end
		local reason = getNextJudgeReason(self,friend)
		if reason then
			if reason=="luoshen" or reason=="tenyearluoshen" then
				zhenji = friend
			elseif reason=="indulgence" then
				for _,card in ipairs(cards)do
					if card:getSuit()==sgs.Card_Heart or (friend:hasSkills("hongyan|olhongyan") and card:getSuit()==sgs.Card_Spade)
						and (friend:hasSkill("tiandu") or not self:isValuableCard(card)) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			elseif reason=="supply_shortage" then
				for _,card in ipairs(cards)do
					if card:getSuit()==sgs.Card_Club and (friend:hasSkill("tiandu") or not self:isValuableCard(card)) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			elseif reason=="lightning" and not friend:hasSkills("hongyan|wuyan|olhongyan") then
				for _,card in ipairs(cards)do
					if (card:getSuit()~=sgs.Card_Spade or card:getNumber()==1 or card:getNumber()>9)
						and (friend:hasSkill("tiandu") or not self:isValuableCard(card)) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			elseif reason=="nosmiji" then
				for _,card in ipairs(cards)do
					if card:getSuit()==sgs.Card_Club or (card:getSuit()==sgs.Card_Spade and not friend:hasSkills("hongyan|olhongyan")) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			elseif reason=="nosqianxi" or reason=="tuntian" then
				for _,card in ipairs(cards)do
					if (card:getSuit()~=sgs.Card_Heart and not (card:getSuit()==sgs.Card_Spade and friend:hasSkills("hongyan|olhongyan")))
						and (friend:hasSkill("tiandu") or not self:isValuableCard(card)) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			elseif reason=="tieji" or reason=="caizhaoji_hujia" then
				for _,card in ipairs(cards)do
					if (card:isRed() or card:getSuit()==sgs.Card_Spade and friend:hasSkills("hongyan|olhongyan"))
						and (friend:hasSkill("tiandu") or not self:isValuableCard(card)) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			end
		end
	end
	if zhenji then
		for _,card in ipairs(cards)do
			if card:isBlack() and not (zhenji:hasSkills("hongyan|olhongyan") and card:getSuit()==sgs.Card_Spade) then
				use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
				use.to:append(zhenji)
				return
			end
		end
	end
	self:sort(self.enemies)
	for _,enemy in ipairs(self.enemies)do
		if enemy:getPile("incantation"):length()>0 then continue end
		local reason = getNextJudgeReason(self,enemy)
		if not enemy:hasSkill("tiandu") and reason then
			if reason=="indulgence" then
				for _,card in ipairs(cards)do
					if not (card:getSuit()==sgs.Card_Heart or (enemy:hasSkills("hongyan|olhongyan") and card:getSuit()==sgs.Card_Spade))
						and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			elseif reason=="supply_shortage" then
				for _,card in ipairs(cards)do
					if card:getSuit()~=sgs.Card_Club and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			elseif reason=="lightning" and not enemy:hasSkills("hongyan|wuyan|olhongyan") then
				for _,card in ipairs(cards)do
					if card:getSuit()==sgs.Card_Spade and card:getNumber()>=2 and card:getNumber()<=9 then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			elseif reason=="nosmiji" then
				for _,card in ipairs(cards)do
					if card:isRed() or card:getSuit()==sgs.Card_Spade and enemy:hasSkills("hongyan|olhongyan") then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			elseif reason=="nosqianxi" or reason=="tuntian" then
				for _,card in ipairs(cards)do
					if (card:getSuit()==sgs.Card_Heart or card:getSuit()==sgs.Card_Spade and enemy:hasSkills("hongyan|olhongyan"))
						and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			elseif reason=="tieji" or reason=="caizhaoji_hujia" then
				for _,card in ipairs(cards)do
					if (card:getSuit()==sgs.Card_Club or (card:getSuit()==sgs.Card_Spade and not enemy:hasSkills("hongyan|olhongyan")))
						and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			end
		end
	end

	local has_indulgence,has_supplyshortage
	local friend
	for _,p in ipairs(self.friends)do
		if getKnownCard(p,self.player,"Indulgence",true,"he")>0 then
			has_indulgence = true
			friend = p
			break
		end
		if getKnownCard(p,self.player,"SupplySortage",true,"he")>0 then
			has_supplyshortage = true
			friend = p
			break
		end
	end
	if has_indulgence then
		local indulgence = dummyCard("indulgence")
		for _,enemy in ipairs(self.enemies)do
			if enemy:getPile("incantation"):length()>0 then continue end
			if self:hasTrickEffective(indulgence,enemy,friend) and self:playerGetRound(friend)<self:playerGetRound(enemy) and not self:willSkipPlayPhase(enemy) then
				for _,card in ipairs(cards)do
					if not (card:getSuit()==sgs.Card_Heart or (enemy:hasSkills("hongyan|olhongyan") and card:getSuit()==sgs.Card_Spade))
						and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			end
		end
	elseif has_supplyshortage then
		local supplyshortage = dummyCard("supply_shortage")
		for _,enemy in ipairs(self.enemies)do
			if enemy:getPile("incantation"):length()>0 then continue end
			local distance = self:getDistanceLimit(supplyshortage,friend,enemy)
			if self:hasTrickEffective(supplyshortage,enemy,friend) and self:playerGetRound(friend)<self:playerGetRound(enemy)
				and not self:willSkipDrawPhase(enemy) and friend:distanceTo(enemy)<=distance then
				for _,card in ipairs(cards)do
					if card:getSuit()~=sgs.Card_Club and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			end
		end
	end

	for _,target in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if target:getPile("incantation"):length()>0 then continue end
		if self:hasEightDiagramEffect(target) then
			for _,card in ipairs(cards)do
				if (card:isRed() and self:isFriend(target)) or (card:isBlack() and self:isEnemy(target)) and not self:isValuableCard(card) then
					use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
					use.to:append(target)
					return
				end
			end
		end
	end

	if self:getOverflow()>0 then
		for _,target in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if target:getPile("incantation"):length()>0 then continue end
			for _,card in ipairs(cards)do
				if not self:isValuableCard(card) and math.random()>0.5 then
					use.card = sgs.Card_Parse("@ZhoufuCard="..card:getEffectiveId())
					use.to:append(target)
					return
				end
			end
		end
	end
end

sgs.ai_card_intention.ZhoufuCard = 0
sgs.ai_use_value.ZhoufuCard = 2
sgs.ai_use_priority.ZhoufuCard = sgs.ai_use_priority.Indulgence-0.1

local function getKangkaiCard(self,target,data)
	local use = data:toCardUse()
	local weapon,armor,def_horse,off_horse = {},{},{},{}
	for _,card in sgs.qlist(self.player:getHandcards())do
		if card:isKindOf("Weapon") then table.insert(weapon,card)
		elseif card:isKindOf("Armor") then table.insert(armor,card)
		elseif card:isKindOf("DefensiveHorse") then table.insert(def_horse,card)
		elseif card:isKindOf("OffensiveHorse") then table.insert(off_horse,card)
		end
	end
	if #armor>0 then
		for _,card in ipairs(armor)do
			if ((not target:getArmor() and not target:hasSkills("bazhen|yizhong"))
				or (target:getArmor() and self:evaluateArmor(card,target)>=self:evaluateArmor(target:getArmor(),target)))
				and not (card:isKindOf("Vine") and use.card:isKindOf("FireSlash") and self:slashIsEffective(use.card,target,use.from)) then
				return card:getEffectiveId()
			end
		end
	end
	if self:needToThrowArmor()
		and ((not target:getArmor() and not target:hasSkills("bazhen|yizhong"))
			or (target:getArmor() and self:evaluateArmor(self.player:getArmor(),target)>=self:evaluateArmor(target:getArmor(),target)))
		and not (self.player:getArmor():isKindOf("Vine") and use.card:isKindOf("FireSlash") and self:slashIsEffective(use.card,target,use.from)) then
		return self.player:getArmor():getEffectiveId()
	end
	if #def_horse>0 then return def_horse[1]:getEffectiveId() end
	if #weapon>0 then
		for _,card in ipairs(weapon)do
			if not target:getWeapon()
				or (self:evaluateArmor(card,target)>=self:evaluateArmor(target:getWeapon(),target)) then
				return card:getEffectiveId()
			end
		end
	end
	if self.player:getWeapon() and self:evaluateWeapon(self.player:getWeapon())<5
		and (not target:getArmor()
			or (self:evaluateArmor(self.player:getWeapon(),target)>=self:evaluateArmor(target:getWeapon(),target))) then
		return self.player:getWeapon():getEffectiveId()
	end
	if #off_horse>0 then return off_horse[1]:getEffectiveId() end
	if self.player:getOffensiveHorse()
		and ((self.player:getWeapon() and not self.player:getWeapon():isKindOf("Crossbow")) or self.player:hasSkills("mashu|tuntian")) then
		return self.player:getOffensiveHorse():getEffectiveId()
	end
end

sgs.ai_skill_invoke.kangkai = function(self,data)
	self.kangkai_give_id = nil
	if hasManjuanEffect(self.player) then return false end
	local target = data:toPlayer()
	if not target then return false end
	if target:objectName()==self.player:objectName() then
		return true
	elseif not self:isFriend(target) then
		return hasManjuanEffect(target)
	else
		local id = getKangkaiCard(self,target,self.player:getTag("KangkaiSlash"))
		if id then return true else return not self:needKongcheng(target,true) end
	end
end

sgs.ai_skill_cardask["@kangkai_give"] = function(self,data,pattern,target)
	if self:isFriend(target) then
		local id = getKangkaiCard(self,target,data)
		if id then return "$"..id end
		if self:getCardsNum("Jink")>1 then
			for _,card in sgs.qlist(self.player:getHandcards())do
				if isCard("Jink",card,target) then return "$"..card:getEffectiveId() end
			end
		end
		for _,card in sgs.qlist(self.player:getHandcards())do
			if not self:isValuableCard(card) then return "$"..card:getEffectiveId() end
		end
	else
		local to_discard = self:askForDiscard("dummyreason",1,1,false,true)
		if #to_discard>0 then return "$"..to_discard[1] end
	end
end

sgs.ai_skill_invoke.kangkai_use = function(self,data)
	local use = self.player:getTag("KangkaiSlash"):toCardUse()
	local card = self.player:getTag("KangkaiCard"):toCard()
	if not use.card or not card then return false end
	if card:isKindOf("Vine") and use.card:isKindOf("FireSlash") and self:slashIsEffective(use.card,self.player,use.from) then return false end
	if ((card:isKindOf("DefensiveHorse") and self.player:getDefensiveHorse())
		or (card:isKindOf("OffensiveHorse") and (self.player:getOffensiveHorse() or (self.player:hasSkill("drmashu") and self.player:getDefensiveHorse()))))
		and not self.player:hasSkills(sgs.lose_equip_skill) then
		return false
	end
	if card:isKindOf("Armor") and ((self.player:hasSkills("bazhen|yizhong") and not self.player:getArmor())
	or (self.player:getArmor() and self:evaluateArmor(card)<self:evaluateArmor(self.player:getArmor()))) then return false end
	if card:isKindOf("Weapon") and (self.player:getWeapon() and self:evaluateArmor(card)<self:evaluateArmor(self.player:getWeapon())) then return false end
	return true
end

sgs.ai_skill_use["@@qingyi"] = function(self,prompt)
	local dc = dummyCard()
	dc:setSkillName("qingyi")
	local d = self:aiUseCard(dc)
	if d.card then
		local tos = {}
		for _,to in sgs.qlist(d.to)do
			table.insert(tos,to:objectName())
		end
		return dc:toString().."->"..table.concat(tos,"+")
	end
	return "."
end

--星彩

sgs.ai_skill_invoke.shenxian = sgs.ai_skill_invoke.luoying

local qiangwu_skill = {}
qiangwu_skill.name = "qiangwu"
table.insert(sgs.ai_skills,qiangwu_skill)
qiangwu_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@QiangwuCard=.")
end

sgs.ai_skill_use_func.QiangwuCard = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.QiangwuCard = 3
sgs.ai_use_priority.QiangwuCard = 11

--祖茂
sgs.ai_skill_use["@@yinbing"] = function(self,prompt)
	--手牌
	local otherNum = self.player:getHandcardNum()-self:getCardsNum("BasicCard")
	if otherNum==0 then return "." end

	local slashNum = self:getCardsNum("Slash")
	local jinkNum = self:getCardsNum("Jink")
	local enemyNum = #self.enemies
	local friendNum = #self.friends

	local value = 0
	if otherNum>1 then value = value+0.3 end
	for _,card in sgs.qlist(self.player:getHandcards())do
		if card:isKindOf("EquipCard") then value = value+1 end
	end
	if otherNum==1 and self:getCardsNum("Nullification")==1 then value = value-0.2 end

	--已有引兵
	if self.player:getPile("yinbing"):length()>0 then value = value+0.2 end

	--双将【空城】
	if self:needKongcheng() and self.player:getHandcardNum()==1 then value = value+3 end

	if enemyNum==1 then value = value+0.7 end
	if friendNum-enemyNum>0 then value = value+0.2 else value = value-0.3 end
	local slash = dummyCard()
	--关于 【杀】和【决斗】
	if slashNum==0 then value = value-0.1 end
	if jinkNum==0 then value = value-0.5 end
	if jinkNum==1 then value = value+0.2 end
	if jinkNum>1 then value = value+0.5 end
	if self.player:getArmor() and self.player:getArmor():isKindOf("EightDiagram") then value = value+0.4 end
	for _,enemy in ipairs(self.enemies)do
		if enemy:canSlash(self.player,slash) and self:slashIsEffective(slash,self.player,enemy) and (enemy:inMyAttackRange(self.player) or enemy:hasSkills("zhuhai|shensu")) then
			if ((enemy:getWeapon() and enemy:getWeapon():isKindOf("Crossbow")) or enemy:hasSkills("paoxiao|tianyi|xianzhen|jiangchi|fuhun|gongqi|longyin|qiangwu")) and enemy:getHandcardNum()>1 then
				value = value-0.2
			end
			if enemy:hasSkills("tieqi|wushuang|yijue|liegong|mengjin|qianxi") then
				value = value-0.2
			end
			value = value-0.2
		end
		if enemy:hasSkills("lijian|shuangxiong|mingce|mizhao") then
			value = value-0.2
		end
	end
	--肉盾
	local yuanshu = self.room:findPlayerBySkillName("tongji")
	if yuanshu and yuanshu:getHandcardNum()>yuanshu:getHp() then value = value+0.4 end
	for _,friend in ipairs(self.friends)do
		if friend:hasSkills("fangquan|zhenwei|kangkai") then value = value+0.4 end
	end

	if value<0 then return "." end

	local card_ids = {}
	local nulId
	for _,card in sgs.qlist(self.player:getHandcards())do
		if not card:isKindOf("BasicCard") then
			if card:isKindOf("Nullification") then
				nulId = card:getEffectiveId()
			else
				table.insert(card_ids,card:getEffectiveId())
			end
		end
	end
	if nulId and #card_ids==0 then
		table.insert(card_ids,nulId)
	end
	return "@YinbingCard="..table.concat(card_ids,"+").."->."
end

sgs.yinbing_keep_value = {
	EquipCard = 5,
	TrickCard = 4
}

sgs.ai_skill_invoke.juedi = function(self,data)
	for _,friend in ipairs(self.friends_noself)do
		if friend:getLostHp()>0 then return true end
	end
	if self:isWeak() then return true end
	return false
end

sgs.ai_skill_playerchosen.juedi  = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"defense")
	for _,p in ipairs(targets)do
		if self:isFriend(p) then return p end
	end
	return
end

sgs.ai_skill_invoke.meibu = function (self,data)
	local target = self.room:getCurrent()
	if self:isFriend(target) then
		--锦囊不如杀重要的情况
		local trick = dummyCard("nullification")
		if target:hasSkill("wumou") or target:isJilei(trick) then return true end
		local slash = dummyCard()
		local dummy_use = dummy()
		dummy_use.from = target
		self:useBasicCard(slash,dummy_use)
		if target:getWeapon() and target:getWeapon():isKindOf("Crossbow") and not dummy_use.to:isEmpty() then return true end
		if target:hasSkills("paoxiao|tianyi|xianzhen|jiangchi|fuhun|qiangwu") and not self:isWeak(target) and not dummy_use.to:isEmpty() then return true end
	else
		local slash2 = dummyCard()
		if target:isJilei(slash2) then return true end
		if target:getWeapon() and target:getWeapon():isKindOf("blade") then return false end
		if target:hasSkills("paoxiao|tianyi|xianzhen|jiangchi|fuhun|qiangwu") or (target:getWeapon() and target:getWeapon():isKindOf("Crossbow")) then return false end
		if target:hasSkills("wumou|gongqi") then return false end
		if target:hasSkills("guose|qixi|duanliang|luanji") and target:getHandcardNum()>1 then return true end
		if target:hasSkills("shuangxiong") and not self:isWeak(target) then return true end
		if not self:slashIsEffective(slash2,self.player,target) and not self:isWeak() then return true end
		if self.player:getArmor() and self.player:getArmor():isKindOf("Vine") and not self:isWeak() then return true end
		if self.player:getArmor() and not self:isWeak() and self:getCardsNum("Jink")>0 then return true end
	end
	return false
end

sgs.ai_skill_choice.mumu = function(self,choices)
	local armorPlayersF = {}
	local weaponPlayersE = {}
	local armorPlayersE = {}

	for _,p in ipairs(self.friends_noself)do
		if p:getArmor() then
			table.insert(armorPlayersF,p)
		end
	end
	for _,p in ipairs(self.enemies)do
		if p:getWeapon() and self.player:canDiscard(p,p:getWeapon():getEffectiveId()) then
			table.insert(weaponPlayersE,p)
		end
		if p:getArmor() then
			table.insert(armorPlayersE,p)
		end
	end

	self.player:setFlags("mumu_armor")
	if #armorPlayersF>0 then
		for _,friend in ipairs(armorPlayersF)do
			if (friend:getArmor():isKindOf("Vine") and not self.player:getArmor() and not friend:hasSkills("kongcheng|zhiji"))
			or (friend:getArmor():isKindOf("SilverLion") and friend:getLostHp()>0) then
				return "armor"
			end
		end
	end

	if #armorPlayersE>0 then
		if not self.player:getArmor() then return "armor" end
		if self.player:getArmor() and self.player:getArmor():isKindOf("SilverLion") and self.player:getLostHp()>0 then return "armor" end
		for _,enemy in ipairs(armorPlayersE)do
			if enemy:getArmor():isKindOf("Vine") or self:isWeak(enemy) then
				return "armor"
			end
		end
	end

	self.player:setFlags("-mumu_armor")
	if #weaponPlayersE>0 then
		return "weapon"
	end
	self.player:setFlags("mumu_armor")
	if #armorPlayersE>0 then
		for _,enemy in ipairs(armorPlayersE)do
			if not enemy:getArmor():isKindOf("SilverLion") and enemy:getLostHp()>0 then
				return "armor"
			end
		end
	end
	self.player:setFlags("-mumu_armor")
end

sgs.ai_skill_playerchosen.mumu = function(self,targets)
	sgs.ai_skill_choice.mumu(self,{})
	if self.player:hasFlag("mumu_armor") then
		for _,target in sgs.qlist(targets)do
			if self:isFriend(target) and target:getArmor():isKindOf("SilverLion") and target:getLostHp()>0 then return target end
			if self:isEnemy(target) and target:getArmor():isKindOf("SilverLion") and target:getLostHp()==0 then return target end
		end
		for _,target in sgs.qlist(targets)do
			if self:isEnemy(target) and (self:isWeak(target) or target:getArmor():isKindOf("Vine")) then return target end
		end
		for _,target in sgs.qlist(targets)do
			if self:isEnemy(target) then return target end
		end
	else
		for _,target in sgs.qlist(targets)do
			if self:isEnemy(target) and target:hasSkills("liegong|qiangxi|jijiu|guidao|anjian") then return target end
		end
		for _,target in sgs.qlist(targets)do
			if self:isEnemy(target) then return target end
		end
	end
end

--马良
local xiemu_skill = {}
xiemu_skill.name = "xiemu"
table.insert(sgs.ai_skills,xiemu_skill)
xiemu_skill.getTurnUseCard = function(self)
	if self:getCardsNum("Slash")<1 then return end
	local kingdomDistribute = {}
	for k,p in sgs.qlist(self.room:getAlivePlayers())do
		k = p:getKingdom()
		kingdomDistribute[k] = kingdomDistribute[k] or 0
		if self:isEnemy(p) and p:inMyAttackRange(self.player)
		then kingdomDistribute[k] = kingdomDistribute[k]+1
		else kingdomDistribute[k] = kingdomDistribute[k]+0.2 end
		if p:hasSkill("luanji") and p:getHandcardNum()>2 then kingdomDistribute[k] = kingdomDistribute[k]+3 end
		if p:hasSkill("qixi") and self:isEnemy(p) and p:getHandcardNum()>2 then kingdomDistribute[k] = kingdomDistribute[k]+2 end
		if p:hasSkill("zaoxian") and self:isEnemy(p) and p:getPile("field"):length()>1 then kingdomDistribute[k] = kingdomDistribute[k]+2 end
	end
	local maxK,x = nil,-99
	for k,n in pairs(kingdomDistribute)do
		maxK = maxK or k
		if n>x
		then
			maxK = k
			x = n
		end
	end
	if kingdomDistribute[maxK]+self:getCardsNum("Slash")<4 then return end
	self.room:setTag("xiemu_choice",sgs.QVariant(maxK))
	for _,c in sgs.qlist(self.player:getHandcards())do
		if c:isKindOf("Slash") then return sgs.Card_Parse("@XiemuCard="..c:getEffectiveId()) end
	end
end

sgs.ai_skill_use_func.XiemuCard = function(card,use,self)
	use.card = card
end

sgs.ai_skill_choice.xiemu = function(self,choices)
	local choice = self.room:getTag("xiemu_choice"):toString()
	self.room:setTag("xiemu_choice",sgs.QVariant())
	return choice
end

sgs.ai_use_value.XiemuCard = 5
sgs.ai_use_priority.XiemuCard = 10

sgs.ai_skill_invoke.naman = function(self,data)
	if self:needKongcheng(self.player,true) and self.player:getHandcardNum()==0 then return false end
	return true
end

--chengyi

--黄巾雷使
sgs.ai_view_as.fulu = function(card,player,card_place)
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	if card_place~=sgs.Player_PlaceSpecial and card:getClassName()=="Slash" and not card:hasFlag("using") then
		return ("thunder_slash:fulu[%s:%s]=%d"):format(suit,number,card_id)
	end
end

sgs.ai_skill_invoke.fulu = function(self,data)
	local use = data:toCardUse()
	for _,player in sgs.qlist(use.to)do
		if self:isEnemy(player)
		and self:isGoodTarget(player,self.enemies,use.card) 
		then
			return true
		end
	end
	return false
end

local fulu_skill = {}
fulu_skill.name = "fulu"
table.insert(sgs.ai_skills,fulu_skill)
fulu_skill.getTurnUseCard = function(self,inclusive)
	local cards = self:addHandPile()
	local slash
	self:sortByUseValue(cards,true)
	for _,card in ipairs(cards)do
		if card:getClassName()=="Slash" then
			slash = card
			break
		end
	end
	if not slash then return nil end
	local dummy_use = dummy()
	self:useCardThunderSlash(slash,dummy_use)
	if dummy_use.card and dummy_use.to:length()>0 then
		local use = sgs.CardUseStruct()
		use.from = self.player
		use.to = dummy_use.to
		use.card = slash
		local data = sgs.QVariant()
		data:setValue(use)
		if not sgs.ai_skill_invoke.fulu(self,data) then return nil end
	else return nil end
	if slash then
		local suit = slash:getSuitString()
		local number = slash:getNumberString()
		local card_id = slash:getEffectiveId()
		local card_str = ("thunder_slash:fulu[%s:%s]=%d"):format(suit,number,card_id)
		local mySlash = sgs.Card_Parse(card_str)
		assert(mySlash)
		return mySlash
	end
end

sgs.ai_skill_invoke.zhuji = function(self,data)
	local damage = data:toDamage()
	if self:isFriend(damage.from) and not self:isFriend(damage.to) then return true end
	return false
end

--文聘
sgs.ai_skill_cardask["@sp_zhenwei"] = function(self,data)
	local use = data:toCardUse()
	if use.to:length()~=1 or not use.from or not use.card then return "." end
	if not self:isFriend(use.to:at(0)) or self:isFriend(use.from) then return "." end
	if use.to:at(0):hasSkills("liuli|tianxiang") and use.card:isKindOf("Slash") and use.to:at(0):getHandcardNum()>1 then return "." end
	if use.card:isKindOf("Slash") and not self:slashIsEffective(use.card,use.to:at(0),use.from) then return "." end
	if use.to:at(0):hasSkills(sgs.masochism_skill) and not self:isWeak(use.to:at(0)) then return "." end
	if self.player:getHandcardNum()+self.player:getEquips():length()<2 and not self:isWeak(use.to:at(0)) then return "." end
	local to_discard = self:askForDiscard("sp_zhenwei",1,1,false,true)
	if #to_discard>0 then
		if not (use.card:isKindOf("Slash") and  self:isWeak(use.to:at(0))) and sgs.Sanguosha:getCard(to_discard[1]):isKindOf("Peach") then return "." end
		return "$"..to_discard[1]
	else
		return "."
	end
end

sgs.ai_skill_choice.spzhenwei = function(self,choices,data)
	local use = data:toCardUse()
	if self:isWeak() or self.player:getHandcardNum()<2 then return "null" end
	if use.card:isKindOf("TrickCard") and use.from:hasSkill("jizhi") then return "draw" end
	if use.card:isKindOf("Slash") and (use.from:hasSkills("paoxiao|tianyi|xianzhen|jiangchi|fuhun|qiangwu")
		or (use.from:getWeapon() and use.from:getWeapon():isKindOf("Crossbow"))) and self:getCardsNum("Jink")==0 then return "null" end
	if use.card:isKindOf("SupplyShortage") then return "null" end
	if use.card:isKindOf("Slash") and self:getCardsNum("Jink")==0 and self.player:getLostHp()>0 then return "null" end
	if use.card:isKindOf("Indulgence") and self.player:getHandcardNum()+1>self.player:getHp() then return "null" end
	if use.card:isKindOf("Slash") and use.from:hasSkills("tieqi|wushuang|yijue|liegong|mengjin|qianxi") and not (use.from:getWeapon() and use.from:getWeapon():isKindOf("Crossbow")) then return "null" end
	return "draw"
end

--司马朗
local quji_skill = {}
quji_skill.name = "quji"
table.insert(sgs.ai_skills,quji_skill)
quji_skill.getTurnUseCard = function(self)
	if self.player:getHandcardNum()<self.player:getLostHp() then return nil end
	if self.player:getLostHp()==0 then return end

	local cards = self.player:getHandcards()
	cards = sgs.QList2Table(cards)

	local arr1,arr2 = self:getWoundedFriend(false,true)
	if #arr1+#arr2<self.player:getLostHp() then return end

	local compare_func = function(a,b)
		local v1 = self:getKeepValue(a)+( a:isBlack() and 50 or 0 )+( a:isKindOf("Peach") and 50 or 0 )
		local v2 = self:getKeepValue(b)+( b:isBlack() and 50 or 0 )+( b:isKindOf("Peach") and 50 or 0 )
		return v1<v2
	end
	table.sort(cards,compare_func)

	if cards[1]:isBlack() and self.player:getLostHp()>0 then return end
	if self.player:getLostHp()==2 and (cards[1]:isBlack() or cards[2]:isBlack()) then return end

	local card_str = "@QujiCard="..cards[1]:getId()
	local left = self.player:getLostHp()-1
	while left>0 do
		card_str = card_str.."+"..cards[self.player:getLostHp()+1-left]:getId()
		left = left-1
	end

	return sgs.Card_Parse(card_str)
end

sgs.ai_skill_use_func.QujiCard = function(card,use,self)
	local arr1,arr2 = self:getWoundedFriend(false,true)
	local target = nil
	local num = self.player:getLostHp()
	for num = 1,self.player:getLostHp()do
		if #arr1>num-1 and (self:isWeak(arr1[num]) or self:getOverflow()>=1) and arr1[num]:getHp()<getBestHp(arr1[num]) then target = arr1[num] end
		if target then
			use.to:append(target)
		else
			break
		end
	end

	if num<self.player:getLostHp() then
		if #arr2>0 then
			for _,friend in ipairs(arr2)do
				if not friend:hasSkills("hunzi|longhun") then
					use.to:append(friend)
					num = num+1
					if num==self.player:getLostHp() then break end
				end
			end
		end
	end
	use.card = card
	return
end

sgs.ai_use_priority.QujiCard = 4.2
sgs.ai_card_intention.QujiCard = -100
sgs.dynamic_value.benefit.QujiCard = true

sgs.quji_suit_value = {
	heart = 6,
	diamond = 6
}

sgs.ai_cardneed.quji = function(to,card)
	return card:isRed()
end

sgs.ai_skill_invoke.junbing = function(self,data)
	local simalang = self.room:findPlayerBySkillName("junbing")
	if self:isFriend(simalang) then return true end
	return false
end

--孙皓
sgs.ai_skill_invoke.canshi = function(self,data)
	local n = 0
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if p:isWounded() or (self.player:hasSkill("guiming") and self.player:isLord() and p:getKingdom()=="wu" and self.player:objectName()~=p:objectName()) then n = n+1 end
	end
	if n<=2 then return false end
	if n==3 and (not self:isWeak() or self:willSkipPlayPhase()) then return true end
	if n>3 then return true end
	return false
end

sgs.ai_card_intention.QingyiCard = sgs.ai_card_intention.Slash

--OL专属--

--李丰
--屯储
--player->askForSkillInvoke("tunchu")
sgs.ai_skill_invoke["tunchu"] = function(self,data)
	if #self.enemies==0 then
		return true
	end
	local callback = sgs.ai_skill_choice.jiangchi
	local choice = callback(self,"jiang+chi+cancel")
	if choice=="jiang" then
		return true
	end
	for _,friend in ipairs(self.friends_noself)do
		if (friend:getHandcardNum()<2 or (friend:hasSkill("rende") and friend:getHandcardNum()<3)) and choice=="cancel" then
		return true
		end
	end
	return false
end
--room->askForExchange(player,"tunchu",1,1,false,"@tunchu-put")
--输粮
--room->askForUseCard(p,"@@shuliang","@shuliang",-1,Card::MethodNone)
sgs.ai_skill_use["@@shuliang"] = function(self,prompt,method)
	local target = self.room:getCurrent()
	if target and self:isFriend(target) then
		return "@ShuliangCard="..self.player:getPile("food"):first()
	end
	return "."
end

--朱灵
--战意
--ZhanyiCard:Play
--ZhanyiViewAsBasicCard:Response
--ZhanyiViewAsBasicCard:Play
--room->askForDiscard(p,"zhanyi_equip",2,2,false,true,"@zhanyiequip_discard")
--room->askForChoice(zhuling,"zhanyi_slash",guhuo_list.join("+"))
--room->askForChoice(zhuling,"zhanyi_saveself",guhuo_list.join("+"))

local zhanyi_skill = {}
zhanyi_skill.name = "zhanyi"
table.insert(sgs.ai_skills,zhanyi_skill)
zhanyi_skill.getTurnUseCard = function(self)
	if not self.player:hasUsed("ZhanyiCard")
	then
		return sgs.Card_Parse("@ZhanyiCard=.")
	end
	if self.player:getMark("ViewAsSkill_zhanyiEffect")>0
	then
		local use_basic = self:ZhanyiUseBasic()
		local cards = self.player:getCards("h")
		cards = sgs.QList2Table(cards)
		self:sortByUseValue(cards,true)
		local BasicCards = {}
		for _,card in ipairs(cards)do
			if card:isKindOf("BasicCard")
			then
				table.insert(BasicCards,card)
			end
		end
		if use_basic and #BasicCards>0
		then
			return sgs.Card_Parse("@ZhanyiViewAsBasicCard="..BasicCards[1]:getId()..":"..use_basic)
		end
	end
end

sgs.ai_skill_use_func.ZhanyiCard = function(card,use,self)
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
				and self.player:canSlash(enemy) and self:slashIsEffective(dummyCard(),enemy,self.player)
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
	or (use_basic=="slash" and self.player:getHp()>1 and #BasicCards>1)
	then to_discard = BasicCards[1] end
	if to_discard then
		use.card = sgs.Card_Parse("@ZhanyiCard="..to_discard:getEffectiveId())
		return
	end
end

sgs.ai_use_priority.ZhanyiCard = 10

sgs.ai_skill_use_func.ZhanyiViewAsBasicCard=function(card,use,self)
	local userstring=card:toString()
	userstring=(userstring:split(":"))[3]
	local zhanyicard=dummyCard(userstring)
	zhanyicard:setSkillName("zhanyi")
	if zhanyicard:getTypeId()==sgs.Card_TypeBasic then
		if not use.isDummy and use.card and zhanyicard:isKindOf("Slash") and (not use.to or use.to:isEmpty()) then return end
		self:useBasicCard(zhanyicard,use)
	end
	if not use.card then return end
	use.card=card
end

sgs.ai_use_priority.ZhanyiViewAsBasicCard = 8

function SmartAI:ZhanyiUseBasic()
	local has_slash,has_peach
	local BasicCards = {}
	for _,card in sgs.qlist(self.player:getCards("h"))do
		if card:isKindOf("BasicCard")
		then
			table.insert(BasicCards,card)
			if card:isKindOf("Slash") then has_slash = true end
			if card:isKindOf("Peach") then has_peach = true end
		end
	end
	if #BasicCards<=1 then return end
	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if self:isWeak(enemy) and self.player:canSlash(enemy)
		and self:slashIsEffective(dummyCard(),enemy,self.player)
		then
			if not has_slash then return "slash" end
			break
		end
	end
	if self:isWeak() and not has_peach then return "peach" end
end

sgs.ai_skill_choice.zhanyi_saveself = function(self,choices)
	if self:getCard("Peach") or not self:getCard("Analeptic") then return "peach" else return "analeptic" end
end

sgs.ai_skill_choice.zhanyi_slash = function(self,choices)
	return "slash"
end

--马谡
--散谣
--SanyaoCard:Play
local sanyao_skill = {
	name = "sanyao",
	getTurnUseCard = function(self,inclusive)
		if self.player:canDiscard(self.player,"he") then
			return sgs.Card_Parse("@SanyaoCard=.")
		end
	end,
}
table.insert(sgs.ai_skills,sanyao_skill)
sgs.ai_skill_use_func["SanyaoCard"] = function(card,use,self)
	local alives = self.room:getAlivePlayers()
	local max_hp = -1000
	for _,p in sgs.qlist(alives)do
		local hp = p:getHp()
		if hp>max_hp then
			max_hp = hp
		end
	end
	local friends,enemies = {},{}
	for _,p in sgs.qlist(alives)do
		if p:getHp()==max_hp then
			if self:isFriend(p) then
				table.insert(friends,p)
			elseif self:isEnemy(p) then
				table.insert(enemies,p)
			end
		end
	end
	local target = nil
	if #enemies>0 then
		self:sort(enemies,"hp")
		for _,enemy in ipairs(enemies)do
			if self:damageIsEffective(enemy,sgs.DamageStruct_Normal,self.player) then
				if self:cantbeHurt(enemy,self.player)
				or self:needToLoseHp(enemy,self.player,false) then
				else
					target = enemy
					break
				end
			end
		end
	end
	if #friends>0 and not target then
		self:sort(friends,"hp")
		friends = sgs.reverse(friends)
		for _,friend in ipairs(friends)do
			if self:damageIsEffective(friend,sgs.DamageStruct_Normal,self.player) then
				if self:needToLoseHp(friend,self.player,false) then
				elseif friend:getCards("j"):length()>0 and self.player:hasSkill("zhiman") then
				elseif self:needToThrowArmor(friend) and self.player:hasSkill("zhiman") then
					target = friend
					break
				end
			end
		end
	end
	if target then
		local cost = self:askForDiscard("dummy",1,1,false,true)
		if #cost==1 then
			local acard = sgs.Card_Parse("@SanyaoCard="..cost[1])
			use.card = acard
			use.to:append(target)
		end
	end
end
sgs.ai_use_value["SanyaoCard"] = 1.75
sgs.ai_card_intention["SanyaoCard"] = function(self,card,from,tos)
	local target = tos[1]
	if getBestHp(target)>target:getHp() 
	or self:needToLoseHp(target,from,false)
	then return end
	sgs.updateIntention(from,target,30)
end
--制蛮
--player->askForSkillInvoke(this,data)
sgs.ai_skill_invoke["zhiman"] = function(self,data)
	local target = data:toPlayer()
	if not target then return end
	if self:isFriend(target) then
		if self:needToLoseHp(target,self.player)
		and (target:getJudgingArea():isEmpty() or target:containsTrick("YanxiaoCard"))
		then return false end
		return self:doDisCard(target,"ej")
	else
		if self:isWeak(target) then return false end
		if self:doDisCard(target,"e",true) then return true end
		return self:needToLoseHp(target,self.player)
		or self:getDangerousCard(target) or target:getDefensiveHorse()
	end
end
--room->askForCardChosen(player,damage.to,"ej",objectName())

--于禁
--节钺
--room->askForExchange(effect.to,"jieyue",1,1,true,QString("@jieyue_put:%1").arg(effect.from->objectName()),true)
sgs.ai_skill_discard["jieyue"] = function(self,discard_num,min_num,optional,include_equip)
	local source = self.room:getCurrent()
	if source and self:isEnemy(source) then
		return {}
	end
	return self:askForDiscard("dummy",discard_num,min_num,false,include_equip)
end
--room->askForCardChosen(effect.from,effect.to,"he",objectName(),false,Card::MethodDiscard)
--room->askForUseCard(player,"@@jieyue","@jieyue",-1,Card::MethodDiscard,false)
sgs.ai_skill_use["@@jieyue"] = function(self,prompt,method)
	if self.player:isKongcheng() then
		return "."
	elseif #self.enemies==0 then
		return "."
	end
	local handcards = self.player:getHandcards()
	handcards = sgs.QList2Table(handcards)
	self:sortByKeepValue(handcards)
	local to_use = nil
	local isWeak = self:isWeak()
	local isDanger = isWeak and ( self.player:getHp()+self:getAllPeachNum()<=1 )
	for _,card in ipairs(handcards)do
		if self.player:isJilei(card) then
		elseif card:isKindOf("Peach") or card:isKindOf("ExNihilo") then
		elseif isDanger and card:isKindOf("Analeptic") then
		elseif isWeak and card:isKindOf("Jink") then
		else
			to_use = card
			break
		end
	end
	if not to_use then
		return "."
	end
	if #self.friends_noself>0 then
		local has_black,has_red = false,false
		local need_null,need_jink = false,false
		for _,card in ipairs(handcards)do
			if card:getEffectiveId()~=to_use:getEffectiveId() then
				if card:isRed() then
					has_red = true
					break
				end
			end
		end
		for _,card in ipairs(handcards)do
			if card:getEffectiveId()~=to_use:getEffectiveId() then
				if card:isBlack() then
					has_black = true
					break
				end
			end
		end
		if has_black then
			local f_num = self:getCardsNum("Nullification","he",true)
			local e_num = 0
			for _,friend in ipairs(self.friends_noself)do
				f_num = f_num+getCardsNum("Nullification",friend,self.player)
			end
			for _,enemy in ipairs(self.enemies)do
				e_num = e_num+getCardsNum("Nullification",enemy,self.player)
			end
			if f_num<e_num then
				need_null = true
			end
		end
		if has_red and not need_null then
			if self:getCardsNum("Jink","he",false)==0 then
				need_jink = true
			else
				for _,friend in ipairs(self.friends_noself)do
					if getCardsNum("Jink",friend,self.player)==0 then
						if friend:hasLordSkill("hujia") and self.player:getKingdom()=="wei" then
							need_jink = true
							break
						elseif friend:hasSkill("lianli") and self.player:isMale() then
							need_jink = true
							break
						end
					end
				end
			end
		end
		if need_jink or need_null then
			self:sort(self.friends_noself,"defense")
			self.friends_noself = sgs.reverse(self.friends_noself)
			for _,friend in ipairs(self.friends_noself)do
				if not friend:isNude() then
					local card_str = "@JieyueCard="..to_use:getEffectiveId().."->"..friend:objectName()
					return card_str
				end
			end
		end
	end
	local target = self:findPlayerToDiscard("he",false,true)[1]
	if target then
		local card_str = "@JieyueCard="..to_use:getEffectiveId().."->"..target:objectName()
		return card_str
	end
	local targets = self:findPlayerToDiscard("he",false,false)
	for _,friend in ipairs(targets)do
		if not self:isEnemy(friend) then
			local card_str = "@JieyueCard="..to_use:getEffectiveId().."->"..friend:objectName()
			return card_str
		end
	end
	return "."
end
--jieyue:Response
sgs.ai_view_as["jieyue"] = function(card,player,card_place,class_name)
	if not player:getPile("jieyue_pile"):isEmpty() then
		if card_place==sgs.Player_PlaceHand then
			local suit = card:getSuitString()
			local point = card:getNumber()
			local id = card:getEffectiveId()
			if class_name=="Jink" and card:isRed() then
				return string.format("jink:jieyue[%s:%d]=%d",suit,point,id)
			elseif class_name=="Nullification" and card:isBlack() then
				return string.format("nullification:jieyue[%s:%d]=%d",suit,point,id)
			end
		end
	end
end

sgs.ai_skill_invoke.cv_sunshangxiang = function(self,data)
	local lord = self.room:getLord()
	if lord and lord:hasLordSkill("shichou") then
		return self:isFriend(lord)
	end
	return lord:getKingdom()=="shu"
end



sgs.ai_skill_invoke.cv_caiwenji = function(self,data)
	local lord = self.room:getLord()
	if lord and lord:hasLordSkill("xueyi") then
		return not self:isFriend(lord)
	end
	return lord:getKingdom()=="wei"
end



sgs.ai_skill_invoke.cv_machao = function(self,data)
	local lord = self.room:getLord()
	if lord and lord:hasLordSkill("xueyi") and self:isFriend(lord) then
		sgs.ai_skill_choice.cv_machao = "sp_machao"
		return true
	end
	if lord and lord:hasLordSkill("shichou") and not self:isFriend(lord) then
		sgs.ai_skill_choice.cv_machao = "sp_machao"
		return true
	end
	if lord and lord:getKingdom()=="qun" and not lord:hasLordSkill("xueyi") then
		sgs.ai_skill_choice.cv_machao = "sp_machao"
		return true
	end
	if math.random(0,2)==0 then
		sgs.ai_skill_choice.cv_machao = "tw_machao"
		return true
	end
end



sgs.ai_skill_invoke.cv_diaochan = function(self,data)
	if math.random(0,2)==0 then return false
	elseif math.random(0,3)==0 then sgs.ai_skill_choice.cv_diaochan = "tw_diaochan" return true
	elseif math.random(0,4)==0 then sgs.ai_skill_choice.cv_diaochan = "heg_diaochan" return true
	else sgs.ai_skill_choice.cv_diaochan = "sp_diaochan" return true end
end



sgs.ai_skill_invoke.cv_pangde = sgs.ai_skill_invoke.cv_caiwenji
sgs.ai_skill_invoke.cv_jiaxu = sgs.ai_skill_invoke.cv_caiwenji

sgs.ai_skill_invoke.cv_yuanshu = function(self,data)
	return math.random(0,2)==0
end

sgs.ai_skill_invoke.cv_zhaoyun = sgs.ai_skill_invoke.cv_yuanshu
sgs.ai_skill_invoke.cv_ganning = sgs.ai_skill_invoke.cv_yuanshu
sgs.ai_skill_invoke.cv_shenlvbu = sgs.ai_skill_invoke.cv_yuanshu

sgs.ai_skill_invoke.cv_daqiao = function(self,data)
	if math.random(0,3)>=1 then return false
	elseif math.random(0,4)==0 then sgs.ai_skill_choice.cv_daqiao = "tw_daqiao" return true
	else sgs.ai_skill_choice.cv_daqiao = "wz_daqiao" return true end
end

sgs.ai_skill_invoke.cv_xiaoqiao = function(self,data)
	if math.random(0,3)>=1 then return false
	elseif math.random(0,4)==0 then sgs.ai_skill_choice.cv_xiaoqiao = "wz_xiaoqiao" return true
	else sgs.ai_skill_choice.cv_xiaoqiao = "heg_xiaoqiao" return true end
end

sgs.ai_skill_invoke.cv_zhouyu = function(self,data)
	if math.random(0,3)>=1 then return false
	elseif math.random(0,4)==0 then sgs.ai_skill_choice.cv_zhouyu = "heg_zhouyu" return true
	else sgs.ai_skill_choice.cv_zhouyu = "sp_heg_zhouyu" return true end
end

sgs.ai_skill_invoke.cv_zhenji = function(self,data)
	if math.random(0,3)>=2 then return false
	elseif math.random(0,4)==0 then sgs.ai_skill_choice.cv_zhenji = "sp_zhenji" return true
	elseif math.random(0,5)==0 then sgs.ai_skill_choice.cv_zhenji = "tw_zhenji" return true
	else sgs.ai_skill_choice.cv_zhenji = "heg_zhenji" return true end
end

sgs.ai_skill_invoke.cv_lvbu = function(self,data)
	if math.random(0,3)>=1 then return false
	elseif math.random(0,4)==0 then sgs.ai_skill_choice.cv_lvbu = "tw_lvbu" return true
	else sgs.ai_skill_choice.cv_lvbu = "heg_lvbu" return true end
end

sgs.ai_skill_invoke.cv_zhangliao = sgs.ai_skill_invoke.cv_yuanshu
sgs.ai_skill_invoke.cv_luxun = sgs.ai_skill_invoke.cv_yuanshu

sgs.ai_skill_invoke.cv_huanggai = function(self,data)
	return math.random(0,4)==0
end

sgs.ai_skill_invoke.cv_guojia = sgs.ai_skill_invoke.cv_huanggai
sgs.ai_skill_invoke.cv_zhugeke = sgs.ai_skill_invoke.cv_huanggai
sgs.ai_skill_invoke.cv_yuejin = sgs.ai_skill_invoke.cv_huanggai
sgs.ai_skill_invoke.cv_madai = false --@todo: update after adding the avatars

sgs.ai_skill_invoke.cv_zhugejin = function(self,data)
	return math.random(0,4)>1
end

sgs.ai_skill_invoke.conqueror= function(self,data)
	local target = data:toPlayer()
	if self:isFriend(target) and not self:needToThrowArmor(target) then
	return false end
return true
end

sgs.ai_skill_choice.conqueror = function(self,choices,data)
	local target = data:toPlayer()
	if (self:isFriend(target) and not self:needToThrowArmor(target)) or (self:isEnemy(target) and target:getEquips():length()==0) then
	return "EquipCard" end
	local choice = {}
	table.insert(choice,"EquipCard")
	table.insert(choice,"TrickCard")
	table.insert(choice,"BasicCard")
	if (self:isEnemy(target) and not self:needToThrowArmor(target)) or (self:isFriend(target) and target:getEquips():length()==0) then
		table.removeOne(choice,"EquipCard")
		if #choice==1 then return choice[1] end
	end
	if (self:isEnemy(target) and target:getHandcardNum()<2) then
		table.removeOne(choice,"BasicCard")
		if #choice==1 then return choice[1] end
	end
	if (self:isEnemy(target) and target:getHandcardNum()>3) then
		table.removeOne(choice,"TrickCard")
		if #choice==1 then return choice[1] end
	end
	return choice[math.random(1,#choice)]
end

sgs.ai_skill_cardask["@conqueror"] = function(self,data)
	local has_card
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByUseValue(cards,true)
	for _,cd in ipairs(cards)do
		if self:getArmor("SilverLion") and card:isKindOf("SilverLion") then
			has_card = cd
			break
		end
		if not cd:isKindOf("Peach") and not card:isKindOf("Analeptic") and not (self:getArmor() and cd:objectName()==self.player:getArmor():objectName()) then
			has_card = cd
			break
		end
	end
	if has_card then
		return "$"..has_card:getEffectiveId()
	else
		return ".."
	end
end

sgs.ai_skill_playerchosen.fentian = function(self,targets)
	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if (self:doDisCard(enemy,"he") or self:getDangerousCard(enemy) or self:getValuableCard(enemy)) and not enemy:isNude() and self.player:inMyAttackRange(enemy) then
			return enemy
		end
	end
	for _,friend in ipairs(self.friends)do
		if(self:hasSkills(sgs.lose_equip_skill,friend) and not friend:getEquips():isEmpty())
		or (self:needToThrowArmor(friend) and friend:getArmor()) or self:doDisCard(friend,"he") and self.player:inMyAttackRange(friend) then
			return friend
		end
	end
	for _,enemy in ipairs(self.enemies)do
		if not enemy:isNude() and self.player:inMyAttackRange(enemy) then
			return enemy
		end
	end
	for _,friend in ipairs(self.friends)do
		if not friend:isNude() and self.player:inMyAttackRange(friend) then
			return friend
		end
	end
end

sgs.ai_playerchosen_intention.fentian = 20

local getXintanCard = function(pile)
	if #pile>1 then return pile[1],pile[2] end
	return nil
end

local xintan_skill = {}
xintan_skill.name = "xintan"
table.insert(sgs.ai_skills,xintan_skill)
xintan_skill.getTurnUseCard=function(self)
	local ints = sgs.QList2Table(self.player:getPile("burn"))
	local a,b = getXintanCard(ints)
	if a and b then
		return sgs.Card_Parse("@XintanCard="..a.."+"..b)
	end
end

sgs.ai_skill_use_func.XintanCard = function(card,use,self)
	local target
	self:sort(self.enemies,"hp")
	for _,enemy in ipairs(self.enemies)do
		if not self:needToLoseHp(enemy,self.player) and ((self:isWeak(enemy) or enemy:getHp()==1) or self.player:getPile("burn"):length()>3)  then
			target = enemy
		end
	end
	if not target then
		for _,friend in ipairs(self.friends)do
			if not self:needToLoseHp(friend,self.player) then
				target = friend
			end
		end
	end
	if target then
		use.card = card
		use.to:append(target)
		return
	end
end

sgs.ai_use_priority.XintanCard = 7
sgs.ai_use_value.XintanCard = 3
sgs.ai_card_intention.XintanCard = 80

sgs.ai_skill_use["@@shefu"] = function(self,data)
	local record
	for _,friend in ipairs(self.friends)do
		if self:isWeak(friend) then
			for _,enemy in ipairs(self.enemies)do
				if enemy:inMyAttackRange(friend) then
					if self.player:getMark("Shefu_slash")==0 then
						record = "slash"
					end
				end
			end
		end
	end
	if not record then
		for _,enemy in ipairs(self.enemies)do
			if self:isWeak(enemy) then
				for _,friend in ipairs(self.friends)do
					if friend:inMyAttackRange(enemy) then
						if self.player:getMark("Shefu_peach")==0 then
							record = "peach"
						elseif self.player:getMark("Shefu_jink")==0 then
							record = "jink"
						end
					end
				end
			end
		end
	end
	if not record then
		for _,enemy in ipairs(self.enemies)do
			if enemy:getHp()==1 then
				if self.player:getMark("Shefu_peach")==0 then
					record = "peach"
				end
			end
		end
	end
	if not record then
		for _,enemy in ipairs(self.enemies)do
			if getKnownCard(enemy,self.player,"ArcheryAttack",false)>0 or (enemy:hasSkill("luanji") and enemy:getHandcardNum()>3)
			and self.player:getMark("Shefu_archery_attack")==0 then
				record = "archery_attack"
			elseif getKnownCard(enemy,self.player,"SavageAssault",false)>0
			and self.player:getMark("Shefu_savage_assault")==0 then
				record = "savage_assault"
			elseif getKnownCard(enemy,self.player,"Indulgence",false)>0 or (enemy:hasSkills("guose|nosguose") and enemy:getHandcardNum()>2)
			and self.player:getMark("Shefu_indulgence")==0 then
				record = "indulgence"
			end
		end
	end
	for _,player in sgs.qlist(self.room:getAlivePlayers())do
		if player:containsTrick("lightning") and self:hasWizard(self.enemies) then
			if self.player:getMark("Shefu_lightning")==0 then
				record = "lightning"
			end
		end
	end
	if not record then
		if self.player:getMark("Shefu_slash")==0 then
			record = "slash"
		elseif self.player:getMark("Shefu_peach")==0 then
			record = "peach"
		end
	end

	local cards = sgs.QList2Table(self.player:getHandcards())
	local use_card
	self:sortByKeepValue(cards)
	for _,card in ipairs(cards)do
		if not card:isKindOf("Peach") and not (self:isWeak() and card:isKindOf("Jink"))then
			use_card = card
		end
	end
	if record and use_card then
		return "@ShefuCard="..use_card:getEffectiveId()..":"..record
	end
end

sgs.ai_skill_invoke.shefu_cancel = function(self)
	local data = self.room:getTag("ShefuData")
	local use = data:toCardUse()
	local from = use.from
	local to = use.to:first()
	if from and self:isEnemy(from) then
		if (use.card:isKindOf("Jink") and self:isWeak(from))
		or (use.card:isKindOf("Peach") and self:isWeak(from))
		or use.card:isKindOf("Indulgence")
		or use.card:isKindOf("ArcheryAttack") or use.card:isKindOf("SavageAssault") then
			return true
		end
	end
	if to and self:isFriend(to) then
		if (use.card:isKindOf("Slash") and self:isWeak(to))
		or use.card:isKindOf("Lightning") then
			return true
		end
	end
return false
end

sgs.ai_skill_invoke.benyu = function(self,data)
	return true
end

sgs.ai_skill_cardask["@@benyu"] = function(self,data)
	local damage = self.room:getTag("CurrentDamageStruct"):toDamage()
	if not damage.from or self.player:isKongcheng() or not self:isEnemy(damage.from) then return "." end

	local needcard_num = damage.from:getHandcardNum()+1
	local cards = self.player:getCards("he")
	local to_discard = {}
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	for _,card in ipairs(cards)do
		if not card:isKindOf("Peach") or damage.from:getHp()==1 then
			table.insert(to_discard,card:getEffectiveId())
			if #to_discard==needcard_num then break end
		end
	end

	if #to_discard==needcard_num then
		return "$"..table.concat(to_discard,"+")
	end

return "."
end

sgs.ai_can_damagehp.benyu = function(self,from,card,to)
	if from
	and to:getHp()+self:getAllPeachNum()-self:ajustDamage(from,to,1,card)>0
	and self:canLoseHp(from,card,to)
	then
		if from:getHandcardNum()-to:getHandcardNum()>1
		or to:getHandcardNum()-from:getHandcardNum()>0
		and self:isEnemy(from) and from:getHp()<2
		then return true end
	end
end

--凶镬
local xionghuo_skill = {}
xionghuo_skill.name = "xionghuo"
table.insert(sgs.ai_skills,xionghuo_skill)
xionghuo_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@XionghuoCard=.")
end

sgs.ai_skill_use_func.XionghuoCard = function(card,use,self)
	self:sort(self.enemies,"hp")
	local handcards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByUseValue(handcards,true)
	for _,c in ipairs(handcards)do
		if c:targetFixed() then
			if c:isKindOf("SavageAssault") then
				if self:getAoeValue(c)>0 then
					if self.room:findPlayerBySkillName("huoshou") then return end
					for _,enemy in ipairs(self.enemies)do
                        if enemy:getMark("&brutal")==0
						and getCardsNum("Slash",enemy,self.player)<1
						and self:aoeIsEffective(c,enemy,self.player)
						and not self:cantDamageMore(self.player,enemy)
						then
							use.card = card
							use.to:append(enemy)
							return
						end
					end
				end
			end
			if c:isKindOf("ArcheryAttack") then
				if self:getAoeValue(c)>0 then
					for _,enemy in ipairs(self.enemies)do
                        if enemy:getMark("&brutal")==0
						and getCardsNum("Jink",enemy,self.player)<1
						and self:aoeIsEffective(c,enemy,self.player)
						and not self:cantDamageMore(self.player,enemy)
						then
							use.card = card
							use.to:append(enemy)
							return
						end
					end
				end
			end
		else
			if c:isKindOf("Slash") and c:isAvailable(self.player) then
				local dummyuse = dummy()
				self:useBasicCard(c,dummyuse)
				if not dummyuse.to:isEmpty() then
					for _,p in sgs.qlist(dummyuse.to)do
                        if p:getMark("&brutal")==0
						and getCardsNum("Jink",p,self.player)<1
						and not self:cantDamageMore(self.player,p)
						then
							use.card = card
							use.to:append(p)
							return
						end
					end
				end
			end
			if c:isKindOf("TrickCard") then
				if (c:isKindOf("FireAttack") and self.player:getHandcardNum()>2) or c:isKindOf("Duel") then
					local dummyuse = dummy()
					self:useTrickCard(c,dummyuse)
					if not dummyuse.to:isEmpty() then
						for _,p in sgs.qlist(dummyuse.to)do
                            if p:getMark("&brutal")==0 and not self:cantDamageMore(self.player,p) then
								use.card = card
								use.to:append(p)
								return
							end
						end
					end
				end
			end
		end
	end
end

sgs.ai_use_priority.XionghuoCard = 7
sgs.ai_use_value.XionghuoCard = 7
sgs.ai_card_intention.XionghuoCard = 50

--真仪
sgs.ai_skill_invoke.zhenyi = function(self,data)
	local str = data:toString()
	if string.find(str,"flyuqing")
	then
		local damage = self.player:getTag("flyuqing"):toDamage()
		if damage and damage.from and damage.from:objectName()==self.player:objectName()
		then
            if damage.to and self:isEnemy(damage.to) and not self:cantDamageMore(self.player,damage.to)
			then
				if not self:damageIsEffective(damage.to,damage.nature,damage.from) then return false end
				if not self:isGoodChainTarget(damage.to,damage.card or damage.nature,damage.from,damage.damage+1) then return false end
				return true
			end
		end
	elseif string.find(str,"flziwei")
	then
		local judge = self.player:getTag("flziwei"):toJudge()
		if not judge then return false end
		if self:isFriend(judge.who) and judge:isGood(judge.card)
		or self:isEnemy(judge.who) and not judge:isGood(judge.card)
		then return false end
		local new_card = sgs.Sanguosha:getWrappedCard(judge.card:getId())
		new_card:setNumber(5)
		new_card:setSuit(sgs.Card_Spade)
		new_card:setModified(true)
		if self:isFriend(judge.who) and judge:isGood(new_card)
		or self:isEnemy(judge.who) and not judge:isGood(new_card)
		then
			sgs.ai_skill_choice.zhenyi = "spade"
			return true
		end
		new_card:setSuit(sgs.Card_Heart)
		if self:isFriend(judge.who) and judge:isGood(new_card)
		or self:isEnemy(judge.who) and not judge:isGood(new_card)
		then
			sgs.ai_skill_choice.zhenyi = "heart"
			return true
		end
	elseif string.find(str,"flgouchen")
	then
		return self:canDraw(self.player)
	end
	return false
end

sgs.ai_view_as.zhenyi = function(card,player,card_place)
	if player:getMark("@flhoutu")<=0 then return nil end
	for _,c in sgs.qlist(player:getCards("h"))do
		if c:isKindOf("Peach") and player:canUse(c) then return nil end
		if c:isKindOf("Analeptic") and player:canUse(c) then return nil end
	end
	for _,id in sgs.qlist(player:getHandPile())do
		local c = sgs.Sanguosha:getCard(id)
		if c:isKindOf("Peach") and player:canUse(c) then return nil end
		if c:isKindOf("Analeptic") and player:canUse(c) then return nil end
	end
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	if card_place==sgs.Player_PlaceHand or player:getHandPile():contains(card_id) then
		return ("peach:zhenyi[%s:%s]=%d"):format(suit,number,card_id)
	end
end

--点化
sgs.ai_skill_invoke.dianhua = true

--十周年真仪
sgs.ai_skill_invoke.tenyearzhenyi = function(self,data)
	local str = data:toString()
	if string.find(str,"flyuqing") then
		local damage = self.player:getTag("flyuqing_tenyear"):toDamage()
		if damage and damage.from and damage.from:objectName()==self.player:objectName() then
            if damage.to and self:isEnemy(damage.to) and not self:cantDamageMore(self.player,damage.to) then
				if damage.nature and not self:damageIsEffective(damage.to,damage.nature,damage.from) then return false end
				if not self:isGoodChainTarget(damage.to,damage.card or damage.nature,damage.from,damage.damage+1) then return false end
				return true
			end
		end
	elseif string.find(str,"flziwei") then
		local judge = self.player:getTag("flziwei_tenyear"):toJudge()
		if not judge then return false end
		local suit = judge.card:getSuit()
		if self:isFriend(judge.who) and judge:isGood(judge.card)
		or self:isEnemy(judge.who) and not judge:isGood(judge.card)
		then return false end
		local new_card = sgs.Sanguosha:getWrappedCard(judge.card:getId())
		new_card:setSkillName("tenyearzhenyi")
		new_card:setNumber(5)
		new_card:setSuit(sgs.Card_Spade)
		new_card:setModified(true)
		new_card:deleteLater()
		if self:isFriend(judge.who) and judge:isGood(new_card)
		or self:isEnemy(judge.who) and not judge:isGood(new_card)
		then sgs.ai_skill_choice.zhenyi = "spade" return true end
		new_card:setSuit(sgs.Card_Heart)
		if self:isFriend(judge.who) and judge:isGood(new_card)
		or self:isEnemy(judge.who) and not judge:isGood(new_card)
		then sgs.ai_skill_choice.zhenyi = "heart" return true end
	end
	return false
end

sgs.ai_view_as.tenyearzhenyi = function(card,player,card_place)
	local str = sgs.ai_view_as.zhenyi(card,player,card_place)
	if str then
		return string.gsub(str,"zhenyi","tenyearzhenyi")
	end
end

--连诛
local lianzhu_skill = {}
lianzhu_skill.name = "lianzhu"
table.insert(sgs.ai_skills,lianzhu_skill)
lianzhu_skill.getTurnUseCard = function(self,inclusive)
	if not self.player:isNude() then
		return sgs.Card_Parse("@LianzhuCard=.")
	end
end

sgs.ai_skill_use_func.LianzhuCard = function(card,use,self)
	self:sort(self.friends_noself)
	self:sort(self.enemies,"handcard")
	if self:needToThrowArmor() then
		if not self.player:getArmor():isBlack() or not self.player:hasSkill("xiahui") then
			for _,p in ipairs(self.friends_noself)do
				if self:needKongcheng(p,true) or hasManjuanEffect(p) then continue end
				use.card = sgs.Card_Parse("@LianzhuCard="..self.player:getArmor():getEffectiveId())
				use.to:append(p)
				return
			end
			for _,p in ipairs(self.friends_noself)do
				if self:needKongcheng(p,true) then continue end
				use.card = sgs.Card_Parse("@LianzhuCard="..self.player:getArmor():getEffectiveId())
				use.to:append(p)
				return
			end
			for _,p in ipairs(self.enemies)do
				if self:needKongcheng(p,true) or hasManjuanEffect(p) then
					use.card = sgs.Card_Parse("@LianzhuCard="..self.player:getArmor():getEffectiveId())
					use.to:append(p)
					return
				end
			end
		else
			for _,p in ipairs(self.enemies)do
				if self:needKongcheng(p,true) or hasManjuanEffect(p) then
					use.card = sgs.Card_Parse("@LianzhuCard="..self.player:getArmor():getEffectiveId())
					use.to:append(p) 
					return
				end
			end
			for _,p in ipairs(self.friends_noself)do
				if self:needKongcheng(p,true) or hasManjuanEffect(p,true) then continue end
				use.card = sgs.Card_Parse("@LianzhuCard="..self.player:getArmor():getEffectiveId())
				use.to:append(p) 
				return
			end
		end
	end
	local black,notblack = {},{}
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByUseValue(cards,true)
	for _,c in ipairs(cards)do
		if c:isBlack() then
			if not c:isKindOf("Analeptic") and not (c:isKindOf("TrickCard") and not c:isKindOf("Lightning")) then
				table.insert(black,c)
			end
		else
			table.insert(notblack,c)
		end
	end
	if self.player:hasSkill("xiahui") then
		if #black>0 then
			for _,p in ipairs(self.enemies)do
				if self:needKongcheng(p,true) and not hasManjuanEffect(p,true) then
					use.card = sgs.Card_Parse("@LianzhuCard="..black[1]:getEffectiveId())
					use.to:append(p) 
					return
				end
			end
			for _,p in ipairs(self.enemies)do
				if not hasManjuanEffect(p,true) then
					use.card = sgs.Card_Parse("@LianzhuCard="..black[1]:getEffectiveId())
					use.to:append(p) 
					return
				end
			end
		end
		if #notblack>0 and self:getOverflow()>0 then
			for _,p in ipairs(self.friends_noself)do
				if self:needKongcheng(p,true) or hasManjuanEffect(p) then continue end
				use.card = sgs.Card_Parse("@LianzhuCard="..notblack[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
			for _,p in ipairs(self.friends_noself)do
				if self:needKongcheng(p,true) then continue end
				use.card = sgs.Card_Parse("@LianzhuCard="..notblack[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
		end
	end
	if #black>0 then
		for _,p in ipairs(self.friends_noself)do
			if self:needKongcheng(p,true) or hasManjuanEffect(p) then continue end
			use.card = sgs.Card_Parse("@LianzhuCard="..black[1]:getEffectiveId())
			use.to:append(p) 
			return
		end
		for _,p in ipairs(self.friends_noself)do
			if self:needKongcheng(p,true) then continue end
			use.card = sgs.Card_Parse("@LianzhuCard="..black[1]:getEffectiveId())
			use.to:append(p) 
			return
		end
		for _,p in ipairs(self.enemies)do
			if self:needKongcheng(p,true) and not hasManjuanEffect(p,true) then
				use.card = sgs.Card_Parse("@LianzhuCard="..black[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
		end
		for _,p in ipairs(self.enemies)do
			if self:needKongcheng(p,true) or hasManjuanEffect(p,true) then
				use.card = sgs.Card_Parse("@LianzhuCard="..black[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
		end
	end
	if self:getOverflow()>0 then
		sgs.ai_use_priority.LianzhuCard = 0
		for _,p in ipairs(self.friends_noself)do
			if self:needKongcheng(p,true) or hasManjuanEffect(p) then continue end
			use.card = sgs.Card_Parse("@LianzhuCard="..cards[1]:getEffectiveId())
			use.to:append(p) 
			return
		end
		for _,p in ipairs(self.friends_noself)do
			if self:needKongcheng(p,true) then continue end
			use.card = sgs.Card_Parse("@LianzhuCard="..cards[1]:getEffectiveId())
			use.to:append(p) 
			return
		end
	end
end

sgs.ai_use_priority.LianzhuCard = 7
sgs.ai_use_value.LianzhuCard = 7

sgs.ai_card_intention.LianzhuCard = function(self,card,from,tos)
	if from:hasSkill("xiahui") then
		if card:isBlack() then
			for _,to in ipairs(tos)do
				if hasManjuanEffect(to,true) then continue end
				sgs.updateIntention(from,to,80)
			end
		else
			for _,to in ipairs(tos)do
				if self:needKongcheng(to,true) then
					sgs.updateIntention(from,to,80)
				else
					sgs.updateIntention(from,to,-80)
				end
			end
		end
	else
		for _,to in ipairs(tos)do
			if self:needKongcheng(to,true) then
				sgs.updateIntention(from,to,80)
			else
				sgs.updateIntention(from,to,-80)
			end
		end
	end
end

sgs.ai_skill_discard.lianzhu = function(self,discard_num,min_num,optional,include_equip)
	local from = self.player:getTag("LianzhuFrom"):toPlayer()
	if not from or from:isDead() or self:isFriend(from) then return {} end
	local num = 0
	for _,c in sgs.qlist(self.player:getCards("he"))do
		if self.player:isJilei(c)
		then else num = num+1 end
	end
	if num<2 then return {} end
	return self:askForDiscard("dummyreason",2,2,false,true)
end

local tenyearlianzhu = {}
tenyearlianzhu.name = "tenyearlianzhu"
table.insert(sgs.ai_skills,lianzhu_skill)
tenyearlianzhu.getTurnUseCard = function(self,inclusive)
	if not self.player:isNude() then
		return sgs.Card_Parse("@TenyearLianzhuCard=.")
	end
end

sgs.ai_skill_use_func.TenyearLianzhuCard = function(card,use,self)
	self:sort(self.friends_noself)
	self:sort(self.enemies,"handcard")
	if self:needToThrowArmor() then
		if not self.player:getArmor():isBlack() or not self.player:hasSkill("xiahui") then
			for _,p in ipairs(self.friends_noself)do
				if self:needKongcheng(p,true) or hasManjuanEffect(p) then continue end
				use.card = sgs.Card_Parse("@TenyearLianzhuCard="..self.player:getArmor():getEffectiveId())
				use.to:append(p) 
				return
			end
			for _,p in ipairs(self.friends_noself)do
				if self:needKongcheng(p,true) then continue end
				use.card = sgs.Card_Parse("@TenyearLianzhuCard="..self.player:getArmor():getEffectiveId())
				use.to:append(p) 
				return
			end
			for _,p in ipairs(self.enemies)do
				if self:needKongcheng(p,true) or hasManjuanEffect(p) then
					use.card = sgs.Card_Parse("@TenyearLianzhuCard="..self.player:getArmor():getEffectiveId())
					use.to:append(p)
					return
				end
			end
		else
			for _,p in ipairs(self.enemies)do
				if self:needKongcheng(p,true) or hasManjuanEffect(p) then
					use.card = sgs.Card_Parse("@TenyearLianzhuCard="..self.player:getArmor():getEffectiveId())
					use.to:append(p) 
					return
				end
			end
			for _,p in ipairs(self.friends_noself)do
				if self:needKongcheng(p,true) or hasManjuanEffect(p,true) then continue end
				use.card = sgs.Card_Parse("@TenyearLianzhuCard="..self.player:getArmor():getEffectiveId())
				use.to:append(p)
				return
			end
		end
	end
	local black,notblack = {},{}
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByUseValue(cards,true)
	for _,c in ipairs(cards)do
		if c:isBlack() then
			if not c:isKindOf("Analeptic") and not (c:isKindOf("TrickCard") and not c:isKindOf("Lightning")) then
				table.insert(black,c)
			end
		else
			table.insert(notblack,c)
		end
	end
	if self.player:hasSkill("xiahui") then
		if #black>0 then
			for _,p in ipairs(self.enemies)do
				if self:needKongcheng(p,true) and not hasManjuanEffect(p,true) then
					use.card = sgs.Card_Parse("@TenyearLianzhuCard="..black[1]:getEffectiveId())
					use.to:append(p) 
					return
				end
			end
			for _,p in ipairs(self.enemies)do
				if not hasManjuanEffect(p,true) then
					use.card = sgs.Card_Parse("@TenyearLianzhuCard="..black[1]:getEffectiveId())
					use.to:append(p) 
					return
				end
			end
		end
		if #notblack>0 and self:getOverflow()>0 then
			for _,p in ipairs(self.friends_noself)do
				if self:needKongcheng(p,true) or hasManjuanEffect(p) then continue end
				use.card = sgs.Card_Parse("@TenyearLianzhuCard="..notblack[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
			for _,p in ipairs(self.friends_noself)do
				if self:needKongcheng(p,true) then continue end
				use.card = sgs.Card_Parse("@TenyearLianzhuCard="..notblack[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
		end
	end
	if #black>0 then
		for _,p in ipairs(self.friends_noself)do
			if self:needKongcheng(p,true) or hasManjuanEffect(p) then continue end
			use.card = sgs.Card_Parse("@TenyearLianzhuCard="..black[1]:getEffectiveId())
			use.to:append(p)
			return
		end
		for _,p in ipairs(self.friends_noself)do
			if self:needKongcheng(p,true) then continue end
			use.card = sgs.Card_Parse("@TenyearLianzhuCard="..black[1]:getEffectiveId())
			use.to:append(p) 
			return
		end
		for _,p in ipairs(self.enemies)do
			if self:needKongcheng(p,true) and not hasManjuanEffect(p,true) then
				use.card = sgs.Card_Parse("@TenyearLianzhuCard="..black[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
		end
		for _,p in ipairs(self.enemies)do
			if self:needKongcheng(p,true) or hasManjuanEffect(p,true) then
				use.card = sgs.Card_Parse("@TenyearLianzhuCard="..black[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
		end
	end
	if self:getOverflow()>0 then
		sgs.ai_use_priority.TenyearLianzhuCard = 0
		for _,p in ipairs(self.friends_noself)do
			if self:needKongcheng(p,true) or hasManjuanEffect(p) then continue end
			use.card = sgs.Card_Parse("@TenyearLianzhuCard="..cards[1]:getEffectiveId())
			use.to:append(p) 
			return
		end
		for _,p in ipairs(self.friends_noself)do
			if self:needKongcheng(p,true) then continue end
			use.card = sgs.Card_Parse("@TenyearLianzhuCard="..cards[1]:getEffectiveId())
			use.to:append(p) 
			return
		end
	end
end

sgs.ai_use_priority.TenyearLianzhuCard = 7
sgs.ai_use_value.TenyearLianzhuCard = 7

sgs.ai_card_intention.TenyearLianzhuCard = function(self,card,from,tos)
	if from:hasSkill("xiahui") then
		if card:isBlack() then
			for _,to in ipairs(tos)do
				if hasManjuanEffect(to,true) then continue end
				sgs.updateIntention(from,to,80)
			end
		else
			for _,to in ipairs(tos)do
				if self:needKongcheng(to,true) then
					sgs.updateIntention(from,to,80)
				else
					sgs.updateIntention(from,to,-80)
				end
			end
		end
	else
		for _,to in ipairs(tos)do
			if self:needKongcheng(to,true) then
				sgs.updateIntention(from,to,80)
			else
				sgs.updateIntention(from,to,-80)
			end
		end
	end
end

sgs.ai_skill_discard.tenyearlianzhu = function(self,discard_num,min_num,optional,include_equip)
	local from = self.player:getTag("LianzhuFrom"):toPlayer()
	if not from or from:isDead() or self:isFriend(from) then return {} end
	local num = 0
	for _,c in sgs.qlist(self.player:getCards("he"))do
		if self.player:isJilei(c)
		then else num = num+1 end
	end
	if num<2 then return {} end
	return self:askForDiscard("dummyreason",2,2,false,true)
end

--纵傀
sgs.ai_skill_playerchosen.zongkui = function(self,targets)
	if self.room:getLord() and self.player:getRole()=="rebel" and targets:contains(self.room:getLord()) then
		return self.room:getLord()
	end
	targets = sgs.QList2Table(targets)
	self:sort(targets,"defense")
	return targets[1]
end

--蚕食
sgs.ai_skill_invoke.spcanshi = function(self,data)
	local use = self.player:getTag("SPCanshi"):toCardUse()
	if not use then return false end
	if use.from and self:isFriend(use.from) then return false end
	if use.card:isKindOf("ExNihilo") or use.card:isKindOf("Peach") or use.card:isKindOf("Analeptic") or use.card:isKindOf("EquipCard") or use.card:isKindOf("Nullification") or
		(use.card:isKindOf("Slash") and (self:getCardsNum("Jink")>0) or not self:slashIsEffective(use.card,self.player,use.from)) or
		(use.card:isKindOf("FireAttack") and use.from:getHandcardNum()<4) or (use.card:isKindOf("TrickCard") and not self:hasTrickEffective(use.card,self.player,use.from)) then
		return false
	end
	return true
end

sgs.ai_skill_use["@@spcanshi"] = function(self,prompt,method)
	local use = self.player:getTag("SPCanshiForAI"):toCardUse()
	if not use then return "." end
	
	local tos = {}
	if use.card:targetFixed() then
		if use.card:isKindOf("Analeptic") then return "." end
		if use.card:isKindOf("Peach") then
			for _,p in ipairs(self.friends_noself)do
				if p:getLostHp()>0 and p:getMark("&kui")>0 then
					table.insert(tos,p:objectName())
				end
			end
		elseif use.card:isKindOf("ExNihilo") then
			for _,p in ipairs(self.friends_noself)do
				if self:canDraw(p) and p:getMark("&kui")>0 then
					table.insert(tos,p:objectName())
				end
			end
		end
		if #tos>0 then
			return "@SpCanshiCard=.->"..table.concat(tos,"+")
		end
		return "."
	end
	
	local dummy_use = dummy()
	self.room:setCardFlag(use.card,"spcanshi_distance")
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if p:getMark("&kui")<=0 or use.to:contains(p) then
			table.insert(dummy_use.current_targets,p)
		end
	end
	self:useCardByClassName(use.card,dummy_use)
	self.room:setCardFlag(use.card,"-spcanshi_distance")
	if dummy_use.card and dummy_use.to:length()>0 then
		for _,p in sgs.qlist(dummy_use.to)do
			table.insert(tos,p:objectName())
		end
		return "@SpCanshiCard=.->"..table.concat(tos,"+")
	end
	return "."
end

--征南
sgs.ai_skill_invoke.zhengnan = function(self,data)
	if self.player:hasSkill("wusheng",true) and self.player:hasSkill("dangxian",true) and self.player:hasSkill("zhiman",true) then
		return self:canDraw(self.player)
	end
	return true
end

function zhengnanSkill(self,choices)
	local skills = choices:split("+")
	if self.player:containsTrick("indulgence") then
		if table.contains(skills,"dangxian") then
			return "dangxian"
		end
	end
	if table.contains(skills,"wusheng") then
		return "wusheng"
	end
	if table.contains(skills,"dangxian") then
		return "dangxian"
	end
	if table.contains(skills,"zhiman") then
		return "zhiman"
	end
	return nil
end

sgs.ai_skill_choice.zhengnan = function(self,choices,data)
	if self:canDraw(self.player)
	then
		if self:isWeak()
		or self:getOverflow()<=-3
		then return "draw" end
	end
	local skill = zhengnanSkill(self,choices)
	if skill then return skill end
	return "draw"
end

---OL征南
sgs.ai_skill_invoke.olzhengnan = function(self,data)
	return self:canDraw(self.player)
end

sgs.ai_skill_choice.olzhengnan = function(self,choices,data)
	local skill = zhengnanSkill(self,choices)
	if skill then return skill end
	return choices:split("+")[1]
end

--十周年征南
sgs.ai_skill_invoke.tenyearzhengnan = function(self,data)
	return self:isWeak() or self:canDraw(self.player)
end

sgs.ai_skill_choice.tenyearzhengnan = function(self,choices,data)
	local skill = zhengnanSkill(self,choices)
	if skill then return skill end
	return choices:split("+")[1]
end

--芳魂
local fanghun_skill = {}
fanghun_skill.name = "fanghun"
table.insert(sgs.ai_skills,fanghun_skill)
fanghun_skill.getTurnUseCard = function(self)
	local handcards = self:addHandPile()
	if #handcards<1 then return end
	handcards = self:sortByUseValue(handcards,true)
	for _,c in sgs.list(handcards)do
		local slash = dummyCard()
		slash:setSkillName("_longdan")
		slash:addSubcard(c)
		if c:isKindOf("Jink")
		and slash:isAvailable(self.player)
		then
			return sgs.Card_Parse("@FanghunCard="..c:getEffectiveId()..":slash")
		end
	end
end

sgs.ai_skill_use_func.FanghunCard = function(card,use,self)
	local slash = dummyCard()
	slash:setSkillName("_longdan")
	slash:addSubcards(card:getSubcards())
	local dummy = self:aiUseCard(slash)
	if dummy.card then
		use.card = card
		use.to = dummy.to
	end
end

sgs.ai_cardsview.fanghun = function(self,class_name,player)
	local handcards = self:addHandPile()
	handcards = self:sortByKeepValue(handcards,true)
	for _,c in sgs.list(handcards)do
		if class_name=="Slash" and c:isKindOf("Jink")
		then return ("@FanghunCard="..c:getEffectiveId()..":slash")
		elseif class_name=="Jink" and c:isKindOf("Slash")
		then return ("@FanghunCard="..c:getEffectiveId()..":jink") end
	end
end

sgs.ai_use_priority.FanghunCard = sgs.ai_use_priority.Slash+0.1
sgs.ai_use_value.FanghunCard = sgs.ai_use_value.Slash+0.1

sgs.ai_need_damaged.fanghun = function(self,from,to,card)
	if card and card:isKindOf("Slash") and self:ajustDamage(from,to,1,card)==1
	then return to:getMark("&meiying")<2 and not self:isWeak() end
end

--扶汉
sgs.ai_skill_invoke.fuhan = function(self,data)
	local meiying = self.player:getMark("meiying")+self.player:getMark("&meiying")
	if self.player:getLostHp()>0 then
		return meiying>self.player:getHp() and self.player:isLowestHpPlayer() and self:getCardsNum("Peach")==0 and self:isWeak()
	else
		return meiying>self.player:getMaxHp() and self.player:getMark("&meiying")<2
	end
	return false
end

--OL芳魂
local olfanghun_skill = {}
olfanghun_skill.name = "olfanghun"
table.insert(sgs.ai_skills,olfanghun_skill)
olfanghun_skill.getTurnUseCard = function(self,inclusive)
	local str = canAiSkills("fanghun")
	if str
	then
		str = str.ai_fill_skill(self,inclusive)
		if str
		then
			str = str:toString()
			str = string.gsub(str,"FanghunCard","OLFanghunCard")
			return sgs.Card_Parse(str)
		end
	end
end

sgs.ai_skill_use_func.OLFanghunCard = function(card,use,self)
	return sgs.ai_skill_use_func.FanghunCard(card,use,self)
end

sgs.ai_cardsview.olfanghun = function(self,class_name,player)
	local card_str = sgs.ai_cardsview.fanghun(self,class_name,player)
	if card_str then return string.gsub(card_str,"FanghunCard","OLFanghunCard") end
end

sgs.ai_use_priority.OLFanghunCard = sgs.ai_use_priority.FanghunCard
sgs.ai_use_value.OLFanghunCard = sgs.ai_use_priority.FanghunCard

sgs.ai_need_damaged.olfanghun = function(self,from,to,card)
	if card and card:isKindOf("Slash")
	and self:ajustDamage(from,to,1,card)==1 then
		return to:getMark("&meiying")<2 and not self:isWeak()
	end
end

--OL扶汉
sgs.ai_skill_invoke.olfuhan = function(self,data)
	local meiying = self.player:getMark("meiying")+self.player:getMark("&meiying")
	meiying = math.max(2,meiying)
	meiying = math.min(8,meiying)
	if self.player:getLostHp()>0 then
		return meiying>self.player:getHp() and self:getCardsNum("Peach")==0 and self:isWeak()
	else
		return meiying>self.player:getMaxHp() and self.player:getMark("&meiying")<2
	end
	return false
end

--手杀芳魂
local mobilefanghun_skill = {}
mobilefanghun_skill.name = "mobilefanghun"
table.insert(sgs.ai_skills,mobilefanghun_skill)
mobilefanghun_skill.getTurnUseCard = function(self,inclusive)
	local str = canAiSkills("fanghun")
	if str
	then
		str = str.ai_fill_skill(self,inclusive)
		if str
		then
			str = str:toString()
			str = string.gsub(str,"FanghunCard","MobileFanghunCard")
			return sgs.Card_Parse(str)
		end
	end
end

sgs.ai_skill_use_func.MobileFanghunCard = function(card,use,self)
	return sgs.ai_skill_use_func.FanghunCard(card,use,self)
end

sgs.ai_cardsview.mobilefanghun = function(self,class_name,player)
	local card_str = sgs.ai_cardsview.fanghun(self,class_name,player)
	if card_str then return string.gsub(card_str,"FanghunCard","MobileFanghunCard") end
end

sgs.ai_use_priority.MobileFanghunCard = sgs.ai_use_priority.FanghunCard
sgs.ai_use_value.MobileFanghunCard = sgs.ai_use_priority.FanghunCard

sgs.ai_need_damaged.mobilefanghun = function(self,from,to,card)
	if card and card:isKindOf("Slash")
	and self:ajustDamage(from,to,1,card)==1 then
		return to:getMark("&meiying")<2 and not self:isWeak()
	end
end

--手杀扶汉
sgs.ai_skill_invoke.mobilefuhan = function(self,data)
	local meiying = self.player:getMark("meiying")+self.player:getMark("&meiying")
	meiying = math.min(meiying,self.room:getAllPlayers(true):length())
	if self.player:getLostHp()>0
	then
		return meiying>self.player:getHp() and self.player:isLowestHpPlayer() and self:getCardsNum("Peach")==0 and self:isWeak()
	else
		return meiying>self.player:getMaxHp() and self.player:getMark("&meiying")<2
	end
	return false
end

--十周年芳魂
local tenyearfanghun_skill = {}
tenyearfanghun_skill.name = "tenyearfanghun"
table.insert(sgs.ai_skills,tenyearfanghun_skill)
tenyearfanghun_skill.getTurnUseCard = function(self,inclusive)
	local str = canAiSkills("fanghun")
	if str then
		str = str.ai_fill_skill(self,inclusive)
		if str then
			str = str:toString()
			str = string.gsub(str,"FanghunCard","TenyearFanghunCard")
			return sgs.Card_Parse(str)
		end
	end
end

sgs.ai_skill_use_func.TenyearFanghunCard = function(card,use,self)
	return sgs.ai_skill_use_func.FanghunCard(card,use,self)
end

sgs.ai_cardsview.tenyearfanghun = function(self,class_name,player)
	local card_str = sgs.ai_cardsview.fanghun(self,class_name,player)
	if card_str then return string.gsub(card_str,"FanghunCard","TenyearFanghunCard") end
end

sgs.ai_use_priority.TenyearFanghunCard = sgs.ai_use_priority.FanghunCard
sgs.ai_use_value.TenyearFanghunCard = sgs.ai_use_priority.FanghunCard

sgs.ai_need_damaged.tenyearfanghun = function(self,from,to,card)
	if card and card:isKindOf("Slash")
	and self:ajustDamage(from,to,1,card)==1 then
		return to:getMark("&meiying")<2 and not self:isWeak()
	end
end

--十周年扶汉
sgs.ai_skill_invoke.tenyearfuhan = function(self,data)
	if data:toString()=="getskill" or data:toString()=="continue" then return true end
	if not self:isWeak() then
		return self:canDraw() and self.player:getMark("&meiying")>=2 and not self:willSkipPlayPhase()
	else
		return self:canDraw() and not self:willSkipPlayPhase()
	end
	return false
end

--武娘
sgs.ai_skill_playerchosen.wuniang = function(self,targets)
	local enemy,friend = 0,0
	for _,p in sgs.qlist(self.room:getAllPlayers())do
		if string.find(p:getGeneralName(),"guansuo") or string.find(p:getGeneral2Name(),"guansuo") then
			if self:isFriend(p) then
				friend = friend+1
			elseif self:isEnemy(p) then
				enemy = enemy+1
			end
		end
	end
	if enemy>friend then return nil end
	return self:findPlayerToDiscard("he",false,false,targets)[1]
end

--许身
sgs.ai_skill_invoke.xushen = function(self,data)
	local player = data:toPlayer()
	if self:isEnemy(player) and player:hasSkill("zhennan",true) and player:getLostHp()<=0 then return true end
	if self:isEnemy(player) then return false end
	return player:getLostHp()>0 or player:getMaxHp()<=1
end

--镇南
sgs.ai_skill_playerchosen.zhennan = function(self,targets)
	return self:findPlayerToDamage(2,self.player,nil,targets)[1]
end

--姝勇
sgs.ai_skill_playerchosen.shuyong = function(self,targets)
	return self:findPlayerToDiscard("hej",false,false,targets)[1]
end

--手杀许身
local mobilexushen_skill = {}
mobilexushen_skill.name = "mobilexushen"
table.insert(sgs.ai_skills,mobilexushen_skill)
mobilexushen_skill.getTurnUseCard = function(self,inclusive)
	local male = 0
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if p:isMale() then male = male+1 end
	end
	if male==0 or self.player:getHp()>male or male>self:getCardsNum("Peach")+self:getCardsNum("Analeptic") then return end
	return sgs.Card_Parse("@MobileXushenCard=.")
end

sgs.ai_skill_use_func.MobileXushenCard = function(card,use,self)
	use.card = card
end

sgs.ai_skill_invoke.mobilexushen = function(self,data)
	local saver = data:toString():split(":")[2]
	if saver then
		saver = self.room:findPlayerByObjectName(saver)
	end
	if not saver or saver:isDead() then return false end
	return self:isFriend(saver)
end

sgs.ai_use_priority.MobileXushenCard = 7
sgs.ai_use_value.MobileXushenCard = 7
sgs.ai_card_intention.MobileXushenCard = 50

--手杀镇南
sgs.ai_skill_cardask["@mobolezhennan-discard"] = function(self,data)
	local use = data:toCardUse()
	if not self:isEnemy(use.from) then return "." end
	local to_discard = self:askForDiscard("dummy",1,1,false,true)
	if #to_discard>0 then return "$"..to_discard[1] else return "." end
end

--十周年许身
sgs.ai_skill_invoke.tenyearxushen = function(self,data)
	local saver = data:toPlayer()
	return self:isFriend(saver)
end

sgs.ai_skill_invoke.tenyearxushenChange = true

--十周年镇南
sgs.ai_skill_playerchosen.tenyearzhennan = function(self,targets)
	return self:findPlayerToDamage(1,self.player,"N",targets)[1]
end

--十周年二版武娘
sgs.ai_skill_playerchosen.wuniang = function(self,targets)
	if not self.player:getTag("secondxushen_used"):toBool()
	then return self:findPlayerToDiscard("he",false,false,targets)[1] end
	local enemy,friend = 0,0
	for _,p in sgs.qlist(self.room:getAllPlayers())do
		if string.find(p:getGeneralName(),"guansuo") or string.find(p:getGeneral2Name(),"guansuo") then
			if self:isFriend(p) then
				friend = friend+1
			elseif self:isEnemy(p) then
				enemy = enemy+1
			end
		end
	end
	if enemy>friend then return nil end
	return self:findPlayerToDiscard("he",false,false,targets)[1]
end

--十周年二版许身
sgs.ai_skill_invoke.secondxushen = true

sgs.ai_skill_playerchosen.secondxushen = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"handcard")
	for _,p in ipairs(targets)do
		if self:isFriend(p) and self:canDraw(p) then return p end
	end
	return nil
end

--十周年二版镇南
sgs.ai_skill_playerchosen.secondzhennan = function(self,targets)
	return self:findPlayerToDamage(1,self.player,"N",targets)[1]
end

--OL武娘
sgs.ai_skill_invoke.olwuniang = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isEnemy(target)
		or self:getCardsNum("Jink")>0
		or target:getHandcardNum()<3
	end
end

--OL许身
sgs.ai_skill_invoke.olxushen = function(self,data)
    return true
end

sgs.ai_skill_playerchosen.olxushen = function(self,players)
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

sgs.ai_skill_invoke.tenyearxushenChange = function(self,data)
    return math.random()>0.4
end

--OL镇南

--凌人
sgs.ai_skill_playerchosen.lingren = function(self,targets)
	local targets = sgs.QList2Table(targets)
	self:sort(targets,"handcard")
	for _,p in ipairs(targets)do
		if self:isEnemy(p) then
			return p
		end
	end
	return nil
end

sgs.ai_skill_choice.lingren = function(self,choices,data)  --就写个全作弊ai吧
	local target = data:toPlayer()
	local guess = choices:split("+")
	if target:isKongcheng() then return guess[2] end
	
	local name = "BasicCard"
	if guess[1]=="hasbasic" then
	elseif guess[1]=="hastrick" then
		name = "TrickCard"
	else
		name = "EquipCard"
	end
	
	for _,c in sgs.qlist(target:getCards("h"))do
		if c:isKindOf(name) then
			return guess[1]
		end
	end
	return guess[math.random(1,#guess)]
end

--缮甲
sgs.ai_skill_invoke.shanjia = true

sgs.ai_skill_discard.shanjia = function(self,discard_num,min_num,optional,include_equip)
	local cards = self.player:getCards("he")
	cards = sgs.QList2Table(cards)
	self:sortByKeepValue(cards)
	local dis = {}
	for i = 1,math.min(min_num,#cards)do
		table.insert(dis,cards[i]:getEffectiveId())
	end
	return dis
end

sgs.ai_skill_use["@@shanjia"] = function(self,prompt,method)
	self:sort(self.enemies,"defense")
	local slash = dummyCard()
	slash:setSkillName("_shanjia")
	local dummyuse = dummy()
	self:useBasicCard(slash,dummyuse)
	local targets = {}
	if not dummyuse.to:isEmpty() then
		for _,p in sgs.qlist(dummyuse.to)do
			table.insert(targets,p:objectName())
		end
	end
	if #targets>0 then
		return "@ShanjiaCard=.->"..table.concat(targets)
	end
	return "."
end

sgs.shanjia_keep_value = {
    Peach = 6,
    Jink = 5.1,
    EquipCard = 4.7,
}

--OL缮甲
sgs.ai_skill_invoke.olshanjia = true

sgs.ai_skill_discard.olshanjia = function(self,discard_num,min_num,optional,include_equip)
	return sgs.ai_skill_discard.shanjia(self,discard_num,min_num,optional,include_equip)
end

sgs.ai_skill_use["@@olshanjia"] = function(self,prompt,method)
	local str = sgs.ai_skill_use["@@shanjia"](self,prompt,method)
	if not str or str=="" or str=="." then return "." end
	return string.gsub(str,"ShanjiaCard","OLShanjiaCard")
end

sgs.olshanjia_keep_value = sgs.shanjia_keep_value

--先辅
function getXianfuTarget(self,targets,RolePredictable)
	if not RolePredictable then
		for _,p in sgs.qlist(targets)do
			if p:hasSkill("shibei") then return p end
		end
		for _,p in sgs.qlist(targets)do
			if p:hasSkills(sgs.recover_skill) then return p end
		end
	else
		for _,p in sgs.qlist(targets)do
			if self.player:isYourFriend(p) and p:hasSkill("shibei") then return p end
		end
		for _,p in sgs.qlist(targets)do
			if self.player:isYourFriend(p) and p:hasSkills(sgs.recover_skill) then return p end
		end
	end
	return nil
end

sgs.ai_skill_playerchosen.xianfu = function(self,targets)
	if self.player:getRole()=="loyalist" and self.room:getLord() then
		return self.room:getLord()
	end
	
	if self.player:getRole()=="rebel" then
		local new_targets = sgs.SPlayerList()
		for _,p in sgs.qlist(targets)do
			if p:isLord() then continue end
			new_targets:append(p)
		end
		if not new_targets:isEmpty() then
			local target = getXianfuTarget(self,new_targets,isRolePredictable())
			if target then return target end
			return new_targets:at(math.random(0,new_targets:length() -1 ))
		end
	end
	
	local target = getXianfuTarget(self,targets,isRolePredictable())
	if target then return target end
	
	return targets:at(math.random(0,targets:length() -1 ))
end

--筹策
sgs.ai_skill_invoke.chouce = true

sgs.ai_can_damagehp.chouce = function(self,from,card,to)
	return to:getHp()+self:getAllPeachNum()-self:ajustDamage(from,to,1,card)>0
	and self:canLoseHp(from,card,to)
end

sgs.ai_skill_playerchosen.chouce = function(self,targets)
	local target = self:findPlayerToDiscard("hej",true,true,targets)
	if target then return target end
	for _,enemy in ipairs(self.enemies)do
		if (self:doDisCard(enemy) or self:getDangerousCard(enemy) or self:getValuableCard(enemy)) and self.player:canDiscard(enemy,"he") then
			return enemy
		end
	end
	for _,friend in ipairs(self.friends)do
		if (self:needToThrowArmor(friend) and friend:getArmor()) or (self:hasSkills(sgs.lose_equip_skill,friend) and self.player:canDiscard(friend,"e"))
		or self:doDisCard(friend) then
			return friend
		end
	end
	for _,p in sgs.list(targets)do
		if not self:isFriend(p) then
			return p
		end
	end
end

sgs.ai_skill_playerchosen.chouce_draw = function(self,targets)
	local xianfu = self.player:getTag("XianfuTarget"):toPlayer()
	if xianfu and xianfu:isAlive() and self:isFriend(xianfu) and self:canDraw(xianfu) then return xianfu end
	local target = self:findPlayerToDraw(true,1)
	if target then return target end
	if self:isWeak() then return self.player end
	self:sort(self.friends_noself,"handcard")
	for _,friend in ipairs(self.friends_noself)do
		if friend:hasSkills(sgs.cardneed_skill) and self:canDraw(friend) then
			return friend
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if self:canDraw(friend) then
			return friend
		end
	end
	return self.player
end

sgs.ai_playerchosen_intention.chouce_draw = function(self,from,to)
	if from:objectName()~=to:objectName() then
		sgs.updateIntention(from,to,-50)
	end
end

--谦雅
sgs.ai_skill_askforyiji.qianya = function(self,card_ids,tos)
	return sgs.ai_skill_askforyiji.nosyiji(self,card_ids,tos)
end

--说盟
sgs.ai_skill_playerchosen.shuomeng = function(self,targets)
	local card = self.player:getCards("h"):first()
	if self.player:getHandcardNum()==1 and self.player:getEquips():isEmpty() and (self.player:hasSkill("kongcheng") or
		(not card:isKindOf("Jink") and not card:isKindOf("Peach") and not card:isKindOf("Analeptic"))) then
		self:sort(self.enemies,"handcard")
		for _,p in ipairs(self.enemies)do
			if self:doDisCard(p,"h",true) and targets:contains(p) then
				return p
			end
		end
		self:sort(self.friends_noself,"handcard")
		for _,p in ipairs(self.friends_noself)do
			if self:doDisCard(p,"h",true) and targets:contains(p) and p:getOverflow()>0 then
				return p
			end
		end
	end
	
	if self:getMaxCard():getNumber()<6 then return nil end
	self:sort(self.enemies,"handcard")
	for _,p in ipairs(self.enemies)do
		if self:doDisCard(p,"h",true) and targets:contains(p) then
			return p
		end
	end
	return nil
end

--推锋
sgs.ai_skill_cardask["tuifeng-put"] = function(self,data)
	local to_discard = self:askForDiscard("dummy",1,1,false,true)
	if #to_discard>0 then return "$"..to_discard[1] else return "." end
end

--安东
sgs.ai_skill_invoke.andong = true

sgs.ai_skill_choice.andong = function(self,choices,data)
	local damage = data:toDamage()
	if self:isFriend(damage.to) then return "prevent" end
	local heart = 0
	for _,c in sgs.qlist(self.player:getCards("h"))do
		if c:getSuit()==sgs.Card_Heart then
			heart = heart+1
			if c:isKindOf("Peach") or c:isKindOf("ExNihilo") then
				heart = heart+1
			elseif c:isDamageCard() then
				heart = heart+0.5
			end
		end
	end
	if heart<2*self:ajustDamage(damage.from,damage.to,damage.damage) then return "get" end
	return "prevent"
end

--应势
sgs.ai_skill_playerchosen.yingshi = function(self,targets)
	if #self.enemies==0 then return nil end
	for _,c in sgs.qlist(self.player:getCards("he"))do
		if c:isKindOf("Peach") and self:isWeak(self.friends) then return nil end
	end
	self:sort(self.enemies,"defense")
	return self.enemies[1]
end

sgs.ai_playerchosen_intention.yingshi = 20

sgs.ai_skill_use["@@yingshi"] = function(self,prompt,method)
	local name = prompt:split(":")[2]
	if not name then return "." end
	local to = self.room:findPlayerByObjectName(name)
	if not to or to:isDead() or to:getPile("yschou"):isEmpty() then return "." end
	local cards = {}
	for _,id in sgs.qlist(to:getPile("yschou"))do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
	self:sortByUseValue(cards)
	return "@YingshiCard="..cards[1]:getEffectiveId()
end

--赠刀
local zengdao_skill = {}
zengdao_skill.name = "zengdao"
table.insert(sgs.ai_skills,zengdao_skill)
zengdao_skill.getTurnUseCard = function(self,inclusive)
	if #self.friends_noself>0 then
		return sgs.Card_Parse("@ZengdaoCard=.")
	end
end

sgs.ai_skill_use_func.ZengdaoCard = function(card,use,self)
	local cards = {}
	for _,c in sgs.qlist(self.player:getEquips())do
		if not c:isKindOf("WoodenOx") or self.player:getPile("wooden_ox"):isEmpty() then
			table.insert(cards,c:getEffectiveId())
		end
	end
	if #cards<=0 then return end
	self:sort(self.friends_noself,"defense")
	for _,p in ipairs(self.friends_noself)do
		if p:hasSkills(sgs.hit_skill) then
			use.card = sgs.Card_Parse("@ZengdaoCard="..table.concat(cards,"+"))
			use.to:append(p)
			return
		end
	end
	use.card = sgs.Card_Parse("@ZengdaoCard="..table.concat(cards,"+"))
	use.to:append(self.friends_noself[1])
	return
end

sgs.ai_use_priority.ZengdaoCard = 0.5
sgs.ai_use_value.ZengdaoCard = 0.5
sgs.ai_card_intention.ZengdaoCard = -50

sgs.ai_skill_use["@@zengdao!"] = function(self,prompt,method)
	local id = self.player:getPile("zengdao"):first()
	return "@ZengdaoRemoveCard="..id
end


--鼓舌
local gushe_skill={}
gushe_skill.name="gushe"
table.insert(sgs.ai_skills,gushe_skill)
gushe_skill.getTurnUseCard=function(self,inclusive)
	local card = self:getMaxCard()
	if not card then return end
	if card:getNumber()<=11 and self.player:getMark("&raoshe")>=6 then return end
	if card:getNumber()<=6 then return end
	for _,enemy in ipairs(self.enemies)do
		if self.player:canPindian(enemy) then
			return sgs.Card_Parse("@GusheCard=.")
		end
	end
end

sgs.ai_skill_use_func.GusheCard = function(card,use,self)
	local max_card = self:getMaxCard()
	if not max_card then return end
	self.gushe_card = max_card:getEffectiveId()
	self:sort(self.enemies,"handcard")
	local tos = sgs.SPlayerList()
	local mark = self.player:getMark("&raoshe")
	for _,enemy in ipairs(self.enemies)do
		if self.player:canPindian(enemy) and self:doDisCard(enemy,"he") then
			if tos:length()<math.max(1,math.min(3,6-mark)) then
				tos:append(enemy)
			end
		end
	end
	if tos:isEmpty() then return end
	use.card = card
	use.to = tos 
end

sgs.ai_use_value.GusheCard = sgs.ai_use_value.ExNihilo-0.1
sgs.ai_card_intention.GusheCard = 80

function sgs.ai_skill_pindian.gushe(minusecard,self,requestor)
	local maxcard = self:getMaxCard()
	return self:isFriend(requestor) and self:getMinCard() or ( maxcard:getNumber()<6 and  minusecard or maxcard )
end

sgs.ai_skill_discard.gushe = function(self,discard_num,min_num,optional,include_equip)
	local source = self.player:getTag("gusheDiscard"):toPlayer()
	if not source then return {} end
	if self:isFriend(source) then return {} end
	local to_discard = self:askForDiscard("dummy",1,1,false,true)
	if #to_discard>0 then return {to_discard[1]} end
	return {}
end

--激词
sgs.ai_skill_invoke.jici = function(self,data)
	return true
end

--十周年鼓舌
local tenyeargushe_skill={}
tenyeargushe_skill.name="tenyeargushe"
table.insert(sgs.ai_skills,tenyeargushe_skill)
tenyeargushe_skill.getTurnUseCard=function(self,inclusive)
	local card = self:getMaxCard()
	if not card then return end
	if card:getNumber()<=11 and self.player:getMark("&raoshe")>=6 then return end
	if card:getNumber()<=6 then return end
	for _,enemy in ipairs(self.enemies)do
		if self.player:canPindian(enemy) then
			return sgs.Card_Parse("@TenyearGusheCard=.")
		end
	end
end

sgs.ai_skill_use_func.TenyearGusheCard = function(card,use,self)
	local max_card = self:getMaxCard()
	if not max_card then return end
	self.tenyeargushe_card = max_card:getEffectiveId()
	self:sort(self.enemies,"handcard")
	local tos = sgs.SPlayerList()
	local mark = self.player:getMark("&raoshe")
	for _,enemy in ipairs(self.enemies)do
		if self.player:canPindian(enemy) and self:doDisCard(enemy,"he") then
			if tos:length()<math.max(1,math.min(3,6-mark)) then
				tos:append(enemy)
			end
		end
	end
	if tos:isEmpty() then return end
	use.card = card
	use.to = tos
end

sgs.ai_use_value.TenyearGusheCard = sgs.ai_use_value.GusheCard
sgs.ai_card_intention.TenyearGusheCard = sgs.ai_card_intention.GusheCard

function sgs.ai_skill_pindian.tenyeargushe(minusecard,self,requestor)
	return sgs.ai_skill_pindian.gushe(minusecard,self,requestor)
end

sgs.ai_skill_discard.tenyeargushe = function(self,discard_num,min_num,optional,include_equip)
	local source = self.player:getTag("tenyeargusheDiscard"):toPlayer()
	if not source then return {} end
	if self:isFriend(source) then return {} end
	local to_discard = self:askForDiscard("dummy",1,1,false,true)
	if #to_discard>0 then return {to_discard[1]} end
	return {}
end

--十周年激词
sgs.ai_skill_discard.tenyearjici = function(self,discard_num,min_num,optional,include_equip)
	return self:askForDiscard("dummyreason",discard_num,min_num,false,include_equip)
end

function sgs.ai_slash_prohibit.tenyearjici(self,from,to)
	if hasJueqingEffect(from,to) or (from:hasSkill("nosqianxi") and from:distanceTo(to)==1) then return false end
	if from:hasFlag("NosJiefanUsed") then return false end
	if to:getHp()>1 or #(self:getEnemies(from))==1 then return false end
	if from:isLord() and self:isWeak(from) then return true end
	if self.room:getLord() and from:getRole()=="renegade" then return true end
	return false
end

--清忠
sgs.ai_skill_invoke.qingzhong = function(self,data)
	local num = self.room:getOtherPlayers(self.player):first():getHandcardNum()
	for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
		num = math.min(num,p:getHandcardNum())
	end
	
	local crossbow = false
	for _,c in sgs.qlist(self.player:getCards("h"))do
		if c:isKindOf("Crossbow") and self.player:canUse(c) then
			crossbow = true
			break
		end
	end
	
	local use,slash,analeptic = 0,false,0
	for _,c in sgs.qlist(self.player:getCards("h"))do
		if not self:willUse(self.player,c) then continue end
		if c:isKindOf("Slash") and not slash then
			slash = true
			use = use+1
		elseif c:isKindOf("Slash") and slash then
			if self.player:canSlashWithoutCrossbow() or self.player:hasWeapon("crossbow") or crossbow then
				use = use+1
			end
		elseif c:isKindOf("Analeptic") then
			analeptic = analeptic+1
			if analeptic<=1+sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,c,self.player) then
				use = use+1
			end
		else
			use = use+1
		end
	end
	return self.player:getHandcardNum()+2-use<=num
end

sgs.ai_skill_playerchosen.qingzhong = function(self,targets)
	local selfhand,targethand = self.player:getHandcardNum(),targets:first():getHandcardNum()
	targets = sgs.QList2Table(targets)
	self:sort(targets,"defense")
	for _,p in ipairs(targets)do
		if not self:isFriend(p) then continue end
		if self:doDisCard(p,"h") then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if not self:isFriend(p) then continue end
		return p
	end
	for _,p in ipairs(targets)do
		if not self:isEnemy(p) and self:doDisCard(p,"h") then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if not self:isEnemy(p) then
			return p
		end
	end
	return targets[math.random(1,#targets)]
end

--卫境
local weijing_skill = {}
weijing_skill.name = "weijing"
table.insert(sgs.ai_skills,weijing_skill)
weijing_skill.getTurnUseCard = function(self,inclusive)
	local card_str = string.format("slash:weijing[%s:%s]=.","no_suit",0)
	local slash = sgs.Card_Parse(card_str)
	assert(slash)
	return slash
end

sgs.ai_cardsview_valuable.weijing = function(self,class_name,player)
	if class_name=="Slash" then
		return string.format("slash:weijing[%s:%s]=.","no_suit",0)
	elseif class_name=="Jink" then
		return string.format("jink:weijing[%s:%s]=.","no_suit",0)
	end
end

--膂力
sgs.ai_skill_invoke.lvli = function(self,data)
	if self.player:getHp()>self.player:getHandcardNum() and self:canDraw(self.player) then return true end
	if self.player:getHp()<self.player:getHandcardNum() and self.player:getLostHp()>0 then return true end
	return false
end

--清剿
sgs.ai_skill_invoke.qingjiao = function(self,data)
	if self.player:hasWeapon("CrossBow") or self.player:canSlashWithoutCrossbow() or self:getCardsNum("Crossbow")>0 or
		self:getCardsNum("VSCrossbow")>0 then
		for _,enemy in ipairs(self.enemies)do
			if self.player:canSlash(enemy,true) and not self:slashProhibit(nil,enemy)
				and (self:getCardsNum("Slash")-enemy:getHp()>=1 or self:getCardsNum("Slash")>=3) then
				return false
			end
		end
	end
	local damage_cards_num = 0
	for _,c in sgs.qlist(self.player:getCards("h"))do
		if c:isDamageCard() and not c:isKindOf("DelayedTrick") and not c:isKindOf("Slash") then
			damage_cards_num = damage_cards_num+1
		end
	end
	local valuable_cards_num = self:getCardsNum("Duel")+self:getCardsNum("SavageAssault")+self:getCardsNum("ArcheryAttack")
							+self:getCardsNum("Snatch")+self:getCardsNum("Dismantlement")
	if self.player:getCards("h"):length()>=7 then
		return not (damage_cards_num>=2 or self.player:getLostHp()>=self:getCardsNum("Peach") or valuable_cards_num>=3)
	end
	return true
end

--伪诚
sgs.ai_skill_invoke.weicheng = function(self,data)
	return self:canDraw(self.player)
end

--盗书
local daoshu_skill= {}
daoshu_skill.name = "daoshu"
table.insert(sgs.ai_skills,daoshu_skill)
daoshu_skill.getTurnUseCard = function(self,inclusive)
	if #self.enemies==0 then return end
	return sgs.Card_Parse("@DaoshuCard=.")
end

sgs.ai_skill_use_func.DaoshuCard = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,enemy in ipairs(self.enemies)do
		if self:doDisCard(enemy,"h",true) then 
			use.card = card
			use.to:append(enemy)
			return
		end
	end
end

sgs.ai_skill_cardask["daoshu-give"] = function(self,data)
	local list = data:toStringList()
	local to = self.room:findPlayerByObjectName(list[1])
	local suitstring = list[2]
	if not to or to:isDead() then return "." end
	local cards = {}
	for _,c in sgs.qlist(self.player:getCards("h"))do
		if c:getSuitString()==suitstring then continue end
		table.insert(cards,c)
	end
	if #cards==0 then return "." end
	self:sortByUseValue(cards,true)
	return "$"..cards[1]:getEffectiveId()
end

sgs.ai_use_value.DaoshuCard = 8
sgs.ai_use_priority.DaoshuCard = 5.3

--持节

--引裾
addAiSkills("yinju").getTurnUseCard = function(self)
	local parse = sgs.Card_Parse("@YinjuCard=.")
	assert(parse)
	return parse
end

sgs.ai_skill_use_func["YinjuCard"] = function(card,use,self)
	local n = self:getCardsNum("AOE")
	self:sort(self.friends_noself,"hp")
	for _,fp in sgs.list(self.friends_noself)do
		if fp:getHp()<=n
		then
			use.card = card
			use.to:append(fp)
			fp:addMark("ai_hp-Clear",n)
			return
		end
	end
end

sgs.ai_use_value.YinjuCard = 6.4
sgs.ai_use_priority.YinjuCard = 5.8


--谦冲
sgs.ai_skill_choice.qianchong = function(self,choices,data)
	if self:getCardsNum("Slash")<2 or self.player:hasWeapon("CrossBow") or self.player:canSlashWithoutCrossbow() or
		self:getCardsNum("Crossbow")>0 or self:getCardsNum("VSCrossbow")>0 then
		local choose_trick = true
		for _,enemy in ipairs(self.enemies)do
			if self.player:canSlash(enemy,true) and not self:slashProhibit(slash,enemy) then
				choose_trick = false
				break
			end
		end
		if choose_trick then
			for _,c in sgs.qlist(self.player:getCards("h"))do
				if c:isKindOf("Snatch") or c:isKindOf("SupplyShortage") then  --需要判断本来是不是就是无距离限制，待补充
					return "trick"
				end
			end
		end
	end
	return "basic"
end

--尚俭
sgs.ai_skill_invoke.shangjian = function(self,data)
	return self:canDraw(self.player)
end

--怠攻
sgs.ai_skill_invoke.daigong = function(self,data)
	local from = data:toPlayer()
	if not from or from:isDead() then return false end
	if not self:isFriend(from) and from:getArmor() and self:needToThrowArmor(from) and (not self:isWeak() or self:getCardsNum("Peach")>0) then
		for _,c in sgs.qlist(self.player:getCards("h"))do
			if c:getSuit()==from:getArmor():getSuit() then
				return true
			end
		end
		return false
	end
	return true
end

sgs.ai_skill_cardask["daigong-give"] = function(self,data)
	local list = data:toStringList()
	local to = self.room:findPlayerByObjectName(list[1])
	local suitstring = list[2]
	if not to or to:isDead() or self:isFriend(to) then return "." end
	local cards = {}
	for _,c in sgs.qlist(self.player:getCards("he"))do
		if not string.find(suitstring,c:getSuitString()) then continue end
		table.insert(cards,c)
	end
	if #cards==0 then return "." end
	self:sortByUseValue(cards,true)
	return "$"..cards[1]:getEffectiveId()
end

--昭心
local spzhaoxin_skill = {}
spzhaoxin_skill.name = "spzhaoxin"
table.insert(sgs.ai_skills,spzhaoxin_skill)
spzhaoxin_skill.getTurnUseCard = function(self,inclusive)
	if not self.player:isNude() and self.player:getPile("zxwang"):length()<3 then
		return sgs.Card_Parse("@SpZhaoxinCard=.")
	end
end

sgs.ai_skill_use_func.SpZhaoxinCard = function(card,use,self)
	local unpreferedCards = {}
	local cards = sgs.QList2Table(self.player:getHandcards())

	if self.player:getHp()<3 then
		local zcards = self.player:getCards("he")
		local use_slash,keep_jink,keep_analeptic,keep_weapon = false,false,false
		local keep_slash = self.player:getTag("JilveWansha"):toBool()
		for _,zcard in sgs.qlist(zcards)do
			if not isCard("Peach",zcard,self.player) and not isCard("ExNihilo",zcard,self.player) then
				local shouldUse = true
				if isCard("Slash",zcard,self.player) and not use_slash then
					local dummy_use = dummy()
					self:useBasicCard(zcard,dummy_use)
					if dummy_use.card then
						if keep_slash then shouldUse = false end
						if dummy_use.to then
							for _,p in sgs.qlist(dummy_use.to)do
								if p:getHp()<=1 then
									shouldUse = false
									if self.player:distanceTo(p)>1 then keep_weapon = self.player:getWeapon() end
									break
								end
							end
							if dummy_use.to:length()>1 then shouldUse = false end
						end
						if not self:isWeak() then shouldUse = false end
						if not shouldUse then use_slash = true end
					end
				end
				if zcard:getTypeId()==sgs.Card_TypeTrick then
					local dummy_use = dummy()
					self:useTrickCard(zcard,dummy_use)
					if dummy_use.card then shouldUse = false end
				end
				if zcard:getTypeId()==sgs.Card_TypeEquip and not self.player:hasEquip(zcard) then
					local dummy_use = dummy()
					self:useEquipCard(zcard,dummy_use)
					if dummy_use.card then shouldUse = false end
					if keep_weapon and zcard:getEffectiveId()==keep_weapon:getEffectiveId() then shouldUse = false end
				end
				if self.player:hasEquip(zcard) and zcard:isKindOf("Armor") and not self:needToThrowArmor() then shouldUse = false end
				if self.player:hasEquip(zcard) and zcard:isKindOf("DefensiveHorse") and not self:needToThrowArmor() then shouldUse = false end
				if self.player:hasEquip(zcard) and zcard:isKindOf("WoodenOx") and self.player:getPile("wooden_ox"):length()>1 then shouldUse = false end
				if isCard("Jink",zcard,self.player) and not keep_jink then
					keep_jink = true
					shouldUse = false
				end
				if self.player:getHp()==1 and isCard("Analeptic",zcard,self.player) and not keep_analeptic then
					keep_analeptic = true
					shouldUse = false
				end
				if shouldUse then table.insert(unpreferedCards,zcard:getId()) end
			end
		end
	end

	if #unpreferedCards==0 then
		local use_slash_num = 0
		self:sortByKeepValue(cards)
		for _,card in ipairs(cards)do
			if card:isKindOf("Slash") then
				local will_use = false
				if use_slash_num<=sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,card) then
					local dummy_use = dummy()
					self:useBasicCard(card,dummy_use)
					if dummy_use.card then
						will_use = true
						use_slash_num = use_slash_num+1
					end
				end
				if not will_use then table.insert(unpreferedCards,card:getId()) end
			end
		end

		local num = self:getCardsNum("Jink")-1
		if self.player:getArmor() then num = num+1 end
		if num>0 then
			for _,card in ipairs(cards)do
				if card:isKindOf("Jink") and num>0 then
					table.insert(unpreferedCards,card:getId())
					num = num-1
				end
			end
		end
		for _,card in ipairs(cards)do
			if (card:isKindOf("Weapon") and self.player:getHandcardNum()<3) or card:isKindOf("OffensiveHorse")
				or self:getSameEquip(card,self.player) or card:isKindOf("AmazingGrace") then
				table.insert(unpreferedCards,card:getId())
			elseif card:getTypeId()==sgs.Card_TypeTrick then
				local dummy_use = dummy()
				self:useTrickCard(card,dummy_use)
				if not dummy_use.card then table.insert(unpreferedCards,card:getId()) end
			end
		end

		if self.player:getWeapon() and self.player:getHandcardNum()<3 then
			table.insert(unpreferedCards,self.player:getWeapon():getId())
		end

		if self:needToThrowArmor() then
			table.insert(unpreferedCards,self.player:getArmor():getId())
		end

		if self.player:getOffensiveHorse() and self.player:getWeapon() then
			table.insert(unpreferedCards,self.player:getOffensiveHorse():getId())
		end
	end

	for index = #unpreferedCards,1,-1 do
		if sgs.Sanguosha:getCard(unpreferedCards[index]):isKindOf("WoodenOx") and self.player:getPile("wooden_ox"):length()>1 then
			table.removeOne(unpreferedCards,unpreferedCards[index])
		end
	end

	local use_cards = {}
	for index = #unpreferedCards,1,-1 do
		if #use_cards>=3-self.player:getPile("zxwang"):length() then break end
		table.insert(use_cards,unpreferedCards[index])
	end

	if #use_cards>0 then
		use.card = sgs.Card_Parse("@SpZhaoxinCard="..table.concat(use_cards,"+"))
		return
	end
end

sgs.ai_use_priority.SpZhaoxinCard = 9
sgs.ai_use_value.SpZhaoxinCard = 2.61

sgs.ai_skill_use["@@spzhaoxin"] = function(self,prompt,method)
	local name = prompt:split(":")[2]
	if not name then return "." end
	local current = self.room:findPlayerByObjectName(name)
	if not current or current:isDead() or not self:isFriend(current) then return "." end
	local wang = {}
	for _,id in sgs.qlist(self.player:getPile("zxwang"))do
		table.insert(wang,sgs.Sanguosha:getCard(id))
	end
	self:sortByUseValue(wang)
	return "@SpZhaoxinChooseCard="..wang[1]:getEffectiveId()
end

sgs.ai_skill_invoke.spzhaoxin = function(self,data)
	local str = data:toString()
	str = str:split(":")
	if str[1]=="spzhaoxin_get" then
		local name = str[#str]
		local player = self.room:findPlayerByObjectName(name)
		if player and player:isAlive() and self:isFriend(player) then return true end
		if not self:isFriend(player) and self:needToLoseHp(self.player,player) then return true end
		
		local id = str[2]
		if not id or tonumber(id)<0 then return false end
		local card = sgs.Sanguosha:getCard(id)
		if card:isKindOf("Peach") or card:isKindOf("Analeptic") or (not self:isWeak() and card:isKindOf("ExNihilo")) then return true end
		return false
	elseif str[1]=="spzhaoxin_damage" then
		local name = str[2]
		local player = self.room:findPlayerByObjectName(name)
		if not player or player:isDead() then return false end
		if not self:isFriend(player) and self:needToLoseHp(player,self.player) then return false end
		if self:isFriend(player) and not self:needToLoseHp(player,self.player) then return false end
		return true
	end
	return false
end

--忠佐
sgs.ai_skill_playerchosen.zhongzuo = function(self,targets)
	self:sort(self.friends_noself,"defense")
	self.friends_noself = sgs.reverse(self.friends_noself)
	for _,friend in ipairs(self.friends_noself)do
		if not self:canDraw(friend) then continue end
		if (friend:getHandcardNum()+(friend:isWounded() and -2 or 1))<(self.player:getHandcardNum()+(self.player:isWounded() and -2 or 0)) then
			return friend
		end
	end
	if self:canDraw(self.player) then return self.player end
	return nil
end

--挽澜
sgs.ai_skill_invoke.wanlan = function(self,data)
	local who = data:toPlayer()
	local current = self.room:getCurrent()
	if not current or current:isDead() or current:getPhase()==sgs.Player_NotActive then return self:isFriend(who) end
	if not self:isFriend(who) then return false end
	if self.player:getHandcardNum()>((self:isEnemy(current) and self:isWeak(current)) and 6 or 4)
		or self:getCardsNum("Peach")>((self:isEnemy(current) and self:isWeak(current)) and 1 or 0) then return false end
	return true
end

--通渠
sgs.ai_skill_playerchosen.tongqu = function(self,targets)
	if self:isWeak() then return nil end
	local friends = {}
	for _,p in sgs.qlist(targets)do
		if self:isFriend(p) then
			table.insert(friends,p)
		end
	end
	if #friends<=0 then return nil end
	self:sort(friends,"handcard")
	return friends[1]
end

sgs.ai_skill_use["@@tongqu!"] = function(self,prompt,method)
	local friends = {}
	for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if self:isFriend(p) and p:getMark("&tqqu")>0
		then table.insert(friends,p) end
	end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	local pc = self:poisonCards("he")
	if #friends>0
	then
		self:sort(friends,"handcard")
		for _,c in sgs.list(pc)do
			if c:getTypeId()<3 or c:isAvailable(friends[1]) then continue end
			return "@TongquCard="..c:getEffectiveId().."->"..friends[1]:objectName()
		end
		self:sortByUseValue(cards,true)
		return "@TongquCard="..cards[1]:getEffectiveId().."->"..friends[1]:objectName()
	end
	for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if not self:isFriend(p) and p:getMark("&tqqu")>0
		then
			for _,c in sgs.list(pc)do
				if c:getTypeId()<3
				or c:isAvailable(p)
				then
					return "@TongquCard="..c:getEffectiveId().."->"..p:objectName()
				end
			end
		end
	end
	for _,c in sgs.list(pc)do
		if c:getTypeId()<2 then continue end
		return "@TongquCard="..c:getEffectiveId()
	end
	self:sortByKeepValue(cards)
	return "@TongquCard="..cards[1]:getEffectiveId()
end

--新挽澜
sgs.ai_skill_invoke.newwanlan = function(self,data)
	local who = data:toPlayer()
	return self:isFriend(who) and not hasBuquEffect(who)
end

--推演
sgs.ai_skill_invoke.tuiyan = function(self,data)
	local dp = self.room:getDrawPile()
	self.tuiyanIds = {}
	for i=0,1 do
		table.insert(self.tuiyanIds,dp:at(i))
	end
	return true
end

sgs.ai_skill_invoke.tenyeartuiyan = function(self,data)
	local dp = self.room:getDrawPile()
	self.tuiyanIds = {}
	for i=0,2 do
		table.insert(self.tuiyanIds,dp:at(i))
	end
	return true
end

--卜算
local busuan_skill = {}
busuan_skill.name = "busuan"
table.insert(sgs.ai_skills,busuan_skill)
busuan_skill.getTurnUseCard = function(self,inclusive)
	self.busuan_target = nil
	if #self.friends_noself>0 then
		self:sort(self.friends_noself,"hp")
		for _,friend in ipairs(self.friends_noself)do
			if self:isWeak(friend) and friend:getLostHp()>0 then
				self.busuan_target = friend
				return sgs.Card_Parse("@BusuanCard=.")
			end
		end
	end
	if #self.enemies>0 then
		self:sort(self.enemies,"defense")
		self.busuan_target = self.enemies[1]
		return sgs.Card_Parse("@BusuanCard=.")
	end
	return
end

sgs.ai_skill_use_func.BusuanCard = function(card,use,self)
	if not self.busuan_target then return end
	use.card = card
	use.to:append(self.busuan_target)
end

sgs.ai_skill_askforag.busuan = function(self,card_ids)
	if not self.busuan_target then return card_ids[1] end
	local cards = {}
	for _,id in ipairs(card_ids)do
		table.insert(cards,sgs.Sanguosha:getEngineCard(id))
	end
	self:sortByUseValue(cards,not self:isFriend(self.busuan_target))
	if self:isWeak(self.busuan_target) and self.busuan_target:getLostHp()>0 and self:isFriend(self.busuan_target) then
		for _,c in ipairs(cards)do
			if c:isKindOf("Peach") then
				return c:getEffectiveId()
			end
		end
	end
	return cards[1]:getEffectiveId()
end

--命戒
sgs.ai_skill_invoke.mingjie = function(self,data)
	local isRed
	for i,c in ipairs(self.tuiyanIds or {})do
		if self.room:getCardPlace(c)==sgs.Player_DrawPile then
			c = CardFilter(c,self.player,sgs.Player_PlaceHand)
			isRed = c:isRed()
			table.remove(self.tuiyanIds,i)
			break
		end
	end
	return isRed and self:canDraw()
end

sgs.ai_skill_invoke.tenyearmingjie = function(self,data)
	local isRed
	for i,c in ipairs(self.tuiyanIds or {})do
		if self.room:getCardPlace(c)==sgs.Player_DrawPile then
			c = CardFilter(c,self.player,sgs.Player_PlaceHand)
			isRed = c:isRed()
			table.remove(self.tuiyanIds,i)
			break
		end
	end
	return (isRed or self.player:getHp()<2) and self:canDraw()
end

--遣信
local spqianxin_skill = {}
spqianxin_skill.name = "spqianxin"
table.insert(sgs.ai_skills,spqianxin_skill)
spqianxin_skill.getTurnUseCard = function(self,inclusive)
	if self.room:getDrawPile():length()<self.room:alivePlayerCount() then return end
	if self.player:getMark("spqianxin_disabled")==0 and #self.enemies>0 and not self.player:isKongcheng() then
		return sgs.Card_Parse("@SpQianxinCard=.")
	end
end

sgs.ai_skill_use_func.SpQianxinCard = function(card,use,self)
	self:sort(self.enemies,"defense")
	self.enemies = sgs.reverse(self.enemies)
	
	local handcards = sgs.QList2Table(self.player:getCards("h"))
	local use_card = {}
	self:sortByUseValue(handcards,true)
	for _,c in ipairs(handcards)do
		if (c:isKindOf("Jink") and self:getCardsNum("Jink")>1) or c:isKindOf("Lightning") or c:isKindOf("AmazingGrace") or c:isKindOf("GodSalvation") then
			table.insert(use_card,c:getEffectiveId())
		end
	end
	if #use_card==0 then return end
	use.card = sgs.Card_Parse("@SpQianxinCard="..use_card[1])
	use.to:append(self.enemies[1])
end

sgs.ai_use_priority.SpQianxinCard = 3
sgs.ai_use_value.SpQianxinCard = 3
sgs.ai_card_intention.SpQianxinCard = 50

sgs.ai_skill_choice.spqianxin = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if target then
		if self:isFriend(target) or not self:canDraw(target) then
			return "draw"
		else
			if self.player:getMaxCards()-self.player:getHandcardNum()>=2 then
				return "maxcards"
			end
		end
	end
	return items[1]
end

--镇行
sgs.ai_skill_invoke.zhenxing = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_choice.zhenxing = function(self,choices)
	return "3"
end

--手杀遣信
local mobilespqianxin_skill = {}
mobilespqianxin_skill.name = "mobilespqianxin"
table.insert(sgs.ai_skills,mobilespqianxin_skill)
mobilespqianxin_skill.getTurnUseCard = function(self,inclusive)
	if not self.player:isKongcheng() then
		return sgs.Card_Parse("@MobileSpQianxinCard=.")
	end
end

sgs.ai_skill_use_func.MobileSpQianxinCard = function(card,use,self)
	local handcards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(handcards)
	local cards = {}
	local maxnum = math.min(2,self.room:getOtherPlayers(self.player):length())
	for _,c in ipairs(handcards)do
		if not (c:isKindOf("Peach") or c:isKindOf("Nullification") or (c:isKindOf("Analeptic") and #self.friends_noself<#self.enemies)) then
			table.insert(cards,c:getEffectiveId())
		end
		if #cards>=maxnum then break end
	end
	if #cards==0 then return end
	use.card = sgs.Card_Parse("@MobileSpQianxinCard="..table.concat(cards,"+"))
end

sgs.ai_use_priority.MobileSpQianxinCard = 3
sgs.ai_use_value.MobileSpQianxinCard = 3
sgs.ai_card_intention.MobileSpQianxinCard = 50

sgs.ai_skill_choice.mobilespqianxin = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if target then
		if self:isFriend(target) or not self:canDraw(target) then
			return "draw"
		else
			local fixed = math.max(self.player:getMaxCards()-2,0)
			if ((fixed>0 and self:isEnemy(target)) and self.player:getHandcardNum()-fixed<=3 or self.player:getHandcardNum()-fixed<=1)
				and not self:isWeak() then
				return "maxcards"
			end
		end
	end
	return items[1]
end

--手杀镇行
sgs.ai_skill_invoke.mobilezhenxing = function(self,data)
	return self:canDraw()
end

--机捷
local jijie_skill = {}
jijie_skill.name = "jijie"
table.insert(sgs.ai_skills,jijie_skill)
jijie_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@JijieCard=.")
end

sgs.ai_skill_use_func.JijieCard = function(card,use,self)
	use.card = card
end

sgs.ai_skill_askforyiji.jijie = function(self,card_ids,tos)
	return sgs.ai_skill_askforyiji.nosyiji(self,card_ids,tos)
end

sgs.ai_use_priority.JijieCard = 7
sgs.ai_use_value.JijieCard = 7
sgs.ai_playerchosen_intention.jijie = -20

--急援
sgs.ai_skill_invoke.jiyuan = function(self,data)
	local current_dying_player = self.room:getCurrentDyingPlayer()
	local to = data:toPlayer()
	if current_dying_player then
		if self:isFriend(current_dying_player) and self:canDraw(current_dying_player) then
			return true
		end
	end
	if to then
		if self:isFriend(to) and self:canDraw(to) then
			return true
		end
	end
	return false
end

sgs.ai_choicemade_filter.skillInvoke.jiyuan = function(self,player,promptlist)
	local current_dying_player = self.room:getCurrentDyingPlayer()
	if current_dying_player then
		if promptlist[#promptlist]=="yes" then
			sgs.updateIntention(player,current_dying_player,-80)
		else
			sgs.updateIntention(player,current_dying_player,80)
		end
	end
end

--资援

--秉正
sgs.ai_skill_playerchosen.bingzheng = function(self,targets)
	local targets = sgs.QList2Table(targets)
	self:sort(targets,"handcard")
	local tag = sgs.QVariant()
	for _,p in ipairs(targets)do
		if self:isFriend(p) then
			if p:getHandcardNum()+1==p:getHp() and self:canDraw(p) then
				sgs.ai_skill_choice.bingzheng = "draw"
				tag:setValue(p)
				self.player:setTag("bingzhengForAI",tag)
				return p
			end
		elseif self:isEnemy(p) and p:getHandcardNum()>0 and self:doDisCard(p,"h") then
			if p:getHandcardNum()-1==p:getHp() then
				sgs.ai_skill_choice.bingzheng = "discard"
				tag:setValue(p)
				self.player:setTag("bingzhengForAI",tag)
				return p
			end
		end
	end
	for _,p in ipairs(targets)do
		if self:isEnemy(p) and p:getHandcardNum()>0 and self:doDisCard(p,"h") then
			sgs.ai_skill_choice.bingzheng = "discard"
			tag:setValue(p)
			self.player:setTag("bingzhengForAI",tag)
			return p
		elseif self:isFriend(p) and self:canDraw(p) then
			sgs.ai_skill_choice.bingzheng = "draw"
			tag:setValue(p)
			self.player:setTag("bingzhengForAI",tag)
			return p
		end
	end
	return nil
end

sgs.ai_choicemade_filter.skillChoice.bingzheng = function(self,player,promptlist)
	local choice = promptlist[#promptlist]
	local target = player:getTag("bingzhengForAI"):toPlayer()
	self.player:removeTag("bingzhengForAI")
	if target then
		if choice=="discard" then
			sgs.updateIntention(player,target,80)
		elseif choice=="draw" then
			sgs.updateIntention(player,target,-80)
		end
	end
end

sgs.ai_skill_askforyiji.bingzheng = function(self,card_ids,tos)
	return sgs.ai_skill_askforyiji.nosyiji(self,card_ids,tos)
end

--舍宴
sgs.ai_skill_choice.sheyan = function(self,choices,data)
	local use = data:toCardUse()
	choices = choices:split("+")
	self.sheyan_extra_target = nil
	self.sheyan_remove_target = nil
	local players = sgs.PlayerList()
	if use.card:isKindOf("Collateral")
	then
		if table.contains(choices,"add")
		then
			self.sheyan_collateral = nil
			local dummy_use = dummy()
			table.insert(dummy_use.current_targets,use.from)  --ai还是可以把use.from选择为额外目标，所以这么处理
			for _,p in sgs.qlist(use.to)do
				table.insert(dummy_use.current_targets,p)
			end
			self:useCardCollateral(use.card,dummy_use)
			if dummy_use.card and dummy_use.to:length()==2
			then
				local first = dummy_use.to:at(0):objectName()
				local second = dummy_use.to:at(1):objectName()
				self.sheyan_collateral = { first,second }
				return "add"
			end
		elseif table.contains(choices,"remove") then
			self.sheyan_remove_target = self.player
			return "remove"
		end
	elseif use.card:isKindOf("ExNihilo")
	or use.card:isKindOf("Dongzhuxianji")
	then
		if table.contains(choices,"add")
		then
			self:sort(self.friends_noself,"defense")
			for _,friend in ipairs(self.friends_noself)do
				if not self:hasTrickEffective(use.card,friend,use.from)
				or self.room:isProhibited(use.from,friend,use.card)
				or not self:canDraw(friend)
				or use.to:contains(friend)
				then continue end
				self.sheyan_extra_target = friend
				return "add"
			end
		end
	elseif use.card:isKindOf("GodSalvation")
	then
		if table.contains(choices,"remove")
		then
			self:sort(self.enemies,"hp")
			for _,enemy in ipairs(self.enemies)do
				if use.to:contains(enemy)
				and enemy:isWounded()
				and self:hasTrickEffective(use.card,enemy,use.from)
				then
					self.sheyan_remove_target = enemy
					return "remove"
				end
			end
		end
	elseif use.card:isKindOf("AmazingGrace")
	then
		if table.contains(choices,"remove")
		then
			self:sort(self.enemies)
			for _,enemy in ipairs(self.enemies)do
				if use.to:contains(enemy)
				and self:hasTrickEffective(use.card,enemy,use.from)
				and not hasManjuanEffect(enemy)
				and not self:needKongcheng(enemy,true)
				then
					self.sheyan_remove_target = enemy
					return "remove"
				end
			end
		end
	elseif use.card:isKindOf("SavageAssault")
	or use.card:isKindOf("ArcheryAttack")
	then
		if table.contains(choices,"remove")
		then
			self:sort(self.friends)
			local lord = self.room:getLord()
			if lord and use.to:contains(lord)
			and lord:objectName()~=self.player:objectName()
			and self:isFriend(lord) and self:isWeak(lord)
			and self:hasTrickEffective(use.card,lord,use.from)
			then
				self.sheyan_remove_target = lord
				return "remove"
			end
			for _,friend in ipairs(self.friends)do
				if use.to:contains(friend)
				and self:hasTrickEffective(use.card,friend,use.from)
				then
					self.sheyan_remove_target = friend
					return "remove"
				end
			end
		end
	elseif use.card:isKindOf("Snatch")
	or use.card:isKindOf("Dismantlement")
	then
		self:sort(self.friends_noself,"defense")
		self:sort(self.enemies,"defense")
		if table.contains(choices,"add")
		then
			if self:isFriend(use.from)
			then
				for _,friend in ipairs(self.friends_noself)do
					if use.to:contains(friend)
					or not self:hasTrickEffective(use.card,friend,use.from)
					or self.room:isProhibited(use.from,friend,use.card)
					then continue end
					if friend:getJudgingArea():isEmpty()
					or friend:containsTrick("YanxiaoCard") or not self:needToThrowArmor(friend)
					or use.card:isKindOf("Dismantlement") and not use.from:canDiscard(friend,friend:getArmor():getEffectiveId())
					then continue end
					if not use.card:targetFilter(players,friend,use.from)
					then continue end
					self.sheyan_extra_target = friend
					return "add"
				end
				for _,enemy in ipairs(self.enemies)do
					if use.to:contains(enemy)
					or not self:hasTrickEffective(use.card,enemy,use.from)
					or self.room:isProhibited(use.from,enemy,use.card)
					then continue end
					if not use.card:targetFilter(players,enemy,use.from)
					or not self:doDisCard(enemy,"he") then continue end
					self.sheyan_extra_target = enemy
					return "add"
				end
			else
				for _,friend in ipairs(self.friends_noself)do
					if use.to:contains(friend)
					or not self:hasTrickEffective(use.card,friend,use.from)
					or self.room:isProhibited(use.from,friend,use.card)
					then continue end
					if not use.card:targetFilter(players,friend,use.from)
					then continue end
					if use.card:isKindOf("Snatch") and not friend:isNude()
					then continue end
					if use.card:isKindOf("Dismantlement") then
						local candis = false
						for _,c in sgs.qlist(friend:getCards("he"))do
							if c:isKindOf("Armor") then continue end
							if use.from:canDiscard(friend,c:getEffectiveId()) then
								candis = true
								break
							end
						end
						if candis then continue end
					end
					if not friend:getJudgingArea():isEmpty() and not friend:containsTrick("YanxiaoCard") then
						self.sheyan_extra_target = friend
						return "add"
					elseif self:needToThrowArmor(friend) and (not use.card:isKindOf("Dismantlement") or use.from:canDiscard(friend,friend:getArmor():getEffectiveId())) then
						self.sheyan_extra_target = friend
						return "add"
					end
				end
				for _,enemy in ipairs(self.enemies)do
					if use.to:contains(enemy) or not self:hasTrickEffective(use.card,enemy,use.from) then continue end
					if not use.card:targetFilter(players,enemy,use.from) or not self:doDisCard(enemy,"he") then continue end
					self.sheyan_extra_target = enemy
					return "add"
				end
			end
		elseif table.contains(choices,"remove") then
			if not self:isFriend(use.from) then
				for _,enemy in ipairs(self.enemies)do
					if not use.to:contains(enemy) or not self:hasTrickEffective(use.card,enemy,use.from) then continue end
					if self:doDisCard(enemy,"he") or not enemy:getJudgingArea():isEmpty() then continue end
					self.sheyan_remove_target = enemy
					return "remove"
				end
				for _,friend in ipairs(self.friends_noself)do
					if not use.to:contains(enemy) or not self:hasTrickEffective(use.card,enemy,use.from) then continue end
					if friend:isNude() and friend:containsTrick("YanxiaoCard") then
						self.sheyan_remove_target = friend
						return "remove"
					end
				end
			end
			for _,friend in ipairs(self.friends_noself)do
				if not use.to:contains(friend) or not self:hasTrickEffective(use.card,friend,use.from) then continue end
				if not self:doDisCard(friend,"he") then continue end
				self.sheyan_remove_target = friend
				return "remove"
			end
		end
	elseif use.card:isKindOf("FireAttack") then
		if table.contains(choices,"add") then
			self:sort(self.enemies,"hp")
			for _,enemy in ipairs(self.enemies)do
				if use.to:contains(enemy) or not self:hasTrickEffective(use.card,enemy,use.from) or self.room:isProhibited(use.from,enemy,use.card) then continue end
				if not use.card:targetFilter(players,enemy,use.from) or not self:damageIsEffective(enemy,sgs.DamageStruct_Fire,use.from) then continue end
				self.sheyan_extra_target = enemy
				return "add"
			end
			for _,enemy in ipairs(self.enemies)do
				if use.to:contains(enemy) or not self:hasTrickEffective(use.card,enemy,use.from) or self.room:isProhibited(use.from,enemy,use.card) then continue end
				if not use.card:targetFilter(players,enemy,use.from) then continue end
				self.sheyan_extra_target = enemy
				return "add"
			end
		end
	elseif use.card:isKindOf("IronChain") then
		if table.contains(choices,"remove") then
			local tos = sgs.QList2Table(use.to)
			self:sort(tos,"defense")
			for _,p in ipairs(tos)do
				if not self:hasTrickEffective(use.card,p,use.from) then continue end
				if self:isFriend(p) and not p:isChained() and not p:hasSkill("qianjie") then
					self.sheyan_remove_target = p
					return "remove"
				elseif self:isEnemy(p) and p:isChained() and not p:hasSkill("jieying") then
					self.sheyan_remove_target = p
					return "remove"
				end
			end
		elseif table.contains(choices,"add") then
			self:sort(self.friends_noself,"defense")
			for _,friend in ipairs(self.friends_noself)do
				if use.to:contains(friend) or not self:hasTrickEffective(use.card,friend,use.from) or self.room:isProhibited(use.from,friend,use.card) then continue end
				if friend:isChained() and not enemy:hasSkill("jieying") then
					self.sheyan_extra_target = friend
					return "add"
				end
			end
			self:sort(self.enemies,"defense")
			for _,enemy in ipairs(self.enemies)do
				if use.to:contains(enemy) or not self:hasTrickEffective(use.card,enemy,use.from) or self.room:isProhibited(use.from,enemy,use.card) then continue end
				if not enemy:isChained() and not enemy:hasSkill("qianjie") then
					self.sheyan_extra_target = enemy
					return "add"
				end
			end
		end
	elseif use.card:isKindOf("Duel")
	then
		if table.contains(choices,"add")
		then
			self:sort(self.enemies,"hp")
			for _,enemy in ipairs(self.enemies)do
				if use.to:contains(enemy)
				or not self:hasTrickEffective(use.card,enemy,use.from)
				or self.room:isProhibited(use.from,enemy,use.card) then continue end
				if not use.card:targetFilter(players,enemy,use.from) then continue end
				self.sheyan_extra_target = enemy
				return "add"
			end
		end
	end
	
	return "cancel"
end

sgs.ai_skill_playerchosen.sheyan = function(self,targets)
	if not self.sheyan_extra_target and not self.sheyan_remove_target then self.room:writeToConsole("sheyan player chosen error!!") end
	return self.sheyan_extra_target or self.sheyan_remove_target
end

sgs.ai_skill_use["@@sheyan!"] = function(self,prompt) -- extra target for Collateral
	if not self.sheyan_collateral then self.room:writeToConsole("sheyan player chosen error!!") end
	return "@ExtraCollateralCard=.->"..self.sheyan_collateral[1].."+"..self.sheyan_collateral[2]
end

sgs.ai_target_revises.sheyan = function(to,card,self,use)
    if card:isNDTrick()
	and use.to:length()>1
	and self:isEnemy(to)
	then return true end
end

--抚蛮
local fuman_skill = {}
fuman_skill.name = "fuman"
table.insert(sgs.ai_skills,fuman_skill)
fuman_skill.getTurnUseCard = function(self,inclusive)
	if #self.friends_noself>0 then
		return sgs.Card_Parse("@FumanCard=.")
	end
end

sgs.ai_skill_use_func.FumanCard = function(card,use,self)
	local handcards = self.player:getHandcards()
    local slashs = {}
    for _,c in sgs.qlist(handcards)do
        if c:isKindOf("Slash") then
			table.insert(slashs,c)
        end
    end
    if #slashs==0 then return end
    self:sortByUseValue(slashs)
	
    self:sort(self.friends_noself,"handcard")
	for _,p in ipairs(self.friends_noself)do
        if p:getMark("fuman_target-PlayClear")==0 and not self:needKongcheng(p,true) and not self:willSkipPlayPhase(p) and not hasManjuanEffect(p) then
			use.card = sgs.Card_Parse("@FumanCard="..slashs[1]:getEffectiveId())
			use.to:append(p)
			return
        end
    end
end

sgs.ai_use_priority.FumanCard = sgs.ai_use_priority.Slash-0.1
sgs.ai_use_value.FumanCard = 4
 
sgs.ai_card_intention.FumanCard = function(self,card,from,tos)
    local to = tos[1]
    local intention = -70
    if hasManjuanEffect(to) then
        intention = 0
    elseif self:needKongcheng(to,true) then
        intention = 0
    end
    sgs.updateIntention(from,to,intention)
end

--图南
local tunan_skill = {}
tunan_skill.name = "tunan"
table.insert(sgs.ai_skills,tunan_skill)
tunan_skill.getTurnUseCard = function(self,inclusive)
	if #self.friends_noself>0 then
		return sgs.Card_Parse("@TunanCard=.")
	end
end

sgs.ai_skill_use_func.TunanCard = function(card,use,self)
	self:sort(self.friends_noself,"defense")
	self:sort(self.enemies,"defense")
	local targets = {}
	local slash = dummyCard()
	for i = #self.friends_noself,1,-1 do
		for _,enemy in ipairs(self.enemies)do
			if self.friends_noself[i]:canSlash(enemy,slash,true)
			and self:isGoodTarget(enemy,self.enemies,slash) 
			then
				use.card = card
				use.to:append(self.friends_noself[i])
				return
			end
		end
	end
	if #self.friends_noself>0 then
		use.card = card
		use.to:append(self.friends_noself[#self.friends_noself])
	end
end

sgs.ai_use_priority.TunanCard = 3
sgs.ai_use_value.TunanCard = 3
sgs.ai_card_intention.TunanCard = -80

sgs.ai_skill_choice.tunan = function(self,choices,data)
	local card = data:toCard()
	local dummy_use = self:aiUseCard(card)
	if dummy_use.card and dummy_use.to
	then return "use" end
	return "slash"
end

sgs.ai_skill_use["@@tunan1!"] = function(self,prompt)
	local id = self.player:getMark("tunan_id-PlayClear")-1
	if id<0 then return "." end
	local card = sgs.Sanguosha:getEngineCard(id)
	local dummy_use = self:aiUseCard(card)
	if dummy_use.card then
		local targets = {}
		for _,p in sgs.qlist(dummy_use.to)do
			table.insert(targets,p:objectName())
		end
		return card:toString().."->"..table.concat(targets,"+")
	end
	return "."
end

sgs.ai_skill_use["@@tunan2!"] = function(self,prompt)
	local id = self.player:getMark("tunan_id-PlayClear")-1
	if id<0 then return "." end
	local slash = dummyCard()
	slash:addSubcard(id)
	slash:setSkillName("_tunan")
	local dummy_use = self:aiUseCard(slash)
	if dummy_use.card then
		local targets = {}
		for _,p in sgs.qlist(dummy_use.to)do
			table.insert(targets,p:objectName())
		end
		return slash:toString().."->"..table.concat(targets,"+")
	end
	return "."
end

--闭境
sgs.ai_skill_cardask["bijing-invoke"] = function(self,data)
	local cards = {}
	for _,c in sgs.qlist(self.player:getCards("h"))do
		if c:isKindOf("Jink") then
			table.insert(cards,c)
		end
	end
	if #cards>0 then
		self:sortByKeepValue(cards)
		return "$"..cards[1]:getEffectiveId()
	end
	
	for _,c in sgs.qlist(self.player:getCards("h"))do
		if c:isKindOf("Slash") then
			table.insert(cards,c)
		end
	end
	if #cards>0 then
		self:sortByKeepValue(cards)
		return "$"..cards[1]:getEffectiveId()
	end
	
	return "."
end

--点虎
function getDianhuTarget(self,targets,RolePredictable)
	if not RolePredictable then
		for _,p in sgs.qlist(targets)do
			if p:hasSkill("shibei") then return p end
		end
		for _,p in sgs.qlist(targets)do
			if p:hasSkills(sgs.recover_skill) then return p end
		end
	else
		for _,p in sgs.qlist(targets)do
			if not self.player:isYourFriend(p) and p:hasSkill("shibei") then return p end
		end
		for _,p in sgs.qlist(targets)do
			if not self.player:isYourFriend(p) and p:hasSkills(sgs.recover_skill) then return p end
		end
	end
	return nil
end

sgs.ai_skill_playerchosen.dianhu = function(self,targets)
	if self.player:getRole()=="rebel" and self.room:getLord() then
		return self.room:getLord()
	end
	local target = getDianhuTarget(self,targets,isRolePredictable())
	if target then return target end
	for _,p in sgs.qlist(targets)do
		if self:isEnemy(p) then
			return p
		end
	end
	if self.player:getRole()=="loyalist" and self.room:getLord() then
		local new_targets = sgs.SPlayerList()
		for _,p in sgs.qlist(targets)do
			if p:isLord() then continue end
			new_targets:append(p)
		end
		if not new_targets:isEmpty() then return new_targets:at(math.random(0,new_targets:length()-1)) end
	end
	return targets:at(math.random(0,targets:length()-1))
end

--谏计
local jianji_skill = {}
jianji_skill.name = "jianji"
table.insert(sgs.ai_skills,jianji_skill)
jianji_skill.getTurnUseCard = function(self,inclusive)
	if #self.friends_noself>0 then
		return sgs.Card_Parse("@JianjiCard=.")
	end
end

sgs.ai_skill_use_func.JianjiCard = function(card,use,self)
	self:sort(self.friends_noself,"handcard")
	for _,friend in ipairs(self.friends_noself)do
		if self:canDraw(friend) then
			use.card = card
			use.to:append(friend) 
			return
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if not hasManjuanEffect(friend) then
			use.card = card
			use.to:append(friend) 
			return
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if not hasManjuanEffect(friend,true) then
			use.card = card
			use.to:append(friend) 
			return
		end
	end
end

sgs.ai_use_priority.JianjiCard = 7
sgs.ai_use_value.JianjiCard = 7
sgs.ai_card_intention.JianjiCard = -20

sgs.ai_skill_use["@@jianji"] = function(self,prompt)
	local id = self.player:getMark("jianji_id-PlayClear")-1
	if id<0 then return "." end
	local c = sgs.Sanguosha:getEngineCard(id)
	local d = self:aiUseCard(c)
	if d.card then
		if c:canRecast() and d.to:isEmpty()
		then return "." end
		local targets = {}
		for _,p in sgs.qlist(d.to)do
			table.insert(targets,p:objectName())
		end
		return id.."->"..table.concat(targets,"+")
	end
	return "."
end

--蒺藜
sgs.ai_skill_invoke.jili = function(self,data)
	return self:canDraw()
end

sgs.ai_use_revises.jili = function(self,card,use)
	if card:isKindOf("Weapon") then
		local r = card:getRealCard():toWeapon():getRange()
		if #self.toUse>1 then
			if self.player:getMark("jili-Clear")+1==r then
				return true
			end
		else
			if r==1 and self.player:getAttackRange()~=1 then
				return true
			end
		end
	end
end

--翊赞
local yizan_skill = {}
yizan_skill.name = "yizan"
table.insert(sgs.ai_skills,yizan_skill)
yizan_skill.getTurnUseCard = function(self)
	local basic,notbasic = {},{}
	local HandPile = self:addHandPile("he")
	self:sortByUseValue(HandPile,true)
	for _,c in sgs.list(HandPile)do
		if c:isKindOf("BasicCard") then table.insert(basic,c)
		else table.insert(notbasic,c) end
	end
	local name = self:ZhanyiUseBasic()
	if name and #basic>0
	then
		local c = dummyCard(name)
		c:setSkillName("yizan")
		c:addSubcard(basic[1])
		if self.player:property("yizan_level"):toInt()<=0
		then
			if #notbasic<=0 and #basic<=1 then return end
			if self:needToThrowArmor() and self.player:getArmor()
			then c:addSubcard(self.player:getArmor())
			elseif #notbasic>0 then c:addSubcard(notbasic[1])
			else c:addSubcard(basic[2]) end
		end
		return c
		--sgs.Card_Parse("@YizanCard="..table.concat(use_cards,"+")..":"..name)
	end
end

sgs.ai_skill_use_func.YizanCard = function(card,use,self)
	local userstring = card:toString()
	userstring = userstring:split(":")[3]
	local yizancard = dummyCard(userstring)
	yizancard:addSubcards(card:getSubcards())
	yizancard:setSkillName("yizan")
	if yizancard:isAvailable(self.player) then
		self:aiUseCard(yizancard,use)
		if use.card and use.to
		then use.card = card end
	end	
end

sgs.ai_use_priority.YizanCard = 3
sgs.ai_use_value.YizanCard = 3

sgs.ai_cardsview_valuable.yizan = function(self,class_name,player)
	local HandPile = self:addHandPile("he")
	self:sortByKeepValue(HandPile)
	local basic,notbasic = {},{}
	for _,c in sgs.list(HandPile)do
		if c:isKindOf(class_name) then return end
		if c:isKindOf("BasicCard") then table.insert(basic,c)
		else table.insert(notbasic,c) end
	end
	if #basic<1 then return end
	local c = dummyCard(class_name)
	c:setSkillName("yizan")
	c:addSubcard(basic[1])
	if player:property("yizan_level"):toInt()<=0
	then
		if #notbasic<=0 and #basic<=1 then return end
		if self:needToThrowArmor() and player:getArmor()
		then c:addSubcard(player:getArmor())
		elseif #notbasic>0 then c:addSubcard(notbasic[1])
		else c:addSubcard(basic[2]) end
	end
	return c:toString()
	--("@YizanCard="..table.concat(use_cards,"+")..":"..name)
end

sgs.ai_skill_choice.yizan_saveself = function(self,choices)
	if self:getCard("Peach") or not self:getCard("Analeptic") then return "peach" else return "analeptic" end
end

sgs.ai_skill_choice.yizan_slash = function(self,choices)
	return "slash"
end

sgs.ai_cardneed.yizan = function(to,card,self)
	if to:property("yizan_level"):toInt()<=0 then return false end
	return card:isKindOf("BasicCard")
end

--武缘
local wuyuan_skill = {}
wuyuan_skill.name = "wuyuan"
table.insert(sgs.ai_skills,wuyuan_skill)
wuyuan_skill.getTurnUseCard = function(self,inclusive)
	if #self.friends_noself==0 then return false end
	return sgs.Card_Parse("@WuyuanCard=.")
end

sgs.ai_skill_use_func.WuyuanCard = function(card,use,self)
	local red_tf_slash,red_slash,tf_slash,slash = {},{},{},{}
	for _,c in sgs.list(self:sortByUseValue(self.player:getCards("h")))do
		if c:isKindOf("Slash") then
			table.insert(slash,c)
			if c:isRed() and c:objectName()~="slash"
			then table.insert(red_tf_slash,c) end
			if c:isRed() then
				table.insert(red_slash,c)
			end
			if c:objectName()~="slash" then
				table.insert(tf_slash,c)
			end
		end
	end
	if #slash<=0 then return end
	if self:isWeak(self.friends_noself)
	then
		self:sort(self.friends_noself,"hp")
		local target
		for _,p in ipairs(self.friends_noself)do
			if not self:isWeak(p) or p:getLostHp()<=0 then continue end
			target = p
			break
		end
		if target then
			local id
			if #red_tf_slash>0 then
				id = red_tf_slash[1]:getEffectiveId()
			end
			if #red_slash>0 then
				id = red_slash[1]:getEffectiveId()
			end
			if id
			then
				use.card = sgs.Card_Parse("@WuyuanCard="..id)
				use.to:append(target)
				return
			end
		end
	end
	if #red_tf_slash>0 then
		self:sort(self.friends_noself,"defense")
		for _,p in ipairs(self.friends_noself)do
			if self:canDraw(p) and p:getLostHp()>0 then
				use.card = sgs.Card_Parse("@WuyuanCard="..red_tf_slash[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
		end
	end
	if #tf_slash>0 then
		local friend = self:findPlayerToDraw(false,2)
		if friend then
			use.card = sgs.Card_Parse("@WuyuanCard="..tf_slash[1]:getEffectiveId())
			use.to:append(friend) 
			return
		end
	end
	if #red_tf_slash>0 then
		local friend = self:findPlayerToDraw(false,2)
		if friend then
			use.card = sgs.Card_Parse("@WuyuanCard="..red_tf_slash[1]:getEffectiveId())
			use.to:append(friend) 
			return
		end
	end
	if #slash>0 then
		local num = 1
		if slash[1]:objectName()~="slash" then num = 2 end
		local friend = self:findPlayerToDraw(false,num)
		if friend then
			use.card = sgs.Card_Parse("@WuyuanCard="..slash[1]:getEffectiveId())
			use.to:append(friend) 
			return
		end
	end
	if #slash>0 then
		self:sort(self.friends_noself,"defense")
		for _,p in ipairs(self.friends_noself)do
			if self:canDraw(p) then
				use.card = sgs.Card_Parse("@WuyuanCard="..slash[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
		end
	end
end

sgs.ai_use_value.WuyuanCard = 5.9
sgs.ai_use_priority.WuyuanCard = 4
sgs.ai_card_intention.WuyuanCard = -70

sgs.ai_cardneed.wuyuan = function(to,card,self)
	return card:isKindOf("Slash")
end

local tenyearwuyuan = {}
tenyearwuyuan.name = "tenyearwuyuan"
table.insert(sgs.ai_skills,tenyearwuyuan)
tenyearwuyuan.getTurnUseCard = function(self,inclusive)
	if #self.friends_noself<1 then return false end
	return sgs.Card_Parse("@TenyearWuyuanCard=.")
end

sgs.ai_skill_use_func.TenyearWuyuanCard = function(card,use,self)
	local red_tf_slash,red_slash,tf_slash,slash = {},{},{},{}
	for _,c in sgs.qlist(self.player:getCards("h"))do
		if c:isKindOf("Slash") then
			table.insert(slash,c)
			if c:isRed() and c:objectName()~="slash"
			then
				table.insert(red_tf_slash,c)
			end
			if c:isRed() then
				table.insert(red_slash,c)
			end
			if c:objectName()~="slash" then
				table.insert(tf_slash,c)
			end
		end
	end
	if #slash<=0 then return end
	
	if self:isWeak(self.friends_noself)
	then
		self:sort(self.friends_noself,"hp")
		local target
		for _,p in ipairs(self.friends_noself)do
			if not self:isWeak(p) or p:getLostHp()<=0 then continue end
			target = p
			break
		end
		if target then
			local id
			if #red_tf_slash>0 then
				self:sortByUseValue(red_tf_slash)
				id = red_tf_slash[1]:getEffectiveId()
			end
			if #red_slash>0 then
				self:sortByUseValue(red_slash)
				id = red_slash[1]:getEffectiveId()
			end
			if id
			then
				use.card = sgs.Card_Parse("@TenyearWuyuanCard="..id)
				use.to:append(target)
				return
			end
		end
	end
	
	if #red_tf_slash>0 then
		self:sortByUseValue(red_tf_slash)
		self:sort(self.friends_noself,"defense")
		for _,p in ipairs(self.friends_noself)do
			if self:canDraw(p) and p:getLostHp()>0 then
				use.card = sgs.Card_Parse("@TenyearWuyuanCard="..red_tf_slash[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
		end
	end
	
	if #tf_slash>0 then
		self:sortByUseValue(tf_slash)
		local friend = self:findPlayerToDraw(false,2)
		if friend then
			use.card = sgs.Card_Parse("@TenyearWuyuanCard="..tf_slash[1]:getEffectiveId())
			use.to:append(friend) 
			return
		end
	end
	
	if #red_tf_slash>0 then
		self:sortByUseValue(red_tf_slash)
		local friend = self:findPlayerToDraw(false,2)
		if friend then
			use.card = sgs.Card_Parse("@TenyearWuyuanCard="..red_tf_slash[1]:getEffectiveId())
			use.to:append(friend) 
			return
		end
	end
	
	if #slash>0 then
		self:sortByUseValue(slash)
		local num = 1
		if slash[1]:objectName()~="slash" then
			num	= 2
		end
		local friend = self:findPlayerToDraw(false,num)
		if friend then
			use.card = sgs.Card_Parse("@TenyearWuyuanCard="..slash[1]:getEffectiveId())
			use.to:append(friend) 
			return
		end
	end
	
	if #slash>0 then
		self:sortByUseValue(slash)
		self:sort(self.friends_noself,"defense")
		for _,p in ipairs(self.friends_noself)do
			if self:canDraw(p) then
				use.card = sgs.Card_Parse("@TenyearWuyuanCard="..slash[1]:getEffectiveId())
				use.to:append(p) 
				return
			end
		end
	end
end

sgs.ai_use_value.TenyearWuyuanCard = 5.9
sgs.ai_use_priority.TenyearWuyuanCard = 4
sgs.ai_card_intention.TenyearWuyuanCard = -70

--誉虚
sgs.ai_skill_invoke.yuxu = true

sgs.ai_skill_discard.yuxu = function(self,discard_num,min_num,optional,include_equip)
	return self:askForDiscard("dummyreason",1,1,false,true)
end

--实荐
sgs.ai_skill_cardask["@shijian-discard"] = function(self,data)
	local player = data:toPlayer()
	if not self:isFriend(player) then
		if player:getHandcardNum()<=2 and self:isWeak() and self.player:getArmor() and self.player:hasArmorEffect("SilverLion") and self.player:getLostHp()>0 then
			return "$"..self.player:getArmor():getEffectiveId()
		end
		return "."
	end
	if player:hasSkill("yuxu",true) then
		if self:isWeak() and self.player:getArmor() and self.player:hasArmorEffect("SilverLion") and self.player:getLostHp()>0 then
			return "$"..self.player:getArmor():getEffectiveId()
		end
		return "."
	end
	
	if player:getHandcardNum()<=2 then return "." end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	return "$"..cards[1]:getEffectiveId()
end

--蛮嗣
sgs.ai_skill_invoke.mansi = function(self,data)
	return self:canDraw()
end

--薮影
sgs.ai_skill_cardask["souying-invoke"] = function(self,data)
	local damage = data:toDamage()
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	if damage.from:objectName()==self.player:objectName() then
		local male = damage.to
		if self:isFriend(male) then return "." end
		local n = 0
		if self:isEnemy(male) and damage.nature~=sgs.DamageStruct_Normal and male:isChained() then
			for _,p in sgs.qlist(self.room:getAllPlayers())do
				if not p:isChained() then continue end
				if p:getHp()>damage.damage or hasBuquEffect(p) or self:cantDamageMore(damage.from,p) or canNiepan(p) or
					not self:damageIsEffective(p,damage.nature,damage.from) then continue end
				if self:isFriend(p) then
					n = n+1
					if p:isLord() then
						n = n+10
					end
				elseif self:isEnemy(p) then  --判断天香 待补充
					n = n-1
					if p:isLord() then
						n = n-10
					end
				end
			end
		end
		if n>0 then return "." end
		return "$"..cards[1]:getEffectiveId()
	else
		local male = damage.from
		if self:isFriend(male) and damage.nature~=sgs.DamageStruct_Normal and self.player:isChained() then 
			for _,p in sgs.qlist(self.room:getAllPlayers())do
				if not p:isChained() or not self:damageIsEffective(p,damage.nature,damage.from) then continue end
				if self:isFriend(p) then
					n = n+1
					if p:isLord() then
						n = n+10
					end
				elseif self:isEnemy(p) then  --判断天香 待补充
					n = n-1
					if p:isLord() then
						n = n-10
					end
				end
			end
			if n>0 then return "." end
			return "$"..cards[1]:getEffectiveId()
		else
			return "$"..cards[1]:getEffectiveId()
		end
	end
end

--战缘
sgs.ai_skill_playerchosen.zhanyuan = function(self,targets)
	local friends = {}
	for _,p in sgs.qlist(targets)do
		if self:isFriend(p) and p:objectName()~=self.player:objectName() then
			table.insert(friends,p)
		end
	end
	if #friends>0 then
		self:sort(friends)
		return friends[#friends]
	end
	
	if targets:contains(self.player) then return self.player end
	return nil
end

--系力
sgs.ai_skill_cardask["xili-invoke"] = function(self,data)
	local use = data:toCardUse()
	local n,enemy_effective,friend_effective = 0,0,0
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	local lord = self.room:getLord()
	if lord and self:isFriend(lord) and use.to:contains(lord) then return "." end
	if lord and self:isEnemy(lord) and use.to:contains(lord) and not self:cantDamageMore(use.from,lord) then
		if not self:slashIsEffective(use.card,lord,use.from) or (lord:hasArmorEffect("EightDiagram") and not self:isWeak(lord)) or
			getKnownCard(lord,self.player,"Jink",true,"hej")>0 then return "." end
		return "$"..cards[1]:getEffectiveId()
	end
	
	for _,p in sgs.qlist(use.to)do
		if self:slashIsEffective(use.card,p,use.from) or self:cantDamageMore(use.from,p) then
			if self:isFriend(p) then
				friend_effective = friend_effective+1
			elseif self:isEnemy(p) then
				enemy_effective = enemy_effective+1
			end
		end
		if p:hasArmorEffect("EightDiagram") then
			if self:isFriend(p) then
				n = n+1
			elseif self:isEnemy(p) then
				n = n-1
			end
		end
		if getKnownCard(p,self.player,"Jink",true,"hej")>0 then
			if self:isFriend(p) then
				n = n+1
			elseif self:isEnemy(p) then
				n = n-1
			end
		end
	end
	if friend_effective>=enemy_effective or n>0 then return "." end
	return "$"..cards[1]:getEffectiveId()
end

--二版蛮嗣
local secondmansi_skill = {}
secondmansi_skill.name = "secondmansi"
table.insert(sgs.ai_skills,secondmansi_skill)
secondmansi_skill.getTurnUseCard = function(self)
	if self.player:isKongcheng() then return end
	local savage_assault = dummyCard("savage_assault")
	savage_assault:addSubcards(self.player:getHandcards())
	savage_assault:setSkillName("secondmansi")
	if not savage_assault:isAvailable(self.player) or self:getAoeValue(savage_assault)<=0 then return end
	local handcards = sgs.QList2Table(self.player:handCards())
	return sgs.Card_Parse("@SecondMansiCard="..table.concat(handcards,"+")..":".."savage_assault")
end

sgs.ai_skill_use_func.SecondMansiCard = function(card,use,self)
	local userstring = card:toString()
	userstring = (userstring:split(":"))[3]
	local sa = dummyCard(userstring)
	sa:setSkillName("secondmansi")
	self:useTrickCard(sa,use)
	if use.card then
		for _,acard in sgs.qlist(self.player:getHandcards())do
			if isCard("Peach",acard,self.player) and self.player:getHandcardNum()>1 and self.player:isWounded()
			and not self:needToLoseHp(self.player,nil,acard) then
				use.card = acard
				return
			end
		end
		use.card = card
	end
end

sgs.ai_use_priority.SecondMansiCard = 1.5

--二版薮影
sgs.ai_skill_cardask["@secondsouying-dis"] = function(self,data)
	local use = data:toCardUse()
	if self:isWeak() and self.player:getArmor() and self.player:hasArmorEffect("SilverLion") and self.player:getLostHp()>0 then
		return "$"..self.player:getArmor():getEffectiveId()
	end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByUseValue(cards)
	if self:getUseValue(cards[#cards])>self:getUseValue(use.card) then
		return "$"..cards[#cards]:getEffectiveId()
	end
	return "."
end

sgs.ai_skill_cardask["@secondsouying-dis2"] = function(self,data)
	local use = data:toCardUse()
	if self:isWeak() and self.player:getArmor() and self.player:hasArmorEffect("SilverLion") and self.player:getLostHp()>0 then
		return "$"..self.player:getArmor():getEffectiveId()
	end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	if use.card:isKindOf("Slash") then
		if not self:slashIsEffective(use.card,self.player,use.from) then return "." end
	elseif use.card:isKindOf("TrickCard") then
		if not self:hasTrickEffective(use.card,self.player,use.from) then return "." end
		if self:isFriend(use.from) then return "." end
	end
	return "$"..cards[1]:getEffectiveId()
end

--二版战缘
sgs.ai_skill_playerchosen.secondzhanyuan = function(self,targets)
	local friends = {}
	for _,p in sgs.qlist(targets)do
		if self:isFriend(p) and p:objectName()~=self.player:objectName() then
			table.insert(friends,p)
		end
	end
	if #friends>0 then
		self:sort(friends)
		return friends[#friends]
	end
	return nil
end

--二版系力
sgs.ai_skill_cardask["@secondxili-dis"] = function(self,data)
	local damage = data:toDamage()
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	if self:isFriend(damage.from) and not self:isFriend(damage.to) then
		if self:isWeak() and self.player:getArmor() and self.player:hasArmorEffect("SilverLion") and self.player:getLostHp()>0 then
			return "$"..self.player:getArmor():getEffectiveId()
		end
		if self:cantDamageMore(damage.from,damage.to) then return "." end
		return "$"..cards[1]:getEffectiveId()
	elseif self:isEnemy(damage.from) and self:isEnemy(damage.to) then
		if self:isWeak() and self.player:getArmor() and self.player:hasArmorEffect("SilverLion") and self.player:getLostHp()>0 then
			return "$"..self.player:getArmor():getEffectiveId()
		end
		if self:cantDamageMore(damage.from,damage.to) then return "." end
		return "$"..cards[1]:getEffectiveId()
	else
		if self:cantDamageMore(damage.from,damage.to) then
			if self:isWeak() and self.player:getArmor() and self.player:hasArmorEffect("SilverLion") and self.player:getLostHp()>0 then
				return "$"..self.player:getArmor():getEffectiveId()
			end
		end
	end
	return "."
end

--弘德
sgs.ai_skill_playerchosen.hongde = function(self,targets)
	return self:findPlayerToDraw(false,1)
end

--定叛
local dingpan_skill = {}
dingpan_skill.name = "dingpan"
table.insert(sgs.ai_skills,dingpan_skill)
dingpan_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@DingpanCard=.")
end

sgs.ai_skill_use_func.DingpanCard = function(card,use,self)
	local friends,enemies = {},{}
	for _,player in sgs.qlist(self.room:getPlayers())do
        if not player:getEquips():isEmpty() then
            if self:isFriend(player) and self:canDraw(player) then
                table.insert(friends,player)
            elseif self:isEnemy(player) and self:doDisCard(player,"e") 
    	   	and not player:hasArmorEffect("SilverLion")
			and not self:needToLoseHp(player)
			then
                table.insert(enemies,player)
            end
        end
    end
	if #friends==0 and #enemies==0 then return end
	
	if self:needToThrowArmor() then
		use.card = card
		use.to:append(self.player) 
		return
	end
	
	self:sort(friends)
	self:sort(enemies)
	
	for _,friend in ipairs(friends)do
		if self:needToThrowArmor(friend) then
			use.card = card
			use.to:append(friend) 
			return
		end
	end
	
	for _,enemy in ipairs(enemies)do
		if self:needKongcheng(enemy,true) then
			use.card = card
			use.to:append(enemy)
			return
		end
	end
	
	for _,enemy in ipairs(enemies)do
		if enemy:containsTrick("indulgence") and not enemy:containsTrick("YanxiaoCard") then
			use.card = card
			use.to:append(enemy) 
			return
        end
	end
	
	for i = #friends,1,-1 do
		local friend = friends[i]
		if not self:isWeak(friend) and (friend:hasSkills(sgs.lose_equip_skill) or hasTuntianEffect(friend,true)) then
			use.card = card
			use.to:append(friend) 
			return
		end
	end
	
	for i = #friends,1,-1 do
		local friend = friends[i]
		if not self:isWeak(friend) and (friend:hasSkills(sgs.lose_equip_skill) or hasTuntianEffect(friend)) then
			use.card = card
			use.to:append(friend) 
			return
		end
	end
	
	if #enemies==0 then return end
	use.card = card
	use.to:append(enemies[1]) 
	return
end

sgs.ai_use_priority.DingpanCard = sgs.ai_use_priority.Slash+0.1

sgs.ai_skill_choice.dingpan = function(self,choices,data)
	if (self.player:hasArmorEffect("SilverLion") and self.player:getArmor()) or self:needToLoseHp() then 
        return "get" 
    end
    return "discard"
end

--闪袭
getShanxiTarget = function(self,targets)
	targets = targets or self.room:getOtherPlayers(self.player)
	local friends,enemies = {},{}
	for _,p in sgs.qlist(targets)do
		if not self.player:canDiscard(p,"he") then continue end
		if self.player:inMyAttackRange(p) then
			if self:isFriend(p) then
				table.insert(friends,p)
			elseif self:isEnemy(p) and self:doDisCard(p,"he") then
				table.insert(enemies,p)
			end
		end
    end
	if #friends==0 and #enemies==0 then return nil end
	self:sort(enemies,"defense")
	self:sort(friends,"defense")
	
	for _,enemy in ipairs(enemies)do
		if self:getDangerousCard(enemy) then
			return enemy
        end
    end
	
	for _,friend in ipairs(friends)do
		if self:needToThrowArmor(friend) then
			return friend
		end
	end
	
	for _,friend in ipairs(friends)do
		if friend:hasSkill("kongcheng") and friend:getHandcardNum()==1 and self:getEnemyNumBySeat(self.player,friend)>0 and friend:getHp()<=2 then
			return friend
		end
	end
	
	for _,friend in ipairs(friends)do
		if (friend:hasSkill("zhiji") and friend:getMark("zhiji")==0) or (friend:hasSkill("mobilezhiji") and friend:getMark("mobilezhiji")==0) and
			friend:getHandcardNum()==1 and (self:getEnemyNumBySeat(self.player,friend)==0 or (not self:isWeak(friend) and self:getEnemyNumBySeat(self.player,friend)<=2)) then
			return friend
		end
	end
	
	for _,enemy in ipairs(enemies)do
		if self:getValuableCard(enemy) and self:doDisCard(enemy,"e") then
			return enemy
        end
	end
	
	for _,enemy in ipairs(enemies)do
		local cards = sgs.QList2Table(enemy:getHandcards())
		local flag = string.format("%s_%s_%s","visible",self.player:objectName(),enemy:objectName())
		if #cards<=2 and self:doDisCard(enemy,"h") then
			for _,cc in ipairs(cards)do
				if (cc:hasFlag("visible") or cc:hasFlag(flag)) and (cc:isKindOf("Peach") or cc:isKindOf("Analeptic") or cc:isKindOf("ExNihilo")) then
					return enemy
				end
            end
        end
	end
	
	for _,enemy in ipairs(enemies)do
		if self:doDisCard(enemy,"e") and self.player:canDiscard(enemy,"e") then
			return enemy
		end
	end
	
	self:sort(enemies,"handcard")
	for _,enemy in ipairs(enemies)do
       if self:doDisCard(enemy,"h") and self.player:canDiscard(enemy,"h") then
			return enemy
		end
	end
end

local shanxi_skill = {}
shanxi_skill.name = "shanxi"
table.insert(sgs.ai_skills,shanxi_skill)
shanxi_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@ShanxiCard=.")
end

sgs.ai_skill_use_func.ShanxiCard = function(card,use,self)
	local handcards = self.player:getHandcards()
    local cards = {}
    for _,c in sgs.qlist(handcards)do
        if c:isKindOf("BasicCard") and c:isRed() and not c:isKindOf("Peach") then
            if self.player:canDiscard(self.player,c:getEffectiveId()) then
                table.insert(cards,c)
            end
        end
    end
    if #cards==0 then return end
    self:sortByKeepValue(cards)
	
	if self:getOverflow()>0 then
		local no_one_inMyAttackRange = true
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if not self.player:canDiscard(p,"he") then continue end
			if self.player:inMyAttackRange(p) then
				no_one_inMyAttackRange = false
				break
			end
		end
		if no_one_inMyAttackRange then
			for _,p in ipairs(self.friends_noself)do
				if (p:hasSkill("shenxian") and p:getPhase()==sgs.Player_NotActive) or (p:hasSkill("olshenxian") and p:getPhase()==sgs.Player_NotActive and p:getMark("olshenxian")==0) then
					use.card = sgs.Card_Parse("@ShanxiCard="..cards[1]:getEffectiveId())
				end
				return
			end
		end
	end
	
	self.shanxi_target = getShanxiTarget(self)
	if not self.shanxi_target then return end
	use.card = sgs.Card_Parse("@ShanxiCard="..cards[1]:getEffectiveId())
end

sgs.ai_skill_playerchosen.shanxi = function(self,targets)
	if not self.shanxi_target or not targets:contains(self.shanxi_target) then
		local target = getShanxiTarget(self,targets)
		if target then return target end
		local enemies = {}
		for _,p in sgs.qlist(targets)do
			if not self:isFriend(p) then
				table.insert(enemies,p)
			end
		end
		self:sort(enemies)
		for _,p in ipairs(enemies)do
			if self:doDisCard(p,"he") then
				return p
			end
		end
		if #enemies>0 then return enemies[1] end
		return targets:at(math.random(0,targets:length()-1))
	end
	return self.shanxi_target
end

sgs.ai_use_priority.ShanxiCard = sgs.ai_use_priority.Slash+0.1

--下书
sgs.ai_skill_playerchosen.xiashu = function(self,targets)
	self:sort(self.enemies,"handcard")
	self:sort(self.friends_noself,"defense")
	self.xiashu_target = nil
	if (self.player:getHandcardNum()<3 and self:getCardsNum("Peach")==0 and self:getCardsNum("Jink")==0 and self:getCardsNum("Analeptic")==0) or
		(self.player:getHandcardNum()<=1 and self:getCardsNum("Peach")==0 and self:getCardsNum("Analeptic")==0) then
		local max_card_num = 0
		for _,enemy in ipairs(self.enemies)do
			max_card_num = math.max(max_card_num,enemy:getHandcardNum())
		end
		for _,enemy in ipairs(self.enemies)do
			if enemy:getHandcardNum()==max_card_num and enemy:getHandcardNum()>0 then
				self.xiashu_target = enemy
				return enemy
			end
		end
	else
		for _,friend in ipairs(self.friends_noself)do
			if not hasManjuanEffect(friend) and not self:needKongcheng(friend,true) then
				self.xiashu_target = friend
				return friend
			end
		end
	end
	return nil
end

sgs.ai_skill_discard.xiashu = function(self,discard_num,min_num,optional,include_equip)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByCardNeed(cards,true)
	local to_discard = {}
	local half_all_card_num = math.max(1,math.floor(self.player:getHandcardNum()/2))
	for i = 1,half_all_card_num,1 do
		table.insert(to_discard,cards[i]:getEffectiveId())
	end
	return to_discard
end

sgs.ai_skill_choice.xiashu = function(self,choices,data)
	local items = choices:split("+")
	if not self.xiashu_target then
		local items = choices:split("+")
		return items[math.random(1,#items)]
	end
	local ids = data:toIntList()
	local show_need,notshow_need = 0,0
	for _,id in sgs.qlist(ids)do
		show_need = show_need+self:cardNeed(sgs.Sanguosha:getCard(id))
	end
	local flag = string.format("%s_%s_%s","visible",self.player:objectName(),self.xiashu_target:objectName())
	for _,c in sgs.qlist(self.xiashu_target:getHandcards())do
		if ids:contains(c:getEffectiveId()) then continue end
		if c:hasFlag("visible") or c:hasFlag(flag) then
			notshow_need = notshow_need+self:cardNeed(c)
		else
			notshow_need = notshow_need+0.5
		end
	end
	if show_need>notshow_need then return "getshow" end
	if show_need<=notshow_need then return "getnotshow" end
	return items[math.random(1,#items)]
end

--宽释
sgs.ai_skill_playerchosen.kuanshi = function(self,targets)
	self:sort(self.friends,"defense")
	for _,friend in ipairs(self.friends)do
		return friend
	end
	return nil
end

--十周年宽释

sgs.ai_skill_playerchosen.tenyearkuanshi = function(self,targets)
	return sgs.ai_skill_playerchosen.kuanshi(self,targets)
end

--过论
local guolun_skill = {}
guolun_skill.name = "guolun"
table.insert(sgs.ai_skills,guolun_skill)
guolun_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@GuolunCard=.")
end

sgs.ai_skill_use_func.GuolunCard = function(card,use,self)
	self:sort(self.friends_noself,"handcard")
	self:sort(self.enemies,"handcard")
	self.guolun_target = nil

	for _,friend in ipairs(self.friends_noself)do
		if friend:getHandcardNum()>0 and not hasManjuanEffect(friend) then
			use.card = card
			self.guolun_target = friend
			use.to:append(friend)
			return
		end
	end
	for _,enemy in ipairs(self.enemies)do
		if enemy:getHandcardNum()>0 then
			use.card = card
			self.guolun_target = enemy
			use.to:append(enemy)
			return
		end
	end
end

sgs.ai_use_priority.GuolunCard = 7
sgs.ai_use_value.GuolunCard = 7

sgs.ai_skill_cardask["guolun-show"] = function(self,data)
	local id = data:toInt()
	if not id or id<0 or not self.guolun_target then return "." end
	local card = sgs.Sanguosha:getCard(id)
	if self:isFriend(self.guolun_target) then
		local cards = sgs.QList2Table(self.player:getCards("he"))
		self:sortByUseValue(cards,true)
		if #cards>0 then return "$"..cards[1]:getEffectiveId() end
	else
		local num = card:getNumber()
		local canshow = {}
		for _,c in sgs.qlist(self.player:getCards("he"))do
			if c:getNumber()<num then
				table.insert(canshow,c)
			end
		end
		if #canshow>0 then
			self:sortByUseValue(canshow,true)
			return "$"..canshow[1]:getEffectiveId()
		end
	end
	return "."
end

--送丧
sgs.ai_skill_invoke.songsang = true

--弼政
sgs.ai_skill_playerchosen.bizheng = function(self,targets)
	local friends = {}
	for _,p in ipairs(self.friends_noself)do
		if self:canDraw(p) and p:getHandcardNum()+2<=p:getMaxHp() then
			table.insert(friends,p)
		end
	end
	if #friends>0 then
		self:sort(friends)
		return friends[1]
	end
	for _,p in ipairs(self.friends_noself)do
		if self:canDraw(p) then
			table.insert(friends,p)
		end
	end
	if #friends>0 then
		self:sort(friends)
		return friends[1]
	end
	return nil
end

sgs.ai_skill_discard.bizheng = function(self,discard_num,min_num,optional,include_equip)
	return self:askForDiscard("dummyreason",2,2,false,true)
end

--佚典
sgs.ai_skill_use["@@yidian"] = function(self,prompt) -- extra target for Collateral
	local dummy_use = dummy()
	dummy_use.current_targets = self.player:property("extra_collateral"):toString():split("+")
	local card = sgs.Card_Parse(dummy_use.current_targets[1])
	self:useCardCollateral(card,dummy_use)
	if dummy_use.card and dummy_use.to:length()==2 then
		return "@ExtraCollateralCard=.->"..dummy_use.to:first():objectName().."+"..dummy_use.to:last():objectName()
	end
	return "."
end

sgs.ai_skill_playerchosen.yidian = function(self,targets)
	local use = self.player:getTag("YidianData"):toCardUse()
	local dummy_use = dummy(true,0,use.to)
	self:useCardByClassName(use.card,dummy_use)
	if dummy_use.card and dummy_use.to:length()>0 then
		return dummy_use.to:first()
	end
	return nil
end

--联翩
sgs.ai_skill_invoke.lianpian = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_playerchosen.lianpian = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"defense")
	for _,p in ipairs(targets)do
		if self:isFriend(p) and not self:needKongcheng(p,true) and not hasManjuanEffect(p) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if self:isFriend(p) and not self:needKongcheng(p,true) and not hasManjuanEffect(p,true) then
			return p
		end
	end
end

sgs.ai_playerchosen_intention.lianpian = -50

--观潮
sgs.ai_skill_invoke.guanchao = true

sgs.ai_skill_choice.guanchao = function(self,choices,data)
	local items = choices:split("+")
	local choice = items[math.random(1,#items)]
	if self.player:getHandcardNum()<2 then return choice end
	local handcards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByDynamicUsePriority(handcards)
	if handcards[1]:getNumber()<handcards[2]:getNumber() then
		return "up"
	elseif handcards[1]:getNumber()>handcards[2]:getNumber() then
		return "down"
	end
	return choice
end

--逊贤
sgs.ai_skill_playerchosen.xunxian = function(self,targets)
	local targets = sgs.QList2Table(targets)
	self:sort(targets,"defense")
	for _,p in ipairs(targets)do
		if self:isFriend(p) and not self:needKongcheng(p,true) and not hasManjuanEffect(p) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if self:isFriend(p) and not self:needKongcheng(p,true) and not hasManjuanEffect(p,true) then
			return p
		end
	end
	return nil
end

sgs.ai_playerchosen_intention.xunxian = -50

--诱敌
sgs.ai_skill_playerchosen.spyoudi = function(self,targets)
	local num = 0
	for _,c in sgs.qlist(self.player:getCards("h"))do
		if c:isKindOf("Peach") or c:isKindOf("Slash") or c:isKindOf("ExNihilo") then
			num = num+1
		end
	end
	if num>self.player:getHandcardNum()/2 then return nil end
	if self:getCardsNum("Jink")==1 and self:isWeak() and self.player:getHandcardNum()<3 then return nil end
	local targets = sgs.QList2Table(targets)
	self:sort(targets,"defense")
	for _,p in ipairs(targets)do
		if self:isEnemy(p) and not p:isNude() and self:doDisCard(p,"he") then
			return p
		end
	end
end

sgs.ai_playerchosen_intention.spyoudi = 20

--断发
local duanfa_skill = {}
duanfa_skill.name = "duanfa"
table.insert(sgs.ai_skills,duanfa_skill)
duanfa_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@DuanfaCard=.")
end

sgs.ai_skill_use_func.DuanfaCard = function(card,use,self)
	if self:needToThrowArmor() and self.player:getArmor() and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId()) and
		self.player:getArmor():isBlack() then
		use.card = sgs.Card_Parse("@DuanfaCard="..self.player:getArmor():getEffectiveId())
	end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	if self.player:hasSkills("youdi|spyoudi") then
		for _,c in ipairs(cards)do
			if c:isKindOf("Slash") and c:isBlack() then
				use.card = sgs.Card_Parse("@DuanfaCard="..c:getEffectiveId())
				return
			end
		end
	end
	for _,c in ipairs(cards)do
		if c:isBlack() and not (c:isKindOf("Lightning") and self:willUseLightning(c)) then
			use.card = sgs.Card_Parse("@DuanfaCard="..c:getEffectiveId())
			return
		end
	end
end

sgs.ai_use_priority.DuanfaCard = 0
sgs.ai_use_value.DuanfaCard = 2.61

--勤国
sgs.ai_skill_use["@@qinguo"] = function(self,prompt,method)
	local slash = dummyCard()
    slash:setSkillName("qinguo")
	local dummy_use = dummy()
	self:useCardSlash(slash,dummy_use)
	if dummy_use.card and dummy_use.to:length()>0 then
		local tos = {}
		for _,p in sgs.qlist(dummy_use.to)do
			table.insert(tos,p:objectName())
		end
		return "@QinguoCard=.->"..table.concat(tos,"+")
	end
	return "."
end

--札符
local zhafu_skill = {}
zhafu_skill.name = "zhafu"
table.insert(sgs.ai_skills,zhafu_skill)
zhafu_skill.getTurnUseCard = function(self,inclusive)
	if #self.enemies>0 then
		return sgs.Card_Parse("@ZhafuCard=.")
	end
end

sgs.ai_skill_use_func.ZhafuCard = function(card,use,self)
	self:sort(self.enemies,"handcard")
	use.card = card
	use.to:append(self.enemies[#self.enemies])
end

sgs.ai_skill_cardask["zhafu-keep"] = function(self,data)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
	return "$"..cards[#cards]:getEffectiveId()
end

--颂蜀
local songshu_skill = {}
songshu_skill.name = "songshu"
table.insert(sgs.ai_skills,songshu_skill)
songshu_skill.getTurnUseCard = function(self,inclusive)
	if self:needBear() then return end
	return sgs.Card_Parse("@SongshuCard=.")
end

sgs.ai_skill_use_func.SongshuCard = function(card,use,self)
	self:sort(self.friends_noself,"handcard")
	self:sort(self.enemies,"handcard")
	local cards = sgs.CardList()
	local peach = 0
	for _,c in sgs.qlist(self.player:getHandcards())do
		if isCard("Peach",c,self.player) and peach<2
		then peach = peach+1 else cards:append(c) end
	end
	local min_card = self:getMinCard(self.player,cards)
	local max_card = self:getMaxCard(self.player,cards)
	
	if min_card
	and min_card:getNumber()<7
	then
		for _,p in ipairs(self.friends_noself)do
			if p:hasSkill("kongcheng") and p:getHandcardNum()==1 then continue end
			if not self:canDraw(p) or not self.player:canPindian(p) then continue end
			use.card = sgs.Card_Parse("@SongshuCard=.")
			self.songshu_card = min_card
			use.to:append(p)
			return
		end
	end
	
	if max_card
	and max_card:getNumber()>=7
	then
		for _,p in ipairs(self.enemies)do
			if p:hasSkill("kongcheng") and p:getHandcardNum()==1 then continue end
			if not self:doDisCard(p,"h") or not self.player:canPindian(p) then continue end
			use.card = sgs.Card_Parse("@SongshuCard=.")
			self.songshu_card = max_card
			use.to:append(p)
			return
		end
			
		for _,p in ipairs(self.friends_noself)do
			if not (p:hasSkill("kongcheng") and p:getHandcardNum()==1) then continue end
			if not self.player:canPindian(p) then continue end
			use.card = sgs.Card_Parse("@SongshuCard=.")
			self.songshu_card = max_card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_priority.SongshuCard = 3

function sgs.ai_skill_pindian.songshu(dc,self,requestor,xc,nc)
	return xc or dc
end

--思辩
sgs.ai_skill_invoke.sibian = true

sgs.ai_skill_playerchosen.sibian = function(self,targets)
	local friends = {}
	for _,p in sgs.qlist(targets)do
		if self:isFriend(p) and self:canDraw(p)
		then table.insert(friends,p) end
	end
	if #friends>0 then
		self:sort(friends,"defense")
		return friends[1]
	end
end

--表召
sgs.ai_skill_cardask["biaozhao-put"] = function(self,data)
	if self.player:getArmor() and self:needToThrowArmor() then
		return "$"..self.player:getArmor():getEffectiveId()
	end
	local cards = {}
	for _,c in sgs.qlist(self.player:getCards("he"))do
		if c:isKindOf("Peach") or c:isKindOf("ExNihilo") or (c:isKindOf("Jink") and (self:isWeak() or self:getCardsNum("Jink")==1)) or
		(self:isWeak() and c:isKindOf("Analeptic")) or (self.player:getArmor() and c:getEffectiveId()==self.player:getArmor():getEffectiveId() and not self:needToThrowArmor()) or
			(c:isKindOf("WoodenOx") and not self.player:getPile("wooden_ox"):isEmpty())	then continue end
		table.insert(cards,c)
	end
	if #cards>0 then
		self:sortByKeepValue(cards)
		return "$"..cards[1]:getEffectiveId()
	end
	return "."
end

sgs.ai_skill_playerchosen.biaozhao = function(self,targets)
	local num = self.player:getHandcardNum()
	for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
		num = math.max(num,p:getHandcardNum())
	end
	num = math.max(num,5)
	
	local weak = {}
	for _,p in ipairs(self.friends)do
		if hasManjuanEffect(p) and not self:needKongcheng(p,true) and p:getHp()<=1 and self:isWeak(p) then
			table.insert(weak,p)
		end
	end
	if #weak>0 then
		self:sort(weak)
		return weak[1]
	end
	
	function biaozhaosort(a,b)
		local c1 = a:getLostHp()+math.max(num-a:getHandcardNum(),0)/2
		local c2 = b:getLostHp()+math.max(num-b:getHandcardNum(),0)/2
		if c1==c2 then
			return math.max(num-a:getHandcardNum(),0)>math.max(num-b:getHandcardNum(),0)
		end
		return c1>c2
	end
	
	local friends = {}
	for _,p in ipairs(self.friends)do
		if self:canDraw(p) then
			table.insert(friends,p)
		end
	end
	if #friends>0 then
		table.sort(friends,biaozhaosort)
		return friends[1]
	end
	table.sort(self.friends,biaozhaosort)
	return self.friends[1]
end

--业仇
sgs.ai_skill_playerchosen.yechou = function(self,targets)
	local targets = sgs.QList2Table(targets)
	local cu = self.room:getCurrent()
	local function Next(p)
		local n = 1
		local to = cu:getNextAlive()
		while to:objectName()~=p:objectName()do
			n = n+1
			to = to:getNextAlive()
		end
		return n
	end
	local func = function(a,b)
		return Next(a)>Next(b)
	end
	table.sort(targets,func)
	for _,p in ipairs(targets)do
		if self:isEnemy(p)
		and not hasBuquEffect(p)
		and Next(p)>=p:getHp()
		then return p end
	end
	for _,p in ipairs(targets)do
		if self:isEnemy(p)
		and Next(p)>=p:getHp()
		then return p end
	end
	for _,p in ipairs(targets)do
		if not self:isFriend(p)
		and Next(p)>=p:getHp()
		then return p end
	end
	self:sort(targets,"hp")
	for _,p in ipairs(targets)do
		if self:isEnemy(p)
		and not hasBuquEffect(p)
		then return p end
	end
	for _,p in ipairs(targets)do
		if self:isEnemy(p)
		then return p end
	end
	for _,p in ipairs(targets)do
		if not self:isFriend(p)
		then return p end
	end
	return nil
end

--观微
sgs.ai_skill_cardask["guanwei-invoke"] = function(self,data,pattern,target,target2)
	local player = data:toPlayer()
	if not player or not self:isFriend(player) then return "." end
	if self:needToThrowArmor() and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId()) then
		return "$"..self.player:getArmor():getEffectiveId()
	end
	if self.player:getHandcardNum()<2 and not self.player:hasSkill("kongcheng") then return "." end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	for _,c in ipairs(cards)do
		if not c:isKindOf("Peach") then
			return "$"..c:getEffectiveId()
		end
	end
	return "."
end

--浮海
--[[sgs.ai_cardshow.fuhai = function(self,requestor)
	local id = self.player:getTag("FuhaiID"):toInt()-1
	if id<0 then return self.player:getRandomHandCard() end
	if self.player:objectName()==requestor:objectName() then
		local now = self.player:getTag("FuhaiNow"):toPlayer()
		if not now then return self.player:getRandomHandCard() end
		local next_p = now:getNextAlive()
		local last_p = now:getNextAlive(self.room:alivePlayerCount()-1)
		
	
	else
		if self:isFriend(requestor) then
		
		
		else
		
		end
	end
end]]

--手杀浮海
local mobilefuhai_skill = {}
mobilefuhai_skill.name = "mobilefuhai"
table.insert(sgs.ai_skills,mobilefuhai_skill)
mobilefuhai_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@MobileFuhaiCard=.")
end

sgs.ai_skill_use_func.MobileFuhaiCard = function(card,use,self)
	use.card = card
end

sgs.ai_use_priority.MobileFuhaiCard = 7
sgs.ai_use_value.MobileFuhaiCard = 7

sgs.ai_skill_choice.mobilefuhai = function(self,choices,data)
	choices = choices:split("+")
	return choices[math.random(1,#choices)]
end



--问计
sgs.ai_skill_playerchosen.wenji = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"handcard")
	for _,p in ipairs(targets)do
		if self:isEnemy(p) and self:doDisCard(p,"he",true) then
			return p
		end
	end
	targets = sgs.reverse(targets)
	for _,p in ipairs(targets)do
		if self:isFriend(p) and self:doDisCard(p,"he",true) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if self:isFriend(p) and p:hasSkills(sgs.lose_equip_skill) and (p:getEquips():length()>1 or p:getPile("wooden_ox"):isEmpty()) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if self:isEnemy(p) and p:getCardCount()>0 then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if self:isFriend(p) and self:getOverflow(p)>0 then
			return p
		end
	end
end

sgs.ai_playerchosen_intention.wenji = function(self,from,to)
	if sgs.turncount<=1 then
		sgs.updateIntention(from,to,80)
	end
end

sgs.ai_skill_cardask["wenji-give"] = function(self,data,pattern,target,target2)
	local from = data:toPlayer()
	if self:needToThrowArmor() then return "$"..self.player:getArmor():getEffectiveId() end
	if self.player:hasSkills(sgs.lose_equip_skill) then
		local id = self:disEquip(true)
		if id then return "$"..id end
	end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	
	if self:isFriend(from) then
		local enemies = sgs.SPlayerList()
		for _,p in ipairs(self:getEnemies(from))do
			enemies:append(p)
		end
		
		self:sortByUseValue(cards)
		for _,c in ipairs(cards)do
			if c:isDamageCard() and not c:isKindOf("Lightning") and from:canUse(c,enemies) then
				return "$"..c:getEffectiveId()
			end
		end
		for _,c in ipairs(cards)do
			if from:canUse(c,enemies) then
				return "$"..c:getEffectiveId()
			end
		end
		if self.player:hasSkills(sgs.lose_equip_skill) then
			local id = self:disEquip(true)
			if id then return "$"..id end
		end
		for _,c in ipairs(cards)do
			if from:canUse(c) then
				return "$"..c:getEffectiveId()
			end
		end
		return "$"..cards[1]:getEffectiveId()
	else
		self:sortByUseValue(cards,true)
		for _,c in ipairs(cards)do
			if c:isKindOf("Peach") or (c:isDamageCard() and not c:isKindOf("Lightning")) then continue end
			if not from:canUse(c) then
				return "$"..c:getEffectiveId()
			end
		end
		for _,c in ipairs(cards)do
			if c:isKindOf("Peach") or (c:isDamageCard() and not c:isKindOf("Lightning")) then continue end
			return "$"..c:getEffectiveId()
		end
		return "$"..cards[1]:getEffectiveId()
	end
	return "."
end

--屯江
sgs.ai_skill_invoke.tunjiang = function(self,data)
	return self:canDraw()
end

--掠命
local lveming_skill = {}
lveming_skill.name = "lveming"
table.insert(sgs.ai_skills,lveming_skill)
lveming_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@LvemingCard=.")
end

sgs.ai_skill_use_func.LvemingCard = function(card,use,self)
	self:sort(self.friends_noself,"defense")
	for _,friend in ipairs(self.friends_noself)do
		if friend:getEquips():length()<self.player:getEquips():length() then
			if ((friend:isNude() and not friend:isAllNude() and not friend:containsTrick("YanxiaoCard")) or
				(friend:isKongcheng() and friend:getJudgingArea():isEmpty() and self:needToThrowArmor(friend) and friend:getEquips():length()==1) or
				(friend:getJudgingArea():isEmpty() and friend:getEquips():isEmpty() and self:needToThrowLastHandcard(friend))) then
				use.card = card
				use.to:append(friend) 
				return
			end
		end
	end
	self:sort(self.enemies,"defense")
	self.enemies = sgs.reverse(self.enemies)
	for _,enemy in ipairs(self.enemies)do
		if enemy:getEquips():length()<self.player:getEquips():length() and not enemy:isNude() and self:doDisCard(enemy,"he",true) then
			use.card = card
			use.to:append(enemy) 
			return
		end
	end
	for _,enemy in ipairs(self.enemies)do
		if enemy:getEquips():length()<self.player:getEquips():length() and enemy:isNude() then
			use.card = card
			use.to:append(enemy) 
			return
		end
	end
end

sgs.ai_use_priority.LvemingCard = 7
sgs.ai_use_value.LvemingCard = 7

sgs.ai_card_intention.LvemingCard = function(self,card,from,tos)
	local to = tos[1]
	if ((to:isNude() and not to:isAllNude() and not to:containsTrick("YanxiaoCard")) or
		(to:isKongcheng() and to:getJudgingArea():isEmpty() and self:needToThrowArmor(to) and to:getEquips():length()==1) or
		(to:getJudgingArea():isEmpty() and to:getEquips():isEmpty() and self:needToThrowLastHandcard(to))) then
		sgs.updateIntention(from,to,-80)
	else
		sgs.updateIntention(from,to,80)
	end
end

sgs.ai_skill_choice.lveming = function(self,choices,data)
	local items = choices:split("+")
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if p:hasSkill("zhenyi") and p:getMark("@flziwei")>0 and self:isEnemy(p) then
			local zhenyi_items = {}
			for _,item in ipairs(items)do
				if tonumber(item)~=5 then
					table.insert(zhenyi_items,item)
				end
			end
			return zhenyi_items[math.random(1,#zhenyi_items)]
		end
	end
	return items[math.random(1,#items)]
end

--屯军
function getTunjunEquipNum(player)
	local num = 0
	for i = 0,4 do
		if player:hasEquipArea(i) and not player:getEquip(i) then
			num = num+1
		end
	end
	return num
end

local compareByEquipNum = function(a,b)
	return getTunjunEquipNum(a)>getTunjunEquipNum(b)
end

local tunjun_skill = {}
tunjun_skill.name = "tunjun"
table.insert(sgs.ai_skills,tunjun_skill)
tunjun_skill.getTurnUseCard = function(self,inclusive)
	local friends = {}
	for _,p in ipairs(self.friends)do
		if p:hasEquipArea() then
			table.insert(friends,p)
		end
	end
	if #friends==0 then return end
	table.sort(friends,compareByEquipNum)
	
	self.tunjun_target = nil
	for _,p in ipairs(friends)do
		if self:isWeak(p) and ((p:hasEquipArea(1) and not p:getEquip(1)) or (p:hasEquipArea(2) and not p:getEquip(2))) then
			self.tunjun_target = p
			return sgs.Card_Parse("@TunjunCard=.")
		end
	end
	
	if self.player:getMark("&lveming")>=getTunjunEquipNum(friends[1]) then
		return sgs.Card_Parse("@TunjunCard=.")
	end
	if self.player:getMark("&lveming")>2 then
		return sgs.Card_Parse("@TunjunCard=.")
	end
end

sgs.ai_skill_use_func.TunjunCard = function(card,use,self)
	if self.tunjun_target then
		use.card = card
		use.to:append(self.tunjun_target) 
		return
	end
	
	local friends = {}
	for _,p in ipairs(self.friends)do
		if p:hasEquipArea() then
			table.insert(friends,p)
		end
	end
	table.sort(friends,compareByEquipNum)
	for _,friend in ipairs(friends)do
		if getTunjunEquipNum(friend)>=self.player:getMark("&lveming") then
			use.card = card
			use.to:append(friend) 
			return
		end
	end
	for _,friend in ipairs(friends)do
		if friend:hasSkills(sgs.lose_equip_skill) and not friend:getEquips():isEmpty() then
			use.card = card
			use.to:append(friend) 
			return
		end
	end
	for _,friend in ipairs(friends)do
		if friend:hasSkills(sgs.lose_equip_skill) then
			use.card = card
			use.to:append(friend) 
			return
		end
	end
	for _,friend in ipairs(friends)do
		use.card = card
		use.to:append(friend) 
		return
	end
end

sgs.ai_use_priority.TunjunCard = sgs.ai_use_priority.LvemingCard-0.1
sgs.ai_use_value.TunjunCard = 7
sgs.ai_card_intention.TunjunCard = -80

--散文
sgs.ai_skill_invoke.sanwen = function(self,data)
	local ids = data:toIntList()
	for _,id in sgs.qlist(ids)do
		if sgs.Sanguosha:getCard(id):isKindOf("Peach") and self:isWeak(self.friends) then
			return false
		end
	end
	return true
end

--七哀
sgs.ai_skill_invoke.qiai = true

sgs.ai_skill_cardask["qiai-give"] = function(self,data,pattern,target,target2)
	local player = data:toPlayer()
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	if self:isFriend(player) then
		for _,c in ipairs(cards)do
			if isCard("Peach",c,player) or isCard("Analeptic",c,player) then
				return "$"..c:getEffectiveId()
			end
		end
	else
		for _,c in ipairs(cards)do
			if isCard("Peach",c,player) or isCard("Analeptic",c,player) then continue end
			return "$"..c:getEffectiveId()
		end
	end
	return "$"..cards[1]:getEffectiveId()
end

--登楼
sgs.ai_skill_invoke.denglou = true

sgs.ai_skill_use["@@denglou!"] = function(self,prompt,method)
	local ids = self.player:property("denglou_ids"):toString():split("+")
	local cards = {}
	for _,id in ipairs(ids)do
		table.insert(cards,sgs.Sanguosha:getCard(tonumber(id)))
	end
	
	self:sortByDynamicUsePriority(cards)
	if cards[1]:targetFixed() and self.player:canUse(cards[1]) then
		return "@DenglouCard="..cards[1]:getEffectiveId()
	elseif not cards[1]:targetFixed() then
		local dummy_use = dummy()
		self:useCardByClassName(cards[1],dummy_use)
		if dummy_use.card and dummy_use.to:length()>0 then
			local tos = {}
			for _,p in sgs.qlist(dummy_use.to)do
				table.insert(tos,p:objectName())
			end
			return "@DenglouCard="..cards[1]:getEffectiveId().."->"..table.concat(tos,"+")
		end
	end
	return "."
end

--备战
sgs.ai_skill_playerchosen.beizhan = function(self,targets)
	local friends = {}
	for _,p in ipairs(self.friends)do
		if self:canDraw(p) and math.min(5,p:getMaxHp())-p:getHandcardNum()>0 then table.insert(friends,p) end
	end
	if #friends>0 then
		local beizhansort = function(a,b)
			local c1 = math.max(0,math.min(5,a:getMaxHp())-a:getHandcardNum())
			local c2 = math.max(0,math.min(5,b:getMaxHp())-b:getHandcardNum())
			return c1>c2
		end
		table.sort(friends,beizhansort)
		return friends[1]
	end
	for _,p in ipairs(self.enemies)do
		if math.min(5,p:getMaxHp())-p:getHandcardNum()<=0 or hasManjuanEffect(p) then
			return p
		end
	end
	for _,p in ipairs(self.enemies)do
		if self:needKongcheng(p,true) and math.min(5,p:getMaxHp())-p:getHandcardNum()<=1 and self:getEnemyNumBySeat(self.player,p,p)>0 then
			return p
		end
	end
	return nil
end

--守邺
sgs.ai_skill_invoke.mobileshouye = function(self,data)
	local use = self.player:getTag("mobileshouyeForAI"):toCardUse()
	if not use then return false end
	if self:isFriend(use.from) then return false end
	if use.card:isKindOf("GlobalEffect") or use.card:isKindOf("Peach") or use.card:isKindOf("ExNihilo") or use.card:isKindOf("Analeptic") then return false end
	return true
end

sgs.ai_skill_choice.mobileshouye = function(self,choices,data)
	choices = choices:split("+")
	return choices[math.random(1,#choices)]
end

--烈直
sgs.ai_skill_use["@@mobileliezhi"] = function(self,prompt)
	local targets = self:findPlayerToDiscard("hej",false,true)
	if #targets>0 then
		local tos = {}
		for i = 1,math.min(2,#targets)do
			table.insert(tos,targets[i]:objectName())
		end
		return "@MobileLiezhiCard=.->"..table.concat(tos,"+")
	end
	return "."
end

--悍勇
sgs.ai_skill_invoke.hanyong = function(self,data)
	local use = data:toCardUse()
	local earnings = 0
	local need = nil
	if use.card:isKindOf("SavageAssault") then need = "Slash"
	elseif use.card:isKindOf("ArcheryAttack") then need = "Jink" end
	if not need then return false end
	
	for _,enemy in ipairs(self.enemies)do
		if self:hasTrickEffective(use.card,enemy,from) and not enemy:hasArmorEffect("Vine") and self:damageIsEffective(enemy,sgs.DamageStruct_Normal,self.player) and
			getCardsNum(need,enemy,self.player)==0 then
			earnings = earnings+1
			if self:isWeak(enemy) then
				earnings = earnings+1
			end
			if self:hasEightDiagramEffect(enemy) and need=="Jink" then
				earnings = earnings-1
			end
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if not friend:hasArmorEffect("Vine") and self:hasTrickEffective(use.card,friend,from) and self:damageIsEffective(friend,sgs.DamageStruct_Normal,self.player) and
			getCardsNum(need,friend,self.player)==0 then
			earnings = earnings-1
			if self:isWeak(friend) then
				earnings = earnings-1
			end
			if self:hasEightDiagramEffect(friend) and need=="Jink" then
				earnings = earnings+1
			end
		else
			earnings = earnings+1
		end
	end
	return earnings>=0
end

--十周年悍勇
sgs.ai_skill_invoke.tenyearhanyong = function(self,data)
	local use = data:toCardUse()
	local can = self.player:getHp()<=self.room:getTag("TurnLengthCount"):toInt()
	if use.card:isKindOf("Slash")
	then
		return not self:isFriend(use.to:at(0))
		and (use.to:at(0):getHandcardNum()<2 or can)
	else
		for _,fp in sgs.list(self.friends_noself)do
			if self:isWeak(fp)
			then return false end
		end
		return can or self.player:getHp()>self.player:getMaxHp()/2
	end
end



--狼袭
sgs.ai_skill_playerchosen.langxi = function(self,targets)
	local tos = self:findPlayerToDamage(2,self.player,nil,targets)
	if #tos>0 then
		for _,p in ipairs(tos)do
			if self:cantDamageMore(self.player,p) or self:isFriend(p) then continue end
			return p
		end
		for _,p in ipairs(tos)do
			if self:isFriend(p) then continue end
			return p
		end
	end
	self:sort(self.enemies,"hp")
	for _,enemy in ipairs(self.enemies)do
		if enemy:getHp()<=self.player:getHp()
		and self:damageIsEffective(enemy)
		and not self:cantDamageMore(self.player,enemy)
		then return enemy end
	end
	for _,enemy in ipairs(self.enemies)do
		if enemy:getHp()<=self.player:getHp()
		and self:damageIsEffective(enemy)
		then return enemy end
	end
end

--亦算
sgs.ai_skill_invoke.yisuan = function(self,data)
	local card = self.player:getTag("yisuanForAI"):toCard()
	if not card or self.player:getMaxHp()<=3 then return false end
	if card:isKindOf("AOE") and self:getAoeValue(card)>0 then return true end
	if card:isKindOf("Duel") and self:willUse(self.player,card) then return true end
	if card:isKindOf("ExNihilo") and self:getOverflow()<=-2 and self.player:getLostHp()>0 and self:canDraw() then return true end
	if (card:isKindOf("Snatch") or card:isKindOf("Dismantlement")) and self:willUse(self.player,card) and self.player:getLostHp()>0 then return true end
	return false
end

--兴乱
sgs.ai_skill_invoke.xingluan = true

--贪狈
local tanbei_skill = {}
tanbei_skill.name = "tanbei"
table.insert(sgs.ai_skills,tanbei_skill)
tanbei_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@TanbeiCard=.")
end

sgs.ai_skill_use_func.TanbeiCard = function(card,use,self)
	self:sort(self.friends_noself)
	for _,friend in ipairs(self.friends_noself)do
		if ((friend:isNude() and not friend:isAllNude() and not friend:containsTrick("YanxiaoCard")) or
			(friend:isKongcheng() and friend:getJudgingArea():isEmpty() and self:needToThrowArmor(friend) and friend:getEquips():length()==1) or
			(friend:getJudgingArea():isEmpty() and friend:getEquips():isEmpty() and self:needToThrowLastHandcard(friend))) then
			use.card = card
			use.to:append(friend) 
			return
		end
	end
	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if not self:doDisCard(enemy,"hej",true) then continue end
		use.card = card
		use.to:append(enemy) 
		return
	end
end

sgs.ai_use_priority.TanbeiCard = sgs.ai_use_priority.Slash-0.1
sgs.ai_use_value.TanbeiCard = 7

sgs.ai_card_intention.TanbeiCard = function(self,card,from,tos)
	local to = tos[1]
	if ((to:isNude() and not to:isAllNude() and not to:containsTrick("YanxiaoCard")) or
		(to:isKongcheng() and to:getJudgingArea():isEmpty() and self:needToThrowArmor(to) and to:getEquips():length()==1) or
		(to:getJudgingArea():isEmpty() and to:getEquips():isEmpty() and self:needToThrowLastHandcard(to))) then
		sgs.updateIntention(from,to,-80)
	else
		sgs.updateIntention(from,to,80)
	end
end

sgs.ai_skill_choice.tanbei = function(self,choices,data)
	local from = data:toPlayer()
	if not from or from:isDead() then return "get" end
	if self:isFriend(from) then return "get" end
	if ((self.player:isNude() and not self.player:isAllNude() and not self.player:containsTrick("YanxiaoCard")) or
		(self.player:isKongcheng() and self.player:getJudgingArea():isEmpty() and self:needToThrowArmor(self.player) and self.player:getEquips():length()==1) or
		(self.player:getJudgingArea():isEmpty() and self.player:getEquips():isEmpty() and self:needToThrowLastHandcard(self.player))) then
		return "get"
	end
	local slash = dummyCard()
	local slash_num = getCardsNum("Slash",from,self.player)
	if slash_num>0 and self:slashIsEffective(slash,self.player,from) and self:damageIsEffective(self.player,DamageStruct_Normal,from) and
		self:getCardsNum("Jink")<slash_num then
		return "get"
	end
	return "nolimit"
end

--伺盗
sgs.ai_skill_use["@@sidao"] = function(self,prompt,method)
	local target_name = prompt:split(":")[2]
	if not target_name then return "." end
	local cards = sgs.QList2Table(self.player:getCards("h"))
	for _,id in sgs.qlist(self.player:getHandPile())do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
	self:sortByUseValue(cards,true)
	local sp = sgs.SPlayerList()
	local to = self.room:findPlayerByObjectName(target_name)
	if not to or to:isDead() then return "." end
	sp:append(to)
	for _,c in ipairs(cards)do
		if c:isKindOf("Peach") and self:isWeak(self.friends) then continue end
		local snatch = dummyCard("snatch")
        snatch:setSkillName("sidao")
        snatch:addSubcard(c)
		if not self.player:canUse(snatch,sp) then continue end
		if c:isKindOf("Snatch") and self.player:canUse(c,sp) then continue end
		local dummy_use = dummy()
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if p:objectName()~=target_name then
				table.insert(dummy_use.current_targets,p)
			end
		end
		self:aiUseCard(snatch,dummy_use)
		if dummy_use.card and dummy_use.to:length()>0 then
			return "@SidaoCard="..c:getEffectiveId()
		end
	end
end

--荐杰
local jianjie_skill = {}
jianjie_skill.name = "jianjie"
table.insert(sgs.ai_skills,jianjie_skill)
jianjie_skill.getTurnUseCard = function(self,inclusive)
	for _,friend in ipairs(self.friends)do
		if friend:getMark("&dragon_signet")>0 and friend:getMark("&phoenix_signet")>0 and friend:getHandcardNum()>=4 then
			return
		end
	end
	return sgs.Card_Parse("@JianjieCard=.")
end

sgs.ai_skill_use_func.JianjieCard = function(card,use,self)
	local dragon_player = {}
	local phoenix_player = {}
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if p:getMark("&dragon_signet")>0 then
			table.insert(dragon_player,p)
		end
		if p:getMark("&phoenix_signet")>0 then
			table.insert(phoenix_player,p)
		end
	end
	
	local no_dragon_friend_players = {}
	local no_phoenix_friend_players = {}
	self:sort(self.friends,"handcard")
	self.friends = sgs.reverse(self.friends)
	for _,friend in ipairs(self.friends)do
		if friend:getMark("&dragon_signet")==0 and friend:faceUp() then
			table.insert(no_dragon_friend_players,friend)
		end
		if friend:getMark("&phoenix_signet")==0 and friend:faceUp() then
			table.insert(no_phoenix_friend_players,friend)
		end
	end
	if #dragon_player>0 and #no_dragon_friend_players>0 then
		if self:isEnemy(dragon_player[1]) and dragon_player[1]:objectName()~=no_dragon_friend_players[1]:objectName() then
			use.card = card
			use.to:append(dragon_player[1])
			use.to:append(no_dragon_friend_players[1])
			return
		end
	end
	if #phoenix_player>0 and #no_phoenix_friend_players>0 then
		if self:isEnemy(phoenix_player[1]) and phoenix_player[1]:objectName()~=no_phoenix_friend_players[1]:objectName() then
			use.card = card
			use.to:append(phoenix_player[1])
			use.to:append(no_phoenix_friend_players[1])
			return
		end
	end
	
	if #dragon_player>0 and #no_dragon_friend_players>0 and dragon_player[1]:getHandcardNum()<=2 and no_dragon_friend_players[1]:getHandcardNum()>2 then
		if self:isFriend(dragon_player[1]) and dragon_player[1]:objectName()~=no_dragon_friend_players[1]:objectName() then
			use.card = card
			use.to:append(dragon_player[1])
			use.to:append(no_dragon_friend_players[1])
			return
		end
	end
	if #phoenix_player>0 and #no_phoenix_friend_players>0 and phoenix_player[1]:getHandcardNum()<=2 and no_phoenix_friend_players[1]:getHandcardNum()>2 then
		if self:isFriend(phoenix_player[1]) and phoenix_player[1]:objectName()~=no_phoenix_friend_players[1]:objectName() then
			use.card = card
			use.to:append(phoenix_player[1])
			use.to:append(no_phoenix_friend_players[1])
			return
		end
	end
end

sgs.ai_use_priority.JianjieCard = 7
sgs.ai_use_value.JianjieCard = 7

sgs.ai_skill_choice.jianjie = function(self,choices,data)
	local items = choices:split("+")
	return items[math.random(1,#items)]
end

function SmartAI:GetAskForPeachActionOrderSeat(player)
	local another_seat = {}
	player = player or self.player
	local nextAlive = self.room:getCurrent()
	for i = 1,self.room:alivePlayerCount(),1 do
		table.insert(another_seat,nextAlive)
		nextAlive = nextAlive:getNextAlive()
	end
	for i = 1,#another_seat,1 do
		if another_seat[i]:objectName()==player:objectName()
		then return i end
	end
	return -1
end

sgs.ai_skill_use["@@jianjie!"] = function(self,prompt,method)
	self:sort(self.friends_noself,"handcard")
	self.friends_noself = sgs.reverse(self.friends_noself)
	local targets = {}
	for _,friend in ipairs(self.friends_noself)do
		if #targets<2 then
			table.insert(targets,friend:objectName())
		end
	end
	local compareBySeat = function(a,b)
		local player_a = self.room:findPlayerByObjectName(a)
		local player_b = self.room:findPlayerByObjectName(b)
		return self:GetAskForPeachActionOrderSeat(player_a)>self:GetAskForPeachActionOrderSeat(player_b)
	end
	local unknown_num = 0
	for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if sgs.ai_role[p:objectName()]=="neutral" then
			unknown_num = unknown_num+1
		end
	end
	if #targets==1 then
		if unknown_num>0 then
			for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
				if sgs.ai_role[p:objectName()]=="neutral" and #targets<2 then
					table.insert(targets,p:objectName())
				end
			end
		else
			table.insert(targets,self.player:objectName())
		end
		table.sort(targets,compareBySeat)
		return "@JianjieCard=.->"..table.concat(targets,"+")
	elseif #targets==2 then
		table.sort(targets,compareBySeat)
		return "@JianjieCard=.->"..table.concat(targets,"+")
	else
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if sgs.ai_role[p:objectName()]=="neutral" and #targets<2 then
				table.insert(targets,p:objectName())
			end
		end
		if #targets==1 then
			table.insert(targets,self.player:objectName())
			table.sort(targets,compareBySeat)
			return "@JianjieCard=.->"..table.concat(targets,"+")
		elseif #targets==2 then
			table.sort(targets,compareBySeat)
			return "@JianjieCard=.->"..table.concat(targets,"+")
		else
			table.insert(targets,self.player:objectName())
			for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
				if #targets<2 then
					table.insert(targets,p:objectName())
				end
			end
			table.sort(targets,compareBySeat)
			return "@JianjieCard=.->"..table.concat(targets,"+")
		end
	end
	return "."
end

sgs.ai_skill_playerchosen.jianjie_dragon = function(self,targets)
	self:sort(self.friends_noself,"handcard")
	self.friends_noself = sgs.reverse(self.friends_noself)
	for _,p in ipairs(self.friends_noself)do
		if p:isAlive() and p:getMark("&phoenix_signet")>0 then
			return p
		end
	end
	--if #self.friends_noself>0 then return self.friends_noself[1] end
	for _,p in ipairs(self.friends_noself)do
		if p:isAlive() then
			return p
		end
	end
	return self.player
end

sgs.ai_skill_playerchosen.jianjie_phoenix = function(self,targets)
	self:sort(self.friends_noself,"handcard")
	self.friends_noself = sgs.reverse(self.friends_noself)
	for _,p in ipairs(self.friends_noself)do
		if p:isAlive() and p:getMark("&dragon_signet")>0 then
			return p
		end
	end
	--if #self.friends_noself>0 then return self.friends_noself[1] end
	for _,p in ipairs(self.friends_noself)do
		if p:isAlive() then
			return p
		end
	end
	return self.player
end

--荐杰火计
addAiSkills("jianjiehuoji").getTurnUseCard = function(self)
	local cards = self:addHandPile("he")
	cards = self:sortByKeepValue(cards,nil,true)
  	for _,c in sgs.list(cards)do
	   	if c:isRed() then
			local fs = dummyCard("fire_attack")
			fs:setSkillName("jianjiehuoji")
			fs:addSubcard(c)
			if fs:isAvailable(self.player) then
				self.dummy_use = self:aiUseCard(fs)
				if self.dummy_use.card then
					return sgs.Card_Parse("@JianjieHuojiCard="..c:toString())
				end
			end
		end
	end
end

sgs.ai_skill_use_func["JianjieHuojiCard"] = function(card,use,self)
	if self.dummy_use.to:length()>0 then
		use.card = card
		use.to = self.dummy_use.to
	end
end

--荐杰连环
addAiSkills("jianjielianhuan").getTurnUseCard = function(self)
	local cards = self:addHandPile("he")
	cards = self:sortByKeepValue(cards,nil,true)
  	for _,c in sgs.list(cards)do
	   	if c:getSuit()==1 then
			local fs = dummyCard("iron_chain")
			fs:setSkillName("jianjielianhuan")
			fs:addSubcard(c)
			if fs:isAvailable(self.player) then
				self.dummy_use = self:aiUseCard(fs)
				if self.dummy_use.card then
					return sgs.Card_Parse("@JianjieLianhuanCard="..c:toString())
				end
			end
		end
	end
end

sgs.ai_skill_use_func["JianjieLianhuanCard"] = function(card,use,self)
	if self.dummy_use.to then
		use.card = card
		use.to = self.dummy_use.to
	end
end

--荐杰业炎
addAiSkills("jianjieyeyan").getTurnUseCard = function(self)
	local give,ids,can = {},{},0
    local cards = self.player:getCards("h")
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
	for _,c in sgs.list(cards)do
		if table.contains(give,c:getSuit()) then continue end
    	table.insert(ids,c:getEffectiveId())
    	table.insert(give,c:getSuit())
	end
	for _,p in sgs.list(self.enemies)do
		if self:isWeak(p) then can = can+1 end
		if p:getHp()<2 then can = can+2 end
	end
	if can<#self.enemies+1 then return end
	if #ids<4 then return sgs.Card_Parse("@SmallJianjieYeyanCard=.")
	elseif #self.friends>1 or self.player:getHp()>3
	then return sgs.Card_Parse("@GreatJianjieYeyanCard="..table.concat(ids,"+")) end
end

sgs.ai_skill_use_func["SmallJianjieYeyanCard"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	for _,to in sgs.list(self.enemies)do
		use.card = card
		use.to:append(to)
	   	if use.to:length()>=3
		then return end
	end
	for _,to in sgs.list(self.room:getOtherPlayers(self.player))do
		if not self:isFriend(to)
		and not use.to:contains(to)
		then
			use.card = card
			use.to:append(to)
		   	if use.to:length()>=3
			then return end
		end
	end
end

sgs.ai_use_value.SmallJianjieYeyanCard = 3.4
sgs.ai_use_priority.SmallJianjieYeyanCard = 2.4

sgs.ai_skill_use_func["GreatJianjieYeyanCard"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	local n = 0
	for _,to in sgs.list(self.enemies)do
		if n<2
		then
			for i=1,to:getHp()do
		    	if use.to:length()>=3
				then return end
				use.to:append(to)
				use.card = card
			end
			n = n+1
		end
	end
	for i=1,5 do
		for _,to in sgs.list(self.enemies)do
			if n<2
			then
				if use.to:length()>=3
				then return end
				use.card = card
				use.to:append(to)
				n = n+1
			end
		end
	end
end

sgs.ai_use_value.SmallJianjieYeyanCard = 3.4
sgs.ai_use_priority.SmallJianjieYeyanCard = 2.4

--称好

sgs.ai_skill_invoke.chenghao = function(self)
	return true
end

sgs.ai_skill_askforyiji.chenghao = function(self,card_ids,tos)
	return sgs.ai_skill_askforyiji.nosyiji(self,card_ids,tos)
end

sgs.ai_use_revises.yinshi = function(self,card,use)
	if self.player:getMark("&dragon_signet")+self.player:getMark("&phoenix_signet")<1
	and card:isKindOf("Armor")
	then return false end
end

sgs.ai_target_revises.yinshi = function(to,card)
	if not to:getArmor()
	and to:getMark("&dragon_signet")+to:getMark("&phoenix_signet")<1
	and card:isDamageCard()
	then
    	if card:isKindOf("NatureSlash")
		or card:isKindOf("TrickCard")
		then return true end
	end
end

sgs.ai_can_damagehp.yinshi = function(self,from,card,to)--类卖血技能决策
    if not to:getArmor()
	and to:getMark("&dragon_signet")+to:getMark("&phoenix_signet")<1
	then --先判断是否可以隐士
    	if card --再判断是否是牌的伤害
		then
			if card:isKindOf("NatureSlash")
			then --隐士受到属性杀时不闪
				if self:canLoseHp(from,card,to)--规避掉一些特殊技能，例如绝情，来保证是会造成伤害
				then return true end
			elseif card:isKindOf("TrickCard")
			and card:isDamageCard()
			then --隐士受到伤害锦囊时不响应
				if self:canLoseHp(from,card,to)
				then return true end
			end
		end
	end
end

--袭营
sgs.ai_skill_cardask["xiying-invoke"] = function(self,data,pattern,prompt)
    if #self.enemies>0
	and self.player:getHandcardNum()>3
	then return true end
	return "."
end

--二版袭营
sgs.ai_skill_cardask["secondxiying-invoke"] = function(self,data,pattern,prompt)
    if #self.enemies>0
	and self.player:getHandcardNum()>3
	then return true end
	return "."
end

--乱战  --未测试
sgs.ai_skill_use["@@luanzhan"] = function(self,prompt,method)
	if self.player:hasFlag("luanzhan_now_use_collateral")
	then
		local card = sgs.Card_Parse(self.player:property("extra_collateral"):toString())
		if not card then return "." end
		local dummy_use = dummy()
		dummy_use.current_targets = self.player:property("extra_collateral_current_targets"):toString():split("+")
		self:useCardCollateral(card,dummy_use)
		if dummy_use.card and dummy_use.to:length()==2
		then
			return "@ExtraCollateralCard=.->"..dummy_use.to:first().."+"..dummy_use.to:last()
		end
	else
		local use = self.player:getTag("luanzhanData"):toCardUse()
		if not use then return "." end
		local n = self.player:getMark("luanzhan_target_num-Clear")
		local friends = {}
		for _,p in ipairs(self.friends_noself)do
			if not p:hasFlag("luanzhan_canchoose") then continue end
			table.insert(friends,p)
		end
		n = math.min(n,#friends)
		if n==0 then return "." end
		local extra = {}
		if use.card:isKindOf("ExNihilo")
		then
			self:sort(friends,"defense")
			for _,p in ipairs(friends)do
				if self:canDraw(p) and #extra<n
				then
					table.insert(extra,p:objectName())
				end
			end
			if #extra>0 then
				return "@LuanzhanCard=.->"..table.concat(extra,"+")
			end
		else
			
		end
	end
	return "."
end

sgs.ai_skill_use["@@luanzhan"] = function(self,prompt,method)
	if self.player:hasFlag("olluanzhan_now_use_collateral")
	then
		local card = sgs.Card_Parse(self.player:property("extra_collateral"):toString())
		if not card then return "." end
		local dummy_use = dummy()
		dummy_use.current_targets = self.player:property("extra_collateral_current_targets"):toString():split("+")
		self:useCardCollateral(card,dummy_use)
		if dummy_use.card and dummy_use.to:length()==2
		then
			return "@ExtraCollateralCard=.->"..dummy_use.to:first().."+"..dummy_use.to:last()
		end
	else
		local use = self.player:getTag("olluanzhanData"):toCardUse()
		if not use then return "." end
		local n = self.player:getMark("olluanzhan_target_num-Clear")
		local friends = {}
		for _,p in ipairs(self.friends_noself)do
			if p:hasFlag("olluanzhan_canchoose")
			then table.insert(friends,p) end
		end
		n = math.min(n,#friends)
		if n==0 then return "." end
		local extra = {}
		if use.card:isKindOf("ExNihilo")
		then
			self:sort(friends,"defense")
			for _,p in ipairs(friends)do
				if self:canDraw(p) and #extra<n
				then
					table.insert(extra,p:objectName())
				end
			end
			if #extra>0 then
				return "@OLLuanzhanCard=.->"..table.concat(extra,"+")
			end
		else
			
		end
	end
	return "."
end

--内伐
sgs.ai_skill_use["@@neifa"] = function(self,prompt)
	local destlist = self.room:getAlivePlayers()
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist,"hp")
	for _,p in sgs.list(destlist)do
		if self:isFriend(p) and self:doDisCard(p,"ej")
		then return ("@NeifaCard=.->"..p:objectName()) end
	end
	for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and self:doDisCard(p,"ej")
		then return ("@NeifaCard=.->"..p:objectName()) end
	end
	for _,p in sgs.list(destlist)do
		if not self:isFriend(p) and self:doDisCard(p,"ej")
		then return ("@NeifaCard=.->"..p:objectName()) end
	end
	return ("@NeifaCard=.")
end

sgs.ai_skill_choice.neifa = function(self,choices,data)
	self.neifa_use = data:toCardUse()
	local items = choices:split("+")
	local targets = sgs.SPlayerList()
	if table.contains(items,"add")
	then
		for _,p in sgs.list(self.room:getAllPlayers())do
			if self.player:isProhibited(p,self.neifa_use.card)
			or self.neifa_use.to:contains(p)
			then continue end
			targets:append(p)
		end
		self.player:setTag("yb_zhuzhan2_data",data)
		local to = sgs.ai_skill_playerchosen.yb_zhuzhan2(self,targets)
		if to then self.neifa_to = to return "add" end
	end
	if table.contains(items,"remove")
	then
		self.player:setTag("yb_fujia2_data",data)
		local to = sgs.ai_skill_playerchosen.yb_fujia2(self,self.neifa_use.to)
		if to then self.neifa_to = to return "remove" end
	end
	return items[#items]
end

sgs.ai_skill_playerchosen.neifa = function(self,players)
    for _,target in sgs.list(players)do
		if target:objectName()==self.neifa_to:objectName()
		then return target end
	end
end

sgs.ai_skill_use["@@neifa!"] = function(self,prompt)
	local destlist = self.room:getOtherPlayers(self.player)
    destlist = self:sort(destlist,"hp")
	local c = sgs.Card_Parse(self.player:property("extra_collateral"):toString())
	local valid = self.player:property("extra_collateral_current_targets"):toStringList()
	for _,to in sgs.list(destlist)do
		if table.contains(valid,to:objectName()) then continue end
		if self:isEnemy(to) and c:isDamageCard() and CanToCard(c,self.player,to)
		then return ("@ExtraCollateralCard=.->"..to:objectName()) end
	end
end

--锋略
function fenglveJudge(player,self)
	if player:getJudgingArea():isEmpty() then return true end
	if player:getJudgingArea():length()==1 and player:containsTrick("YanxiaoCard") then return true end
	local lightning = dummyCard("lightning")
	if player:getJudgingArea():length()==1 and player:containsTrick("lightning") and not self:willUseLightning(lightning) then return true end
	return false
end

sgs.ai_skill_playerchosen.fenglve = function(self,targets)
	local cards = sgs.CardList()
	local peach = 0
	for _,c in sgs.qlist(self.player:getHandcards())do
		if isCard("Peach",c,self.player) and peach<2
		then peach = peach+1 else cards:append(c) end
	end
	local max_card = self:getMaxCard(self.player,cards)
	if not max_card then return nil end
	local max_point = max_card:getNumber()
	if self.player:hasSkill("tianbian") and max_card:getSuit()==sgs.Card_Heart then max_point = 13 end
	if max_point>=7 then
		self.fenglve_card = max_card:getId()
		
		for _,p in ipairs(self.friends_noself)do
			if self:getOverflow(p)>2 and not p:containsTrick("YanxiaoCard") and p:containsTrick("indulgence")
			then return p end
		end
		for _,p in ipairs(self.friends_noself)do
			if not p:containsTrick("YanxiaoCard") and p:containsTrick("lightning") then
				local lightning = dummyCard("lightning")
				if not self:willUseLightning(lightning)
				then return p end
			end
		end
		
		local enemies = {}
		for _,p in ipairs(self.enemies)do
			if not fenglveJudge(p,self) then continue end
			if not p:getEquips():isEmpty() and self:doDisCard(p,"e",true)
			then table.insert(enemies,p) end
		end
		if #enemies>0 then
			self:sort(enemies,"handcard")
			for _,p in ipairs(enemies)do
				if p:getHandcardNum()>1 and self:doDisCard(p,"h",true)
				then return p end
			end
			for _,p in ipairs(enemies)do
				if self:doDisCard(p,"h",true)
				then return p end
			end
		end
		
		self:sort(self.enemies,"handcard")
		for _,p in ipairs(self.enemies)do
			if not fenglveJudge(p,self) then continue end
			if p:getHandcardNum()>1 and self:doDisCard(p,"h",true) and not (not p:getEquips():isEmpty() and self:doDisCard(p,"e",true))
			then return p end
		end
		for _,p in ipairs(self.enemies)do
			if not fenglveJudge(p,self) then continue end
			if self:doDisCard(p,"h",true) and not (not p:getEquips():isEmpty() and self:doDisCard(p,"e",true))
			then return p end
		end
	end
end

sgs.ai_playerchosen_intention.fenglve = function(self,from,to)
	if self:getOverflow(to)>2 and not to:containsTrick("YanxiaoCard") and to:containsTrick("indulgence") then
		sgs.updateIntention(from,to,-80)
	elseif not to:containsTrick("YanxiaoCard") and to:containsTrick("lightning") then
		sgs.updateIntention(from,to,-80)
	else
		sgs.updateIntention(from,to,80)
	end
end

function sgs.ai_skill_pindian.fenglve(minusecard,self,requestor)
	local maxcard = self:getMaxCard()
	return self:isFriend(requestor) and self:getMinCard() or ( maxcard:getNumber()<6 and  minusecard or maxcard )
end

sgs.ai_skill_use["@@fenglve!"] = function(self,prompt,method)
	local give = {}
	if not self.player:isKongcheng() then
		local hands = sgs.QList2Table(self.player:getCards("h"))
		self:sortByUseValue(hands,true)
		table.insert(give,hands[1]:getEffectiveId())
	end
	if not self.player:getEquips():isEmpty() then
		local id = self:disEquip(false,true)
		if id then table.insert(give,id)
		else
			local equips = self.player:getEquipsId()
			id = equips:at(math.random(0,equips:length()-1))
			table.insert(give,id)
		end
	end
	if not self.player:getJudgingArea():isEmpty() then
		if self.player:getJudgingArea():length()==1 then  --不单独列出来，直接self:askForCardChosen游戏会崩
			table.insert(give,self.player:getJudgingAreaID():first())
		else
			local id = self:askForCardChosen(self.player,"j","snatch")
			if id<0 then id = self.player:getJudgingAreaID():first() end
			table.insert(give,id)
		end
	end
	return "@FenglveCard="..table.concat(give,"+")
end

--谋识
local moushi_skill = {}
moushi_skill.name = "moushi"
table.insert(sgs.ai_skills,moushi_skill)
moushi_skill.getTurnUseCard = function(self,inclusive)
	if #self.friends_noself>0 and not self.player:isKongcheng() then
		return sgs.Card_Parse("@MoushiCard=.")
	end
end

sgs.ai_skill_use_func.MoushiCard = function(card,use,self)
	self:sort(self.friends_noself)
	local cards,slashs = sgs.QList2Table(self.player:getCards("h")),{}
	self:sortByUseValue(cards,true)
	if cards[1]:isKindOf("Analeptic") and self:isWeak() then return end
	if cards[1]:isKindOf("Jink") and self:isWeak() and self:getCardsNum("Jink")==1 then return end
	for _,c in ipairs(cards)do
		if c:isKindOf("Slash") then
			table.insert(slashs,c)
		end
	end
	local id = -1
	if #slashs>0 then id = slashs[1]:getEffectiveId() else id = cards[1]:getEffectiveId() end
	
	if #slashs>0 then
		for _,p in ipairs(self.friends_noself)do
			if hasManjuanEffect(p) or self:needKongcheng(p,true) or p:hasSkills("jueqing|gangzhi") then continue end
			if not self:canUse(sgs.Sanguosha:getCard(id),self.enemies,p) then continue end
			use.card = sgs.Card_Parse("@MoushiCard="..id)
			use.to:append(p) 
			return
		end
	end
	
	if self:getOverflow()>=0 then
		sgs.ai_use_priority.MoushiCard = 0
		if #slashs>0 then
			for _,p in ipairs(self.friends_noself)do
				if hasManjuanEffect(p) or self:needKongcheng(p,true) then continue end
				if not self:canUse(sgs.Sanguosha:getCard(id),self.enemies,p) then continue end
				use.card = sgs.Card_Parse("@MoushiCard="..id)
				use.to:append(p) 
				return
			end
		end
		
		for _,p in ipairs(self.friends_noself)do
			if hasManjuanEffect(p) or self:needKongcheng(p,true) then continue end
			use.card = sgs.Card_Parse("@MoushiCard="..id)
			use.to:append(p) 
			return
		end
	end
end

sgs.ai_use_priority.MoushiCard = sgs.ai_use_priority.Slash-0.1
sgs.ai_use_value.MoushiCard = 5

sgs.ai_card_intention.MoushiCard = function(self,card,from,tos)
	local to = tos[1]
	local intention = -70
	if hasManjuanEffect(to) then
		intention = 0
	elseif self:needKongcheng(to,true) then
		intention = 30
	end
	sgs.updateIntention(from,to,intention)
end

addAiSkills("tenyearfenglve").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		if c:getNumber()>9
		then
			return sgs.Card_Parse("@TenyearFenglveCard=.")
		end
	end
end

sgs.ai_skill_use_func["TenyearFenglveCard"] = function(card,use,self)
	self:sort(self.enemies,"card",true)
	for _,ep in sgs.list(self.enemies)do
		if self.player:canPindian(ep)
		and ep:getCardCount(true,true)>1
		and ep:getCards("j"):length()<1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if self.player:canPindian(ep)
		and ep:getCardCount(true,true)>1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	self:sort(self.friends_noself,"card",true)
	for _,ep in sgs.list(self.friends_noself)do
		if self.player:canPindian(ep)
		and self:doDisCard(ep,"ej")
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.TenyearFenglveCard = 9.4
sgs.ai_use_priority.TenyearFenglveCard = 4.8

sgs.ai_skill_use["@@tenyearfenglve!"] = function(self,prompt)
	local valid = {}
    local cards = self.player:getCards("j")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if #valid>1 then break end
    	table.insert(valid,h:getEffectiveId())
	end
	cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if #valid>1 then break end
    	table.insert(valid,h:getEffectiveId())
	end
	return #valid>0 and string.format("@TenyearFenglveGiveCard=%s",table.concat(valid,"+"))
end

sgs.ai_skill_cardask["@anyong-discard"] = function(self,data)
    local damage = data:toDamage()
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if self:isEnemy(damage.to)
    	then return h:getEffectiveId() end
	end
    return "."
end


--击虚
local jixu_skill = {}
jixu_skill.name = "jixu"
table.insert(sgs.ai_skills,jixu_skill)
jixu_skill.getTurnUseCard = function(self,inclusive)
	if self:getOverflow()<=1 then
		sgs.ai_use_priority.JixuCard = sgs.ai_use_priority.Indulgence-1
		sgs.ai_use_value.JixuCard = sgs.ai_use_value.Indulgence-1
	else
		sgs.ai_use_priority.JixuCard = sgs.ai_use_priority.Slash-1
		sgs.ai_use_value.JixuCard = sgs.ai_use_value.Slash-1
	end
	return sgs.Card_Parse("@JixuCard=.")
end

sgs.ai_skill_use_func.JixuCard = function(card,use,self)
	self:sort(self.enemies)
	if #self.enemies==0 then return end
	use.card = card
	local target_hp = self.enemies[1]:getHp()
	for _,enemy in ipairs(self.enemies)do
		if enemy:getHp()==target_hp then
			use.to:append(enemy)
		end
	end
end

sgs.ai_card_intention.JixuCard = 10

sgs.ai_skill_choice.jixu = function(self,choices,data)
	local source = data:toPlayer()
	if not source then
		choices = choices:split("+")
		return choices[math.random(1,#choices)]
	end
	if source:isKongcheng() then return "not" end
	local know = 0
	local flag = string.format("%s_%s_%s","visible",self.player:objectName(),source:objectName())
	for _,c in sgs.qlist(source:getCards("h"))do
		if (c:hasFlag("visible") or c:hasFlag(flag)) then
			know = know+1
			if c:isKindOf("Slash") then
				return "has"
			end
		end
	end
	local handnum = source:getHandcardNum()-know
	if handnum>=3 then return "has" end
	return "not"
end

--戡难
local kannan_skill = {}
kannan_skill.name = "kannan"
table.insert(sgs.ai_skills,kannan_skill)
kannan_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@KannanCard=.")
end

sgs.ai_skill_use_func.KannanCard = function(card,use,self)
	local cards = sgs.CardList()
	local peach = 0
	for _,c in sgs.qlist(self.player:getHandcards())do
		if isCard("Peach",c,self.player) and peach<2 then
			peach = peach+1
		else
			cards:append(c)
		end
	end
	
	local min_card = self:getMinCard(self.player,cards)
	if min_card then
		local min_point = min_card:getNumber()
		if self.player:hasSkill("tianbian") and min_card:getSuit()==sgs.Card_Heart then min_point = 13 end
		if min_point<7 then
			self:sort(self.friends_noself,"handcard")
			self.friends_noself = sgs.reverse(self.friends_noself)
			for _,p in ipairs(self.friends_noself)do
				if p:getMark("kannan_target-PlayClear")>0 or not self.player:canPindian(p) then continue end
				if not self:needToThrowLastHandcard(p) then continue end
				self.kannan_card = min_card
				use.card = sgs.Card_Parse("@KannanCard=.")
				use.to:append(p) 
				return
			end
			for _,p in ipairs(self.friends_noself)do
				if p:getMark("kannan_target-PlayClear")>0 or not self.player:canPindian(p) then continue end
				self.kannan_card = min_card
				use.card = sgs.Card_Parse("@KannanCard=.")
				use.to:append(p) 
				return
			end
		end
	end
	
	local max_card = self:getMaxCard(self.player,cards)
	if max_card then
		local max_point = max_card:getNumber()
		if self.player:hasSkill("tianbian") and max_card:getSuit()==sgs.Card_Heart then max_point = 13 end
		if max_point>=7 then
			self:sort(self.enemies,"handcard")
			for _,p in ipairs(self.enemies)do
				if p:getMark("kannan_target-PlayClear")>0 or not self.player:canPindian(p) or not self:doDisCard(p,"h",true) then continue end
				self.kannan_card = max_card
				use.card = sgs.Card_Parse("@KannanCard=.")
				use.to:append(p) 
				return
			end
		end
	end
end

sgs.ai_use_priority.KannanCard = 7
sgs.ai_use_value.KannanCard = 7

function sgs.ai_skill_pindian.kannan(minusecard,self,requestor)
	return self:isFriend(requestor) and self:getMaxCard() or ( self:getMinCard():getNumber()>6 and  minusecard or self:getMinCard() )
end

--集军
sgs.ai_skill_invoke.jijun = true

--方统
sgs.ai_skill_cardask["fangtong-invoke"] = function(self,data)
	return "."
end

sgs.ai_skill_use["@@fangtong!"] = function(self,prompt,method)
	return "."
end

--奋钺
addAiSkills("fenyue").getTurnUseCard = function(self)
	local cards = sgs.QList2Table(self.player:getCards("h"))
	self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		if c:getNumber()>9
		then
			return sgs.Card_Parse("@FenyueCard=.")
		end
	end
end

sgs.ai_skill_use_func["FenyueCard"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	for _,ep in sgs.list(self.enemies)do
		if self.player:canPindian(ep)
		then
			use.card = card
			use.to:append(ep) 
			return
		end
	end
end

sgs.ai_use_value.FenyueCard = 9.4
sgs.ai_use_priority.FenyueCard = 4.8


--截刀
sgs.ai_skill_invoke.jiedao = function(self,data)
	local to = data:toPlayer()
	return self:isEnemy(to) and not self:cantDamageMore(self.player,to)
end

sgs.ai_skill_choice.jiedao = function(self,choices,data)
	--[[local damage = data:toDamage()
	local to = damage.to
	choices = choices:split("+")
	if to:getHp()-damage.damage<tonumber(choices[1]) then return choices[1] end
	if to:getHp()-damage.damage>tonumber(choices[#choices]) then return choices[#choices] end
	return ""..to:getHp()]]
	choices = choices:split("+")
	return choices[#choices]
end

sgs.ai_skill_discard.jiedao = function(self,discard_num,min_num,optional,include_equip)
	return self:askForDiscard("dummyreason",min_num,min_num,false,true)
end

--虚猲
sgs.ai_skill_invoke.xuhe = function(self,data)
	if self.player:getMaxHp()<=3 then return false end
	local num = 0
	self.xuhe_choice = nil
	local can_dis = false
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if self.player:distanceTo(p)<=1 then
			if self.player:canDiscard(p,"he") then
				can_dis = true
				if self:isFriend(p) then
					num = num+1
					if self:doDisCard(p,"he") then
						num = num-2
					end
				elseif self:isEnemy(p) then
					num = num-1
					if self:doDisCard(p,"he") then
						num = num+2
					end
				end
			end
		end
	end
	if num<=0 and can_dis then 
		sgs.ai_skill_choice.xuhe = "discard"
		return true
	end
	
	num = 0
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if self.player:distanceTo(p)<=1 then
			if self:isFriend(p) and self:canDraw(p) then num = num+1
			elseif self:isEnemy(p) and self:canDraw(p) then num = num-1 end
		end
	end
	if num>=0 then
		sgs.ai_skill_choice.xuhe = "draw"
		return true
	end
	return false
end

--利熏
sgs.ai_skill_discard.lixun = function(self,discard_num,min_num,optional,include_equip)
	return self:askForDiscard("dummy",discard_num,discard_num,false,false)
end

--馈珠
sgs.ai_skill_playerchosen.spkuizhu = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"handcard")
	targets = sgs.reverse(targets)
	if math.min(5,targets[1]:getHandcardNum())<=self.player:getHandcardNum() then return nil end
	for _,p in ipairs(targets)do
		if self:isFriend(p) then
			return p
		end
	end
	return nil
end

sgs.ai_skill_use["@@spkuizhu"] = function(self,prompt)
	local hands = sgs.QList2Table(self.player:getCards("h"))
	local name = prompt:split(":")[2]
	if not name then return "." end
	local from = self.room:findPlayerByObjectName(name)
	if not from or from:isDead() then return "." end
	--[[local piles = {}
	for _,id in sgs.qlist(self.player:getPile("#spkuizhu"))do  --认为是empty
		table.insert(piles,sgs.Sanguosha:getCard(id))
	end]]
	local piles = sgs.QList2Table(from:getCards("h"))
	
	if #hands==0 or #piles==0 then return "." end
	
	local exchange_pile = {}
	local exchange_handcard = {}
	self:sortByCardNeed(hands)
	self:sortByCardNeed(piles)
	local max_num = math.min(#hands,#piles)
	for i = 1 ,max_num,1 do
		if self:cardNeed(piles[#piles])>self:cardNeed(hands[1]) then
			table.insert(exchange_handcard,piles[#piles])
			table.insert(exchange_pile,hands[1])
			table.removeOne(piles,piles[#piles])
			table.removeOne(hands,hands[1])
		else
			break
		end
	end
	if #exchange_handcard==0 then return "." end
	local exchange = {}

	for _,c in ipairs(exchange_handcard)do
		table.insert(exchange,c:getId())
	end

	for _,c in ipairs(exchange_pile)do
		table.insert(exchange,c:getId())
	end
	
	return "@SpKuizhuCard="..table.concat(exchange,"+")
end

--义襄
sgs.ai_skill_invoke.yixiang = function(self,data)
	return self:canDraw()
end

--揖让
sgs.ai_skill_playerchosen.yirang = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"maxhp")
	targets = sgs.reverse(targets)
	for _,p in ipairs(targets)do
		if self:canDraw(p) and self:isFriend(p) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if hasManjuanEffect(p) then
			return p
		end
	end
	return nil
end

--评才
local pingcai_skill = {}
pingcai_skill.name = "pingcai"
table.insert(sgs.ai_skills,pingcai_skill)
pingcai_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@PingcaiCard=.")
end

sgs.ai_skill_use_func.PingcaiCard = function(card,use,self)
	use.card = card
end

sgs.ai_use_priority.PingcaiCard = 10
sgs.ai_use_value.PingcaiCard = 5

function pingcaiMoveArmor(self)
	local friends = {}
	for _,p in ipairs(self.friends)do
		if not p:hasEquipArea(1) or p:getArmor() then continue end
		table.insert(friends,p)
	end
	if #friends==0 then return {} end
	self:sort(friends)
	self:sort(self.friends_noself)
	self:sort(self.enemies)
	
	local pingcai = {}
	
	for _,p in ipairs(self.friends_noself)do
		if p:getArmor() and self:needToThrowArmor(p) then
			table.insert(pingcai,p)
			for _,q in ipairs(friends)do
				if q:objectName()~=p:objectName() and q:hasSkills(sgs.need_equip_skill.."|"..sgs.lose_equip_skill) then
					table.insert(pingcai,q)
					return pingcai
				end
			end
			for _,q in ipairs(friends)do
				if q:objectName()~=p:objectName() then
					table.insert(pingcai,q)
					return pingcai
				end
			end
		end
	end
	
	for _,p in ipairs(self.friends_noself)do
		if p:getArmor() and self:hasSkills(sgs.lose_equip_skill,p) then
			table.insert(pingcai,p)
			for _,q in ipairs(friends)do
				if q:objectName()~=p:objectName() and q:hasSkills(sgs.need_equip_skill.."|"..sgs.lose_equip_skill) then
					table.insert(pingcai,q)
					return pingcai
				end
			end
			for _,q in ipairs(friends)do
				if q:objectName()~=p:objectName() then
					table.insert(pingcai,q)
					return pingcai
				end
			end
		end
	end
	
	for _,p in ipairs(self.enemies)do
		if p:getArmor() and self:doDisCard(p,"e") then
			table.insert(pingcai,p)
			for _,q in ipairs(friends)do
				if q:hasSkills(sgs.need_equip_skill.."|"..sgs.lose_equip_skill) then
					table.insert(pingcai,q)
					return pingcai
				end
			end
			table.insert(pingcai,friends[1])
			return pingcai
		end
	end
	
	return pingcai
end

sgs.ai_skill_choice.pingcai = function(self,choices)
	choices = choices:split("+")
	if table.contains(choices,"pcxuanjian") then
		for _,p in ipairs(self.friends)do
			if self:isWeak(p) and p:getLostHp()>0
			and not self:needKongcheng(p,true)
			then return "pcxuanjian" end
		end
	end
	if table.contains(choices,"pcwolong") then
		for _,p in ipairs(self.enemies)do
			if self:damageIsEffective(p,sgs.DamageStruct_Fire,self.player)
			and self:hasHeavyDamage(self.player,nil,p,"F")
			then return "pcwolong" end
		end
	end
	if table.contains(choices,"pcxuanjian") then
		for _,p in ipairs(self.enemies)do
			if p:getLostHp()>0
			and self:needKongcheng(p,true)
			and not hasManjuanEffect(p)
			and self:getEnemyNumBySeat(self.player,p,p)>0
			then return "pcxuanjian" end
		end
	end
	if table.contains(choices,"pcfengchu")
	and #choices>1 then
		local fengchu = 0
		for _,p in ipairs(self.enemies)do
			if not p:isChained()
			and not p:hasSkill("qianjie")
			then fengchu = fengchu+1 end
		end
		if fengchu<2 then table.removeOne(choices,"pcfengchu") end
	end
	if table.contains(choices,"pcshuijing")
	and #choices>1 then
		local shuijing = false
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if string.find(sgs.Sanguosha:translate(p:getGeneralName()),"司马徽")
			or string.find(sgs.Sanguosha:translate(p:getGeneral2Name()),"司马徽")
			then shuijing = true break end
		end
		if shuijing then
			local from,card,to = self:moveField(nil,"e")
			if not from or not card or not to then
				table.removeOne(choices,"pcshuijing")
			end
		else
			if #pingcaiMoveArmor(self)==0 then
				table.removeOne(choices,"pcshuijing")
			end
		end
	end
	return choices[math.random(1,#choices)]
end

sgs.ai_skill_playerschosen.pcwolong = function(self,targets,x)
	local tos = self:findPlayerToDamage(1,self.player,sgs.DamageStruct_Fire,targets)
	self:sort(tos)
	local tos2 = {}
	for _,p in ipairs(tos)do
		if #tos2<x and not self:isFriend(p)
		then table.insert(tos2,p) end
	end
	self:sort(self.enemies)
	for _,p in ipairs(self.enemies)do
		if #tos2<x and not table.contains(tos2,p)
		then table.insert(tos2,p) end
	end
	return tos2
end

sgs.ai_skill_playerschosen.pcfengchu = function(self,targets,x)
	local tos = {}
	self:sort(self.enemies)
	for _,p in ipairs(self.enemies)do
		if not p:isChained() and #tos<x and not p:hasSkill("qianjie")
		then table.insert(tos,p) end
	end
	return tos
end

sgs.ai_card_intention.PingcaiFengchuCard = 30

sgs.ai_skill_playerchosen.pcxuanjian = function(self,targets)
	self:sort(self.friends,"hp")
	for _,p in ipairs(self.friends)do
		if p:getLostHp()>0 and self:isWeak(p) and not self:needKongcheng(p,true)
		then return p end
	end
	for _,p in ipairs(self.friends)do
		if p:getLostHp()>0 and not self:needKongcheng(p,true) and not hasManjuanEffect(p)
		then return p end
	end
	for _,p in ipairs(self.friends)do
		if p:getLostHp()>0 and not self:needKongcheng(p,true)
		then return p end
	end
	for _,p in ipairs(self.enemies)do
		if self:needKongcheng(p,true) and not hasManjuanEffect(p)
		and not p:isWounded()
		then return p end
	end
	for _,p in ipairs(self.friends)do
		if p:getLostHp()>0 then return p end
	end
	return self.player
end

sgs.ai_playerchosen_intention.pcxuanjian = function(self,from,to)
	local intention = -50
	if to:getLostHp()==0 and self:needKongcheng(to,true) and not hasManjuanEffect(to)
	then intention = 50 end
	sgs.updateIntention(from,to,intention)
end

sgs.ai_skill_playerchosen.pingcai_from = function(self,targets)
	local from,card,to = self:moveField(nil,"e")
	if from then return from end
end

sgs.ai_skill_cardchosen.pingcai = function(self,who,flags)
	local from,card,to = self:moveField(nil,"e")
	if card then return card end
end

sgs.ai_skill_playerchosen.pingcai_to = function(self,targets)
	local from,card,to = self:moveField(nil,"e")
	if to then return to end
end

sgs.ai_skill_playerchosen.pingcai_shuijing_from = function(self,targets)
	local pingcai = pingcaiMoveArmor(self)
	if #pingcai>1 then return pingcai[1] end
	return targets:at(math.random(0,targets:length()-1))
end

sgs.ai_skill_playerchosen.pingcai_shuijing_to = function(self,targets)
	local pingcai = pingcaiMoveArmor(self)
	if #pingcai>1 then return pingcai[2] end
	return targets:at(math.random(0,targets:length()-1))
end

--持节
sgs.ai_skill_invoke.chijiec = true

sgs.ai_skill_choice.chijiec = function(self,choices)
	if self.room:getLord() and self.player:isYourFriend(self.room:getLord()) then return self.room:getLord():getKingdom() end
	choices = choices:split(":")
	return choices[math.random(1,#choices)]
end

--外使
local waishi_skill = {}
waishi_skill.name = "waishi"
table.insert(sgs.ai_skills,waishi_skill)
waishi_skill.getTurnUseCard = function(self,inclusive)
	if not self.player:isNude() then
		return sgs.Card_Parse("@WaishiCard=.")
	end
end

sgs.ai_skill_use_func.WaishiCard = function(card,use,self)
	local kingdoms = {}
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if not table.contains(kingdoms,p:getKingdom()) then
			table.insert(kingdoms,p:getKingdom())
		end
	end
	local cards = sgs.QList2Table(self.player:getCards("he"))
    self:sortByKeepValue(cards)
	local equip = 0
	local give = {}
	for _,c in ipairs(cards)do
		if #give>=#kingdoms then break end
		if c:isKindOf("Peach") or c:isKindOf("ExNihilo") or (c:isKindOf("Jink") and self:getCardsNum("Jink")==1) or (c:isKindOf("Analeptic") and self:isWeak()) then continue end
		table.insert(give,c:getEffectiveId())
		if self.player:getCards("e"):contains(c) then equip = equip+1 end
	end
	if #give==0 then return end
	
	local enemies = {}
    for _,enemy in ipairs(self.enemies)do
        if hasManjuanEffect(enemy,true) and self:doDisCard(enemy,"h",true)
		and not self:needToThrowLastHandcard(enemy,math.min(#give,enemy:getHandcardNum()))
		and enemy:getKingdom()==self.player:getKingdom()
		then table.insert(enemies,friend) end
    end
    if #enemies>0 then
		self:sort(enemies)
		local give_cards = {}
		for i = 1,math.min(#give,enemies[1]:getHandcardNum())do
			table.insert(give_cards,give[i])
		end
		if #give_cards>0 then
			use.card = sgs.Card_Parse("@WaishiCard="..table.concat(give_cards,"+"))
			use.to:append(enemies[1]) 
			return
		end
	end
	for _,enemy in ipairs(self.enemies)do
        if hasManjuanEffect(enemy,true) and self:doDisCard(enemy,"h",true) and
			not self:needToThrowLastHandcard(enemy,math.min(#give,enemy:getHandcardNum())) then
            table.insert(enemies,friend)
        end
    end
    if #enemies>0 then
		self:sort(enemies)
		local give_cards = {}
		for i = 1,math.min(#give,enemies[1]:getHandcardNum())do
			table.insert(give_cards,give[i])
		end
		if #give_cards>0 then
			use.card = sgs.Card_Parse("@WaishiCard="..table.concat(give_cards,"+"))
			use.to:append(enemies[1]) 
			return
		end
	end
	
	self:sort(self.enemies,"handcard")
	self.enemies = sgs.reverse(self.enemies)
	for _,enemy in ipairs(self.enemies)do
		if not hasManjuanEffect(enemy,true) and self:doDisCard(enemy,"h",true)
		and (enemy:getKingdom()==self.player:getKingdom() or enemy:getHandcardNum()+equip>self.player:getHandcardNum())
		and enemy:getHandcardNum()>=#give
		then
			use.card = sgs.Card_Parse("@WaishiCard="..table.concat(give,"+"))
			use.to:append(enemy) 
			return
		end
	end
	
	self:sort(self.friends_noself,"handcard")
	self.friends_noself = sgs.reverse(self.friends_noself)
	for _,friend in ipairs(self.friends_noself)do
		if not hasManjuanEffect(friend) and self:doDisCard(friend,"h",true)
		and (friend:getKingdom()==self.player:getKingdom() or friend:getHandcardNum()+equip>self.player:getHandcardNum())
		and friend:getHandcardNum()>=#give
		then
			use.card = sgs.Card_Parse("@WaishiCard="..table.concat(give,"+"))
			use.to:append(friend) 
			return
		end
	end
	
	for _,enemy in ipairs(self.enemies)do
		if not hasManjuanEffect(enemy,true) and not self:doDisCard(enemy,"h",true)
		and enemy:getHandcardNum()>=#give
		then
			use.card = sgs.Card_Parse("@WaishiCard="..table.concat(give,"+"))
			use.to:append(enemy) 
			return
		end
	end
	
	for _,friend in ipairs(self.friends_noself)do
		if not hasManjuanEffect(friend) and self:doDisCard(friend,"h",true)
		and friend:getHandcardNum()>=#give
		then
			use.card = sgs.Card_Parse("@WaishiCard="..table.concat(give,"+"))
			use.to:append(friend) 
			return
		end
	end
end

sgs.ai_use_priority.WaishiCard = 0
sgs.ai_use_value.WaishiCard = 2

--忍涉
sgs.ai_skill_choice.renshe = function(self,choices)
	local new_choices = {}
	choices = choices:split("+")
	for _,choice in ipairs(choices)do
		if choice=="change" then continue end
		table.insert(new_choices,choice)
	end
	if self:canDraw() and self:findPlayerToDraw(false,1) then
		return new_choices[math.random(1,#new_choices)]
	end
	return "extra"
end

sgs.ai_skill_choice.renshe_change = function(self,choices)
	choices = choices:split("+")
	if self.room:getLord() and self.player:isYourFriend(self.room:getLord()) and table.contains(choices,self.room:getLord():getKingdom()) then
		return self.room:getLord():getKingdom()
	end
	return choices[math.random(1,#choices)]
end

sgs.ai_skill_playerchosen.renshe = function(self,targets)
	local target = self:findPlayerToDraw(false,1)
	if target then return target end
	self:sort(self.friends_noself)
	for _,p in ipairs(self.friends_noself)do
		if self:canDraw(p) then
			return p
		end
	end
	for _,p in ipairs(self.friends_noself)do
		if not hasManjuanEffect(p) and not self:needKongcheng(p,true) then
			return p
		end
	end
	for _,p in ipairs(self.friends_noself)do
		if not self:needKongcheng(p,true) then
			return p
		end
	end
	return self.friends_noself[math.random(1,#self.friends_noself)]
end

--血卫
sgs.ai_skill_playerchosen.xuewei = function(self,targets)
	if self:isWeak() and self:getCardsNum("Peach")+self:getCardsNum("Analeptic")<=0 then return nil end
	self:sort(self.friends_noself,"hp")
	for _,p in ipairs(self.friends_noself)do
		if not self:isWeak(p) then continue end
		return p
	end
	return nil
end

--烈斥
sgs.ai_skill_discard.liechi = function(self,discard_num,min_num,optional,include_equip)
	return self:askForDiscard("dummy",1,1,false,true)
end

--执义
sgs.ai_skill_choice.zhiyi = function(self,choices,data)
	local c = data:toCard()
	if (c:isKindOf("Peach") and self.player:getLostHp()<2) or (c:isKindOf("Analeptic") and self:canDraw()) then
		return "draw"
	end
	local card = dummyCard(c:objectName())
	card:setSkillName("_zhiyi")
	local dummyuse = dummy()
	self:useCardByClassName(card,dummyuse)
	if dummyuse.card then
		return "use"
	end
	return "draw"
end

sgs.ai_skill_use["@@zhiyi!"] = function(self,prompt,method)
	local name = prompt:split(":")[2]
	if not name then return "." end
	local card = dummyCard(name)
	card:setSkillName("_zhiyi")
	local dummyuse = dummy()
	self:useCardByClassName(card,dummyuse)
	if dummyuse.card then
		local tos = {}
		for _,p in sgs.qlist(dummyuse.to)do
			table.insert(tos,p:objectName())
		end
		return card:toString().."->"..table.concat(tos,"+")
	end
	return "."
end

--二版执义
sgs.ai_skill_choice.secondzhiyi = function(self,choices,data)
	choices = choices:split("+")
	local cards = {}
	for _,choice in ipairs(choices)do
		if choice=="draw" then continue end
		local card = dummyCard(choice)
		card:setSkillName("_secondzhiyi")
		local dummyuse = dummy()
		self:useCardByClassName(card,dummyuse)
		if dummyuse.card then
			table.insert(cards,card)
		end
	end
	if #cards>0 then
		self:sortByUseValue(cards)
		return cards[1]:objectName()
	end
	if table.contains(choices,"analeptic") and not self:canDraw() then return "analeptic" end
	return "draw"
end

sgs.ai_skill_use["@@secondzhiyi!"] = function(self,prompt,method)
	local name = prompt:split(":")[2]
	if not name then return "." end
	local card = dummyCard(name)
	card:setSkillName("_secondzhiyi")
	local dummyuse = dummy()
	self:useCardByClassName(card,dummyuse)
	if dummyuse.card then
		local tos = {}
		for _,p in sgs.qlist(dummyuse.to)do
			table.insert(tos,p:objectName())
		end
		return card:toString().."->"..table.concat(tos,"+")
	end
	return "."
end

--急盟
sgs.ai_skill_playerchosen.jimeng = function(self,targets)
	if self.player:getHp()>1 then return nil end
	return self:findPlayerToDiscard("he",false,false)[1]
end

sgs.ai_skill_discard.jimeng = function(self,discard_num,min_num,optional,include_equip)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	local dis = {}
	for i = 1,math.min(#cards,min_num)do
		table.insert(dis,cards[i]:getEffectiveId())
	end
	return dis
end

--率言
sgs.ai_skill_invoke.shuaiyan = function(self,data)
	local target = self:findPlayerToDiscard("he",false,false)[1]
	if target then
		sgs.ai_skill_playerchosen.shuaiyan = target
		return true
	end
	return false
end

sgs.ai_skill_discard.shuaiyan = function(self,discard_num,min_num,optional,include_equip)
	local cards = sgs.QList2Table(self.player:getCards("he"))
	return {cards[1]:getEffectiveId()}
end

--托孤
sgs.ai_skill_invoke.tuogu = function(self,data)
	local who = data:toPlayer()
	local good,bad = false,false
	local g = sgs.Sanguosha:getGeneral(who:getGeneralName())
	for _,skill in sgs.qlist(g:getSkillList())do
		if not skill:isVisible() then continue end
		if skill:isLimitedSkill() or skill:getFrequency()==sgs.Skill_Wake then continue end
		if skill:isLordSkill() then continue end
		if string.find(sgs.bad_skills,skill:objectName()) then
			bad = true
			continue
		end
		good = true
	end
	if who:getGeneral2() then
		local g2 = sgs.Sanguosha:getGeneral(who:getGeneral2Name())
		for _,skill in sgs.qlist(g2:getSkillList())do
			if not skill:isVisible() then continue end
			if skill:isLimitedSkill() or skill:getFrequency()==sgs.Skill_Wake then continue end
			if skill:isLordSkill() then continue end
			if string.find(sgs.bad_skills,skill:objectName()) then
				bad = true
				continue
			end
			good = true
		end
	end
	
	if self:isFriend(who) and good then return true end
	if not self:isFriend(who) and not bad then return true end
	return false
end

sgs.ai_skill_choice.tuogu = function(self,choices,data)
	local who = data:toPlayer()
	choices = choices:split("+")
	if self:isFriend(who) then
		for _,choice in ipairs(choices)do
			if self:isValueSkill(choice,who,true) then
				return choice
			end
		end
		for _,choice in ipairs(choices)do
			if self:isValueSkill(choice,who) then
				return choice
			end
		end
		for _,choice in ipairs(choices)do
			if string.find(sgs.bad_skills,choice) then continue end
			return choice
		end
		return choices[math.random(1,#choices)]
	else
		for _,choice in ipairs(choices)do
			if string.find(sgs.bad_skills,choice) then
				return choice
			end
		end
		for _,choice in ipairs(choices)do
			if self:isValueSkill(choice,who) then continue end
			return choice
		end
		for _,choice in ipairs(choices)do
			if self:isValueSkill(choice,who,true) then continue end
			return choice
		end
		return choices[math.random(1,#choices)]
	end
end

--擅专
sgs.ai_skill_invoke.shanzhuan = function(self,data)
	local to = data:toPlayer()
	if to then return not self:isFriend(to) end
	if data:toString()=="draw" then return self:canDraw() end
	return false
end

--二版托孤
sgs.ai_skill_invoke.secondtuogu = function(self,data)
	return sgs.ai_skill_invoke.tuogu(self,data)
end

sgs.ai_skill_choice.secondtuogu = function(self,choices,data)
	return sgs.ai_skill_choice.tuogu(self,choices,data)
end

--诤荐
sgs.ai_skill_playerchosen.zhengjian = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"handcard")
	targets = sgs.reverse(targets)
	for _,p in ipairs(targets)do
		if self:isFriend(p) and p:getMark("&zhengjian")<=0 then return p end
	end
	self:sort(targets,"handcard")
	for _,p in ipairs(targets)do
		if not self:isFriend(p) and p:getMark("&zhengjian")<=0 then return p end
	end
	
	return self.player
end

--告援
sgs.ai_skill_use["@@gaoyuan"] = function(self,prompt,method)
	local tos,source = {},nil
	for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if p:hasFlag("GaoyuanFrom") then
			source = p
			break
		end
	end
	if not source then return "." end
	local slash = sgs.Card_Parse(self.player:property("gaoyuanData"):toString())
	if not slash then return "." end
	for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if p:getMark("&zhengjian")>0 and not p:hasFlag("GaoyuanFrom") and source:canSlash(p,slash,false) then
			table.insert(tos,p)
		end
	end
	if #tos==0 then return "." end
	
	local id = -1
	if self:needToThrowArmor() and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId()) then
		id = self.player:getArmor():getEffectiveId()
	else
		local cards = sgs.QList2Table(self.player:getCards("he"))
		self:sortByKeepValue(cards)
		for _,c in ipairs(cards)do
			if self.player:canDiscard(self.player,c:getEffectiveId()) then
				id = c:getEffectiveId()
				break
			end
		end
	end
	if id<0 then return "." end
	
	self:sort(tos)
	for _,p in ipairs(tos)do
		if self:isEnemy(p) and self:slashIsEffective(slash,p,source) and not p:hasArmorEffect("EightDiagram") then
			return "@GaoyuanCard="..id.."->"..p:objectName()
		end
	end
	for _,p in ipairs(tos)do
		if self:isEnemy(p) and self:slashIsEffective(slash,p,source) then
			return "@GaoyuanCard="..id.."->"..p:objectName()
		end
	end
	
	for _,p in ipairs(tos)do
		if not self:isFriend(p) and self:slashIsEffective(slash,p,source) and not p:hasArmorEffect("EightDiagram") then
			return "@GaoyuanCard="..id.."->"..p:objectName()
		end
	end
	for _,p in ipairs(tos)do
		if not self:isFriend(p) and self:slashIsEffective(slash,p,source) then
			return "@GaoyuanCard="..id.."->"..p:objectName()
		end
	end
	
	for _,p in ipairs(tos)do
		if self:isFriend(p) and not self:slashIsEffective(slash,p,source) then
			return "@GaoyuanCard="..id.."->"..p:objectName()
		end
	end
	
	if self:isWeak() then
		tos = sgs.reverse(tos)
		for _,p in ipairs(tos)do
			if self:isFriend(p) and p:hasArmorEffect("EightDiagram") and not self:isWeak(p) then
				return "@GaoyuanCard="..id.."->"..p:objectName()
			end
		end
		
		for _,p in ipairs(tos)do
			if self:isFriend(p) and not self:isWeak(p) then
				return "@GaoyuanCard="..id.."->"..p:objectName()
			end
		end
	end
	
	return "."
end

--让节
sgs.ai_skill_invoke.rangjie = function(self,data)
	if not self.room:canMoveField("ej") and not self:canDraw() then return false end
	if self:canDraw() then return true end
	local from,card,to = self:moveField()
	if from and card and to then return true end
	return false
end

sgs.ai_skill_choice.rangjie = function(self,choices)
	choices = choices:split("+")
	if table.contains(choices,"move") then
		local from,card,to = self:moveField()
		if from and card and to then return "move" end
		table.removeOne(choices,"move")
	end
	return choices[math.random(1,#choices)]
end

sgs.ai_skill_playerchosen.rangjie_from = function(self,targets)
	local from,card,to = self:moveField()
	if from then return from end
end

sgs.ai_skill_cardchosen.rangjie = function(self,who,flags)
	local from,card,to = self:moveField()
	if card then return card end
end

sgs.ai_skill_playerchosen.rangjie_to = function(self,targets)
	local from,card,to = self:moveField()
	if to then return to end
end

--义争
local yizheng_skill = {}
yizheng_skill.name = "yizheng"
table.insert(sgs.ai_skills,yizheng_skill)
yizheng_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@YizhengCard=.")
end

sgs.ai_skill_use_func.YizhengCard = function(card,use,self)
	local max_card = self:getMaxCard()
	if not max_card then return end
	local point = max_card:getNumber()
	if self.player:hasSkill("tianbian") and max_card:getSuit()==sgs.Card_Heart then point = 13 end
	if (self.player:getMaxHp()<=3 and point>=10) or point>=7 then
		self:sort(self.enemies,"handcard")
		for _,p in ipairs(self.enemies)do
			if not self.player:canPindian(p) or not self:doDisCard(p,"h",true) then continue end
			local maxcard = self:getMaxCard(p)
			if maxcard then
				local number = maxcard:getNumber()
				if p:hasSkill("tianbian") and maxcard:getSuit()==sgs.Card_Heart then number = 13 end
				if number<point then
					use.card = sgs.Card_Parse("@YizhengCard=.")
					self.yizheng_card = max_card:getEffectiveId()
					use.to:append(p) 
					return
				end
			end
		end
	end
end

function sgs.ai_skill_pindian.yizheng(minusecard,self,requestor)
	local maxcard = self:getMaxCard()
	return self:isFriend(requestor) and self:getMinCard() or ( maxcard:getNumber()<6 and  minusecard or maxcard )
end

sgs.ai_use_priority.YizhengCard = 7
sgs.ai_use_value.YizhengCard = 2
sgs.ai_card_intention.YizhengCard = 50

--知略
local xingzhilve_skill = {}
xingzhilve_skill.name = "xingzhilve"
table.insert(sgs.ai_skills,xingzhilve_skill)
xingzhilve_skill.getTurnUseCard = function(self,inclusive)
	if self.player:getHp()>1 then
		return sgs.Card_Parse("@XingZhilveCard=.")
	end
	if self.player:getHp()<=1 and (hasBuquEffect(self.player) or self:getCardsNum("Peach")+self:getCardsNum("Analeptic")>0) then
		return sgs.Card_Parse("@XingZhilveCard=.")
	end
end

sgs.ai_skill_use_func.XingZhilveCard = function(card,use,self)
	if self.room:canMoveField() then
		local from,card,to = self:moveField()
		if from and card and to then
			use.card = sgs.Card_Parse("@XingZhilveCard=.")
			sgs.ai_skill_choice.xingzhilve = "move"
			return
		end
	end
	
	local slash = dummyCard()
	slash:setSkillName("_xingzhilve")
	local dummy_use = dummy()
	self:useCardSlash(slash,dummy_use)
	if dummy_use.card and dummy_use.to:length()>0 then
		use.card = sgs.Card_Parse("@XingZhilveCard=.")
		sgs.ai_skill_choice.xingzhilve = "draw"
	end
end

sgs.ai_skill_playerchosen.xingzhilve_from = function(self,targets)
	local from,card,to = self:moveField()
		if from then return from
	end
end

sgs.ai_skill_cardchosen.xingzhilve = function(self,who,flags)
	local from,card,to = self:moveField()
	if card then return card end
end

sgs.ai_skill_playerchosen.xingzhilve_to = function(self,targets)
	local from,card,to = self:moveField()
		if to then return to
	end
end

sgs.ai_skill_use["@@xingzhilve!"] = function(self,prompt)
	local slash = dummyCard("slash")
	slash:setSkillName("_xingzhilve")
	local dummy_use = dummy()
	self:useCardSlash(slash,dummy_use)
	if dummy_use.card and dummy_use.to:length()>0 then
		return "@XingZhilveSlashCard=.->"..dummy_use.to:first():objectName()
	end
	return "."
end

sgs.ai_use_priority.XingZhilveCard = 0
sgs.ai_use_value.XingZhilveCard = 2.5

--威风
sgs.ai_skill_playerchosen.xingweifeng = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets)
	for _,p in ipairs(targets)do
		if not self:isFriend(p) then return p end
	end
	return targets[#targets]
end

--治严
local xingzhiyan_skill = {}
xingzhiyan_skill.name = "xingzhiyan"
table.insert(sgs.ai_skills,xingzhiyan_skill)
xingzhiyan_skill.getTurnUseCard = function(self,inclusive)
	if self.player:getHandcardNum()>self.player:getHp() then
		sgs.ai_use_priority.XingZhiyanCard = 8
		sgs.ai_use_value.XingZhiyanCard = 8
		return sgs.Card_Parse("@XingZhiyanCard=.")
	end
	if self.player:getMaxHp()>self.player:getHandcardNum() and self:canDraw() then
		sgs.ai_use_priority.XingZhiyanCard = 0
		sgs.ai_use_value.XingZhiyanCard = 5
		return sgs.Card_Parse("@XingZhiyanCard=.")
	end
end

sgs.ai_skill_use_func.XingZhiyanCard = function(card,use,self)
	if self.player:getMark("xingzhiyan_give-PlayClear")<1
	and self.player:getHandcardNum()>self.player:getHp()
	then
		local cards = sgs.QList2Table(self.player:getHandcards())
		self:sortByUseValue(cards,true)
		local num = self.player:getHandcardNum()-self.player:getHp()
		local give = {}
		for _,c in ipairs(cards)do
			if #give<num then
				table.insert(give,c:getEffectiveId())
			else
				break
			end
		end
		if #give==0 then return end
		self:sort(self.friends_noself)
		for _,p in ipairs(self.friends_noself)do
			if hasManjuanEffect(p) or self:needKongcheng(p,true) then continue end
			if p:hasSkills(sgs.cardneed_skill) then
				use.card = sgs.Card_Parse("@XingZhiyanCard="..table.concat(give,"+"))
				use.to:append(p) 
				return
			end
		end
		for _,p in ipairs(self.friends_noself)do
			if hasManjuanEffect(p) or self:needKongcheng(p,true) then continue end
			use.card = sgs.Card_Parse("@XingZhiyanCard="..table.concat(give,"+"))
			use.to:append(p) 
			return
		end
		self:sort(self.enemies)
		local c = sgs.Sanguosha:getCard(give[1])
		if #give==1
		and not(c:isKindOf("Jink") or c:isKindOf("Peach") or c:isKindOf("Analeptic") or c:isKindOf("ExNihilo") or c:isKindOf("AOE"))
		then
			sgs.ai_use_priority.XingZhiyanCard = 2
			for _,p in ipairs(self.enemies)do
				if not self:needKongcheng(p,true) or hasManjuanEffect(p,true) then continue end
				if self:getEnemyNumBySeat(self.player,p,p)==0 then continue end
				use.card = sgs.Card_Parse("@XingZhiyanCard="..table.concat(give,"+"))
				use.to:append(p) 
				return
			end
		end
		if self.player:getHandcardNum()-#give<self.player:getMaxHp() and self:canDraw()
		and self.player:getMark("xingzhiyan_draw-PlayClear")<=0
		then
			sgs.ai_use_priority.XingZhiyanCard = 1
			for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
				if not hasManjuanEffect(p) then continue end
				use.card = sgs.Card_Parse("@XingZhiyanCard="..table.concat(give,"+"))
				use.to:append(p) 
				return
			end
		end
	end
	if self.player:getMark("xingzhiyan_draw-PlayClear")<1
	and self.player:getMaxHp()>self.player:getHandcardNum()
	then use.card = card end
end

sgs.ai_use_priority.XingZhiyanCard = 8
sgs.ai_use_value.XingZhiyanCard = 8

--锦帆
sgs.ai_skill_use["@@xingjinfan"] = function(self,prompt)
	local valid,ts = {},sgs.IntList()
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for c,id in sgs.list(self.player:getPile("&xingling"))do
		c = sgs.Sanguosha:getCard(id)
		ts:append(c:getSuit())
	end
	for _,h in sgs.list(cards)do
		if ts:contains(h:getSuit())
		or #valid>#cards/2
		then continue end
    	table.insert(valid,h:getEffectiveId())
		ts:append(h:getSuit())
	end
	return #valid>0 and ("@XingJinfanCard="..table.concat(valid,"+"))
end

--射却
sgs.ai_skill_use["@xingsheque"] = function(self,prompt)
	local tos = {}
	for d,s in sgs.list(self:getCards("Slash"))do
		d = self:aiUseCard(s)
		if d.card then 
			for _,to in sgs.list(d.to)do
				if to:hasFlag("SlashAssignee") then
					table.insert(tos,to:objectName())
				end
			end
			if #tos>0 then
				return s:toString().."->"..table.concat(tos,"+")
			end
		end
	end
	
end

--二版锦帆
sgs.ai_skill_use["@@secondxingjinfan"] = function(self,prompt)
	local valid,ts = {},sgs.IntList()
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for c,id in sgs.list(self.player:getPile("&xingling"))do
		c = sgs.Sanguosha:getCard(id)
		ts:append(c:getSuit())
	end
	for _,h in sgs.list(cards)do
		if ts:contains(h:getSuit())
		or #valid>#cards/2
		then continue end
    	table.insert(valid,h:getEffectiveId())
		ts:append(h:getSuit())
	end
	return ("@XingJinfanCard="..table.concat(valid,"+"))
end

--图射
sgs.ai_skill_invoke.tushe = function(self,data)
	return self:canDraw()
end

--立牧

local limu_skill = {name = "limu"}
table.insert(sgs.ai_skills,limu_skill)
limu_skill.getTurnUseCard = function(self)
	local cards,peach = {},0
	for i,c in sgs.list(self:addHandPile())do
		if peach<2 and isCard("Peach",c,self.player)
		then peach = peach+1
		elseif c:getSuit()==3
		then
			i = dummyCard("indulgence")
			i:addSubcard(c)
			i:setSkillName("limu")
			if not self.player:isLocked(i)
			then table.insert(cards,c) end
		end
	end
	if #cards<1 then return end
	self:sortByKeepValue(cards)
	if self:isWeak()
	then
		sgs.ai_use_priority.Peach = sgs.ai_use_priority.LimuCard+0.1
		if cards[1]:isKindOf("Peach") and cards[1]:isAvailable(self.player) then return cards[1] end
		return sgs.Card_Parse("@LimuCard="..cards[1]:getEffectiveId())
	end
	local id = -1
	for _,c in ipairs(cards)do
		if isCard("Slash",c,self.player)
		then else id = c:getEffectiveId() break end
	end
	local slash_num = self:getCardsNum("Slash")
	if id<0 then
		id = cards[1]:getEffectiveId()
		slash_num = slash_num-1
	end
	if slash_num>1
	then
		for _,slash in ipairs(self:getCards("Slash"))do
			if self:aiUseCard(slash).card then
				return sgs.Card_Parse("@LimuCard="..id)
			end
		end
	end
end

sgs.ai_skill_use_func.LimuCard = function(card,use,self)
	use.card = card
end

sgs.ai_use_priority.LimuCard = 2.5
sgs.ai_use_value.LimuCard = 2.5

sgs.ai_use_revises.limu = function(self,card,use)
	if self.player:hasWeapon("spear")
	and card:isKindOf("Weapon")
	then return false end
	if card:objectName()=="spear"
	then
		use.card = card
		return true
	end
	if self:getCardsNum("BasicCard","h")<1
	then
		local ge = self:getCard("GlobalEffect") or self:getCard("AOE")
		if ge and ge:isAvailable(self.player)
		then use.card = ge return true end
	end
	if self.player:hasWeapon("spear")
	then
		local cards = self.player:getCards("he")
		cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
		for _,c1 in sgs.list(cards)do
			if c1:getSuit()==3
			and self.player:hasJudgeArea()
			and self.player:getJudgingArea():isEmpty()
			then
				use.card = sgs.Card_Parse("@LimuCard="..c1:getEffectiveId())
				return true
			end
			if (c1:isKindOf("Peach") or c1:isKindOf("Analeptic"))
			and c1:isAvailable(self.player)
			then
				use.card = c1
				return true
			end
			if c1:getTypeId()~=1
			or c1:isKindOf("Slash")
			or c1:isAvailable(self.player)
			or self.player:getEquips():contains(c1)
			then continue end
			for _,c2 in sgs.list(cards)do
				if c1:getEffectiveId()==c2:getEffectiveId()
				or self.player:getEquips():contains(c2) then continue end
				local slash = dummyCard("slash")
				slash:setSkillName("spear")
				slash:addSubcard(c1)
				slash:addSubcard(c2)
				if slash:isAvailable(self.player)
				and self:getUseValue(c2)<=self:getUseValue(slash)
				then card = slash return end
			end
		end
	end
end

--力激
local liji_skill = {}
liji_skill.name = "liji"
table.insert(sgs.ai_skills,liji_skill)
liji_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@LijiCard=.")
end

sgs.ai_skill_use_func.LijiCard = function(card,use,self)
	local target = self:findPlayerToDamage(1,self.player,nil,self.room:getOtherPlayers(self.player))[1]
	if target and not self:isFriend(target) then
		local cards = sgs.QList2Table(self.player:getCards("he"))
		self:sortByKeepValue(cards)
		if self:needToThrowArmor() and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId()) then
			use.card = sgs.Card_Parse("@LijiCard="..self.player:getArmor():getEffectiveId())
			use.to:append(target) 
			return
		end
		for _,c in ipairs(cards)do
			if c:isKindOf("Peach") then continue end
			if c:isKindOf("WoodenOx") and not self.player:getPile("wooden_ox"):isEmpty() then continue end
			if self.player:canDiscard(self.player,c:getEffectiveId()) then
				use.card = sgs.Card_Parse("@LijiCard="..c:getEffectiveId())
				use.to:append(target) 
				return
			end
		end
	end
end

sgs.ai_use_priority.LijiCard = 2.5
sgs.ai_use_value.LijiCard = 2.5

--决死
local juesi_skill = {}
juesi_skill.name = "juesi"
table.insert(sgs.ai_skills,juesi_skill)
juesi_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@JuesiCard=.")
end

sgs.ai_skill_use_func.JuesiCard = function(card,use,self)
	local handcards = self.player:getHandcards()
	local slashs = {}
	for _,c in sgs.qlist(handcards)do
		if c:isKindOf("Slash") and self.player:canDiscard(self.player,c:getEffectiveId()) then
			table.insert(slashs,c)
		end
	end
	if #slashs==0 then return end
	self:sortByKeepValue(slashs)
	self:sort(self.enemies,"chaofeng")
	
	local enemys = {}
	for _,enemy in ipairs(self.enemies)do
		if not self.player:inMyAttackRange(enemy) then continue end
		if not enemy:canDiscard(enemy,"he") then continue end
		if not self:doDisCard(enemy,"he") and not (self:isWeak(enemy) and self.player:getHp()<=enemy:getHp()) then continue end
		if self:hasSkills(sgs.lose_equip_skill,enemy) and not (self:isWeak(enemy) and self.player:getHp()<=enemy:getHp()) then continue end
		if self:needToThrowLastHandcard(enemy) then continue end
		if self:getCardsNum("Slash")-1<getCardsNum("Slash",enemy) and self.player:getHp()<=enemy:getHp() then continue end
		table.insert(enemys,enemy)
	end
	if #enemys>0 then
		use.card = sgs.Card_Parse("@JuesiCard="..slashs[1]:getEffectiveId())
		use.to:append(enemys[1]) 
		return
	end
	
	if self:getOverflow()<=0 then return end
	sgs.ai_use_priority.JuesiCard = 0
	local friends = {}
	for _,friend in ipairs(self.friends_noself)do
		if self.player:getHp()<=friend:getHp() then continue end
		if not self.player:inMyAttackRange(friend) then continue end
		if self:needToThrowLastHandcard(friend) or (self:needToThrowArmor(friend) and friend:canDiscard(friend,friend:getArmor():getEffectiveId())) or self:hasSkills(sgs.lose_equip_skill,friend) then
			table.insert(friends,friend)
		end
	end
	if #friends>0 then
		use.card = sgs.Card_Parse("@JuesiCard="..slashs[1]:getEffectiveId())
		use.to:append(friends[1]) 
	end
end

sgs.ai_use_priority.JuesiCard = sgs.ai_use_priority.Duel+0.1
sgs.ai_use_value.JuesiCard = sgs.ai_use_value.Duel+0.1

sgs.ai_skill_discard.juesi = function(self,discard_num,min_num,optional,include_equip)
	if self:needToThrowArmor() and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId()) then return {self.player:getArmor():getEffectiveId()} end
	if self:needToThrowLastHandcard() and self.player:canDiscard(self.player,self.player:handCards():first()) then return {self.player:handCards():first()} end
	
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	local card = nil
	for _,c in ipairs(cards)do
		if self.player:canDiscard(self.player,c:getEffectiveId()) then
			card = c
			break
		end
	end
	if not card then return {} end
	
	local slashs = {}
	for _,c in sgs.qlist(self.player:getHandcards())do
		if c:isKindOf("Slash") and self.player:canDiscard(self.player,c:getEffectiveId()) then
			table.insert(slashs,c)
		end
	end
	
	local source = self.player:getTag("juesiSource"):toPlayer()
	if not source or source:isDead() then return {card:getEffectiveId()} end
	if self.player:getHp()>=source:getHp() then
		if self:isEnemy(source) and self:getCardsNum("Slash")>getCardsNum("Slash",source) then
			if card:isKindOf("Slash") then
				for _,c in ipairs(cards)do
					if not c:isKindOf("Slash") and self.player:canDiscard(self.player,c:getEffectiveId()) then
						card = c
						break
					end
				end
			end
			return {card:getEffectiveId()}
		end
		if #slashs>0 then return {slashs[1]:getEffectiveId()} end
		return {card:getEffectiveId()}
	end
	return {card:getEffectiveId()}
end

--誓仇
sgs.ai_skill_use["@@tenyearnewshichou"] = function(self,prompt)
	local use = self.player:getTag("tenyearnewshichou_data"):toCardUse()
	local dummyuse = dummy()
	self:useCardSlash(use.card,dummyuse)
	if dummyuse.card and not dummyuse.to:isEmpty() then
		local lost = self.player:getLostHp()
		local num = 0
		local tos = {}
		for _,p in sgs.qlist(dummyuse.to)do
			if num>=lost then break end
			num = num+1
			table.insert(tos,p:objectName())
		end
		if #tos>0 then return "@TenyearNewShichouCard=.->"..table.concat(tos,"+") end
	end
	return "."
end

--间书
local jianshu_skill = {}
jianshu_skill.name = "jianshu"
table.insert(sgs.ai_skills,jianshu_skill)
jianshu_skill.getTurnUseCard = function(self,inclusive)
	local can_use = false
	self:sort(self.enemies,"chaofeng")
	for _,a in ipairs(self.enemies)do
		for _,b in ipairs(self.enemies)do
			if a:canPindian(b)
			and b:inMyAttackRange(a)
			then can_use = a:isWounded() and b:isWounded() end
		end
	end
	if can_use then
		local cards = sgs.QList2Table(self.player:getCards("h"))
		self:sortByKeepValue(cards)
		for _,c in sgs.list(cards)do
			if not c:isBlack() then continue end
			return sgs.Card_Parse("@JianshuCard="..c:getEffectiveId())
		end
	end
end

sgs.ai_skill_use_func.JianshuCard = function(card,use,self)
	self:sort(self.enemies,"chaofeng")
	for _,a in ipairs(self.enemies)do
		for _,b in ipairs(self.enemies)do
			if a~=b and a:canPindian(b)
			and b:inMyAttackRange(a) then
				use.card = card
				use.to:append(a)
				use.to:append(b)
				return
			end
		end
	end
	if #self.enemies==1 then
		local aps = sgs.QList2Table(self.room:getOtherPlayers(self.player))
		self:sort(aps,"chaofeng")
		for _,a in ipairs(self.enemies)do
			for _,b in ipairs(aps)do
				if a~=b and a:canPindian(b) and not self:isFriend(b)
				and b:inMyAttackRange(a) and self:isWeak(a) then
					use.card = card
					use.to:append(a)
					use.to:append(b)
					return
				end
			end
		end
	end
end

sgs.ai_use_priority.JianshuCard = 0
sgs.ai_use_value.JianshuCard = 2.5
sgs.ai_card_intention.JianshuCard = 80

--拥嫡
function getYongdiTarget(self,targets,lord)
	local lords = {}
	local good_targets = {}
	local weaks = {}
	targets = sgs.QList2Table(targets)
	self:sort(targets,"chaofeng")
	for _,p in ipairs(targets)do
		if not self:isFriend(p) then continue end
		if self:hasSkills(sgs.need_maxhp_skill,p) then
			table.insert(good_targets,p)
		end
		
		for _,skill in sgs.qlist(p:getGeneral():getVisibleSkillList())do
			if skill:isLordSkill() and not p:hasLordSkill(skill,true) and (not lord or not p:isLord()) then
				table.insert(lords,p)
				break
			end
		end
		
		if not table.contains(lords,p) and p:getGeneral2() then
			for _,skill in sgs.qlist(p:getGeneral2():getVisibleSkillList())do
				if skill:isLordSkill() and not p:hasLordSkill(skill,true) and (not lord or not p:isLord()) then
					table.insert(lords,p)
					break
				end
			end
		end
		if self:isWeak(p) then table.insert(weaks,p) end
	end
	
	for _,p in ipairs(targets)do
		if not self:isFriend(p) then continue end
		if table.contains(lords,p) and table.contains(good_targets,p) and table.contains(weaks,p) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if not self:isFriend(p) then continue end
		if table.contains(lords,p) and table.contains(good_targets,p) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if not self:isFriend(p) then continue end
		if table.contains(good_targets,p) and table.contains(weaks,p) then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if not self:isFriend(p) then continue end
		if table.contains(lords,p) and table.contains(weaks,p) then
			return p
		end
	end
	if #good_targets>0 then return good_targets[1] end
	if #lords>0 then return lords[1] end
	if #weaks>0 then return weaks[1] end
	return nil
end

sgs.ai_skill_playerchosen.yongdi = function(self,targets)
	return getYongdiTarget(self,targets)
end

sgs.ai_playerchosen_intention.yongdi = -20

--二版拥嫡
sgs.ai_skill_playerchosen.newyongdi = function(self,targets)
	return getYongdiTarget(self,targets,true)
end

sgs.ai_playerchosen_intention.newyongdi = sgs.ai_playerchosen_intention.yongdi

--雪恨
local newxuehen_skill = {}
newxuehen_skill.name = "newxuehen"
table.insert(sgs.ai_skills,newxuehen_skill)
newxuehen_skill.getTurnUseCard = function(self,inclusive)
	if self.player:getLostHp()>0 then
		return sgs.Card_Parse("@NewxuehenCard=.")
	end
end

sgs.ai_skill_use_func.NewxuehenCard = function(card,use,self)
	self:sort(self.enemies,"hp")
	local targets = {}
	local lost = self.player:getLostHp()
	for _,enemy in ipairs(self.enemies)do
		if not enemy:isChained() and not enemy:hasSkill("qianjie") and self:isWeak(enemy) and self:damageIsEffective(enemy,sgs.DamageStruct_Fire) then
			table.insert(targets,enemy)
		end
	end
	for _,enemy in ipairs(self.enemies)do
		if #targets>=lost then break end
		if self:isWeak(enemy) and self:damageIsEffective(enemy,sgs.DamageStruct_Fire) then
			table.insert(targets,enemy)
		end
	end
	for _,enemy in ipairs(self.enemies)do
		if #targets>=lost then break end
		if self:damageIsEffective(enemy,sgs.DamageStruct_Fire) and not self:cantbeHurt(enemy) and not self:needToLoseHp(enemy) then
			table.insert(targets,enemy)
		end
	end
	if #targets==0 then return end
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	for _,c in ipairs(cards)do
		if c:isRed() and not c:isKindOf("Peach") and self.player:canDiscard(self.player,c:getEffectiveId()) then
			use.card = sgs.Card_Parse("@NewxuehenCard="..c:getEffectiveId())
			for i = 1,#targets,1 do
				use.to:append(targets[i])
			end
			return
		end
	end
end

sgs.ai_use_priority.NewxuehenCard = 3
sgs.ai_use_value.NewxuehenCard = 2.35
sgs.ai_card_intention.NewxuehenCard = 20

sgs.ai_skill_playerchosen.newxuehen = function(self,targets)
	local to = self:findPlayerToDamage(1,self.player,sgs.DamageStruct_Fire,targets)[1]
	if to then return to end
	targets = sgs.QList2Table(targets)
	self:sort(targets,"hp")
	for _,p in ipairs(targets)do
		if not self:isEnemy(p) or not self:damageIsEffective(p,sgs.DamageStruct_Fire) then continue end
		return p
	end
	for _,p in ipairs(targets)do
		if self:isFriend(p) or not self:damageIsEffective(p,sgs.DamageStruct_Fire) then continue end
		return p
	end
	return targets[math.random(1,#targets)]
end

--应援
sgs.ai_skill_playerchosen.yingyuan = function(self,targets)
	local friends = {}
	targets = sgs.QList2Table(targets)
	self:sort(targets)
	local card = self.player:getTag("yingyuanCard"):toCard()
	if card then
		for _,p in ipairs(targets)do
			if not self:isFriend(p) then continue end
			if (card:isKindOf("TrickCard") and p:hasSkills("tenyearjizhi|jizhi|nosjizhi")) or (card:isKindOf("EquipCard") and p:hasSkills(sgs.need_equip_skill.."|qiangxi")) then  --极略不考虑了
				table.insert(friends,p)
			end
		end
	end
	if #friends>0 then friends = sgs.reverse(friends) return friends[1] end
	for _,p in ipairs(targets)do
		if not self:isFriend(p) or self:needKongcheng(p,true) or hasManjuanEffect(p) then continue end
		return p
	end
	return nil
end

sgs.ai_playerchosen_intention.yingyuan = function(self,from,to)
	if hasManjuanEffect(to) then return end
	local intention = -20
	if self:needKongcheng(to,true) then intention = 20 end
	sgs.updateIntention(from,to,intention)
end

--手杀应援
sgs.ai_skill_playerchosen.mobileyingyuan = function(self,targets)
	local card = self.player:getTag("mobileyingyuanCard"):toCard()
	if card then
		local cards = {}
		if not card:isVirtualCard() then
			table.insert(cards,card)
		elseif card:subcardsLength()==1 then
			table.insert(cards,sgs.Sanguosha:getCard(card:getSubcards():first()))
		end
		if #cards>0 then
			local c,to = self:getCardNeedPlayer(cards)
			if to then return to end
		end
	end
	self:sort(self.friends_noself)
	for _,p in ipairs(self.friends_noself)do
		if not self:isFriend(p) or self:needKongcheng(p,true) or hasManjuanEffect(p) then continue end
		return p
	end
	return nil
end

sgs.ai_playerchosen_intention.mobileyingyuan = sgs.ai_playerchosen_intention.yingyuan

--鸩毒
sgs.ai_skill_cardask["@newzhendu-discard"] = function(self,data)
	local discard_trend = will_discard_zhendu(self,"newzhendu")
	if discard_trend<=0 then return "." end
	if self.player:getHandcardNum()+math.random(1,100)/100>=discard_trend then
		local cards = sgs.QList2Table(self.player:getHandcards())
		self:sortByKeepValue(cards)
		for _,card in ipairs(cards)do
			if not self:isValuableCard(card,self.player) then return "$"..card:getEffectiveId() end
		end
	end
	return "."
end

--戚乱
sgs.ai_skill_invoke.newqiluan = function(self,data)
	return self:canDraw()
end

--励战
sgs.ai_skill_use["@@lizhan"] = function(self,prompt)
    local targets = {}
    for _,friend in ipairs(self.friends)do
        if friend:isWounded() and self:canDraw(friend) then
            table.insert(targets,friend:objectName())
        end 
    end
	for _,enemy in ipairs(self.enemies)do
        if enemy:isWounded() and self:needKongcheng(enemy,true) and not hasManjuanEffect(enemy) and self:getEnemyNumBySeat(self.player,enemy,enemy)>0 then
            table.insert(targets,enemy:objectName())
        end 
    end
	if #targets==0 then return "." end
    return "@LizhanCard=.->"..table.concat(targets,"+")
end

sgs.ai_card_intention.LizhanCard = function(self,card,from,tos)
	local intention = -20
	for _,to in ipairs(tos)do
		if hasManjuanEffect(to) then continue end
		if self:needKongcheng(to,true) and self:getEnemyNumBySeat(from,to,to)>0 then
			intention = 20
		end
		sgs.updateIntention(from,to,intention)
	end
end

--伪溃
local weikui_skill = {}
weikui_skill.name = "weikui"
table.insert(sgs.ai_skills,weikui_skill)
weikui_skill.getTurnUseCard = function(self,inclusive)
	if self.player:getHp()>1 then
		return sgs.Card_Parse("@WeikuiCard=.")
	end
	if self.player:getHp()<=1 and (hasBuquEffect(self.player) or self:getCardsNum("Peach")+self:getCardsNum("Analeptic")>0) then
		return sgs.Card_Parse("@WeikuiCard=.")
	end
end

sgs.ai_skill_use_func.WeikuiCard = function(card,use,self)
	self:sort(self.enemies)
	local slash = dummyCard("slash")
	slash:setSkillName("_weikui")
	
	for _,enemy in ipairs(self.enemies)do
		if not self:doDisCard(enemy,"h") then continue end
		local jink,visible = 0,0
		local flag = string.format("%s_%s_%s","visible",self.player:objectName(),enemy:objectName())
		for _,c in sgs.qlist(enemy:getCards("h"))do
			if c:hasFlag("visible") or c:hasFlag(flag) then
				visible = visible+1
				if c:isKindOf("Jink") then
					jink = jink+1
				end
			end
		end
		
		if jink>0 and (not self.player:canSlash(enemy,slash,false) or self:slashProhibit(slash,enemy)) then continue end
		if enemy:getHandcardNum()-visible>2 and (not self.player:canSlash(enemy,slash,false) or self:slashProhibit(slash,enemy)) then continue end
		use.card = sgs.Card_Parse("@WeikuiCard=.")
		use.to:append(enemy) 
		return
	end
end

sgs.ai_use_priority.WeikuiCard = sgs.ai_use_priority.Dismantlement-0.1
sgs.ai_use_value.WeikuiCard = sgs.ai_use_value.Dismantlement-0.1
sgs.ai_card_intention.WeikuiCard = 80

--影箭
sgs.ai_skill_use["@@yingjian"] = function(self,prompt)
	local slash = dummyCard("slash")
	slash:setSkillName("yingjian")
	local dummy_use = self:aiUseCard(slash)
	if dummy_use.card then
		local c_tos = {}
		for _,p in sgs.list(dummy_use.to)do
			table.insert(c_tos,p:objectName())
		end
		return slash:toString().."->"..table.concat(c_tos,"+")
	end
	return "."
end

--募兵
sgs.ai_skill_invoke.mubing = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_use["@@mubing"] = function(self,prompt)
	local valid = {}
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	local cidlist = self.player:getTag("mubingForAI"):toIntList()
	local n1,n2 = 0,0
	for _,h in sgs.list(cards)do
		for c,id in sgs.list(cidlist)do
			c = sgs.Sanguosha:getCard(id)
			if self:getKeepValue(c)>self:getKeepValue(h)
			and not table.contains(valid,h:getEffectiveId())
			and not table.contains(valid,c:getEffectiveId())
			then
				if n1+h:getNumber()>n2+c:getNumber()
				then
					table.insert(valid,h:getEffectiveId())
					table.insert(valid,c:getEffectiveId())
					n1 = n1+h:getNumber()
					n2 = n2+c:getNumber()
					break
				end
			end
		end
	end
	return #valid>1 and ("@MubingCard="..table.concat(valid,"+"))
end

--资取
sgs.ai_skill_invoke.ziqu = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isFriend(target) and target:getCards("he"):length()>0 or target:getCards("he"):length()>4
	end
end

sgs.ai_skill_use["@@ziqu!"] = function(self,prompt)
	local n = 0
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if h:getNumber()>n then n = h:getNumber() end
	end
	for _,h in sgs.list(cards)do
		if h:getNumber()>=n then n = h:getEffectiveId() break end
	end
	return #cards>0 and ("@ZiquCard="..n)
end

--调令
sgs.ai_skill_choice.diaoling = function(self,choice)
	if hasManjuanEffect(self.player) then return "recover" end
	if self:needToLoseHp() and not self:isWeak() then return "draw" end
	return "recover"
end

--谋诛
addAiSkills("spmouzhu").getTurnUseCard = function(self)
	return sgs.Card_Parse("@SpMouzhuCard=.")
end

sgs.ai_skill_use_func["SpMouzhuCard"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	local n,x = 0,0
	for _,ep in sgs.list(self.enemies)do
		if ep:getHp()==self.player:getHp()
		then n = n+1 end
		if self.player:distanceTo(ep)==1
		then x = x+1 end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:getHp()==self.player:getHp()
		and n>x
		then
			use.card = card
			use.to:append(ep)
			if use.to:length()>=n then return end
		elseif self.player:distanceTo(ep)==1
		and x>=n
		then
			use.card = card
			use.to:append(ep)
			if use.to:length()>=x then return end
		end
	end
end

sgs.ai_use_value.SpMouzhuCard = 9.4
sgs.ai_use_priority.SpMouzhuCard = 3.8


--备诛
local beizhu_skill = {}
beizhu_skill.name = "beizhu"
table.insert(sgs.ai_skills,beizhu_skill)
beizhu_skill.getTurnUseCard = function(self,inclusive)
	return sgs.Card_Parse("@BeizhuCard=.")
end

sgs.ai_skill_use_func.BeizhuCard = function(card,use,self)
	self:sort(self.enemies,"handcard")
	local enemies= {}
	for _,enemy in ipairs(self.enemies)do
		if not self:doDisCard(enemy,"he") then continue end
		local slash,visible = 0,0
		local flag = string.format("%s_%s_%s","visible",self.player:objectName(),enemy:objectName())
		for _,c in sgs.qlist(enemy:getCards("h"))do
			if c:hasFlag("visible") or c:hasFlag(flag) then
				visible = visible+1
				if c:isKindOf("Slash") then
					slash = slash+1
				end
			end
		end
		if slash>0 or enemy:getHandcardNum()-visible>2 then continue end
		table.insert(enemies,enemy)
	end
	if #enemies<=0 then return end
	use.card = sgs.Card_Parse("@BeizhuCard=.")
	use.to:append(enemies[1])
end

sgs.ai_use_priority.BeizhuCard = sgs.ai_use_priority.Dismantlement-0.1
sgs.ai_use_value.BeizhuCard = sgs.ai_use_value.Dismantlement-0.1
sgs.ai_card_intention.BeizhuCard = 80

sgs.ai_skill_invoke.beizhu = function(self,data)
	local name = data:toString():split(":")[2]
	if not name then return false end
	local target = self.room:findPlayerByObjectName(name)
	if not target or target:isDead() then return false end
	if self:isFriend(target) then return self:canDraw(target) end
	if self:isEnemy(target) then return not self:canDraw(target) end
	return false
end

--承诏
sgs.ai_skill_playerchosen.chengzhao = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"handcard")
	
	local cards = sgs.CardList()
	local peach,jink = 0,0
	for _,c in sgs.qlist(self.player:getHandcards())do
		if isCard("Peach",c,self.player) and peach<2 then
			peach = peach+1
		elseif isCard("Jink",c,self.player) then
			if not self:isWeak() or jink>0 then
				cards:append(c)
			else
				jink = jink+1
			end
		else
			cards:append(c)
		end
	end
	local max_card = self:getMaxCard(self.player,cards)
	if not max_card then return nil end
	
	self.chengzhao_card = max_card:getEffectiveId()
	
	local slash = dummyCard("slash")
	slash:setSkillName("_chengzhao");
	
	for _,p in ipairs(targets)do
		if not self:isEnemy(p) or not self.player:canSlash(p,slash,false) then continue end
		if p:hasSkill("kongcheng") and p:getHandcardNum()==1 then continue end
		if not self:doDisCard(p,"h") then continue end
		if not self:damageIsEffective(p,nil,self.player) then continue end
		if not self:slashIsEffective(slash,p,self.player,true) then continue end
		return p
	end
	
	for _,p in ipairs(targets)do
		if self:isFriend(p) and p:getHandcardNum()==1 and p:hasSkill("kongcheng") then
			return p
		end
	end
	
	for _,p in ipairs(targets)do
		if not self:isEnemy(p) or not self.player:canSlash(p,slash,false) then continue end
		if p:hasSkill("kongcheng") and p:getHandcardNum()==1 then continue end
		if not self:doDisCard(p,"h") then continue end
		return p
	end
	
	return nil
end

sgs.ai_fill_skill.xuezhao = function(self)
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,c in sgs.list(cards)do
	   	if table.contains(self.toUse,c) then continue end
		return sgs.Card_Parse("@XuezhaoCard="..c:getEffectiveId())
	end
end

sgs.ai_skill_use_func["XuezhaoCard"] = function(card,use,self)
	use.card = card
	self:sort(self.friends_noself,"card",true)
	for _,p in sgs.list(self.friends_noself)do
		if p:getCardCount()>0 and use.to
		and use.to:length()<self.player:getHp()
		then use.to:append(p) end
	end
	self:sort(self.enemies,"hp")
	for _,p in sgs.list(self.enemies)do
		if p:getCardCount()>0 and use.to
		and use.to:length()<self.player:getHp()
		then use.to:append(p) end
	end
end

sgs.ai_use_value.XuezhaoCard = 3.4
sgs.ai_use_priority.XuezhaoCard = 7.2

sgs.ai_skill_cardask["@xuezhao-give"] = function(self,data,pattern,prompt)
    local parsed = data:toPlayer()
    if not self:isEnemy(parsed)
	or self:isWeak()
	then return true end
	return "."
end

sgs.ai_fill_skill.secondxuezhao = function(self)
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,c in sgs.list(cards)do
	   	if table.contains(self.toUse,c) then continue end
		return sgs.Card_Parse("@SecondXuezhaoCard="..c:getEffectiveId())
	end
end

sgs.ai_skill_use_func["SecondXuezhaoCard"] = function(card,use,self)
	use.card = card
	self:sort(self.friends_noself,"card",true)
	for _,p in sgs.list(self.friends_noself)do
		if p:getCardCount()>0 and use.to
		and use.to:length()<self.player:getMaxHp()
		then use.to:append(p) end
	end
	self:sort(self.enemies,"hp")
	for _,p in sgs.list(self.enemies)do
		if p:getCardCount()>0 and use.to
		and use.to:length()<self.player:getMaxHp()
		then use.to:append(p) end
	end
end

sgs.ai_use_value.SecondXuezhaoCard = 3.4
sgs.ai_use_priority.SecondXuezhaoCard = 7.2

sgs.ai_skill_cardask["@secondxuezhao-give"] = function(self,data,pattern,prompt)
    local parsed = data:toPlayer()
    if not self:isEnemy(parsed)
	or self:isWeak()
	then return true end
	return "."
end






--咒缚
local newzhoufu_skill = {}
newzhoufu_skill.name = "newzhoufu"
table.insert(sgs.ai_skills,newzhoufu_skill)
newzhoufu_skill.getTurnUseCard = function(self)
	if self.player:isKongcheng() then return end
	return sgs.Card_Parse("@NewZhoufuCard=.")
end

sgs.ai_skill_use_func.NewZhoufuCard = function(card,use,self)
	local cards = {}
	for _,card in sgs.qlist(self.player:getHandcards())do
		table.insert(cards,sgs.Sanguosha:getEngineCard(card:getEffectiveId()))
	end
	self:sortByKeepValue(cards)
	self:sort(self.friends_noself)
	local zhenji
	for _,friend in ipairs(self.friends_noself)do
		if friend:getPile("incantation"):length()>0 then continue end
		local reason = getNextJudgeReason(self,friend)
		if reason then
			if reason=="luoshen" or reason=="tenyearluoshen" then
				zhenji = friend
			elseif reason=="indulgence" then
				for _,card in ipairs(cards)do
					if card:getSuit()==sgs.Card_Heart or (friend:hasSkills("hongyan|olhongyan") and card:getSuit()==sgs.Card_Spade)
						and (friend:hasSkill("tiandu") or not self:isValuableCard(card)) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			elseif reason=="supply_shortage" then
				for _,card in ipairs(cards)do
					if card:getSuit()==sgs.Card_Club and (friend:hasSkill("tiandu") or not self:isValuableCard(card)) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			elseif reason=="lightning" and not friend:hasSkills("hongyan|wuyan|olhongyan") then
				for _,card in ipairs(cards)do
					if (card:getSuit()~=sgs.Card_Spade or card:getNumber()==1 or card:getNumber()>9)
						and (friend:hasSkill("tiandu") or not self:isValuableCard(card)) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			elseif reason=="nosmiji" then
				for _,card in ipairs(cards)do
					if card:getSuit()==sgs.Card_Club or (card:getSuit()==sgs.Card_Spade and not friend:hasSkills("hongyan|olhongyan")) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			elseif reason=="nosqianxi" or reason=="tuntian" then
				for _,card in ipairs(cards)do
					if (card:getSuit()~=sgs.Card_Heart and not (card:getSuit()==sgs.Card_Spade and friend:hasSkills("hongyan|olhongyan")))
						and (friend:hasSkill("tiandu") or not self:isValuableCard(card)) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			elseif reason=="tieji" or reason=="caizhaoji_hujia" then
				for _,card in ipairs(cards)do
					if (card:isRed() or card:getSuit()==sgs.Card_Spade and friend:hasSkills("hongyan|olhongyan"))
						and (friend:hasSkill("tiandu") or not self:isValuableCard(card)) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(friend)
						return
					end
				end
			end
		end
	end
	if zhenji then
		for _,card in ipairs(cards)do
			if card:isBlack() and not (zhenji:hasSkills("hongyan|olhongyan") and card:getSuit()==sgs.Card_Spade) then
				use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
				use.to:append(zhenji)
				return
			end
		end
	end
	self:sort(self.enemies)
	for _,enemy in ipairs(self.enemies)do
		if enemy:getPile("incantation"):length()>0 then continue end
		local reason = getNextJudgeReason(self,enemy)
		if not enemy:hasSkill("tiandu") and reason then
			if reason=="indulgence" then
				for _,card in ipairs(cards)do
					if not (card:getSuit()==sgs.Card_Heart or (enemy:hasSkills("hongyan|olhongyan") and card:getSuit()==sgs.Card_Spade))
						and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			elseif reason=="supply_shortage" then
				for _,card in ipairs(cards)do
					if card:getSuit()~=sgs.Card_Club and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			elseif reason=="lightning" and not enemy:hasSkills("hongyan|wuyan|olhongyan") then
				for _,card in ipairs(cards)do
					if card:getSuit()==sgs.Card_Spade and card:getNumber()>=2 and card:getNumber()<=9 then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			elseif reason=="nosmiji" then
				for _,card in ipairs(cards)do
					if card:isRed() or card:getSuit()==sgs.Card_Spade and enemy:hasSkills("hongyan|olhongyan") then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			elseif reason=="nosqianxi" or reason=="tuntian" then
				for _,card in ipairs(cards)do
					if (card:getSuit()==sgs.Card_Heart or card:getSuit()==sgs.Card_Spade and enemy:hasSkills("hongyan|olhongyan"))
						and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			elseif reason=="tieji" or reason=="caizhaoji_hujia" then
				for _,card in ipairs(cards)do
					if (card:getSuit()==sgs.Card_Club or (card:getSuit()==sgs.Card_Spade and not enemy:hasSkills("hongyan|olhongyan")))
						and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			end
		end
	end

	local has_indulgence,has_supplyshortage
	local friend
	for _,p in ipairs(self.friends)do
		if getKnownCard(p,self.player,"Indulgence",true,"he")>0 then
			has_indulgence = true
			friend = p
			break
		end
		if getKnownCard(p,self.player,"SupplySortage",true,"he")>0 then
			has_supplyshortage = true
			friend = p
			break
		end
	end
	if has_indulgence then
		local indulgence = dummyCard("indulgence")
		for _,enemy in ipairs(self.enemies)do
			if enemy:getPile("incantation"):length()>0 then continue end
			if self:hasTrickEffective(indulgence,enemy,friend) and self:playerGetRound(friend)<self:playerGetRound(enemy) and not self:willSkipPlayPhase(enemy) then
				for _,card in ipairs(cards)do
					if not (card:getSuit()==sgs.Card_Heart or (enemy:hasSkills("hongyan|olhongyan") and card:getSuit()==sgs.Card_Spade))
						and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			end
		end
	elseif has_supplyshortage then
		local supplyshortage = dummyCard("supply_shortage")
		for _,enemy in ipairs(self.enemies)do
			if enemy:getPile("incantation"):length()>0 then continue end
			local distance = self:getDistanceLimit(supplyshortage,friend,enemy)
			if self:hasTrickEffective(supplyshortage,enemy,friend) and self:playerGetRound(friend)<self:playerGetRound(enemy)
				and not self:willSkipDrawPhase(enemy) and friend:distanceTo(enemy)<=distance then
				for _,card in ipairs(cards)do
					if card:getSuit()~=sgs.Card_Club and not self:isValuableCard(card) then
						use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
						use.to:append(enemy)
						return
					end
				end
			end
		end
	end

	for _,target in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if target:getPile("incantation"):length()>0 then continue end
		if self:hasEightDiagramEffect(target) then
			for _,card in ipairs(cards)do
				if (card:isRed() and self:isFriend(target)) or (card:isBlack() and self:isEnemy(target)) and not self:isValuableCard(card) then
					use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
					use.to:append(target)
					return
				end
			end
		end
	end

	if self:getOverflow()>0 then
		for _,target in sgs.qlist(self.room:getOtherPlayers(self.player))do
		if target:getPile("incantation"):length()>0 then continue end
			for _,card in ipairs(cards)do
				if not self:isValuableCard(card) and math.random()>0.5 then
					use.card = sgs.Card_Parse("@NewZhoufuCard="..card:getEffectiveId())
					use.to:append(target)
					return
				end
			end
		end
	end
end

sgs.ai_card_intention.NewZhoufuCard = sgs.ai_card_intention.ZhoufuCard
sgs.ai_use_value.NewZhoufuCard = sgs.ai_use_value.ZhoufuCard
sgs.ai_use_priority.NewZhoufuCard = sgs.ai_use_priority.ZhoufuCard

--流矢
local liushi_skill = {}
liushi_skill.name = "liushi"
table.insert(sgs.ai_skills,liushi_skill)
liushi_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@LiushiCard=.")
end

sgs.ai_skill_use_func.LiushiCard = function(card,use,self)
	local hearts = {}
	for _,c in sgs.qlist(self.player:getCards("he"))do
		if c:getSuit()==sgs.Card_Heart and not c:isKindOf("Peach") and not c:isKindOf("ExNihilo") then
			table.insert(hearts,c)
		end
	end
	if self.player:hasSkills(sgs.lose_equip_skill) then self:sortByKeepValue(hearts)
	else self:sortByUseValue(hearts,true) end
	
	if #hearts<=0 then return end
	if hearts[1]:isKindOf("Jink") and self:getCardsNum("Jink")==1 then return end
	
	local slash = dummyCard("slash")
	slash:setSkillName("_liushi")
	if self.player:isLocked(slash) then return end
	local dummy_use = dummy()
	self:useCardSlash(slash,dummy_use)
	if dummy_use.card and dummy_use.to:length()>0 then
		use.card = sgs.Card_Parse("@LiushiCard="..hearts[1]:getEffectiveId())
		use.to:append(dummy_use.to:first()) 
		return
	end
end

sgs.ai_use_value.LiushiCard = sgs.ai_use_value.Slash+0.1
sgs.ai_use_priority.LiushiCard = sgs.ai_use_priority.Slash+0.1

--同疾
sgs.ai_skill_use["@@mobiletongji"] = function(self,prompt,method)
	local others = self.room:getOtherPlayers(self.player)
	local slash = self.player:getTag("mobiletongji-card"):toCard()
	others = sgs.QList2Table(others)
	local source
	for _,player in ipairs(others)do
		if player:hasFlag("MobileTongjiSlashSource") then
			source = player
			break
		end
	end
	self:sort(self.enemies,"defense")

	local doTongji = function(who,source)
		if not who:hasSkill("mobiletongji") then return "." end
		if not self:isFriend(who) and who:hasSkills("leiji|nosleiji|olleiji")
			and (self:hasSuit("spade",true,who) or who:getHandcardNum()>=3)
			and (getKnownCard(who,self.player,"Jink",true)>=1 or self:hasEightDiagramEffect(who)) then
			return "."
		end

		local cards = self.player:getCards("h")
		cards = sgs.QList2Table(cards)
		self:sortByKeepValue(cards)
		for _,card in ipairs(cards)do
			if not self.player:isCardLimited(card,method) and (not source or source:canSlash(who,slash,false)) then
				if self:isFriend(who) and not (isCard("Peach",card,self.player) or isCard("Analeptic",card,self.player)) then
					return "@MobileTongjiCard="..card:getEffectiveId().."->"..who:objectName()
				else
					return "@MobileTongjiCard="..card:getEffectiveId().."->"..who:objectName()
				end
			end
		end

		local cards = self.player:getCards("e")
		cards=sgs.QList2Table(cards)
		self:sortByKeepValue(cards)
		for _,card in ipairs(cards)do
			local range_fix = 0
			if card:isKindOf("Weapon") then range_fix = range_fix+sgs.weapon_range[card:getClassName()]-self.player:getAttackRange(false) end
			if card:isKindOf("OffensiveHorse") then range_fix = range_fix+1 end
			if not self.player:isCardLimited(card,method) and (not source or source:canSlash(who,slash,false)) and self.player:inMyAttackRange(who,range_fix) then
				return "@MobileTongjiCard="..card:getEffectiveId().."->"..who:objectName()
			end
		end
		return "."
	end

	for _,enemy in ipairs(self.enemies)do
		if not (source and source:objectName()==enemy:objectName()) then
			local ret = doTongji(enemy,source)
			if ret~="." then return ret end
		end
	end

	for _,player in ipairs(others)do
		if self:objectiveLevel(player)==0 and not (source and source:objectName()==player:objectName()) then
			local ret = doTongji(player,source)
			if ret~="." then return ret end
		end
	end


	self:sort(self.friends_noself,"defense")
	self.friends_noself = sgs.reverse(self.friends_noself)


	for _,friend in ipairs(self.friends_noself)do
		if not self:slashIsEffective(slash,friend) or self:findLeijiTarget(friend,50,source) then
			if not (source and source:objectName()==friend:objectName()) then
				local ret = doTongji(friend,source)
				if ret~="." then return ret end
			end
		end
	end

	for _,friend in ipairs(self.friends_noself)do
		if self:needToLoseHp(friend,source,dummyCard()) then
			if not (source and source:objectName()==friend:objectName()) then
				local ret = doTongji(friend,source)
				if ret~="." then return ret end
			end
		end
	end

	if (self:isWeak() or self:ajustDamage(source,nil,1,slash)>1) and source:hasWeapon("Axe") and source:getCards("he"):length()>2
	  and not self:getCardId("Peach") and not self:getCardId("Analeptic") then
		for _,friend in ipairs(self.friends_noself)do
			if not self:isWeak(friend) then
				if not (source and source:objectName()==friend:objectName()) then
					local ret = doTongji(friend,source)
					if ret~="." then return ret end
				end
			end
		end
	end

	if (self:isWeak() or self:ajustDamage(source,nil,1,slash)>1) and not self:getCardId("Jink") then
		for _,friend in ipairs(self.friends_noself)do
			if not self:isWeak(friend) or (self:hasEightDiagramEffect(friend) and getCardsNum("Jink",friend)>=1) then
				if not (source and source:objectName()==friend:objectName()) then
					local ret = doTongji(friend,source)
					if ret~="." then return ret end
				end
			end
		end
	end
	return "."
end

sgs.ai_card_intention.MobileTongjiCard = function(self,card,from,to)
	sgs.ai_mobiletongji_effect = true
	if not self:hasExplicitRebel() then sgs.ai_mobiletongji_user = from
	else sgs.ai_mobiletongji_user = nil end
end

--[[function sgs.ai_slash_prohibit.mobiletongji(self,from,to,card)
	
--end]]

--败移
local baiyi_skill = {}
baiyi_skill.name = "baiyi"
table.insert(sgs.ai_skills,baiyi_skill)
baiyi_skill.getTurnUseCard = function(self)
	if self.room:alivePlayerCount()<=2 or self.role=="renegade" then return end
	if #self.friends_noself==0 then return end
	local rene = 0
	for _,ap in sgs.qlist(self.room:getAlivePlayers())do
		if sgs.ai_role[ap:objectName()]=="renegade" then rene = rene+1 end
	end
	if #self.friends+#self.enemies+rene<self.room:alivePlayerCount() then return end
	return sgs.Card_Parse("@BaiyiCard=.")
end

sgs.ai_skill_use_func.BaiyiCard = function(card,use,self)
	if #self.friends_noself==0 then return end
	self:sort(self.friends_noself,"handcard")
	local friend = self.friends_noself[#self.friends_noself]
	local nplayer = self.friends_noself[#self.friends_noself]
	local values,range = {},friend:getAttackRange()
	for i = 1,self.player:aliveCount()do
		local fediff,add,isfriend = 0,0
		local np = nplayer
		for value = #self.friends_noself,1,-1 do
			np = np:getNextAlive()
			if np:objectName()==nplayer:objectName() then
				if self:isFriend(nplayer) then fediff = fediff+value
				else fediff = fediff-value
				end
			else
				if self:isFriend(np) then
					fediff = fediff+value
					if isfriend then add = add+1
					else isfriend = true end
				elseif self:isEnemy(np) then
					fediff = fediff-value
					isfriend = false
				end
			end
		end
		values[nplayer:objectName()] = fediff+add
		nplayer = nplayer:getNextAlive()
	end
	local function get_value(a)
		local ret = 0
		for _,enemy in ipairs(self.enemies)do
			if a:objectName()~=enemy:objectName() and a:distanceTo(enemy)<=range then ret = ret+1 end
		end
		return ret
	end
	local function compare_func(a,b)
		if values[a:objectName()]~=values[b:objectName()] then
			return values[a:objectName()]>values[b:objectName()]
		else
			return get_value(a)>get_value(b)
		end
	end
	local players = sgs.QList2Table(self.room:getAlivePlayers())
	table.sort(players,compare_func)
	if values[players[1]:objectName()]>0 and players[1]:objectName()~=self.player:objectName() and players[1]:objectName()~=friend:objectName() then
		use.card = card
		use.to:append(players[1]) use.to:append(friend)
	end
end

sgs.ai_use_priority.BaiyiCard = 8

--景略
local jinglve_skill = {}
jinglve_skill.name = "jinglve"
table.insert(sgs.ai_skills,jinglve_skill)
jinglve_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@JinglveCard=.")
end

sgs.ai_skill_use_func.JinglveCard = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,p in ipairs(self.enemies)do
		if p:isKongcheng() then continue end
		use.card = card
		use.to:append(p) 
		return
	end
end

sgs.ai_use_priority.JinglveCard = 8
sgs.ai_card_intention.JinglveCard = 80 

--擅立
sgs.ai_skill_playerchosen.shanli = function(self,targets)
	self:sort(self.friends)
	if #self.friends>0 then return self.friends[#self.friends] end
	return self.player
end

sgs.ai_skill_choice.shanli = function(self,choices,data)
	local player = data:toPlayer()
	choices = choices:split(":")
	if player and player:isAlive() then
		if self:isFriend(player) then
			for _,skill in ipairs(choices)do
				if player:hasLordSkill(skill,true) then continue end
				return skill
			end
		else
			for _,skill in ipairs(choices)do
				if not player:hasLordSkill(skill,true) then continue end
				return skill
			end
		end
	end
	return choices[math.random(1,#choices)]
end

--弘仪
local hongyi_skill = {}
hongyi_skill.name = "hongyi"
table.insert(sgs.ai_skills,hongyi_skill)
hongyi_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("@HongyiCard=.")
end

sgs.ai_skill_use_func.HongyiCard = function(card,use,self)
	if #self.enemies==0 then return end
	
	local death = 0
	for _,p in sgs.qlist(self.room:getAllPlayers(true))do
		if p:isDead() then death = death+1 end
		if death>=2 then break end
	end
	
	self:sort(self.enemies,"handcard")
	self.enemies = sgs.reverse(self.enemies)
	local enemy = self.enemies[1]
	for _,p in ipairs(self.enemies)do
		if (p:hasSkill("keji") and not self:hasCrossbowEffect(p)) or self:willSkipPlayPhase(p) then continue end
		enemy = p
		break
	end
	
	if death==0 then
		sgs.ai_use_priority.HongyiCard = 10
		use.card = sgs.Card_Parse("@HongyiCard=.")
		use.to:append(enemy)
		return
	end
	
	local candis = {}
	for _,c in sgs.qlist(self.player:getCards("he"))do
		if self:isValuableCard(c) or not self.player:canDiscard(self.player,c:getEffectiveId()) then continue end
		table.insert(candis,c)
	end
	if #candis<death then return end
	self:sortByKeepValue(candis)
	local dis = {}
	if self:needToThrowArmor() and self.player:canDiscard(self.player,self.player:getArmor():getEffectiveId()) then
		table.insert(dis,self.player:getArmor():getEffectiveId())
	end
	if death>#dis then
		for i = 1,death-#dis do
			table.insert(dis,candis[i]:getEffectiveId())
		end
	end
	use.card = sgs.Card_Parse("@HongyiCard="..table.concat(dis,"+"))
	use.to:append(enemy) 
	return
end

sgs.ai_use_priority.HongyiCard = sgs.ai_use_priority.ExNihilo-0.1
sgs.ai_card_intention.HongyiCard = 80 

--劝封
sgs.ai_skill_choice.quanfeng = function(self,choices,data)
	choices = choices:split(":")
	for _,choice in ipairs(choices)do
		if self.player:hasSkill(choice,true) or string.find(sgs.bad_skills,choice) then continue end
		if self:isValueSkill(choice,nil,true) then
			return choice
		end
	end
	for _,choice in ipairs(choices)do
		if self.player:hasSkill(choice,true) or string.find(sgs.bad_skills,choice) then continue end
		if self:isValueSkill(choice) then
			return choice
		end
	end
	local skills = {}
	for _,choice in ipairs(choices)do
		if self.player:hasSkill(choice,true) or string.find(sgs.bad_skills,choice) then continue end
		table.insert(skills,choice)
	end
	if #skills>0 then return skills[math.random(1,#skills)] end
	for _,choice in ipairs(choices)do
		if string.find(sgs.bad_skills,choice) then continue end
		return choice
	end
	return choices[math.random(1,#choices)]
end

--二版弘仪
local secondhongyi = {}
secondhongyi.name = "secondhongyi"
table.insert(sgs.ai_skills,secondhongyi)
secondhongyi.getTurnUseCard = function(self)
	if #self.enemies==0 then return end
	return sgs.Card_Parse("@SecondHongyiCard=.")
end

sgs.ai_skill_use_func.SecondHongyiCard = function(card,use,self)
	self:sort(self.enemies,"handcard")
	self.enemies = sgs.reverse(self.enemies)
	local enemy = self.enemies[1]
	for _,p in ipairs(self.enemies)do
		if (p:hasSkill("keji") and not self:hasCrossbowEffect(p)) or self:willSkipPlayPhase(p) then continue end
		enemy = p
		break
	end
	sgs.ai_use_priority.SecondHongyiCard = 10
	use.card = card
	use.to:append(enemy) 
end

sgs.ai_use_priority.SecondHongyiCard = sgs.ai_use_priority.ExNihilo-0.1
sgs.ai_card_intention.SecondHongyiCard = 80 

--二版劝封
sgs.ai_skill_invoke.secondquanfeng = function(self,data)
	local target = data:toPlayer()
	if target
	then
		for _,s in sgs.list(target:getSkillList())do
			if s:objectName()=="benghuai"
			then return end
		end
	end
	return true
end



