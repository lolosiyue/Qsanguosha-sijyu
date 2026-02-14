sgs.ai_judgestring = {
	indulgence = ".|heart",
	supply_shortage = ".|club",
	lightning = {".|spade|2~9",false}
}

sgs.ai_skill_guanxing = {}

local function getIdToCard(cards)
	local tocard = {}
	for _,id in sgs.list(cards)do
		table.insert(tocard,sgs.Sanguosha:getCard(id))
	end
	return tocard
end

local function getBackToId(cards)
	local ids = {}
	for _,c in sgs.list(cards)do
		table.insert(ids,c:getId())
	end
	return ids
end

--for test--
local function ShowGuanxingResult(self,up,bottom)
	self.room:writeToConsole("----GuanxingResult----")
	self.room:writeToConsole(string.format("up:%d",#up))
	if #up>0 then
		for _,card in pairs(up)do
			self.room:writeToConsole(string.format("(%d)%s[%s%d]",card:getId(),card:getClassName(),card:getSuitString(),card:getNumber()))
		end
	end
	self.room:writeToConsole(string.format("down:%d",#bottom))
	if #bottom>0 then
		for _,card in pairs(bottom)do
			self.room:writeToConsole(string.format("(%d)%s[%s%d]",card:getId(),card:getClassName(),card:getSuitString(),card:getNumber()))
		end
	end
	self.room:writeToConsole("----GuanxingEnd----")
end
--end--

local function GuanXing(self,cards)
	local up,bottom = {},getIdToCard(cards)
	self:sortByUseValue(bottom,true)
	if self.player:hasSkill("myjincui") then
		local cs = {}
		for _,c in ipairs(table:copyFrom(bottom))do
			if c:getNumber()==7 then
				table.insert(cs,c)
				table.removeOne(bottom,c)
			end
		end
		table.insertTable(bottom,cs)
	end
	local lightnings = {}
   	local target = self.room:getCurrent() or self.player
	local judge = sgs.QList2Table(target:getJudgingArea())
	local self_has_judged,willSkipDrawPhase,willSkipPlayPhase
	if #judge>0 and target:getPhase()<sgs.Player_Play and not target:containsTrick("YanxiaoCard") then
		judge = sgs.reverse(judge)
		local judged_list = {}
		for _,jc in ipairs(judge)do
			if jc:isKindOf("Indulgence") then
				willSkipPlayPhase = true
				if target:isSkipped(sgs.Player_Play) then continue end
			elseif jc:isKindOf("SupplyShortage") then
				willSkipDrawPhase = true
				if target:isSkipped(sgs.Player_Draw) then continue end
			elseif jc:isKindOf("DelayedTrick") and jc:isDamageCard() then
				table.insert(lightnings,jc)
			end
			local judge_str = sgs.ai_judgestring[jc:objectName()]
			if type(judge_str)=="string" then judge_str = {judge_str,true}
			elseif type(judge_str)~="table" then
				self.room:writeToConsole(debug.traceback())
				judge_str = {".|"..jc:getSuitString(),true}
			end
			for _,c in ipairs(table:copyFrom(bottom))do
				local cf = CardFilter(c,target)
				if sgs.Sanguosha:matchExpPattern(judge_str[1],target,cf)==judge_str[2] and self:isFriend(target)
				or sgs.Sanguosha:matchExpPattern(judge_str[1],target,cf)~=judge_str[2] and self:isEnemy(target) then
					judged_list[jc:objectName()] = c
					if jc:isKindOf("SupplyShortage")
					then willSkipDrawPhase = false
					elseif jc:isKindOf("Indulgence")
					then willSkipPlayPhase = false end
					self_has_judged = true
					table.removeOne(bottom,c)
					break
				end
			end
		end
		for i,jc in ipairs(judge)do
			local c = judged_list[jc:objectName()]
			if c then table.insert(up,c)
			else table.insert(up,table.remove(bottom,1)) end
		end
	end
	if sgs.cardEffect and sgs.cardEffect.card and sgs.cardEffect.card:isKindOf("Dongzhuxianji")
	or sgs.guanXingFriend then
		sgs.guanXingFriend = false
		for _,c in ipairs(table:copyFrom(bottom))do
			if self.player:getPhase()<=sgs.Player_Play and self:cardNeed(c)>6
			and c:isAvailable(self.player) and self:aiUseCard(c).card
			then table.insert(up,c) table.removeOne(bottom,c) end
		end
		for _,c in ipairs(table:copyFrom(bottom))do
			if self:cardNeed(c)>6 then
				table.insert(up,c)
				table.removeOne(bottom,c)
			end
		end
	end
	local drawCards = self:ImitateResult_DrawNCards(target,target:getVisibleSkillList(true))
	if willSkipDrawPhase or target:getPhase()>sgs.Player_Draw then drawCards = 0 end
	local np = target:getNextAlive()
	while np~=target do
		if np:faceUp() then break end
		np = np:getNextAlive()
	end
	judge = sgs.QList2Table(np:getJudgingArea())
	for i,jc in ipairs(lightnings)do
		if np:containsTrick(jc:objectName()) then continue end
		table.insert(judge,jc)
	end
	judge = sgs.reverse(judge)
	local judged_list = {}
	for x,jc in ipairs(judge)do
		local n = x-1
		if #bottom-drawCards-n<1 then break end
		local judge_str = sgs.ai_judgestring[jc:objectName()]
		if type(judge_str)=="string" then judge_str = {judge_str,true}
		elseif type(judge_str)~="table" then
			self.room:writeToConsole(debug.traceback())
			judge_str = {".|"..jc:getSuitString(),true}
		end
		for _,c in ipairs(table:copyFrom(bottom))do
			local cf = CardFilter(c,np)
			if self:isFriend(np) then
				if sgs.Sanguosha:matchExpPattern(judge_str[1],np,cf)==judge_str[2] then
					judged_list[jc:objectName()] = c
					table.removeOne(bottom,c)
					break
				end
			elseif sgs.Sanguosha:matchExpPattern(judge_str[1],np,cf)~=judge_str[2] then
				judged_list[jc:objectName()] = c
				table.removeOne(bottom,c)
				break
			end
		end
	end
	self:sortByUseValue(bottom)
	if #bottom>0 and self.player:hasSkill("myjincui") then
		local cs = {}
		for _,c in ipairs(table:copyFrom(bottom))do
			if c:getNumber()==7 then
				table.insert(cs,c)
				table.removeOne(bottom,c)
			end
		end
		table.insertTable(bottom,cs)
	end
	if drawCards>0 and target==self.player then
		for n=1,drawCards do
			local nocan = true
			if #bottom<1 then break end
			for _,c in ipairs(table:copyFrom(bottom))do
				if self:aiUseCard(c).card then
					nocan = false
					table.insert(up,c)
					table.removeOne(bottom,c)
					break
				end
			end
			if nocan then
				table.insert(up,table.remove(bottom,1))
			end
		end
	end
	for _,jc in ipairs(judge)do
		local c = judged_list[jc:objectName()]
		if c then table.insert(up,c)
		elseif #bottom>0 then table.insert(up,table.remove(bottom,#bottom)) end
	end
	--self.player:speak("GuanXing="..#up.."="..#bottom)
	return getBackToId(up),getBackToId(bottom)
end

local function XinZhan(self,cards)
	local judged_list,has_judged = {},false
	local up,bottom = {},getIdToCard(cards)
	local np = self.player:getNextAlive()
	local judge = np:getCards("j")
	judge = sgs.QList2Table(judge)
	for i,j in sgs.list(sgs.reverse(judge))do
		local judge_str = sgs.ai_judgestring[j:objectName()]
		if type(judge_str)=="string" then judge_str = {judge_str,true}
		elseif type(judge_str)~="table" then
			self.room:writeToConsole(debug.traceback())
			judge_str = {".|"..j:getSuitString(),true}
		end
		local index = 1
		local lightning_flag = false

		for _,for_judge in sgs.list(bottom)do
			local cf = CardFilter(for_judge,np)
			if string.find(judge_str[1],"spade") and not lightning_flag then
				if cf:getNumber()>=2 and cf:getNumber()<=9 then lightning_flag = true end
			end
			if self:isFriend(np) then
				if sgs.Sanguosha:matchExpPattern(judge_str[1],np,cf)==judge_str[2] then
					if not lightning_flag then
						table.insert(up,for_judge)
						table.remove(bottom,index)
						judged_list[i] = 1
						has_judged = true
						break
					end
				end
			else
				if sgs.Sanguosha:matchExpPattern(judge_str[1],np,cf)~=judge_str[2]
				and lightning_flag then
					table.insert(up,for_judge)
					table.remove(bottom,index)
					judged_list[i] = 1
					has_judged = true
				end
			end
			index = index+1
		end
		judged_list[i] = judged_list[i] or 0
	end

	if has_judged then
		for index = 1,#judged_list do
			if judged_list[index]==0 then
				table.insert(up,index,table.remove(bottom))
			end
		end
	end

	while #bottom>0 do
		table.insert(up,table.remove(bottom))
	end

	return getBackToId(up),{}
end

function SmartAI:askForGuanxing(cards,guanxing_type)
	--KOF模式--
	if guanxing_type~=sgs.Room_GuanxingDownOnly then
		local func = Tactic("guanxing",self,guanxing_type==sgs.Room_GuanxingUpOnly)
		if func then return func(self,cards) end
	end
	--身份局--
	--[[
	for sg,sk in ipairs(sgs.getPlayerSkillList(self.player))do
		sg = sgs.ai_skill_guanxing[sk:objectName()]
		if type(sg)=="function" then
			local up,bottom = sg(self,cards,guanxing_type)
		end
	end--]]
	if guanxing_type==sgs.Room_GuanxingBothSides
	then return GuanXing(self,cards)
	elseif guanxing_type==sgs.Room_GuanxingUpOnly
	then return XinZhan(self,cards)
	elseif guanxing_type==sgs.Room_GuanxingDownOnly
	then return {},cards end
	return cards,{}
end

function SmartAI:getValuableCardForGuanxing(cards)
	local ag = sgs.ai_skill_askforag.amazing_grace(self,getBackToId(cards))
	if ag then return sgs.Sanguosha:getCard(ag) end
end