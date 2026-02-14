sgs.ai_skill_playerchosen.huituo = function(self,targets)
    self:sort(self.friends)
	for _,p in sgs.list(self.friends)do
		if p:isWounded() then
			return p
		end
	end
	for _,p in sgs.list(self.friends)do
		if self:canDraw(p) then
			return p
		end
	end
    return self.friends[1]
end

sgs.ai_playerchosen_intention.huituo = -80

sgs.ai_skill_playerchosen.mingjian = function(self,targets)
    if (sgs.ai_skill_invoke.fangquan(self) or self:needKongcheng(self.player)) then
        local cards = sgs.QList2Table(self.player:getHandcards())
        self:sortByKeepValue(cards)
        if sgs.playerRoles.rebel==0 then
            local lord = self.room:getLord()
            if lord and self:isFriend(lord) and lord:objectName()~=self.player:objectName() then
                return lord
            end
        end

        local AssistTarget = self:AssistTarget()
        if AssistTarget and not self:willSkipPlayPhase(AssistTarget) then
            return AssistTarget
        end

        self:sort(self.friends_noself,"chaofeng")
        return self.friends_noself[1]
    end
    return nil
end

sgs.ai_playerchosen_intention.mingjian = -80

addAiSkills("newmingjian").getTurnUseCard = function(self)
	return sgs.Card_Parse("@NewMingjianCard=.:")
end

sgs.ai_skill_use_func["NewMingjianCard"] = function(card,use,self)
	self:sort(self.friends_noself)
	for _,p in sgs.list(self.friends_noself)do
		if self:isWeak(p) then
			use.card = card
			use.to:append(p)
			return
		end
	end
	self:sort(self.friends_noself,nil,true)
	for _,p in sgs.list(self.friends_noself)do
		if not self:isWeak(p) then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.NewMingjianCard = 2.4
sgs.ai_use_priority.NewMingjianCard = -2.4


sgs.ai_skill_invoke.xingshuai = sgs.ai_skill_invoke.niepan

sgs.ai_skill_invoke._xingshuai = function(self)
    local lord = self.room:getCurrentDyingPlayer()
    local save = false
    if lord and lord:getLostHp()>0 then
        if self:isFriend(lord) then
            save = true
        elseif self.role=="renegade" and lord:isLord() and self.room:alivePlayerCount()>2 then
            if lord:getHp()<=0 then
                save = true
            end
        end
    end
    if save then
        local hp_max = lord:getHp()+self:getAllPeachNum(lord)
        local must_save = ( hp_max<=0 )
        local others = self.room:getOtherPlayers(lord)
        others:removeOne(self.player)
        local count = 0
        for _,p in sgs.qlist(others)do
            if p:getKingdom()=="wei" and p:getMark("xingshuai_act")==0 then
                if not self:isEnemy(p,lord) then
                    count = count+1
                end
            end
        end
        local am_safe = false
        if self.player:getHp()>1 then
            am_safe = true
        elseif self.player:hasSkill("buqu") then
            am_safe = true
        elseif self.player:hasSkill("nosbuqu") then
            am_safe = true
        elseif self.player:hasSkill("niepan") and self.player:getMark("@nirvana")>0 then
            am_safe = true
        elseif self.player:hasSkill("fuli") and self.player:getMark("@laoji")>0 then
            am_safe = true
        elseif self.player:hasSkill("zhichi") and self.player:getMark("@late")>0 then
            am_safe = true
        elseif self.player:hasSkill("fenyong") and self.player:getMark("@fenyong")>0 then
            am_safe = true
        elseif self.player:getMark("&dawu")>0 then
            am_safe = true
        elseif self.player:hasSkill("sizhan") then
            am_safe = true
        elseif self:getAllPeachNum()>0 then
            am_safe = true
        end
        if self:hasSkills(sgs.masochism_skill) and am_safe then
            return true
        elseif must_save then
            if am_safe then
                return true
            elseif hp_max+count<=0 then
                return true
            elseif self.role=="renegade" or self.role=="lord" then
                return false
            end
        end
        if am_safe then
            if getBestHp(self.player)>self.player:getHp() then
                return true
            end
        elseif self.player:hasSkill("wuhun") and self:needDeath(self.player) then
            if self.role~="renegade" and self.role~="lord" then
                return true
            end
        end
    end
    return false
end
sgs.ai_choicemade_filter["skillInvoke"]["_xingshuai"] = function(self,player,promptlist)
    if #promptlist=="yes" then
        local lord = self.room:getCurrentDyingPlayer()
        if lord and not self:hasSkills(sgs.masochism_skill,player) then
            sgs.updateIntention(player,lord,-60)
        end
    end
end

sgs.ai_skill_invoke["taoxi"] = function(self,data)
    local to = data:toPlayer()
    if to and self:isEnemy(to) then

        if self.player:hasSkill("lihun") and to:isMale() and not self.player:hasUsed("LihunCard") then
            local callback = lihun_skill.getTurnUseCard
            if type(callback)=="function" then
                local skillcard = callback(self)
                if skillcard then
                    local dummy_use = dummy()
                    self:useSkillCard(skillcard,dummy_use)
                    if dummy_use.card then
                        for _,p in sgs.qlist(dummy_use.to)do
                            if p:objectName()==to:objectName() then
                                return true
                            end
                        end
                    end
                end
            end
        end

        if self.player:hasSkill("dimeng") and not self.player:hasUsed("DimengCard") then
            local callback = dimeng_skill.getTurnUseCard
            if type(callback)=="function" then
                local skillcard = callback(self)
                if skillcard then
                    local dummy_use = dummy()
                    self:useSkillCard(skillcard,dummy_use)
                    if dummy_use.card then
                        for _,p in sgs.qlist(dummy_use.to)do
                            if p:objectName()==to:objectName() then
                                return true
                            end
                        end
                    end
                end
            end
        end

        local dismantlement_count = 0
        if to:hasSkill("noswuyan") or to:getMark("@late")>0 then
            if self.player:hasSkill("yinling") then
                local black = self:getSuitNum("spade|club",true)
                local num = 4-self.player:getPile("brocade"):length()
                dismantlement_count = dismantlement_count+math.max(0,math.min(black,num))
            end
        else
            dismantlement_count = dismantlement_count+self:getCardsNum("Dismantlement")
            if self.player:distanceTo(to)==1 or self:hasSkills("qicai|nosqicai") then
                dismantlement_count = dismantlement_count+self:getCardsNum("Snatch")
            end
        end

        local handcards = to:getHandcards()
        if dismantlement_count>=handcards:length() then
            return true
        end

        local can_use,cant_use = {},{}
        for _,c in sgs.qlist(handcards)do
            if self.player:isCardLimited(c,sgs.Card_MethodUse,false) then
                table.insert(cant_use,c)
            else
                table.insert(can_use,c)
            end
        end

        if #can_use==0 and dismantlement_count==0 then
            return false
        end

        if self:needToLoseHp() then
            return true
        end

        local knowns,unknowns = {},{}
        local flag = string.format("visible_%s_%s",self.player:objectName(),to:objectName())
        for _,c in sgs.qlist(handcards)do
            if c:hasFlag("visible") or c:hasFlag(flag) then
                table.insert(knowns,c)
            else
                table.insert(unknowns,c)
            end
        end

        if #knowns>0 then --Now I begin to lose control...Need more help.
            local can_use_record = {}
            for _,c in ipairs(can_use)do
                can_use_record[c:getId()] = true
            end

            local can_use_count = 0
            local to_can_use_count = 0
            local function can_use_check(user,to_use)
                if to_use:isKindOf("EquipCard") then
                    return not user:isProhibited(user,to_use)
                elseif to_use:isKindOf("BasicCard") then
                    if to_use:isKindOf("Jink") then
                        return false
                    elseif to_use:isKindOf("Peach") then
                        if user:hasFlag("Global_PreventPeach") then
                            return false
                        elseif user:getLostHp()==0 then
                            return false
                        elseif user:isProhibited(user,to_use) then
                            return false
                        end
                        return true
                    elseif to_use:isKindOf("Slash") then
                        if to_use:isAvailable(user) then
                            local others = self.room:getOtherPlayers(user)
                            for _,p in sgs.qlist(others)do
                                if user:canSlash(p,to_use) then
                                    return true
                                end
                            end
                        end
                        return false
                    elseif to_use:isKindOf("Analeptic") then
                        if to_use:isAvailable(user) then
                            return not user:isProhibited(user,to_use)
                        end
                    end
                elseif to_use:isKindOf("TrickCard") then
                    if to_use:isKindOf("Nullification") then
                        return false
                    elseif to_use:isKindOf("DelayedTrick") then
                        if user:containsTrick(to_use:objectName()) then
                            return false
                        elseif user:isProhibited(user,to_use) then
                            return false
                        end
                        return true
                    elseif to_use:isKindOf("Collateral") then
                        local others = self.room:getOtherPlayers(user)
                        local selected = sgs.PlayerList()
                        for _,p in sgs.qlist(others)do
                            if to_use:targetFilter(selected,p,user) then
                                local victims = self.room:getOtherPlayers(p)
                                for _,p2 in sgs.qlist(victims)do
                                    if p:canSlash(p2) then
                                        return true
                                    end
                                end
                            end
                        end
                    elseif to_use:isKindOf("ExNihilo") then
                        return not user:isProhibited(user,to_use)
                    else
                        local others = self.room:getOtherPlayers(user)
                        for _,p in sgs.qlist(others)do
                            if not user:isProhibited(p,to_use) then
                                return true
                            end
                        end
                        if to_use:isKindOf("GlobalEffect") and not user:isProhibited(user,to_use) then
                            return true
                        end
                    end
                end
                return false
            end
            for _,c in ipairs(knowns)do
                if can_use_record[c:getId()] and can_use_check(self.player,c) then
                    can_use_count = can_use_count+1
                end
            end

            local to_friends = self:getFriends(to)
            local to_has_weak_friend = false
            local to_is_weak = self:isWeak(to)
            for _,friend in ipairs(to_friends)do
                if self:isEnemy(friend) and self:isWeak(friend) then
                    to_has_weak_friend = true
                    break
                end
            end

            local my_trick,my_slash,my_aa,my_duel,my_sa = nil,nil,nil,nil,nil
            local use = self.player:getTag("taoxi_carduse"):toCardUse()
            local ucard = use.card
            if ucard:isKindOf("TrickCard") then
                my_trick = 1
                if ucard:isKindOf("Duel") then
                    my_duel = 1
                elseif ucard:isKindOf("ArcheryAttack") then
                    my_aa = 1
                elseif ucard:isKindOf("SavageAssault") then
                    my_sa = 1
                end
            elseif ucard:isKindOf("Slash") then
                my_slash = 1
            end
            
            for _,c in ipairs(knowns)do
                if isCard("Nullification",c,to) then
                    my_trick = my_trick or ( self:getCardsNum("TrickCard")-self:getCardsNum("DelayedTrick") )
                    if my_trick>0 then
                        to_can_use_count = to_can_use_count+1
                        continue
                    end
                end
                if isCard("Jink",c,to) then
                    my_slash = my_slash or self:getCardsNum("Slash")
                    if my_slash>0 then
                        to_can_use_count = to_can_use_count+1
                        continue
                    end
                    my_aa = my_aa or self:getCardsNum("ArcheryAttack")
                    if my_aa>0 then
                        to_can_use_count = to_can_use_count+1
                        continue
                    end
                end
                if isCard("Peach",c,to) then
                    if to_has_weak_friend then
                        to_can_use_count = to_can_use_count+1
                        continue
                    end
                end
                if isCard("Analeptic",c,to) then
                    if to:getHp()<=1 and to_is_weak then
                        to_can_use_count = to_can_use_count+1
                        continue
                    end
                end
                if isCard("Slash",c,to) then
                    my_duel = my_duel or self:getCardsNum("Duel")
                    if my_duel>0 then
                        to_can_use_count = to_can_use_count+1
                        continue
                    end
                    my_sa = my_sa or self:getCardsNum("SavageAssault")
                    if my_sa>0 then
                        to_can_use_count = to_can_use_count+1
                        continue
                    end
                end
            end

            if can_use_count>=to_can_use_count+#unknowns then
                return true
            elseif can_use_count>0 and ( can_use_count+0.01 )/( to_can_use_count+0.01 )>=0.5 then
                return true
            end
        end

        if self:getCardsNum("Peach")>0 then
            return true
        end
    end
    return false
end

local taoxi = {}
taoxi.name = "taoxi"
table.insert(sgs.ai_skills,taoxi)
taoxi.getTurnUseCard = function(self)
    if self.player:hasFlag("TaoxiRecord")
	then
        local c = self.player:getTag("TaoxiId"):toInt()
        if c and c>=0
		then
            c = sgs.Sanguosha:getCard(c)
			if c:isAvailable(self.player)
			then return c end
        end
    end
end

sgs.ai_cardsview_valuable.taoxi = function(self,class_name,player)
    local id = player:getTag("TaoxiId"):toInt()
     if id and id>=0
	then
		if sgs.Sanguosha:getCard(id):isKindOf(class_name)
		then return tostring(id) end
    end
end

huaiyi_skill = {
    name = "huaiyi",
    getTurnUseCard = function(self,inclusive)
        if self.player:isKongcheng() then return nil end
        local handcards = self.player:getHandcards()
        local red,black = false,false
        for _,c in sgs.qlist(handcards)do
            if c:isRed() and not red then
                red = true
                if black then
                    break
                end
            elseif c:isBlack() and not black then
                black = true
                if red then
                    break
                end
            end
        end
        if red and black then
            return sgs.Card_Parse("@HuaiyiCard=.")
        end
    end,
}
table.insert(sgs.ai_skills,huaiyi_skill)
sgs.ai_skill_use_func["HuaiyiCard"] = function(card,use,self)
    local handcards = self.player:getHandcards()
    local reds,blacks = {},{}
    for _,c in sgs.qlist(handcards)do
        local dummy_use = dummy()
        if c:isKindOf("BasicCard") then
            self:useBasicCard(c,dummy_use)
        elseif c:isKindOf("EquipCard") then
            self:useEquipCard(c,dummy_use)
        elseif c:isKindOf("TrickCard") then
            self:useTrickCard(c,dummy_use)
        end
        if dummy_use.card then
            return --It seems that self.player should use this card first.
        end
        if c:isRed() then
            table.insert(reds,c)
        else
            table.insert(blacks,c)
        end
    end
    
    local targets = self:findPlayerToDiscard("he",false,false)
    local n_reds,n_blacks,n_targets = #reds,#blacks,#targets
    if n_targets==0 then
        return 
    elseif n_reds-n_targets>=2 and n_blacks-n_targets>=2 and handcards:length()-n_targets>=5 then
        return 
    end
    --[[------------------
        Haven't finished.
    ]]--------------------
    use.card = card
end

sgs.ai_skill_choice["huaiyi"] = function(self,choices,data)
    local choice = sgs.huaiyi_choice
    if choice then
        sgs.huaiyi_choice = nil
        return choice
    end
    local handcards = self.player:getHandcards()
    local reds,blacks = {},{}
    for _,c in sgs.qlist(handcards)do
        if c:isRed() then
            table.insert(reds,c)
        else
            table.insert(blacks,c)
        end
    end
    if #reds<#blacks then
        return "red"
    else
        return "black"
    end
end

sgs.ai_skill_use["@@huaiyi"] = function(self,prompt,method)
    local n = self.player:getMark("huaiyi_num")
    if n>=2 then
        if self:needToLoseHp() then
        elseif self:isWeak() then
            n = 1
        end
    end
    if n==0 then
        return "."
    end
    local targets = self:findPlayerToDiscard("he",false,false)
    local names = {}
    for index,target in ipairs(targets)do
        if index<=n then
            table.insert(names,target:objectName())
        else
            break
        end
    end
    return "@HuaiyiSnatchCard=.->"..table.concat(names,"+")
end

sgs.ai_skill_invoke.jigong = function(self)
    if self.player:isKongcheng() then return true end
    for _,c in sgs.qlist(self.player:getHandcards())do
        local x = nil
        if isCard("ArcheryAttack",c,self.player) then
            x = dummyCard("ArcheryAttack")
        elseif isCard("SavageAssault",c,self.player) then
            x = dummyCard("SavageAssault")
        else continue end

        local du = dummy()
        self:useTrickCard(x,du)
        if (du.card) then return true end
    end

    return false
end

sgs.ai_skill_invoke.shifei = function(self)
    local sf = {}
    for _,p in sgs.qlist(self.room:getAlivePlayers())do
        sf[p:objectName()] = p:getHandcardNum()
        if p==self.room:getCurrent() then
            sf[p:objectName()] = p:getHandcardNum()+1
        end
    end

    local most = {}
    for k,t in pairs(sf)do
        if #most==0 then
            table.insert(most,k)
            continue
        end

        if t>sf[most[1]] then
            most = {}
        end

        table.insert(most,k)
    end

    if table.contains(most,self.room:getCurrent():objectName()) then
        return table.contains(self.friends,self.room:getCurrent(),true)
    end

    for _,p in ipairs(most)do
        if table.contains(self.enemies,BeMan(self.room,p)) then return true end
    end
    return false
end

sgs.ai_skill_playerchosen.shifei = function(self,targets)
    targets = sgs.QList2Table(targets)
    for _,target in ipairs(targets)do
        if self:isEnemy(target) and target:isAlive() then
            return target
        end
    end
end


zhanjue_skill = {name = "zhanjue"}
table.insert(sgs.ai_skills,zhanjue_skill)
zhanjue_skill.getTurnUseCard = function(self)
    if self.player:getHandcardNum()>self.player:getHp()
	or self.player:isKongcheng() or #self.toUse>1 then return end
	local duel = dummyCard("duel")
    duel:addSubcards(self.player:getHandcards())
    duel:setSkillName("zhanjue")
	return duel
end

sgs.ai_use_revises.zhanjue = function(self,card,use)
	if card:getSkillName()=="zhanjue" then
		for _,ep in sgs.list(self.enemies)do
			if CanToCard(card,self.player,ep,use.to)
			and getCardsNum("Slash",ep,self.player)<1 then
				use.card = card
				use.to:append(ep)
			end
		end
		return use.card==card
	end
end

addAiSkills("qinwang").getTurnUseCard = function(self)
	local can
    for _,target in sgs.list(self.room:getLieges("shu",self.player))do
		if self:isFriend(target)
		and target:getHandcardNum()>2
		then can = true break end
	end
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards)
    if self.player:getMark("qinwang-PlayClear")>0
	or not self.player:hasLordSkill("qinwang")
	or self.player:hasFlag("qinwangjijiang")
	or #cards<2 or not can then return end
   	local poi = dummyCard()
   	local dummy = self:aiUseCard(poi)
 	if poi:isAvailable(self.player)
	and dummy.card and dummy.to:length()>0
  	then
       	local parse = sgs.Card_Parse("@QinwangCard="..cards[1]:getEffectiveId()..":slash")
      	assert(parse)
       	return parse
	end
end

sgs.ai_skill_use_func["QinwangCard"] = function(card,use,self)
   	local poi = dummyCard()
   	local dummy = self:aiUseCard(poi)
	if dummy.card then use.card = card end
	if dummy.to then use.to = dummy.to end
end

sgs.ai_use_value.QinwangCard = 4
sgs.ai_use_priority.QinwangCard = 2.2

sgs.ai_guhuo_card.qinwang = function(self,toname,class_name)
    if self.player:hasFlag("qinwangjijiang")
	or class_name~="Slash" then return end
	local can
    for _,target in sgs.list(self.room:getLieges("shu",self.player))do
		if self:isFriend(target)
		then can = getCardsNum(class_name,target,self.player)>0 end
	end
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
	if can and #cards>0 and self:getCardsNum(class_name)<1 and self.player:hasLordSkill("qinwang")
	then return "@QinwangCard="..cards[1]:getEffectiveId()..":"..toname end
end

olzhanjue_skill = {name = "olzhanjue"}
table.insert(sgs.ai_skills,olzhanjue_skill)
olzhanjue_skill.getTurnUseCard = function(self)
    if self.player:getHandcardNum()>self.player:getHp()
	or self.player:isKongcheng() then return end
	local duel = dummyCard("duel")
    duel:addSubcards(self.player:getHandcards())
    duel:setSkillName("olzhanjue")
	return duel
end

sgs.ai_use_revises.olzhanjue = function(self,card,use)
	if card:getSkillName()=="olzhanjue"
	then
		for _,ep in sgs.list(self.enemies)do
			if CanToCard(card,self.player,ep,use.to)
			and getCardsNum("Slash",ep,self.player)<1
			then
				use.card = card
				use.to:append(ep)
			end
		end
		return use.card==card
	end
end

sgs.ai_skill_playerchosen.zhenshan = function(self,players)
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
	self:sort(destlist,"handcard",true)
	return destlist[1]
end

sgs.ai_guhuo_card.zhenshan = function(self,toname,class_name)
	local can
   	local poi = dummyCard(toname)
	if not poi then return end
  	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if not self:isEnemy(p)
		and p:getHandcardNum()<self.player:getHandcardNum()
		then can = true break end
	end
    if can and poi:isKindOf("BasicCard")
	and self:getCardsNum(class_name)<1
	and not self.player:hasFlag("ZhenshanUsed")
    then
        return "@ZhenshanCard=.:"..toname
	end
end

addAiSkills("zhenshan").getTurnUseCard = function(self)
	local can
  	for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
		if not self:isEnemy(p)
		and p:getHandcardNum()<self.player:getHandcardNum()
		then can = true break end
	end
	if not can
	then return end
   	for _,name in sgs.list(patterns())do
   		local poi = dummyCard(name)
		if not poi then continue end
	   	if poi:isAvailable(self.player)
	   	and poi:isKindOf("BasicCard")
		and self:getCardsNum(poi:getClassName())<1
	   	then
         	local dummy = self:aiUseCard(poi)
	       	if dummy.card
           	and dummy.to
	       	then
				self.zhenshan_to = dummy.to
				local parse = sgs.Card_Parse("@ZhenshanCard=.:"..name)
                assert(parse)
                return parse
	    	end
		end
	end
end

sgs.ai_skill_use_func["ZhenshanCard"] = function(card,use,self)
	use.card = card
   	use.to = self.zhenshan_to
end

sgs.ai_use_value.ZhenshanCard = 2.4
sgs.ai_use_priority.ZhenshanCard = 2.4


yanzhu_skill = {name = "yanzhu"}
table.insert(sgs.ai_skills,yanzhu_skill)
yanzhu_skill.getTurnUseCard = function(self)
    return sgs.Card_Parse("@YanzhuCard=.")
end


sgs.ai_skill_use_func.YanzhuCard = function(card,use,self)

    self:sort(self.enemies,"threat")
    for _,p in ipairs(self.enemies)do
        if not p:isNude() then
            use.card = card
            if (use.to) then use.to:append(p) end
            -- use.from = self.player
            return
        end
    end

    for _,p in ipairs(self.friends_noself)do
        if self.needToThrowArmor(p) and p:getArmor() and not p:isJilei(p:getArmor()) then
            use.card = card
            if (use.to) then use.to:append(p) end
            -- use.from = self.player
            return
        end
    end

    return nil
end

sgs.ai_use_priority.YanzhuCard = sgs.ai_use_priority.Dismantlement-0.1

sgs.ai_skill_discard.yanzhu = function(self,_,__,optional)
    if not optional then return self:askForDiscard("dummyreason",1,1,false,true) end

    if self:needToThrowArmor() and self.player:getArmor() and not self.player:isJilei(self.player:getArmor()) then return self.player:getArmor():getEffectiveId() end

    if self.player:getTreasure() then
        if (self.player:getCardCount()==1) then return self.player:getTreasure():getEffectiveId()
        elseif not self.player:isKongcheng() then return self:askForDiscard("dummyreason",1,1,false,false) end
    end

    if self.player:getEquips():length()>2 and not self.player:isKongcheng() then return self:askForDiscard("dummyreason",1,1,false,false) end

    if self.player:getEquips():length()==1 then return {} end

     return self:askForDiscard("dummyreason",1,1,false,true)
end

sgs.ai_skill_use["@@xingxue"] = function(self)

    local n = (self.player:getMark("yanzhu_lost")==0) and self.player:getHp() or self.player:getMaxHp()
	n = math.min(n,#self.friends)
	if n<=0 then return "." end
    
	self:sort(self.friends,"defense",true)

    local l = sgs.SPlayerList()
    local s = {}
    for i = 1,n,1 do
        l:append(self.friends[i])
        table.insert(s,self.friends[i]:objectName())
    end

    if #s==0 then return "." end

    sgs.xingxuelist = l

    return "@XingxueCard=.->"..(table.concat(s,"+"))
end

-- 兴学剩下的部分大家想象吧，我懒得想
-- sgs.ai_skill_discard.xingxue = function(self) end

sgs.ai_skill_invoke.qiaoshi = true

yanyu_skill = {name = "yjyanyu"}
table.insert(sgs.ai_skills,yanyu_skill)
yanyu_skill.getTurnUseCard = function(self)
    return sgs.Card_Parse("@YjYanyuCard=.")
end

sgs.ai_skill_use_func.YjYanyuCard = function(card,use,self)
    local n = self.player:getMark("yjyanyu")

    if n>=2 then return nil end

    local ns,fs,ts = {},{},{}
    for _,c in sgs.qlist(self.player:getHandcards())do
        if c:isKindOf("Slash") then
            n = n+1
            if (c:isKindOf("FireSlash")) then
                table.insert(fs,c)
            elseif (c:isKindOf("ThunderSlash")) then
                table.insert(ts,c)
            else
                table.insert(ns,c)
            end
        end
    end

    if n<2 then return nil end

    local hasmale = false
    for _,p in ipairs(self.friends_noself)do
        if p:isMale() then
            hasmale = true
            break
        end
    end

    if not hasmale then return nil end

    local id = -1
    if #ns>0 then
        id = ns[1]:getEffectiveId()
    elseif #ts>0 then
        id = ts[1]:getEffectiveId()
    elseif #fs>0 then
        id = fs[1]:getEffectiveId()
    end

    if id<0 then return nil end

    use.card = sgs.Card_Parse("@YjYanyuCard="..id)
end

sgs.ai_skill_playerchosen.yjyanyu = function(self)

    self:sort(self.friends_noself,"handcard")

    for i = #self.friends_noself,1,-1 do
        if self.friends_noself[i]:isMale() then
            return self.friends_noself[i]
        end
    end

    return nil
end

sgs.ai_use_priority.YjYanyuCard = sgs.ai_use_priority.Slash-0.01

function getKnownHandcards(player,target)    
    local knowns,unknowns = getKnownCards(target,player),{}
    for _,card in sgs.qlist(target:getHandcards())do
        if not table.contains(knowns,card) then
            table.insert(unknowns,card)
        end
    end
    return knowns,unknowns
end

wurong_skill = {name = "wurong"}
table.insert(sgs.ai_skills,wurong_skill)
wurong_skill.getTurnUseCard = function(self)
    if self.player:isKongcheng() then
        return nil
    end
    return sgs.Card_Parse("@WurongCard=.")
end

sgs.ai_skill_use_func.WurongCard = function(card,use,self)
    local handcards = self.player:getHandcards()
    local my_slashes,my_cards = {},{}
    for _,c in sgs.qlist(handcards)do
        if c:isKindOf("Slash") then
            table.insert(my_slashes,c)
        else
            table.insert(my_cards,c)
        end
    end
    local no_slash = ( #my_slashes==0 ) 
    local all_slash = ( #my_cards==0 ) 
    local need_slash,target = nil,nil
    
    --自己展示的一定不是【杀】，目标展示的必须是【闪】，方可获得目标一张牌
    if no_slash then
        local others = self.room:getOtherPlayers(self.player)
        local targets = self:findPlayerToDiscard("he",false,false,others)
        for _,p in ipairs(targets)do
            if not p:isKongcheng() then
                local knowns,unknowns = getKnownHandcards(self.player,p)
                if #unknowns==0 then
                    local all_jink = true
                    for _,jink in ipairs(knowns)do
                        if not jink:isKindOf("Jink") then
                            all_jink = false
                            break
                        end
                    end
                    if all_jink then
                        need_slash,target = false,p
                        break
                    end
                end
            end
        end
    end
    
    --自己展示的一定是【杀】，目标展示的不是【闪】时，可对目标造成1点伤害
    if all_slash and not target then
        local targets = self:findPlayerToDamage(1,self.player,nil,nil,false,5)
        for _,p in ipairs(targets)do
            if not p:isKongcheng() then
                local knowns,unknowns = getKnownHandcards(self.player,p)
                if self:isFriend(p) then
                    local all_jink = true
                    if #unknowns==0 then
                        for _,c in ipairs(knowns)do
                            if not c:isKindOf("Jink") then
                                all_jink = false
                                break
                            end
                        end
                    else
                        all_jink = false --队友会配合不展示【闪】的
                    end
                    if not all_jink then
                        need_slash,target = true,p
                        break
                    end
                else
                    local all_jink = false
                    if #unknowns==0 then
                        for _,c in ipairs(knowns)do
                            if c:isKindOf("Jink") then
                                all_jink = true
                                break
                            end
                        end
                    end
                    if not all_jink then
                        need_slash,target = true,p
                        break
                    end
                end
            end
        end
    end
    
    --自己展示的不一定是【杀】，可根据目标情况决定展示的牌
    if not target then
        local friends,enemies,others = {},{},{}
        for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
            if not p:isKongcheng() then
                if self:isFriend(p) then
                    table.insert(friends,p)
                elseif self:isEnemy(p) then
                    table.insert(enemies,p)
                else
                    table.insert(others,p)
                end
            end
        end
        
        local to_damage = self:findPlayerToDamage(1,self.player,nil,enemies,false,5)
        for _,enemy in ipairs(to_damage)do
            local knowns,unknowns = getKnownHandcards(self.player,enemy)
            local no_jink = true
            if #unknowns==0 then
                for _,jink in ipairs(knowns)do
                    if jink:isKindOf("Jink") then
                        no_jink = false
                        break
                    end
                end
            else
                no_jink = false
            end
            if no_jink then
                need_slash,target = true,enemy
                break
            end
        end
        
        if not target then
            local other_players = self.room:getOtherPlayers(self.player)
            local to_obtain = self:findPlayerToDiscard("he",false,false,other_players)
            for _,p in ipairs(to_obtain)do
                if not p:isKongcheng() then
                    local knowns,unknowns = getKnownHandcards(self.player,p)
                    if self:isFriend(p) then
                        local has_jink = false
                        for _,jink in ipairs(knowns)do
                            if jink:isKindOf("Jink") then
                                has_jink = true
                                break
                            end
                        end
                        if has_jink then
                            need_slash,target = false,p
                            break
                        end
                    else
                        local all_jink = true
                        if #unknowns==0 then
                            for _,c in ipairs(knowns)do
                                if not c:isKindOf("Jink") then
                                    all_jink = false
                                    break
                                end
                            end
                        else
                            all_jink = false
                        end
                        if all_jink then
                            need_slash,target = false,p
                            break
                        end
                    end
                end
            end
        end
        
        if not target then
            to_damage = self:findPlayerToDamage(1,self.player,nil,friends,false,25)
            for _,friend in ipairs(to_damage)do
                local knowns,unknowns = getKnownHandcards(self.player,friend)
                local all_jink = true
                for _,c in ipairs(knowns)do
                    if not c:isKindOf("Jink") then
                        all_jink = false
                        break
                    end
                end
                if not all_jink then
                    need_slash,target = true,friend
                    break
                end
            end
        end
        
        if not target then
            local victim = self:findPlayerToDamage(1,self.player,nil,others,false,5)[1]
            if victim then
                need_slash,target = true,victim
            end
        end
        
        --只是为了看牌……
        if not target and #my_cards>0 and self:getOverflow()>0 then
            if #enemies>0 then
                self:sort(enemies,"handcard")
                need_slash,target = false,enemies[1]
            elseif #others>0 then
                self:sort(others,"threat")
                need_slash,target = false,others[1]
            end
        end
        
        if not target and #enemies>0 then
            self:sort(enemies,"defense",true)
            need_slash,target = (math.random(0,1)==0),enemies[1]
        end
    end
    
    if target and not target:isKongcheng() and target~=self.player then
        local use_cards = need_slash and my_slashes or my_cards
        if #use_cards>0 then
            self:sortByUseValue(use_cards,true)
            use.card = sgs.Card_Parse("@WurongCard="..use_cards[1]:getEffectiveId())
            use.to:append(target)
        end
    end
end
sgs.ai_use_priority["WurongCard"] = 3.1
sgs.ai_use_value["WurongCard"] = 4.5

-- sgs.ai_skill_discard.wurong = function(self) end

sgs.ai_guhuo_card.huomo = function(self,toname,class_name)
    local cards = self.player:getCards("he")
    cards = self:sortByKeepValue(cards) -- 按保留值排序
   	for d,c in sgs.list(cards)do
       	d = dummyCard(toname)
        if c:isBlack() and c:getTypeId()~=1
		and self.player:getMark("Huomo_"..d:getClassName())<1
       	and d:isKindOf("BasicCard") and self:getCardsNum(class_name)<1
		and sgs.Sanguosha:getCurrentCardUseReason()==sgs.CardUseStruct_CARD_USE_REASON_RESPONSE_USE
      	then return "@HuomoCard="..c:getEffectiveId()..":"..toname end
   	end
end

huomo_skill = {name = "huomo"}
table.insert(sgs.ai_skills,huomo_skill)
huomo_skill.getTurnUseCard = function(self)
    local handcards = self.player:getCards("he")
	handcards = self:sortByKeepValue(handcards)
    for _,h in sgs.list(handcards)do
        if h:isBlack() and h:getTypeId()~=1
		then
			for dc,pn in sgs.list(patterns())do
				dc = PatternsCard(pn)
				if dc and dc:isKindOf("BasicCard")
				and self:getCardsNum(dc:getClassName())<1
				then
					local cn = dc:getClassName()
					if dc:isKindOf("Slash") then cn = "Slash" end
					if self.player:getMark("Huomo_"..cn)>0
					then continue end
					cn = dummyCard(pn)
					if cn and cn:isAvailable(self.player)
					then
						cn:setSkillName("huomo")
						dc = self:aiUseCard(cn)
						if dc.card and dc.to
						then
							self.huomo_to = dc.to
							sgs.ai_use_priority["HuomoCard"] = sgs.ai_use_priority[dc.card:getClassName()]-0.3
							return sgs.Card_Parse("@HuomoCard="..h:getEffectiveId()..":"..pn)
						end
					end
				end
			end
        end
    end
end

sgs.ai_skill_use_func.HuomoCard = function(card,use,self) 
   if self.huomo_to
	then
		use.card = card
		use.to = self.huomo_to
    end
end

sgs.ai_use_value["HuomoCard"] = 2.7
sgs.ai_use_priority["HuomoCard"] = 3.5

sgs.ai_skill_playerchosen.zuoding = function(self,targets)
    local l = {}
    for _,p in sgs.qlist(targets)do
        if table.contains(self.friends,p,true) then
            table.insert(l,p)
        end
    end

    if #l==0 then return nil end

    self:sort(l,"defense")
    return l[#l]
end

anguo_skill = {name = "anguo"}
table.insert(sgs.ai_skills,anguo_skill)
anguo_skill.getTurnUseCard = function(self)
    return sgs.Card_Parse("@AnguoCard=.")
end

sgs.ai_skill_use_func.AnguoCard = function(card,use,self)
    local l = {}
    function calculateMinus(player,range)
        if range<=0 then return 0 end
        local n = 0
        for _,p in sgs.qlist(self.room:getAlivePlayers())do
            if player:inMyAttackRange(p) and not player:inMyAttackRange(p,range) then n = n+1 end
        end
        return n
    end

    function filluse(to,id)
        use.card = card
        if (use.to) then use.to:append(to) end
        self.anguoid = id
    end

    for _,p in ipairs(self.enemies)do
        if (p:getWeapon()) then
            local weaponrange = p:getWeapon():getRealCard():toWeapon():getRange()
            local n = calculateMinus(p,weaponrange-1)
            table.insert(l,{player = p,id = p:getWeapon():getEffectiveId(),minus = n})
        end
        if (p:getOffensiveHorse()) then
            local n = calculateMinus(p,1)
            table.insert(l,{player = p,id = p:getOffensiveHorse():getEffectiveId(),minus = n})
        end
    end

    if #l>0 then
        function sortByMinus(a,b)
            return a.minus>b.minus
        end

        table.sort(l,sortByMinus)
        if l[1].minus>0 then
            filluse(l[1].player,l[1].id)
            return
        end
    end

    for _,p in ipairs(self.enemies)do
        if (p:getTreasure()) then
            filluse(p,p:getTreasure():getEffectiveId())
            return
        end
    end

    for _,p in ipairs(self.friends_noself)do
        if (self:needToThrowArmor(p) and p:getArmor()) then
            filluse(p,p:getArmor():getEffectiveId())
            return
        end
    end

    self:sort(self.enemies,"threat")
    for _,p in ipairs(self.enemies)do
        if (p:getArmor() and not p:getArmor():isKindOf("GaleShell")) then
            filluse(p,p:getArmor():getEffectiveId())
            return
        end
    end

    for _,p in ipairs(self.enemies)do
        if (p:getDefensiveHorse()) then
            filluse(p,p:getDefensiveHorse():getEffectiveId())
            return
        end
    end
end

sgs.ai_use_priority.AnguoCard = sgs.ai_use_priority.ExNihilo+0.01

sgs.ai_skill_cardchosen.anguo = function(self)
    return self.anguoid
end

--OL诏缚
function SmartAI:zhaofuSort()
	local func = function(a,b)
		local a1,b1 = 0,0
		for _,p in ipairs(self.friends)do
			if not p:inMyAttackRange(a) and a:getKingdom()=="wu" then --and canSlash
				a1 = a1+1
			end
		end
		for _,p in ipairs(self.friends)do
			if not p:inMyAttackRange(b) and b:getKingdom()=="wu" then
				b1 = b1+1
			end
		end
		if a1==b1 then
			if self:getDefenseSlash(a)>self:getDefenseSlash(b) then
				a1 = a1-1
			end
		end
		return a1>b1
	end
	table.sort(self.enemies,func)
end

local olzhaofu_skill = {}
olzhaofu_skill.name = "olzhaofu"
table.insert(sgs.ai_skills,olzhaofu_skill)
olzhaofu_skill.getTurnUseCard = function(self)
	if not self.player:hasLordSkill("olzhaofu") or self.player:getMark("@olzhaofuMark")<=0 then return end
	if #self.enemies<=0 then return end
	return sgs.Card_Parse("@OLzhaofuCard=.")
end

sgs.ai_skill_use_func.OLzhaofuCard = function(card,use,self)
	self:zhaofuSort()
	
	local targets = {}
	for _,e in ipairs(self.enemies)do
		if #targets>=2 then break end
		local to
		for _,f in ipairs(self.friends)do
			if not f:inMyAttackRange(e) and f:getKingdom()=="wu" then
				to = e
				break
			end
		end
		if to then
			table.insert(targets,to)
		end
	end
	
	if #targets<=0 then return end
	
	use.card = card
	
	for i = 1,2 do
		if #targets<i then break end
		use.to:append(targets[i])
	end
end

sgs.ai_card_intention.OLzhaofuCard = 80

sgs.ai_use_priority.OLzhaofuCard = 10
sgs.ai_use_value.OLzhaofuCard = 3

sgs.ai_skill_playerchosen.yaoming = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"card")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()<self.player:getHandcardNum()
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and target:getHandcardNum()>self.player:getHandcardNum()
		then return target end
	end
end

