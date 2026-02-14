sgs.ai_skill_invoke.aocai = function(self,data)
    return true
end

addAiSkills("aocai").getTurnUseCard = function(self)
	local ids = self.room:getDrawPile()
	if self.player:getMark("aocai_id-PlayClear")~=ids:at(1)
	then return sgs.Card_Parse("@AocaiCard=.") end
end

sgs.ai_skill_use_func["AocaiCard"] = function(card,use,self)
	use.card = card
	local ids = self.room:getDrawPile()
	self.player:setMark("aocai_id-PlayClear",ids:at(1))
end

sgs.ai_guhuo_card.aocai = function(self,toname,class_name)
    return "@AocaiCard=.:"..toname
end

sgs.ai_skill_use["@@aocai"] = function(self,prompt)
    local c = self.player:getMark("aocaiId")
	c = sgs.Sanguosha:getCard(c)
    local dummy = self:aiUseCard(c)
   	if dummy.card then
      	local tos = {}
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
       	return c:toString().."->"..table.concat(tos,"+")
    end
end

sgs.ai_skill_askforag.aocai = function(self,card_ids)
	local pattern = sgs.Sanguosha:getCurrentCardUsePattern()
	if pattern~="" then return card_ids[1] end
    for _,id in sgs.list(card_ids)do
		local c = sgs.Sanguosha:getCard(id)
		if self:aiUseCard(c).card
		then return id end
	end
end

addAiSkills("duwu").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,true)
	local toids = {}
	self:sort(self.enemies,"hp")
	for _,ep in sgs.list(self.enemies)do
		if self:isWeak(ep)
		and #cards>=ep:getHp()
		and self.player:inMyAttackRange(ep)
		and self.player:getHp()>1
		then
			for _,c in sgs.list(cards)do
				if #toids>=ep:getHp() then break end
				table.insert(toids,c:getEffectiveId())
			end
			self.duwu_to = ep
			toids = #toids>0 and table.concat(toids,"+") or "."
			return sgs.Card_Parse("@DuwuCard="..toids)
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:getHp()<2
		and #cards>=ep:getHp()
		and self.player:inMyAttackRange(ep)
		then
			for _,c in sgs.list(cards)do
				if #toids>=ep:getHp() then break end
				table.insert(toids,c:getEffectiveId())
			end
			self.duwu_to = ep
			toids = #toids>0 and table.concat(toids,"+") or "."
			return sgs.Card_Parse("@DuwuCard="..toids)
		end
	end
end

sgs.ai_skill_use_func["DuwuCard"] = function(card,use,self)
	use.card = card
	use.to:append(self.duwu_to)
end

sgs.ai_use_value.DuwuCard = 4.4
sgs.ai_use_priority.DuwuCard = -0.8

sgs.ai_skill_invoke.olpojun = function(self,data)
	local target = data:toPlayer()
	if target then
		return not self:isFriend(target)
	end
end

sgs.ai_use_revises.olpojun = function(self,card,use)
	if card:isKindOf("Slash") then
		card:setFlags("Qinggang")
	end
end

sgs.ai_skill_invoke.lixia = function(self,data)
    return true
end

sgs.ai_skill_invoke.biluan = function(self,data)
    return self.player:getHandcardNum()>3 and #self:getTurnUse()>2
end

sgs.ai_skill_choice.olmiji_draw = function(self,choices)
    return ""..self.player:getLostHp()
end

sgs.ai_skill_invoke.olmiji = function(self,data)
    if #self.friends==0 then return false end
    for _,friend in ipairs(self.friends)do
        if not friend:hasSkill("manjuan") and not self:isLihunTarget(friend) then return true end
    end
    return false
end

sgs.ai_skill_askforyiji.olmiji = function(self,card_ids)
    local available_friends = {}
    for _,friend in ipairs(self.friends)do
        if not friend:hasSkill("manjuan") and not self:isLihunTarget(friend) then table.insert(available_friends,friend) end
    end
    local toGive,allcards = {},{}
    local keep
    for _,id in ipairs(card_ids)do
        local card = sgs.Sanguosha:getCard(id)
        if not keep and (isCard("Jink",card,self.player) or isCard("Analeptic",card,self.player)) then
            keep = true
        else
            table.insert(toGive,card)
        end
        table.insert(allcards,card)
    end
    local cards = #toGive>0 and toGive or allcards
    self:sortByKeepValue(cards,true)
    local id = cards[1]:getId()
    local card,friend = self:getCardNeedPlayer(cards,true)
    if card and friend and table.contains(available_friends,friend) then 
        if friend:objectName()==self.player:objectName() then 
            return nil,-1
        else
            return friend,card:getId() 
        end
    end
    if #available_friends>0 then
        self:sort(available_friends,"handcard")
        for _,afriend in ipairs(available_friends)do
            if not self:needKongcheng(afriend,true) then
                if afriend:objectName()==self.player:objectName() then 
                    return nil,-1
                else
                    return afriend,id
                end
            end
        end
        self:sort(available_friends,"defense")
        if available_friends[1]:objectName()==self.player:objectName() then 
            return nil,-1
        else
            return available_friends[1],id
        end
    end
    return nil,-1
end

sgs.ai_skill_use["@@bushi"] = function(self,prompt,method)
    local zhanglu = self.room:findPlayerBySkillName("bushi")
    if not zhanglu then return "." end
    local ids = zhanglu:getPile("rice")
    if self:isEnemy(zhanglu) and ids:length()==1 and zhanglu:isWounded() then return "." end
    if self:isFriend(zhanglu) then
		local damage = self.room:getTag("CurrentDamageStruct"):toDamage()
		if damage.damage<ids:length() and self:getOverflow()>1 or not zhanglu:isWounded() then
			return "."
		end
	end
    local cards = {}
    for _,id in sgs.list(ids)do
        table.insert(cards,sgs.Sanguosha:getCard(id))
    end
    self:sortByUseValue(cards,true)
    return "@BushiCard="..cards[1]:getEffectiveId()    
end    

sgs.ai_skill_use["@@midao"] = function(self,prompt,method)
    local judge = self.player:getTag("judgeData"):toJudge()
    local ids = self.player:getPile("rice")
    if sgs.getMode:find("_mini_46") and not judge:isGood() then return "@MidaoCard="..ids:first() end
    local cards = {}
    for _,id in sgs.list(ids)do
        table.insert(cards,sgs.Sanguosha:getCard(id))
    end
    if self:needRetrial(judge) then
        local id = self:getRetrialCardId(cards,judge)
        if id~=-1 then return "@MidaoCard="..id end
    end
	if self:isWeak() and #cards<2
	and self.player:getLostHp()>0
	then
        local id = self:getRetrialCardId(cards,judge,nil,true)
        if id~=-1 then return "@MidaoCard="..id end
	end
    return "."    
end

sgs.ai_skill_invoke["olmeibu2"] = function(self,data)
   	local target = self.room:getCurrent()
	return self:isEnemy(target)
	and (self:getCardsNum("Jink")>0 or not self:isWeak())
end

--[[
    技能：安恤（阶段技）
    描述：你可以选择两名手牌数不同的其他角色，令其中手牌多的角色将一张手牌交给手牌少的角色，然后若这两名角色手牌数相等，你选择一项：1．摸一张牌；2．回复1点体力。
]]--
--OlAnxuCard:Play
anxu_skill = {
    name = "olanxu",
    getTurnUseCard = function(self,inclusive)
        if self.room:alivePlayerCount()>2
		then return sgs.Card_Parse("@OlAnxuCard=.") end
    end,
}
table.insert(sgs.ai_skills,anxu_skill)
sgs.ai_skill_use_func["OlAnxuCard"] = function(card,use,self)
    local tos = self.room:getOtherPlayers(self.player)
	for i,to1 in sgs.list(tos)do
		for i,to2 in sgs.list(tos)do
			if to1:getHandcardNum()<to2:getHandcardNum()
			and self:isFriend(to1) and self:isEnemy(to2)
			then
				use.card = card
				use.to:append(to1)
				use.to:append(to2)
				return
			end
		end
	end
	for i,to1 in sgs.list(tos)do
		for i,to2 in sgs.list(tos)do
			if to1:getHandcardNum()<to2:getHandcardNum()
			and self:isFriend(to1) and not self:isFriend(to2)
			then
				use.card = card
				use.to:append(to1)
				use.to:append(to2)
				return
			end
		end
	end
	for i,to1 in sgs.list(tos)do
		for i,to2 in sgs.list(tos)do
			if to1:getHandcardNum()<to2:getHandcardNum()
			and not self:isEnemy(to1) and self:isEnemy(to2)
			then
				use.card = card
				use.to:append(to1)
				use.to:append(to2)
				return
			end
		end
	end
	for i,to1 in sgs.list(tos)do
		for i,to2 in sgs.list(tos)do
			if to1:getHandcardNum()<to2:getHandcardNum()
			and not self:isEnemy(to1) and not self:isFriend(to2)
			then
				use.card = card
				use.to:append(to1)
				use.to:append(to2)
				return
			end
		end
	end
	for i,to1 in sgs.list(tos)do
		for i,to2 in sgs.list(tos)do
			if to1:getHandcardNum()<to2:getHandcardNum()
			and self:isFriend(to1) and self:isFriend(to2)
			then
				use.card = card
				use.to:append(to1)
				use.to:append(to2)
				return
			end
		end
	end
	for i,to1 in sgs.list(tos)do
		for i,to2 in sgs.list(tos)do
			if to1:getHandcardNum()<to2:getHandcardNum()
			and not self:isEnemy(to1) and not self:isEnemy(to2)
			then
				use.card = card
				use.to:append(to1)
				use.to:append(to2)
				return
			end
		end
	end
end
--room->askForExchange(playerA,"olanxu",1,1,false,QString("@olanxu:%1:%2").arg(source->objectName()).arg(playerB->objectName()))
sgs.ai_skill_discard["olanxu"] = function(self,discard_num,min_num,optional,include_equip,pattern)
    for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
        if p:hasFlag("olanxu_target") and self:isFriend(target)
		and not hasManjuanEffect(target)
		then
			local handcards = self.player:getHandcards()
			handcards = self:sortByUseValue(handcards)
			return {handcards[1]:getEffectiveId()}
		end
    end
    return self:askForDiscard("dummy",discard_num,min_num,optional,include_equip,pattern)
end
--room->askForChoice(source,"olanxu",choices)
sgs.ai_skill_choice["olanxu"] = function(self,choices,data)
    local items = choices:split("+")
    if #items==1 then
        return items[1]
    end
    return "recover"
end
--[[
    技能：追忆
    描述：你死亡时，你可以令一名其他角色（除杀死你的角色）摸三张牌并回复1点体力。
]]--

--[[
    技能：陈情
    描述：每轮限一次，当一名角色处于濒死状态时，你可以令另一名其他角色摸四张牌，然后弃置四张牌。若其以此法弃置的四张牌花色各不相同，则视为该角色对濒死的角色使用一张【桃】
]]--
--room->askForPlayerChosen(source,targets,"olchenqing",QString("@olchenqing:%1").arg(victim->objectName()),false,true)
sgs.ai_skill_playerchosen["olchenqing"] = function(self,targets)
    local victim = self.room:getCurrentDyingPlayer()
    local help = false
    local careLord = false
    if victim then
        if self:isFriend(victim) then
            help = true
        elseif self.role=="renegade" and victim:isLord() and self.room:alivePlayerCount()>2 then
            help = true
            careLord = true
        end
    end
    local friends,enemies = {},{}
    for _,p in sgs.list(targets)do
        if self:isFriend(p) then
            table.insert(friends,p)
        else
            table.insert(enemies,p)
        end
    end
    local compare_func = function(a,b)
        local nA = a:getCardCount(true)
        local nB = b:getCardCount(true)
        if nA==nB then
            return a:getHandcardNum()>b:getHandcardNum()
        else
            return nA>nB
        end
    end
    if help and #friends>0 then
        table.sort(friends,compare_func)
        for _,friend in ipairs(friends)do
            if not hasManjuanEffect(friend) then
                return friend
            end
        end
    end
    if careLord and #enemies>0 then
        table.sort(enemies,compare_func)
        for _,enemy in ipairs(enemies)do
            if sgs.ai_role[enemy:objectName()]=="loyalist" then
                return enemy
            end
        end
    end
    if #enemies>0 then
        self:sort(enemies,"threat")
        for _,enemy in ipairs(enemies)do
            if hasManjuanEffect(enemy) then
                return enemy
            end
        end
    end
    if #friends>0 then
        self:sort(friends,"defense")
        for _,friend in ipairs(friends)do
            if not hasManjuanEffect(friend) then
                return friend
            end
        end
    end
end
--room->askForExchange(target,"olchenqing",4,4,true,QString("@olchenqing-exchange:%1:%2").arg(source->objectName()).arg(victim->objectName()),false)
sgs.ai_skill_discard["olchenqing"] = function(self,discard_num,min_num,optional,include_equip)
    local victim = self.room:getCurrentDyingPlayer()
    local help = false
    if victim
	then
        if self:isFriend(victim)
		then help = true
        elseif self.role=="renegade"
		and victim:isLord() and self.room:alivePlayerCount()>2
		then help = true end
    end
    if help
	then
        local peach_num = 0
        local suits,ids = {},{}
		local cards = self.player:getCards("he")
		cards = self:sortByKeepValue(cards,nil,true)
        for _,c in ipairs(cards)do
            if isCard("Peach",c,self.player)
			then peach_num = peach_num+1
            else
                if table.contains(suits,c:getSuit())
				then continue end
				table.insert(suits,c:getSuit())
				table.insert(ids,c:getEffectiveId())
            end
        end
        if peach_num+victim:getHp()<1
		or #ids>3 then return ids end
    end
    return self:askForDiscard("dummy",discard_num,min_num,optional,include_equip)
end

sgs.ai_skill_playerchosen["chenqing"] = function(self,targets)
	return sgs.ai_skill_playerchosen["olchenqing"](self,targets)
end

sgs.ai_skill_discard["Chenqing"] = function(self,discard_num,min_num,optional,include_equip)
	return sgs.ai_skill_discard["olchenqing"](self,discard_num,min_num,optional,include_equip)
end

--[[
    技能：默识
    描述：结束阶段开始时，你可以将一张手牌当你本回合出牌阶段使用的第一张基本或非延时类锦囊牌使用。然后，你可以将一张手牌当你本回合出牌阶段使用的第二张基本或非延时类锦囊牌使用。
]]--

sgs.ai_skill_use["@@Omozhi"] = function(self,prompt,method)
    local list = self.player:property("allowed_guhuo_dialog_buttons"):toString():split("+")
    local cards = self.player:getCards("h")
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
	if #list<1 or #cards<1 then return end
	list = dummyCard(list[1])
	list:setSkillName("Omozhi")
	list:addSubcard(cards[1])
    local dummy = self:aiUseCard(list)
   	if dummy.card and dummy.to
   	then
		if list:canRecast() and dummy.to:length()<1
		and method~=sgs.Card_MethodRecast
		then return end
      	local tos = {}
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
       	return list:toString().."->"..table.concat(tos,"+")
    end
end

sgs.ai_skill_use["@@mozhi"] = function(self,prompt,method)
    local c = self.player:property("mozhi"):toString()
    local cards = self.player:getCards("h")
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
	c = dummyCard(c)
	if #cards<1 or not c then return end
	c:setSkillName("mozhi")
 	c:addSubcard(cards[1])
	local dummy = self:aiUseCard(c)
   	if dummy.card and dummy.to
   	then
		if c:canRecast() and dummy.to:length()<1
		and method~=sgs.Card_MethodRecast
		then return end
      	local tos = {}
       	for _,p in sgs.list(dummy.to)do
       		table.insert(tos,p:objectName())
       	end
       	return c:toString().."->"..table.concat(tos,"+")
    end
end

--[[
    技能：庸肆（锁定技）
    描述：摸牌阶段开始时，你改为摸X张牌。锁定技，弃牌阶段开始时，你选择一项：1．弃置一张牌；2．失去1点体力。（X为场上势力数） 
]]--
--room->askForDiscard(player,"olyongsi",1,1,true,true,"@olyongsi")
sgs.ai_skill_discard["olyongsi"] = function(self,discard_num,min_num,optional,include_equip)
    if self:needToLoseHp() or getBestHp(self.player)<=self.player:getHp()
	then return "." end
    return self:askForDiscard("dummy",discard_num,min_num,optional,include_equip)
end
--[[
    技能：觊玺（觉醒技）
    描述：你的回合结束时，若你连续三回合没有失去过体力，则你加1点体力上限并回复1点体力，然后选择一项：1．获得技能“妄尊”；2．摸两张牌并获得当前主公的主公技。
]]--
--room->askForChoice(player,"oljixi",choices.join("+"))
sgs.ai_skill_choice["oljixi"] = function(self,choices,data)
    local items = choices:split("+")
    if #items==1 then
        return items[1]
    end
    return "wangzun"
end

--[[
    技能：仁德
    描述：出牌阶段，你可以将任意张手牌交给一名其他角色，然后你于此阶段内不能再次以此法交给该角色牌。当你以此法交给其他角色的牌数在同一阶段内首次达到两张或更多时，你回复1点体力
]]--
function OlRendeArrange(self,cards,friends,enemies,unknowns,arrange,recover_only)
	recover_only = recover_only or true
    if #enemies>0 then
        self:sort(enemies,"hp")
        for _,card in ipairs(cards)do
            if card:isKindOf("Shit") then
                return enemies[1],card,"enemy"
            end
        end
    end
    if #friends>0 then
        self:sort(friends,"defense")
        for _,friend in ipairs(friends)do
            local arranged = arrange[friend:objectName()] or {}
            if self:isWeak(friend) and friend:getHandcardNum()+#arranged<3 then
                for _,card in ipairs(cards)do
                    if card:isKindOf("Shit") then
                    elseif isCard("Peach",card,friend) or isCard("Analeptic",card,friend)
					then return friend,card,"friend"
                    elseif isCard("Jink",card,friend) and self:getEnemyNumBySeat(self.player,friend)>0
					then return friend,card,"friend"
                    end
                end
            end
        end
        for _,friend in ipairs(friends)do
            local arranged = arrange[friend:objectName()] or {}
            if friend:getHp()<=2 and friend:faceUp() then
                for _,card in ipairs(cards)do
                    if card:isKindOf("Armor") then
                        if not friend:getArmor() and not self:hasSkills("yizhong|bazhen|bossmanjia",friend) then
                            local given = false
                            for _,c in ipairs(arranged)do
                                if c:isKindOf("Armor") then
                                    given = true
                                    break
                                end
                            end
                            if not given then
                                return friend,card,"friend"
                            end
                        end
                    elseif card:isKindOf("DefensiveHorse") then
                        if not friend:getDefensiveHorse() then
                            local given = false
                            for _,c in ipairs(arranged)do
                                if c:isKindOf("DefensiveHorse") then
                                    given = true
                                    break
                                end
                            end
                            if not given then
                                return friend,card,"friend"
                            end
                        end
                    end
                end
            end
        end
        for _,friend in ipairs(friends)do
            local arranged = arrange[friend:objectName()] or {}
            if friend:getHandcardNum()+#arranged<4 then
                if friend:hasSkill("jijiu") then
                    for _,card in ipairs(cards)do
                        if card:isRed() then
                            return friend,card,"friend"
                        end
                    end
                end
                if friend:hasSkill("jieyin") then
                    return friend,cards[1],"friend"
                elseif friend:hasSkill("nosrenxin") and friend:isKongcheng() then
                    return friend,cards[1],"friend"
                end
            end
        end
        for _,friend in ipairs(friends)do
            if self:hasSkills("wusheng|longdan|wushen|keji|chixin",friend) then
                local arranged = arrange[friend:objectName()] or {}
                if friend:getHandcardNum()+#arranged>=2 and not self:hasCrossbowEffect(friend) then
                    for _,card in ipairs(cards)do
                        if card:isKindOf("Crossbow") then
                            local given = false
                            for _,c in ipairs(arranged)do
                                if c:isKindOf("Crossbow") then
                                    given = true
                                    break
                                end
                            end
                            if not given then
                                return friend,card,"friend"
                            end
                        end
                    end
                end
            end
        end
        for _,friend in ipairs(friends)do
            local arranged = arrange[friend:objectName()] or {}
            local has_crossbow = self:hasCrossbowEffect(friend)
            if not has_crossbow then
                for _,c in ipairs(arranged)do
                    if c:isKindOf("Crossbow") then
                        has_crossbow = true
                        break
                    end
                end
            end
            if has_crossbow or getKnownCard(friend,self.player,"Crossbow")>0 then
                for _,p in ipairs(self.enemies)do
                    if self:isGoodTarget(p,self.enemies,dummyCard()) and friend:distanceTo(p)<=1 then
                        for _,card in ipairs(cards)do
                            if isCard("Slash",card,friend) then
                                return friend,card,"friend"
                            end
                        end
                    end
                end
            end
        end
        local compareByAction = function(a,b)
            return self.room:getFront(a,b):objectName()==a:objectName()
        end
        table.sort(friends,compareByAction)
        for _,friend in ipairs(friends)do
            local flag = string.format("weapon_done_%s_%s",self.player:objectName(),friend:objectName())
            if friend:faceUp() and not friend:hasFlag(flag) then
                local can_slash = false
                local others = self.room:getOtherPlayers(friend)
                for _,p in sgs.list(others)do
                    if self:isEnemy(p) and self:isGoodTarget(p,self.enemies) then
                        if friend:distanceTo(p)<=friend:getAttackRange() then
                            can_slash = true
                            break
                        end
                    end
                end
                if not can_slash then
                    for _,p in sgs.list(others)do
                        if self:isEnemy(p) and self:isGoodTarget(p,self.enemies) then
                            local distance = friend:distanceTo(p)
                            local range = friend:getAttackRange()
                            if distance>range then
                                for _,card in ipairs(cards)do
                                    if card:isKindOf("Weapon") then
                                        if not friend:getWeapon() then
                                            if distance<=range+(sgs.weapon_range[card:getClassName()] or 0) then
                                                self.room:setPlayerFlag(friend,flag)
                                                return friend,card,"friend"
                                            end
                                        end
                                    elseif card:isKindOf("OffensiveHorse") then
                                        if not friend:getOffensiveHorse() then
                                            if distance<=range+1 then
                                                self.room:setPlayerFlag(friend,flag)
                                                return friend,card,"friend"
                                            end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
        local compareByNumber = function(a,b)
            return a:getNumber()>b:getNumber()
        end
        table.sort(cards,compareByNumber)
        for _,friend in ipairs(friends)do
            if friend:faceUp() then
                local skills = friend:getVisibleSkillList(true)
                for _,skill in sgs.list(skills)do
                    local callback = sgs.ai_cardneed[skill:objectName()]
                    if type(callback)=="function" then
                        for _,card in ipairs(cards)do
                            if callback(friend,card,self) then
                                return friend,card,"friend"
                            end
                        end
                    end
                end
            end
        end
        for _,card in ipairs(cards)do
            if card:isKindOf("Shit") then
                for _,friend in ipairs(friends)do
                    if self:isWeak(friend) then
                    elseif friend:hasSkill("jueqing") or card:getSuit()==sgs.Card_Spade then
                        if friend:hasSkill("zhaxiang") then
                            return friend,card,"friend"
                        end
                    elseif self:hasSkills("guixin|jieming|yiji|nosyiji|chengxiang|noschengxiang|jianxiong",friend) then
                        return friend,card,"friend"
                    end
                end
            end
        end
        if self.role=="lord" and self.player:hasLordSkill("jijiang") then
            for _,friend in ipairs(friends)do
                local arranged = arrange[friend:objectName()] or {}
                if friend:getKingdom()=="shu" and friend:getHandcardNum()+#arranged<3 then
                    for _,card in ipairs(cards)do
                        if isCard("Slash",card,friend) then
                            return friend,card,"friend"
                        end
                    end
                end
            end
        end
    end
    if #enemies>0 then
        self:sort(enemies,"defense")
        for _,enemy in ipairs(enemies)do
            if enemy:hasSkill("kongcheng") and enemy:isKongcheng() then
                if not enemy:hasSkill("manjuan") then
                    for _,card in ipairs(cards)do
                        if isCard("Jink",card,enemy) then
                        elseif card:isKindOf("Disaster") or card:isKindOf("Shit") then
                            return enemy,card,"enemy"
                        elseif card:isKindOf("Collateral") or card:isKindOf("AmazingGrace") then
                            return enemy,card,"enemy"
                        elseif card:isKindOf("OffensiveHorse") or card:isKindOf("Weapon") then
                            return enemy,card,"enemy"
                        end
                    end
                end
            end
        end
    end
    local overflow = self:getOverflow()
    if #friends>0 then
        for _,friend in ipairs(friends)do
            local arranged = arrange[friend:objectName()] or {}
            if self:willSkipPlayPhase(friend) then
            elseif self:hasSkills(sgs.priority_skill,friend) and friend:getHandcardNum()+#arranged<=3 then
                if overflow-#arranged>0 or self.player:getHandcardNum()-#arranged>3 then
                    return friend,cards[1],"friend"
                end
            end
        end
    end
    if overflow>0 and #friends>0 then
        for _,card in ipairs(cards)do
            if not self:aiUseCard(card).card then
                self:sort(friends,"defense")
                return friends[1],card,"friend"
            end
        end
    end
	
	local slash = dummyCard()
	local will_use_slash = false
	if self:aiUseCard(slash).card
	then will_use_slash = true end
	if not will_use_slash then
		local fire_slash = dummyCard("fire_slash")
		if self:aiUseCard(fire_slash).card
		then will_use_slash = true end
		if not will_use_slash then
			local thunder_slash = dummyCard("thunder_slash")
			if self:aiUseCard(thunder_slash).card then
				will_use_slash = true
			end
		end
	end
	
    if arrange["count"]<2 and self.player:getHandcardNum()>=2
	and ((self.player:getLostHp()>0 and self:isWeak()) or (not recover_only and will_use_slash))
	then
        if #friends>0 then
            return friends[1],cards[1],"friend"
        elseif #unknowns>0 then
            self:sortByKeepValue(cards)
            for _,p in ipairs(unknowns)do
                if p:hasSkill("manjuan") then
                    return p,cards[1],"unknown"
                end
            end
            self:sort(unknowns,"threat")
            return unknowns[#unknowns],cards[1],"unknown"
        elseif #enemies>0 then
            for _,enemy in ipairs(enemies)do
                if enemy:hasSkill("manjuan") then
                    return enemy,cards[1],"enemy"
                end
            end
        end
    end
end
local function resetPlayers(players,except)
    local result = {}
    for _,p in ipairs(players)do
        if not p:objectName()==except:objectName() then
            table.insert(result,p)
        end
    end
    return result
end
--[[local rende_skill = {
    name = "olrende",
    getTurnUseCard = function(self,inclusive)
        if not self.player:isKongcheng() then
            return sgs.Card_Parse("@OlRendeCard=.")
        end
    end,
}
table.insert(sgs.ai_skills,rende_skill)
sgs.ai_skill_use_func["OlRendeCard"] = function(card,use,self)
    local names = self.player:property("olrende"):toString():split("+")
    local others = self.room:getOtherPlayers(self.player)
    local friends,enemies,unknowns = {},{},{}
    local arrange = {}
    arrange["count"] = 0
    for _,p in sgs.list(others)do
        local can_give = true
        for _,name in ipairs(names)do
            if name==p:objectName() then
                can_give = false
                break
            end
        end
        if can_give then
            arrange[p:objectName()] = {}
            if self:isFriend(p) then
                table.insert(friends,p)
            elseif self:isEnemy(p) then
                table.insert(enemies,p)
            else
                table.insert(unknowns,p)
            end
        end
    end
    local new_friends = {}
    for _,friend in ipairs(friends)do
        local exclude = false
        if self:needKongcheng(friend,true) or self:willSkipPlayPhase(friend) then
            exclude = true
            if self:hasSkills("keji|qiaobian|shensu",friend) then
                exclude = false
            elseif friend:getHp()-friend:getHandcardNum()>=3 then
                exclude = false
            elseif friend:isLord() and self:isWeak(friend) and self:getEnemyNumBySeat(self.player,friend)>=1 then
                exclude = false
            end
        end
        if not exclude and not hasManjuanEffect(friend) and self:objectiveLevel(friend)<=-2 then
            table.insert(new_friends,friend)
        end
    end
    friends = new_friends
    local overflow = self:getOverflow()
    if overflow<=0 and #friends==0 then
        return 
    end
    local handcards = self.player:getHandcards()
    handcards = sgs.QList2Table(handcards)
    self:sortByUseValue(handcards)
    while true do
        if #handcards==0 then
            break
        end
        local target,to_give,group = OlRendeArrange(self,handcards,friends,enemies,unknowns,arrange)
        if target and to_give and group then
            table.insert(arrange[target:objectName()],to_give)
            arrange["count"] = arrange["count"]+1
            handcards = self:resetCards(handcards,to_give)
        else
            break
        end
    end
    local max_count,max_name = 0,nil
    for name,cards in pairs(arrange)do
        if type(cards)=="table" then
            local count = #cards
            if count>max_count then
                max_count = count
                max_name = name
            end
        end
    end
    if max_count==0 or not max_name then
        return 
    end
    local max_target = nil
    for _,p in sgs.list(others)do
        if p:objectName()==max_name then
            max_target = p
            break
        end
    end
    if max_target and type(arrange[max_name])=="table" and #arrange[max_name]>0 then
        local to_use = {}
        for _,c in ipairs(arrange[max_name])do
            table.insert(to_use,c:getEffectiveId())
        end
        local card_str = "@OlRendeCard="..table.concat(to_use,"+")
        local acard = sgs.Card_Parse(card_str)
        assert(acard)
        use.card = acard
        use.to:append(max_target)
    end
end
sgs.ai_use_value.OlRendeCard = sgs.ai_use_value.RendeCard
sgs.ai_use_priority.OlRendeCard = sgs.ai_use_priority.RendeCard
sgs.ai_card_intention.OlRendeCard = sgs.ai_card_intention.RendeCard
sgs.dynamic_value.benefit.OlRendeCard = true]]
--[[
    技能：激将（主公技）
    描述：每当你需要使用或打出一张【杀】时，你可以令其他蜀势力角色打出一张【杀】，视为你使用或打出之。
]]

sgs.ai_skill_choice.fengpo = function(self,choices,data)
	local use = data:toCardUse()
	local to,slash = use.to:first(),use.card
	if to:isDead() or not self:isEnemy(to) or not self:slashIsEffective(slash,to,from) then return "drawCards" end
	local nature = sgs.DamageStruct_Normal
	if slash:isKindOf("FireSlash") then nature = sgs.DamageStruct_Fire
	elseif slash:isKindOf("ThunderSlash") then nature = sgs.DamageStruct_Thunder end
	if not self:damageIsEffective(to,nature,from) then return "drawCards" end
	if getCardsNum("Jink",to,self.player)==0 and not self:cantDamageMore(self.player,to) then return "addDamage" end
	return "drawCards"
end

sgs.ai_skill_discard["olqingjian"] = function(self,discard_num,min_num,optional,include_equip,pattern)
    local qj = self.player:getTag("olqingjian"):toIntList()
	qj = sgs.QList2Table(qj)
	local n = #qj
	local ids = {}
	for i=1,n do
		local to,id = sgs.ai_skill_askforyiji.nosyiji(self,qj)
		if to and id then
			table.insert(ids,id)
			table.removeOne(qj,id)
		end
	end
	return ids
end

sgs.ai_skill_use["@@olqingjian!"] = function(self,prompt,method)
    local qj = self.player:getPile("olqingjian")
	local to,id = sgs.ai_skill_askforyiji.nosyiji(self,sgs.QList2Table(qj))
	if to and id then
		return "@OlQingjianCard="..id.."->"..to:objectName()
	end
    local cards = {}
    for _,id in sgs.list(qj)do
        table.insert(cards,sgs.Sanguosha:getCard(id))
    end
    self:sortByUseValue(cards,true)
	for _,p in sgs.list(self.friends_noself)do
		if self:canDraw(p) and not hasManjuanEffect(p) then
			return "@OlQingjianCard="..cards[1]:getEffectiveId().."->"..p:objectName()
		end
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if not self:isEnemy(p) and not hasManjuanEffect(p) then
			return "@OlQingjianCard="..cards[1]:getEffectiveId().."->"..p:objectName()
		end
	end
	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		return "@OlQingjianCard="..cards[1]:getEffectiveId().."->"..p:objectName()
	end
end    



sgs.ai_skill_invoke.liangzhu = function(self,data)
	return true
end

sgs.ai_skill_choice.liangzhu = function(self,choices,data)
	local current = self.room:getCurrent()
	if self:isFriend(current) and (self:isWeak(current) or current:hasSkills(sgs.cardneed_skill))then
		return "letdraw"
	end
	return "draw"
end

sgs.ai_skill_invoke.cihuai = function(self,data)
	local has_slash = false
	local cards = self.player:getCards("h")
	cards=sgs.QList2Table(cards)
	for _,card in ipairs(cards)do
		if card:isKindOf("Slash") then has_slash = true end
	end
	if has_slash then return false end

	self:sort(self.enemies,"defenseSlash")
	for _,enemy in ipairs(self.enemies)do
		local slash = dummyCard()
		local eff = self:slashIsEffective(slash,enemy) and self:isGoodTarget(enemy,self.enemies,slash)
		if eff and self.player:canSlash(enemy) and not self:slashProhibit(nil,enemy) then
			return true
		end
	end

return false
end

local cihuai_skill = {}
cihuai_skill.name = "cihuai"
table.insert(sgs.ai_skills,cihuai_skill)
cihuai_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("slash:_cihuai[no_suit:0]=.")
end

sgs.ai_skill_invoke.kunfen = function(self,data)
	if not self:isWeak() and (self:getCardsNum("Peach")+self.player:getHp()>1 or hasZhaxiangEffect(self.player)) then
		return true
	end
end

local chixin_skill={}
chixin_skill.name="chixin"
table.insert(sgs.ai_skills,chixin_skill)
chixin_skill.getTurnUseCard = function(self,inclusive)
	local cards = self.player:getCards("he")
	cards=sgs.QList2Table(cards)

	local diamond_card

	self:sortByUseValue(cards,true)

	local useAll = false
	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if enemy:getHp()==1 and not enemy:hasArmorEffect("EightDiagram") and self.player:distanceTo(enemy)<=self.player:getAttackRange() and self:isWeak(enemy)
			and getCardsNum("Jink",enemy,self.player)+getCardsNum("Peach",enemy,self.player)+getCardsNum("Analeptic",enemy,self.player)==0 then
			useAll = true
			break
		end
	end

	local disCrossbow = false
	if self:getCardsNum("Slash")<2 or self.player:hasSkill("paoxiao") then
		disCrossbow = true
	end


	for _,card in ipairs(cards)  do
		if card:getSuit()==sgs.Card_Diamond
		and (not isCard("Peach",card,self.player) and not isCard("ExNihilo",card,self.player) and not useAll)
		and (not isCard("Crossbow",card,self.player) and not disCrossbow)
		and (self:getUseValue(card)<sgs.ai_use_value.Slash or inclusive or sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,dummyCard())>0) then
			diamond_card = card
			break
		end
	end
	if not diamond_card then return nil end
	local suit = diamond_card:getSuitString()
	local number = diamond_card:getNumberString()
	local card_id = diamond_card:getEffectiveId()
	return sgs.Card_Parse(("slash:chixin[%s:%s]=%d"):format(suit,number,card_id))

end

sgs.ai_view_as.chixin = function(card,player,card_place,class_name)
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	if card_place~=sgs.Player_PlaceSpecial and card:getSuit()==sgs.Card_Diamond and not card:isKindOf("Peach") and not card:hasFlag("using") then
		if class_name=="Slash" then
			return ("slash:chixin[%s:%s]=%d"):format(suit,number,card_id)
		elseif class_name=="Jink" then
			return ("jink:chixin[%s:%s]=%d"):format(suit,number,card_id)
		end
	end
end

sgs.ai_cardneed.chixin = function(to,card)
	return card:getSuit()==sgs.Card_Diamond
end

sgs.ai_skill_playerchosen.suiren = function(self,targets)
	if self.player:getMark("@suiren")==0 then return "." end
	if self:isWeak() and (self:getOverflow()<-2 or not self:willSkipPlayPhase()) then return self.player end
	self:sort(self.friends_noself,"defense")
	for _,friend in ipairs(self.friends)do
		if self:isWeak(friend) and not self:needKongcheng(friend) then
			return friend
		end
	end
	self:sort(self.enemies,"defense")
	for _,enemy in ipairs(self.enemies)do
		if (self:isWeak(enemy) and enemy:getHp()==1)
			and self.player:getHandcardNum()<2 and not self:willSkipPlayPhase() and self.player:inMyAttackRange(enemy) then
			return self.player
		end
	end
end

sgs.ai_playerchosen_intention.suiren = -60

sgs.ai_skill_use["@@jiqiao"] = function(self,prompt,method)
	local usable_cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByUseValue(usable_cards,true)
	local use_cards = {}
	for _,c in ipairs(usable_cards)do
		if c:isKindOf("Armor") or c:isKindOf("DefensiveHorse") or c:isKindOf("OffensiveHorse") or (c:isKindOf("WoodenOx") and self.player:getPile("wooden_ox"):length()==0) then
			table.insert(use_cards,c:getEffectiveId())
		end
	end
	if #use_cards==0 then return end
	if #use_cards>0 then
		return "@JiqiaoCard="..table.concat(use_cards,"+")
	end
end


addAiSkills("xufa").getTurnUseCard = function(self)
	local ids = self.player:getPile("xufa")
	local toids = {}
	if ids:length()>0 and self.player:getMark("xufa2-PlayClear")<1 then
		local xf = {}
		for _,id in sgs.list(ids)do
			table.insert(xf,sgs.Sanguosha:getCard(id))
		end
		xf = self:sortByUseValue(xf)
		local cards = self.player:getCards("he")
		cards = self:sortByUseValue(cards)
		for _,c in sgs.list(xf)do
			if c:isNDTrick() and #cards>0 then
				local dc = dummyCard(c:objectName())
				dc:setSkillName("xufa")
				table.insert(toids,c:getId())
				for _,c in sgs.list(sgs.reverse(xf))do
					if not table.contains(toids,c:getId()) and #toids<#xf/2 then
						table.insert(toids,c:getId())
					end
				end
				dc:addSubcard(cards[#cards])
				if dc:isAvailable(self.player) then
					local d = self:aiUseCard(dc)
					if d.card then
						if dc:canRecast() and d.to:length()<1
						then continue end
						self.xufaUse = d
						sgs.ai_skill_choice.xufa = c:objectName()
						return sgs.Card_Parse("@XufaCard="..table.concat(toids,"+"))
					end
				end
				toids = {}
			end
		end
	end
	local cards = self.player:getCards("he")
	if cards:length()>0 and self.player:getMark("xufa1-PlayClear")<1 then
		cards = self:sortByUseValue(cards)
		for _,c in sgs.list(cards)do
			if c:isNDTrick() then
				local dc = dummyCard(c:objectName())
				dc:setSkillName("xufa")
				table.insert(toids,c:getId())
				for _,c in sgs.list(sgs.reverse(cards))do
					if not table.contains(toids,c:getId()) and #toids<#cards/2 then
						if dc:subcardsLength()<1 then
							dc:addSubcard(c)
						else
							table.insert(toids,c:getId())
						end
					end
				end
				if dc:isAvailable(self.player) then
					local d = self:aiUseCard(dc)
					if d.card then
						if dc:canRecast() and d.to:length()<1
						then continue end
						self.xufaUse = d
						sgs.ai_skill_choice.xufa = c:objectName()
						return sgs.Card_Parse("@XufaCard="..table.concat(toids,"+"))
					end
				end
				toids = {}
			end
		end
	end
end

sgs.ai_skill_use_func["XufaCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.XufaCard = 4.4
sgs.ai_use_priority.XufaCard = 11.8

sgs.ai_skill_use["@@xufa"] = function(self,prompt,method)
    local d = self.xufaUse
   	if d.card then
		if d.card:canRecast() and d.to:length()<1
		and method~=sgs.Card_MethodRecast
		then return end
      	local tos = {}
       	for _,p in sgs.list(d.to)do
       		table.insert(tos,p:objectName())
       	end
       	return d.card:toString().."->"..table.concat(tos,"+")
    end
end








