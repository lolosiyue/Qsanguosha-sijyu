function SmartAI:canAttack(enemy,attacker,nature)
	attacker = attacker or self.player
	nature = nature or sgs.DamageStruct_Normal
	local damage = 1
	if nature==sgs.DamageStruct_Fire and not enemy:hasArmorEffect("SilverLion") then
		if enemy:hasArmorEffect("Vine") then damage = damage+1 end
        if enemy:getMark("&kuangfeng")>0 then damage = damage+1 end
	end
	if #self.enemies==1 or self:hasSkills("jueqing") then return true end
	if self:needToLoseHp(enemy,attacker,false,true) and #self.enemies>1
	or not self:isGoodTarget(enemy,self.enemies)
	or self:objectiveLevel(enemy)<=2
	or self:cantbeHurt(enemy,self.player,damage)
	or not self:damageIsEffective(enemy,nature,attacker)
	or nature~=sgs.DamageStruct_Normal and enemy:isChained() and not self:isGoodChainTarget(enemy,nature)
	then return false end
	return true
end

function SmartAI:hasExplicitRebel()
	for _,p in sgs.qlist(self.room:getAllPlayers())do
		if sgs.ai_role[p:objectName()]=="rebel" and isRolePredictable()
		or self:compareRoleEvaluation(p,"rebel","loyalist")=="rebel" then return true end
	end
	return false
end

function SmartAI:isGoodTarget(to,targets,card)
	if type(targets)=="table" then
		for _,p in ipairs(targets)do
			if self:isGoodTarget(p,nil,card)
			then return p==to or self:isGoodTarget(to,nil,card) end
		end
		return true
	end
	local damageNum = self:ajustDamage(self.player,to,1,card)
	if damageNum==0 then return end
	if to:getMark("hunzi")<1 and to:isLord() and to:getHp()>=2 and math.abs(damageNum)==1
	and sgs.playerRoles["loyalist"]>0 and to:hasSkill("hunzi") then return end
	if damageNum<0 then return true end
	if not to:isLord() and to:getHp()<=damageNum and to:hasSkill("huilei") then
		if self.player:getHandcardNum()>=4 then return end
		return self:compareRoleEvaluation(to,"rebel","loyalist")=="rebel"
	end
	local apn = self:getAllPeachNum(to)
	if apn+to:getHp()>damageNum then
		if to:hasSkill("jieming") and self:getJiemingChaofeng(to)>-4 then return end
		if to:hasSkill("yiji") and not self:findFriendsByType(sgs.Friend_Draw,to) then return end
		if to:hasSkills("nosmiji|miji") then return end
		if to:hasSkills("neoganglie|xuehen|xueji")
		and self:isWeak(self.friends) then return end
		if self.player:getHp()<2 and math.random()<0.5
		and to:hasSkill("ganglie") then return end
		if self.player:getHp()<2 and math.random()<0.7
		and to:hasSkill("nosganglie") then return end
		if to:aliveCount()>2 and to:hasSkill("guixin") then return end
		if math.random()<0.6 and self:needToLoseHp(to,self.player,card) then return end
	end
	if (to:getLostHp()<2 or apn+to:getHp()>damageNum and #self:getFriends(to)>1)
	and to:hasSkill("fangzhu") then return end
	if not to:isLord() and to:hasSkill("wuhun")
	and #self:getFriends(to,true)>0 then
		local maxfriendmark,maxenemymark = 0,0
		for _,friend in ipairs(self.friends)do
            local friendmark = friend:getMark("&nightmare+#"..to:objectName())
			if friendmark>maxfriendmark then maxfriendmark = friendmark end
		end
		for _,enemy in ipairs(self.enemies)do
            local enemymark = enemy:getMark("&nightmare+#"..to:objectName())
			if enemymark>maxenemymark and enemy~=to
			then maxenemymark = enemymark end
		end
		if self:isEnemy(to) then
			if maxfriendmark+damageNum-to:getHp()/2>=maxenemymark
			and not(#self.enemies==1 and #self.friends+#self.enemies==self.room:alivePlayerCount())
            and not(self.player:getMark("&nightmare+#"..to:objectName())==maxfriendmark and self.player:getRole()=="loyalist")
			then return end
		elseif maxfriendmark+damageNum-to:getHp()/2>maxenemymark
		then return end
	end
	if to:getHp()<=damageNum and self.player:getMaxHp()<6
	and not to:isLord() and #self:getFriends(to,true)>0 and to:hasSkill("duanchang") then
		if not(self.player:getMaxHp()>=3 and self.player:getArmor() and self.player:getDefensiveHorse()) then
			if self.player:isLord() and self:isWeak() or self.room:getLord() and self.player:getRole()=="renegade"
			then return end
		end
	end
	if to:hasSkill("tianxiang")
	and getKnownCard(to,self.player,"diamond,club")<to:getHandcardNum() then
		for _,friend in ipairs(self.friends)do
			if friend:getHp()+self:getCardsNum("Peach")-damageNum<2
			then return end
		end
	end
	if card and card:isKindOf("Slash") then
		for _,askill in sgs.qlist(to:getVisibleSkillList(true))do
			local filter = sgs.ai_slash_prohibit[askill:objectName()]
			if type(filter)=="function" and filter(self,self.player,to,card)
			then return end
		end
	end
	return true
end

function SmartAI:getDefenseSlash(to)
	to = to or self.player
	if sgs.turncount<1 then return sgs.getValue(to) end
	local knownJink = getCardsNum("Jink",to,self.player)
	local defense = knownJink*1.2
	local hasEightDiagram = not IgnoreArmor(self.player,to) and to:hasArmorEffect("EightDiagram")
	if hasEightDiagram then
		defense = defense+1.3
		if to:hasSkill("tiandu") then defense = defense+0.6 end
		if to:hasSkill("gushou") then defense = defense+0.4 end
		if to:hasSkill("leiji") then defense = defense+0.4 end
		if to:hasSkill("nosleiji") then defense = defense+0.4 end
		if to:hasSkill("noszhenlie") then defense = defense+0.2 end
		if to:hasSkill("hongyan") then defense = defense+0.2 end
	else
		if to:hasSkill("jijiu") then defense = defense-3 end
		if to:hasSkill("dimeng") then defense = defense-2.5 end
		if knownJink<1 and to:hasSkill("guzheng") then defense = defense-2.5 end
		if to:hasSkill("qiaobian") then defense = defense-2.4 end
		if to:hasSkill("jieyin") then defense = defense-2.3 end
		if to:hasSkills("noslijian|lijian") then defense = defense-2.2 end
		if to:hasSkill("nosmiji") and to:isWounded() then defense = defense-1.5 end
		if knownJink<1 and to:hasSkill("xiliang") then defense = defense-2 end
		if to:hasSkill("shouye") then defense = defense-2 end
	end
	if knownJink>0 then
		if to:hasSkill("mingzhe") then defense = defense+0.2 end
		if to:hasSkill("gushou") then defense = defense+0.2 end
		if to:hasSkills("tuntian+zaoxian") then defense = defense+1.5 end
	end
	if to:getPhase()==sgs.Player_NotActive and to:hasSkill("aocai") then defense = defense+0.5 end
	if to:hasSkill("wanrong") and not hasManjuanEffect(to) then defense = defense+0.5 end
	local hujiaJink = 0
	if to:hasLordSkill("hujia") then
		for _,liege in sgs.qlist(global_room:getLieges("wei",to))do
			if self:compareRoleEvaluation(liege,"rebel","loyalist")==self:compareRoleEvaluation(to,"rebel","loyalist") then
				hujiaJink = hujiaJink+getCardsNum("Jink",liege,self.player)
				if liege:hasArmorEffect("EightDiagram") then hujiaJink = hujiaJink+0.8 end
			end
		end
		defense = defense+hujiaJink
	end
	if to:getMark("@tied")>0 and not self.player:hasSkill("jueqing") then defense = defense+1 end
	if self.player:getPhase()==sgs.Player_Play and self.player:canSlashWithoutCrossbow() then
		local hcard = to:getHandcardNum()
		if self.player:hasSkill("liegong") and (hcard>=self.player:getHp() or hcard<=self.player:getAttackRange()) then defense = 0 end
		if hcard>=self.player:getHp() and self.player:hasSkill("kofliegong") then defense = 0 end
	end
	local jiangqin = global_room:findPlayerBySkillName("niaoxiang")
	local need_double_jink = self.player:hasSkills("wushuang|drwushuang")
				or self.player:hasSkill("roulin") and to:isFemale()
				or to:hasSkill("roulin") and self.player:isFemale()
				or jiangqin and jiangqin:isAdjacentTo(to) and self.player:isAdjacentTo(to) and self:isFriend(jiangqin,attacker)
	if need_double_jink and knownJink<2 and (not to:hasLordSkill("hujia") or hujiaJink<2) then defense = 0 end
	if self.player:hasSkill("dahe") and to:hasFlag("dahe") and knownJink<1 and getKnownNum(player)==player:getHandcardNum()
	and not (to:hasLordSkill("hujia") and hujiaJink>=1) then defense = 0 end
	if to:isCardLimited(dummyCard("jink"),sgs.Card_MethodUse) then defense = 0 end
	if to:hasFlag("QianxiTarget") then
		if to:getMark("@qianxi_red")>0 then
			if to:hasSkill("qingguo")
			or to:hasSkill("longhun") and to:isWounded()
			then defense = defense-1 else defense = 0 end
		elseif to:getMark("@qianxi_black")>0 then
			if to:hasSkill("qingguo")
			then defense = defense-1 end
		end
	end
	defense = defense+math.min(to:getHp()*0.45,10)
	if not self.player:hasSkill("jueqing") then
		for _,masochism in ipairs(sgs.masochism_skill:split("|"))do
			if to:hasSkill(masochism) and self:isGoodHp(to)
			then defense = defense+1 end
		end
		if to:hasSkill("jieming") then defense = defense+4 end
		if to:hasSkill("yiji") then defense = defense+4 end
		if to:hasSkill("guixin") then defense = defense+4 end
		if to:hasSkill("yuce") then defense = defense+2 end
	end
	if not self:isGoodTarget(to) then defense = defense+10 end
	if to:getHp()>2 and to:hasSkills("nosrende|rende") then defense = defense+1 end
	if to:getHp()>1 and to:hasSkill("kuanggu") then defense = defense+0.2 end
	if to:getHp()>1 and to:hasSkill("zaiqi") then defense = defense+0.35 end
	if to:hasSkill("tianming") then defense = defense+0.1 end
	if to:getHp()>getBestHp(to) then defense = defense+0.8 end
	if to:getHp()<=2 then defense = defense-0.4 end
	local playernum = global_room:alivePlayerCount()
	if playernum>3 and (to:getSeat()-self.player:getSeat())%playernum>=playernum-2
	and to:getHandcardNum()<=2 and to:getHp()<=2 then defense = defense-0.4 end
	if to:hasSkill("tianxiang") then defense = defense+to:getHandcardNum()*0.5 end
	if to:getHandcardNum()+hujiaJink<1 and not to:hasSkill("kongcheng") then
		if to:getHp()<=1 then defense = defense-2.5 end
		if to:getHp()==2 then defense = defense-1.5 end
		if not hasEightDiagram then defense = defense-2 end
		if self.player:hasWeapon("GudingBlade") and to:getHandcardNum()<1
		and not(to:hasArmorEffect("SilverLion") and not IgnoreArmor(self.player,to))
		then defense = defense-2 end
	end
	for _,c in sgs.qlist(self.player:getHandcards())do
		if (c:isKindOf("FireSlash") or c:objectName()=="slash" and self.player:hasWeapon("Fan"))
		and not IgnoreArmor(self.player,to) and to:hasArmorEffect("Vine")
		then defense = defense-0.6 break end
	end
 	if isLord(to) then
		defense = defense-0.4
		if sgs.isLordInDanger() then defense = defense-0.7 end
	end
	if not to:faceUp() then defense = defense-0.35 end
	if to:containsTrick("indulgence") and not to:containsTrick("YanxiaoCard") then defense = defense-0.15 end
	if to:containsTrick("supply_shortage") and not to:containsTrick("YanxiaoCard") then defense = defense-0.15 end
	if to:isFemale() and self.player:hasSkill("roulin") or self.player:isFemale() and to:hasSkill("roulin")
	then defense = defense-2.4 end
	return defense
end

function SmartAI:slashProhibit(card,enemy,from)
	if sgs.getMode:find("_mini_36")
	then return self.player:hasSkill("keji") end
	card = card or dummyCard()
	from = from or self.player
	if from:isProhibited(enemy,card) then return true end
	for _,askill in sgs.list(enemy:getVisibleSkillList(true))do
		local filter = sgs.ai_slash_prohibit[askill:objectName()]
		if type(filter)=="function" and filter(self,from,enemy,card)
		then return true end
	end
	if self:isFriend(enemy,from) and enemy:isLord()
	and self:isWeak(enemy) then return true end
end

function SmartAI:canLiuli(other,another)
	if not other:hasSkill("liuli") then return end
	if type(another)=="table"
	then
		for _,target in sgs.list(another)do
			if target:getHp()<3 and self:canLiuli(other,target)
			then return true end
		end
		return
	end
	if not self:needToLoseHp(another,self.player,dummyCard()) then return end
	if other:getHandcardNum()>0 and other:distanceTo(another)<=other:getAttackRange()
	or other:getWeapon() and other:getOffensiveHorse() and other:distanceTo(another)<=other:getAttackRange()
	or (other:getWeapon() or other:getOffensiveHorse()) and other:distanceTo(another)<=1
	then return true end
end

sgs.ai_target_revises.yizhong = function(to,card,self)
	if not to:getArmor() and card:isBlack() and card:isKindOf("Slash")
	then return true end
end

sgs.ai_target_revises.xiemu = function(to,card,self)
	if card:isBlack() and card:getTypeId()>0
	and to:getMark("@xiemu_"..self.player:getKingdom())>0
	then return math.random()<0.7 end
end

sgs.ai_target_revises.renwang_shield = function(to,card,self)
	if card:isBlack() and card:isKindOf("Slash")
	then return true end
end

function SmartAI:slashIsEffective(slash,to,from,ignore_armor)
	from = from or self.player
    if from:isProhibited(to,slash) then return end
	local use = {card=slash,from=from,to=sgs.SPlayerList()}
	use.to:append(to)
	ignore_armor = ignore_armor or slash:hasFlag("Qinggang") or not to:hasArmorEffect(nil)
	for _,sk in ipairs(aiConnect(to))do
		if ignore_armor and sgs.armorName[sk] then continue end
		local tr = sgs.ai_target_revises[sk]
       	if type(tr)=="function" and tr(to,slash,self,use)
		then return end
	end
	return self:ajustDamage(from,to,1,slash)~=0
end

function SmartAI:slashIsAvailable(player,slash) -- @todo: param of slashIsAvailable
	player = player or self.player
	if slash then return (slash==true and dummyCard() or slash):isAvailable(player) end
	for _,s in ipairs(sgs.ais[player:objectName()]:getCards("Slash"))do
		if s:isAvailable(player) then return true end
	end
	return false
end

function sgs.isJinkAvailable(from,to,slash,judge_considered)
	return not judge_considered and from:hasSkills("tieji|nostieji")
	or from:hasSkill("liegong") and from:getPhase()==sgs.Player_Play and (to:getHandcardNum()<=from:getAttackRange() or to:getHandcardNum()>=from:getHp())
	or from:hasSkill("kofliegong") and from:getPhase()==sgs.Player_Play and to:getHandcardNum()>=from:getHp()
	or from:hasFlag("ZhaxiangInvoked") and slash and slash:isRed()
end

function SmartAI:findWeaponToUse(enemy)
	local wv,w = {},self.player:getWeapon()
	for _,c in sgs.list(self:addHandPile())do
		if c:isKindOf("Weapon") and self:aiUseCard(c).card
		then wv[c] = self:evaluateWeapon(c,self.player,enemy) end
	end
	if #wv<1 then return end
	if w then wv[w] = self:evaluateWeapon(w,self.player,enemy) end
	local max_value,max_card = -100,nil
	for c,v in pairs(wv)do
		if v>max_value then max_card = c max_value = v end
	end
	if w and w:getId()==max_card:getId()
	then else return max_card end
end

function SmartAI:isPriorFriendOfSlash(friend,card,source)
	source = source or self.player
	if card:isKindOf("NatureSlash") and hasChainEffect(friend,source)
	and self:isGoodChainTarget(friend,card,source) then return true end
	if ((friend:isWounded() and self:findLeijiTarget(friend,50,source,1) or self:findLeijiTarget(friend,50,source,-1))
	or friend:isLord() and source:hasSkill("guagu") and friend:getLostHp()>=1 and getCardsNum("Jink",friend,source)<1
	or friend:hasSkill("jieming") and source:hasSkill("nosrende") and self:hasSkills("jijiu",self.friends)
	or friend:hasSkill("hunzi") and friend:getHp()==2 and self:needToLoseHp(friend,source,card))
	and card:getSkillName()~="lihuo" and self:ajustDamage(source,friend,1,card)==1
	or self:hasQiuyuanEffect(source,friend)
	then return true end
end

sgs.ai_use_revises.qingnang = function(self,card,use)
	if card:isKindOf("Slash") and self:isWeak()
	and self:getOverflow()<=0
	then return false end
end

sgs.ai_use_revises.keji = function(self,card,use)
	if card:isKindOf("Slash") and not self:hasCrossbowEffect()
	and (#self.enemies>1 or #self.friends>1) and self:getOverflow()>1
	then return false end
end

sgs.ai_used_revises.jilve = function(self,use)
	if use.card:isKindOf("Slash")
	and self.player:getMark("&bear")>0
	and not self.player:hasFlag("JilveWansha")
	and not use.isDummy then
		for _,to in sgs.list(use.to)do
			if self:isEnemy(to) and to:getHp()<2 and not self.player:hasSkill("wansha")
			and getCardsNum("Jink",to,self.player)<1 then
				use.card = sgs.Card_Parse("@JilveCard=.")
				sgs.ai_skill_choice.jilve = "wansha"
				use.to = sgs.SPlayerList()
				return false
			end
		end
	end
end

sgs.ai_used_revises.duyi = function(self,use)
	if use.card:isDamageCard()
	and self.room:getDrawPile():length()>0
	and not self.player:hasUsed("DuyiCard")
	and not use.isDummy then
		for _,to in sgs.list(use.to)do
			if self:isEnemy(to) and (to:getHp()<=2 or self:ajustDamage(self.player,to,1,use.card)>1) then
				sgs.ai_duyi = {id=self.room:getDrawPile():first(),tg=to}
				use.card = sgs.Card_Parse("@DuyiCard=.")
				use.to = sgs.SPlayerList()
				return false
			end
		end
	end
end

sgs.ai_used_revises.wuqian = function(self,use)
	if use.card:isKindOf("Slash")
	and self.player:getMark("&wrath")>2
	and not use.isDummy
	then
		for _,to in sgs.list(use.to)do
			if self:isEnemy(to) and self:isWeak(to)
			and to:getMark("Armor_Nullified")<1
			then
				use.card = sgs.Card_Parse("@WuqianCard=.")
				use.to = sgs.SPlayerList()
				use.to:append(to)
				return false
			end
		end
	end
end

sgs.ai_target_revises.jianxiong = function(to,card,self)
	if card:isDamageCard()
	and not self:isFriend(to)
	and card:subcardsLength()>0
	then
		for _,id in sgs.list(card:getSubcards())do
			if isCard("Peach,Analeptic",sgs.Sanguosha:getCard(id),to)
			then return true end
		end
	end
end

sgs.ai_target_revises.liuli = function(to,card,self,use)
	if card:isKindOf("Slash")
	then
		for _,friend in sgs.list(self.friends_noself)do
			if self:canLiuli(to,friend)
			and use.to:length()<2 and friend:getHp()<3
			then return true end
		end
	end
end

sgs.ai_target_revises.xiangle = function(to,card,self,use)
	if card:isKindOf("Slash")
	and self:getCardsNum("BasicCard")-self:getCardsNum("Peach")<2
	then return true end
end

function hasChainEffect(to,from)
	if to:isChained() then return true end
	if to:hasSkill("qianjie") then return end
	if from and from:hasWeapon("wutiesuolian") then return true end
end

function isCurrent(use,to)
	if type(use.current_targets)=="table" then
		for _,p in ipairs(use.current_targets)do
			if p==to or p==to:objectName()
			then return true end
		end
	elseif type(use.current_targets)=="userdata"
	then return use.current_targets:contains(to) end
end

function SmartAI:useCardSlash(card,use)
	local extraTarget = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,card)
	if use.extra_target then extraTarget = extraTarget+use.extra_target end
	function slashNoTarget(target)
		if isCurrent(use,target)
		or use.to:contains(target) then return end
		if use.card and use.card~=card then
			extraTarget = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,use.card)
			if use.extra_target then extraTarget = extraTarget+use.extra_target end
		end
		if use.to:length()>extraTarget then return true end
		if CanToCard(use.card or card,self.player,target)
		and self:ajustDamage(self.player,target,1,use.card or card)~=0 then
			if card:isKindOf("NatureSlash")
			and hasChainEffect(target,self.player) then
				if self:isGoodChainTarget(target,card) then
					use.card = card
					use.to:append(target)
				end
			else
				use.card = card
				use.to:append(target)
			end
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if sgs.ai_role[friend:objectName()]==sgs.ai_role[self.player:objectName()]
		and self:isPriorFriendOfSlash(friend,use.card or card)
		and slashNoTarget(friend) then return end
	end
	local forbidden = {}
	self:sort(self.enemies,"defenseSlash")
	for _,enemy in ipairs(self.enemies)do
		if self:isGoodTarget(enemy,self.enemies,use.card or card) then
			if self:objectiveLevel(enemy)<=3
			or self:hasQiuyuanEffect(self.player,enemy)
			then table.insert(forbidden,enemy)
			elseif slashNoTarget(enemy)
			then return end
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if sgs.ai_role[friend:objectName()]==sgs.ai_role[self.player:objectName()]
		and (use.card or card):getSkillName()~="lihuo"
		and not(friend:isLord() and #self.enemies<1)
		and self:needToLoseHp(friend,self.player,use.card or card,true)
		and self:ajustDamage(self.player,friend,1,use.card or card)==1
		and slashNoTarget(friend) then return end
	end
	for _,target in ipairs(forbidden)do
		if slashNoTarget(target) then return end
	end
end

sgs.ai_skill_use.slash = function(self,prompt)
	local parsed = prompt:split(":")
	if self.player:hasFlag("slashTargetFixToOne") then
		local target,target2 = nil,nil
		for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
			if p:hasFlag("SlashAssignee") then target = p break end
		end
		if not target then return "." end
		local callback = sgs.ai_skill_cardask[parsed[1]]
		if type(callback)=="function" then
			if parsed[1]~="@niluan-slash" and target:hasSkills("xiansi|tenyearxiansi")
			and target:getPile("counter"):length()>1 then
				local d = dummyCard()
				d:setSkillName("_xiansi")
				if CanToCard(d,self.player,target) then
					local ids = {}
					for _,id in sgs.list(target:getPile("counter"))do
						table.insert(ids,id)
						if #ids>=2 then break end
					end
					return "@XiansiSlashCard="..table.concat(ids,"+").."->"..target:objectName()
				end
			end
			if #parsed>=3 then target2 = BeMan(self.room,parsed[3]) end
			callback = callback(self,ToData(target),"Slash",target,target2,prompt)
			if callback=="." then return "." end
			callback = sgs.Card_Parse(callback)
			if CanToCard(callback,self.player,target) then
				local targets,use = {},{to=sgs.SPlayerList(),current_targets=self.room:getOtherPlayers(target)}
				self:useCardSlash(callback,use)
				if use.to:contains(target) then
					use.card = nil
					use.current_targets = {}
					table.insert(use.current_targets,target)
					self:useCardSlash(callback,use)
					for _,p in sgs.list(use.to)do table.insert(targets,p:objectName()) end
					return callback:toString().."->"..table.concat(targets,"+")
				end
			end
		end
		for _,slash in sgs.list(self:getCard("Slash",true))do
			local dc = slash
			if slash:isKindOf("SkillCard") then
				dc = slash:toString():split("->")[1]:split(":")
				dc = dummyCard(dc[#dc])
				if dc then
					dc:addSubcards(slash:getSubcards())
					dc:setNumber(slash:getNumber())
					dc:setSuit(slash:getSuit())
				end
			end
			if dc then
				local d = self:aiUseCard(dc,dummy(true,0,self.room:getOtherPlayers(target)))
				if d.card and d.to:contains(target) then
					dc = {}
					for _,p in sgs.list(d.to)do
						table.insert(dc,p:objectName())
					end
					return slash:toString().."->"..table.concat(dc,"+")
				end
			end
		end
	else
		for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
			if p:hasSkills("xiansi|tenyearxiansi") and p:getPile("counter"):length()>1
			and parsed[1]~="@niluan-slash" then
				local d = dummyCard()
				d:setSkillName("_xiansi")
				if CanToCard(d,self.player,p) then
					local ids = {}
					for _,id in sgs.list(p:getPile("counter"))do
						table.insert(ids,id)
						if #ids>=2 then break end
					end
					return "@XiansiSlashCard="..table.concat(ids,"+").."->"..p:objectName()
				end
			end
		end
		for _,slash in sgs.list(self:getCard("Slash",true))do
			local dc = slash
			if slash:isKindOf("SkillCard") then
				dc = slash:toString():split("->")[1]:split(":")
				dc = dummyCard(dc[#dc])
				if dc then
					dc:addSubcards(slash:getSubcards())
					dc:setNumber(slash:getNumber())
					dc:setSuit(slash:getSuit())
				end
			end
			if dc then
				local d = self:aiUseCard(dc)
				if d.card then
					dc = {}
					for _,p in sgs.list(d.to)do
						table.insert(dc,p:objectName())
					end
					return slash:toString().."->"..table.concat(dc,"+")
				end
			end
		end
	end
	return "."
end

sgs.ai_skill_playerchosen.slash_extra_targets = function(self,targets)
	local slash = dummyCard()
	targets = sgs.QList2Table(targets)
	self:sort(targets,"defenseSlash")
	for _,target in sgs.list(targets)do
		if self:isEnemy(target)
		and not self:slashProhibit(slash,target)
		and self:isGoodTarget(target,targets,slash)
		and self:slashIsEffective(slash,target)
		then return target end
	end
	return nil
end

sgs.ai_skill_playerschosen.slash_extra_targets = function(self,targets,x,n)
	local slash = dummyCard()
	targets = sgs.QList2Table(targets)
	self:sort(targets,"defenseSlash")
	local tos = {}
	for _,target in sgs.list(targets)do
		if #tos<x and self:isEnemy(target)
		and not self:slashProhibit(slash,target)
		and self:isGoodTarget(target,targets,slash)
		and self:slashIsEffective(slash,target)
		then table.insert(tos,target) end
	end
	return tos
end

sgs.ai_skill_playerchosen.zero_card_as_slash = function(self,targets)
	local slash = dummyCard()
	local targetlist = sgs.QList2Table(targets)
	local arrBestHp,canAvoidSlash,forbidden = {},{},{}
	self:sort(targetlist,"defenseSlash")
	for _,target in sgs.list(targetlist)do
		if self:isEnemy(target)
		and not self:slashProhibit(slash,target)
		and self:isGoodTarget(target,targetlist,slash) then
			if self:slashIsEffective(slash,target) then
				if self:needToLoseHp(target,self.player,slash) 
				or self:needLeiji(target,self.player)
				then table.insert(forbidden,target)
				elseif self:needToLoseHp(target,self.player,slash,true)
				then table.insert(arrBestHp,target)
				else return target end
			else
				table.insert(canAvoidSlash,target)
			end
		end
	end
	targetlist = sgs.reverse(targetlist)
	for _,target in sgs.list(targetlist)do
		if not self:slashProhibit(slash,target) then
			if self:slashIsEffective(slash,target) then
				if self:isFriend(target)
				and (self:needToLoseHp(target,self.player,slash,true) or self:needLeiji(target,self.player))
				then return target end
			else
				table.insert(canAvoidSlash,target)
			end
		end
	end
	if #canAvoidSlash>0 then return canAvoidSlash[1] end
	if #arrBestHp>0 then return arrBestHp[1] end
	for _,target in sgs.list(targetlist)do
		if target:objectName()~=self.player:objectName()
		and not self:isFriend(target) and not table.contains(forbidden,target)
		then return target end
	end
	return targetlist[1]
end

sgs.ai_card_intention.Slash = function(self,card,from,tos)
	if sgs.ai_liuli_user then
		sgs.updateIntention(from,sgs.ai_liuli_user,10)
		sgs.ai_liuli_user = nil
		return
	end
	if sgs.ai_mobiletongji_user then
		sgs.updateIntention(from,sgs.ai_mobiletongji_user,10)
		sgs.ai_mobiletongji_user = nil
		return
	end
	if sgs.ai_collateral then sgs.ai_collateral = false return end
	if card:getSkillName():match("huqi") or card:getSkillName():match("mizhao")
	or card:hasFlag("nosjiefan-slash") then return end
	for _,to in sgs.list(tos)do
		local d = math.abs(self:ajustDamage(from,to,1,card))
		if self:hasQiuyuanEffect(from,to)
		and d<2 and (self:hasExplicitRebel() or sgs.explicit_renegade) and not self:canLiegong(to,from)
		or d==1 and self:needToLoseHp(to,from,card,true)
		or to:getHp()>2+d and from:hasSkill("pojun")
		then continue end
		local value = 80
		if self:needLeiji(to,from) then value = from:getState()=="online" and 0 or -10 end
		if to:isLord() and sgs.turncount<2 and to:hasSkill("fangzhu") then value = 10 end
		sgs.updateIntention(from,to,value)
	end
end

sgs.ai_skill_cardask["slash-jink"] = function(self,data,pattern,target)
	local slash = dummyCard()
	local j = 1
	if type(data)=="userdata" then
		slash = data:toCardEffect().card
		j = data:toCardEffect().offset_num
	end
	if not target or sgs.ai_skill_cardask.nullfilter(self,data,pattern,target)=="."
	or slash:isKindOf("NatureSlash") and self.player:isChained() and self:isGoodChainTarget(self.player,slash,target)
	or self:isFriend(target) and slash:hasFlag("nosjiefan-slash") then return "." end
	local n = self:ajustDamage(target,nil,1,slash)
	if n==1 and self:needToLoseHp(self.player,target,slash)
	then return "." end
	function getJink()
		if sgs.AIHumanized then
			self.room:getThread():delay(math.random(global_delay*0.3,global_delay*1.3))
		end
		local js = self:getCard("Jink",true)
		if j>#js then return "." end
		for _,c in sgs.list(js)do
			if self.player:hasFlag("dahe") and c:getSuit()~=sgs.Card_Heart then continue end
			return c:toString()
		end
		if sgs.AIHumanized and math.random()<0.5 and #self.friends_noself>0
		and self:getCardsNum("Peach,Analeptic")<1 and self.player:getHp()<=math.abs(n) then
			self:speak("noJink")
			self.room:getThread():delay(math.random(global_delay*0.5,global_delay*1.5))
		end
		return "."
	end
	if self:isFriend(target) then
		if self:findLeijiTarget(self.player,50,target) then return getJink() end
		if self.player:getLostHp()<1 and self.player:isMale() and target:hasSkill("jieyin") then return "." end
		if not hasJueqingEffect(target,self.player) then
			if (target:hasSkill("nosrende") or target:hasSkill("rende") and not target:hasUsed("RendeCard")) and self.player:hasSkill("jieming")
			or target:hasSkill("pojun") and not self.player:faceUp()
			then return "." end
		end
	else
		if n>1 or n>=self.player:getHp() then return getJink() end
		local current = self.room:getCurrent()
		if current and current:hasSkill("juece") and self.player:getHp()>0 then
			for _,c in sgs.list(self:getCards("Jink"))do
				if self.player:isLastHandCard(c,true)
				then return "." end
			end
		end
		if self.player:getHandcardNum()==1 and self:needKongcheng()
		or not(self:hasLoseHandcardEffective() or self.player:isKongcheng()) then return getJink() end
		if target:hasSkill("mengjin") and not(target:hasSkill("nosqianxi") and target:distanceTo(self.player)==1) then
			if self.player:getCardCount()<2 and not self.player:getArmor() then return getJink() end
			if self:getCardsNum("Peach,Analeptic")>0 and self:isWeak() and not(self.player:hasSkills("tuntian+zaoxian") or self:willSkipPlayPhase())
			or self.player:getCardCount()>1 and self.player:hasSkills("jijiu|qingnang")
			or self:canUseJieyuanDecrease(target) then return "." end
		end
		if self.player:getHp()>1 and self:getCardsNum("Jink")<2 and getKnownCard(target,self.player,"Analeptic")>0
		and target:getPhase()<=sgs.Player_Play and self:slashIsAvailable(target) and target:canSlash(self.player)
		then return "." end
		if not(target:hasSkill("nosqianxi") and target:distanceTo(self.player)==1) then
			if target:hasWeapon("Axe") then
				if target:hasSkills(sgs.lose_equip_skill) and target:getEquips():length()>1 and target:getCardCount()>2
				or target:getHandcardNum()-target:getHp()>2 and not self:isWeak() and self:getOverflow()>0
				then return "." end
			elseif target:hasWeapon("Blade") then
				if math.abs(n)>1 or self.player:hasArmorEffect("EightDiagram")
				or self.player:hasArmorEffect("renwang_shield")
				or slash:isKindOf("NatureSlash") and self.player:hasArmorEffect("Vine")
				or self.player:getHp()<2 and #self.friends_noself<1 then
				elseif self.player:hasSkill("jijiu") and getKnownCard(self.player,self.player,"red")>0
				or (self:getCardsNum("Jink")<=getCardsNum("Slash",target,self.player) or self.player:hasSkill("qingnang")) and self.player:getHp()>1
				or self:canUseJieyuanDecrease(target)
				then return "." end
			end
		end
		if slash:objectName()=="yj_stabs_slash"
		and self.player:getHandcardNum()>1 and self.player:getHp()>1 then
			current = self:sortByKeepValue(self.player:getHandcards())
			if isCard("Jink",current[1],self.player) then if self:getKeepValue(current[2])>7 then return "." end
			elseif self:getKeepValue(current[1])>6 then return "." end
		end
	end
	return getJink()
end

sgs.ai_skill_use["slash-jink"] = function(self,prompt)
	local data = self.room:getTag("SlashData")
	return sgs.ai_skill_cardask["slash-jink"](self,data,"jink",data:toCardEffect().from)
end

sgs.dynamic_value.damage_card.Slash = true

sgs.ai_use_value.Slash = 4.5
sgs.ai_keep_value.Slash = 3.6
sgs.ai_use_priority.Slash = 2.6

function SmartAI:canHit(to,from,conservative)
	from = from or self.room:getCurrent()
	to = to or self.player
	local jink = dummyCard("jink")
	if to:isCardLimited(jink,sgs.Card_MethodUse) then return true end
	if self:canLiegong(to,from) then return true end
	if not self:isFriend(to,from) then
		if from:hasWeapon("Axe") and from:getCards("he"):length()>2 then return true end
		if from:hasWeapon("blade") and getCardsNum("Jink",to,from)<=getCardsNum("Slash",from,from) then return true end
		if from:hasSkill("mengjin") and not (from:hasSkill("nosqianxi") and not from:hasSkill("jueqing") and from:distanceTo(to)==1)
		and self:ajustDamage(from,to,1,dummyCard())<2 and not self:needLeiji(to,from) then
			if not self:doDisCard(to,"he",true) then
			elseif to:getCards("he"):length()==1 and not to:getArmor() then
			elseif self:canUseJieyuanDecrease(from,to) then return false
			elseif self:willSkipPlayPhase() then
			elseif (getCardsNum("Peach",to,from)>0 or getCardsNum("Analeptic",to,from)>0) then return true
			elseif not self:isWeak(to) and to:getArmor() and not self:needToThrowArmor() then return true
			elseif not self:isWeak(to) and to:getDefensiveHorse() then return true end
		end
	end

	local hasHeart,hasRed,hasBlack
	for _,card in sgs.list(self:getCards("Jink"))do
		if card:getSuit()==sgs.Card_Heart then hasHeart = true end
		if card:isRed() then hasRed = true end
		if card:isBlack() then hasBlack = true end
	end
	if to:hasFlag("dahe") and not hasHeart then return true end
	if to:getMark("@qianxi_red")>0 and not hasBlack then return true end
	if to:getMark("@qianxi_black")>0 and not hasRed then return true end
	if not conservative and self:ajustDamage(from,to,1,dummyCard())>1 then conservative = true end
	if not conservative and from:hasSkill("moukui") then conservative = true end
	if not conservative and to:hasArmorEffect("EightDiagram") and not IgnoreArmor(from,to) then return false end
	local need_double_jink = from and (from:hasSkill("wushuang") or (from:hasSkill("roulin") and to:isFemale()) or (from:isFemale() and to:hasSkill("roulin")))
	if to:objectName()==self.player:objectName() then
		if getCardsNum("Jink",to,from)==0 then return true end
		if need_double_jink and getCardsNum("Jink",to,from)<2 then return true end
	end
	if getCardsNum("Jink",to,from)==0 then return true end
	if need_double_jink and getCardsNum("Jink",to,from)<2 then return true end
	return false
end

sgs.ai_use_revises.yongsi = function(self,card,use)
	if card:isKindOf("Peach")
	and self:getCardsNum("Peach")>self:getOverflow(nil,true)
	then use.card = card end
end

sgs.ai_use_revises.longhun = function(self,card,use)
	if card:isKindOf("Peach") and not self.player:isLord()
	and math.min(self.player:getMaxCards(),self.player:getHandcardNum())+self.player:getEquips():length()>3
	then return false end
end

sgs.ai_use_revises.hunzi = function(self,card,use)
	if card:isKindOf("Peach") and self.player:isLord() and self.player:getMark("hunzi")<1
	and self:getCardsNum("Peach","h")<self.player:getHp() and self.player:getHp()<4
	then return false end
end

sgs.ai_use_revises.silver_lion = function(self,card,use)
	if card:isKindOf("Peach")
	then
		for _,h in sgs.list(self.player:getHandcards())do
			if h:isKindOf("Armor") and self:evaluateArmor(h)>0
			then
				use.card = h
				return
			end
		end
	end
end

sgs.ai_use_revises.nosbuqu = function(self,card,use)
	if card:isKindOf("Peach")
	and self.player:getHp()<1 and self.player:getMaxCards()<1
	then use.card = card end
end

sgs.ai_use_revises.kuanggu = function(self,card,use)
	if card:isKindOf("Peach")
	and not hasJueqingEffect(self.player)
	and self.player:getOffensiveHorse()
	and self.player:getLostHp()==1
	and self:getOverflow()<1
	then return false end
end

sgs.ai_use_revises.jieyin = function(self,card,use)
	if card:isKindOf("Peach")
	and self:getOverflow()>0
	then
		for _,friend in sgs.list(self.friends)do
			if friend:isWounded() and friend:isMale()
			then return false end
		end
	end
end

sgs.ai_use_revises.ganlu = function(self,card,use)
	if card:isKindOf("Peach") and not self.player:hasUsed("GanluCard")
	and self:aiUseCard(sgs.Card_Parse("@GanluCard=.")).card
	then return false end
end

function SmartAI:useCardPeach(card,use)
	for _,enemy in sgs.list(self.enemies)do
		if self.player:getHandcardNum()<3 and enemy:hasSkills(sgs.drawpeach_skill
		or getCardsNum("Dismantlement",enemy)>0
		or enemy:hasSkill("jixi") and enemy:distanceTo(self.player)<2
		or enemy:hasSkill("qixi") and getKnownCard(enemy,self.player,"black",nil,"he")>0
		or getCardsNum("Snatch",enemy)>0 and enemy:distanceTo(self.player)==1
		or enemy:hasSkill("tiaoxin") and (self.player:inMyAttackRange(enemy) and self:getCardsNum("Slash")<1 or not self.player:canSlash(enemy)))
		then use.card = card return end
	end
	local lord = getLord(self.player)
	if not(lord and self:isFriend(lord) and self:isWeak(lord)) and self.player:getHp()<2
	or self.player:getLostHp()>=2 and hasWulingEffect("@water")
	or self:getCardsNum("Peach","h")>self.player:getHp()
	or not self.player:hasCard(card)
	then use.card = card return end
	local of = self:getOverflow()
	if of<1 and #self.friends_noself>0
	or self:needToLoseHp(self.player,nil,card,nil,true)
	then return end
	if lord and lord:getHp()<=2 and self:isFriend(lord)
	and self:isWeak(lord) then
		if self.player==lord
		or self:getCardsNum("Peach")>1 and self:getCardsNum("Peach,Jink")>self.player:getMaxCards()
		then use.card = card end
		return
	end
	self:sort(self.friends,"hp")
	if #self.friends>0 and self.friends[1]==self.player
	or self.player:getHp()<2 then use.card = card return end
	if #self.friends>1 and (not hasBuquEffect(self.friends[2]) and self.friends[2]:getHp()<3 and of<2
	or not hasBuquEffect(self.friends[1]) and self.friends[1]:getHp()<2 and self:getCardsNum("Peach","h")<=1 and of<3)
	then return end
	use.card = card
end

sgs.ai_card_intention.Peach = function(self,card,from,tos)
	for _,to in sgs.list(tos)do
		if to:hasSkill("wuhun") then continue end
		if not isRolePredictable() and from:objectName()~=to:objectName()
		and sgs.playerRoles["renegade"]>0 and sgs.ai_role[to:objectName()]=="rebel"
		and (sgs.ai_role[from:objectName()]=="loyalist" or sgs.ai_role[from:objectName()]=="renegade") then
			outputRoleValues(from,100)
			sgs.roleValue[from:objectName()]["renegade"] = sgs.roleValue[from:objectName()]["renegade"]+100
			outputRoleValues(from,100)
		end
		sgs.updateIntention(from,to,-120)
	end
end

sgs.ai_use_value.Peach = 6
sgs.ai_keep_value.Peach = 7
sgs.ai_use_priority.Peach = 0.9

sgs.ai_use_value.Jink = 8.9
sgs.ai_keep_value.Jink = 5.2

sgs.dynamic_value.benefit.Peach = true

sgs.ai_keep_value.Weapon = 2.08
sgs.ai_keep_value.Armor = 2.06
sgs.ai_keep_value.Horse = 2.04

sgs.weapon_range.Weapon = 1
sgs.weapon_range.Crossbow = 1
sgs.weapon_range.DoubleSword = 2
sgs.weapon_range.QinggangSword = 2
sgs.weapon_range.IceSword = 2
sgs.weapon_range.GudingBlade = 2
sgs.weapon_range.Axe = 3
sgs.weapon_range.Blade = 3
sgs.weapon_range.spear = 3
sgs.weapon_range.Halberd = 4
sgs.weapon_range.KylinBow = 5

sgs.ai_skill_invoke.double_sword = function(self,data)
	return not self:needKongcheng(self.player,true)
end

function sgs.ai_slash_weaponfilter.double_sword(self,to,player)
	return player:distanceTo(to)<=math.max(sgs.weapon_range.DoubleSword,player:getAttackRange()) and player:getGender()~=to:getGender()
end

function sgs.ai_weapon_value.double_sword(self,enemy,player)
	if enemy and enemy:isMale()~=player:isMale() then return 4 end
end

function SmartAI:getExpectedJinkNum(use)
	local jink_list = use.from:getTag("Jink_"..use.card:toString()):toStringList()
	local index,jink_num = 1,1
	for _,p in sgs.list(use.to)do
		if p:objectName()==self.player:objectName() then
			local n = tonumber(jink_list[index])
			if n==0 then return 0
			elseif n>jink_num then jink_num = n end
		end
		index = index+1
	end
	return jink_num
end

sgs.ai_skill_cardask["double-sword-card"] = function(self,data,pattern,target)
	if self.player:isKongcheng() then return "." end
	local use = data:toCardUse()
	local jink_num = self:getExpectedJinkNum(use)
	if jink_num>1 and self:getCardsNum("Jink")==jink_num then return "." end

	if self:needKongcheng(self.player,true) and self.player:getHandcardNum()<=2 then
		if self.player:getHandcardNum()==1 then
			local card = self.player:getHandcards():first()
			return (jink_num>0 and isCard("Jink",card,self.player)) and "." or ("$"..card:getEffectiveId())
		end
		if self.player:getHandcardNum()==2 then
			local first = self.player:getHandcards():first()
			local last = self.player:getHandcards():last()
			local jink = isCard("Jink",first,self.player) and first or (isCard("Jink",last,self.player) and last)
			if jink then
				return first:getEffectiveId()==jink:getEffectiveId() and ("$"..last:getEffectiveId()) or ("$"..first:getEffectiveId())
			end
		end
	end
	if target and self:isFriend(target) then return "." end
	if self:needBear() then return "." end
	if target and self:needKongcheng(target,true) then return "." end
	local cards = self.player:getHandcards()
	for _,card in sgs.list(cards)do
		if (card:isKindOf("Slash") and self:getCardsNum("Slash")>1)
			or (card:isKindOf("Jink") and self:getCardsNum("Jink")>2)
			or card:isKindOf("Disaster")
			or (card:isKindOf("EquipCard") and not self:hasSkills(sgs.lose_equip_skill))
			or (not self.player:hasSkills("nosjizhi|jizhi") and (card:isKindOf("Collateral") or card:isKindOf("GodSalvation")
			or card:isKindOf("FireAttack") or card:isKindOf("IronChain") or card:isKindOf("AmazingGrace"))) then
			return "$"..card:getEffectiveId()
		end
	end
	return "."
end

function sgs.ai_weapon_value.qinggang_sword(self,enemy)
	if enemy and enemy:hasArmorEffect(nil) then return 3 end
end

function sgs.ai_slash_weaponfilter.qinggang_sword(self,enemy,player)
	if player:distanceTo(enemy)>math.max(sgs.weapon_range.QinggangSword,player:getAttackRange()) then return end
	if enemy:hasArmorEffect(nil)
	and getCardsNum("Jink",enemy,self.player)<1 then return true end
end

sgs.ai_skill_invoke.ice_sword = function(self,data)
	local damage = data:toDamage()
	local target = damage.to
	if self:isFriend(target) then
		if self:needToLoseHp(target,self.player,damage.card) then return false
		elseif target:isChained() and self:isGoodChainTarget(target,damage.card) then return false
		elseif self:isWeak(target) or damage.damage>1 then return true
		elseif target:getLostHp()<1 then return false end
		return true
	else
		if self:isWeak(target) then return false end
		if damage.damage>1 or self:ajustDamage(self.player,target,1,damage.card)>1 then return false end
		if target:hasSkill("lirang") and #self:getFriends(target,true)>0 then return false end
		if target:getArmor() and self:evaluateArmor(target:getArmor(),target)>3
		and not (target:hasArmorEffect("SilverLion") and target:isWounded()) then return true end
		if self.player:hasSkill("tieji") or self:canLiegong(target,self.player) then return false end
		if target:hasSkills("tuntian+zaoxian") and target:getPhase()==sgs.Player_NotActive then return false end
		if self:hasSkills(sgs.need_kongcheng,target) then return false end
		return target:getCardCount()<4 and target:getCardCount()>1
	end
end

function sgs.ai_slash_weaponfilter.guding_blade(self,to)
	return to:isKongcheng() and not to:hasArmorEffect("SilverLion")
end

function sgs.ai_weapon_value.guding_blade(self,enemy)
	if not enemy then return end
	local value = 2
	if enemy:getHandcardNum()<1 and not enemy:hasArmorEffect("SilverLion") then value = 4 end
	return value
end

function SmartAI:needToThrowAll(player)
	player = player or self.player
	if player:hasSkill("conghui") then return false end
	if not player:hasSkill("yongsi") then return false end
	if player:getPhase()==sgs.Player_NotActive or player:getPhase()==sgs.Player_Finish then return false end
	local zhanglu = self.room:findPlayerBySkillName("xiliang")
	if zhanglu and self:isFriend(zhanglu,player) then return false end
	local erzhang = self.room:findPlayerBySkillName("guzheng")
	if erzhang and not zhanglu and self:isFriend(erzhang,player) then return false end
	self.yongsi_discard = nil
	local index = 0
	local kingdom_num = 0
	local kingdoms = {}
	for _,ap in sgs.list(self.room:getAlivePlayers())do
		if not kingdoms[ap:getKingdom()] then
			kingdoms[ap:getKingdom()] = true
			kingdom_num = kingdom_num+1
		end
	end
	local cards = self.player:getCards("he")
	local Discards = {}
	for _,card in sgs.list(cards)do
		local shouldDiscard = true
		if card:isKindOf("Axe") then shouldDiscard = false end
		if isCard("Peach",card,player) or isCard("Slash",card,player) then
			local dummy_use = dummy()
			self:useBasicCard(card,dummy_use)
			if dummy_use.card then shouldDiscard = false end
		end
		if card:getTypeId()==sgs.Card_TypeTrick then
			local dummy_use = dummy()
			self:useTrickCard(card,dummy_use)
			if dummy_use.card then shouldDiscard = false end
		end
		if shouldDiscard then
			if #Discards<2 then table.insert(Discards,card:getEffectiveId()) end
			index = index+1
		end
	end
	if #Discards==2 and index<kingdom_num then
		self.yongsi_discard = Discards
		return true
	end
	return false
end

sgs.ai_skill_cardask["@axe"] = function(self,data,pattern)
	local effect = data:toCardEffect()
	if self:isFriend(effect.to) then return "." end
	if self.player:getCardCount()-3>=self.player:getHp()
	or self:needKongcheng() and self.player:getHandcardNum()>0
	or self.player:hasSkill("kuanggu") and self.player:isWounded() and self.player:distanceTo(effect.to)==1
	or self:hasSkills(sgs.lose_equip_skill,self.player) and self.player:getEquips():length()>1 and self.player:getHandcardNum()<2
	or effect.to:getHp()<2 and not hasBuquEffect(effect.to)
	or self:needToThrowAll()
	then
		if self.yongsi_discard then return "$"..table.concat(self.yongsi_discard,"+") end
		local hcards = {}
		for _,c in sgs.list(self.player:getHandcards())do
			if not (isCard("Slash",c,self.player) and self:hasCrossbowEffect()) then table.insert(hcards,c) end
		end
		local cards = {}
		self:sortByKeepValue(hcards)
		local ec = self.player:getArmor()
		if ec and self:needToThrowArmor() then
			table.insert(cards,ec:getEffectiveId())
		end
		if (self:hasSkills(sgs.need_kongcheng) or not self:hasLoseHandcardEffective())
		and #hcards>0
		then
			for _,card in sgs.list(hcards)do
				if #cards>1 then break end
				table.insert(cards,card:getEffectiveId())
			end
		end
		if #cards<2
		and self:hasSkills(sgs.lose_equip_skill,self.player)
		then
			ec = self.player:getOffensiveHorse()
			if ec then table.insert(cards,ec:getEffectiveId()) end
			ec = self.player:getArmor()
			if #cards<2 and ec and not table.contains(cards,ec:getEffectiveId())
			then table.insert(cards,ec:getEffectiveId()) end
			ec = self.player:getDefensiveHorse()
			if #cards<2 and ec then table.insert(cards,ec:getEffectiveId()) end
		end
		for _,card in sgs.list(hcards)do
			if #cards>1 then break end
			if table.contains(cards,card:getEffectiveId()) then continue end
			table.insert(cards,card:getEffectiveId())
		end
		ec = self.player:getOffensiveHorse()
		if #cards<2 and ec and not table.contains(cards,ec:getEffectiveId())
		then table.insert(cards,ec:getEffectiveId()) end
		for _,card in sgs.list(hcards)do
			if #cards>1 then break end
			if table.contains(cards,card:getEffectiveId()) then continue end
			table.insert(cards,card:getEffectiveId())
		end
		ec = self.player:getArmor()
		if #cards<2 and ec and not table.contains(cards,ec:getEffectiveId())
		then table.insert(cards,ec:getEffectiveId()) end
		ec = self.player:getDefensiveHorse()
		if #cards<2 and ec and not table.contains(cards,ec:getEffectiveId())
		then table.insert(cards,ec:getEffectiveId()) end
		if #cards>1
		then
			local num = 0
			for _,id in sgs.list(cards)do
				if self.player:hasEquip(sgs.Sanguosha:getCard(id)) then num = num+1 end
			end
			self.equipsToDec = num
			if self:ajustDamage(self.player,effect.to,1,effect.card)~=0
			then return "$"..table.concat(cards,"+") end
		end
	end
	return "."
end


function sgs.ai_slash_weaponfilter.axe(self,to,player)
	return player:distanceTo(to)<=math.max(sgs.weapon_range.Axe,player:getAttackRange()) and self:getOverflow(player)>0
end

function sgs.ai_weapon_value.axe(self,enemy,player)
	if player:hasSkills("jiushi|jiuchi|luoyi|pojun") then return 6 end
	if enemy and self:getOverflow()>0 then return 3.1 end
	if enemy and enemy:getHp()<3 then return 3-enemy:getHp() end
end

sgs.ai_skill_cardask["blade-slash"] = function(self,data,pattern,target)
	if target and self:isFriend(target)
	and not self:findLeijiTarget(target,50,self.player)
	then return "." end
	for _,slash in sgs.list(self:getCards("Slash"))do
		if self:slashIsEffective(slash,target) and (self:isWeak(target) or self:getOverflow()>0)
		then return slash:toString() end
	end
	return "."
end

function sgs.ai_weapon_value.blade(self,enemy)
	if not enemy and not self.player:hasWeapon("Axe") then return math.min(self:getCardsNum("Slash"),3) end
end

function cardsView_spear(self,player,skill_name,n,card_name)
	local c = dummyCard(card_name)
	c:setSkillName(skill_name)
	local newcards = {}
	for _,c in sgs.list(sgs.ais[player:objectName()]:addHandPile())do
		if isCard("ExNihilo",c,player) and player:getPhase()<=sgs.Player_Play
		or isCard("Peach",c,player) then continue end
		if isCard("Slash",c,player) then return end
		table.insert(newcards,c)
	end
	n = n or 2
	sgs.ais[player:objectName()]:sortByKeepValue(newcards,nil,true)
	for _,h in sgs.list(newcards)do
		if c:subcardsLength()<n
		then c:addSubcard(h) end
	end
	if c:subcardsLength()>=n
	then return c:toString() end
end

function sgs.ai_cardsview.spear(self,class_name,player)
	if class_name=="Slash"
	then
		return cardsView_spear(self,player,"spear")
	end
end

function turnUse_spear(self,skill_name)
	local newcards = {}
	local cards = self:addHandPile()
	cards = self:sortByUseValue(cards,nil,true)
	for _,card in sgs.list(cards)do
		if isCard("Peach",card,self.player)
		or isCard("ExNihilo",card,self.player) and self.player:getPhase()<=sgs.Player_Play
		then continue end
		table.insert(newcards,card)
	end
	if #newcards<2
	or #cards<self.player:getHp() and self.player:getHp()<5
	and not self:needKongcheng() then return end
	local slashs = {}
	local newcards2 = InsertList({},newcards)
	for _,c1 in sgs.list(newcards)do
		if #slashs>#newcards/2 or #slashs>3 then break end
		table.removeOne(newcards2,c1)
		for _,c2 in sgs.list(newcards2)do
			local dc = sgs.Card_Parse("slash:"..skill_name.."[no_suit:0]="..c1:getId().."+"..c2:getId())
			if dc:isAvailable(self.player) then table.insert(slashs,dc) end
		end
	end
	return #slashs>0 and slashs
end

local Spear_skill = {}
Spear_skill.name = "spear"
table.insert(sgs.ai_skills,Spear_skill)
Spear_skill.getTurnUseCard = function(self)
	return turnUse_spear(self,"spear")
end

function sgs.ai_weapon_value.spear(self,enemy,player)
	if enemy and getCardsNum("Slash",player,self.player)==0 then
		if self:getOverflow(player)>0 then return 2
		elseif player:getHandcardNum()>2 then return 1 end
	end
	return 0
end

function sgs.ai_slash_weaponfilter.fan(self,to,player)
	return player:distanceTo(to)<=math.max(sgs.weapon_range.fan,player:getAttackRange())
	and to:hasArmorEffect("Vine")
end

sgs.ai_skill_invoke.kylin_bow = function(self,data)
	local damage = data:toDamage()
	self.damage_to = damage.to
	if damage.from:hasSkill("kuangfu") and damage.to:getEquips():length()==1 then return false end
	if self:hasSkills(sgs.lose_equip_skill,damage.to) then
		return self:isFriend(damage.to)
	end
	for _,e in sgs.qlist(damage.to:getEquips())do
		if e:isKindOf("Horse") and self:doDisCard(damage.to,e:getEffectiveId()) then
			return true
		end
	end
	return self:isEnemy(damage.to)
end

sgs.ai_skill_askforag.kylin_bow = function(self,card_ids)
	for _,id in sgs.list(card_ids)do
		if self:doDisCard(self.damage_to,id) then
			return true
		end
	end
end

function sgs.ai_slash_weaponfilter.kylin_bow(self,to,player)
	return player:distanceTo(to)<=math.max(sgs.weapon_range.KylinBow,player:getAttackRange())
	and (to:getDefensiveHorse() or to:getOffensiveHorse())
end

function sgs.ai_weapon_value.kylin_bow(self,enemy)
	if enemy and (enemy:getOffensiveHorse() or enemy:getDefensiveHorse()) then return 1 end
end

sgs.ai_skill_invoke.eight_diagram = function(self,data)
	local dying = 0
	local handang = self.room:findPlayerBySkillName("nosjiefan")
	for _,aplayer in sgs.list(self.room:getAlivePlayers())do
		if aplayer:getHp()<1 and not aplayer:hasSkill("nosbuqu") then dying = 1 break end
	end
	if handang and self:isFriend(handang) and dying>0 then return false end

	local heart_jink = false
	for _,card in sgs.list(self.player:getCards("he"))do
		if card:getSuit()==sgs.Card_Heart and isCard("Jink",card,self.player) then
			heart_jink = true
			break
		end
	end

	if self:hasSkills("tiandu|leiji|nosleiji|gushou") then
		if self.player:hasFlag("dahe") and not heart_jink then return true end
		if sgs.hujiasource and not self:isFriend(sgs.hujiasource) and (sgs.hujiasource:hasFlag("dahe") or self.player:hasFlag("dahe")) then return true end
		if sgs.lianlisource and not self:isFriend(sgs.lianlisource) and (sgs.lianlisource:hasFlag("dahe") or self.player:hasFlag("dahe")) then return true end
		if self.player:hasFlag("dahe") and handang and self:isFriend(handang) and dying>0 then return true end
	end
	if self.player:getHandcardNum()==1 and self:getCardsNum("Jink")==1 and self.player:hasSkills("zhiji|beifa") and self:needKongcheng() then
		local enemy_num = self:getEnemyNumBySeat(self.room:getCurrent(),self.player,self.player)
		if self.player:getHp()>enemy_num and enemy_num<=1 then return false end
	end
	if handang and self:isFriend(handang) and dying>0 then return false end
	if self.player:hasFlag("dahe") then return false end
	if sgs.hujiasource and (not self:isFriend(sgs.hujiasource) or sgs.hujiasource:hasFlag("dahe")) then return false end
	if sgs.lianlisource and (not self:isFriend(sgs.lianlisource) or sgs.lianlisource:hasFlag("dahe")) then return false end
	if self:needToLoseHp(self.player,nil,dummyCard(),true) then return false end
	if self:getCardsNum("Jink")==0 then return true end
	local zhangjiao = self.room:findPlayerBySkillName("guidao")
	if zhangjiao and self:isEnemy(zhangjiao) then
		if getKnownCard(zhangjiao,self.player,"black",false,"he")>1 then return false end
		if self:getCardsNum("Jink")>1 and getKnownCard(zhangjiao,self.player,"black",false,"he")>0 then return false end
	end
	if self:getCardsNum("Jink")>0 and self.player:getPile("incantation"):length()>0 then return false end
	return true
end

function sgs.ai_armor_value.eight_diagram(player,self)
	if self:hasSkills("guidao",self:getEnemies(player)) then return 2 end
	if player:hasSkills("tiandu|leiji|nosleiji|noszhenlie|gushou")
	then return 6 end
	if self.role=="loyalist" and self.player:getKingdom()=="wei"
	and getLord(self.player):hasLordSkill("hujia")
	and not self.player:hasSkill("bazhen")
	then return 5 end
	return 4
end

function sgs.ai_armor_value.renwang_shield(player,self)
	if player:hasSkill("yizhong") then return 0 end
	if player:hasSkill("bazhen") then return 0 end
	if player:hasSkills("leiji|nosleiji")
	and getKnownCard(player,self.player,"Jink",true)>1 and player:hasSkill("guidao")
	and getKnownCard(player,self.player,"black",false,"he")>0
	then return 0 end
	return 4.5
end

function sgs.ai_armor_value.silver_lion(player,self)
	if self:hasWizard(self:getEnemies(player),true) then
		for _,p in sgs.list(self.room:getAlivePlayers())do
			if p:containsTrick("lightning") then return 5 end
		end
	end
	if player:isWounded() and not player:getArmor() then return 9 end
	if player:isWounded() and getKnownCard(player,self.player,"Armor",false,"h")>1
	and not player:hasArmorEffect("SilverLion") then return 8 end
	return 1
end

sgs.ai_use_priority.OffensiveHorse = 2.69

sgs.ai_use_priority.Axe = 2.688
sgs.ai_use_priority.Halberd = 2.685
sgs.ai_use_priority.KylinBow = 2.68
sgs.ai_use_priority.Blade = 2.675
sgs.ai_use_priority.GudingBlade = 2.67
sgs.ai_use_priority.DoubleSword =2.665
sgs.ai_use_priority.spear = 2.66
-- sgs.ai_use_priority.fan = 2.655
sgs.ai_use_priority.IceSword = 2.65
sgs.ai_use_priority.QinggangSword = 2.645
sgs.ai_use_priority.Crossbow = 2.63

sgs.ai_use_priority.SilverLion = 1.0
-- sgs.ai_use_priority.Vine = 0.95
sgs.ai_use_priority.EightDiagram = 0.8
sgs.ai_use_priority.RenwangShield = 0.85
sgs.ai_use_priority.DefensiveHorse = 2.75

sgs.dynamic_value.damage_card.ArcheryAttack = true
sgs.dynamic_value.damage_card.SavageAssault = true

sgs.ai_use_value.ArcheryAttack = 3.8
sgs.ai_use_priority.ArcheryAttack = 3.5
sgs.ai_keep_value.ArcheryAttack = 3.38
sgs.ai_use_value.SavageAssault = 3.9
sgs.ai_use_priority.SavageAssault = 3.5
sgs.ai_keep_value.SavageAssault = 3.36

sgs.ai_skill_cardask.aoe = function(self,data,pattern,target,name)
	if sgs.getMode:find("_mini_35") and self.player:getLostHp()==1 and name=="archery_attack"
	or sgs.ai_skill_cardask.nullfilter(self,data,pattern,target)=="."
	then return "." end
	local aoe = dummyCard(name)
	if type(data)=="userdata" then aoe = data:toCardEffect().card end
	local menghuo = self.room:findPlayerBySkillName("huoshou")
	if menghuo and aoe:isKindOf("SavageAssault") then target = menghuo end
	if self.player:hasSkill("wuyan") and not target:hasSkill("jueqing")
	or target:hasSkill("wuyan") and not target:hasSkill("jueqing")
	or self.player:getMark("@fenyong")>0 and not target:hasSkill("jueqing")
	then return "." end
	if not target:hasSkill("jueqing") and self.player:hasSkill("jianxiong")
	and not self:isWeak() and not self:needKongcheng(self.player,true)
	and not self:willSkipPlayPhase() then
		if self:getAoeValue(aoe)>-10 then return "." end
		if aoe:isVirtualCard() then
			if aoe:subcardsLength()>2 then self.jianxiong = true return "." end
			for _,id in sgs.list(aoe:getSubcards())do
				if isCard("Peach",id,self.player)
				then return "." end
			end
		end
	end
	local current = self.room:getCurrent()
	if current and current:hasSkill("juece")
	and self:isEnemy(current) and self.player:getHp()>0 then
		for _,c in sgs.list(self:getCards(name=="savage_assault" and "Slash" or "Jink"))do
			if self.player:isLastHandCard(c,true) then continue end
			return
		end
		return "."
	end
end

sgs.ai_skill_cardask["savage-assault-slash"] = function(self,data,pattern,target)
	return sgs.ai_skill_cardask.aoe(self,data,pattern,target,"savage_assault")
end

sgs.ai_skill_cardask["archery-attack-jink"] = function(self,data,pattern,target)
	return sgs.ai_skill_cardask.aoe(self,data,pattern,target,"archery_attack")
end

sgs.ai_keep_value.Nullification = 3.8
sgs.ai_use_value.Nullification = 8

function SmartAI:useCardAmazingGrace(card,use)
	if (self.role=="lord" or self.role=="loyalist") and sgs.turncount<=2
	and self.player:getSeat()<=3 and self.player:aliveCount()>5 then return end
	local value,suf,coeff = 1,0.8,0.8
	if self:needKongcheng() and self.player:getHandcardNum()==1 or self.player:hasSkills("nosjizhi|jizhi")
	then suf = 0.6 coeff = 0.6 end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		local index = 0
		if self.player:canUse(card,p) then
			if self:isFriend(p) then index = 1
			elseif self:isEnemy(p) then index = -1 end
		end
		value = value+index*suf
		if value<0 then return end
		suf = suf*coeff
	end
	use.card = card
end

sgs.ai_use_value.AmazingGrace = 3
sgs.ai_keep_value.AmazingGrace = -1
sgs.ai_use_priority.AmazingGrace = 1.2
sgs.dynamic_value.benefit.AmazingGrace = true

function SmartAI:willUseGodSalvation(card)
	if not card then self.room:writeToConsole(debug.traceback()) return false end
	local liuxie = self.room:findPlayerBySkillName("huangen")
	local wounded_friend,wounded_enemy = 0,0
	local good,bad = 0,0
	if liuxie then
		if self:isFriend(liuxie) then
			if self.player:hasSkill("noswuyan") and liuxie:getHp()>0 then return true end
			good = good+7*liuxie:getHp()
		else
			if self.player:hasSkill("noswuyan") and self:isEnemy(liuxie) and liuxie:getHp()>1 and #self.enemies>1 then return false end
			bad = bad+7*liuxie:getHp()
		end
	end
	if self.player:hasSkills("nosjizhi|jizhi") then good = good+6 end
	if self.player:hasSkill("kongcheng") and self.player:isLastHandCard(card)
	or not self:hasLoseHandcardEffective() then good = good+5 end
	for _,friend in sgs.list(self.friends)do
		good = good+10*getCardsNum("Nullification",friend,self.player)
		if self.player:canUse(card,friend) then
			if friend:isWounded() then
				good = good+10
				wounded_friend = wounded_friend+1
				if friend:isLord() then good = good+10/math.max(friend:getHp(),1) end
				if self:hasSkills(sgs.masochism_skill,friend) then good = good+5 end
				if friend:getHp()<=1 and self:isWeak(friend)
				then
					good = good+5
					if friend:isLord() then good = good+10 end
				elseif friend:isLord() then good = good+5 end
				if self:needToLoseHp(friend,nil,card,true,true) then good = good-3 end
			elseif friend:hasSkill("danlao") then good = good+5 end
		end
	end
	for _,enemy in sgs.list(self.enemies)do
		bad = bad+10*getCardsNum("Nullification",enemy,self.player)
		if self.player:canUse(card,enemy) then
			if enemy:isWounded() then
				bad = bad+10
				wounded_enemy = wounded_enemy+1
				if enemy:isLord() then
					bad = bad+10/math.max(enemy:getHp(),1)
				end
				if self:hasSkills(sgs.masochism_skill,enemy)
				then bad = bad+5 end
				if enemy:getHp()<=1 and self:isWeak(enemy) then
					bad = bad+5
					if enemy:isLord() then bad = bad+10 end
				elseif enemy:isLord() then bad = bad+5 end
				if self:needToLoseHp(enemy,nil,card,true,true) then bad = bad-3 end
			elseif enemy:hasSkill("danlao") then bad = bad+5 end
		end
	end
	return good-bad>5 and wounded_friend>0 or wounded_friend+wounded_enemy<1 and self.player:hasSkills("nosjizhi|jizhi")
end

function SmartAI:useCardGodSalvation(card,use)
	if self:willUseGodSalvation(card)
	then use.card = card end
end

sgs.ai_use_priority.GodSalvation = 4.1
sgs.ai_keep_value.GodSalvation = 3.32
sgs.dynamic_value.benefit.GodSalvation = true

sgs.ai_card_intention.GodSalvation = function(self,card,from,tos)
	local can,first
	for _,to in sgs.list(tos)do
		if to:isWounded() and not first then first = to can = true
		elseif first and to:isWounded() and not self:isFriend(first,to)
		then can = false break end
	end
	if can then
		sgs.updateIntention(from,first,-10)
	end
end

function SmartAI:JijiangSlash(player)
	if not player then self.room:writeToConsole(debug.traceback()) return 0 end
	if not player:hasLordSkill("jijiang") then return 0 end
	local slashs = 0
	for _,p in sgs.list(self.room:getOtherPlayers(player))do
		local slash_num = getCardsNum("Slash",p,self.player)
		if p:getKingdom()=="shu" and slash_num>=1 and
			(sgs.turncount<=1 and sgs.ai_role[p:objectName()]=="neutral" or self:isFriend(player,p)) then
				slashs = slashs+slash_num
		end
	end
	return slashs
end

function SmartAI:useCardDuel(duel,use)
	local n1 = self:getCardsNum("Slash")
	if self.player:hasSkill("wushuang")
	or use.isWuqian then n1 = n1*2 end
	local extraTarget = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,duel)
	if use.extra_target then extraTarget = extraTarget+use.extra_target end
	if use.xiechan then extraTarget = 100 end
	self:sort(self.enemies)
	local enemySlash = 0
	local bcv = {}
	for _,enemy in ipairs(self.enemies)do
		bcv[enemy:objectName()] = 0
		if isCurrent(use,enemy) then continue end
		local n2 = getCardsNum("Slash",enemy,self.player)
		if self.player:hasFlag("duelTo_"..enemy:objectName())
		and CanToCard(duel,self.player,enemy) and self:ajustDamage(self.player,enemy,1,duel)~=0 then
			if enemy:hasSkill("wushuang") then n2 = n2*2 end
			enemySlash = enemySlash+n2
			if n1<enemySlash and n2>0 then continue end
			if self.player:getPhase()<=sgs.Player_Play and math.random()<0.5
			then self.player:setFlags("duelTo_"..enemy:objectName()) end
			use.card = duel
			use.to:append(enemy)
			if use.to:length()>extraTarget
			then return end
		end
		n2 = n2+enemy:getHp()
		if not self:isWeak(enemy) and enemy:hasSkill("jianxiong")
		and not hasJueqingEffect(self.player,enemy) then n2 = n2+10 end
		if self:needToLoseHp(enemy,nil,duel) then n2 = n2+15 end
		if enemy:hasSkills(sgs.masochism_skill) then n2 = n2+5 end
		if not self:isWeak(enemy) and enemy:hasSkill("jiang") then n2 = n2+5 end
		if enemy:hasLordSkill("jijiang") then n2 = n2+self:JijiangSlash(enemy)*2 end
		bcv[enemy:objectName()] = n2
	end
	local function compare_func(a,b)
		return bcv[a:objectName()]<bcv[b:objectName()]
	end
	table.sort(self.enemies,compare_func)
	for _,enemy in ipairs(self.enemies)do
		if isCurrent(use,enemy) or use.to:contains(enemy) then continue end
		if CanToCard(duel,self.player,enemy) and self:objectiveLevel(enemy)>3
		and self:isGoodTarget(enemy,self.enemies,duel) and self:ajustDamage(self.player,enemy,1,duel)~=0 then
			local n2 = getCardsNum("Slash",enemy,self.player)
			if enemy:hasSkill("wushuang") then n2 = n2*2 end
			enemySlash = enemySlash+n2
			if n1>=enemySlash or self:needToLoseHp(self.player,nil,duel,true) or n2<1 
			or self.player:hasSkill("jianxiong") or self.player:getMark("shuangxiong")>0 then else continue end
			if self.player:getPhase()<=sgs.Player_Play and math.random()<0.5
			then self.player:setFlags("duelTo_"..enemy:objectName()) end
			use.card = duel
			use.to:append(enemy)
			if use.to:length()>extraTarget
			then return end
		end
	end
end

sgs.ai_card_intention.Duel = function(self,card,from,tos)
	if string.find(card:getSkillName(),"lijian") then return end
	sgs.updateIntentions(from,tos,66)
end

sgs.ai_use_value.Duel = 3.7
sgs.ai_use_priority.Duel = 2.9
sgs.ai_keep_value.Duel = 3.42

sgs.dynamic_value.damage_card.Duel = true

sgs.ai_skill_cardask["duel-slash"] = function(self,data,pattern,target)
	if self.player:hasFlag("AIGlobal_NeedToWake") and self.player:getHp()>1
	or (target:hasSkill("wuyan") or self.player:hasSkill("wuyan")) and not target:hasSkill("jueqing")
	or self.player:hasSkill("wuhun") and self:isEnemy(target) and target:isLord() and #self.friends_noself>0
	or target:hasSkill("wushuang") and not self.player:hasFlag("wushuangSlash") and self:getCardsNum("Slash")<2
	or self:isFriend(target) and target:hasSkill("rende") and self.player:hasSkill("jieming")
	or self:isEnemy(target) and not self:isWeak() and self:needToLoseHp(self.player,target)
	or self.player:getMark("@fenyong")>0 and not target:hasSkill("jueqing")
	or sgs.ai_skill_cardask.nullfilter(self,data,pattern,target)=="."
	or self:cantbeHurt(target)
	then return "." end

	if self:isFriend(target) then
		if target:objectName()==self.player:objectName()
		or self:needToLoseHp(self.player,target)
		then return "." end
		if self:needToLoseHp(target,self.player)
		or target:isLord() and not sgs.isLordInDanger() and not self:isGoodHp()
		or self.player:isLord() and sgs.isLordInDanger()
		then return self:getCardId("Slash") end
	elseif self:getCardsNum("Slash")>=getCardsNum("Slash",target,self.player)
	or self:isWeak() then return self:getCardId("Slash") end
	return "."
end

function SmartAI:useCardExNihilo(card,use)
	local xiahou = self:hasSkills("yanyu",self.enemies)
	if xiahou and xiahou:getMark("YanyuDiscard2")>0 then return end
	use.card = card
end

sgs.ai_card_intention.ExNihilo = -80

sgs.ai_keep_value.ExNihilo = 3.9
sgs.ai_use_value.ExNihilo = 9
sgs.ai_use_priority.ExNihilo = 9.3

sgs.dynamic_value.benefit.ExNihilo = true

function SmartAI:getDangerousCard(who)
	local weapon = who:getWeapon()
	if weapon then
		if weapon:isKindOf("Crossbow")
		or weapon:isKindOf("GudingBlade") then
			for _,friend in sgs.list(self.friends)do
				if weapon:isKindOf("Crossbow") and who:canSlash(friend)
				and getCardsNum("Slash",who,self.player)>0
				then return weapon:getEffectiveId() end
				if weapon:isKindOf("GudingBlade") and who:canSlash(friend)
				and friend:isKongcheng() and getCardsNum("Slash",who)>0
				then return weapon:getEffectiveId() end
			end
		elseif weapon:isKindOf("Spear") and self:hasCrossbowEffect(who) and who:getHandcardNum()>=1
		then return weapon:getEffectiveId()
		elseif weapon:isKindOf("Axe")
		and (who:hasSkills("luoyi|pojun|jiushi|jiuchi|jie|wenjiu|shenli|jieyuan") or self:getOverflow(who)>0 or who:getCardCount()>=4)
		then return weapon:getEffectiveId() end
	end
	local armor = who:getArmor()
	if armor then
		if armor:isKindOf("EightDiagram") then
			local lord = self.room:getLord()
			if lord and lord:hasLordSkill("hujia")
			and who:getKingdom()=="wei" and self:isFriend(lord,who)
			then return armor:getEffectiveId() end
			for _,sk in ipairs(sgs.getPlayerSkillList(who))do
				local ts = sgs.Sanguosha:getTriggerSkill(sk:objectName())
				if ts and (ts:hasEvent(sgs.AskForRetrial) or ts:hasEvent(sgs.FinishJudge))
				then return armor:getEffectiveId() end
			end
		end
	end
	if weapon then
		if who:hasSkill("liegong|anjian")
		then return weapon:getEffectiveId() end
		if weapon:isKindOf("SPMoonSpear")
		and who:hasSkills("guidao|longdan|guicai|jilve|huanshi|qingguo|kanpo")
		then return weapon:getEffectiveId() end
		for _,friend in sgs.list(self.friends)do
			if who:distanceTo(friend)<=who:getAttackRange()
			and who:distanceTo(friend)>who:getAttackRange(false)
			and self:isWeak(friend) then return weapon:getEffectiveId() end
		end
		for _,friend in sgs.list(self.friends)do
			if who:distanceTo(friend)<=who:getAttackRange()
			and who:distanceTo(friend)>who:getAttackRange(false)
			then return weapon:getEffectiveId() end
		end
	end
end

function SmartAI:getValuableCard(who)
	local weapon = who:getWeapon()
	local offhorse = who:getOffensiveHorse()
	self:sort(self.friends,"hp")
	local friend = #self.friends>0 and self.friends[1]
	if friend and self:isWeak(friend)
	and who:distanceTo(friend)<=who:getAttackRange(false) then
		if weapon and who:distanceTo(friend)>1
		then return weapon:getEffectiveId() end
		if offhorse and who:distanceTo(friend)>1
		then return offhorse:getEffectiveId() end
	end
	local defhorse = who:getDefensiveHorse()
	if defhorse and self.player:distanceTo(who)==2
	then return defhorse:getEffectiveId() end
	if weapon and (weapon:isKindOf("MoonSpear") and who:hasSkill("keji") and who:getHandcardNum()>5
	or who:hasSkills("qiangxi|zhulou|taichen"))
	then return weapon:getEffectiveId() end
	for _,equip in sgs.list(who:getEquips())do
		if who:getHp()<=2 and who:hasSkill("baobian") then return equip:getEffectiveId() end
		if equip:getSuit()~=sgs.Card_Diamond and who:hasSkill("longhun") then return equip:getEffectiveId() end
		if equip:isRed() and who:hasSkills("wusheng|jijiu|xueji|nosfuhun") then return equip:getEffectiveId() end
		if equip:isBlack() and who:hasSkills("qixi|duanliang|yinling|guidao") then return equip:getEffectiveId() end
		if equip:getSuit()==sgs.Card_Diamond and who:hasSkills("guose|yanxiao") then return equip:getEffectiveId() end
		if who:hasSkills(sgs.need_equip_skill) and not who:hasSkills(sgs.lose_equip_skill) then return equip:getEffectiveId() end
	end
	local armor = who:getArmor()
	if armor and self:evaluateArmor(armor,who)>3 and not self:needToThrowArmor(who)
	then return armor:getEffectiveId() end
	if offhorse and who:hasSkills("nosqianxi|kuanggu|duanbing|qianxi")
	then return offhorse:getEffectiveId() end
	if defhorse and (getCardsNum("Jink",who,self.player)<1
	and not(self.player:hasWeapon("kylin_bow") and self.player:canSlash(who) and self:slashIsEffective(dummyCard(),who,self.player)))
	then return defhorse:getEffectiveId() end
	if armor and not self:needToThrowArmor(who)
	then return armor:getEffectiveId() end
	if offhorse and who:getHandcardNum()>1 then
		for _,friend in sgs.list(self.friends)do
			if who:distanceTo(friend)==who:getAttackRange() and who:getAttackRange()>1
			then return offhorse:getEffectiveId() end
		end
	end
	if weapon and who:getHandcardNum()>1 then
		for _,friend in sgs.list(self.friends)do
			if who:distanceTo(friend)<=who:getAttackRange() and who:distanceTo(friend)>1
			then return weapon:getEffectiveId() end
		end
	end
	local treasure = who:getTreasure()
	if treasure and treasure:isKindOf("WoodenOx") and who:getPile("wooden_ox"):length()>1
	then return treasure:getEffectiveId() end
end

function SmartAI:useCardSnatch(card,use)
	local extraTarget = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,card)
	if use.extra_target then extraTarget = extraTarget+use.extra_target end
	local function addTarget(to,fid)
		if isCurrent(use,to) or fid==nil or use.to:contains(to) then return end
		if self:doDisCard(to,fid,true) then
			use.card = card
			use.to:append(to)
			return use.to:length()>extraTarget
		end
	end
	local players = self:exclude(self.room:getOtherPlayers(self.player),card)
	for _,p in ipairs(players)do
		for _,trick in sgs.qlist(p:getJudgingArea())do
			if trick:isDamageCard() and not p:containsTrick("YanxiaoCard")
			and (#self.friends>#self.enemies or self:getFinalRetrial(p)==2)
			and addTarget(p,trick:getEffectiveId()) then return end
		end
	end
	local dummy = self:getCard("Slash")
	if dummy and dummy:isAvailable(self.player) then
		dummy = self:aiUseCard(dummy)
		if dummy.card then
			for _,to in sgs.qlist(dummy.to)do
				if table.contains(players,to) and to:getHp()<=2
				and to:getHandcardNum()<=to:getHp() and not self:needKongcheng(to)
				and addTarget(to,"h") then return end
			end
		end
	end
	self:sort(players,"defense")
	for _,enemy in ipairs(players)do
		if enemy:hasEquip() and addTarget(enemy,self:getDangerousCard(enemy))
		then return end
	end
	self:sort(self.friends_noself,"defense")
	for _,friend in ipairs(self.friends_noself)do
		if table.contains(players,friend) then
			for _,c in sgs.qlist(friend:getCards("ej"))do
				if c:isKindOf("Indulgence") then
					if (friend:getHp()<=friend:getHandcardNum() or friend:isLord())
					and addTarget(friend,c:getEffectiveId()) then return end
				elseif addTarget(friend,c:getEffectiveId()) then return end
			end
		end
	end
	for _,enemy in ipairs(players)do
		for _,j in sgs.qlist(enemy:getJudgingArea())do
			if j:isKindOf("YanxiaoCard") and enemy:getJudgingArea():length()>1
			and addTarget(enemy,j:getEffectiveId()) then return end
		end
	end
	for _,enemy in ipairs(players)do
		local n = 0
		for _,h in sgs.qlist(enemy:getHandcards())do
			if (h:hasFlag("visible") or h:hasFlag("visible_"..self.player:objectName().."_"..enemy:objectName()))
			and isCard("Peach,Analeptic",h,enemy) then
				n = n+1
				if n>=enemy:getHandcardNum()/2 and addTarget(enemy,h:getEffectiveId())
				then return end
			end
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if table.contains(players,friend) then
			for _,c in sgs.qlist(friend:getEquips())do
				if addTarget(friend,c:getEffectiveId()) then return end
			end
		end
	end
	for _,enemy in ipairs(players)do
		for _,e in sgs.qlist(enemy:getEquips())do
			if addTarget(enemy,e:getEffectiveId()) then return end
		end
	end
	for _,enemy in ipairs(players)do
		if enemy:getHandcardNum()>0 and enemy:hasSkills(sgs.cardneed_skill)
		and addTarget(enemy,"h") then return end
	end
	for _,enemy in ipairs(players)do
		if enemy:getHandcardNum()>0
		and addTarget(enemy,"h") then return end
	end
end

sgs.ai_use_value.Snatch = 9
sgs.ai_use_priority.Snatch = 9.3
sgs.ai_keep_value.Snatch = 3.46

sgs.dynamic_value.control_card.Snatch = true

function SmartAI:useCardDismantlement(card,use)
	local extraTarget = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,card)
	if use.extra_target then extraTarget = extraTarget+use.extra_target end
	local function addTarget(to,cid)
		if isCurrent(use,to) or not cid then return end
		if type(cid)~="number" then cid = cid:getEffectiveId() end
		if not use.to:contains(to) and self:doDisCard(to,cid) then
			use.card = card
			use.to:append(to)
			return use.to:length()>extraTarget
		end
	end
	local players = self:exclude(self.room:getOtherPlayers(self.player),card)
	if sgs.getMode~="02_1v1" or sgs.GetConfig("1v1/Rule","Classical")=="Classical" then
		for _,p in ipairs(players)do
			for _,trick in sgs.qlist(p:getJudgingArea())do
				if trick:isDamageCard() and not p:containsTrick("YanxiaoCard")
				and (self:getFinalRetrial(p)==2 or #self.friends>#self.enemies)
				and addTarget(p,trick) then return end
			end
		end
	end
	local dummy = self:getCard("Slash")
	if dummy and dummy:isAvailable(self.player) then
		dummy = self:aiUseCard(dummy)
		if dummy.card then
			for _,to in sgs.qlist(dummy.to)do
				if table.contains(players,to) and to:getHp()<=2
				and to:getHandcardNum()<=to:getHp() and not self:needKongcheng(to)
				and addTarget(to,to:getRandomHandCard()) then return end
			end
		end
	end
	self:sort(players,"defense")
	for _,enemy in ipairs(players)do
		if enemy:hasEquip() and addTarget(enemy,self:getDangerousCard(enemy))
		then return end
	end
	self:sort(self.friends_noself,"defense")
	if sgs.getMode~="02_1v1" or sgs.GetConfig("1v1/Rule","Classical")=="Classical" then
		for _,friend in ipairs(self.friends_noself)do
			if table.contains(players,friend) then
				for _,c in sgs.qlist(friend:getCards("ej"))do
					if c:isKindOf("Indulgence") then
						if (friend:getHp()<=friend:getHandcardNum() or friend:isLord())
						and addTarget(friend,c) then return end
					elseif addTarget(friend,c) then return end
				end
			end
		end
		for _,enemy in ipairs(players)do
			for _,j in sgs.qlist(enemy:getJudgingArea())do
				if j:isKindOf("YanxiaoCard") and enemy:getJudgingArea():length()>1
				and addTarget(enemy,j) then return end
			end
		end
	end
	for _,enemy in ipairs(players)do
		local n = 0
		for _,h in sgs.qlist(enemy:getHandcards())do
			if (h:hasFlag("visible") or h:hasFlag("visible_"..self.player:objectName().."_"..enemy:objectName()))
			and isCard("Peach,Analeptic",h,enemy) then
				n = n+1
				if n>=enemy:getHandcardNum()/2 and addTarget(enemy,h)
				then return end
			end
		end
	end
	for _,friend in ipairs(self.friends_noself)do
		if table.contains(players,friend) then
			for _,c in sgs.qlist(friend:getEquips())do
				if addTarget(friend,c) then return end
			end
		end
	end
	for _,enemy in ipairs(players)do
		for _,e in sgs.qlist(enemy:getEquips())do
			if addTarget(enemy,e) then return end
		end
	end
	for _,enemy in ipairs(players)do
		if enemy:getHandcardNum()>0 and enemy:hasSkills(sgs.cardneed_skill)
		and addTarget(enemy,enemy:getRandomHandCard())
		then return end
	end
	if self:getOverflow()>0 then
		for _,enemy in ipairs(players)do
			if enemy:getHandcardNum()>0 and self:isEnemy(enemy)
			and addTarget(enemy,enemy:getRandomHandCard()) then return end
		end
		for _,enemy in ipairs(players)do
			if enemy:getHandcardNum()>0 and not self:isFriend(enemy)
			and addTarget(enemy,enemy:getRandomHandCard()) then return end
		end
		for _,enemy in ipairs(players)do
			if enemy:getHandcardNum()>0 and addTarget(enemy,enemy:getRandomHandCard()) then return end
		end
	end
end

sgs.ai_use_value.Dismantlement = 5.6
sgs.ai_use_priority.Dismantlement = 9.4
sgs.ai_keep_value.Dismantlement = 3.44

sgs.dynamic_value.control_card.Dismantlement = true

sgs.ai_choicemade_filter.cardChosen.snatch = function(self,player,promptlist)
	local to = self.room:findPlayerByObjectName(promptlist[4])
	if to then
		local intention = 70
		if to:hasSkills("tuntian+zaoxian") and to:getPile("field")==2 and to:getMark("zaoxian")<1
		then intention = 0 end
		local card = sgs.Sanguosha:getCard(promptlist[3])
		local place = self.room:getCardPlace(promptlist[3])
		if place==sgs.Player_PlaceDelayedTrick then
			if not card:isKindOf("Disaster") or card:isKindOf("YanxiaoCard")
			then intention = -intention else intention = 0 end
		elseif place==sgs.Player_PlaceEquip then
			if card:isKindOf("Armor") and self:evaluateArmor(card,to)<=-2
			then intention = 0
			elseif card:isKindOf("SilverLion") then
				if to:getLostHp()>1 then
					if to:hasSkills(sgs.use_lion_skill)
					then intention = self:willSkipPlayPhase(to) and -intention or 0
					else intention = self:isWeak(to) and -intention or 0 end
				else intention = 0 end
			elseif to:hasSkills(sgs.lose_equip_skill) then
				if self:isWeak(to) and (card:isKindOf("DefensiveHorse") or card:isKindOf("Armor"))
				then intention = math.abs(intention) else intention = 0 end
			end
			if promptlist[2]=="snatch" and self:isFriend(to,player)
			and (card:isKindOf("OffensiveHorse") or card:isKindOf("Weapon")) then
				local canAttack
				for _,p in sgs.list(self.room:getOtherPlayers(player))do
					if player:inMyAttackRange(p) and self:isEnemy(p,player)
					then canAttack = true break end
				end
				if not canAttack then intention = 0 end
			end
		elseif place==sgs.Player_PlaceHand then
			if self:needKongcheng(to,true) and to:getHandcardNum()==1
			then intention = 0 end
		end
		sgs.updateIntention(player,to,intention)
	end
end

sgs.ai_choicemade_filter.cardChosen.dismantlement = sgs.ai_choicemade_filter.cardChosen.snatch

function SmartAI:useCardCollateral(card,use)
	local fromList = sgs.QList2Table(self.room:getOtherPlayers(self.player))
	local cmps = {}
	for _,p in ipairs(fromList)do
		cmps[p:objectName()] = self:objectiveLevel(p)
	end
	local cmp = function(a,b)
		local al = cmps[a:objectName()]
		local bl = cmps[b:objectName()]
		if al~=bl then return al>bl end
		al = getCardsNum("Slash",a,self.player)
		bl = getCardsNum("Slash",b,self.player)
		if al~=bl then return al<bl end
		return a:getHandcardNum()<b:getHandcardNum()
	end
	table.sort(fromList,cmp)
	function useToCard(to)
		return to:getWeapon()
		and not(use.to:contains(to) or isCurrent(use,to)
		or self.player:isProhibited(to,card))
	end
	local toList = self:sort(self.room:getAlivePlayers(),"defense")
	local extraTarget = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,card)
	if use.extra_target then extraTarget = extraTarget+use.extra_target end
	extraTarget = extraTarget*2
	for _,enemy in ipairs(self.enemies)do
		if self.player:distanceTo(enemy)<2
		and self:isGoodTarget(enemy,self.enemies,card)
		and not self:hasCrossbowEffect() and self:getCardsNum("Slash")>2 then
			for _,from in ipairs(sgs.reverse(fromList))do
				if from:hasWeapon("Crossbow")
				and useToCard(from) then
					for _,enemy in ipairs(toList)do
						if from:canSlash(enemy)
						and from:inMyAttackRange(enemy) then
							self.player:setFlags("needCrossbow")
							use.card = card
							use.to:append(from)
							use.to:append(enemy)
							if use.to:length()>extraTarget
							then return end
							break
						end
					end
				end
			end
			break
		end
	end
	for _,enemy in ipairs(fromList)do
		if useToCard(enemy) and self:objectiveLevel(enemy)>=0
		and not(self:loseEquipEffect(enemy) or enemy:hasSkill("tuntian+zaoxian")) then
			local final_enemy
			for _,enemy2 in ipairs(toList)do
				if final_enemy then break end
				if enemy:canSlash(enemy2)
				and enemy:inMyAttackRange(enemy2)
				and self:objectiveLevel(enemy2)>2
				then final_enemy = enemy2 end
			end
			for _,enemy2 in ipairs(toList)do
				if final_enemy then break end
				local ol = self:objectiveLevel(enemy2)
				if enemy:inMyAttackRange(enemy2)
				and enemy:canSlash(enemy2) and ol<=3 and ol>=0
				then final_enemy = enemy2 end
			end
			for _,friend in ipairs(sgs.reverse(toList))do
				if final_enemy then break end
				if enemy:canSlash(friend)
				and enemy:inMyAttackRange(friend)
				and self:objectiveLevel(friend)<0
				and self:needToLoseHp(friend,enemy,dummyCard(),true)
				then final_enemy = friend end
			end
			for _,friend in ipairs(sgs.reverse(toList))do
				if final_enemy then break end
				if enemy:canSlash(friend)
				and enemy:inMyAttackRange(friend)
				and self:objectiveLevel(friend)<0
				and (getCardsNum("Jink",friend,self.player)>1 or getCardsNum("Slash",enemy,self.player)<1)
				then final_enemy = friend end
			end
			if final_enemy then
				use.card = card
				use.to:append(enemy)
				use.to:append(final_enemy)
				if use.to:length()>extraTarget
				then return end
			end
		end
	end
	for _,friend in ipairs(fromList)do
		if useToCard(friend) and self:objectiveLevel(friend)<0
		and getCardsNum("Slash",friend,self.player)>0 then
			for _,enemy in ipairs(toList)do
				if friend:canSlash(enemy)
				and self:objectiveLevel(enemy)>2
				and friend:inMyAttackRange(enemy)
				and self:isGoodTarget(enemy,self.enemies,card) then
					use.card = card
					use.to:append(friend)
					use.to:append(enemy)
					if use.to:length()>extraTarget
					then return end
					break
				end
			end
		end
	end
	self:sortEnemies(toList)
	for _,friend in ipairs(fromList)do
		if useToCard(friend) and self:objectiveLevel(friend)<0
		and not(friend:hasWeapon("Crossbow") and getCardsNum("Slash",friend,self.player)>1)
		and self:loseEquipEffect(friend) then
			for _,enemy in ipairs(toList)do
				if friend:canSlash(enemy) and self:objectiveLevel(enemy)>=0
				and friend:inMyAttackRange(enemy) then
					use.card = card
					use.to:append(friend)
					use.to:append(enemy)
					if use.to:length()>extraTarget
					then return end
					break
				end
			end
			for _,enemy in ipairs(sgs.reverse(toList))do
				if friend:canSlash(enemy) and self:objectiveLevel(enemy)<0
				and friend:inMyAttackRange(enemy) then
					use.card = card
					use.to:append(friend)
					use.to:append(enemy)
					if use.to:length()>extraTarget
					then return end
					break
				end
			end
		end
	end
end

sgs.ai_use_value.Collateral = 5.8
sgs.ai_use_priority.Collateral = 2.75
sgs.ai_keep_value.Collateral = 3.40

sgs.ai_card_intention.Collateral = function(self,card,from,tos)
	sgs.ai_collateral = true
end

sgs.dynamic_value.control_card.Collateral = true

sgs.ai_skill_cardask["collateral-slash"] = function(self,data,pattern,target2,target,prompt)
	if self:isFriend(target)
	and (target:hasFlag("needCrossbow") or getCardsNum("Slash",target,self.player)>=2 and self.player:getWeapon():isKindOf("Crossbow")) then
		target:setFlags("-needCrossbow")
		return "."
	end
	if self:isFriend(target2)
	and self:needLeiji(target2,self.player) then
		for _,slash in sgs.list(self:getCards("Slash"))do
			if self:slashIsEffective(slash,target2)
			then return slash:toString() end
		end
	end
	if target2 and self:needToLoseHp(target2,self.player,dummyCard()) then
		for _,slash in sgs.list(self:getCards("Slash"))do
			if self:slashIsEffective(slash,target2) and self:isFriend(target2) then return slash:toString() end
			if not self:slashIsEffective(slash,target2,self.player,true) and self:isEnemy(target2)
			then return slash:toString() end
		end
		for _,slash in sgs.list(self:getCards("Slash"))do
			if not self:needToLoseHp(target2,self.player,slash) and self:isEnemy(target2)
			then return slash:toString() end
		end
	end
	if target2 and not self:hasSkills(sgs.lose_equip_skill)
	and self:isEnemy(target2) then
		for _,slash in sgs.list(self:getCards("Slash"))do
			if self:slashIsEffective(slash,target2)
			then return slash:toString() end
		end
	end
	if target2 and not self:hasSkills(sgs.lose_equip_skill)
	and self:isFriend(target2) then
		for _,slash in sgs.list(self:getCards("Slash"))do
			if not self:slashIsEffective(slash,target2)
			then return slash:toString() end
		end
		for _,slash in sgs.list(self:getCards("Slash"))do
			if (target2:getHp()>3 or not self:canHit(target2,self.player,self:ajustDamage(self.player,target2,1,slash)>1))
			and target2:getRole()~="lord" and self.player:getHandcardNum()>1
			then return slash:toString() end
			if self:needToLoseHp(target2,self.player,slash) then return slash:toString() end
		end
	end
	self:speak("collateralNoslash",self.player:isFemale())
	return "."
end

sgs.ai_skill_playerchosen.collateral = function(self,players)
	local destlist = self:sort(players)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
	return destlist[#destlist]
end


local function hp_subtract_handcard(a,b)
	return a:getHp()-a:getHandcardNum()<b:getHp()-b:getHandcardNum()
end

function SmartAI:enemiesContainsTrick(EnemyCount)
	local trick_all,possible_indul_enemy,possible_ss_enemy = 0,0,0
	local indul_num = self:getCardsNum("Indulgence")
	local ss_num = self:getCardsNum("SupplyShortage")
	local enemy_num,temp_enemy = 0,nil
	local zhanghe = self.room:findPlayerBySkillName("qiaobian")
	if zhanghe and (not self:isEnemy(zhanghe) or zhanghe:isKongcheng() or not zhanghe:faceUp()) then zhanghe = nil end
	if self.player:hasSkill("guose") then
		for _,acard in sgs.list(self.player:getCards("he"))do
			if acard:getSuit()==sgs.Card_Diamond then indul_num = indul_num+1 end
		end
	end
	if self.player:hasSkill("duanliang") then
		for _,acard in sgs.list(self.player:getCards("he"))do
			if acard:isBlack() then ss_num = ss_num+1 end
		end
	end
	for _,enemy in sgs.list(self.enemies)do
		if not enemy:containsTrick("YanxiaoCard") then
			if enemy:containsTrick("indulgence") then
				if not enemy:hasSkills("keji|conghui") and (not zhanghe or self:playerGetRound(enemy)>=self:playerGetRound(zhanghe)) then
					trick_all = trick_all+1
					if not temp_enemy or temp_enemy:objectName()~=enemy:objectName() then
						enemy_num = enemy_num+1
						temp_enemy = enemy
					end
				end
			else
				possible_indul_enemy = possible_indul_enemy+1
			end
			if self.player:distanceTo(enemy)==1 or self.player:hasSkill("duanliang") and self.player:distanceTo(enemy)<=2 then
				if enemy:containsTrick("supply_shortage") then
					if not self:hasSkills("shensu|jisu",enemy) and (not zhanghe or self:playerGetRound(enemy)>=self:playerGetRound(zhanghe)) then
						trick_all = trick_all+1
						if not temp_enemy or temp_enemy:objectName()~=enemy:objectName() then
							enemy_num = enemy_num+1
							temp_enemy = enemy
						end
					end
				else
					possible_ss_enemy  = possible_ss_enemy+1
				end
			end
		end
	end
	indul_num = math.min(possible_indul_enemy,indul_num)
	ss_num = math.min(possible_ss_enemy,ss_num)
	if not EnemyCount then
		return trick_all+indul_num+ss_num
	else
		return enemy_num+indul_num+ss_num
	end
end

function SmartAI:playerGetRound(player,source)
	if not player then return self.room:writeToConsole(debug.traceback()) end
	source = source or self.room:getCurrent()
	if player:objectName()==source:objectName() then return 0 end
	return (player:getSeat()-source:getSeat())%player:aliveCount()
end

function SmartAI:useCardIndulgence(card,use)
	if #self.enemies<1 and self.player:getHandcardNum()-#self.toUse-self.player:getMaxCards()<1 then return end
	local zhanghe = self.room:findPlayerBySkillName("qiaobian")
	local zhanghe_seat = zhanghe and zhanghe:faceUp() and not zhanghe:isKongcheng() and not self:isFriend(zhanghe) and zhanghe:getSeat()
	local sb_daqiao = self.room:findPlayerBySkillName("yanxiao")
	local yanxiao = sb_daqiao and not self:isFriend(sb_daqiao) and sb_daqiao:faceUp()
		and (getKnownCard(sb_daqiao,self.player,"diamond",nil,"he")>0
		or sb_daqiao:getHandcardNum()+self:ImitateResult_DrawNCards(sb_daqiao,sb_daqiao:getVisibleSkillList(true))>3
		or sb_daqiao:containsTrick("YanxiaoCard"))
	local getvalue = function(enemy)
		if type(enemy)~="userdata"
		or enemy:containsTrick("YanxiaoCard")
		or enemy:hasSkill("qiaobian") and enemy:getJudgingArea():isEmpty() and enemy:getHandcardNum()>0
		or yanxiao and (self:playerGetRound(sb_daqiao)<=self:playerGetRound(enemy) and self:enemiesContainsTrick(true)<=1 or not enemy:faceUp())
		or zhanghe_seat and (self:playerGetRound(zhanghe)<=self:playerGetRound(enemy) and self:enemiesContainsTrick()<=1 or not enemy:faceUp())
		then return -100 end
		local value = enemy:getHandcardNum()-enemy:getHp()
		for _,sk in sgs.list(sgs.getPlayerSkillList(enemy))do
			local s = sgs.Sanguosha:getViewAsSkill(sk:objectName())
			if s and s:isEnabledAtPlay(enemy) then value = value+6 end
			s = sgs.Sanguosha:getTriggerSkill(sk:objectName())
			if s and s:hasEvent(sgs.EventPhaseStart) then value = value-2 end
			if s and s:hasEvent(sgs.EventPhaseChanging) then value = value-2 end
			if s and s:hasEvent(sgs.FinishJudge) then value = value-2 end
		end
		if self:isWeak(enemy) then value = value+3 end
		if enemy:isLord() then value = value+3 end
		if self:objectiveLevel(enemy)<3 then value = value-6 end
		if not enemy:faceUp() then value = value-10 end
		if enemy:hasSkills("keji|shensu|conghui") then value = value-enemy:getHandcardNum() end
		if self:needBear(enemy) then value = value-20 end
		if #self.enemies>1 and getKnownCard(enemy,self.player,"Dismantlement",true)>0 then value = value+3 end
		return value+(enemy:aliveCount()-self:playerGetRound(enemy))/2
	end
	local function cmp(a,b)
		return getvalue(a)>getvalue(b)
	end
	table.sort(self.enemies,cmp)
	for _,ep in sgs.list(self.enemies)do
		if CanToCard(card,self.player,ep)
		and getvalue(ep)>-100 and use.to then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	local aps = self.room:getOtherPlayers(self.player)
	aps = sgs.QList2Table(aps)
	table.sort(aps,cmp)
	for _,ep in sgs.list(aps)do
		if CanToCard(card,self.player,ep)
		and getvalue(ep)>-100 and use.to
		and not self:isFriend(ep) then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.Indulgence = 8
sgs.ai_use_priority.Indulgence = 0.5
sgs.ai_card_intention.Indulgence = 120
sgs.ai_keep_value.Indulgence = 3.5

sgs.dynamic_value.control_usecard.Indulgence = true

function SmartAI:willUseLightning(card)
	if self.player:isProhibited(self.player,card) then return end
	local function hasDangerousFriend()
		local hashy = false
		for _,p in sgs.list(self.enemies)do
			if p:hasSkill("hongyan")
			then hashy = true break end
		end
		for _,p in sgs.list(self.enemies)do
			if p:hasSkill("gongxin") and hashy
			or p:hasSkill("guanxing")
			or p:hasSkill("xinzhan") then
				if self:isFriend(p:getNextAlive())
				then return true end
			end
		end
		return false
	end
	local n = self:getFinalRetrial(self.player)
	if n==2 then return
	elseif n==1 then return true
	elseif not hasDangerousFriend() then
		local friends = 0
		local enemies = 0
		for _,player in sgs.list(self.room:getAllPlayers())do
			if self:objectiveLevel(player)>=4
			and not player:hasSkill("hongyan") and not player:hasSkill("wuyan")
			and not (player:hasSkill("weimu") and card:isBlack())
			then enemies = enemies+1
			elseif self:isFriend(player) and not player:hasSkill("hongyan")
			and not player:hasSkill("wuyan")
			and not (player:hasSkill("weimu") and card:isBlack())
			then friends = friends+1 end
		end
		return friends<1 or enemies/friends>1.5
	end
end

function SmartAI:useCardLightning(card,use)
	if self:willUseLightning(card)
	then use.card = card end
end

sgs.ai_use_priority.Lightning = 0
sgs.dynamic_value.lucky_chance.Lightning = true

sgs.ai_keep_value.Lightning = -1

sgs.ai_skill_askforag.amazing_grace = function(self,card_ids)
	local NextPlayerCanUse,NextPlayerisEnemy
	local NextPlayer = self.player:getNextAlive()
	if sgs.turncount>1 and not self:willSkipPlayPhase(NextPlayer) then
		if self:isFriend(NextPlayer) and sgs.ai_role[NextPlayer:objectName()]~="neutral"
		then NextPlayerCanUse = true else NextPlayerisEnemy = true end
	end
	for _,enemy in ipairs(self.enemies)do
		if enemy:hasSkill("lihun")
		and not NextPlayer:faceUp()
		and NextPlayer:getHandcardNum()>4
		and NextPlayer:isMale() and enemy:faceUp()
		then NextPlayerCanUse = false break end
	end
	local cards = {}
	local trickNum = 0
	for _,id in ipairs(card_ids)do
		local c = sgs.Sanguosha:getCard(id)
		if c:isKindOf("TrickCard") then trickNum = trickNum+1 end
		table.insert(cards,c)
	end
	local nextfriend_num = 0
	local aplayer = self.player:getNextAlive()
	for i=1,self.player:aliveCount()do
		if self:isFriend(aplayer) then
			aplayer = aplayer:getNextAlive()
			nextfriend_num = nextfriend_num+1
		else break end
	end
	
	---------------
	
	self:sortByCardNeed(cards,true)
	
	if self:hasSkills("buyi",self.friends) then
		local maxvaluecard,minvaluecard
		local maxvalue,minvalue = -100,100
		for _,c in ipairs(cards)do
			if c:getTypeId()>1 then
				local value = self:getUseValue(c)
				if value>maxvalue then
					maxvalue = value
					maxvaluecard = c
				end
				if value<minvalue then
					minvalue = value
					minvaluecard = c
				end
			end
		end
		if minvaluecard and NextPlayerCanUse then
			return minvaluecard:getEffectiveId()
		end
		if maxvaluecard then
			return maxvaluecard:getEffectiveId()
		end
	end
	local CP = self.room:getCurrent()
	local SelfisCurrent = CP==self.player
	local friendneedpeach,peach
	if NextPlayerCanUse then
		if not self.player:isWounded() and NextPlayer:isWounded()
		or self.player:getLostHp()<self:getCardsNum("Peach")
		or not SelfisCurrent and self:willSkipPlayPhase() and self.player:getHandcardNum()+2>self.player:getMaxCards()
		then friendneedpeach = true end
	end
	local cid = {}
	local hand = self.player:getHandcards()
	hand = self:sortByKeepValue(hand,true)
	local peachnum,jinknum = 0,0
	for _,c in ipairs(cards)do
		for i,h in ipairs(hand)do
			if i<#hand/2 and self:cardNeed(c)>self:cardNeed(h)
			and self:getCardsNum(c:getClassName())<2
			then return c:getEffectiveId() end
		end
		cid[c:objectName()] = c:getEffectiveId()
		if isCard("Peach",c,self.player) then
			peachnum = peachnum+1
			cid.peach = c:getEffectiveId()
		elseif isCard("ExNihilo,Dongzhuxianji",c,self.player) then
			if not NextPlayerCanUse
			or (not self:willSkipPlayPhase()
				and (self.player:hasSkills("nosjizhi|jizhi|zhiheng|nosrende|rende")
				or not NextPlayer:hasSkills("nosjizhi|jizhi|zhiheng|nosrende|rende")))
			then cid.exnihilo = c:getEffectiveId() end
		elseif isCard("Jink",c,self.player) then
			jinknum = jinknum+1
			cid.jink = c:getEffectiveId()
		elseif isCard("Snatch",c,self.player) then cid.snatch = c
		elseif isCard("Analeptic",c,self.player) then cid.analeptic = c:getEffectiveId()
		elseif isCard("Dismantlement,Zhujinqiyuan",c,self.player) then cid.dismantlement = c
		elseif isCard("Nullification",c,self.player) then cid.nullification = c:getEffectiveId()
		elseif isCard("Indulgence",c,self.player) then cid.indulgence = c:getEffectiveId() end
		if c:isKindOf("Weapon") then cid.weapon = c:getEffectiveId()
		elseif c:isKindOf("Horse") and not self:getSameEquip(c)
		then cid[c:getClassName()] = c:getEffectiveId() end
		for _,skill in sgs.qlist(self.player:getVisibleSkillList(true))do
			local callback = sgs.ai_cardneed[skill:objectName()]
			if type(callback)=="function" and callback(self.player,c,self)
			then return c:getEffectiveId() end
		end
	end
	if (not friendneedpeach and cid.peach) or peachnum>1 then return cid.peach end
	for _,target in sgs.qlist(self.room:getAlivePlayers())do
		if self:willSkipPlayPhase(target)
		or self:willSkipDrawPhase(target) then
			if cid.nullification then return cid.nullification
			elseif cid.snatch
			and self:isFriend(target)
			and not self:willSkipPlayPhase()
			and self.player:distanceTo(target)==1
			and self:hasTrickEffective(cid.snatch,target,self.player)
			then return cid.snatch:getEffectiveId()
			elseif cid.dismantlement
			and self.player~=target
			and self:isFriend(target)
			and not self:willSkipPlayPhase()
			and self:hasTrickEffective(cid.dismantlement,target,self.player)
			then return cid.dismantlement:getEffectiveId() end
		end
	end
	if SelfisCurrent then
		if cid.exnihilo then return cid.exnihilo end
		if (cid.jink or cid.analeptic)
		and (self:getCardsNum("Jink")<1 or self:isWeak() and self:getOverflow()<1)
		then return cid.jink or cid.analeptic end
		if cid.indulgence then return cid.indulgence end
	else
		local possible_attack = 0
		for _,enemy in ipairs(self.enemies)do
			if enemy:inMyAttackRange(self.player)
			and self:playerGetRound(CP,enemy)<self:playerGetRound(CP,self.player)
			then possible_attack = possible_attack+1 end
		end
		if possible_attack>self:getCardsNum("Jink") and self:getCardsNum("Jink")<=2 and self:getDefenseSlash()<=2
		then if cid.jink or cid.analeptic or cid.exnihilo then return cid.jink or cid.analeptic or cid.exnihilo end
		elseif cid.exnihilo or cid.indulgence then return cid.exnihilo or cid.indulgence end
	end
	if cid.nullification
	and (self:getCardsNum("Nullification")<2 or not NextPlayerCanUse)
	then return cid.nullification end
	if jinknum<2 and cid.jink and self:isEnemy(NextPlayer)
	and NextPlayer:isKongcheng()
	then return cid.jink end
	if cid.eight_diagram then
		local lord = getLord(self.player)
		if not self:hasSkills("yizhong|bazhen")
		and self:hasSkills("tiandu|leiji|nosleiji|noszhenlie|gushou|hongyan")
		and not self:getSameEquip(card) then return cid.eight_diagram end
		if NextPlayerisEnemy
		and self:hasSkills("tiandu|leiji|nosleiji|noszhenlie|gushou|hongyan",NextPlayer)
		and not self:getSameEquip(card,NextPlayer) then return cid.eight_diagram end
		if self.role=="loyalist"
		and self.player:getKingdom()=="wei"
		and not self.player:hasSkill("bazhen")
		and lord and lord:hasLordSkill("hujia")
		and (lord:objectName()~=NextPlayer:objectName() and NextPlayerisEnemy or lord:getArmor())
		then return cid.eight_diagram end
	end
	if cid.silver_lion then
		local lightning,canRetrial
		for _,aplayer in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if aplayer:hasSkills("leiji|nosleiji") and self:isEnemy(aplayer)
			then return cid.silver_lion end
			if aplayer:containsTrick("lightning")
			then lightning = true end
			if self:isEnemy(aplayer)
			and self:hasSkills("guicai|guidao",aplayer)
			then canRetrial = true end
		end
		if lightning and canRetrial then return cid.silver_lion end
		if self.player:isChained() then
			for _,friend in ipairs(self.friends)do
				if friend:hasArmorEffect("Vine") and friend:isChained()
				then return cid.silver_lion end
			end
		end
		if self.player:isWounded() then return cid.silver_lion end
	end
	if cid.vine then
		if sgs.ai_armor_value.vine(self.player,self)>0
		and self.room:alivePlayerCount()<=3
		then return cid.vine end
	end
	if cid.renwang_shield then
		if sgs.ai_armor_value.renwang_shield(self.player,self)>0 and self:getCardsNum("Jink")<1
		then return cid.renwang_shield end
	end
	if cid.DefensiveHorse
	and (not self.player:hasSkill("leiji|nosleiji") or self:getCardsNum("Jink")<1) then
		local before_num,after_num = 0,0
		for _,enemy in ipairs(self.enemies)do
			if enemy:canSlash(self.player,nil,true)
			then before_num = before_num+1 end
			if enemy:canSlash(self.player,nil,true,1)
			then after_num = after_num+1 end
		end
		if before_num>after_num and (self:isWeak() or self:getCardsNum("Jink")<1)
		then return cid.DefensiveHorse end
	end
	if cid.analeptic then
		local hit_num = 0
		for _,slash in ipairs(self:getCards("Slash"))do
			local d = self:aiUseCard(slash)
			if d.card then
				hit_num = hit_num+1
				for _,enemy in sgs.qlist(d.to)do
					if getCardsNum("Jink",enemy)<1
					or self:canLiegong(enemy,self.player)
					or self.player:hasSkills("tieji|wushuang|dahe|qianxi")
					or self.player:hasSkill("roulin") and enemy:isFemale()
					or (self.player:hasWeapon("Axe") or self:getCardsNum("Axe")>0) and self.player:getCards("he"):length()>4
					or (self.player:hasWeapon("blade") or self:getCardsNum("Blade")>0) and getCardsNum("Jink",enemy)<=hit_num
					or self:hasCrossbowEffect() and hit_num>=2
					then return cid.analeptic end
				end
			end
		end
	end
	if cid.weapon
	and (self:slashIsAvailable() or not SelfisCurrent) then
		local current_range = (self.player:getWeapon() and sgs.weapon_range[self.player:getWeapon():getClassName()]) or 1
		local slash = SelfisCurrent and self:getCard("Slash") or dummyCard()
		self:sort(self.enemies,"defense")
		if cid.crossbow then
			if self:getCardsNum("Slash")>1 or self:hasSkills("kurou|keji")
			or (self:hasSkills("luoshen|yongsi|luoying|guzheng") and not SelfisCurrent and self.room:alivePlayerCount()>=4)
			then return cid.crossbow end
			if self.player:hasSkill("guixin") and self.room:alivePlayerCount()>=6
			and (self.player:getHp()>1 or self:getCardsNum("Peach")>0)
			then return cid.crossbow end
			if self.player:hasSkill("rende") then
				for _,friend in ipairs(self.friends_noself)do
					if getCardsNum("Slash",friend)>1
					then return cid.crossbow end
				end
			end
			if self:isEnemy(NextPlayer) then
				local CanSave,huanggai,zhenji
				for _,enemy in ipairs(self.enemies)do
					if enemy:hasSkill("buyi") then CanSave = true end
					if enemy:hasSkill("jijiu") and getKnownCard(enemy,self.player,"red",nil,"he")>1 then CanSave = true end
					if enemy:hasSkill("chunlao") and enemy:getPile("wine"):length()>1 then CanSave = true end
					if enemy:hasSkill("kurou") then huanggai = enemy end
					if enemy:hasSkill("keji") then return cid.crossbow end
					if self:hasSkills("luoshen|yongsi|guzheng",enemy) then return cid.crossbow end
					if enemy:hasSkill("luoying") and card:getSuit()~=sgs.Card_Club then return cid.crossbow end
				end
				if huanggai then
					if huanggai:getHp()>2 then return cid.crossbow end
					if CanSave then return cid.crossbow end
				end
				if getCardsNum("Slash",NextPlayer)>=3 and NextPlayerisEnemy then return cid.crossbow end
			end
		end
		if cid.halberd then
			if self.player:hasSkills("nosrende|rende") and self:findFriendsByType(sgs.Friend_Draw)
			then return cid.halberd end
			if SelfisCurrent and self:getCardsNum("Slash")==1 and self.player:getHandcardNum()==1
			then return cid.halberd end
		end
		if cid.gudingdao then
			local range_fix = current_range-2
			for _,enemy in ipairs(self.enemies)do
				if self.player:canSlash(enemy,slash,true,range_fix)
				and enemy:isKongcheng() and not enemy:hasSkill("tianming")
				and (not SelfisCurrent or (self:getCardsNum("Dismantlement")>0 or (self:getCardsNum("Snatch")>0 and self.player:distanceTo(enemy)==1)))
				then return cid.gudingdao end
			end
		end
		if cid.axe then
			local range_fix = current_range-3
			local FFFslash = self:getCard("FireSlash")
			for _,enemy in ipairs(self.enemies)do
				if FFFslash and self.player:getCardCount(true)>=3
				and self.player:canSlash(enemy,FFFslash,true,range_fix)
				and self:ajustDamage(self.player,enemy,1,FFFslash)>1
				then return cid.axe
				elseif self:getCardsNum("Analeptic")>0
				and self.player:getCardCount(true)>=4
				and self.player:canSlash(enemy,slash,true,range_fix)
				and self:slashIsEffective(slash,enemy)
				then return cid.axe end
			end
		end
		if cid.double_sword then
			local range_fix = current_range-2
			for _,enemy in ipairs(self.enemies)do
				if self.player:getGender()~=enemy:getGender()
				and self.player:canSlash(enemy,nil,true,range_fix)
				then return cid.double_sword end
			end
		end
		if cid.qinggang_sword then
			local range_fix = current_range-2
			for _,enemy in ipairs(self.enemies)do
				if self.player:canSlash(enemy,slash,true,range_fix)
				and self:slashIsEffective(slash,enemy,self.player)
				then return cid.qinggang_sword end
			end
		end
	end
	for _,c in ipairs(cards)do
		if self:aiUseCard(c).card
		then return c:getEffectiveId() end
	end
	if cid.weapon and not self.player:getWeapon()
	and (self:slashIsAvailable() or not SelfisCurrent) then
		local inAttackRange
		for _,enemy in sgs.list(self.enemies)do
			if self.player:canSlash(enemy) then inAttackRange = true break end
		end
		if not inAttackRange then return cid.weapon end
	end
	cid = self:poisonCards(cards)
	for _,c in ipairs(cards)do
		if table.contains(cid,c)
		or c:isKindOf("TrickCard")
		or c:isKindOf("Peach")
		then continue end
		return c:getEffectiveId()
	end
	for _,c in ipairs(cards)do
		if table.contains(cid,c) then continue end
		return c:getEffectiveId()
	end
	return cards[1]:getEffectiveId()
end

--WoodenOx
local wooden_ox_skill = {}
wooden_ox_skill.name = "wooden_ox"
table.insert(sgs.ai_skills,wooden_ox_skill)
wooden_ox_skill.getTurnUseCard = function(self)
	self.wooden_ox_assist = nil
	local cards = self.player:getHandcards()
	cards = self:sortByUseValue(cards,true)
	local card,friend = self:getCardNeedPlayer(cards,false)
	if card and friend and (self:getOverflow()>0 or self:isWeak(friend)) then
		self.wooden_ox_assist = not self:isEnemy(friend) and friend
		return sgs.Card_Parse("@WoodenOxCard="..card:getEffectiveId())
	end
	if #cards>0 and (self:getOverflow()>0 or self:needKongcheng() and #cards<2) then
		self:sortByKeepValue(cards)
		card = cards[math.max(1,math.min(self.player:getMaxCards(),#cards))]
		return sgs.Card_Parse("@WoodenOxCard="..card:getEffectiveId())
	end
end

sgs.ai_skill_use_func.WoodenOxCard = function(card,use,self)
	use.card = card
end

sgs.ai_skill_playerchosen.wooden_ox = function(self,targets)
	return self.wooden_ox_assist
end

sgs.ai_playerchosen_intention.wooden_ox = -22

sgs.ai_use_priority.WoodenOxCard = 0
