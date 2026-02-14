--激将
sgs.ai_choicemade_filter.OLJijiangCard = function(self,player,carduse)
	sgs.oljijiangsource = player
end

sgs.ai_skill_invoke.jijiang = function(self,data)
	if not self.player:isLord() then return end
	if sgs.oljijiangsource then return false end
	local asked = data:toStringList()
	local prompt = asked[2]
	if self:askForCard("slash",prompt,1)=="." then return false end

	local current = self.room:getCurrent()
	if self:isFriend(current) and current:getKingdom()=="shu" and self:getOverflow(current)>2 and not self:hasCrossbowEffect(current) then
		return true
	end

	local cards = self.player:getHandcards()
	for _,card in sgs.list(cards)do
		if isCard("Slash",card,self.player) then
			return false
		end
	end

	local lieges = self.room:getLieges("shu",self.player)
	if lieges:isEmpty() then return false end
	local has_friend = false
	for _,p in sgs.list(lieges)do
		if not self:isEnemy(p) then
			has_friend = true
			break
		end
	end
	return has_friend
end

sgs.ai_choicemade_filter.skillInvoke.oljijiang = function(self,player,promptlist)
	if promptlist[#promptlist]=="yes" then
		sgs.oljijiangsource = player
	end
end

local oljijiang_skill = {}
oljijiang_skill.name = "oljijiang"
table.insert(sgs.ai_skills,oljijiang_skill)
oljijiang_skill.getTurnUseCard = function(self)
	local lieges = self.room:getLieges("shu",self.player)
	if lieges:isEmpty() then return end
	local has_friend
	for _,p in sgs.list(lieges)do
		if self:isFriend(p) then
			return sgs.Card_Parse("@OLJijiangCard=.")
		end
	end
end

sgs.ai_skill_use_func.OLJijiangCard = function(card,use,self)
	self:sort(self.enemies,"defenseSlash")
	sgs.oljijiangtarget = {}
	local dummy_use = dummy()
	dummy_use.to = sgs.SPlayerList()
	if self.player:hasFlag("slashTargetFix") then
		for _,p in sgs.list(self.room:getOtherPlayers(self.player))do
			if p:hasFlag("SlashAssignee") then
				dummy_use.to:append(p)
			end
		end
	end
	local slash = dummyCard()
	self:useCardSlash(slash,dummy_use)
	if dummy_use.card and dummy_use.to:length()>0 then
		use.card = card
		for _,p in sgs.list(dummy_use.to)do
			table.insert(sgs.oljijiangtarget,p)
			use.to:append(p)
		end
	end
end

sgs.ai_use_value.OLJijiangCard = sgs.ai_use_value.JijiangCard+0.1
sgs.ai_use_priority.OLJijiangCard = sgs.ai_use_priority.JijiangCard+0.1

sgs.ai_card_intention.OLJijiangCard = function(self,card,from,tos)
	return sgs.ai_card_intention.JijiangCard(self,card,from,tos)
end

sgs.ai_choicemade_filter.cardResponded["@oljijiang-slash"] = function(self,player,promptlist)
	if promptlist[#promptlist]~="" then
		sgs.updateIntention(player,sgs.oljijiangsource,-40)
		sgs.oljijiangsource = nil
		sgs.oljijiangtarget = nil
	elseif sgs.oljijiangsource and player:objectName()==player:getRoom():getLieges("shu",sgs.oljijiangsource):last():objectName() then
		sgs.oljijiangsource = nil
		sgs.oljijiangtarget = nil
	end
end

sgs.ai_skill_cardask["@oljijiang-slash"] = function(self,data)
	if not sgs.oljijiangsource or not self:isFriend(sgs.oljijiangsource) then return "." end
	if self:needBear() then return "." end

	local jijiangtargets = {}
	for _,player in sgs.list(self.room:getAllPlayers())do
		if player:hasFlag("JijiangTarget") then
			if self:isFriend(player) and not self:needToLoseHp(player,sgs.oljijiangsource,dummyCard()) then return "." end
			table.insert(jijiangtargets,player)
		end
	end

	if #jijiangtargets==0 then
		return self:getCardId("Slash") or "."
	end

	self:sort(jijiangtargets,"defenseSlash")
	local slashes = self:getCards("Slash")
	for _,slash in ipairs(slashes)do
		for _,target in ipairs(jijiangtargets)do
			if not self:slashProhibit(slash,target,sgs.oljijiangsource) and self:slashIsEffective(slash,target,sgs.oljijiangsource) then
				return slash:toString()
			end
		end
	end
	return "."
end

function sgs.ai_cardsview_valuable.oljijiang(self,class_name,player)
	local current = self.room:getCurrent()
	if current:getKingdom()=="shu" and self:getOverflow(current)>2 and not self:hasCrossbowEffect(current)
	then
		self.player:setFlags("stack_overflow_jijiang")
		local isfriend = self:isFriend(current,player)
		self.player:setFlags("-stack_overflow_jijiang")
		if isfriend then return "@OLJijiangCard=." end
	end
	local cards = player:getHandcards()
	for _,card in sgs.list(cards)do
		if isCard("Slash",card,player) then return end
	end
	local has_friend = false
	for _,p in sgs.list(self.room:getLieges("shu",player))do
		self.player:setFlags("stack_overflow_jijiang")
		has_friend = self:isFriend(p,player)
		self.player:setFlags("-stack_overflow_jijiang")
		if has_friend then break end
	end
	if has_friend then return "@OLJijiangCard=." end
end

sgs.ai_skill_playerchosen.oljijiang = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets)
	for _,p in ipairs(targets)do
		if self:isFriend(p) and self:canDraw(p) then
			return p
		end
	end
	return nil
end

--替身
sgs.ai_skill_invoke.oltishen = function(self,data)
	return self:isWeak()
end

--龙胆
local ollongdan_skill={}
ollongdan_skill.name="ollongdan"
table.insert(sgs.ai_skills,ollongdan_skill)
ollongdan_skill.getTurnUseCard=function(self)
	local cards = self:addHandPile()
	self:sortByUseValue(cards,true)
	for _,c in ipairs(cards)do
		if c:isKindOf("Analeptic") then
			return sgs.Card_Parse(("peach:ollongdan[%s:%s]=%d"):format(c:getSuitString(),c:getNumberString(),c:getEffectiveId()))
		end
	end
	for _,c in ipairs(cards)do
		if c:isKindOf("Peach") then
			return sgs.Analeptic_IsAvailable(self.player) and sgs.Card_Parse(("analeptic:ollongdan[%s:%s]=%d"):format(c:getSuitString(),c:getNumberString(),c:getEffectiveId()))
		end
	end
	for _,c in ipairs(cards)do
		if c:isKindOf("Jink") then
			return sgs.Card_Parse(("slash:ollongdan[%s:%s]=%d"):format(c:getSuitString(),c:getNumberString(),c:getEffectiveId()))
		end
	end
end

sgs.ai_view_as.ollongdan = function(card,player,card_place)
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	if card_place==sgs.Player_PlaceHand then
		if card:isKindOf("Jink") then
			return ("slash:ollongdan[%s:%s]=%d"):format(suit,number,card_id)
		elseif card:isKindOf("Slash") then
			return ("jink:ollongdan[%s:%s]=%d"):format(suit,number,card_id)
		elseif card:isKindOf("Peach") then
			return ("analeptic:ollongdan[%s:%s]=%d"):format(suit,number,card_id)
		elseif card:isKindOf("Analeptic") then
			return ("peach:ollongdan[%s:%s]=%d"):format(suit,number,card_id)
		end
	end
end

sgs.ollongdan_keep_value = sgs.longdan_keep_value

--涯角
sgs.ai_skill_invoke.olyajiao = true

sgs.ai_skill_playerchosen.olyajiao = function(self,targets)
	local id = self.player:getMark("olyajiao")
	local card = sgs.Sanguosha:getCard(id)
	local cards = { card }
	local c,friend = self:getCardNeedPlayer(cards,self.friends)
	if friend then return friend end

	self:sort(self.friends)
	for _,friend in ipairs(self.friends)do
		if self:isValuableCard(card,friend) and not hasManjuanEffect(friend) and not self:needKongcheng(friend,true) then return friend end
	end
	for _,friend in ipairs(self.friends)do
		if self:isWeak(friend) and not hasManjuanEffect(friend) and not self:needKongcheng(friend,true) then return friend end
	end
	local trash = card:isKindOf("Disaster") or card:isKindOf("GodSalvation") or card:isKindOf("AmazingGrace")
	if trash then
		for _,enemy in ipairs(self.enemies)do
			if enemy:getPhase()>sgs.Player_Play and self:needKongcheng(enemy,true) and not hasManjuanEffect(enemy) then return enemy end
		end
	end
	for _,friend in ipairs(self.friends)do
		if not hasManjuanEffect(friend) and not self:needKongcheng(friend,true) then return friend end
	end
end

sgs.ai_playerchosen_intention.olyajiao = function(self,from,to)
	if not self:needKongcheng(to,true) and not hasManjuanEffect(to) then sgs.updateIntention(from,to,-50) end
end

sgs.ai_skill_playerchosen.olyajiao_discard = function(self,targets)
	return self:findPlayerToDiscard("hej",false,true,targets)[1]
end

--救援
sgs.ai_skill_playerchosen.oljiuyuan = function(self,targets)
	return sgs.ai_skill_playerchosen.tenyearjiuyuan(self,targets)
end

--博图
sgs.ai_skill_invoke.olbotu = true

--雷击
sgs.ai_skill_invoke.olleiji = true

sgs.ai_skill_playerchosen.olleiji = function(self,targets)
	return sgs.ai_skill_playerchosen.leiji(self,targets)
end

sgs.ai_playerchosen_intention.olleiji = sgs.ai_playerchosen_intention.leiji

function sgs.ai_slash_prohibit.olleiji(self,from,to,card) -- @todo: Qianxi flag name
	if self:isFriend(to) then return false end
	if to:hasSkills("hongyan|olhongyan") then return false end
	if to:hasFlag("QianxiTarget") and (not self:hasEightDiagramEffect(to) or self.player:hasWeapon("QinggangSword")) then return false end
	local hcard = to:getHandcardNum()
	if self:canLiegong(to,from) then return false end
	if from:getRole()=="rebel" and to:isLord()
	then
		local other_rebel
		for _,player in sgs.list(self.room:getOtherPlayers(from))do
			if sgs.ai_role[player:objectName()]=="rebel"
			or self:compareRoleEvaluation(player,"rebel","loyalist")=="rebel"
			then
				other_rebel = player
				break
			end
		end
		if not other_rebel and self.player:getHp()>=4
		and (self:getCardsNum("Peach")>0  or self.player:hasSkills("ganglie|neoganglie"))
		then
			return false
		end
	end

	if getKnownCard(to,self.player,"Jink",true)>=1 or (self:hasSuit("spade",true,to) and hcard>=2) or hcard>=4 then return true end
	if not from then
		from = self.room:getCurrent()
	end
	if self:hasEightDiagramEffect(to) and not IgnoreArmor(from,to) then return true end
end

--鬼道
sgs.ai_skill_cardask["@olguidao-card"]=function(self,data)
	local all_cards = sgs.ai_skill_cardask["@guidao-card"](self,data)
	if all_cards~="." then return all_cards end
	all_cards = self:addHandPile("he")
	if #all_cards<1 then return "." end
	local cards = {}
	for _,c in sgs.list(all_cards)do
		if c:getSuit()==0 and c:getNumber()>1 and c:getNumber()<10
		then table.insert(cards,c) end
	end
	local judge = data:toJudge()
	if #cards>0 then
		local id = self:getRetrialCardId(cards,judge,nil,true)
		if id>-1 then
			if self:needRetrial(judge)
			or judge.card:isKindOf("Jink") and self:getCardsNum("Jink")<1
			or judge.card:getSuit()==0 and self:getSuitNum("spade",true)<1
			or self:getUseValue(judge.card)>self:getUseValue(sgs.Sanguosha:getCard(id))
			or #self.enemies>0 and judge.who==self.player and self.player:hasSkill("olleiji")
			then return id end
		end
	end
	cards = {}
	for _,c in sgs.list(all_cards)do
		if c:isBlack() then table.insert(cards,c) end
	end
	if #cards>0 then
		local id = self:getRetrialCardId(cards,judge,nil,true)
		if id>-1 then
			if self:needRetrial(judge)
			or judge.card:isKindOf("Jink") and self:getCardsNum("Jink")<1
			or judge.card:getSuit()==0 and self:getSuitNum("spade",true)<1
			or self:getUseValue(judge.card)>self:getUseValue(sgs.Sanguosha:getCard(id))
			or #self.enemies>0 and judge.who==self.player and self.player:hasSkill("olleiji")
			then return id end
		end
	end
	return "."
end

function sgs.ai_cardneed.olguidao(to,card,self)
	return sgs.ai_cardneed.guidao(to,card,self)
end

sgs.ai_suit_priority.olguidao = sgs.ai_suit_priority.guidao

sgs.ai_useto_revises.olguidao = function(self,card,use,p)
	if card:isKindOf("Lightning")
	then
		if self:isFriend(p) and p:getCardCount()>2
		then use.card = card return end
		return false
	end
end

--黄天
local olhuangtianv_skill = {}
olhuangtianv_skill.name = "olhuangtian_attach"
table.insert(sgs.ai_skills,olhuangtianv_skill)
olhuangtianv_skill.getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = sgs.QList2Table(cards)
	local card
	self:sortByUseValue(cards,true)
	if self:getCardsNum("Jink","h")>1 then
		for _,acard in ipairs(cards)  do
			if acard:isKindOf("Jink") then
				card = acard
				break
			end
		end
	end
	if self:getOverflow(self.player)>0 then
		for _,acard in ipairs(cards)  do
			if acard:getSuit()==sgs.Card_Spade then
				card = acard
				break
			end
		end
	end
	if not card then return nil end
	return sgs.Card_Parse("@OLHuangtianCard="..card:getEffectiveId())
end

sgs.ai_skill_use_func.OLHuangtianCard = function(card,use,self)
	if self:needBear() then
		return "."
	end
	local targets = {}
	for _,friend in ipairs(self.friends_noself)do
		if friend:hasLordSkill("olhuangtian") and friend:getMark("olhuangtian-PlayClear")<=0 then
			if not hasManjuanEffect(friend) then
				table.insert(targets,friend)
			end
		end
	end
	if #targets>0 then --黄天己方
		use.card = card
		self:sort(targets,"defense")
		use.to:append(targets[1])
	elseif self:getCardsNum("Slash","he")>=2 then --黄天对方
		for _,enemy in ipairs(self.enemies)do
			if enemy:hasLordSkill("olhuangtian") and enemy:getMark("olhuangtian-PlayClear")<=0 then
				if not hasManjuanEffect(enemy,true) then
					if enemy:isKongcheng() and not enemy:hasSkill("kongcheng") and not hasTuntianEffect(enemy,true) then --必须保证对方空城，以保证天义/陷阵的拼点成功
						table.insert(targets,enemy)
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

sgs.ai_card_intention.OLHuangtianCard = function(self,card,from,tos)
	return sgs.ai_card_intention.HuangtianCard(self,card,from,tos)
end

sgs.ai_use_priority.OLHuangtianCard = sgs.ai_use_priority.HuangtianCard
sgs.ai_use_value.OLHuangtianCard = sgs.ai_use_value.HuangtianCard

--蛊惑
sgs.ai_skill_choice.olguhuo = function(self,choices,data)
	local yuji = data:toPlayer()
	if not yuji or self:isEnemy(yuji) then return "noquestion" end
	local guhuoname = self.room:getTag("OLGuhuoType"):toString()
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

local olguhuo_skill = {}
olguhuo_skill.name = "olguhuo"
table.insert(sgs.ai_skills,olguhuo_skill)
olguhuo_skill.getTurnUseCard = function(self)
	if self.player:isKongcheng() then return end
    local cards = self.player:getCards("h")
    cards = self:sortByKeepValue(cards,nil,true) -- 按保留值排序
	local Guhuo_str = {}
	for _,card in ipairs(cards)do
		if card:isNDTrick() or card:isKindOf("BasicCard")
		then
			table.insert(Guhuo_str,"@OLGuhuoCard="..card:getId()..":"..card:objectName())
		end
	end
	local peach = sgs.ai_guhuo_card.olguhuo(self,"peach","Peach")
	if peach then table.insert(Guhuo_str,peach) end
	local question = #self.enemies
	for _,enemy in ipairs(self.enemies)do
		if enemy:hasSkill("hunzi") and enemy:getMark("hunzi")<1
		or enemy:hasSkill("chanyuan")
		then question = question-1 end
	end
	local ratio = question<1 and 100 or (#self.enemies/question)
	if #Guhuo_str>0
	and ratio<100
	then
		for i=1,5 do
			local guhuo_str = Guhuo_str[math.random(1,#Guhuo_str)]
			local user = dummyCard(guhuo_str:split(":")[2])
			guhuo_str = sgs.Card_Parse(guhuo_str)
			user:setSkillName("olguhuo")
			user:addSubcards(guhuo_str:getSubcards())
			if user:isAvailable(self.player)
			then
				local dummy = self:aiUseCard(user)
				if dummy.card and dummy.to
				then
					if user:canRecast()
					and dummy.to:length()<1
					then continue end
					self.guhuo_to = dummy.to
					if user:isKindOf("ExNihilo") and math.random(1,3)<=ratio then return guhuo_str
					elseif math.random(1,4)<=ratio then return guhuo_str end
				end
			end
		end
	end
	if math.random(1,5)<=3*ratio
	then
		for c,pn in ipairs(patterns())do
			c = dummyCard(pn)
			if c and (c:getTypeId()==1 or c:isNDTrick())
			and self:getRestCardsNum(c:getClassName())>0
			and c:isDamageCard() then
				c:setSkillName("olguhuo") 
				c:addSubcard(cards[1])
				if c:isAvailable(self.player)
				then
					local dummy = self:aiUseCard(c)
					if dummy.card and dummy.to
					then
						if c:canRecast()
						and dummy.to:length()<1
						then continue end
						self.guhuo_to = dummy.to
						return sgs.Card_Parse("@OLGuhuoCard="..cards[1]:getId()..":"..c:objectName())
					end
				end
			end
		end
		for c,pn in ipairs(patterns())do
			c = dummyCard(pn)
			if c and (c:getTypeId()==1 or c:isNDTrick())
			and self:getRestCardsNum(c:getClassName())>0
			then
				c:setSkillName("olguhuo") 
				c:addSubcard(cards[1])
				if c:isAvailable(self.player)
				then
					local dummy = self:aiUseCard(c)
					if dummy.card and dummy.to
					then
						if c:canRecast()
						and dummy.to:length()<1
						then continue end
						self.guhuo_to = dummy.to
						return sgs.Card_Parse("@OLGuhuoCard="..cards[1]:getId()..":"..c:objectName())
					end
				end
			end
		end
	end
	if self:isWeak()
	then
		ratio = sgs.ai_guhuo_card.olguhuo(self,"peach","Peach")
		if ratio
		then
			local dummy = dummyCard("peach")
			if dummy:isAvailable(self.player)
			then
				dummy = self:aiUseCard(dummy)
				if dummy.card and dummy.to
				then
					self.guhuo_to = dummy.to
					return sgs.Card_Parse(ratio)
				end
			end
		end
	end
	ratio = sgs.ai_guhuo_card.olguhuo(self,"slash","Slash")
	if ratio and self:slashIsAvailable()
	then
		local dummy = self:aiUseCard(dummyCard())
		if dummy.card and dummy.to
		then
			self.guhuo_to = dummy.to
		   	return sgs.Card_Parse(ratio)
		end
	end
end

sgs.ai_skill_use_func.OLGuhuoCard=function(card,use,self)
	use.card = card
   	use.to = self.guhuo_to
end

sgs.ai_use_priority.OLGuhuoCard = sgs.ai_use_priority.GuhuoCard

sgs.ai_guhuo_card.olguhuo = function(self,toname,class_name)
	if self.player:isKongcheng()
	or self.player:getMark("olguhuo-Clear")>0
	then return end
    local hcards = self.player:getCards("h")
    hcards = self:sortByKeepValue(hcards,nil,true) -- 按保留值排序
    local hc,fake = {},{}
    local all = self.room:getMode()=="_mini_48"
    local question = #self.enemies
   	for _,enemy in sgs.list(self.enemies)do
    	if enemy:hasSkill("chanyuan")
    	or enemy:hasSkill("hunzi") and enemy:getMark("hunzi")<1
    	then question = question-1 end
    end
    if question<1 then all = true end
	for _,c in sgs.list(hcards)do
    	if c:isKindOf(class_name)
		then
			table.insert(hc,c)
    		if self:getCardsNum(class_name)>0
			then table.insert(fake,c) end
		end
    end
    question = question<1 and 100 or (#self.enemies/question)
    if all then hc = hcards end
    if #hc>1 or #hc>0 and all
    then
    	local index = 1
    	if not all and (class_name=="Peach" or class_name=="Analeptic" or class_name=="Jink")
    	then index = #hc end
    	return "@OLGuhuoCard="..hc[index]:getEffectiveId()..":"..toname
    end
    if #fake>0 and math.random(1,5)<=question
    then return "@OLGuhuoCard="..fake[1]:getEffectiveId()..":"..toname end
    if #hc>0 then return "@OLGuhuoCard="..hc[1]:getEffectiveId()..":"..toname end
	if self:isWeak()
	then
		if (class_name=="Peach" or class_name=="Analeptic")
		and self:getCardsNum({"Analeptic","Peach"})<1
		then return "@OLGuhuoCard="..hcards[1]:getEffectiveId()..":"..toname
		elseif class_name=="Jink"
		then return "@OLGuhuoCard="..hcards[1]:getEffectiveId()..":"..toname end
	end
	if class_name=="Jink" and math.random(1,#hcards+1)<(#hcards+1)/2
	then return "@OLGuhuoCard="..hcards[1]:getEffectiveId()..":"..toname
	elseif class_name=="Slash" and math.random(1,#hcards+2)>(#hcards+1)/2
	then return "@OLGuhuoCard="..hcards[1]:getEffectiveId()..":"..toname
	elseif class_name=="Peach" or class_name=="Analeptic"
	then
		question = self.room:getCurrentDyingPlayer()
		if question and self:isFriend(question) and math.random(1,#hcards+1)>(#hcards+1)/2
		then return "@OLGuhuoCard="..hcards[1]:getEffectiveId()..":"..toname end
    end
end

sgs.ai_skill_choice.olguhuo_saveself = function(self,choices)
	return sgs.ai_skill_choice.guhuo_saveself(self,choices)
end

sgs.ai_skill_choice.olguhuo_slash = function(self,choices)
	return sgs.ai_skill_choice.guhuo_slash(self,choices)
end

sgs.ai_skill_discard.olguhuo = function(self,discard_num,min_num,optional,include_equip)
	if self.player:hasSkill("zhaxiang") and self:canDraw() and not self:willSkipPlayPhase() and (self.player:getHp()>0 or hasBuquEffect(self.player) or self:getSaveNum(true)>0) then
		return {}
	end
	return self:askForDiscard("dummy",1,1,false,include_equip)
end

--设变
sgs.ai_skill_invoke.olshebian = function(self,data)
	local from,card,to = self:moveField(nil,"e")
	if from and card and to then
		sgs.ai_skill_playerchosen.olshebian_from = from
		sgs.ai_skill_cardchosen.olshebian = card:getId()
		sgs.ai_skill_playerchosen.olshebian_to = to
		return true
	end
	return false
end

--奇谋
local olqimou_skill = {}
olqimou_skill.name = "olqimou"
table.insert(sgs.ai_skills,olqimou_skill)
olqimou_skill.getTurnUseCard = function(self)
	if #self.enemies<=0 then return end
	if self.player:getHp()+self:getCardsNum("Peach")+self:getCardsNum("Analeptic")<2 then return end
	return sgs.Card_Parse("@OLQimouCard=.")
end

sgs.ai_skill_use_func.OLQimouCard = function(card,use,self)
	self.olqimou_lose = 0
	local slashcount = self:getCardsNum("Slash")-1
	self.olqimou_lose = math.min(slashcount,self.player:getHp())
	if self.olqimou_lose<=0 then return end
	
	self.room:addDistance(self.player,-self.olqimou_lose)
	local slash = self:getCard("Slash")
	if not slash then self.room:addDistance(self.player,self.olqimou_lose) return end
	
	local dummy_use = dummy()
	if slash then self:useBasicCard(slash,dummy_use) end
	self.room:addDistance(self.player,self.olqimou_lose)
	if not dummy_use.card or dummy_use.to:isEmpty() then return end
	use.card = card
end

sgs.ai_skill_choice.olqimou = function(self,choices)
	choices = choices:split("+")
	local num = self.olqimou_lose
	for _,choice in ipairs(choices)do
		if tonumber(choice)==num then return choice end
	end
	return choices[1]
end

sgs.ai_use_priority.OLQimouCard = sgs.ai_use_priority.TenyearQimouCard

--天香
sgs.ai_skill_use["@@oltianxiang"] = function(self,prompt)
	return sgs.ai_skill_use["@@tenyeartianxiang"](self,prompt)
end

--红颜
sgs.ai_suit_priority.olhongyan = sgs.ai_suit_priority.hongyan

--飘零
sgs.ai_skill_invoke.olpiaoling = true

sgs.ai_skill_playerchosen.olpiaoling = function(self,targets)
	local card = self.player:getTag("olpiaoling"):toCard()
	local cards = { card }
	local c,friend = self:getCardNeedPlayer(cards,self.friends)
	if friend then return friend end

	self:sort(self.friends)
	for _,friend in ipairs(self.friends)do
		if self:isValuableCard(card,friend) and not hasManjuanEffect(friend) and not self:needKongcheng(friend,true)
		then return friend end
	end
	for _,friend in ipairs(self.friends)do
		if self:isWeak(friend) and not hasManjuanEffect(friend) and not self:needKongcheng(friend,true)
		then return friend end
	end
	if card:isKindOf("Disaster") or card:isKindOf("GodSalvation") or card:isKindOf("AmazingGrace") then
		for _,enemy in ipairs(self.enemies)do
			if enemy:getPhase()>sgs.Player_Play and self:needKongcheng(enemy,true) and not hasManjuanEffect(enemy)
			then return enemy end
		end
	end
	for _,friend in ipairs(self.friends)do
		if not hasManjuanEffect(friend) and not self:needKongcheng(friend,true)
		then return friend end
	end
end

sgs.ai_playerchosen_intention.olpiaoling = function(self,from,to)
	if not self:needKongcheng(to,true) and not hasManjuanEffect(to) then sgs.updateIntention(from,to,-50) end
end

sgs.ai_skill_discard.olpiaoling = function(self,discard_num,min_num,optional,include_equip)
	return self:askForDiscard("dummyreason",1,1,false,include_equip)
end

--乱击
local olluanji_skill = {}
olluanji_skill.name = "olluanji"
table.insert(sgs.ai_skills,olluanji_skill)
olluanji_skill.getTurnUseCard = function(self)
	local cards = self:addHandPile()
	if #cards>=2
	then
		self:sortByKeepValue(cards)
		local useAll = false
		for _,enemy in ipairs(self.enemies)do
			if enemy:getHp()==1 and not enemy:hasArmorEffect("Vine")
			and not self:hasEightDiagramEffect(enemy) and self:damageIsEffective(enemy,nil,self.player)
			and self:isWeak(enemy) and getCardsNum("Jink",enemy,self.player)+getCardsNum("Peach",enemy,self.player)+getCardsNum("Analeptic",enemy,self.player)==0
			then useAll = true end
		end
		for _,fcard in ipairs(cards)do
			if isCard("Peach",fcard,self.player)
			or isCard("ExNihilo",fcard,self.player)
			or isCard("ArcheryAttack",fcard,self.player)
			or useAll and isCard("ArcheryAttack",fcard,self.player)
			then continue end
			for _,scard in ipairs(cards)do
				if scard:getSuit()~=fcard:getSuit()
				or fcard==scard
				or isCard("Peach",scard,self.player)
				or isCard("ExNihilo",scard,self.player)
				or isCard("ArcheryAttack",scard,self.player)
				or useAll and isCard("ArcheryAttack",scard,self.player)
				then continue end
				local archery = dummyCard("archery_attack")
				archery:setSkillName("olluanji")
				archery:addSubcard(fcard)
				archery:addSubcard(scard)
				local dummy_use = self:aiUseCard(archery)
				if dummy_use.card then return archery end
			end
		end
	end
end

sgs.ai_skill_playerchosen.olluanji = function(self,targets)
	local use = self.player:getTag("olluanji_data"):toCardUse()
	if not use then return nil end
	self:sort(self.friends_noself)
	local lord = self.room:getLord()
	if lord and lord:objectName()~=self.player:objectName() and self:isFriend(lord) and self:isWeak(lord) and self:hasTrickEffective(use.card,lord,self.player) then
		return lord
	end
	for _,friend in ipairs(self.friends_noself)do
		if self:hasTrickEffective(use.card,friend,self.player) then
			return friend
		end
	end
	return nil
end

--血裔
sgs.ai_skill_invoke.olxueyi = function(self,data)
	if not self:canDraw() then return false end
	if self:willSkipPlayPhase() then return false end
	local num = 2  --偷懒不管别的摸牌数了，也不管手里的【无中生有】了
	if self:willSkipDrawPhase() then
		num = 0
	end
	if self.player:getHandcardNum()>=4 then return false end
	if self.player:getHandcardNum()+1+num<self.player:getMaxCards()-2 then return true end
	return false
end

--血裔-二版
sgs.ai_skill_invoke.secondolxueyi = function(self,data)
	if not self:canDraw() then return false end
	if self.player:getHandcardNum()>=4 then return false end
	if self.player:getHandcardNum()+1<self.player:getMaxCards()-1 then return true end
	return false
end

--火计
local olhuoji_skill={}
olhuoji_skill.name="olhuoji"
table.insert(sgs.ai_skills,olhuoji_skill)
olhuoji_skill.getTurnUseCard=function(self)
	local cards = self:addHandPile("he")
	local card
	self:sortByUseValue(cards,true)
	for _,acard in ipairs(cards)do
		if acard:isRed() and not acard:isKindOf("Peach") and (self:getDynamicUsePriority(acard)<sgs.ai_use_value.FireAttack or self:getOverflow()>0) then
			if acard:isKindOf("Slash") and self:getCardsNum("Slash")==1 then
				local keep
				local dummy_use = dummy()
				self:useBasicCard(acard,dummy_use)
				if dummy_use.card and dummy_use.to and dummy_use.to:length()>0 then
					for _,p in sgs.list(dummy_use.to)do
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
	return sgs.Card_Parse(("fire_attack:olhuoji[%s:%s]=%d"):format(suit,number,card_id))
end

sgs.ai_cardneed.olhuoji = function(to,card,self)
	return sgs.ai_cardneed.huoji(to,card,self)
end

sgs.ai_skill_cardask["olhuoji0"] = function(self,data,pattern)
	local effect = data:toCardEffect()
    local cards = self.player:getCards("h")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for _,h in sgs.list(cards)do
		if sgs.Sanguosha:matchExpPattern(pattern,self.player,h)
		and (not self:isFriend(effect.to) or effect.to:isChained())
		and self:damageIsEffective(effect.to,effect.card)
    	then return h:getEffectiveId() end
	end
    return "."
end

--看破
sgs.ai_view_as.olkanpo = function(card,player,card_place)
	local suit = card:getSuitString()
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	if card_place==sgs.Player_PlaceHand or card_place==sgs.Player_PlaceEquip then
		if card:isBlack() then
			return ("nullification:olkanpo[%s:%s]=%d"):format(suit,number,card_id)
		end
	end
end

sgs.ai_cardneed.olkanpo = function(to,card,self)
	return sgs.ai_cardneed.kanpo(to,card,self)
end

sgs.olkanpo_suit_value = sgs.kanpo_suit_value

--连环
local ollianhuan_skill={}
ollianhuan_skill.name="ollianhuan"
table.insert(sgs.ai_skills,ollianhuan_skill)
ollianhuan_skill.getTurnUseCard = function(self)
	local cards = self:addHandPile("he")
	local card
	self:sortByUseValue(cards,true)
	local slash = self:getCard("FireSlash") or self:getCard("ThunderSlash") or self:getCard("Slash")
	if slash then
		local dummy_use = dummy()
		self:useBasicCard(slash,dummy_use)
		if not dummy_use.card then slash = nil end
	end
	for _,acard in ipairs(cards)do
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
	return sgs.Card_Parse(("iron_chain:ollianhuan[club:%s]=%d"):format(number,card_id))
end

sgs.ai_cardneed.ollianhuan = function(to,card)
	return sgs.ai_cardneed.lianhuan(to,card)
end

--涅槃
sgs.ai_skill_invoke.olniepan = function(self,data)
	return sgs.ai_skill_invoke.niepan(self,data)
end

sgs.ai_skill_choice.olniepan = function(self,choices)
	choices = choices:split("+")
	if self.player:hasArmorEffect("EightDiagram") or self.player:getArmor() then
		if table.contains(choices,"bazhen") then table.removeOne(choices,"bazhen") end
	end
	if table.contains(choices,"bazhen") then return "bazhen" end
	return choices[math.random(1,#choices)]
end

--鞬出
sgs.ai_skill_invoke.oljianchu = function(self,data)
	return sgs.ai_skill_invoke.tenyearjianchu(self,data)
end

--酣战
sgs.ai_skill_invoke.olhanzhan = function(self,data)
	local to = data:toPlayer()
	if to and not self:isFriend(to) then return true end
	return false
end

--二版酣战
sgs.ai_skill_invoke.secondolhanzhan = function(self,data)
	return sgs.ai_skill_invoke.olhanzhan(self,data)
end

sgs.ai_skill_use["@@secondolhanzhan"] = function(self,prompt,method)
	local ids = self.player:getTag("secondolhanzhanForAI"):toIntList()
	for _,id in sgs.qlist(ids)do
		local card = sgs.Sanguosha:getCard(id)
		if not self.player:canUse(card) then continue end
		local dummyuse = dummy()
		self:useCardByClassName(card,dummyuse)
		if not dummyuse.to:isEmpty() then
			return "@SecondOLHanzhanCard="..id
		end
	end
	if not self:canDraw() then return "." end
	return "@SecondOLHanzhanCard="..ids:at(0)
end

--武烈
sgs.ai_skill_use["@@olwulie"] = function(self,prompt,method)
	local num = self.player:getHp()-1+self:getSaveNum(true)
	if num<=0 and hasBuquEffect(self.player) then
		local buqu,nosbuqu
		if self.player:hasSkill("buqu") then buqu = 4-self.player:getPile("buqu") end
		if self.player:hasSkill("nosbuqu") then nosbuqu = 4-self.player:getPile("nosbuqu") end
		num = math.max(nosbuqu,buqu)
	end
	num = math.min(num,#self.friends_noself)
	if num<=0 then return "." end
	
	self:sort(self.friends_noself)
	local friends = {}
	for i = 1,num do
		table.insert(friends,self.friends_noself[i]:objectName())
	end
	if #friends>0 then return "@OLWulieCard=.->"..table.concat(friends,"+") end
	return "."
end

--酒池
local oljiuchi_skill={}
oljiuchi_skill.name="oljiuchi"
table.insert(sgs.ai_skills,oljiuchi_skill)
oljiuchi_skill.getTurnUseCard=function(self)
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
	local number = card:getNumberString()
	local card_id = card:getEffectiveId()
	local analeptic = sgs.Card_Parse(("analeptic:oljiuchi[spade:%s]=%d"):format(number,card_id))
	assert(analeptic)
	if sgs.Analeptic_IsAvailable(self.player,analeptic) then
		return analeptic
	end
end

sgs.ai_view_as.oljiuchi = function(card,player,card_place)
	local str = sgs.ai_view_as.jiuchi(card,player,card_place)
	if not str or str=="" or str==nil then return end
	return string.gsub(str,"jiuchi","oljiuchi")
end

function sgs.ai_cardneed.oljiuchi(to,card,self)
	return sgs.ai_cardneed.jiuchi(to,card,self)
end

--暴虐
sgs.ai_skill_invoke.olbaonue = function(self,data)
--	local to = data:toPlayer()
	return self.player:isWounded()--to and self:isFriend(to)
end

--化身
sgs.ai_skill_invoke.olhuashen = function(self,data)
	return sgs.ai_skill_invoke.huashen(self,data)
end

function sgs.ai_skill_choice.olhuashen(self,choices,data,xiaode_choice)
	local items = choices:split("+")
	if table.contains(items,"exchangetwo")
	then
		local sk = {}
		local huashens = self.player:getTag("Huashens"):toStringList()
		for _,hs in sgs.list(huashens)do
			local general = sgs.Sanguosha:getGeneral(hs)
			for _,s in sgs.list(general:getVisibleSkillList())do
				table.insert(sk,s:objectName())
			end
		end
		sk = table.concat(sk,"+")
		sk = sgs.ai_skill_choice.huashen(self,sk,data,xiaode_choice)
		if sk and self.player:hasSkill(sk)
		then return "exchangetwo" end
	end
	if table.contains(items,"change") then return "change" end
	return sgs.ai_skill_choice.huashen(self,choices,data,xiaode_choice)
end

--放权
sgs.ai_skill_invoke.olfangquan = function(self,data)
	return sgs.ai_skill_invoke.fangquan(self,data)
end

sgs.ai_skill_use["@@olfangquan"] = function(self,prompt)
	local str = sgs.ai_skill_use["@@fangquan"](self,prompt)
	if not str or str=="" or str==nil then return "." end
	return string.gsub(str,"FangquanCard","OLFangquanCard")
end

--思蜀
sgs.ai_skill_playerchosen.olsishu = function(self,targets)
	targets = sgs.QList2Table(targets)
	self:sort(targets,"handcard")
	targets = sgs.reverse(targets)
	for _,p in ipairs(targets)do
		if not self:isFriend(p) or p:getMark("&olsishu")>0 then continue end
		if p:containsTrick("indulgence") then
			if p:containsTrick("YanxiaoCard") or (p:hasSkill("qiaobian") and p:canDiscard(p,"h")) then continue end
			return p
		end
	end
	for _,p in ipairs(targets)do
		if not self:isFriend(p) or p:getMark("&olsishu")>0 then continue end
		if p:containsTrick("indulgence") then
			return p
		end
	end
	for _,p in ipairs(targets)do
		if not self:isFriend(p) or p:getMark("&olsishu")>0 then continue end
		return p
	end
	return nil
end

--制霸
local olzhiba_skill = {}
olzhiba_skill.name = "olzhiba"
table.insert(sgs.ai_skills,olzhiba_skill)
olzhiba_skill.getTurnUseCard = function(self)
	if self:needBear() then return end
	return sgs.Card_Parse("@OLZhibaCard=.")
end

sgs.ai_use_priority.OLZhibaCard = 7

sgs.ai_skill_use_func.OLZhibaCard = function(card,use,self)
	local enemies = {}
	for _,p in ipairs(self.enemies)do
		if p:getKingdom()=="wu" and self.player:canPindian(p) then
			table.insert(enemies,p)
		end
	end
	if #enemies<=0 then return end
	self:sort(enemies,"defense")
	
	local max_card = self:getMaxCard()
	if not max_card then return end
	local point = max_card:getNumber()
	if self.player:hasSkill("tianbian") and max_card:getSuit()==sgs.Card_Heart then point = 13 end
	
	for _,p in ipairs(enemies)do
		local zhiba_str
		
		local enemy_max_card = self:getMaxCard(p)
		if not enemy_max_card then continue end
		local enemy_point = max_card:getNumber()
		if p:hasSkill("tianbian") and enemy_max_card:getSuit()==sgs.Card_Heart then enemy_point = 13 end

		if point>10 and point>enemy_point then
			if isCard("Jink",max_card,self.player) and self:getCardsNum("Jink")==1 then return end
			if isCard("Peach",max_card,self.player) or isCard("Analeptic",max_card,self.player) then return end
			self.olzhiba_card = max_card
			zhiba_str = "@OLZhibaCard=."
		end

		if zhiba_str then
			use.card = sgs.Card_Parse(zhiba_str)
			use.to:append(p)
			return
		end
	end
end

sgs.ai_skill_choice.olzhiba_pindian_obtain = function(self,choices)
	if self.player:isKongcheng() and self:needKongcheng() then return "reject" end
	return "obtainPindianCards"
end

function sgs.ai_skill_pindian.olzhiba(minusecard,self,requestor,maxcard)
	local cards,maxcard = sgs.QList2Table(self.player:getHandcards())
	local function compare_func(a,b)
		return a:getNumber()>b:getNumber()
	end
	table.sort(cards,compare_func)
	for _,card in ipairs(cards)do
		if self:getUseValue(card)<6 then maxcard = card break end
	end
	return maxcard or cards[1]
end

sgs.ai_card_intention.OLZhibaCard = 50
sgs.ai_choicemade_filter.pindian.olzhiba = 50

--制霸拼点
local olzhiba_pindian_skill = {}
olzhiba_pindian_skill.name = "olzhiba_pindian"
table.insert(sgs.ai_skills,olzhiba_pindian_skill)
olzhiba_pindian_skill.getTurnUseCard = function(self)
	if self:needBear() or self:getOverflow()<=0 then return end
	return sgs.Card_Parse("@OLZhibaPindianCard=.")
end

sgs.ai_use_priority.OLZhibaPindianCard = 0

sgs.ai_skill_use_func.OLZhibaPindianCard = function(card,use,self)
	local lords = {}
	for _,player in sgs.list(self.room:getOtherPlayers(self.player))do
		if player:hasLordSkill("olzhiba") and self.player:canPindian(player) and player:getMark("olzhiba-PlayClear")<=0 and self:isFriend(player) then
			table.insert(lords,player)
		end
	end
	if #lords==0 then return end
	self:sort(lords,"defense")
	for _,lord in ipairs(lords)do
		local zhiba_str
		local cards = self.player:getHandcards()

		local max_num,max_card= 0,nil
		local min_num,min_card = 14,nil
		for _,hcard in sgs.list(cards)do
			if hcard:getNumber()>max_num then
				max_num = hcard:getNumber()
				max_card = hcard
			end

			if hcard:getNumber()<=min_num then
				if hcard:getNumber()==min_num then
					if min_card and self:getKeepValue(hcard)>self:getKeepValue(min_card) then
						min_num = hcard:getNumber()
						min_card = hcard
					end
				else
					min_num = hcard:getNumber()
					min_card = hcard
				end
			end
		end

		local lord_max_num,lord_max_card = 0,nil
		local lord_min_num,lord_min_card = 14,nil
		local lord_cards = lord:getHandcards()
		local flag = string.format("%s_%s_%s","visible",global_room:getCurrent():objectName(),lord:objectName())
		for _,lcard in sgs.list(lord_cards)do
			if (lcard:hasFlag("visible") or lcard:hasFlag(flag)) and lcard:getNumber()>lord_max_num then
				lord_max_card = lcard
				lord_max_num = lcard:getNumber()
			end
			if lcard:getNumber()<lord_min_num then
				lord_min_num = lcard:getNumber()
				lord_min_card = lcard
			end
		end

		if not lord:hasSkill("manjuan") and ((lord_max_num>0 and min_num<=lord_max_num) or min_num<7) then
			if isCard("Jink",min_card,self.player) and self:getCardsNum("Jink")==1 then return end
			self.olzhiba_pindian_card = min_card
			zhiba_str = "@OLZhibaPindianCard=."
		end

		if zhiba_str then
			use.card = sgs.Card_Parse(zhiba_str)
			use.to:append(lord)
			return
		end
	end
end

sgs.ai_skill_choice.olzhiba_pindian = function(self,choices)
	local who = self.room:getCurrent()
	local cards = self.player:getHandcards()
	local has_large_number,all_small_number = false,true
	for _,c in sgs.list(cards)do
		if c:getNumber()>11 then
			has_large_number = true
			break
		end
	end
	for _,c in sgs.list(cards)do
		if c:getNumber()>4 then
			all_small_number = false
			break
		end
	end
	if all_small_number or (self:isEnemy(who) and not has_large_number) then return "reject"
	else return "accept"
	end
end

sgs.ai_skill_choice.olzhiba_pindian_obtain = function(self,choices)
	if self.player:isKongcheng() and self:needKongcheng() then return "reject" end
	return "obtainPindianCards"
end

function sgs.ai_skill_pindian.olzhiba_pindian(minusecard,self,requestor,maxcard)
	local cards,maxcard = sgs.QList2Table(self.player:getHandcards())
	local function compare_func(a,b)
		return a:getNumber()>b:getNumber()
	end
	table.sort(cards,compare_func)
	for _,card in ipairs(cards)do
		if self:getUseValue(card)<6 then maxcard = card break end
	end
	return maxcard or cards[1]
end

sgs.ai_card_intention.OLZhibaPindianCard = 0

sgs.ai_choicemade_filter.pindian.olzhiba_pindian = function(self,from,promptlist)
	local number = sgs.Sanguosha:getCard(tonumber(promptlist[4])):getNumber()
	local lord = self.room:findPlayerByObjectName(promptlist[5])
	if not lord then return end
	local lord_max_card = self:getMaxCard(lord)
	if lord_max_card and lord_max_card:getNumber()>=number then sgs.updateIntention(from,lord,-60)
	elseif lord_max_card and lord_max_card:getNumber()<number then sgs.updateIntention(from,lord,60)
	elseif number<6 then sgs.updateIntention(from,lord,-60)
	elseif number>8 then sgs.updateIntention(from,lord,60)
	end
end

--屯田
sgs.ai_skill_invoke.oltuntian = function(self,data)
	return sgs.ai_skill_invoke.tuntian(self,data)
end

--趫猛
sgs.ai_skill_invoke.olqiaomeng = function(self,data)
	local player = data:toPlayer()
	if self:isFriend(player) then
		if player:getArmor() and self:needToThrowArmor(player) and self.player:canDiscard(player,player:getArmor():getEffectiveId()) then return true end
		if self.player:canDiscard(player,"j") and not player:containsTrick("YanxiaoCard") then
			if player:containsTrick("indulgence") or player:containsTrick("supply_shortage") then return true end
			if (player:containsTrick("lightning") and self:getFinalRetrial(player)==2) or #self.enemies==0 then return true end
		end
		return false
	end
	return self:doDisCard(player,"e")
end

addAiSkills("olchangbiao").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards,nil,true)
	if #cards<1 then return end
	local ids = {}
   	local fs = dummyCard()
	fs:setSkillName("olchangbiao")
  	for _,c in sgs.list(cards)do
		if self:getKeepValue(c)>3
		or #ids>=#cards/2 then continue end
		table.insert(ids,c:getEffectiveId())
		fs:addSubcard(c)
	end
	if #ids<1 and #cards>1
	then
		table.insert(ids,cards[1]:getEffectiveId())
		fs:addSubcard(cards[1])
	end
	local dummy = self:aiUseCard(fs)
	if fs:isAvailable(self.player)
	and dummy.card
	and dummy.to
	and #ids>0
  	then
		self.olcb_to = dummy.to
		ids = #ids>0 and table.concat(ids,"+") or "."
		return sgs.Card_Parse("@OLChangbiaoCard="..ids)
	end
end

sgs.ai_skill_use_func["OLChangbiaoCard"] = function(card,use,self)
	use.card = card
	use.to = self.olcb_to
end

sgs.ai_use_value.OLChangbiaoCard = 9.4
sgs.ai_use_priority.OLChangbiaoCard = 2.8

addAiSkills("oltiaoxin").getTurnUseCard = function(self)
	return sgs.Card_Parse("@OLTiaoxinCard=.")
end

sgs.ai_skill_use_func["OLTiaoxinCard"] = function(card,use,self)
	self:sort(self.enemies,"handcard")
	for _,ep in sgs.list(self.enemies)do
		if ep:inMyAttackRange(self.player)
		and ep:getCardCount()>0
		then
			if self:getCardsNum("Jink","h")>0
			or self.player:getHandcardNum()>ep:getHandcardNum()
			then
				use.card = card
				use.to:append(ep)
				return
			end
		end
	end
end

sgs.ai_use_value.OLTiaoxinCard = 9.4
sgs.ai_use_priority.OLTiaoxinCard = 4.8

sgs.ai_skill_use["@@olzaiqi"] = function(self,prompt)
	local valid = {}
	local destlist = self.room:getAllPlayers()
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist,"hp")
	local n = self.player:getMark("olzaiqi-Clear")
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
    	return string.format("@OLZaiqiCard=.->%s",table.concat(valid,"+"))
	end
end

sgs.ai_skill_choice.olzaiqi = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if self:isFriend(target)
	and self:isWeak(target)
	then return items[2] end
	return items[1]
end

addAiSkills("olduanliang").getTurnUseCard = function(self)
	local cards = self:addHandPile("he")
	cards = self:sortByKeepValue(cards,nil,true)
  	for _,c in sgs.list(cards)do
	   	if c:getTypeId()~=1 and c:getTypeId()~=3 then continue end
		local fs = dummyCard("SupplyShortage")
		fs:setSkillName("olduanliang")
		fs:addSubcard(c)
		if fs:isAvailable(self.player)
		and c:isBlack() then return fs end
	end
end

sgs.ai_skill_playerchosen.oljiezi = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_choice.olqiaobian = function(self,choices)
	local items = choices:split("+")
	self.olqiaobian_phase = items[1]:split("=")[2]
	local can = false
	if #items>2
	then
		if self.olqiaobian_phase=="judge"
		and self:doDisCard(self.player,"j")
		and self:getCardsNum("Nullification")<1
		then can = true end
		if self.olqiaobian_phase=="draw"
		then
			local n = 0
			for _,ep in sgs.list(self.enemies)do
				if self:doDisCard(ep,"h")
				then n = n+1 end
			end
			can = n>1
		end
		if self.olqiaobian_phase=="play"
		then
			for _,fp in sgs.list(self.friends)do
				if can then break end
				self.olqiaobian_to = fp
				for _,j in sgs.list(fp:getJudgingArea())do
					if can then break end
					if self:doDisCard(fp,j:getEffectiveId())
					then
						self.olqiaobian_ej = j
						for _,ep in sgs.list(self.enemies)do
							if ep:containsTrick(j:objectName())
							then continue end
							can = true
						end
						for _,ep in sgs.list(self.room:getAllPlayers())do
							if ep:containsTrick(j:objectName())
							or self:isFriend(ep)
							then continue end
							can = true
						end
					end
				end
			end
			for _,ep in sgs.list(self.enemies)do
				if can then break end
				self.olqiaobian_to = ep
				for _,e in sgs.list(ep:getEquips())do
					if can then break end
					self.olqiaobian_ej = e
					local n = e:getRealCard():toEquipCard():location()
					for _,fp in sgs.list(self.friends)do
						if self:doDisCard(ep,e:getEffectiveId())
						and not fp:getEquip(n)
						then can = true end
					end
				end
			end
			can = can and #self:getTurnUse()<3
		end
		if self.olqiaobian_phase=="discard"
		and self.player:getHandcardNum()>self.player:getMaxCards()
		then can = true end
		if can
		then
			if self.player:getCardCount()>3
			and self.player:getMark("&olzhbian")<3
			then return items[1] end
			return items[2]
		end
	else
		if self.olqiaobian_phase=="judge"
		and self:doDisCard(self.player,"j")
		then can = true end
		if self.olqiaobian_phase=="draw"
		then
			local n = 0
			for _,ep in sgs.list(self.enemies)do
				if self:doDisCard(ep,"h")
				then n = n+1 end
			end
			can = n>1
		end
		if self.olqiaobian_phase=="play"
		then
			for _,fp in sgs.list(self.friends)do
				if can then break end
				self.olqiaobian_to = fp
				for _,j in sgs.list(fp:getJudgingArea())do
					if can then break end
					if self:doDisCard(fp,j:getEffectiveId())
					then
						self.olqiaobian_ej = j
						for _,ep in sgs.list(self.enemies)do
							if ep:containsTrick(j:objectName())
							then continue end
							can = true
						end
						for _,ep in sgs.list(self.room:getAllPlayers())do
							if ep:containsTrick(j:objectName())
							or self:isFriend(ep)
							then continue end
							can = true
						end
					end
				end
			end
			for _,ep in sgs.list(self.enemies)do
				if can then break end
				self.olqiaobian_to = ep
				for _,e in sgs.list(ep:getEquips())do
					if can then break end
					self.olqiaobian_ej = e
					local n = e:getRealCard():toEquipCard():location()
					for _,fp in sgs.list(self.friends)do
						if self:doDisCard(ep,e:getEffectiveId())
						and not fp:getEquip(n)
						then can = true end
					end
				end
			end
			can = can and #self:getTurnUse()<3
		end
		if self.olqiaobian_phase=="discard"
		and self.player:getHandcardNum()>self.player:getMaxCards()
		then can = true end
		if can
		then
			return items[1]
		end
	end
	return items[#items]
end

sgs.ai_skill_use["@@olqiaobian"] = function(self,prompt)
	local valid = {}
	local destlist = self.room:getAllPlayers()
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist,"hp")
	local n = self.olqiaobian_phase=="draw" and 2 or 1
	for _,p in sgs.list(destlist)do
		if #valid>=n then break end
		self.olqiaobian_phase = p
		if self:isEnemy(p) and n>1 and self:doDisCard(p,"h")
		then table.insert(valid,p:objectName()) continue end
		if n<2 and self.olqiaobian_to:objectName()==p:objectName()
		then table.insert(valid,p:objectName()) continue end
	end
	for _,p in sgs.list(destlist)do
		if #valid>=n then break end
		if table.contains(valid,p:objectName()) then continue end
		self.olqiaobian_phase = p
		if not self:isFriend(p) and n>1 and self:doDisCard(p,"h")
		then table.insert(valid,p:objectName()) continue end
		if n<2 and self.olqiaobian_to:objectName()==p:objectName()
		then table.insert(valid,p:objectName()) continue end
		if self:isFriend(p) and n<2 and self:doDisCard(p,"ej")
		then table.insert(valid,p:objectName()) continue end
		if not self:isFriend(p) and n<2 and self:doDisCard(p,"e")
		then table.insert(valid,p:objectName()) continue end
	end
	if #valid>0
	then
    	return string.format("@OLQiaobianCard=.->%s",table.concat(valid,"+"))
	end
end

sgs.ai_skill_playerchosen.olqiaobian = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"handcard")
	if self.olqiaobian_ej and self.olqiaobian_ej:isKindOf("TrickCard")
	or self:isFriend(self.olqiaobian_phase)
	then
		for _,target in sgs.list(destlist)do
			if self:isEnemy(target)
			then return target end
		end
		for _,target in sgs.list(destlist)do
			if not self:isFriend(target)
			then return target end
		end
	else
		for _,target in sgs.list(destlist)do
			if self:isFriend(target)
			then return target end
		end
		for _,target in sgs.list(destlist)do
			if not self:isEnemy(target)
			then return target end
		end
	end
end

sgs.ai_skill_cardchosen.olqiaobian = function(self,who,flags,method)
	for _,c in sgs.list(who:getCards(flags))do
		if self.olqiaobian_ej and c:getEffectiveId()==self.olqiaobian_ej:getEffectiveId()
		then return c:getId() end
	end
end

sgs.ai_skill_invoke.olbeige = function(self,data)
	local target = data:toPlayer()
	if target
	then
		return not self:isEnemy(target)
	end
end

sgs.ai_skill_discard.olbeige = function(self,max,min)
	local to_cards = {}
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards,nil,true)
	local pattern = self.player:getTag("OLbeigeTag"):toString():split("+")
   	for _,c in sgs.list(cards)do
   		if #to_cards>=min then break end
		if c:getSuitString()==pattern[1]
		then
         	table.insert(to_cards,c:getEffectiveId())
		end
	end
   	for _,c in sgs.list(cards)do
   		if #to_cards>=min then break end
		if c:getNumberString()==pattern[2]
		then
         	table.insert(to_cards,c:getEffectiveId())
		end
	end
   	for _,c in sgs.list(cards)do
   		if #to_cards>=min then break end
		local target = self.room:getCurrent()
		if self:isEnemy(target)
		then
         	table.insert(to_cards,c:getEffectiveId())
		end
	end
	return to_cards
end

sgs.ai_skill_playerchosen.oljieming = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	local function func(a,b)
		return a:getMaxHp()-a:getHandcardNum()>b:getMaxHp()-b:getHandcardNum()
	end
	table.sort(destlist,func)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()<5
		and target:getHandcardNum()<target:getMaxHp()
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		and target:getHandcardNum()<6
		and target:getHandcardNum()<=target:getMaxHp()
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		and target:getHandcardNum()>target:getMaxHp()*(math.random()+1)
		then return target end
	end
end

sgs.ai_can_damagehp.oljieming = function(self,from,card,to)
	if self:isFriend(to)
	and self:canLoseHp(from,card,to)
	and to:getHp()+self:getAllPeachNum()-self:ajustDamage(from,to,1,card)>0
	then
		for _,fp in sgs.list(self.friends)do
			if fp:getHandcardNum()<5
			and fp:getHandcardNum()<fp:getMaxHp()
			then return true end
		end
	end
end

addAiSkills("olqiangxi").getTurnUseCard = function(self)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
  	for _,c in sgs.list(cards)do
		if c:isKindOf("Weapon")
		then
			return sgs.Card_Parse("@OLQiangxiCard="..c:getEffectiveId())
		end
	end
	return sgs.Card_Parse("@OLQiangxiCard=.")
end

sgs.ai_skill_use_func["OLQiangxiCard"] = function(card,use,self)
	self:sort(self.enemies,"hp")
	for _,ep in sgs.list(self.enemies)do
		if self:isWeak(ep)
		and ep:getMark("olqiangxiTarget-Clear")<1
		and (self.player:getHp()>1 or self:getCardsNum("Peach")+self:getCardsNum("Analeptic")>0 or card:subcardsLength()>0)
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if (not self:isWeak() or card:subcardsLength()>0)
		and ep:getMark("olqiangxiTarget-Clear")<1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.room:getOtherPlayers(self.player))do
		if self:isWeak(ep) and not self:isWeak()
		and ep:getMark("olqiangxiTarget-Clear")<1
		then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.OLQiangxiCard = 5.4
sgs.ai_use_priority.OLQiangxiCard = 4.8

sgs.ai_skill_playerchosen["#olhujia"] = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,"hp")
    for _,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		and self:isWeak(target)
		then return target end
	end
end

sgs.ai_skill_invoke.olqieting = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_choice.olzaiqi = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if self:isFriend(target)
	and table.contains(items,"draw")
	then return "draw" end
	return items[1]
end

addAiSkills("oljianmie").getTurnUseCard = function(self)
	return sgs.Card_Parse("@OLJianmieCard=.")
end

sgs.ai_skill_use_func["OLJianmieCard"] = function(card,use,self)
	self:sort(self.enemies,nil,true)
	for _,ep in sgs.list(self.enemies)do
		if ep:getCardCount()>=self.player:getCardCount() then
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

sgs.ai_use_value.OLJianmieCard = 5.4
sgs.ai_use_priority.OLJianmieCard = -0.8

addAiSkills("olmieji").getTurnUseCard = function(self)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards)
	if #cards<1 then return end
  	for _,c in sgs.list(cards)do
		if table.contains(self.toUse,c) or c:getTypeId()~=2 then continue end
		return sgs.Card_Parse("@OLMiejiCard="..c:getId())
	end
end

sgs.ai_skill_use_func["OLMiejiCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,ep in sgs.list(self.enemies)do
		if ep:getCardCount()>1 and self.player:inMyAttackRange(ep) then
			use.card = card
			use.to:append(ep)
			return
		end
	end
	for _,ep in sgs.list(self.enemies)do
		if ep:getCardCount()>0 then
			use.card = card
			use.to:append(ep)
			return
		end
	end
end

sgs.ai_use_value.OLMiejiCard = 4.4
sgs.ai_use_priority.OLMiejiCard = 2.8

sgs.ai_skill_invoke.ollihuo = function(self,data)
	return sgs.ai_skill_invoke.fan(self,data)
end

sgs.ai_skill_discard.ollihuo = function(self,max,min)
	local cards = self.player:getCards("he")
	cards = self:sortByKeepValue(cards)
	if #cards>0 and (self:isWeak() or #cards>3) then
       	return {cards[1]}
	end
	return {}
end

sgs.ai_skill_use["@@olchunlao"] = function(self,prompt)
	local valid = {}
	local ids = self.player:getPile("wine")
	if ids:length()<2 then return end
	for _,id in sgs.list(ids)do
		if #valid>1 then break end
		table.insert(valid,id)
	end
	if #valid>0 then
    	return "@OLChunlaoCard="..table.concat(valid,"+")
	end
end

function sgs.ai_cardsview_valuable.olchunlao(self,class_name,player)
	local dy = self.room:getCurrentDyingPlayer()
	if dy and self:isFriend(dy) then
		local ids = player:getPile("wine")
		return "@OLChunlaoCard="..ids:at(0)
	end
end

sgs.ai_skill_cardask["olrenxin0"] = function(self,data,pattern,prompt)
    local dy = data:toDying()
    if self:isFriend(dy.who)
	then return true end
	return "."
end

sgs.ai_skill_invoke.olchengxiang = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_invoke.olbuyi = function(self,data)
	local target = data:toPlayer()
	if target then
		return self:isFriend(target)
	end
end

sgs.ai_skill_cardchosen.olbuyi = function(self,who,flags,method)
	if self:isFriend(who) then
		for _,e in sgs.list(who:getEquips())do
			return e:getId()
		end
	end
end

function sgs.ai_cardsview_valuable.oljiushi(self,class_name,player)
	local dc = dummyCard("analeptic")
	dc:setSkillName("oljiushi")
	return dc:toString()
end

addAiSkills("oljiushi").getTurnUseCard = function(self)
	local dc = dummyCard("analeptic")
	dc:setSkillName("oljiushi")
	return dc
end

sgs.ai_skill_invoke.oljiushi = function(self,data)
	return true
end

sgs.ai_skill_invoke.olguzheng = function(self,data)
	if data:toString()~="" then
		return self:canDraw()
	end
	local target = data:toPlayer()
	if target then
		return not self:isEnemy(target)
		or self.player:getMark("olguzhengNum")>2 and self:canDraw()
	end
end

local olzhijian_skill = {}
olzhijian_skill.name = "olzhijian"
table.insert(sgs.ai_skills,olzhijian_skill)
olzhijian_skill.getTurnUseCard = function(self)
	local equips = {}
	for _,card in sgs.qlist(self.player:getHandcards())do
		if card:getTypeId()==sgs.Card_TypeEquip then
			table.insert(equips,card)
		end
	end
	if #equips==0 then return end

	return sgs.Card_Parse("@OLZhijianCard=.")
end

sgs.ai_skill_use_func.OLZhijianCard = function(card,use,self)
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
	use.card = sgs.Card_Parse("@OLZhijianCard="..select_equip:getId())
end

sgs.ai_card_intention.OLZhijianCard = -80
sgs.ai_use_priority.OLZhijianCard = sgs.ai_use_priority.RendeCard+0.1  -- 刘备二张双将的话，优先直谏
sgs.ai_cardneed.olzhijian = sgs.ai_cardneed.equip

sgs.ai_skill_invoke.oljiang = function(self,data)
	local use = data:toCardUse()
	if use.card then
		return self:canDraw()
	end
	return not self:isWeak()
end

sgs.ai_skill_invoke.olfuli = function(self,data)
	return self:getAllPeachNum()+self.player:getHp()<1
end

sgs.ai_skill_invoke.oldangxian = function(self,data)
	if self:getCardsNum("Slash")<1 then
		for _,enemy in ipairs(self.enemies)do
			if self.player:canSlash(enemy) then
				return true
			end
		end
	end
end

sgs.ai_skill_invoke.olenyuan = function(self,data)
	local damage = data:toDamage()
	if damage.from then
		return not self:isFriend(damage.from)
	end
	local move = data:toMoveOneTime()
	if move.from then
		return self:isFriend(move.from)
	end
end

sgs.ai_skill_use["@@olxuanhuo"] = function(self,prompt)
	local destlist = self.room:getOtherPlayers(self.player)
    destlist = sgs.QList2Table(destlist) -- 将列表转换为表
    self:sort(destlist,nil,true)
	local cards = self.player:getCards("h")
	cards = self:sortByKeepValue(cards)
	for _,p in sgs.list(destlist)do
		for _,e in sgs.list(self.enemies)do
			if p:canSlash(e,false) then
				local valid = {}
				for _,h in sgs.list(cards)do
					table.insert(valid,h:toString())
					if #valid>1 then
						return "@OLXuanhuoCard="..table.concat(valid,"+").."->"..p:objectName()
					end
				end
			end
		end
	end
end

sgs.ai_skill_playerchosen.olxuanhuo = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_cardask["olenyuan0"] = function(self,data,pattern,prompt)
	local damage = data:toDamage()
    if self:isFriend(damage.to) or self:isWeak()
	or self:getOverflow()>0
	then return true end
	return "."
end

sgs.ai_skill_invoke.oljiangchi = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_cardask["oljiangchi0"] = function(self,data,pattern,prompt)
    if self:getCardsNum("Slash")>0
	then return true end
	return "."
end

sgs.ai_skill_use["@@olzongxuan"] = function(self,prompt)
	local valid = {}
	local cs = {}
	for _,id in sgs.list(self.player:getTag("olzongxuanForAI"):toIntList())do
		table.insert(cs,sgs.Sanguosha:getCard(id))
	end
	self:sortByKeepValue(cs,true)
	if self.player:getPhase()==sgs.Player_NotActive then
		for _,c in sgs.list(cs)do
			if self:aiUseCard(c).card or self:getKeepValue(c)>7 then
				table.insert(valid,c:getId())
			end
		end
		for _,c in sgs.list(cs)do
			if #valid<1 then
				table.insert(valid,c:getId())
			end
		end
	else
		self:sort(self.enemies)
		for _,c in sgs.list(cs)do
			if c:getTypeId()==3 then
				if self:isWeak(self.friends) then
					table.insert(valid,c:getId())
				end
			elseif self:isWeak(self.enemies) and self.enemies[1]:getHp()~=self.player:getHp() then
				table.insert(valid,c:getId())
			end
		end
	end
	if #valid>0 then
    	return "@OLZongxuanCard="..table.concat(valid,"+")
	end
end

sgs.ai_skill_playerchosen.olzhiyan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local c = sgs.Sanguosha:getCard(self.room:getDrawPile():first())
	if c and c:hasFlag("visible") then
		if c:getTypeId()==3 then
			for _,p in sgs.list(destlist)do
				if self:isFriend(p) and p:isWounded()
				then return p end
			end
			for _,p in sgs.list(destlist)do
				if self:isFriend(p) and not self:getSameEquip(c) and #self:poisonCards({c},p)<1
				then return p end
			end
			for _,p in sgs.list(destlist)do
				if self:isFriend(p) and p:isWounded()
				and not c:isAvailable(p)
				then return p end
			end
			for _,p in sgs.list(destlist)do
				if self:isEnemy(p) and not p:isWounded()
				and #self:poisonCards({c},p)>0
				and c:isAvailable(p)
				then return p end
			end
			for _,p in sgs.list(destlist)do
				if not self:isEnemy(p)
				then return p end
			end
		end
	end
	for _,p in sgs.list(destlist)do
		if self:isFriend(p) and self.player:getHp()==p:getHp()
		and self:canDraw(p)
		then return p end
	end
	for _,p in sgs.list(destlist)do
		if self:isEnemy(p) and self.player:getHp()~=p:getHp()
		then return p end
	end
    for _,p in sgs.list(destlist)do
		if not self:isFriend(p)
		then return p end
	end
end

sgs.ai_skill_invoke.oljiezishou = function(self,data)
	return self:canDraw() and #self.enemies>0
end

sgs.ai_skill_invoke.olzhuikong = function(self,data)
	local to = data:toPlayer()
	if self:isEnemy(to) then
		if to:inMyAttackRange(self.player) then
			local mc = self:getMaxCard()
			if mc then
				return mc:getNumber()>9
			end
		end
		return #self.enemies>1
	end
end

sgs.ai_skill_playerchosen.olqiuyuan = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_cardask["olqiuyuan1"] = function(self,data,pattern,prompt)
    local use = data:toCardUse()
	if use.card:isKindOf("Slash") then
		if self:slashIsEffective(use.card,self.player) and self:getCardsNum("Jink")<1 then
			return true
		end
	else
		if self:hasTrickEffective(use.card,use.from,self.player) and self:getCardsNum("Nullification")<1 then
			return true
		end
	end
	return "."
end

sgs.ai_skill_invoke.oljiejingce = function(self,data)
	return self:canDraw()
end









