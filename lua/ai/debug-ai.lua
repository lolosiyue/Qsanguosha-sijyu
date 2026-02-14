sgs.ai_event_callback[sgs.EventPhaseStart].debugfunc = function(self,player,data)
	if sgs.debugmode and player:getPhase()==sgs.Player_Start
	then debugFunc(self,player,data) end
end

sgs.ai_event_callback[sgs.CardUsed].debugfunc = function(self,player,data)
	if sgs.debugmode and (data:toCardUse().card:isKindOf("Peach") or data:toCardUse().card:isKindOf("Nullification"))
	then debugFunc(self,player,data) end
end

function debugFunc(self,player,data)
	local choices = {"cancel","showVisiblecards","showHandcards","objectiveLevel","getDefenseSlash"}
	local debugmsg = function(fmt,...)
		if type(fmt)=="boolean" then fmt = fmt and "true" or "false" end
		local msg = string.format(fmt,...)
		player:speak(msg)
		logmsg("ai.html","<pre>"..msg.."</pre>")
	end
	local players = self.room:getAlivePlayers()
	local owner = self.room:getOwner()
	repeat
		local choice = self.room:askForChoice(owner,"aidebug",table.concat(choices,"+"))
		if choice=="showVisiblecards" then
			debugmsg(" ")
			debugmsg("===================")
			debugmsg("查看已知牌; 当前AI是: %s[%s]",player:getLogName(),sgs.Sanguosha:translate(player:getRole()) )
			for _,p in sgs.list(players)do
				local msg=string.format("%s已知牌:",player:getLogName())
				local flag=string.format("%s_%s_%s","visible",player:objectName(),p:objectName())
				for _,card in sgs.list(p:getHandcards())do
					if card:hasFlag("visible") or card:hasFlag(flag)
					then msg = msg..card:getLogName().."," end
				end
				debugmsg(msg)
			end
		elseif choice=="showHandcards" then
			debugmsg(" ")
			debugmsg("===================")
			debugmsg("查看手牌; 当前AI是: %s[%s]",player:getLogName(),sgs.Sanguosha:translate(player:getRole()) )
			for _,p in sgs.list(players)do
				local msg=string.format("%s手牌:",player:getLogName())
				for _,card in sgs.list(p:getHandcards())do
					msg = msg..card:getLogName()..","
				end
				debugmsg(msg)
			end
		elseif choice=="objectiveLevel" then
			debugmsg(" ")
			debugmsg("============%s(%.1f)",sgs.gameProcess(),sgs.gameProcess(1))
			debugmsg("查看关系; 当前AI是: %s[%s]",player:getLogName(),sgs.Sanguosha:translate(player:getRole()) )
			for _,p in sgs.list(players)do
				local l=self:objectiveLevel(p)
				local rel = l>0 and "敌对" or l<0 and "友好" or "中立"
				rel = rel.." "..l
				debugmsg("%s[%s]: %d:%d:%d %s",
					p:getLogName(),
					sgs.Sanguosha:translate(sgs.ai_role[p:objectName()]),
					sgs.roleValue[p:objectName()]["rebel"],
					sgs.roleValue[p:objectName()]["loyalist"],
					sgs.roleValue[p:objectName()]["renegade"],
					rel)
			end
		elseif choice=="getDefenseSlash" then
			debugmsg(" ")
			debugmsg("===================")
			debugmsg("查看对杀的防御; 当前AI是: %s[%s]",p:getLogName(),sgs.Sanguosha:translate(player:getRole()))
			for _,p in sgs.list(players)do
				debugmsg("%s:%.2f",p:getLogName(),self:getDefenseSlash(p))
			end
		else
			break
		end
	until false
end

function logmsg(fname,fmt,...)
	local fp = io.open(fname,"ab")
	if type(fmt)=="boolean" then fmt = fmt and "true" or "false" end
	fp:write(string.format(fmt,...).."\r\n")
	fp:close()
end

function endlessNiepan(who)
	if who:getGeneral2() or who:getHp()>0 then return end
	local room = who:getRoom()
	local rebel_value = sgs.roleValue[who:objectName()]["rebel"]
	local renegade_value = sgs.roleValue[who:objectName()]["renegade"]
	local loyalist_value = sgs.roleValue[who:objectName()]["loyalist"]
	for _,skill in sgs.list(who:getVisibleSkillList())do
		if skill:getLocation()==sgs.Skill_Right then
			room:detachSkillFromPlayer(who,skill:objectName())
		end
	end
	room:changeHero(who,sgs.Sanguosha:getRandomGenerals(1)[1],true,true,false,true)
	room:setPlayerProperty(who,"maxhp",sgs.QVariant(5))
	room:setTag("SwapPile",sgs.QVariant(0))
	who:bury()
	who:drawCards(5,"endlessNiepan")
	sgs.roleValue[who:objectName()]["rebel"] = rebel_value
	sgs.roleValue[who:objectName()]["renegade"] = renegade_value
	sgs.roleValue[who:objectName()]["loyalist"] = loyalist_value
end

function SmartAI:printStand()
	self.room:output(self.player:getRole())
	self.room:output("enemies:")
	for _,p in sgs.list(self.enemies)do
		self.room:output(p:getGeneralName())
	end
	self.room:output("end of enemies")
	self.room:output("friends:")
	for _,p in sgs.list(self.friends)do
		self.room:output(p:getGeneralName())
	end
	self.room:output("end of friends")
end

function SmartAI:printFEList()
	for _,player in sgs.list (self.enemies)do
		self.room:writeToConsole("enemy "..player:getGeneralName()..(sgs.roleValue[player:objectName()][player:getRole()] or "")..player:getRole())
	end
	for _,player in sgs.list (self.friends_noself)do
		self.room:writeToConsole("friend "..player:getGeneralName()..(sgs.roleValue[player:objectName()][player:getRole()] or "")..player:getRole())
	end
	self.room:writeToConsole(self.player:getGeneralName().." list end")
end

function SmartAI:log(outString)
	self.room:output(outString)
end

function outputPlayersEvaluation()
	if not global_room:getLord() then return end
	global_room:writeToConsole("===========MISJUDGE START===========" )
	for _,player in sgs.list(global_room:getOtherPlayers(global_room:getLord()))do
		global_room:writeToConsole("<------- "..player:getGeneralName().." ------->")
		global_room:writeToConsole("Role: "..player:getRole().."      Evaluate role: "..sgs.ai_role[player:objectName()])
		global_room:writeToConsole("Rebel:"..sgs.roleValue[player:objectName()]["rebel"].." Loyalist:"
									..sgs.roleValue[player:objectName()]["loyalist"].." Renegade:"
									..sgs.roleValue[player:objectName()]["renegade"])
	end
	global_room:writeToConsole("================END================")
end

function sgs.checkMisjudge(player)
	local lord = global_room:getLord()
	if not lord then return end
	local mode = global_room:getMode()
	if player then
		if sgs.playerRoles[player:getRole()]>sgs.mode_player[mode][player:getRole()]
		or sgs.playerRoles[player:getRole()]<0 then
			global_room:writeToConsole("Misjudge--------> Role:"..player:getRole().." Current players:"..sgs.playerRoles[player:getRole()]
			.." Valid players:"..sgs.mode_player[mode][player:getRole()])
		end
	else
		local rebel_num,loyalist_num,renegade_num = 0,0,0
		local evaluate_rebel,evaluate_loyalist,evaluate_renegade = 0,0,0
		for _,p in sgs.list(global_room:getOtherPlayers(lord))do
			local role = p:getRole()
			if role=="rebel" then rebel_num = rebel_num+1
			elseif role=="loyalist" then loyalist_num = loyalist_num+1
			elseif role=="renegade" then renegade_num = renegade_num+1 end
		end
		for _,p in sgs.list(global_room:getOtherPlayers(lord))do
			if sgs.ai_role[p:objectName()]=="rebel" and rebel_num>0 then evaluate_rebel = evaluate_rebel+1
			elseif sgs.ai_role[p:objectName()]=="loyalist" and loyalist_num>0
			then evaluate_loyalist = evaluate_loyalist+1
			elseif sgs.ai_role[p:objectName()]=="renegade" and renegade_num>0
			then evaluate_renegade = evaluate_renegade+1 end
		end
		if evaluate_renegade<1 then
			if (evaluate_rebel>=rebel_num+renegade_num and evaluate_rebel>rebel_num)
			or (evaluate_loyalist>=loyalist_num+renegade_num and evaluate_loyalist>loyalist_num)
			or (evaluate_rebel==rebel_num+1 and evaluate_loyalist==loyalist_num+1) then
				outputPlayersEvaluation()
				if evaluate_rebel>=rebel_num+renegade_num and evaluate_rebel>rebel_num
				then sgs.modifiedRoleTrends("rebel")
				elseif evaluate_loyalist>=loyalist_num+renegade_num and evaluate_loyalist>loyalist_num and rebel_num>0
				then sgs.modifiedRoleTrends("loyalist")
				elseif evaluate_rebel>rebel_num and evaluate_loyalist>loyalist_num
				then sgs.modifiedRoleTrends("rebel") sgs.modifiedRoleTrends("loyalist") end
			end
		else
			if evaluate_rebel>rebel_num
			or evaluate_loyalist>loyalist_num
			or evaluate_renegade>renegade_num then
				outputPlayersEvaluation()
				if evaluate_rebel>rebel_num then sgs.modifiedRoleTrends("rebel") end
				if evaluate_loyalist>loyalist_num then sgs.modifiedRoleTrends("loyalist") end
				if evaluate_renegade>renegade_num then sgs.modifiedRoleTrends("renegade") end
			end
		end
	end
end

local cardparse = sgs.Card_Parse
function sgs.Card_Parse(str)
	if type(str)=="string" or type(str)=="number"
	then return cardparse(str) end
	global_room:writeToConsole(debug.traceback())
end

function sgs.broadcastRole(role_type)
	role_type = role_type=="r" and "rebel" or role_type=="R" and "renegade" or role_type=="l" and "loyalist"
	for _,p in sgs.list(global_room:getAlivePlayers())do
		if p:getRole()==role_type then global_room:broadcastProperty(p,"role")
		elseif not role_type then global_room:broadcastProperty(p,"role") end
	end
	evaluateAlivePlayersRole()
end

function sgs.printFEList(player)
	global_room:writeToConsole("")
	global_room:writeToConsole("gameProcess  -> "..sgs.gameProcess())
	for _,p in sgs.list(global_room:getAlivePlayers())do
		if player and p:objectName()~=player:objectName() then continue end
		global_room:writeToConsole("===="..p:getGeneralName().."  Role::"..p:getRole().." ====")
		sgs.ais[p:objectName()]:updatePlayers()
		local msge = "enemies:"
		for _,ep in sgs.list(sgs.ais[p:objectName()].enemies)do
			msge = msge..ep:getGeneralName()..","
		end
		global_room:writeToConsole(msge)
		local msgf = "friends:"
		for _,fp in sgs.list(sgs.ais[p:objectName()].friends)do
			msgf = msgf..fp:getGeneralName()..","
		end
		global_room:writeToConsole(msgf)
	end
end