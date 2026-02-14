--才瑕

sgs.ai_skill_invoke.ny_10th_caixia = true

sgs.ai_skill_choice["ny_10th_caixia"] = function(self, choices, data)
	local items = choices:split("+")
    return items[#items]
end

--赏誉

sgs.ai_skill_playerchosen.ny_10th_shangyu = function(self,targets)

    local min = 9999
    local result = nil
    for _,target in sgs.qlist(targets) do
        if self:isFriend(target) and target:getHandcardNum() < min then
            result = target
            min = target:getHandcardNum()
        end
    end
    if result then return result end
    for _,target in sgs.qlist(targets) do
        if target == self.player then return target end
    end
    local max = 0
    for _,target in sgs.qlist(targets) do
        if not self:isFriend(target) and target:getHandcardNum() > max then
            result = target
            max = target:getHandcardNum()
        end
    end
    if result then return result end
    local all = sgs.QList2Table(targets)
    return all[math.random(1,#all)]
end

sgs.ai_skill_cardask["@ny_tenth_pingliao"] = function(self,data,pattern)
	local use = data:toCardUse()
	if self:isFriend(use.from) then return "." end
	if self:isGoodTarget(self.player,self:getEnemies(use.from),use.from)
	and math.random()>0.4
	then return true end
    return "."
end

--贤淑

local ny_10th_xianshu_skill = {}
ny_10th_xianshu_skill.name = "ny_10th_xianshu"
table.insert(sgs.ai_skills, ny_10th_xianshu_skill)
ny_10th_xianshu_skill.getTurnUseCard = function(self, inclusive)
	for _,card in sgs.qlist(self.player:getHandcards()) do
        if card:hasFlag("ny_10th_konghou") then
	        return sgs.Card_Parse("#ny_10th_xianshu:.:")
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_xianshu"] = function(card, use, self)
	self:updatePlayers()
	
    local red = {}
    local black = {}
    local cards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByKeepValue(cards)
    for _,card in ipairs(cards) do
        if card:hasFlag("ny_10th_konghou") then
	        if card:isRed() then table.insert(red, card:getEffectiveId()) end
            if card:isBlack() then table.insert(black, card:getEffectiveId()) end
        end
    end

    local enduse = false
    if #red > 0 then
        self:sort(self.friends, "hp")
        for _,friend in ipairs(self.friends) do
            if friend:objectName() ~= self.player:objectName() 
            and friend:getHp() <= self.player:getHp() and friend:isWounded() then
                local card_str = string.format("#ny_10th_xianshu:%s:->%s", red[1], friend:objectName())
		        local acard = sgs.Card_Parse(card_str)
		        assert(acard)
		        use.card = acard
		        if use.to then
			        use.to:append(friend)
		        end
                enduse = true
                break
            end
        end
    end
    if #black > 0 and (not enduse) then
        self:sort(self.enemies, "hp")
        for _,enemy in ipairs(self.enemies) do
            if enemy:objectName() ~= self.player:objectName() 
            and enemy:getHp() >= self.player:getHp() then
                local card_str = string.format("#ny_10th_xianshu:%s:->%s", black[1], enemy:objectName())
		        local acard = sgs.Card_Parse(card_str)
		        assert(acard)
		        use.card = acard
		        if use.to then
			        use.to:append(enemy)
		        end
                break
            end
        end
    end
end

sgs.ai_use_priority.ny_10th_xianshu = 10

sgs.ai_card_intention.ny_10th_xianshu = function(self, card, from, tos)
	for _,to in ipairs(tos) do
        if to:getHp() > from:getHp() then
		    sgs.updateIntention(from, to, 80)
        elseif to:getHp() < from:getHp() then
            sgs.updateIntention(from, to, -80)
        else
            if card:isRed() then
                sgs.updateIntention(from, to, -80)
            elseif card:isBlack() then
                sgs.updateIntention(from, to, 80)
            end
        end
	end
end

--生妒

sgs.ai_skill_playerchosen.ny_10th_shengdu = function(self,targets)
    local nextp = self.player:getNextAlive()
    while(nextp:objectName()~= self.player:objectName()) do
        if self:isFriend(nextp) and nextp:getMark("ny_10th_shengdu_from_"..self.player:objectName()) == 0 then
            return nextp
        else
            nextp = nextp:getNextAlive()
        end
    end
    nextp = self.player:getNextAlive()
    while(nextp:objectName()~= self.player:objectName()) do
        if nextp:getMark("ny_10th_shengdu_from_"..self.player:objectName()) == 0 then
            return nextp
        else
            nextp = nextp:getNextAlive()
        end
    end
    local all = sgs.QList2Table(targets)
    return all[math.random(1,#all)]
end

--介绫

local ny_10th_jieling_skill = {}
ny_10th_jieling_skill.name = "ny_10th_jieling"
table.insert(sgs.ai_skills, ny_10th_jieling_skill)
ny_10th_jieling_skill.getTurnUseCard = function(self, inclusive)
	if not self.player:hasUsed("#ny_10th_jieling") then
		return sgs.Card_Parse("#ny_10th_jieling:.:")
	end
end

sgs.ai_skill_use_func["#ny_10th_jieling"] = function(card, use, self)
	self:updatePlayers()
	local red = {}
    local black = {}
    local cards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByKeepValue(cards)
    for _,card in ipairs(cards) do
	    if card:isRed() then table.insert(red, card) end
        if card:isBlack() then table.insert(black, card) end
    end
 
    if #red > 0 and #black > 0 then
        local use_cards = {red[1]:getEffectiveId(),black[1]:getEffectiveId()}
        local slash = sgs.Sanguosha:cloneCard("slash", sgs.Card_SuitToBeDecided, -1)
        slash:setSkillName("ny_10th_jieling")
        slash:addSubcard(red[1])
        slash:addSubcard(black[1])

        --local usec = self:aiUseCard(slash)
        local usec = {isDummy=true,to=sgs.SPlayerList()}
        self:useCardByClassName(slash, usec)
        if usec.card then
            if usec.to and usec.to:length() > 0 then
                local tos = {}
                for _,p in sgs.qlist(usec.to) do
                    table.insert(tos, p:objectName())
                end

				local card_str = string.format("#ny_10th_jieling:%s:->%s", table.concat(use_cards,"+"), table.concat(tos,"+"))
		        local acard = sgs.Card_Parse(card_str)
		        assert(acard)
		        use.card = acard
		        if use.to then
			        use.to = usec.to
		        end
            end
        end
    end
end

--驰应

local ny_10th_chiying_skill = {}
ny_10th_chiying_skill.name = "ny_10th_chiying"
table.insert(sgs.ai_skills, ny_10th_chiying_skill)
ny_10th_chiying_skill.getTurnUseCard = function(self, inclusive)
	if not self.player:hasUsed("#ny_10th_chiying") then
		return sgs.Card_Parse("#ny_10th_chiying:.:")
	end
end

sgs.ai_skill_use_func["#ny_10th_chiying"] = function(card, use, self)
	self:updatePlayers()
	local room = self.room
    local can_dis = 0
    local target = nil

    for _,friend in ipairs(self.friends) do
        if friend:getHp() <= self.player:getHp() then
            local dis = 0
            for _,other in sgs.qlist(room:getOtherPlayers(friend)) do
                if friend:inMyAttackRange(other) and other:objectName() ~= self.player:objectName() then
                    if not self:isFriend(other) then
                        if friend:objectName() == self.player:objectName() then
                            dis = dis + 1
                        else
                            dis = dis + 1.5
                        end
                    end
                end
            end
            if dis > can_dis then
                target = friend
                can_dis = dis
            end
        end
    end

	if not target then return end
	if target then
		local card_str = "#ny_10th_chiying:.:->"..target:objectName()
		local acard = sgs.Card_Parse(card_str)
		assert(acard)
		use.card = acard
		if use.to then
			use.to:append(target)
		end
	end
end

sgs.ai_use_priority.ny_10th_chiying = 6

--长驱

sgs.ai_skill_discard.ny_10th_changqu = function(self,max,min)
    local friend = false
    for _,p in sgs.qlist(self.room:getAlivePlayers()) do
        if p:getPhase() == sgs.Player_Play then
            if self:isFriend(p) then friend = true end
        end
    end

    local dis = {}
	if friend or ((self.player:getMark("&ny_10th_changqu") + min) >= self.player:getHp()) then
        local cards = sgs.QList2Table(self.player:getCards("h"))
	    self:sortByKeepValue(cards)
	    if #cards >= min then
		    for i = 1, min, 1 do
                table.insert(dis, cards[i]:getEffectiveId())
            end
	    end
    end
	return dis
end

local ny_10th_changqu_skill = {}
ny_10th_changqu_skill.name = "ny_10th_changqu"
table.insert(sgs.ai_skills, ny_10th_changqu_skill)
ny_10th_changqu_skill.getTurnUseCard = function(self, inclusive)
	if not self.player:hasUsed("#ny_10th_changqu") then
		return sgs.Card_Parse("#ny_10th_changqu:.:")
	end
end

sgs.ai_skill_use_func["#ny_10th_changqu"] = function(card, use, self)
	self:updatePlayers()
	local room = self.room
    local target = nil

    local move = self.room:getAlivePlayers():length()-1
    local left_now = self.player:getNextAlive(move)
    local left_num_now = 0
    local left_num = 0 
    local left = {}
    while(left_now:objectName() ~= self.player:objectName()) do
        local now = math.max(left_num_now,1)
        if self:isFriend(left_now) then
            left_num = left_num - now
            if left_now:getHandcardNum() < now then
                left_num = left_num - now
            end
        else
            left_num = left_num + now*1.5
            if left_now:getHandcardNum() < now then
                left_num = left_num + now
            end
        end
        left_num_now = left_num_now + 1
        left_now = left_now:getNextAlive(move)
        table.insert(left,left_num)
        if left_now:getHandcardNum() < now then
            break
        end
    end

    local right_now = self.player:getNextAlive(1)
    local right_num_now = 0
    local right_num = 0 
    local right = {}
    while(right_now:objectName() ~= self.player:objectName()) do
        local now = math.max(right_num_now,1)
        if self:isFriend(right_now) then
            right_num = right_num - now
            if right_now:getHandcardNum() < now then
                right_num = right_num - now
            end
        else
            right_num = right_num + now*1.5
            if right_now:getHandcardNum() < now then
                right_num = right_num + now
            end
        end
        right_num_now = right_num_now + 1
        right_now = right_now:getNextAlive(1)
        table.insert(right,right_num)
        if right_now:getHandcardNum() < now then
            break
        end
    end

    local left_max = left[1]
    local right_max = right[1]
    for _,p in ipairs(left) do
        if p > left_max then left_max = p end
    end
    for _,p in ipairs(right) do
        if p > right_max then right_max = p end
    end

    if left_max > right_max then
        target = self.player:getNextAlive(-1)
    else
        target = self.player:getNextAlive(1)
    end

	if not target then return end
	if target then
		local card_str = "#ny_10th_changqu:.:->"..target:objectName()
		local acard = sgs.Card_Parse(card_str)
		assert(acard)
		use.card = acard
		if use.to then
			use.to:append(target)
		end
	end
end

sgs.ai_skill_playerchosen.ny_10th_changqu = function(self,targets)
    local move = self.room:getAlivePlayers():length() - 1
    if self.player:getMark("ny_10th_changqu_right") > 0 then move = 1 end

    local direction_now = self.player:getNextAlive(move)
    local direction_num_now = 0
    local direction_num = 0 
    local direction = {}
    while(direction_now:objectName() ~= self.player:objectName()) do
        local now = math.max(direction_num_now,1)
        if self:isFriend(direction_now) then
            direction_num = direction_num - now
            if direction_now:getHandcardNum() < now then
                direction_num = direction_num - now
            end
        else
            direction_num = direction_num + now*1.5
            if direction_now:getHandcardNum() < now then
                direction_num = direction_num + now
            end
        end
        direction_num_now = direction_num_now + 1
        direction_now = direction_now:getNextAlive(move)
        table.insert(direction,direction_num)
        if direction_now:getHandcardNum() < now then
            break
        end
    end
    local target = 1
    local now = 1
    local max = direction[1]
    for _,p in ipairs(direction) do
        if p > max then 
            max = p
            target = now
        end
        now = now + 1
    end
    local result = self.player
    for i = 1, target, 1 do
        result = result:getNextAlive(move)
    end
    return result
end

--狐魅

local ny_10th_humei_skill = {}
ny_10th_humei_skill.name = "ny_10th_humei"
table.insert(sgs.ai_skills, ny_10th_humei_skill)
ny_10th_humei_skill.getTurnUseCard = function(self, inclusive)
    local choices = {"draw", "give", "recover"}
    for _,p in ipairs(choices) do
        if self.player:getMark("ny_10th_humei_tiansuan_remove_"..p.."-PlayClear") == 0 then
            return sgs.Card_Parse("#ny_10th_humei:.:"..p)
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_humei"] = function(card,use,self)
	local choices = {"draw", "give", "recover"}
    for _,p in ipairs(choices) do
        if self.player:getMark("ny_10th_humei_tiansuan_remove_"..p.."-PlayClear") == 0 then
            if p == "draw" then
                self:sort(self.friends, "handcard")
                local target = nil
                for _,friend in ipairs(self.friends) do
                    if friend:getHp() <= self.player:getMark("&ny_10th_humei-PlayClear") then
                        target = friend
                        break
                    end
                end
                if target then
                    use.card = sgs.Card_Parse("#ny_10th_humei:.:"..p)
                    if use.to then use.to:append(target) end
                    break
                end
            end

            if p == "give" then
                self:sort(self.enemies, "handcard")
                local target = nil
                for _,enemy in ipairs(self.enemies) do
                    if enemy:getHp() <= self.player:getMark("&ny_10th_humei-PlayClear") and (not enemy:isNude()) then
                        target = enemy
                        break
                    end
                end
                if target then
                    use.card = sgs.Card_Parse("#ny_10th_humei:.:"..p)
                    if use.to then use.to:append(target) end
                    break
                end
            end

            if p == "recover" then
                self:sort(self.friends, "hp")
                local target = nil
                for _,friend in ipairs(self.friends) do
                    if friend:getHp() <= self.player:getMark("&ny_10th_humei-PlayClear") and friend:isWounded() then
                        target = friend
                        break
                    end
                end
                if target then
                    use.card = sgs.Card_Parse("#ny_10th_humei:.:"..p)
                    if use.to then use.to:append(target) end
                    break
                end
            end
        end
    end
end

--狡黠

sgs.ai_skill_invoke.ny_10th_jiaoxia = function(self,data)
    if self.player:getHandcardNum() > self.player:getMaxHp() then return false end
    if self:getCardsNum("Slash") <= 1 then return true end
    if math.random(1,3) == 1 then return true end
    return false
end

sgs.ai_skill_use["@@ny_10th_jiaoxia"] = function(self, prompt)
    local card = sgs.Sanguosha:getCard(self.player:getMark("ny_10th_jiaoxia_card"))
    --local use = self:aiUseCard(card)
    local use = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(card, use)
    if use.card then
        if use.to and use.to:length() > 0 then
            local tos = {}
            for _,p in sgs.qlist(use.to) do
                table.insert(tos, p:objectName())
            end
            return card:toString().."->"..table.concat(tos,"+")
        end
    end
end

--没欲

local ny_10th_moyu_skill = {}
ny_10th_moyu_skill.name = "ny_10th_moyu"
table.insert(sgs.ai_skills, ny_10th_moyu_skill)
ny_10th_moyu_skill.getTurnUseCard = function(self, inclusive)
    if self.player:getMark("ny_10th_moyu_damage-Clear") > 0 then return end
    return sgs.Card_Parse("#ny_10th_moyu:.:")
end

sgs.ai_skill_use_func["#ny_10th_moyu"] = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,ep in sgs.list(self.enemies)do
		if ep:getCardCount()>0 and ep:getMark("ny_10th_moyu_chosen-PlayClear") == 0
		then
			if self:getCardsNum("Jink","h")>0
			or (self.player:getHandcardNum() > ep:getHandcardNum() 
            and self.player:getMark("&ny_10th_moyu-Clear") < (self.player:getHp() - 1))
			then
				use.card = card
				if use.to then use.to:append(ep) end
				return
			end
		end
	end
    for _,p in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if (not p:getJudgingArea():isEmpty()) and p:getMark("ny_10th_moyu_chosen-PlayClear") == 0 then
            use.card = card
			if use.to then use.to:append(p) end
			return
        end
    end
end

sgs.ai_use_value.ny_10th_moyu = 9.4
sgs.ai_use_priority.ny_10th_moyu = 5.8

--盼睇

local ny_pandi_tenth_skill = {}
ny_pandi_tenth_skill.name = "ny_pandi_tenth"
table.insert(sgs.ai_skills, ny_pandi_tenth_skill)
ny_pandi_tenth_skill.getTurnUseCard = function(self, inclusive)
    if self.player:getMark("ny_pandi_tenth_notcard-PlayClear") > 0 then return end
    return sgs.Card_Parse("#ny_pandi_tenth:.:")
end

sgs.ai_skill_use_func["#ny_pandi_tenth"] = function(card,use,self)
	for _,p in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if p:getMark("ny_pandi_tenth_damage-Clear") == 0 then
            if self:isFriend(p) then
				for _,h in sgs.qlist(self.player:getHandcards())do
					if h:isKindOf("Peach") and p:isWounded() then
						use.card = card
						if use.to then use.to:append(p) end
						return
					end
                end
            end
			for _,h in sgs.qlist(self.player:getHandcards())do
                if h:isKindOf("Slash") then
					for _,slashto in sgs.qlist(self.room:getCardTargets(p, h)) do
						if not self:isFriend(slashto) then
							use.card = card
							if use.to then use.to:append(p) end
							return
						end
					end
                elseif h:isKindOf("Duel") then
					for _,duelto in sgs.qlist(self.room:getCardTargets(p, duel)) do
						if not self:isFriend(duelto) then
							use.card = card
							if use.to then use.to:append(p) end
							return 
						end
					end
				end
            end
        end
    end

    --使用装备
    local equips = {}
	for _,h in sgs.qlist(self.player:getHandcards())do
		if h:isKindOf("Armor") or h:isKindOf("Weapon") then
			if not self:getSameEquip(h) then
			elseif h:isKindOf("GudingBlade") and self:getCardsNum("Slash")>0 then
				local HeavyDamage
				local slash = self:getCard("Slash")
				for _,enemy in ipairs(self.enemies)do
					if self.player:canSlash(enemy,slash,true) and not self:slashProhibit(slash,enemy)
					and self:slashIsEffective(slash,enemy) and not hasJueqingEffect(self.player,enemy) and enemy:isKongcheng() then
						HeavyDamage = true
						break
					end
				end
				if not HeavyDamage then table.insert(equips,h) end
			else
				table.insert(equips,h)
			end
		elseif h:getTypeId()==sgs.Card_TypeEquip then
			table.insert(equips,h)
		end
	end

	if #equips==0 then return end

	for _,friend in ipairs(self.friends_noself)do
        if friend:getMark("ny_pandi_tenth_damage-Clear") == 0 then
            for _,equip in ipairs(equips)do
                local index = equip:getRealCard():toEquipCard():location()
                if not friend:hasEquipArea(index) then continue end
                if not self:getSameEquip(equip,friend) and self:hasSkills(sgs.need_equip_skill.."|"..sgs.lose_equip_skill,friend) then
					use.card = card
					if use.to then
						use.to:append(friend)
					end
                    return
                end
            end
            for _,equip in ipairs(equips)do
                local index = equip:getRealCard():toEquipCard():location()
                if not friend:hasEquipArea(index) then continue end
                if not self:getSameEquip(equip,friend) then
					use.card = card
					if use.to then
						use.to:append(friend)
					end
                    return
                end
            end
        end
	end
end

sgs.ai_skill_use["@@ny_pandi_tenth"] = function(self, prompt)
    local target = nil
    for _,p in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if p:hasFlag("ny_pandi_tenth_target") then target = p end
    end
    if not target then return end

    if self:isFriend(target) then
        if target:isWounded() then
            for _,card in sgs.qlist(self.player:getHandcards()) do
                if card:isKindOf("Peach") then
                    return string.format("#ny_pandi_tenth_use:%s:->%s", card:getEffectiveId(), target:objectName())
                end
            end
        end
    end
	for _,card in sgs.qlist(self.player:getHandcards()) do
		if card:isKindOf("Slash") then
			for _,slashto in sgs.qlist(self.room:getCardTargets(target, card)) do
				if not self:isFriend(slashto) then
					return string.format("#ny_pandi_tenth_use:%s:->%s", card:getEffectiveId(), slashto:objectName())
                end
            end
        end
    end
	for _,card in sgs.qlist(self.player:getHandcards()) do
        for _,duelto in sgs.qlist(self.room:getCardTargets(target, card)) do
            if not self:isFriend(duelto) then
				if card:isKindOf("Duel") then
					return string.format("#ny_pandi_tenth_use:%s:->%s", card:getEffectiveId(), duelto:objectName())
                end 
            end
        end
    end

    --使用装备
    local equips = {}
	for _,card in sgs.qlist(self.player:getHandcards())do
		if card:isKindOf("Armor") or card:isKindOf("Weapon") then
			if not self:getSameEquip(card) then
			elseif card:isKindOf("GudingBlade") and self:getCardsNum("Slash")>0 then
				local HeavyDamage
				local slash = self:getCard("Slash")
				for _,enemy in ipairs(self.enemies)do
					if self.player:canSlash(enemy,slash,true) and not self:slashProhibit(slash,enemy)
					and self:slashIsEffective(slash,enemy) and not hasJueqingEffect(self.player,enemy) and enemy:isKongcheng() then
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

	for _,friend in ipairs(self.friends_noself)do
		for _,equip in ipairs(equips)do
			local index = equip:getRealCard():toEquipCard():location()
			if not friend:hasEquipArea(index) then continue end
			if not self:getSameEquip(equip,friend) and self:hasSkills(sgs.need_equip_skill.."|"..sgs.lose_equip_skill,friend) then
				return string.format("#ny_pandi_tenth_use:%s:->%s", equip:getEffectiveId(), friend:objectName())
			end
		end
		for _,equip in ipairs(equips)do
			local index = equip:getRealCard():toEquipCard():location()
			if not friend:hasEquipArea(index) then continue end
			if not self:getSameEquip(equip,friend) then
				return string.format("#ny_pandi_tenth_use:%s:->%s", equip:getEffectiveId(), friend:objectName())
			end
		end
	end
    return "."
end

--破锐

sgs.ai_skill_use["@@ny_tenth_porui"] = function(self, prompt)
    local cards = sgs.QList2Table(self.player:getCards("he"))
    self:sortByKeepValue(cards)
    if #cards == 0 then return "." end
    local max = 0
    local target = nil
    for _,p in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if self:isEnemy(p) and p:getMark("&ny_tenth_porui-Clear") > max then
            target = p
            max = p:getMark("&ny_tenth_porui-Clear")
        end
    end
    if target then
        return string.format("#ny_tenth_porui:%s:->%s", cards[1]:getEffectiveId(), target:objectName())
    end
end

--共护

sgs.ai_skill_playerchosen.ny_10th_gonghu = function(self,targets)
    local card = sgs.Sanguosha:getCard(self.player:getMark("ny_10th_gonghu_card"))
    if card then
        if card:objectName() == "ex_nihilo" then
            for _,p in sgs.qlist(targets) do
                if self:isFriend(p) then return p end
            end
        else
            for _,p in sgs.qlist(targets) do
                if self:isEnemy(p) then return p end
            end
        end
    end
    return nil
end

--谏国

local ny_10th_jianguo_skill = {}
ny_10th_jianguo_skill.name = "ny_10th_jianguo"
table.insert(sgs.ai_skills, ny_10th_jianguo_skill)
ny_10th_jianguo_skill.getTurnUseCard = function(self, inclusive)
    if self.player:hasUsed("#ny_10th_jianguo") then return end
    return sgs.Card_Parse("#ny_10th_jianguo:.:")
end

sgs.ai_skill_use_func["#ny_10th_jianguo"] = function(card,use,self)
    local target = nil
    local max = 0
    for _,p in sgs.qlist(self.room:getAlivePlayers()) do
        if self:isFriend(p) then
            local num = -1 + math.floor((p:getHandcardNum() - 1) / 2)
            num = num*1.1
            if num > max then 
                target = p
                max = num
            end
        else
            local num = -1 + math.floor((p:getHandcardNum() + 1) / 2)
            if num > max then 
                target = p
                max = num
            end
        end
    end
    if target then
        use.card = card
        if use.to then use.to:append(target) end
    end
end

sgs.ai_skill_choice["ny_10th_jianguo"] = function(self, choices, data)
	local target = data:toPlayer()
    if self:isFriend(target) then return "draw"
    else return "dis" end
end

sgs.ai_use_priority.ny_10th_jianguo = 6.8

--倾势

sgs.ai_skill_playerchosen.ny_10th_qinshi = function(self, targets)
    local players = sgs.QList2Table(targets)
    self:sort(players, "hp")
    for _,p in ipairs(players) do
        if self:isEnemy(p) then return p end
    end
    return nil
end

--汇灵

sgs.ai_skill_playerchosen.ny_10th_huiling = function(self, targets)
    local players = sgs.QList2Table(targets)
    self:sort(players, "defense")
    for _,p in ipairs(players) do
        if self:isEnemy(p) then return p end
    end
    return nil
end

--冲虚

local ny_10th_chongxu_skill = {}
ny_10th_chongxu_skill.name = "ny_10th_chongxu"
table.insert(sgs.ai_skills, ny_10th_chongxu_skill)
ny_10th_chongxu_skill.getTurnUseCard = function(self, inclusive)
    if self.player:getMark("&ny_10th_huiling_ling") < 4 then return end
    if self.player:getMark("@ny_10th_chongxu_mark") == 0 then return end
    return sgs.Card_Parse("#ny_10th_chongxu:.:")
end

sgs.ai_skill_use_func["#ny_10th_chongxu"] = function(card,use,self)
    use.card = card
end

--踏寂

sgs.ai_skill_playerchosen.taji = function(self, targets)
    local players = sgs.QList2Table(targets)
    self:sort(players, "defense")
    for _,p in ipairs(players) do
        if self:isEnemy(p) then return p end
    end
    return nil
end

--清荒

sgs.ai_skill_invoke.ny_10th_qinghuang = function(self, data)
    if self.player:isWounded() and (self.player:getMaxHp() > 4) then
        return true
    end
    return false
end

--沉勇

sgs.ai_skill_invoke.ny_10th_chenyong = function(self, data)
    return true
end

--救陷

local ny_10th_jiuxian_skill = {}
ny_10th_jiuxian_skill.name = "ny_10th_jiuxian"
table.insert(sgs.ai_skills, ny_10th_jiuxian_skill)
ny_10th_jiuxian_skill.getTurnUseCard = function(self, inclusive)
    if self.player:getHandcardNum() == 0 then return end
    if self.player:hasUsed("#ny_10th_jiuxian") then return end
    return sgs.Card_Parse("#ny_10th_jiuxian:.:")
end

sgs.ai_skill_use_func["#ny_10th_jiuxian"] = function(card,use,self)
    local n = self.player:getHandcardNum()
    n = math.ceil(n/2)
    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByKeepValue(cards)
    local usecards = {}
    for i = 1, n, 1 do
        table.insert(usecards, cards[i]:getEffectiveId())
    end

    local duel = sgs.Sanguosha:cloneCard("duel", sgs.Card_SuitToBeDecided, -1)
    --local usec = self:aiUseCard(duel)
    local usec = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(duel, usec)
    if usec.card then
        local tos = {}
        for _,p in sgs.qlist(usec.to) do
            table.insert(tos, p:objectName())
        end
        local card_str = string.format("#ny_10th_jiuxian:%s:->%s", table.concat(usecards, "+"), table.concat(tos, "+"))
        local acard = sgs.Card_Parse(card_str)
        use.card = acard
        if use.to then use.to = usec.to end
    end
end

sgs.ai_skill_playerchosen.ny_10th_jiuxian = function(self, targets)
    local players = sgs.QList2Table(targets)
    self:sort(players, "hp")
    for _,player in ipairs(players) do
        if self:isFriend(player) then return player end
    end
    return nil
end

sgs.ai_playerchosen_intention.ny_10th_jiuxian = function(self, from, to)
	sgs.updateIntention(from, to, -80)
end

sgs.ai_use_priority.ny_10th_jiuxian = 4.8

--谏诤

local ny_tenth_jianzheng_skill = {}
ny_tenth_jianzheng_skill.name = "ny_tenth_jianzheng"
table.insert(sgs.ai_skills, ny_tenth_jianzheng_skill)
ny_tenth_jianzheng_skill.getTurnUseCard = function(self, inclusive)
    if self.player:hasUsed("#ny_tenth_jianzheng") then return end
    return sgs.Card_Parse("#ny_tenth_jianzheng:.:")
end

sgs.ai_skill_use_func["#ny_tenth_jianzheng"] = function(card,use,self)
    self:sort(self.enemies, "handcard")
    self.enemies = sgs.reverse(self.enemies)
    for _,enemy in ipairs(self.enemies) do
        if (not enemy:isKongcheng()) then
            use.card = card
            if use.to then use.to:append(enemy) end
            return
        end
    end
end

sgs.ai_skill_use["@@ny_tenth_jianzheng"] = function(self, prompt)
    local target = nil
    for _,p in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if p:hasFlag("ny_tenth_jianzheng_target") then
            target = p
            break
        end
    end

    local cards = sgs.QList2Table(target:getHandcards())
    self:sortByUseValue(cards, true)
    for _,card in ipairs(cards) do
        --local use = self:aiUseCard(card)
        local use = {isDummy=true,to=sgs.SPlayerList()}
        self:useCardByClassName(card, use)
        if use.to and use.to:length() > 0 then
            local tos = {}
            for _,p in sgs.qlist(use.to) do
                table.insert(tos, p:objectName())
            end
            local card_str = string.format("#ny_tenth_jianzheng_use:%s:->%s", card:getEffectiveId(), table.concat(tos, "+"))
            return card_str
        end
    end
end

sgs.ai_use_priority.ny_tenth_jianzheng = 6.8
sgs.ai_card_intention.ny_tenth_jianzheng = 50

--腹谋

sgs.ai_skill_playerschosen.ny_10th_fumou = function(self, targets, max, min)
    local selected = sgs.SPlayerList()
    local residue = max
    if not self.room:canMoveField("ej") then
        local can_choose = sgs.QList2Table(targets)
        self:sort(targets, "handcard")
        targets = sgs.reverse(targets)
        for _,p in ipairs(targets) do
            if self:isEnemy(p) and p:getHandcardNum() > 3 then
                selected:append(p)
                residue = residue - 1
            end
            if self:isFriend(p) and p:getHandcardNum() < 2 then
                selected:append(p)
                residue = residue - 1
            end
            if residue <= 0 then return selected end
        end
    else
        local from, card, to = self:moveField()
	    if from and card and to then 
            selected:append(self.player)
            residue = residue - 1
        end
        if residue <= 0 then return selected end

        if ((self:getCardsNum("Peach") == 0 and self.player:getEquips():length() > 0)
        or (self.player:getEquips():length() == 1) 
        or (self.player:getHp() == 1))
        and ( not selected:contains(self.player)) 
        and self.player:isWounded() then 
            selected:append(self.player)
            residue = residue - 1
        end
        if residue <= 0 then return selected end

        local can_choose = sgs.QList2Table(targets)
        for _,target in ipairs(can_choose) do
            if not selected:contains(target) then
                if self:isFriend(target) then
                    if target:isWounded() and target:getEquips():length() > 0
                    and ((target:getHp() == 1) or (target:getEquips():length() == 1)) then
                        selected:append(target)
                        residue = residue - 1
                    elseif target:getHandcardNum() <= 1 then
                        selected:append(target)
                        residue = residue - 1
                    end
                end
            end
            if residue <= 0 then return selected end
        end
    end
    return selected
end



sgs.ai_skill_choice["ny_10th_fumou"] = function(self, choices, data)
	local items = choices:split("+")
    if #items == 1 then return items[1] end
    if table.contains(items, "recover") then
        if self.player:getHp() == 1 or self.player:getEquips():length() <= 2 
        or self:getCardsNum("Peach") == 0 then
            return "recover"
        end
    end
    if self.player:getHandcardNum() > 4 then table.removeOne(items, "discard") end
    if table.contains(items, "move") then
        local from, card, to = self:moveField()
	    if from and card and to then return "move" end
    end
    if self.player:getHandcardNum() <= 2 then return "discard" end
    return items[math.random(1, #items)]
end

--节行

sgs.ai_skill_invoke.ny_10th_jiexing = true

--三首

sgs.ai_skill_invoke.ny_10th_sanshou = true

--肆军

sgs.ai_skill_invoke.ny_10th_sijun = true

--天劫

sgs.ai_skill_playerschosen.ny_10th_tianjie = function(self, targets, max, min)
    local selected = sgs.SPlayerList()
    local n = max
    local can_choose = sgs.QList2Table(targets)
    self:sort(can_choose, "defense")
    for _,target in ipairs(can_choose) do
        if self:isEnemy(target) then
            selected:append(target)
            n = n - 1
        end
        if n <= 0 then break end
    end
    return selected
end

--陷筑

sgs.ai_skill_invoke.ny_tenth_xianzhu = true

sgs.ai_skill_choice["ny_tenth_xianzhu"] = function(self, choices, data)
	local items = choices:split("+")
    if table.contains(items, "ignore") then return "ignore" end
    if self.player:getMark("SkillDescriptionArg2_ny_tenth_xianzhu") + 1 >= #self.enemies then
        table.removeOne(items, "targets")
    end
    return items[math.random(1, #items)]
end

--大攻车

sgs.ai_skill_use["@@ny_tenth_dagongche_slash"] = function(self, prompt)
    local slash = sgs.Sanguosha:cloneCard("slash", sgs.Card_SuitToBeDecided, -1)
    slash:setSkillName("_ny_tenth_dagongche")
    
    --local use = self:aiUseCard(card)
    local use = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(slash, use)
    if use.to and use.to:length() > 0 then
        local tos = {}
        for _,p in sgs.qlist(use.to) do
            table.insert(tos, p:objectName())
        end
        local card_str = string.format("#ny_tenth_dagongche_slash:.:->%s", table.concat(tos, "+"))
        return card_str
    end
end

--权计

sgs.ai_skill_invoke.ny_10th_quanji = true

--排异

local ny_10th_paiyi_skill = {}
ny_10th_paiyi_skill.name = "ny_10th_paiyi"
table.insert(sgs.ai_skills, ny_10th_paiyi_skill)
ny_10th_paiyi_skill.getTurnUseCard = function(self, inclusive)
    if self.player:getPile("ny_10th_quan"):length() <= 2 then return end
    local choices = {"draw", "damage"}
    for _,choice in ipairs(choices) do
        if self.player:getMark("ny_10th_paiyi_tiansuan_remove_"..choice.."-PlayClear") == 0  then
            return sgs.Card_Parse("#ny_10th_paiyi:.:"..choice)
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_paiyi"] = function(card,use,self)
    local card_ids = sgs.QList2Table(self.player:getPile("ny_10th_quan"))
    local choices = {"damage", "draw"}
    for _,choice in ipairs(choices) do
        if self.player:getMark("ny_10th_paiyi_tiansuan_remove_"..choice.."-PlayClear") == 0  then
            if choice == "draw" then
                self:sort(self.friends, "handcard")
                if #self.friends > 0 then
                    local card_str = string.format("#ny_10th_paiyi:%s:%s", card_ids[1], choice)
                    local acard = sgs.Card_Parse(card_str)
                    use.card = acard
                    if use.to then use.to:append(self.friends[1]) end
                    return 
                end
            end
            if choice == "damage" then
                self:sort(self.enemies, "hp")
                local max = #card_ids - 1
                local targets = sgs.SPlayerList()
                if #self.enemies > 0 then
                    for _,enemy in ipairs(self.enemies) do
                        targets:append(enemy)
                        max = max - 1
                        if max <= 0 then break end
                    end
                    if not targets:isEmpty() then
                        if max > 0 and (self.player:getHp() + self:getCardsNum("Peach") > 2) then
                            targets:append(self.player)
                        end
                        local card_str = string.format("#ny_10th_paiyi:%s:%s", card_ids[1], choice)
                        local acard = sgs.Card_Parse(card_str)
                        use.card = acard
                        if use.to then use.to = targets end
                        return 
                    end
                end
            end
        end
    end
end

sgs.ai_use_priority.ny_10th_paiyi = 5.8

--奸雄（经典）

sgs.ai_skill_invoke.ny_10th_jingdianjianxiong = true

--制衡（经典）

local ny_10th_jingdianzhiheng_skill = {}
ny_10th_jingdianzhiheng_skill.name = "ny_10th_jingdianzhiheng"
table.insert(sgs.ai_skills,ny_10th_jingdianzhiheng_skill)
ny_10th_jingdianzhiheng_skill.getTurnUseCard = function(self)
    if self.player:usedTimes("#ny_10th_jingdianzhiheng") < (1 + self.player:getMark("&ny_10th_jingdianzhiheng-Clear")) then
    else return end
	return sgs.Card_Parse("#ny_10th_jingdianzhiheng:.:")
end

--此处引用luas函数内的制衡，不知有无修改，报错直接删除

sgs.ai_skill_use_func["#ny_10th_jingdianzhiheng"] = function(card,use,self)
	local unpreferedCards = {}
	local cards = sgs.QList2Table(self.player:getHandcards())

	if self.player:getHp()<3 then
		local use_slash,keep_jink,keep_analeptic,keep_weapon = false,false,false,nil
		local keep_slash = self.player:getTag("JilveWansha"):toBool()
		for _,zcard in sgs.list(self.player:getCards("he"))do
			if not isCard("Peach",zcard,self.player)
			then
				local shouldUse = true
				if isCard("Slash",zcard,self.player)
				and not use_slash
				then
					--local dummy_use = self:aiUseCard(zcard)
                    local dummy_use = {isDummy=true,to=sgs.SPlayerList()}
                    self:useCardByClassName(zcard, dummy_use)

					if dummy_use.card then
						if keep_slash then shouldUse = false end
						if dummy_use.to then
							for _,p in sgs.list(dummy_use.to)do
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
                    local dummy_use = {isDummy=true,to=sgs.SPlayerList()}
                    self:useCardByClassName(zcard, dummy_use)

					if dummy_use.card then shouldUse = false end
				end
				if zcard:getTypeId()==sgs.Card_TypeEquip and not self.player:hasEquip(zcard) then
                    local dummy_use = {isDummy=true,to=sgs.SPlayerList()}
                    self:useCardByClassName(zcard, dummy_use)
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
				if self.player:getHp()<2 and isCard("Analeptic",zcard,self.player) and not keep_analeptic then
					keep_analeptic = true
					shouldUse = false
				end
				if shouldUse then table.insert(unpreferedCards,zcard:getId()) end
			end
		end
	end

	if #unpreferedCards<1 then
		local use_slash_num = 0
		self:sortByKeepValue(cards)
		for _,card in ipairs(cards)do
			if card:isKindOf("Slash") then
				local will_use = false
				if use_slash_num<=sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,card)
				then
                    local dummy_use = {isDummy=true,to=sgs.SPlayerList()}
                    self:useCardByClassName(card, dummy_use)

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
			if card:isKindOf("Weapon") and self.player:getHandcardNum()<3
			or self:getSameEquip(card,self.player)
			or card:isKindOf("OffensiveHorse")
			or card:isKindOf("AmazingGrace")
			then table.insert(unpreferedCards,card:getId())
			elseif card:getTypeId()==sgs.Card_TypeTrick then
                local dummy_use = {isDummy=true,to=sgs.SPlayerList()}
                    self:useCardByClassName(card, dummy_use)
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

	for i = #unpreferedCards,1,-1 do
		if sgs.Sanguosha:getCard(unpreferedCards[i]):isKindOf("WoodenOx") and self.player:getPile("wooden_ox"):length()>1 then
			table.removeOne(unpreferedCards,unpreferedCards[i])
		end
	end

	local use_cards = {}
	for i = #unpreferedCards,1,-1 do
		if not self.player:isJilei(sgs.Sanguosha:getCard(unpreferedCards[i])) then table.insert(use_cards,unpreferedCards[i]) end
	end

    if #use_cards>0 then
		local card_str = string.format("#ny_10th_jingdianzhiheng:%s:", table.concat(use_cards,"+"))
		local acard = sgs.Card_Parse(card_str)
        use.card = acard
	end
end

sgs.ai_use_value.ny_10th_jingdianzhiheng = sgs.ai_use_value.ZhihengCard
sgs.ai_use_priority.ny_10th_jingdianzhiheng = sgs.ai_use_priority.ZhihengCard
sgs.dynamic_value.benefit.ny_10th_jingdianzhiheng = sgs.dynamic_value.benefit.ZhihengCard

--仁德

local ny_tenth_jingdianrende_skill = {}
ny_tenth_jingdianrende_skill.name = "ny_tenth_jingdianrende"
table.insert(sgs.ai_skills,ny_tenth_jingdianrende_skill)
ny_tenth_jingdianrende_skill.getTurnUseCard = function(self)
	return sgs.Card_Parse("#ny_tenth_jingdianrende:.:")
end

sgs.ai_skill_use_func["#ny_tenth_jingdianrende"] = function(card,use,self)
    self:sort(self.enemies, "handcard")
    for _,enemy in ipairs(self.enemies) do
        if enemy:getHandcardNum() > 0 and enemy:getMark("ny_tenth_jingdianrende_get-PlayClear") == 0 then
            if use.to then use.to:append(enemy) end
            use.card = card
            return 
        end
    end
end

sgs.ai_skill_choice["ny_tenth_jingdianrende"] = function(self, choices, data)
	local items = choices:split("+")
    if #items == 1 then return items[1] end
    if self.player:getLostHp() > 1 then return "peach" end
    table.removeOne(items, "cancel")
    for _,pattern in ipairs(items) do
        local card = sgs.Sanguosha:cloneCard(pattern, sgs.Card_SuitToBeDecided, -1)
        local usec = {isDummy=true,to=sgs.SPlayerList()}
        self:useCardByClassName(card, usec)
        if usec.to and usec.to:length() > 0 then
            return pattern
        end
    end
    return "cancel"
end

sgs.ai_skill_use["@@ny_tenth_jingdianrende"] = function(self, prompt)
    local pattern = self.player:property("ny_tenth_jingdianrende_card"):toString()

    if pattern == "peach" or pattern == "analeptic" then
        return string.format("#ny_tenth_jingdianrende_basic:.:->%s", self.player:objectName())
    end

	local card = sgs.Sanguosha:cloneCard(pattern, sgs.Card_SuitToBeDecided, -1)
	card:setSkillName("ny_tenth_jingdianrende")
    local card = sgs.Sanguosha:cloneCard(pattern, sgs.Card_SuitToBeDecided, -1)
    local usec = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(card, usec)
    if usec.to and usec.to:length() > 0 then
        local tos = {}
        for _,to in sgs.qlist(usec.to) do
            table.insert(tos, to:objectName())
        end
        local card_str = string.format("#ny_tenth_jingdianrende_basic:.:->%s", table.concat(tos, "+"))
        return card_str
    end
    return "."
end

sgs.ai_use_priority.ny_tenth_jingdianrende = 8.8
sgs.ai_use_value.ny_tenth_jingdianrende = 10.8

--慧淑

sgs.ai_skill_invoke.ny_tenth_huishu = true

--易数

sgs.ai_skill_choice["ny_10th_yishu"] = function(self, choices, data)
	local items = choices:split("+")
    for _,item in ipairs(items) do
        if string.find(item, "draw") then return item end
    end
    for _,item in ipairs(items) do
        if string.find(item, "discard") then return item end
    end
    return items[math.random(1,#items)]
end

--离宫

sgs.ai_skill_choice["ny_10th_ligong"] = function(self, choices, data)
	local items = choices:split("+")
    if #items > 1 then
        table.removeOne(items, "cancel")
    end
    return items[math.random(1,#items)]
end

--机巧

sgs.ai_skill_discard.ny_10th_jiqiao = function(self,max,min)
    local cards = sgs.QList2Table(self.player:getCards("he"))
    local peach = 0
    local analeptic = 0
    local slash = 0
    local discards = {}
    for _,card in ipairs(cards) do
        if card:isKindOf("EquipCard") then
            if card:isKindOf("Weapon") and self.player:getWeapon() 
            and self.player:getWeapon():getEffectiveId() ~= card:getEffectiveId() then
                table.insert(discards, card:getEffectiveId())
            elseif (not card:isKindOf("Weapon")) then
                table.insert(discards, card:getEffectiveId())
            end
        end
        if card:isKindOf("Peach") then
            if peach < (self.player:getLostHp() + 1) then
                peach = peach + 1
            else
                table.insert(discards, card:getEffectiveId())
            end
        end
        if card:isKindOf("Analeptic") then
            if analeptic < 1 then
                analeptic = analeptic + 1
            else
                table.insert(discards, card:getEffectiveId())
            end
        end
        if card:isKindOf("Slash") then
            local limit = 1 + sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill.Residue, self.player, card)
            if slash < limit then
                slash = slash + 1
            else
                table.insert(discards, card:getEffectiveId())
            end
        end
        if card:objectName() == "collateral" then
            table.insert(discards, card:getEffectiveId())
        end
    end
    return discards
end
            
--螽集

sgs.ai_skill_invoke.ny_10th_zhongji = function(self, data)
    local draw = self.player:getMaxHp() - self.player:getHandcardNum()
    local dis = 1 + self.player:getMark("&ny_10th_zhongji-Clear")
    if self.player:getPhase() == sgs.Player_NotActive then
        return draw >= dis
    else
        return draw >= (dis + 1)
    end
end

--掠城

local ny_10th_lvecheng_skill = {}
ny_10th_lvecheng_skill.name = "ny_10th_lvecheng"
table.insert(sgs.ai_skills,ny_10th_lvecheng_skill)
ny_10th_lvecheng_skill.getTurnUseCard = function(self)
    if self:getCardsNum("Slash") < 1 then return end
    if self.player:hasUsed("#ny_10th_lvecheng") then return end
	return sgs.Card_Parse("#ny_10th_lvecheng:.:")
end

sgs.ai_skill_use_func["#ny_10th_lvecheng"] = function(card,use,self)
    self:sort(self.enemies, "defense")
    for _,enemy in ipairs(self.enemies) do
        if self.player:canSlash(enemy) then
            if use.to then use.to:append(enemy) end
            use.card = card
            return 
        end
    end
end

sgs.ai_skill_invoke.ny_10th_lvecheng = function(self, data)
    local target
    for _,other in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if other:hasFlag("ny_10th_lvecheng") then
            target = other
            break
        end
    end
    return target and self:isEnemy(target)
end

--复学

sgs.ai_skill_invoke.ny_tenth_fuxue = function(self, data)
    return true
end

sgs.ai_skill_use["@@ny_tenth_fuxue"] = function(self, prompt)
    local n = self.player:getHp()
    local card_ids = self.player:getTag("ny_tenth_fuxue_cards"):toIntList()
    local cards = {}
    for _,id in sgs.qlist(card_ids) do
        table.insert(cards, sgs.Sanguosha:getCard(id))
    end
    self:sortByUseValue(cards)
    local get = {}
    for _,card in ipairs(cards) do
        table.insert(get, card:getEffectiveId())
        n = n - 1
        if n <= 0 then break end
    end
    if #get > 0 then
        return string.format("#ny_tenth_fuxue:%s:", table.concat(get, "+"))
    end
    return "."
end

--手谈

local ny_10th_shoutan_skill = {}
ny_10th_shoutan_skill.name = "ny_10th_shoutan"
table.insert(sgs.ai_skills, ny_10th_shoutan_skill)
ny_10th_shoutan_skill.getTurnUseCard = function(self, inclusive)
	if not self.player:hasUsed("#ny_10th_shoutan") then
        if (not self.player:hasSkill("ny_10th_yaoyi")) 
        and self.player:getHandcardNum() < self.player:getMaxCards() then return end
		return sgs.Card_Parse("#ny_10th_shoutan:.:")
	end
end

sgs.ai_skill_use_func["#ny_10th_shoutan"] = function(card, use, self)
	if self.player:hasSkill("ny_10th_yaoyi") then
		use.card = card
	else
		local usecard = nil
		local handcards = sgs.QList2Table(self.player:getCards("h"))
		self:sortByUseValue(handcards)
		if self.player:getChangeSkillState("ny_10th_shoutan") <= 1 then
			for _,cc in ipairs(handcards) do
				if not cc:isBlack() then
					usecard = cc:getEffectiveId()
					break
				end
			end
		else
			for _,cc in ipairs(handcards) do
				if cc:isBlack() then
					usecard = cc:getEffectiveId()
					break
				end
			end
		end
		if usecard then
			local card_str = "#ny_10th_shoutan:"..usecard..":"
			local acard = sgs.Card_Parse(card_str)
			assert(acard)
			use.card = acard
		end
	end
end

--肃军

sgs.ai_skill_invoke.ny_10th_sujun = true

--砺锋

local ny_10th_lifeng_skill = {}
ny_10th_lifeng_skill.name = "ny_10th_lifeng"
table.insert(sgs.ai_skills, ny_10th_lifeng_skill)
ny_10th_lifeng_skill.getTurnUseCard = function(self, inclusive)
    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByKeepValue(cards)
	for _,card in ipairs(cards) do
        if self.player:getMark(card:getColorString().."ny_10th_lifeng-Clear")>0 then continue end

		local slash = sgs.Sanguosha:cloneCard("slash")
		slash:addSubcard(card)
		slash:setSkillName("ny_10th_lifeng")
		slash:deleteLater()

		local usec = {isDummy=true,to=sgs.SPlayerList()}
		self:useCardByClassName(slash, usec)
		if usec.card then
			return slash
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_lifeng"] = function(card, use, self)
	use.card = card
    if use.to then use.to = self.ny_10th_lifeng_to end
end

sgs.ai_view_as.ny_10th_lifeng = function(card, player, card_place)
	if card_place == sgs.Player_PlaceHand then
        if player:getMark(card:getColorString().."ny_10th_lifeng-Clear")>0 then return end
		local suit = card:getSuitString()
		local number = card:getNumberString()
		local card_id = card:getEffectiveId()
		if sgs.Sanguosha:getCurrentCardUsePattern()=="nullification" then
			return ("nullification:ny_10th_lifeng[%s:%s]=%d"):format(suit, number, card_id)
		else
			return ("slash:ny_10th_lifeng[%s:%s]=%d"):format(suit, number, card_id)
		end
	end
end

sgs.ai_use_priority.ny_10th_lifeng = 8.8

--强识

sgs.ai_skill_playerchosen.ny_10th_jxqiangzhi = function(self, targets)
	for _,target in sgs.qlist(targets) do
		if self:isEnemy(target) then
			return target
		end
	end
	return targets[1]
end

sgs.ai_playerchosen_intention.ny_10th_jxqiangzhi = function(self, from, to)
	sgs.updateIntention(from, to, 10)
end

--献图

sgs.ai_skill_invoke.ny_10th_jxxiantu = function(self, data)
    local peach = self:getCardsNum("Peach")
    local target
    for _,player in sgs.qlist(self.room:getAlivePlayers()) do
        if player:getPhase() == sgs.Player_Play then 
            target = player
            break
        end
    end
    if not target then return false end
    if self:isFriend(target) and ((self.player:getHp() >= 2) or (peach > 0)) then 
        return true
    else
        return false 
    end
end

sgs.ai_skill_discard.ny_10th_jxxiantu = function(self,max,min)
    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByUseValue(cards)
    local n = max
    local give = {}
    for _,card in ipairs(cards) do
        table.insert(give, card:getEffectiveId())
        n = n - 1
        if n <= 0 then break end
    end
    if n > 0 then
        for _,card in sgs.qlist(self.player:getCards("e")) do
            table.insert(give, card:getEffectiveId())
            n = n - 1
            if n <= 0 then break end
        end
    end
    return give
end

--缮甲

sgs.ai_skill_invoke.ny_tenth_shanjia = true

sgs.ai_skill_use["@@ny_tenth_shanjia"] = function(self, prompt)
    local slash = sgs.Sanguosha:cloneCard("slash", sgs.Card_SuitToBeDecided, -1)
    slash:setSkillName("ny_tenth_shanjia")
    slash:deleteLater()

    local usec = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(slash, usec)
    if usec.card then
        local tos = {}
        for _,to in sgs.qlist(usec.to) do
            table.insert(tos, to:objectName())
        end
        return slash:toString().."->"..table.concat(tos, "+")
    end
end

--破垣

sgs.ai_skill_invoke.ny_10th_poyuan = true

sgs.ai_skill_choice["ny_10th_poyuan"] = function(self, choices, data)
	local items = choices:split("+")
    return items[#items]
end

sgs.ai_skill_playerchosen.ny_10th_poyuan = function(self, targets)
    local all = sgs.QList2Table(targets)
    self:sort(all, "defense")
    for _,target in ipairs(all) do
        if self:isEnemy(target) then
            return target
        end
    end
    return nil
end

--画策

local ny_10th_huace_skill = {}
ny_10th_huace_skill.name = "ny_10th_huace"
table.insert(sgs.ai_skills, ny_10th_huace_skill)
ny_10th_huace_skill.getTurnUseCard = function(self, inclusive)
    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByKeepValue(cards, true)
    if #cards == 0 then return end
    if self.player:hasUsed("#ny_10th_huace") then return false end
    local huace_patterns = self.room:getTag("ny_10th_huace_cards"):toString():split("+")
    --if not huace_patterns then return end
	for _,pattern in ipairs(huace_patterns) do
        local canuse = false
        local card = sgs.Sanguosha:cloneCard(pattern, sgs.Card_SuitToBeDecided, -1)
        card:addSubcard(cards[1])
        card:setSkillName("ny_10th_huace")
        card:deleteLater()

        local mark = string.format("ny_10th_huace_guhuo_remove_%s_lun", pattern)
        if self.player:getMark(mark) == 0 and card:isAvailable(self.player) then
            canuse = true
        end

        local cc = ny_10th_huaceCard:clone()
        cc:addSubcard(cards[1])
        cc:setUserString(pattern)

        if canuse then
            local usec = {isDummy=true,to=sgs.SPlayerList()}
            self:useCardByClassName(card, usec)
            if usec.card and (not (card:canRecast() and usec.to:length() < 1)) then
                self.ny_10th_huace_to = usec.to
                --local card_str = string.format("#ny_10th_huace:%s:%s:", cards[1]:getEffectiveId(), pattern)
                local card_str = cc:toString()
                return sgs.Card_Parse(card_str)
            end
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_huace"] = function(card, use, self)
	use.card = card
    if use.to then use.to = self.ny_10th_huace_to end
end

sgs.ai_use_priority.ny_10th_huace = 6.8

--蕙质

sgs.ai_skill_invoke.ny_10th_huizhi = true

sgs.ai_skill_discard.ny_10th_huizhi = function(self, max, min)
    local max = 0
    for _,target in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if target:getHandcardNum() > max then max = target:getHandcardNum() end
    end
    if max < self.player:getHandcardNum() then return {} end
    local discard = {}
    local candis = self.player:getHandcardNum() - (max - 5)
    if candis <= 0 then return {} end
    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByKeepValue(cards)
    local max = 3
    for _,card in ipairs(cards) do
        table.insert(discard, card:getEffectiveId())
        candis = candis - 1
        max = max - 1
        if candis <= 0 then break end
        if max <= 0 then break end
    end
    return candis
end

--继椒

local ny_10th_jijiao_skill = {}
ny_10th_jijiao_skill.name = "ny_10th_jijiao"
table.insert(sgs.ai_skills, ny_10th_jijiao_skill)
ny_10th_jijiao_skill.getTurnUseCard = function(self, inclusive)
    local ids = self.player:getTag("ny_10th_jijiao_cards"):toIntList()
    if (not ids) or ids:isEmpty() then return end
    local n = ids:length()
    if self.player:getMark("ny_10th_jijiao_new-Clear") == 0 then
        if n < 4 then return end
        if math.random(1,3) == 1 and n < 8 then return end
    end
    if self.player:getMark("@ny_10th_jijiao_mark") == 0 then return end
    return sgs.Card_Parse("#ny_10th_jijiao:.:")
end

sgs.ai_skill_use_func["#ny_10th_jijiao"] = function(card,use,self)
    use.card = card
    if use.to then use.to:append(self.player) end
end

--擎北

sgs.ai_skill_choice["ny_10th_qingbei"] = function(self, choices, data)
	local items = choices:split("+")
    if #items > 4 then
        table.removeOne(items, "cancel")
    elseif #items == 4 and math.random(1, 2) == 1 then
        return "cancel"
    else
        return "cancel"
    end
    return items[math.random(1,#items)]
end

--酒遁

sgs.ai_skill_invoke.ny_10th_jiudun = function(self, data)
    return true
end

sgs.ai_skill_discard.ny_10th_jiudun = function(self, max, min)
    if self.player:isKongcheng() then return {} end
    local use = self.room:getTag("ny_10th_jiudun_card"):toCardUse()
    --local discard = false
    --if use.card:isKindOf("DelayedTrick") then discard = true end
    --if use.card:isDamageCard() then discard = true end
    if use.card:isKindOf("Analeptic") or use.card:isKindOf("Peach") then return {} end
    --if use.card:isKindOf("Slash") then discard = true end
    local notc = {"ex_nihilo","amazing_grace","god_salvation", "iron_chain"}
    if table.contains(notc, use.card:objectName()) then return {} end
    if use.card:isKindOf("EquipCard") then return {} end

    local cards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByKeepValue(cards)
    local dis = {}
    table.insert(dis, cards[1]:getEffectiveId())
    return dis
end

--昭文

sgs.ai_skill_invoke.ny_10th_zhaowen = function(self, data)
    return true
end

local function shuffle_zhaowen(t)
    if type(t)~="table" then
        return
    end
    local tab={}
    local index=1
    while #t~=0 do
        local n=math.random(0,#t)
        if t[n]~=nil then
            tab[index]=t[n]
            table.remove(t,n)
            index=index+1
        end
    end
    return tab
end

local ny_10th_zhaowen_skill = {}
ny_10th_zhaowen_skill.name = "ny_10th_zhaowen"
table.insert(sgs.ai_skills, ny_10th_zhaowen_skill)
ny_10th_zhaowen_skill.getTurnUseCard = function(self, inclusive)
    local cards = sgs.QList2Table(self.player:getHandcards())
    --self:sortByCardNeed(cards)
    self:sortByUseValue(cards, true)
    if #cards == 0 then return end

    local use_card
    for _,card in ipairs(cards) do
        if card:hasFlag("ny_10th_zhaowen") and card:isBlack() then
            use_card = card
            break
        end
    end

    if not use_card then return end

    local zhaowen_patterns = self.room:getTag("ny_10th_zhaowen_cards"):toString():split("+")
    local rand_patterns = shuffle_zhaowen(zhaowen_patterns)
    --if not huace_patterns then return end
	for _,pattern in ipairs(rand_patterns) do
        local canuse = false
        local card = sgs.Sanguosha:cloneCard(pattern, sgs.Card_SuitToBeDecided, -1)
        card:addSubcard(cards[1])
        card:setSkillName("ny_10th_zhaowen")
        card:deleteLater()

        local mark = string.format("ny_10th_zhaowen_guhuo_remove_%s-Clear", pattern)
        if self.player:getMark(mark) == 0 and card:isAvailable(self.player) then
            canuse = true
        end
 
        local cc = ny_10th_zhaowenCard:clone()
        cc:addSubcard(use_card)
        cc:setUserString(pattern)

        if canuse then
            local usec = {isDummy=true,to=sgs.SPlayerList()}
            self:useCardByClassName(card, usec)
            if usec.card and (not (card:canRecast() and usec.to:length() < 1)) then
                self.ny_10th_zhaowen_to = usec.to
                --local card_str = string.format("#ny_10th_zhaowen:%s:%s:", cards[1]:getEffectiveId(), pattern)
                local card_str = cc:toString()
                return sgs.Card_Parse(card_str)
            end
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_zhaowen"] = function(card, use, self)
	use.card = card
    if use.to then use.to = self.ny_10th_zhaowen_to end
end

sgs.ai_use_priority.ny_10th_zhaowen = 6
sgs.ai_use_value.ny_10th_zhaowen = 5

--割圆

sgs.ai_skill_playerchosen.ny_10th_geyuan = function(self,targets)
    if self.player:hasFlag("ny_10th_gusuan_draw") then
        self:sort(self.friends, "handcard")
        return self.friends[1]
    end
    if self.player:hasFlag("ny_10th_gusuan_discard") then
        local all = {}
        for _,target in sgs.qlist(targets) do
            if self:isEnemy(target) and (not target:isNude()) then
                table.insert(all, target)
            end
        end
        if #all > 0 then
            local max = -1
            local target
            self:sort(all, "defense")
            for _,p in ipairs(all) do
                local value = math.min(4, p:getCards("he"):length())
                if value > max then
                    target = p
                    max = value
                end
            end
            return target
        end
    end
    if self.player:hasFlag("ny_10th_gusuan_change") then
        local max = -9999
        local re 
        for _,target in sgs.qlist(targets) do
            local value = 0
            if self:isEnemy(target) and (not target:isKongcheng()) then
                value = target:getHandcardNum() - 5
            elseif self:isFriend(target) and (not target:isKongcheng()) then
                value = 5 - target:getHandcardNum()
            end
            if value > max then
                max = value
                re = target
            end
        end
        return re
    end
    return nil
end

--数荐

local ny_tenth_shujian_skill = {}
ny_tenth_shujian_skill.name = "ny_tenth_shujian"
table.insert(sgs.ai_skills, ny_tenth_shujian_skill)
ny_tenth_shujian_skill.getTurnUseCard = function(self, inclusive)
	if self.player:isKongcheng() then return end
    if self.player:usedTimes("#ny_tenth_shujian") >= 3 then return end
    if self.player:getMark("ny_tenth_shujian_failed-PlayClear") > 0 then return end
    if #self.friends_noself <= 0 then return end
	return sgs.Card_Parse("#ny_tenth_shujian:.:")
end

sgs.ai_skill_use_func["#ny_tenth_shujian"] = function(card, use, self)
    self:sort(self.friends_noself, "defense")
    local cards = sgs.QList2Table(self.player:getCards("he"))
    if #cards <= 2 then return end

    --self:sortByCardNeed(cards)
    self:sortByKeepValue(cards)

    local card_str = string.format("#ny_tenth_shujian:%s:",cards[1]:getEffectiveId())
    local acard = sgs.Card_Parse(card_str)
    use.card = acard
    if use.to then use.to:append(self.friends_noself[1]) end
end

sgs.ai_skill_choice["ny_tenth_shujian"] = function(self, choices, data)
    local target = data:toPlayer()
	local items = choices:split("+")
    if target and self:isFriend(target) and target:getMark("ny_tenth_shujian-PlayClear") <= 1 then
        for _,item in ipairs(items) do
            if string.find(item, "draw") then return item end
        end
    end
    if (not target) or (target and self:isEnemy(target)) then
        for _,item in ipairs(items) do
            if string.find(item, "dis") then return item end
        end
    end

    local card = sgs.Sanguosha:cloneCard("dismantlement", sgs.Card_SuitToBeDecided, -1)
    card:setSkillName("_ny_tenth_shujian")
    card:deleteLater()
    local usec = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(card, usec)
    if usec.card then
        for _,item in ipairs(items) do
            if string.find(item, "dis") then return item end
        end
    end

    return items[math.random(1,#items)]
end

sgs.ai_skill_use["@@ny_tenth_shujian"] = function(self, prompt)
    local card = sgs.Sanguosha:cloneCard("dismantlement", sgs.Card_SuitToBeDecided, -1)
    card:setSkillName("_ny_tenth_shujian")
    card:deleteLater()
    local usec = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(card, usec)
    if usec.card then
        local tos = {}
        for _,to in sgs.qlist(usec.to) do
            table.insert(tos, to:objectName())
        end
        return card:toString().."->"..table.concat(tos, "+")
    end
    return "."
end

sgs.ai_use_priority.ny_tenth_shujian = 6

--修文

sgs.ai_skill_invoke.ny_tenth_xiuwen = true

--龙诵

sgs.ai_skill_use["@@ny_tenth_longsong"] = function(self, prompt)
    local cards = sgs.QList2Table(self.player:getCards("he"))
    if #cards <= 0 then return false end
    local card
    --self:sortByCardNeed(cards)
    self:sortByUseValue(cards, true)
    for _,cc in ipairs(cards) do
        if cc:isRed() then
            card = cc
            break
        end
    end
    if not card then return "." end

    if #self.friends_noself <= 0 then return "." end
    self:sort(self.friends_noself, "defense")

    local target
    for _,p in ipairs(self.friends_noself) do
        local find = false
        local skills = p:getVisibleSkillList()
        for _,s in sgs.qlist(skills) do
            local skillname = s:objectName()
            if (not s:isAttachedLordSkill()) and (not self.player:hasSkill(skillname)) then
                local translation = sgs.Sanguosha:translate(":"..skillname)
                if string.find(translation,"出牌阶段") then
                    find = true
                    break
                end
            end
        end
        if find then 
            target = p
            break
        end
    end
    if (not target) and #cards <= 3 then return "." end
    if (not target) then target = self.friends_noself[1] end
    local card_str = string.format("#ny_tenth_longsong:%s:->%s", card:getEffectiveId(), target:objectName())
    return card_str
end

--逆击

sgs.ai_skill_invoke.ny_tenth_niji = true

sgs.ai_skill_use["@@ny_tenth_niji"] = function(self, prompt)
    local cards = {}
    for _,card in sgs.qlist(self.player:getCards("h")) do
        if card:hasTip("ny_tenth_niji") then
            table.insert(cards, card)
        end
    end
    if #cards <= 0 then return "." end
    self:sortByUseValue(cards)

    for _,card in ipairs(cards) do
        local usec = {isDummy=true,to=sgs.SPlayerList()}
        self:useCardByClassName(card, usec)
        if usec.card then
            local tos = {}
            for _,to in sgs.qlist(usec.to) do
                table.insert(tos, to:objectName())
            end
            return card:toString().."->"..table.concat(tos, "+")
        end
    end
    
    return "."
end

--恃纵

sgs.ai_skill_discard.ny_tenth_shizong = function(self,max,min)
    local target = self.player:getTag("ny_tenth_shizong_from"):toPlayer()
    if not self:isFriend(target) then return {} end
    if self.player:isNude() then return {} end
    local cards = sgs.QList2Table(self.player:getCards("he"))
    --self:sortByCardNeed(cards)
    self:sortByKeepValue(cards)
    return {cards[1]:getEffectiveId()}
end

sgs.ai_skill_use["@@ny_tenth_shizong!"] = function(self, prompt)
    local cards = sgs.QList2Table(self.player:getCards("he"))
    --self:sortByCardNeed(cards)
    self:sortByKeepValue(cards)
    local need = self.player:getMark("&ny_tenth_shizong-Clear") + 1
    local give = {}
    for _,card in ipairs(cards) do
        table.insert(give, card:getEffectiveId())
        need = need - 1
        if need <= 0 then break end
    end

    local target
    if #self.friends_noself > 0 then
        self:sort(self.friends_noself, "defense")
        target = self.friends_noself[1]
    end
    for _,friend in ipairs(self.friends_noself) do
        if friend:getPhase() ~= sgs.Player_NotActive then
            target = friend
            break
        end
    end
    if not target then
        local all = sgs.QList2Table(self.room:getOtherPlayers(self.player))
        target = all[math.random(1,#all)]
    end

    return string.format("#ny_tenth_shizong_give:%s:->%s", table.concat(give, "+"), target:objectName())
end

local ny_tenth_shizong_skill = {}
ny_tenth_shizong_skill.name = "ny_tenth_shizong"
table.insert(sgs.ai_skills, ny_tenth_shizong_skill)
ny_tenth_shizong_skill.getTurnUseCard = function(self, inclusive)
	--if self.player:hasUsed("#ny_tenth_shizong") then return end
    if self.player:getMark("ny_tenth_shizong_disable-Clear") > 0 then return end
    if #self.friends_noself == 0 then return end
    local num = self.player:getHandcardNum() + self.player:getEquips():length()
    local need = self.player:getMark("&ny_tenth_shizong-Clear") + 1
    if num < need or need > 2 then return end

    local basics = {"peach", "analeptic", "thunder_slash", "slash", "fire_slash"}
    for _,basic in ipairs(basics) do
        local card = sgs.Sanguosha:cloneCard(basic, sgs.Card_SuitToBeDecided, -1)
        card:setSkillName("ny_tenth_shizong")
        card:deleteLater()

        local usec = {isDummy=true,to=sgs.SPlayerList()}
        self:useCardByClassName(card, usec)
        if usec.card and self:getCardsNum(card:getClassName()) < 1 and card:isAvailable(self.player) then
            self.ny_tenth_shizong_to = usec.to
            local cc = ny_tenth_shizongCard:clone()
            cc:setUserString(basic)

            local card_str = cc:toString()
            return sgs.Card_Parse(card_str)
        end
    end
end

sgs.ai_skill_use_func["#ny_tenth_shizong"] = function(card, use, self)
    use.card = card
    if use.to then use.to = self.ny_tenth_shizong_to end
end

sgs.ai_guhuo_card.ny_tenth_shizong = function(self,toname,class_name)
    if self.player:getMark("ny_tenth_shizong_disable-Clear") > 0 then return end
    if #self.friends_noself == 0 then return end
    local num = self.player:getHandcardNum() + self.player:getEquips():length()
    local need = self.player:getMark("&ny_tenth_shizong-Clear") + 1
    if num < need or need > 2 then return end
    if sgs.Sanguosha:getCurrentCardUseReason() ~= sgs.CardUseStruct_CARD_USE_REASON_RESPONSE_USE then return nil end
    if self:getCardsNum(class_name) > 0 then return end

	local card = sgs.Sanguosha:cloneCard(toname, sgs.Card_SuitToBeDecided, -1)
    card:deleteLater()
    if not card:isKindOf("BasicCard") then return end

    local cc = ny_tenth_shizongCard:clone()
    cc:setUserString(toname)
    return cc:toString()
end

sgs.ai_use_priority.ny_tenth_shizong = 3

--悖逆

local ny_10th_beini_skill = {}
ny_10th_beini_skill.name = "ny_10th_beini"
table.insert(sgs.ai_skills, ny_10th_beini_skill)
ny_10th_beini_skill.getTurnUseCard = function(self, inclusive)
	if self.player:hasUsed("#ny_10th_beini") then return end
    if #self.enemies == 0 then return end
	return sgs.Card_Parse("#ny_10th_beini:.:")
end

sgs.ai_skill_use_func["#ny_10th_beini"] = function(card, use, self)
    self:sort(self.enemies, "defense")
    use.card = card
    if #self.enemies > 1 then
        if use.to then
            use.to:append(self.enemies[2])
            use.to:append(self.enemies[1])
        end
    else
        if use.to then
            use.to:append(self.player)
            use.to:append(self.enemies[1])
        end
    end
end

--移驾

sgs.ai_skill_playerchosen.ny_10th_yijia = function(self,targets)
    local name = self.player:getTag("ny_10th_yijia"):toString()
    local target = self.room:findPlayerByObjectName(name)
    if (not target) or (not self:isFriend(target)) then return nil end
    local all = {}
    for _,p in sgs.qlist(targets) do
        if not self:isFriend(p) then
            table.insert(all, p)
        end
    end
    if #all == 0 then return nil end
    self:sort(all, "defense")
    for _,p in ipairs(all) do
        for _,card in sgs.qlist(p:getEquips()) do
            local equip_index = card:getRealCard():toEquipCard():location()
            if target:hasEquipArea(equip_index) and (not target:getEquip(equip_index)) then
                return p
            end
        end
    end
    return all[1]
end

sgs.ai_skill_cardchosen.ny_10th_yijia = function(self, who,flags,reason,method)
    local enemy = who
    local name = self.player:getTag("ny_10th_yijia"):toString()
    local target = self.room:findPlayerByObjectName(name)
    if not target then
        local cards = sgs.QList2Table(enemy:getCards("e"))

        for _,id in sgs.qlist(self.disabled_ids) do
            table.removeOne(cards, sgs.Sanguosha:getCard(id))
        end

        return cards[math.random(1,#cards)]:getEffectiveId()
    end

    for _,card in sgs.qlist(enemy:getEquips()) do
        local equip_index = card:getRealCard():toEquipCard():location()
        if target:hasEquipArea(equip_index) and (not target:getEquip(equip_index)) then
            return card:getEffectiveId()
        end
    end

    local cards = sgs.QList2Table(enemy:getCards("e"))

    for _,id in sgs.qlist(self.disabled_ids) do
        table.removeOne(cards, sgs.Sanguosha:getCard(id))
    end

    return cards[math.random(1,#cards)]:getEffectiveId()
end

--定基

sgs.ai_skill_playerchosen.ny_tenth_dingji = function(self,targets)
    local max = -9999
    local maxtarget
    for _,target in sgs.qlist(targets) do
        local value
        if self:isFriend(target) or target:objectName() == self.player:objectName() then
            value = 5.5 - target:getHandcardNum()
            if target:getHandcardNum() == 5 then value = -9999 end
        else
            value = target:getHandcardNum() - 5
        end
        if (value > max) then
            max = value
            maxtarget = target
        end
    end
    return maxtarget
end

sgs.ai_skill_use["@@ny_tenth_dingji"] = function(self, prompt)
    local cards = sgs.QList2Table(self.player:getHandcards())
    if #cards <= 0 then return "." end
    self:sortByUseValue(cards)

    for _,handcard in ipairs(cards) do
        if handcard:isNDTrick() or handcard:isKindOf("BasicCard") then
            local card = sgs.Sanguosha:cloneCard(handcard:objectName(), sgs.Card_SuitToBeDecided, -1)
            card:setSkillName("_ny_tenth_dingji")
            card:deleteLater()

            local usec = {isDummy=true,to=sgs.SPlayerList()}
            self:useCardByClassName(card, usec)
            if usec.card then
                local tos = {}
                for _,to in sgs.qlist(usec.to) do
                    table.insert(tos, to:objectName())
                end
                return card:toString().."->"..table.concat(tos, "+")
            end
        end
    end
    
    return "."
end

--花火

local ny_tenth_huahuo_skill = {}
ny_tenth_huahuo_skill.name = "ny_tenth_huahuo"
table.insert(sgs.ai_skills, ny_tenth_huahuo_skill)
ny_tenth_huahuo_skill.getTurnUseCard = function(self, inclusive)
    if self.player:hasUsed("#ny_tenth_huahuo") then return end
    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByKeepValue(cards)
	for _,card in ipairs(cards) do
        if card:isRed() then
            local slash = sgs.Sanguosha:cloneCard("fire_slash", sgs.Card_SuitToBeDecided, -1)
            slash:addSubcard(card)
            slash:setSkillName("ny_tenth_huahuo")
            slash:deleteLater()

            local usec = {isDummy=true,to=sgs.SPlayerList()}
            self:useCardByClassName(slash, usec)
            if usec.card then
                self.ny_tenth_huahuo_to = usec.to
                local card_str = string.format("#ny_tenth_huahuo:%s:", card:getEffectiveId())
                return sgs.Card_Parse(card_str)
            end
        end 
    end
end

sgs.ai_skill_use_func["#ny_tenth_huahuo"] = function(card, use, self)
	use.card = card
    if use.to then use.to = self.ny_tenth_huahuo_to end
end

sgs.ai_skill_choice["ny_tenth_huahuo"] = function(self, choices, data)
	local items = choices:split("+")
    if #items <= 1 then return "cancel" end
    local n = 0
    local card = sgs.Sanguosha:cloneCard("fire_slash", sgs.Card_SuitToBeDecided, -1)
    card:deleteLater()
    for _,other in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if (not other:getPile("ny_tenth_xiaoyin"):isEmpty())
            and (not self.player:isProhibited(other, card)) then 
                if self:isFriend(other) then n = n - 1.1
                else n = n + 1 end
        end
    end
    if n > 0 then return "change" end
    return "cancel" 
end

--硝引

sgs.ai_skill_invoke.ny_tenth_xiaoyin = true

sgs.ai_skill_use["@@ny_tenth_xiaoyin"] = function(self, prompt)
    local ids = self.player:getTag("ny_tenth_xiaoyin_tem"):toIntList()
    if self.player:hasFlag("ny_tenth_xiaoyin_change") then
        local cards = {}
        for _,id in sgs.qlist(ids) do
            table.insert(cards, sgs.Sanguosha:getCard(id))
        end
        --self:sortByCardNeed(cards, true)
        self:sortByUseValue(cards)
        return string.format("#ny_tenth_xiaoyin_buff:%s:", cards[1]:getEffectiveId())
    end
    if self.player:hasFlag("ny_tenth_xiaoyin_add") then
        local damage = self.player:getTag("ny_tenth_xiaoyin_buff"):toDamage()
        if self:isFriend(damage.to) or damage.to:objectName() == self.player:objectName() then return "." end

        local cards = {}
        for _,id in sgs.qlist(ids) do
            table.insert(cards, sgs.Sanguosha:getCard(id))
        end
        --self:sortByCardNeed(cards)
        self:sortByUseValue(cards, true)

        local selfcards = sgs.QList2Table(self.player:getCards("he"))
        --self:sortByCardNeed(selfcards)
        self:sortByKeepValue(selfcards)

        for _,s in ipairs(selfcards) do
            for _,c in ipairs(cards) do
                local ctypes = {"BasicCard","TrickCard", "EquipCard"}
                for _,ctype in ipairs(ctypes) do
                    if s:isKindOf(ctype) and c:isKindOf(ctype) then 
                        local card_str = string.format("#ny_tenth_xiaoyin_buff:%s+%s:",s:getEffectiveId(),c:getEffectiveId())
                        return card_str
                    end
                end
            end
        end
        return "."
    end
    if #self.enemies == 0 then return "." end
    self:sort(self.enemies,"defense")
    for _,id in sgs.qlist(ids) do
        local card = sgs.Sanguosha:getCard(id)
        if (not card:hasFlag("ny_tenth_xiaoyin")) and card:isBlack() then
            
            for _,target in ipairs(self.enemies) do
                local can = true

                local need = false
                for _,other in sgs.qlist(self.room:getOtherPlayers(target)) do
                    if other:hasFlag("ny_tenth_xiaoyin") then inneed = true end
                end

                if not need then
                    for _,other in sgs.qlist(self.room:getOtherPlayers(target)) do
                        if other:hasFlag("ny_tenth_xiaoyin") and target:isAdjacentTo(other) then 
                            can = true
                            break
                        end
                    end
                end

                if target:hasFlag("ny_tenth_xiaoyin") then can = false end

                if can then return string.format("#ny_tenth_xiaoyin:%s:->%s",id,target:objectName()) end
            end
        end
    end
    return "."
end

--元嫡

sgs.ai_skill_invoke.ny_10th_yuandi = true

sgs.ai_skill_choice["ny_10th_yuandi"] = function(self, choices, data)
	local target = data:toPlayer()
    if self:isFriend(target) then
        return "draw="..target:getGeneralName()
    else
        return "discard="..target:getGeneralName()
    end
end

--心幽

local ny_10th_xinyou_skill = {}
ny_10th_xinyou_skill.name = "ny_10th_xinyou"
table.insert(sgs.ai_skills, ny_10th_xinyou_skill)
ny_10th_xinyou_skill.getTurnUseCard = function(self, inclusive)
    if self.player:hasUsed("#ny_10th_xinyou") then return end
    return sgs.Card_Parse("#ny_10th_xinyou:.:")
end

sgs.ai_skill_use_func["#ny_10th_xinyou"] = function(card, use, self)
	use.card = card
end

--英谋

sgs.ai_skill_playerchosen.ny_10th_yingmou = function(self, targets)
    if self.player:hasFlag("ny_10th_yingmou_second_second") then
        for _,p in sgs.qlist(targets) do
            if not self:isFriend(p) then return p end
        end
        local all = sgs.QList2Table(targets)
        return all[math.random(1,#all)]
    else
        local all = {}
        for _,p in sgs.qlist(targets) do
            if not self:isFriend(p) then 
                table.insert(all, p)
            end
        end
        if #all <= 0 then return nil end
        self:sort(all, "defense")
        return all[1]
    end
end

--作威

sgs.ai_skill_invoke.ny_10th_zuowei = true

sgs.ai_skill_playerchosen.ny_10th_zuowei = function(self, targets)
    if #self.enemies <= 0 then return nil end
    self:sort(self.enemies, "defense")
    return self.enemies[1]
end

--自固

local ny_10th_zigu_skill = {}
ny_10th_zigu_skill.name = "ny_10th_zigu"
table.insert(sgs.ai_skills, ny_10th_zigu_skill)
ny_10th_zigu_skill.getTurnUseCard = function(self, inclusive)
    if self.player:hasUsed("#ny_10th_zigu") then return end
    if #self.enemies <= 0 and self.player:getEquips():isEmpty() then return end
    local cards = sgs.QList2Table(self.player:getCards("he"))
    if (#cards <= 2) and self:isWeak() then return end
    --self:sortByCardNeed(cards)
    self:sortByKeepValue(cards)
    return sgs.Card_Parse("#ny_10th_zigu:"..cards[1]:getEffectiveId()..":")
end

sgs.ai_skill_use_func["#ny_10th_zigu"] = function(card, use, self)
    local target
    if #self.enemies > 0 then
        self:sort(self.enemies, "defense")
        for _,enemy in ipairs(self.enemies) do
            if enemy:getEquips():length() > 0 then
                target = enemy
                break
            end
        end
    end
    if (not target) and (not self.player:getEquips():isEmpty()) then
        target = self.player
    end
	if target then
        use.card = card
        --if use.to then use.to:append(target) end
    end
end

sgs.ai_skill_playerchosen.ny_10th_zigu = function(self, targets)
    local tos = sgs.QList2Table(targets)
    self:sort(tos, "defense")
    for _,to in ipairs(tos) do
        if (not self:isFriend(to)) then return to end
    end
    for _,to in ipairs(tos) do
        if to:objectName() == self.player:objectName() then return to end
    end
    return tos[math.random(1,#tos)]
end

sgs.ai_use_priority.ny_10th_zigu = 3

--双壁

local ny_tenth_shuangbi_skill = {}
ny_tenth_shuangbi_skill.name = "ny_tenth_shuangbi"
table.insert(sgs.ai_skills, ny_tenth_shuangbi_skill)
ny_tenth_shuangbi_skill.getTurnUseCard = function(self, inclusive)
    if self.player:hasUsed("#ny_tenth_shuangbi") then return end
    return sgs.Card_Parse("#ny_tenth_shuangbi:.:")
end

sgs.ai_skill_use_func["#ny_tenth_shuangbi"] = function(card, use, self)
    use.card = card
end

sgs.ai_use_priority.ny_tenth_shuangbi = 6

sgs.ai_skill_choice["ny_tenth_shuangbi"] = function(self, choices, data)
    if data:toInt() == 1 then
        if self:isWeak() or self.player:getHandcardNum() <= 2 then
            local item = string.format("draw=%s",self.room:getAlivePlayers():length())
            return item
        end
        local patterns = {"fire_slash", "fire_attack"}
        for _,pattern in ipairs(patterns) do
            local card = sgs.Sanguosha:cloneCard(pattern)
            card:setSkillName("_ny_tenth_shuangbi_mouzhouyu")
            card:deleteLater()
            local use = {isDummy=true,to=sgs.SPlayerList()}
            self:useCardByClassName(card, use)

            if use.card then return string.format("slash=%s",self.room:getAlivePlayers():length()) end
        end
        if (not self:isWeak()) and (#self.friends_noself == 0) and (math.random(1,3) > 1)
        and (self.player:getHandcardNum() > self.player:getMaxCards()) then
            return string.format("damage=%s",self.room:getAlivePlayers():length())
        end
        return string.format("draw=%s",self.room:getAlivePlayers():length())
    else
        local patterns = {"fire_slash", "fire_attack"}
        for _,pattern in ipairs(patterns) do
            local card = sgs.Sanguosha:cloneCard(pattern)
            card:setSkillName("_ny_tenth_shuangbi_mouzhouyu")
            card:deleteLater()
            local use = {isDummy=true,to=sgs.SPlayerList()}
            self:useCardByClassName(card, use)

            if use.card then return pattern end
        end
        return "cancel"
    end
end

sgs.ai_skill_discard.ny_tenth_shuangbi = function(self,max,min)
    if #self.friends_noself > 0 then return {} end

    local cards = sgs.QList2Table(self.player:getCards("h"))
	--self:sortByCardNeed(cards)
    self:sortByKeepValue(cards)
    local need = max
    local discard = {}

    for _,c in ipairs(cards) do
        if self:getUseValue(c) < sgs.ai_use_value.Slash then
            need = need - 1
            table.insert(discard, c:getEffectiveId())
        end
        if need <= 0 then break end
    end

    return discard
end

sgs.ai_skill_use["@@ny_tenth_shuangbi"] = function(self, prompt)
    local pattern = self.player:property("ny_tenth_shuangbi_card"):toString()
    local card = sgs.Sanguosha:cloneCard(pattern)
    card:setSkillName("_ny_tenth_shuangbi_mouzhouyu")
    card:deleteLater()

    local use = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(card, use)
    if use.card then
        if use.to and use.to:length() > 0 then
            local tos = {}
            for _,p in sgs.qlist(use.to) do
                table.insert(tos, p:objectName())
            end
            return "#ny_tenth_shuangbi_card:.:->"..table.concat(tos,"+")
        end
    end
    return "."
end

--寅君

sgs.ai_skill_invoke.ny_10th_yinjun = function(self, data)
    local use = self.player:getTag("ny_10th_yinjun"):toCardUse()
    local target = use.to:at(0)
    return self:isEnemy(target)
end

--蜜饴

sgs.ai_skill_invoke.ny_10th_miyi = true

sgs.ai_skill_choice["ny_10th_miyi"] = function(self, choices, data)
    local damage = 0
    local recover = 0
    for _,p in sgs.qlist(self.room:getAlivePlayers()) do
        if self:isEnemy(p) then
            if p:getHp() <= 2 or self:isWeak(p) then
                damage = damage + 1
            end
            if (not p:isWounded()) then recover = recover + 2 end
        end
        if self:isFriend(p) then
            if p:isWounded() and (p:objectName() == self.player:objectName() or self:isWeak(p)) then recover = recover + 1 end
        end
    end
    if damage >= recover then return "damage" end
    return "recover"
end

sgs.ai_skill_playerschosen.ny_10th_miyi = function(self, targets, max, min)
    local selected = sgs.SPlayerList()
    if self.player:hasFlag("ny_10th_miyi_damage") then
        for _,p in sgs.qlist(targets) do
            if self:isEnemy(p) then selected:append(p) end
        end
    else
        for _,p in sgs.qlist(targets) do
            if self:isEnemy(p) and (not p:isWounded()) then selected:append(p) end
            if self:isFriend(p) and p:isWounded() then selected:append(p) end
        end
    end
    return selected
end

--暖惠

sgs.ai_skill_playerchosen.ny_tenth_nuanhui = function(self, targets)
    local friends = {}
    for _,p in sgs.qlist(targets) do
        if self:isFriend(p) then
            table.insert(friends, p)
        end
    end
    if #friends == 0 then return nil end
    self:sort(friends, "defense")
    return friends[1]
end

sgs.ai_skill_choice["ny_tenth_nuanhui"] = function(self, choices, data)
    if self.player:getMark("ny_tenth_nuanhui") > 1 then return "cancel" end
    local items = {"peach", "fire_slash", "thunder_slash", "slash"}
    for _,item in ipairs(items) do
        local card = sgs.Sanguosha:cloneCard(item, sgs.Card_SuitToBeDecided, -1)
        card:deleteLater()
        if card:isAvailable(self.player) then
            --local use = self:aiUseCard(card)
            local use = {isDummy=true,to=sgs.SPlayerList()}
            self:useCardByClassName(card, use)
            if use.card then return item end
        end
    end
    return "cancel"
end

sgs.ai_skill_use["@@ny_tenth_nuanhui"] = function(self, prompt)
    local pattern = self.player:property("ny_tenth_nuanhui_card"):toString()
    local card = sgs.Sanguosha:cloneCard(pattern)
    card:setSkillName("ny_tenth_nuanhui")
    card:deleteLater()

    local use = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(card, use)
    if use.card then
        local tos = {}
        for _,p in sgs.qlist(use.to) do
            table.insert(tos, p:objectName())
        end
        return card:toString().."->"..table.concat(tos,"+")
    end
    return "."
end

--琼英

local ny_10th_qiongying_skill = {}
ny_10th_qiongying_skill.name = "ny_10th_qiongying"
table.insert(sgs.ai_skills, ny_10th_qiongying_skill)
ny_10th_qiongying_skill.getTurnUseCard = function(self, inclusive)
    if not self.room:canMoveField("ej") then return end
    if self.player:hasUsed("#ny_10th_qiongying") then return end
    local from, card, to = self:moveField()
    if from and card and to then else return false end
    return sgs.Card_Parse("#ny_10th_qiongying:.:")
end

sgs.ai_skill_use_func["#ny_10th_qiongying"] = function(card, use, self)
    use.card = card
    local from, card, to = self:moveField()
    if use.to then use.to:append(from) end
end

sgs.ai_skill_playerchosen.ny_10th_qiongying = function(self, targets)
    local from, card, to = self:moveField()
    if from and card and to then
        for _,target in sgs.qlist(targets) do
            if target:objectName() == to:objectName() then
                return target
            end
        end
    end
    local items = sgs.QList2Table(targets)
    return items[math.random(1, #items)]
end

sgs.ai_skill_cardchosen.ny_10th_qiongying = function(self, who,flags,reason,method)
    local from, card, to = self:moveField()
    if from and card and to then
        return card:getEffectiveId()
    end
    local cards = who:getCards(flags)
    for _,card in sgs.qlist(cards) do
        if not self.disabled_ids:contains(cards:getEffectiveId()) then
            return card:getEffectiveId()
        end
    end
end

--统围

local ny_10th_tongwei_skill = {}
ny_10th_tongwei_skill.name = "ny_10th_tongwei"
table.insert(sgs.ai_skills, ny_10th_tongwei_skill)
ny_10th_tongwei_skill.getTurnUseCard = function(self, inclusive)
    if self.player:hasUsed("#ny_10th_tongwei") then return end
    if self.player:getCards("he"):length() < 3 then return end
    if #self.enemies == 0 then return end
    return sgs.Card_Parse("#ny_10th_tongwei:.:")
end

sgs.ai_skill_use_func["#ny_10th_tongwei"] = function(card, use, self)
    local cards = sgs.QList2Table(self.player:getCards("he"))
    --self:sortByCardNeed(cards)
    self:sortByUseValue(cards, true)
    local target
    self:sort(self.enemies, "defense")
    for _,enemy in ipairs(self.enemies) do
        if enemy:getMark("ny_10th_tongwei+"..self.player:objectName()) == 0 then
            target = enemy
            break
        end
    end
    if target then
        local card_str = string.format("#ny_10th_tongwei:%s+%s:->%s", cards[1]:getEffectiveId(), cards[2]:getEffectiveId(), target:objectName())
        local acard = sgs.Card_Parse(card_str)
        use.card = acard
        if use.to then use.to:append(target) end
    end
end

sgs.ai_skill_choice["ny_10th_tongwei"] = function(self, choices, data)
    local use = data:toCardUse()
    local target = use.from
    if self:isWeak(target) then return "slash" end
    if target:getEquips():length() > 1 then return "dismantlement" end
    return "slash"
end

--连枝

sgs.ai_skill_playerchosen.ny_10th_lianzhi = function(self, targets)
    if self.player:hasFlag("ny_10th_shouze") then
        if #self.enemies == 0 then return nil end
        self:sort(self.enemies, "hp")
        return self.enemies[1]
    else
        for _,target in sgs.qlist(targets) do
            if (not self:isEnemy(target)) then
                return target
            end
        end
        local players = sgs.QList2Table(targets)
        return players[math.random(1,#players)]
    end
end

--风影

local ny_tenth_fengying_skill={}
ny_tenth_fengying_skill.name="ny_tenth_fengying"
table.insert(sgs.ai_skills,ny_tenth_fengying_skill)
ny_tenth_fengying_skill.getTurnUseCard=function(self,inclusive)
    if self.player:getMark("&ny_10th_jiao") <= 0 then return end

    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByKeepValue(cards)
    local need
    for _,card in ipairs(cards) do
        if card:getNumber() <= self.player:getMark("&ny_10th_jiao") then
            need = card
            break
        end
    end
    if (not need) then return end

    local names = self.player:getTag("ny_tenth_fengying"):toString():split("+")
    if (not names) or (#names <= 0) then return end

	for _,name in sgs.list(names)do
        local c = sgs.Sanguosha:cloneCard(name)
        c:addSubcard(need)
        local mark = string.format("ny_tenth_fengying_%s-Clear", name)
		if c and c:isAvailable(self.player)
		and self:getCardsNum(c:getClassName())<1
        and self.player:getMark(mark) == 0
		then
         	--local dummy = self:aiUseCard(c)
            local dummy = {isDummy=true,to=sgs.SPlayerList()}
            self:useCardByClassName(c, dummy)

    		if dummy.card
	    	and dummy.to
	     	then
	           	if c:canRecast()
				and dummy.to:length()<1
				then continue end
				self.ny_tenth_fengying_to = dummy.to
				sgs.ai_use_priority["#ny_tenth_fengying"] = sgs.ai_use_priority[c:getClassName()]
                local card_str = string.format("#ny_tenth_fengying:%s:%s",need:getEffectiveId(), name)
                return sgs.Card_Parse(card_str)
			end
		end
		if c then c:deleteLater() end
	end
end

sgs.ai_skill_use_func["#ny_tenth_fengying"] = function(card, use, self)
	use.card = card
	if use.to then use.to = self.ny_tenth_fengying_to end
end

sgs.ai_use_priority.ny_tenth_fengying = 6

sgs.ai_guhuo_card.ny_tenth_fengying = function(self,toname,class_name)
	if sgs.Sanguosha:getCurrentCardUseReason() == sgs.CardUseStruct_CARD_USE_REASON_RESPONSE then return end
    
    local names = self.player:getTag("ny_tenth_fengying"):toString():split("+")
    if (not names) or (#names <= 0) then return end

    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByKeepValue(cards)
    local need
    for _,card in ipairs(cards) do
        if card:getNumber() <= self.player:getMark("&ny_10th_jiao") then
            need = card
            break
        end
    end
    if (not need) then return end

    local pattern = toname
    if string.find(pattern, "slash") then pattern = "slash" end

    for _,name in ipairs(names) do
        if string.find(name, pattern) then
            local mark = string.format("ny_tenth_fengying_%s-Clear", name)
            if self.player:getMark(mark) == 0 then
                local card_str = string.format("#ny_tenth_fengying:%s:%s",need:getEffectiveId(), name)
                return card_str
            end
        end
    end
    return
end

--才思

sgs.ai_skill_invoke.ny_10th_caisi = true

--耽意

sgs.ai_skill_invoke.ny_10th_danyi = true

--文灿

local ny_tenth_wencan_skill = {}
ny_tenth_wencan_skill.name = "ny_tenth_wencan"
table.insert(sgs.ai_skills, ny_tenth_wencan_skill)
ny_tenth_wencan_skill.getTurnUseCard = function(self, inclusive)
    if self.player:hasUsed("#ny_tenth_wencan") then return end
    if #self.enemies == 0 then return end
    return sgs.Card_Parse("#ny_tenth_wencan:.:")
end

sgs.ai_skill_use_func["#ny_tenth_wencan"] = function(card, use, self)
    local targets = {}
    self:sort(self.enemies, "defense")
    if #self.enemies > 0 then
        for _,enemy in ipairs(self.enemies) do
            local can = true
            for _,target in ipairs(targets) do
                if target:getHp() == enemy:getHp() then
                    can = false
                    break
                end
            end
            if can then table.insert(targets, enemy) end
            if #targets >= 2 then break end
        end
        if #targets > 0 then
            use.card = card
            if use.to then
                for _,target in ipairs(targets) do
                    use.to:append(target)
                end
            end
        end
    end
end


sgs.ai_use_priority.ny_tenth_wencan = 8

sgs.ai_skill_use["@@ny_tenth_wencan"] = function(self, prompt)
    local target
    for _,p in sgs.qlist(self.room:getAlivePlayers()) do
        if p:getPhase() == sgs.Player_Play then
            target = p
            break
        end
    end
    if (not target) then return "." end
    local cards = {}
    local all = sgs.QList2Table(self.player:getCards("he"))
    if #all <= 3 then return "." end
    self:sortByKeepValue(all)
    for _,card in ipairs(all) do
        local can = true
        for _,cc in ipairs(cards) do
            if cc:getSuit() == card:getSuit() then
                can = false
                break
            end
        end
        if can then table.insert(cards, card) end
        if #cards >= 2 then break end
    end
    if #cards < 2 then return "." end
    if target:getHandcardNum() < self.player:getHandcardNum() then return "." end
    if self:isWeak() then
        return string.format("#ny_tenth_wencanDisCard:%s+%s:", cards[1]:getEffectiveId(), cards[2]:getEffectiveId())
    elseif target:getHandcardNum() > self.player:getHandcardNum() + 2 then
        return string.format("#ny_tenth_wencanDisCard:%s+%s:", cards[1]:getEffectiveId(), cards[2]:getEffectiveId())
    end
    return "."
end


--忘隙

sgs.ai_skill_invoke.ny_10th_wangxi = true

--贤淑·第二版

local ny_10th_xianshu_second_skill = {}
ny_10th_xianshu_second_skill.name = "ny_10th_xianshu_second"
table.insert(sgs.ai_skills, ny_10th_xianshu_second_skill)
ny_10th_xianshu_second_skill.getTurnUseCard = function(self, inclusive)
	for _,card in sgs.qlist(self.player:getHandcards()) do
        if card:hasFlag("ny_10th_konghou_second") then
	        return sgs.Card_Parse("#ny_10th_xianshu_second:.:")
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_xianshu_second"] = function(card, use, self)
	self:updatePlayers()
	
    local red = {}
    local black = {}
    local cards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByKeepValue(cards)
    for _,card in ipairs(cards) do
        if card:hasFlag("ny_10th_konghou_second") then
	        if card:isRed() then table.insert(red, card:getEffectiveId()) end
            if card:isBlack() then table.insert(black, card:getEffectiveId()) end
        end
    end

    local enduse = false
    if #red > 0 then
        self:sort(self.friends, "hp")
        for _,friend in ipairs(self.friends) do
            if friend:objectName() ~= self.player:objectName() 
            and friend:getHp() <= self.player:getHp() and friend:isWounded() then
                local card_str = string.format("#ny_10th_xianshu_second:%s:->%s", red[1], friend:objectName())
		        local acard = sgs.Card_Parse(card_str)
		        assert(acard)
		        use.card = acard
		        if use.to then
			        use.to:append(friend)
		        end
                enduse = true
                break
            end
        end
    end
    if #black > 0 and (not enduse) then
        self:sort(self.enemies, "hp")
        for _,enemy in ipairs(self.enemies) do
            if enemy:objectName() ~= self.player:objectName() 
            and enemy:getHp() >= self.player:getHp() then
                local card_str = string.format("#ny_10th_xianshu_second:%s:->%s", black[1], enemy:objectName())
		        local acard = sgs.Card_Parse(card_str)
		        assert(acard)
		        use.card = acard
		        if use.to then
			        use.to:append(enemy)
		        end
                break
            end
        end
    end
end

sgs.ai_use_priority.ny_10th_xianshu_second = 10

sgs.ai_card_intention.ny_10th_xianshu_second = function(self, card, from, tos)
	for _,to in ipairs(tos) do
        if to:getHp() > from:getHp() then
		    sgs.updateIntention(from, to, 80)
        elseif to:getHp() < from:getHp() then
            sgs.updateIntention(from, to, -80)
        else
            if card:isRed() then
                sgs.updateIntention(from, to, -80)
            elseif card:isBlack() then
                sgs.updateIntention(from, to, 80)
            end
        end
	end
end

--灵犀

sgs.ai_skill_discard.ny_10th_lingxi = function(self,max,min)
    local cards
    local need = self.player:getMaxHp()
    if self.player:getMark("ny_10th_lingxi_start") > 0 then
        if self.player:isNude() then return {} end
        cards = sgs.QList2Table(self.player:getCards("he"))
        self:sortByUseValue(cards, true)
        need = math.min(need, #cards - 2)
    else
        if self.player:isKongcheng() then return {} end
        cards = sgs.QList2Table(self.player:getCards("h"))
        self:sortByKeepValue(cards)
        need = math.min(need, self.player:getHandcardNum() - self.player:getMaxCards())
    end
    if need <= 0 then return {} end
    if #cards <= 0 then return {} end
    local put = {}
    for _,card in ipairs(cards) do
        table.insert(put, card:getEffectiveId())
        need = need - 1
        if need <= 0 then break end
    end
    return put
end

--知否

sgs.ai_skill_invoke.ny_tenth_zhifou = function(self, data)
    if #self.enemies <= 0 and self.player:getMark("ny_tenth_zhifou_put-Clear") > 0 then return false end
    if (self.player:getHandcardNum() > 8) 
    or (self.player:getHandcardNum() > (2 * self.player:getPile("ny_10th_yi"):length())) then return false end
    local need = self.player:getMark("ny_tenth_zhifou-Clear") + 1
    local suits = {}
    for _,id in sgs.qlist(self.player:getPile("ny_10th_yi")) do
        local suit = sgs.Sanguosha:getCard(id):getSuitString()
        if (not table.contains(suits, suit)) then
            table.insert(suits, suit)
        else
            need = need - 1
        end
    end
    local num = #suits
    if need > 0 then num = num - need end
    if num <= 0 then return false end
    num = num * 2
    if self:isWeak() and (self.player:getHandcardNum() < num) then return true end
    if (self.player:getHandcardNum() +self.player:getMark("ny_tenth_zhifou-Clear") + 1) > num then return false end
    return true
end

sgs.ai_skill_choice["ny_tenth_zhifou"] = function(self, choices, data)
	local items = choices:split("+")
    table.removeOne(items, "cancel")
    if #items == 1 then return items[1] end
    if #self.enemies > 0 then
        self:sort(self.enemies, "defense")
        for _,enemy in ipairs(self.enemies) do
            if enemy:getHp() == 1 and table.contains(items, "lose") then return "lose" end
        end
        local n = self.player:getPile("ny_10th_yi"):length()
        for _,enemy in ipairs(self.enemies) do
            if (not enemy:isNude()) and n <= 8 and table.contains(items, "put") then return "put" end
        end
        for _,enemy in ipairs(self.enemies) do
            if self:isWeak(enemy) and table.contains(items, "lose") then return "lose" end
        end
        return items[math.random(1, #items)]
    end
    local others = sgs.QList2Table(self.room:getOtherPlayers(self.player))
    self:sort(others, "defense")
    for _,other in ipairs(others) do
        if (not self:isFriend(other)) and table.contains(items, "put") then return "put" end
    end
    return "cancel"
end

sgs.ai_skill_use["@@ny_tenth_zhifou"] = function(self, prompt)
    local target
    local need = self.player:getMark("ny_tenth_zhifou-Clear") + 1
    if #self.enemies <= 0 and (not self.player:hasFlag("ny_tenth_zhifou_put")) then return "." end
    if #self.enemies <= 0 then
        local others = sgs.QList2Table(self.room:getOtherPlayers(self.player))
        self:sort(others, "defense")
        for _,other in ipairs(others) do
            if (not self:isFriend(other)) then
                target = other
                break
            end
        end
    else
        self:sort(self.enemies, "defense")
        if self.player:hasFlag("ny_tenth_zhifou_lose") then
            for _,enemy in ipairs(self.enemies) do
                if enemy:getHp() == 1 then
                    target = enemy
                    break
                end
            end
            if (not target) then target = self.enemies[1] end
        else
            for _,enemy in ipairs(self.enemies) do
                if not enemy:isNude() then
                    target = enemy
                    break
                end
            end
        end
    end
    if (not target) then return "." end

    local suits = {}
    local cards = {}
    local lastcards = {}
    for _,id in sgs.qlist(self.player:getPile("ny_10th_yi")) do
        local card = sgs.Sanguosha:getCard(id)
        local suit = card:getSuitString()
        if (not table.contains(suits, suit)) then
            table.insert(suits, suit)
            table.insert(lastcards, id)
        else
            table.insert(cards, id)
            need = need - 1
        end
        if need <= 0 then break end
    end
    if need > 0 then
        for _,id in ipairs(lastcards) do
            table.insert(cards, id)
            need = need - 1
            if need <= 0 then break end
        end
    end
    if need > 0 then return "." end

    return string.format("#ny_tenth_zhifou:%s:->%s", table.concat(cards, "+"), target:objectName())
end

--狡黠·第二版

sgs.ai_skill_invoke.ny_tenth_jiaoxia_second = function(self,data)
    if self:isWeak() then return false end
    local n = 8
    if self.room:getAlivePlayers():length() <= 4 then n = 5 end
    if self.player:getHandcardNum() > n then return false end
    if self:getCardsNum("Slash") >= #self.enemies then return false end
    return true
end

sgs.ai_skill_use["@@ny_tenth_jiaoxia_second"] = function(self, prompt)
    local card = sgs.Sanguosha:getCard(self.player:getMark("ny_tenth_jiaoxia_second_card"))
    --local use = self:aiUseCard(card)
    local use = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(card, use)
    if use.card then
        if use.to and use.to:length() > 0 then
            local tos = {}
            for _,p in sgs.qlist(use.to) do
                table.insert(tos, p:objectName())
            end
            return card:toString().."->"..table.concat(tos,"+")
        end
    end
end

--狐魅·第二版

local ny_10th_humei_second_skill = {}
ny_10th_humei_second_skill.name = "ny_10th_humei_second"
table.insert(sgs.ai_skills, ny_10th_humei_second_skill)
ny_10th_humei_second_skill.getTurnUseCard = function(self, inclusive)
    local choices = {"draw", "give", "recover"}
    for _,p in ipairs(choices) do
        if self.player:getMark("ny_10th_humei_second_tiansuan_remove_"..p.."-PlayClear") == 0 then
            return sgs.Card_Parse("#ny_10th_humei_second:.:"..p)
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_humei_second"] = function(card,use,self)
	local choices = {"draw", "give", "recover"}
    for _,p in ipairs(choices) do
        if self.player:getMark("ny_10th_humei_second_tiansuan_remove_"..p.."-PlayClear") == 0 then
            if p == "draw" then
                self:sort(self.friends, "handcard")
                local target = nil
                for _,friend in ipairs(self.friends) do
                    if friend:getHp() <= self.player:getMark("&ny_10th_humei_second-PlayClear") then
                        target = friend
                        break
                    end
                end
                if target then
                    use.card = sgs.Card_Parse("#ny_10th_humei_second:.:"..p)
                    if use.to then use.to:append(target) end
                    break
                end
            end

            if p == "give" then
                self:sort(self.enemies, "handcard")
                local target = nil
                for _,enemy in ipairs(self.enemies) do
                    if enemy:getHp() <= self.player:getMark("&ny_10th_humei_second-PlayClear") and (not enemy:isNude()) then
                        target = enemy
                        break
                    end
                end
                if target then
                    use.card = sgs.Card_Parse("#ny_10th_humei_second:.:"..p)
                    if use.to then use.to:append(target) end
                    break
                end
            end

            if p == "recover" then
                self:sort(self.friends, "hp")
                local target = nil
                for _,friend in ipairs(self.friends) do
                    if friend:getHp() <= self.player:getMark("&ny_10th_humei_second-PlayClear") and friend:isWounded() then
                        target = friend
                        break
                    end
                end
                if target then
                    use.card = sgs.Card_Parse("#ny_10th_humei_second:.:"..p)
                    if use.to then use.to:append(target) end
                    break
                end
            end
        end
    end
end

--彰才

sgs.ai_skill_invoke.ny_10th_zhangcai = true

--雄幕

sgs.ai_skill_invoke.ny_10th_xiongmu = true

sgs.ai_skill_discard.ny_10th_xiongmu = function(self,max,min)
    local player = self.player
	local cards = sgs.QList2Table(self.player:getCards("he"))
	self:sortByKeepValue(cards)
	local dis = {}
    local n = 12
	for _,card in ipairs(cards) do
        table.insert(dis, card:getEffectiveId())
        n = n - 1
        if n <= 0 then break end
    end
	return dis
end

--儒贤

local ny_10th_ruxian_skill = {}
ny_10th_ruxian_skill.name = "ny_10th_ruxian"
table.insert(sgs.ai_skills, ny_10th_ruxian_skill)
ny_10th_ruxian_skill.getTurnUseCard = function(self, inclusive)
	if self.player:getMark("ny_10th_ruxian_limit") == 0 then
		return sgs.Card_Parse("#ny_10th_ruxian:.:")
	end
end

sgs.ai_skill_use_func["#ny_10th_ruxian"] = function(card, use, self)
    use.card = card
end

sgs.ai_use_priority.ny_10th_ruxian = 10
sgs.ai_use_value.ny_10th_ruxian = 10

--龙魂

local ny_10th_longhun_skill = {}
ny_10th_longhun_skill.name = "ny_10th_longhun"
table.insert(sgs.ai_skills, ny_10th_longhun_skill)
ny_10th_longhun_skill.getTurnUseCard = function(self, inclusive)
    if self.player:getMark("&ny_10th_longhun-Clear") >= 20 then return end

    local cards = sgs.QList2Table(self.player:getCards("h"))
    cards = self:sortByUseValue(cards, true)
    for _,card in ipairs(cards) do
        if card:getSuit() == sgs.Card_Heart then
            local usecard = sgs.Sanguosha:cloneCard("peach", sgs.Card_SuitToBeDecided, -1)
            usecard:setSkillName("ny_10th_longhun")
            usecard:addSubcard(card)
            usecard:deleteLater()

            local usec = {isDummy=true,to=sgs.SPlayerList()}
            self:useCardByClassName(usecard, usec)
            if usec.card and self.player:isWounded() then
                self.ny_10th_longhun_to = usec.to
                local card_str = string.format("#ny_10th_longhun:%s:%s",card:getEffectiveId(), "peach")
                local acard = sgs.Card_Parse(card_str)
                return acard
            end
        end
        if card:getSuit() == sgs.Card_Diamond then
            if self.player:getWeapon() and (card:getEffectiveId() == self.player:getWeapon():getId())
            and card:isKindOf("Crossbow")  then
                if self.player:canSlashWithoutCrossbow() then
                    local slash = sgs.Sanguosha:cloneCard("fire_slash", sgs.Card_SuitToBeDecided, -1)
                    slash:setSkillName("ny_10th_longhun")
                    slash:addSubcard(card)
                    slash:deleteLater()
    
                    local usec = {isDummy=true,to=sgs.SPlayerList()}
                    self:useCardByClassName(slash, usec)
                    if usec.card and slash:isAvailable(self.player) then
                        self.ny_10th_longhun_to = usec.to
                        local card_str = string.format("#ny_10th_longhun:%s:%s",card:getEffectiveId(), "fire_slash")
                        local acard = sgs.Card_Parse(card_str)
                        return acard
                    end
                end
            else
                local slash = sgs.Sanguosha:cloneCard("fire_slash", sgs.Card_SuitToBeDecided, -1)
                slash:setSkillName("ny_10th_longhun")
                slash:addSubcard(card)
                slash:deleteLater()

                local usec = {isDummy=true,to=sgs.SPlayerList()}
                self:useCardByClassName(slash, usec)
                if usec.card and slash:isAvailable(self.player) then
                    self.ny_10th_longhun_to = usec.to
                    local card_str = string.format("#ny_10th_longhun:%s:%s",card:getEffectiveId(), "fire_slash")
                    local acard = sgs.Card_Parse(card_str)
                    return acard
                end
            end
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_longhun"] = function(card, use, self)
    use.card = card
    if use.to then use.to = self.ny_10th_longhun_to end
end

sgs.ai_use_priority.ny_10th_longhun = sgs.ai_use_priority.Slash

--[[sgs.ai_guhuo_card.ny_10th_longhun = function(self,toname,class_name)
    if self.player:getMark("&ny_10th_longhun-Clear") >= 20 then return end
    --if self:getCardsNum(class_name) > 0 then return end
    if class_name == "Peach" and self.player:getMark("Global_PreventPeach") > 0 then return end

    --local names = {"nullification", "jink", "fire_slash", "peach"}
    local names = {["Nullification"] = "nullification", ["Jink"] = "jink",
    ["Slash"] = "fire_slash", ["FireSlash"] = "fire_slash", ["Peach"] = "peach"}

    local pattern = names[class_name]
    if (not pattern) then return end
    
    local suit
    if pattern == "peach" then suit = sgs.Card_Heart
    elseif pattern == "nullification" then suit = sgs.Card_Spade
    elseif pattern == "jink" then suit = sgs.Card_Club
    elseif pattern == "fire_slash" then suit = sgs.Card_Diamond
    end
    local cards = sgs.QList2Table(player:getCards("he"))
    self:sortByUseValue(cards, true)
    for _,card in ipairs(cards) do
        if card:getSuitString() == suit then
            local card_str = string.format("#ny_10th_longhun:%s:%s",card:getEffectiveId(), pattern)
            return card_str
        end
    end
    return
end]]

--弱智，这一段加了可以回合外响应，但回合内直接弃牌过
--[[sgs.ai_cardsview_valuable.ny_10th_longhun = function(self,class_name,player)
    if player:getMark("&ny_10th_longhun-Clear") >= 20 then return end
    --if self:getCardsNum(class_name) > 0 then return end
    if class_name == "Peach" and player:getMark("Global_PreventPeach") > 0 then return end

    --local names = {"nullification", "jink", "fire_slash", "peach"}
    local names = {["Nullification"] = "nullification", ["Jink"] = "jink",
    ["Slash"] = "fire_slash", ["FireSlash"] = "fire_slash", ["Peach"] = "peach"}

    local pattern = names[class_name]
    if (not pattern) then return end
    
    local suit
    if pattern == "peach" then suit = sgs.Card_Heart
    elseif pattern == "nullification" then suit = sgs.Card_Spade
    elseif pattern == "jink" then suit = sgs.Card_Club
    elseif pattern == "fire_slash" then suit = sgs.Card_Diamond
    end
    local cards = sgs.QList2Table(player:getCards("h"))
    self:sortByUseValue(cards, true)
    for _,card in ipairs(cards) do
        if card:getSuitString() == suit then
            local card_str = string.format("#ny_10th_longhun:%s:%s",card:getEffectiveId(), pattern)
            return card_str
        end
    end
    return
end]]

--用旧龙魂糊弄过去完了，没限制就没限制吧，反正也基本触发不了
sgs.ai_view_as.ny_10th_longhun = function(card, player, card_place)
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	if player:getHp() > 1 or card_place == sgs.Player_PlaceSpecial then return end
	if card:getSuit() == sgs.Card_Diamond then
		return ("fire_slash:ny_10th_longhun[%s:%s]=%d"):format(suit, number, card_id)
	elseif card:getSuit() == sgs.Card_Club then
		return ("jink:ny_10th_longhun[%s:%s]=%d"):format(suit, number, card_id)
	elseif card:getSuit() == sgs.Card_Heart and player:getMark("Global_PreventPeach") == 0 then
		return ("peach:ny_10th_longhun[%s:%s]=%d"):format(suit, number, card_id)
	elseif card:getSuit() == sgs.Card_Spade then
		return ("nullification:ny_10th_longhun[%s:%s]=%d"):format(suit, number, card_id)
	end
end

--斩将

sgs.ai_skill_invoke.ny_10th_zhanjiang = function(self, data)
    for _, p in sgs.qlist(self.room:getAlivePlayers()) do
        local weapon = p:getWeapon()
        if weapon and weapon:objectName() == "QinggangSword" then
            if self:isEnemy(p) then return true end
            return false
        end
    end
    return false
end

--媦婉

local ny_10th_weiwan_skill = {}
ny_10th_weiwan_skill.name = "ny_10th_weiwan"
table.insert(sgs.ai_skills, ny_10th_weiwan_skill)
ny_10th_weiwan_skill.getTurnUseCard = function(self, inclusive)
    if self.player:hasUsed("#ny_10th_weiwan") then return end

    local cards = sgs.QList2Table(self.player:getCards("he"))
    cards = self:sortByUseValue(cards, true)
    for _,card in ipairs(cards) do
        if card:getTag("ny_10th_qin"):toBool() then
            local card_str = string.format("#ny_10th_weiwan:%s:", card:getEffectiveId())
            return sgs.Card_Parse(card_str)
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_weiwan"] = function(card, use, self)
    local target
    if #self.enemies == 0 then return end
    self:sort(self.enemies,"defense")
    for _,enemy in ipairs(self.enemies) do
        if (not enemy:isNude()) then
            target = enemy
            break
        end
    end
    if target then
        use.card = card
        if use.to then use.to:append(target) end
    end
end

sgs.ai_use_priority.ny_10th_weiwan = 8
sgs.ai_use_value.ny_10th_weiwan = 8
sgs.ai_card_intention.ny_10th_weiwan = 80

--四论

sgs.ai_skill_invoke.ny_tenth_silun = true

sgs.ai_skill_choice["ny_tenth_silun"] = function(self, choices, data)
    return "bottom"
end

sgs.ai_skill_use["@@ny_tenth_silun!"] = function(self, prompt)
    if self.player:isNude() then return "." end
    local cards = sgs.QList2Table(self.player:getCards("he"))
    self:sortByKeepValue(cards)
    local target
    local needturn = false
    local need = {}
    if #self.friends > 0 then
        self:sort(self.friends, "defense")
        for _,friend in ipairs(self.friends) do
            if (not friend:faceUp()) then 
                needturn = true
                table.insert(need, friend)
            end
        end
    end
    if needturn then
        for _,card in ipairs(cards) do
            if card:isKindOf("EquipCard") then
                local equip_index = card:getRealCard():toEquipCard():location()
                for _,to_select in ipairs(need) do
                    if to_select:hasEquipArea(equip_index) and (not to_select:getEquip(equip_index)) then
                        return string.format("#ny_tenth_silun:%d:->%s", card:getEffectiveId(), to_select:objectName())
                    end
                end
            end
        end
    end
    for _,card in ipairs(cards) do
        if card:isKindOf("EquipCard") then
            local equip_index = card:getRealCard():toEquipCard():location()
            if self.player:hasEquipArea(equip_index) and (not self.player:getEquip(equip_index)) then
                return string.format("#ny_tenth_silun:%d:->%s", card:getEffectiveId(), self.player:objectName())
            end
            for _,to_select in ipairs(self.friends) do
                if to_select:hasEquipArea(equip_index) and (not to_select:getEquip(equip_index)) then
                    return string.format("#ny_tenth_silun:%d:->%s", card:getEffectiveId(), to_select:objectName())
                end
            end
        end
    end
    if #self.enemies > 0 then
        self:sort(self.enemies, "defense")
        for _,card in ipairs(cards) do
            if card:isKindOf("DelayedTrick") then
                for _,to_select in ipairs(self.enemies) do
                    if to_select:hasJudgeArea() and (not to_select:containsTrick(card:objectName())) then
                        return string.format("#ny_tenth_silun:%d:->%s", card:getEffectiveId(), to_select:objectName())
                    end
                end
            end
        end
    end
    return string.format("#ny_tenth_silun:%d:", cards[1]:getEffectiveId())
end

--十计

sgs.ai_skill_invoke.ny_tenth_shiji = function(self, data)
    local target = data:toPlayer()
    if target and self:isFriend(target) then return true end
    return false
end

sgs.ai_skill_use["@@ny_tenth_shiji"] = function(self, prompt)
    if self.player:isKongcheng() then return "." end
    if self.player:getHandcardNum() == 1 then return false end
    local pattern = self.player:property("ny_tenth_shiji_card"):toString()
    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByKeepValue(cards)

    local card = sgs.Sanguosha:cloneCard(pattern, sgs.Card_SuitToBeDecided, -1)
    card:setSkillName("ny_tenth_shiji")
    card:addSubcard(cards[1])
    local use = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(card, use)
    if use.card then
        if use.to then
            local tos = {}
            for _,to in sgs.qlist(use.to) do
                table.insert(tos, to:objectName())
            end
            return card:toString().."->"..table.concat(tos, "+")
        else
            return card:toString()
        end
    end
    return "."
end

sgs.ai_skill_choice["ny_tenth_shiji"] = function(self, choices, data)
	local items = choices:split("+")
    if #items > 1 then
        table.removeOne(items, "cancel")
    end
    return items[#items]
end

--绡刃

sgs.ai_skill_invoke.ny_10th_xiaoren = true

sgs.ai_skill_playerchosen.ny_10th_xiaoren = function(self, targets)
    if self.player:getMark("ny_10th_xiaoren_recover") > 0 then
        if #self.friends > 0 then
            self:sort(self.friends, "defense")
            return self.friends[1]
        end
        return self.player
    end
    local can_select = sgs.QList2Table(targets)
    self:sort(can_select, "defense")
    for _,to in ipairs(can_select) do
        if (not self:isFriend(to)) then return to end
    end
    return nil
end

--婆娑

local ny_tenth_posuo_skill = {}
ny_tenth_posuo_skill.name = "ny_tenth_posuo"
table.insert(sgs.ai_skills, ny_tenth_posuo_skill)
ny_tenth_posuo_skill.getTurnUseCard = function(self, inclusive)
    if self.player:getMark("ny_tenth_posuo_disable-PlayClear") > 0 then return end

    local cards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByKeepValue(cards)
    for _,card in ipairs(cards) do
        local mark = string.format("ny_tenth_posuo_%s-PlayClear", card:getSuitString())
        if self.player:getMark(mark) == 0 then
            local key = string.format("ny_tenth_posuo_%s", card:getSuitString())
            local all = self.room:getTag(key):toString():split("+")
            if (not all) or (#all <= 0) then
                local card_str = "#ny_tenth_posuo_select:"..card:getEffectiveId()..":"
                return sgs.Card_Parse(card_str)
            else
                for _,pattern in ipairs(all) do
                    local newcard = sgs.Sanguosha:cloneCard(pattern, sgs.Card_SuitToBeDecided, -1)
                    newcard:addSubcard(card)
                    newcard:deleteLater()

                    local use = {isDummy=true,to=sgs.SPlayerList()}
                    self:useCardByClassName(newcard, use)
                    if use.card and newcard:isAvailable(self.player) then
                        self.ny_tenth_posuo_to = use.to
                        local card_str = string.format("#ny_tenth_posuo:%s:%s",card:getEffectiveId(), pattern)
                        local acard = sgs.Card_Parse(card_str)
                        return acard
                    end
                end
            end
        end
    end
end

sgs.ai_skill_use_func["#ny_tenth_posuo"] = function(card, use, self)
    use.card = card
    if use.to then use.to = self.ny_tenth_posuo_to end
end

sgs.ai_skill_use_func["#ny_tenth_posuo_select"] = function(card, use, self)
    use.card = card
end

sgs.ai_skill_choice["ny_tenth_posuo"] = function(self, choices, data)
	local items = choices:split("+")
    return "cancel"
end

sgs.ai_use_priority.ny_tenth_posuo = 8

--硝焰

sgs.ai_skill_discard.ny_10th_xiaoyan = function(self, max, min)
    if self.player:isNude() then return {} end
    local target = self.room:getTag("ny_10th_xiaoyan"):toPlayer()
    if target and self:isFriend(target) then
        if (not self:isWeak()) and (self.player:getHandcardNum() > 1) then
            local cards = sgs.QList2Table(self.player:getCards("h"))
            self:sortByUseValue(cards)
            return {cards[1]:getEffectiveId()}
        end
    end
    if (not self:isWeak()) then return {} end
    if (not self.player:isWounded()) then return {} end
    local cards = sgs.QList2Table(self.player:getCards("he"))
    self:sortByKeepValue(cards)
    return {cards[1]:getEffectiveId()}
end

--傲势

local ny_10th_aoshi_give_skill = {}
ny_10th_aoshi_give_skill.name = "ny_10th_aoshi_give"
table.insert(sgs.ai_skills, ny_10th_aoshi_give_skill)
ny_10th_aoshi_give_skill.getTurnUseCard = function(self, inclusive)
    if self.player:getKingdom() ~= "qun" then return end
    if self.player:hasUsed("#ny_10th_aoshi_give") then return end
    if self:isWeak() or self.player:getHandcardNum() <= 1 then return end
    for _,other in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if self:isFriend(other) and other:hasLordSkill("ny_10th_aoshi") then
            return sgs.Card_Parse("#ny_10th_aoshi_give:.:")
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_aoshi_give"] = function(card, use, self)
    local targets = {}
    for _,other in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if self:isFriend(other) and other:hasLordSkill("ny_10th_aoshi") then
            table.insert(targets, other)
        end
    end
    if #targets <= 0 then return end
    self:sort(targets, "defense")
    local target = targets[1]
    if target then
        local cards = sgs.QList2Table(self.player:getHandcards())
        if self:isWeak(target) then
            self:sortByKeepValue(cards)
        else
            self:sortByUseValue(cards, true)
        end
        local card_str = string.format("#ny_10th_aoshi_give:%s:", cards[#cards]:getEffectiveId())
        local acard = sgs.Card_Parse(card_str)
        use.card = acard
        if use.to then use.to:append(target) end
    end
end
 
sgs.ai_use_priority.ny_10th_aoshi_give = 8
sgs.ai_use_value.ny_10th_aoshi_give = 8
sgs.ai_card_intention.ny_10th_aoshi_give = -80

--纵势

local ny_tenth_zongshi_skill = {}
ny_tenth_zongshi_skill.name = "ny_tenth_zongshi"
table.insert(sgs.ai_skills, ny_tenth_zongshi_skill)
ny_tenth_zongshi_skill.getTurnUseCard = function(self, inclusive)
    if self:isWeak() or self.player:getHandcardNum() <= 1 then return end

    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByUseValue(cards, true)
    for i,card in ipairs(cards) do
        if (not card:isKindOf("BasicCard")) and (not card:isNDTrick()) then continue end
		local ccc = sgs.Sanguosha:cloneCard(card:objectName())
		ccc:setSkillName("ny_tenth_zongshi")
        for n,cc in ipairs(cards) do
            if cc:getSuit() == card:getSuit() and i~=n then
                ccc:addSubcard(cc)
            end
        end
		ccc:deleteLater()
		if ccc:subcardsLength()>0 and ccc:isAvailable(self.player) then
			local usec = {isDummy=true,to=sgs.SPlayerList()}
			self:useCardByClassName(ccc, usec)
			if usec.card then
				return sgs.Card_Parse(string.format("#ny_tenth_zongshi:%s:",card:getEffectiveId()))
			end
		end
    end
end

sgs.ai_skill_use_func["#ny_tenth_zongshi"] = function(card, use, self)
    use.card = card
end

sgs.ai_use_priority.ny_tenth_zongshi = 3
sgs.ai_use_value.ny_tenth_zongshi = 8

sgs.ai_skill_playerschosen.ny_tenth_zongshi = function(self, targets, max, min)
    local card = self.player:getTag("ny_tenth_zongshi"):toCard()

    local num = max
    local rtargets = sgs.SPlayerList()

    local usec = {isDummy=true,to=sgs.SPlayerList(),extra_target = max-1}
    self:useCardByClassName(card, usec)
    if usec.card then
        for _,to in sgs.qlist(usec.to) do
            rtargets:append(to)
            num = num - 1
            if num <= 0 then return rtargets end
        end
    end
    local items = sgs.QList2Table(targets)
    self:sort(items, "defense")
    local good = {"ex_nihilo", "amazing_grace", "god_salvation", "peach"}
    if table.contains(good, card:objectName()) then
        for _,to in ipairs(items) do
            if (not rtargets:contains(to)) and self:isFriend(to) then
                rtargets:append(to)
                num = num - 1
                if num <= 0 then return rtargets end
            end
        end
        return rtargets
    end

    for _,to in ipairs(items) do
        if (not rtargets:contains(to)) and self:isEnemy(to) then
            rtargets:append(to)
            num = num - 1
            if num <= 0 then return rtargets end
        end
    end
    return rtargets
end

sgs.ai_skill_use["@@ny_tenth_zongshi"] = function(self, prompt)
    if self:isWeak() or self.player:getHandcardNum() <= 1 then return end

    local cards = sgs.QList2Table(self.player:getHandcards())
    self:sortByUseValue(cards, true)
    for i,card in ipairs(cards) do
        if (not card:isKindOf("BasicCard")) and (not card:isNDTrick()) then continue end
		local cc = sgs.Sanguosha:cloneCard(card:objectName())
		cc:setSkillName("ny_tenth_zongshi")
        for n,c in ipairs(cards) do
            if c:getSuit() == card:getSuit() and i~=n then
                cc:addSubcard(c)
            end
        end
		cc:deleteLater()
		if cc:subcardsLength()>0 and cc:isAvailable(self.player) then
			local usec = {isDummy=true,to=sgs.SPlayerList()}
			self:useCardByClassName(cc, usec)
			if usec.card then
				return string.format("#ny_tenth_zongshi:%s:",card:getEffectiveId())
            end
        end
    end
end

--灵慧

sgs.ai_skill_use["@@ny_tenth_linghui"] = function(self, prompt)
    local cards = {}
    local num = 3
    for _,id in sgs.qlist(self.room:getDrawPile()) do
        local card = sgs.Sanguosha:getCard(id)
        table.insert(cards, card)
        num = num - 1
        if num <= 0 then break end
    end
    self:sortByUseValue(cards)
    for _,card in ipairs(cards) do
        local use = {isDummy=true,to=sgs.SPlayerList()}
        self:useCardByClassName(card, use)
        if use.card then
            local tos = {}
            for _,to in sgs.qlist(use.to) do
                table.insert(tos, to:objectName())
            end
            return string.format("#ny_tenth_linghui:%s:->%s", card:getEffectiveId(), table.concat(tos, "+"))
        end
    end
    return "."
end

--黠策

sgs.ai_skill_discard.ny_10th_xiace = function(self, max, min)
    if self.player:isNude() then return {} end
    if (not self.player:isWounded()) then return {} end
    local cards = sgs.QList2Table(self.player:getCards("he"))
    self:sortByKeepValue(cards)
    if #cards > 0 then return {cards[1]:getEffectiveId()} end
    return {}
end

sgs.ai_skill_playerchosen.ny_10th_xiace = function(self, targets)
    local now = self.room:getCurrent()
    if now and self:isEnemy(now) then return now end
    if #self.enemies <= 0 then return nil end
    self:sort(self.enemies, "defense")
    return self.enemies[1]
end

sgs.ai_playerchosen_intention.ny_10th_xiace = function(self, from, to)
	sgs.updateIntention(from, to, 80)
end

--御心

sgs.ai_skill_invoke.ny_10th_yuxin = function(self, data)
    local target = self.room:getTag("ny_10th_yuxin"):toPlayer()
    if (not target) or (not self:isFriend(target)) then return false end
    if self:getCardsNum("Peach") + target:getHp() > 0 then return false end
    return self:isFriend(target)
end

--殉别

sgs.ai_skill_invoke.ny_10th_xunbie = function(self, data)
    if self:getCardsNum("Peach") + self:getCardsNum("Analeptic") + self.player:getHp() > 0 then return false end
    return true
end

--婵娟

sgs.ai_skill_use["@@ny_tenth_chanjuan"] = function(self, prompt)
    local pattern = self.player:property("ny_tenth_chanjuan_card"):toString()
    local card = sgs.Sanguosha:cloneCard(pattern, sgs.Card_SuitToBeDecided, -1)
	card:setSkillName("ny_tenth_chanjuan")
    card:deleteLater()
    local use = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(card, use)
    if use.card then
        if use.to:length() > 0 then
            local tos = {}
            for _,to in sgs.qlist(use.to) do
                table.insert(tos, to:objectName())
            end
            return string.format("#ny_tenth_chanjuan:.:->%s",table.concat(tos, "+"))
        else
            return "#ny_tenth_chanjuan:.:"
        end
    end
    return "."
end

--壮魄

local ny_10th_zhuangpo_skill = {}
ny_10th_zhuangpo_skill.name = "ny_10th_zhuangpo"
table.insert(sgs.ai_skills, ny_10th_zhuangpo_skill)
ny_10th_zhuangpo_skill.getTurnUseCard = function(self, inclusive)
    local cards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByKeepValue(cards)
    for _,card in ipairs(cards) do
        local to_translate = ":"..card:objectName()
        local translation = sgs.Sanguosha:translate(to_translate)
        if card:isKindOf("Slash") or string.find(translation, "【杀】") then
            local duel = sgs.Sanguosha:cloneCard("duel", sgs.Card_SuitToBeDecided, -1)
            duel:addSubcard(card)
            duel:setSkillName("ny_10th_zhuangpo")
            duel:deleteLater()

            local usec = {isDummy=true,to=sgs.SPlayerList()}
            self:useCardByClassName(duel, usec)
            if usec.card then
                self.ny_10th_zhuangpo_to = usec.to
                local card_str = string.format("#ny_10th_zhuangpo:%s:", card:getEffectiveId())
                return sgs.Card_Parse(card_str)
            end
        end
    end
end

sgs.ai_skill_use_func["#ny_10th_zhuangpo"] = function(card, use, self)
	use.card = card
    if use.to then use.to = self.ny_10th_zhuangpo_to end
end

sgs.ai_use_priority.ny_10th_zhuangpo = sgs.ai_use_priority.Duel

sgs.ai_skill_invoke.ny_10th_zhuangpo = function(self, data)
    local target = self.player:getTag("ny_10th_zhuangpo"):toPlayer()
    return self:isEnemy(target)
end

sgs.ai_skill_choice["ny_10th_zhuangpo"] = function(self, choices, data)
	local items = choices:split("+")
    return items[#items]
end

--密运

sgs.ai_skill_playerchosen.ny_tenth_miyun = function(self, targets)
    local objects = sgs.QList2Table(targets) 
    self:sort(objects, "defense")
    for _,p in ipairs(objects) do
        if (not self:isFriend(p)) then return p end
    end
    return objects[#objects]
end

sgs.ai_skill_use["@@ny_tenth_miyun!"] = function(self, prompt)
    if #self.friends_noself == 0 then
        for _,cc in sgs.qlist(self.player:getHandcards()) do
            if cc:hasFlag("ny_tenth_miyun_an") then
                local others = sgs.QList2Table(self.room:getOtherPlayers(self.player))
                self:sort(others, "defendse")
                return string.format("#ny_tenth_miyun:%s:->%s", cc:getEffectiveId(), others[#others]:objectName())
            end
        end
    else
        self:sort(self.friends_noself, "defense")
        local give = {}
        for _,card in sgs.qlist(self.player:getHandcards()) do
            table.insert(give, card:getEffectiveId())
        end
        return string.format("#ny_tenth_miyun:%s:->%s", table.concat(give, "+"), self.friends_noself[1]:objectName())
    end
end

sgs.ai_card_intention.ny_tenth_miyun = -80

--胆迎

sgs.ai_cardsview_valuable.ny_10th_danyin = function(self,class_name,player)
    if player:getMark("ny_10th_danyin-Clear") > 0 then return end
	local HandPile = sgs.QList2Table(player:getHandcards())
	self:sortByKeepValue(HandPile)
    local find = false
	for _,c in sgs.list(HandPile)do
		if c:isKindOf(class_name) and (not self:isWeak()) then return end
        if c:hasFlag("ny_tenth_miyun_an") then find = true end
	end
    if (not find) then return end
	local can_use = {["Slash"] = "slash", ["Jink"] = "jink"}
	local name = can_use[class_name]
	if not name then return end
	return string.format("#ny_10th_danyin:.:%s", name)
end

local ny_10th_danyin_skill = {}
ny_10th_danyin_skill.name = "ny_10th_danyin"
table.insert(sgs.ai_skills, ny_10th_danyin_skill)
ny_10th_danyin_skill.getTurnUseCard = function(self)
    if self.player:getMark("ny_10th_danyin-Clear") > 0 then return end
	local HandPile = sgs.QList2Table(self.player:getHandcards())
    local find = false
	for _,c in sgs.list(HandPile)do
        if c:hasFlag("ny_tenth_miyun_an") then find = true end
	end
    if (not find) then return end

    local card = sgs.Sanguosha:cloneCard("slash", sgs.Card_SuitToBeDecided, -1)
	card:deleteLater()
	local usec = {isDummy=true,to=sgs.SPlayerList()}
	self:useCardByClassName(card, usec)
	if usec.card then
        self.ny_10th_danyin_to = usec.to
	    local card_str = string.format("#ny_10th_danyin:.:%s", "slash")
		return sgs.Card_Parse(card_str)
	end
end

sgs.ai_skill_use_func["#ny_10th_danyin"] = function(card, use, self)
	use.card = card
    if use.to then use.to = self.ny_10th_danyin_to end
end

sgs.ai_use_priority.ny_tenth_miyun = sgs.ai_use_priority.Slash

--德化

sgs.ai_skill_choice.ny_tenth_dehua = function(self, choices)
    local items = choices:split("+")
    if #items == 1 then return items[1] end
    table.removeOne(items, "slash")
    for _,item in ipairs(items) do
        local card = sgs.Sanguosha:cloneCard(item, sgs.Card_SuitToBeDecided, -1)
        card:deleteLater()
        local usec = {isDummy=true,to=sgs.SPlayerList()}
        self:useCardByClassName(card, usec)
        if usec.card then
            return item
        end
    end
    return items[math.random(1,#items)]
end

sgs.ai_skill_choice.ny_tenth_dehua_slash = function(self, choices)
    return "slash"
end

sgs.ai_skill_use["@@ny_tenth_dehua"] = function(self, prompt)
    local pattern = self.player:property("ny_tenth_dehua_card"):toString()
	local card = sgs.Sanguosha:cloneCard(pattern, sgs.Card_SuitToBeDecided, -1)
    card:setSkillName("ny_tenth_dehua")
    card:deleteLater()

    if card:targetFixed() then
        return string.format("#ny_tenth_dehua:.:%s",pattern)
    end

    local usec = {isDummy=true,to=sgs.SPlayerList()}
    self:useCardByClassName(card, usec)
    if usec.card then
        if usec.to:length() > 0 then
            local tos = {}
            for _,to in sgs.qlist(usec.to) do
                table.insert(tos, to:objectName())
            end
            return string.format("#ny_tenth_dehua:.:%s->%s",pattern, table.concat(tos, "+"))
        else
            return string.format("#ny_tenth_dehua:.:%s",pattern)
        end
    end
end

--抚黎

local ny_10th_fuli_skill = {}
ny_10th_fuli_skill.name = "ny_10th_fuli"
table.insert(sgs.ai_skills, ny_10th_fuli_skill)
ny_10th_fuli_skill.getTurnUseCard = function(self)
    if self.player:isNude() then return end
    if self.player:hasUsed("#ny_10th_fuli") then return end
    return sgs.Card_Parse("#ny_10th_fuli:.:")
end

sgs.ai_skill_use_func["#ny_10th_fuli"] = function(card, use, self)
	use.card = card
end

sgs.ai_skill_choice.ny_10th_fuli = function(self, choices)
    local items = choices:split("+")
    if #items == 1 then return items[1] end
    table.removeOne(items, "BasicCard")
    return items[math.random(1,#items)]
end

sgs.ai_skill_playerchosen.ny_10th_fuli = function(self, targets)
    local target = nil
    local min = 999
    for _,p in sgs.qlist(targets) do
        if self:isEnemy(p) and p:getAttackRange() >= 1 and p:getAttackRange() <= min then
            target = p
            min = p:getAttackRange()
        end
    end
    if not target then
        for _,p in sgs.qlist(targets) do
            if self:isEnemy(p) then
                target = p
            end
        end
    end
    return target
end

sgs.ai_playerchosen_intention.ny_10th_fuli = 40

--严教

local ny_tenth_yanjiao_skill = {}
ny_tenth_yanjiao_skill.name = "ny_tenth_yanjiao"
table.insert(sgs.ai_skills, ny_tenth_yanjiao_skill)
ny_tenth_yanjiao_skill.getTurnUseCard = function(self)
    if self.player:hasUsed("#ny_tenth_yanjiao") then return end
    if #self.friends_noself <= 0 then return end
    return sgs.Card_Parse("#ny_tenth_yanjiao:.:")
end

sgs.ai_skill_use_func["#ny_tenth_yanjiao"] = function(card, use, self)
    self:sort(self.friends_noself, "defense")
    if #self.friends_noself > 0 then
	    use.card = card
        if use.to then use.to:append(self.friends_noself[1]) end
    end
end

sgs.ai_use_priority.ny_tenth_yanjiao = 8
sgs.ai_card_intention.ny_tenth_yanjiao = -80

sgs.ai_skill_choice.ny_tenth_yanjiao = function(self, choices)
    local target = self.room:getCurrent()
    if target and self:isEnemy(target) then
        return "no"
    else
        return "yes"
    end
end

--省身

sgs.ai_skill_invoke.ny_10th_xingshen = true

--权谋

local ny_10th_quanmou_skill = {}
ny_10th_quanmou_skill.name = "ny_10th_quanmou"
table.insert(sgs.ai_skills, ny_10th_quanmou_skill)
ny_10th_quanmou_skill.getTurnUseCard = function(self)
    return sgs.Card_Parse("#ny_10th_quanmou:.:")
end

sgs.ai_skill_use_func["#ny_10th_quanmou"] = function(card, use, self)
    local target
    for _,p in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if p:getMark("ny_10th_quanmou_selelcted-PlayClear") == 0 
        and self.player:inMyAttackRange(p) then
            if self:isFriend(p) and self.player:getChangeSkillState("ny_10th_quanmou") <= 1 then
                target = p
                break
            else
                if (not self:isFriend(p)) and self.player:getChangeSkillState("ny_10th_quanmou") == 2 then
                    target = p
                    break
                end
            end
        end
    end

    if (not target) then
        for _,p in sgs.qlist(self.room:getOtherPlayers(self.player)) do
            if p:getMark("ny_10th_quanmou_selelcted-PlayClear") == 0 
            and self.player:inMyAttackRange(p) then
                target = p
                break
            end
        end
    end

    if target then
	    use.card = card
        if use.to then use.to:append(target) end
    end
end

sgs.ai_use_priority.ny_10th_quanmou = 10

sgs.ai_skill_playerschosen.ny_10th_quanmou = function(self, targets, max, min)
    local selected = sgs.SPlayerList()
    local n = max
    local can_choose = sgs.QList2Table(targets)
    self:sort(can_choose, "defense")
    for _,target in ipairs(can_choose) do
        if self:isEnemy(target) then
            selected:append(target)
            n = n - 1
        end
        if n <= 0 then break end
    end
    return selected
end

--渐专

sgs.ai_skill_choice.ny_10th_jianzhuan = function(self, choices)
    if string.find(choices, "disself") then
        return "disself="..self.player:getMark("&ny_10th_jianzhuan-PlayClear")
    elseif string.find(choices, "recast") then
        return "recast="..self.player:getMark("&ny_10th_jianzhuan-PlayClear")
    end
    local items = choices:split("+")
    return items[math.random(1,#items)]
end

sgs.ai_skill_playerchosen.ny_10th_jianzhuan = function(self, targets)
    if #self.enemies > 0 then
        self:sort(self.enemies, "defense")
        for _,enemy in ipairs(self.enemies) do
            if (not enemy:isNude()) then return enemy end
        end
    end
    local tos = self.room:getOtherPlayers(self.player)
    return tos[math.random(1, #tos)]
end

sgs.ai_playerchosen_intention.ny_10th_jianzhuan = 80

--覆斗

sgs.ai_skill_invoke.ny_10th_fudou = function(self, data)
    local use = self.player:getTag("ny_10th_fudou"):toCardUse()
    local target = use.to:at(0)
    return ((not self:isWeak()) and self:isEnemy(target) and use.card:isBlack())
    or (self:isFriend(target) and use.card:isRed())
end

--谏国·第二版

local ny_10th_jianguo_second_skill = {}
ny_10th_jianguo_second_skill.name = "ny_10th_jianguo_second"
table.insert(sgs.ai_skills, ny_10th_jianguo_second_skill)
ny_10th_jianguo_second_skill.getTurnUseCard = function(self, inclusive)
    if self.player:getMark("ny_10th_jianguo_second_tiansuan_remove_draw-PlayClear") == 0
    or self.player:getMark("ny_10th_jianguo_second_tiansuan_remove_dis-PlayClear") == 0 then
        return sgs.Card_Parse("#ny_10th_jianguo_second:.:")
    end
end

sgs.ai_skill_use_func["#ny_10th_jianguo_second"] = function(card,use,self)
    if self.player:getMark("ny_10th_jianguo_second_tiansuan_remove_draw-PlayClear") == 0 then
        if #self.friends > 0 then
            self:sort(self.friends, "handcard", true)
            use.card = sgs.Card_Parse("#ny_10th_jianguo_second:.:draw")
            if use.to then use.to:append(self.friends[1]) end
            return
        end
    end
    if self.player:getMark("ny_10th_jianguo_second_tiansuan_remove_dis-PlayClear") == 0 then
        if #self.enemies > 0 then
            self:sort(self.enemies, "handcard", true)
            use.card = sgs.Card_Parse("#ny_10th_jianguo_second:.:dis")
            if use.to then use.to:append(self.enemies[1]) end
            return
        end
    end
end

sgs.ai_use_priority.ny_10th_jianguo_second = 8

--置民

sgs.ai_skill_playerschosen.ny_10th_zhimin = function(self, targets, max, min)
    local selected = sgs.SPlayerList()
    local n = max
    local can_choose = sgs.QList2Table(targets)
    self:sort(can_choose, "defense")
    for _,target in ipairs(can_choose) do
        if self:isEnemy(target) or (not self:isFriend(target)) then
            selected:append(target)
            n = n - 1
        end
        if n <= 0 then break end
    end
    if (not selected:isEmpty()) then
        return selected
    end
    selected:append(can_choose[#can_choose])
    return selected
end

--拒谏

local ny_10th_jujian_skill = {}
ny_10th_jujian_skill.name = "ny_10th_jujian"
table.insert(sgs.ai_skills, ny_10th_jujian_skill)
ny_10th_jujian_skill.getTurnUseCard = function(self, inclusive)
    if (not self.player:hasUsed("#ny_10th_jujian")) then
        return sgs.Card_Parse("#ny_10th_jujian:.:")
    end
end

sgs.ai_skill_use_func["#ny_10th_jujian"] = function(card,use,self)
    local targets = self.room:getOtherPlayers(self.player)
    local tos = sgs.QList2Table(targets)
    self:sort(tos, "defense", true)
    for _,to in ipairs(tos) do
        if to:getKingdom() == "wei" and (self:isEnemy(to) and (not self:isWeak(to))) then
            use.card = card
            if use.to then use.to:append(to) end
            return
        end
    end
    for _,to in ipairs(tos) do
        if to:getKingdom() == "wei" and (self:isFriend(to)) then
            use.card = card
            if use.to then use.to:append(to) end
            return
        end
    end
end

--水淹七军

function SmartAI:useCardNYTShuiyanqijun(card,use)
	local target_num = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,card) + 2
    local targets = self.room:getAlivePlayers()
    local tos = sgs.SPlayerList()
    while(target_num > 0) do
        local target = self:findPlayerToDamage(1,self.player,"N",targets)[1]
        if not target or self:isFriend(target) then break end
        targets:removeOne(target)
        if not self.player:isProhibited(target, card) then
            target_num = target_num - 1
            tos:append(target)
        end
        if targets:isEmpty() then break end
    end
    if (not tos:isEmpty()) then
        use.card = card
        if use.to then use.to = tos end
    end
end

sgs.ai_use_priority.NYTShuiyanqijun = 6.4
sgs.ai_keep_value.NYTShuiyanqijun = 4.4
sgs.ai_use_value.NYTShuiyanqijun = 5.4
sgs.ai_card_intention.NYTShuiyanqijun = 50

--绝武

local ny_tenth_jvewu_skill = {}
ny_tenth_jvewu_skill.name = "ny_tenth_jvewu"
table.insert(sgs.ai_skills, ny_tenth_jvewu_skill)
ny_tenth_jvewu_skill.getTurnUseCard = function(self, inclusive)
    local alls = sgs.QList2Table(self.player:getCards("he"))
    self:sortByKeepValue(alls)
    local cards = {}
    for _,card in ipairs(alls) do
        if card:getNumber() == 2 then
            table.insert(cards, card)
        end
    end
    if #cards <= 0 then return end

    local names = {"_ny_tenth_shuiyanqijun"}
    for _,id in sgs.qlist(sgs.Sanguosha:getRandomCards()) do
        local card = sgs.Sanguosha:getEngineCard(id)
        if card:isKindOf("BasicCard") or card:isNDTrick() then
            if card:isDamageCard() then
                if (not table.contains(names, card:objectName())) then
                    table.insert(names, card:objectName())
                end
            end
        end
    end

    local able = {}
    for _,name in ipairs(names) do
        local card = sgs.Sanguosha:cloneCard(name)
        card:deleteLater()
        local mark = string.format("ny_tenth_jvewu_%s-Clear", card:objectName())
        if card:isKindOf("Slash") then mark = "ny_tenth_jvewu_Slash-Clear" end
        if self.player:getMark(mark) == 0 and card:isAvailable(self.player) then
            table.insert(able, name)
        end
    end
    if #able <= 0 then return end

    for _,pattern in ipairs(able) do
        local card = sgs.Sanguosha:cloneCard(pattern, sgs.Card_SuitToBeDecided, -1)
        card:addSubcard(cards[1])
        card:deleteLater()
        local types = {"BasicCard","TrickCard"}
        local dummy = {isDummy=true,to=sgs.SPlayerList()}
        self["use"..types[card:getTypeId()]](self,card,dummy)
        if dummy.card and dummy.to then
            self.ny_tenth_jvewu_to = dummy.to
            local card_str = string.format("#ny_tenth_jvewu:%s:%s",cards[1]:getEffectiveId(), pattern)
            return sgs.Card_Parse(card_str)
        end
    end
        
end

sgs.ai_skill_use_func["#ny_tenth_jvewu"] = function(card, use, self)
	use.card = card
    if use.to then use.to = self.ny_tenth_jvewu_to end
end

sgs.ai_cardsview_valuable.ny_tenth_jvewu = function(self, class_name, player)
	if self.player:isKongcheng() then return end
    if self.player:getMark("ny_tenth_jvewu_Slash-Clear") > 0 then return end
    if sgs.Sanguosha:getCurrentCardUseReason() == sgs.CardUseStruct_CARD_USE_REASON_RESPONSE then return false end
	local classname2objectname = {
		["Slash"] = "slash", ["FireSlash"] = "fire_slash", ["ThunderSlash"] = "thunder_slash",
	}
	local name = classname2objectname[class_name]
	if not name then return end
    for _,card in sgs.qlist(self.player:getHandcards()) do
        if card:isKindOf(class_name) then return end
    end
    local cards = sgs.QList2Table(self.player:getCards("he"))
    self:sortByKeepValue(cards)
    for _,card in ipairs(cards) do
        if card:getNumber() == 2 then
            local card_str = string.format("#ny_tenth_jvewu:%s:%s",card:getEffectiveId(), name)    
            return card_str
        end
    end
    return
end

sgs.ai_use_priority.ny_tenth_jvewu = 7

--武佑

local ny_tenth_wuyou_other_skill = {}
ny_tenth_wuyou_other_skill.name = "ny_tenth_wuyou_other"
table.insert(sgs.ai_skills, ny_tenth_wuyou_other_skill)
ny_tenth_wuyou_other_skill.getTurnUseCard = function(self, inclusive)
    if (self.player:isKongcheng() or self:isWeak()) and self.player:getHandcardNum() <= self.player:getMaxCards() then return end
    if (not self.player:hasUsed("#ny_tenth_wuyou")) then
        for _,p in ipairs(self.friends) do
            if p:hasSkill("ny_tenth_wuyou") then
                return sgs.Card_Parse("#ny_tenth_wuyou:.:")
            end
        end
    end
end

sgs.ai_skill_use_func["#ny_tenth_wuyou"] = function(card,use,self)
    local hands = sgs.QList2Table(self.player:getHandcards())
    self:sortByKeepValue(hands)
    self:sort(self.friends, "defense")
    for _,friend in ipairs(self.friends) do
        if friend:hasSkill("ny_tenth_wuyou") then
            local card_str = "#ny_tenth_wuyou:"..hands[1]:getEffectiveId()..":"
            use.card = sgs.Card_Parse(card_str)
            if use.to then use.to:append(friend) end
            return 
        end
    end
end

sgs.ai_card_intention.ny_tenth_wuyou = -40

sgs.ai_skill_discard.ny_tenth_wuyou = function(self, max, min)
    if self.player:isKongcheng() or self:isWeak() then return false end
    local target = self.room:getCurrent()
    if target and self:isFriend(target) then
        local hands = sgs.QList2Table(self.player:getHandcards())
        self:sortByKeepValue(hands)
        return (hands[1]:getEffectiveId())
    end
    return {}
end

sgs.ai_skill_choice.ny_tenth_wuyou = function(self, choices, data)
    local target = self.room:getCurrent()
    if (not target) or (not self:isWeak(target)) then
        local items = choices:split("+")
        local cards = {}
        for _,item in ipairs(items) do
            local card = sgs.Sanguosha:cloneCard(item, sgs.Card_SuitToBeDecided, -1)
            card:deleteLater()
            table.insert(cards, card)
        end
        self:sortByUseValue(cards)
        return cards[1]:objectName()
    end
    local items = choices:split("+")
    local cards = {}
    for _,item in ipairs(items) do
        local card = sgs.Sanguosha:cloneCard(item, sgs.Card_SuitToBeDecided, -1)
        card:deleteLater()
        table.insert(cards, card)
    end
    self:sortByKeepValue(cards)
    return cards[1]:objectName()
end

sgs.ai_use_priority.ny_tenth_wuyou = 4

--义贤

local ny_tenth_yixian_skill = {}
ny_tenth_yixian_skill.name = "ny_tenth_yixian"
table.insert(sgs.ai_skills, ny_tenth_yixian_skill)
ny_tenth_yixian_skill.getTurnUseCard = function(self, inclusive)
    if self.player:getMark("@ny_tenth_yixian_mark") == 0 then return end
    local field = 0
    local discardpile = 0
    if self.player:getEquips():length() > 0 then
        field = field + self.player:getEquips():length()
        if self.player:isWounded() then
            field = field + 1.5
            if self:isWeak() then field = field + 1 end
        end
    end
    for _,p in sgs.qlist(self.room:getOtherPlayers(self.player)) do
        if p:getEquips():length() > 0 then
            if self:isFriend(p) and p:isWounded() then 
                field = field + 2 
                if self:isWeak(p) then field = field + 1 end
            end
            if self:isEnemy(p) then
                field = field + p:getEquips():length()
            end
            field = field + 0.5*(p:getEquips():length())
        end
    end
    local equips = {}
    for i = 0, 4, 0 do
        if self.player:getEquip(i) then
            table.insert(equips, 1)
        else
            table.insert(equips, 0)
        end
    end
    for _,id in sgs.qlist(self.room:getDiscardPile()) do
        local card = sgs.Sanguosha:getCard(id)
        if card:isKindOf("EquipCard") then
            local equip_index = card:getRealCard():toEquipCard():location()
            if self.player:hasEquipArea(equip_index) and equips[equip_index] == 0 then
                equips[equip_index] = 1
                discardpile = discardpile + 1
            else
                if card:getNumber() == 2 then
                    discardpile = discardpile + 1
                end
            end
        end
    end
    if math.max(field, discardpile) < ((self.room:getAlivePlayers():length()) + 1) then return end
    if field >= discardpile then
        return sgs.Card_Parse("#ny_tenth_yixian:.:field")
    else
        return sgs.Card_Parse("#ny_tenth_yixian:.:discardpile")
    end
end

sgs.ai_skill_use_func["#ny_tenth_yixian"] = function(card,use,self)
    use.card = card
end

sgs.ai_use_priority.ny_tenth_yixian = 3.8

sgs.ai_skill_choice.ny_tenth_yixian = function(self, choices, data)
    local target = data:toPlayer()
    if target and self:isFriend(target) then
        return "buff="..target:getGeneralName()
    end
    return "cancel"
end 

--识草

local ny_10th_shicao_skill = {}
ny_10th_shicao_skill.name = "ny_10th_shicao"
table.insert(sgs.ai_skills, ny_10th_shicao_skill)
ny_10th_shicao_skill.getTurnUseCard = function(self, inclusive)
    if self.player:getMark("ny_10th_shicao_disable-Clear") > 0 then return end
    return sgs.Card_Parse("#ny_10th_shicao:.:")
end

sgs.ai_skill_use_func["#ny_10th_shicao"] = function(card,use,self)
    use.card = card
end

sgs.ai_use_priority.ny_10th_shicao = 10

sgs.ai_skill_choice.ny_10th_shicao_place = function(self, choices)
    local items = choices:split("+")
    return items[math.random(1,#items)]
end

sgs.ai_skill_choice.ny_10th_shicao_type = function(self, choices, data)
    local cardTypes = {"BasicCard","TrickCard","EquipCard"}
    local n = self.player:usedTimes("#ny_10th_shicao")
    if math.random(0,n) < 2 then
        local place = data:toString()
        if place == "top" then
            local card = sgs.Sanguosha:getCard(self.room:getDrawPile():at(0))
            return cardTypes[card:getTypeId()]
        else
            local card = sgs.Sanguosha:getCard(self.room:getDrawPile():at(self.room:getDrawPile():length() - 1))
            return cardTypes[card:getTypeId()]
        end
    end
    local items = choices:split("+")
    return items[math.random(1,#items)]
end