-- This is the Smart AI,and it should be loaded and run at the server side

-- "middleclass" is the Lua OOP library written by kikito
-- more information see: https://github.com/kikito/middleclass
local middleclass = require "middleclass"

-- initialize the random seed for later use
math.randomseed(os.time())

-- SmartAI is the base class for all other specialized AI classes
SmartAI = middleclass.class("SmartAI")

local version = "QSanguosha AI 20141006 (V1.32 Alpha)"

-- checkout https://github.com/haveatry823/QSanguoshaAI for details

--- this function is only function that exposed to the host program
--- and it clones an AI instance by general name
-- @param player The ServerPlayer object that want to create the AI object
-- @return The AI object
function CloneAI(player)
	return SmartAI(player).lua_ai
end

sgs.ais =				 		{}
sgs.ai_role = 					{}
sgs.roleValue = 				{}
sgs.ai_card_intention =			{}
sgs.ai_playerchosen_intention = {}
sgs.ai_Yiji_intention =			{}
sgs.ai_retrial_intention =		{}
sgs.ai_keep_value =		 		{}
sgs.ai_use_value =				{}
sgs.ai_use_priority =			{}
sgs.ai_suit_priority =			{}
sgs.ai_chaofeng =				{}
sgs.ai_skill_invoke =			{}
sgs.ai_skill_suit =		 		{}
sgs.ai_skill_cardask =			{}
sgs.ai_skill_choice =			{}
sgs.ai_skill_askforag =	 		{}
sgs.ai_skill_askforyiji =		{}
sgs.ai_skill_pindian =			{}
sgs.ai_filterskill_filter = 	{}
sgs.ai_skill_playerchosen = 	{}
sgs.ai_skill_discard =			{}
sgs.ai_cardshow =				{}
sgs.ai_nullification =			{}
sgs.ai_skill_cardchosen =		{}
sgs.ai_skill_use =				{}
sgs.ai_cardneed =				{}
sgs.ai_skill_use_func =	 		{}
sgs.ai_skills =			 		{}
sgs.ai_slash_weaponfilter = 	{}
sgs.ai_slash_prohibit =	 		{}
sgs.ai_view_as =				{}
sgs.ai_cardsview =				{}
sgs.ai_cardsview_valuable = 	{}
sgs.dynamic_value =			 	{
	damage_card =			{},
	control_usecard =		{},
	control_card =			{},
	lucky_chance =			{},
	benefit =				{}
}
sgs.ai_choicemade_filter =		{
	cardUsed =				{},
	cardResponded =			{},
	skillInvoke =			{},
	skillChoice =			{},
	cardChosen =			{},
	pindian =				{}
}
sgs.ai_need_damaged =			{}
sgs.ai_event_callback =	 		{}
sgs.ai_NeedPeach =				{}
sgs.ai_damage_effect =			{}
sgs.ai_judgeGood =				{}
sgs.ai_need_retrial_func =		{}
sgs.ai_use_revises =			{}
sgs.ai_guhuo_card =		 		{}
sgs.ai_target_revises =	 		{}
sgs.ai_useto_revises =			{}
sgs.ai_skill_defense = 			{}
sgs.card_value = 				{}
sgs.ai_card_priority =			{}
sgs.ai_poison_card =			{}
sgs.ai_skill_playerschosen =	{}
sgs.ai_used_revises =			{}
sgs.weapon_range = 				{}


for i=sgs.GameStart,sgs.EventForDiy do
	sgs.ai_event_callback[i] = {}
end

do
	sgs.playerRoles = {lord=0,loyalist=0,rebel=0,renegade=0}
	sgs.aiData = {drawData={},convertData={},damageData={},throwData={}}
	
	sgs.ai_type_name = {"SkillCard","BasicCard","TrickCard","EquipCard"}
	
	sgs.lose_equip_skill = "kofxiaoji|xiaoji|xuanfeng|nosxuanfeng|tenyearxuanfeng|mobilexuanfeng"
	
	sgs.need_kongcheng = "lianying|noslianying|kongcheng|sijian|hengzheng"
	
	sgs.masochism_skill = "guixin|yiji|fankui|jieming|xuehen|neoganglie|ganglie|vsganglie|enyuan|"..
						"fangzhu|nosenyuan|langgu|quanji|zhiyu|renjie|tanlan|tongxin|huashen|duodao|chengxiang|benyu"
	
	sgs.wizard_skill = "nosguicai|guicai|guidao|olguidao|jilve|tiandu|luoying|noszhenlie|huanshi|jinshenpin"
	
	sgs.wizard_harm_skill = "nosguicai|guicai|guidao|olguidao|jilve|jinshenpin|midao|zhenyi"
	
	sgs.priority_skill = "dimeng|haoshi|qingnang|nosjizhi|jizhi|guzheng|qixi|jieyin|guose|duanliang|jujian|fanjian|"..
						"neofanjian|lijian|noslijian|manjuan|tuxi|qiaobian|yongsi|zhiheng|luoshen|nosrende|rende|"..
						"mingce|wansha|gongxin|jilve|anxu|qice|yinling|qingcheng|houyuan|zhaoxin|shuangren|zhaxiang|"..
						"xiansi|junxing|bifa|yanyu|shenxian|jgtianyun"
	
	sgs.save_skill = "jijiu|buyi|nosjiefan|chunlao|tenyearchunlao|secondtenyearchunlao|longhun|newlonghun"
	
	sgs.exclusive_skill = "huilei|duanchang|wuhun|buqu|dushi"
	
	sgs.dont_kongcheng_skill = "yuce|tanlan|toudu|qiaobian|jieyuan|anxian|liuli|chongzhen|tianxiang|tenyeartianxiang|"..
						"oltianxiang|guhuo|nosguhuo|olguhuo|leiji|nosleiji|olleiji|qingguo|yajiao|chouhai|tenyearchouhai|"..
						"nosrenxin|taoluan|tenyeartaoluan|huisheng|zhendu|newzhendu|kongsheng|zhuandui|longhun|"..
						"newlonghun|fanghun|olfanghun|mobilefanghun|zhenshan|jijiu|daigong|yinshicai"
	
	sgs.Active_cardneed_skill = "paoxiao|tenyearpaoxiao|olpaoxiao|tianyi|xianzhen|shuangxiong|nosjizhi|jizhi|guose|"..
						"duanliang|qixi|qingnang|luoyi|guhuo|nosguhuo|jieyin|zhiheng|rende|nosrende|nosjujian|luanji|"..
						"qiaobian|lirang|mingce|fuhun|spzhenwei|nosfuhun|nosluoyi|yinbing|jieyue|sanyao|xinzhan"
	
	sgs.notActive_cardneed_skill = "kanpo|guicai|guidao|beige|xiaoguo|liuli|tianxiang|jijiu|leiji|nosleiji"..
						"qingjian|zhuhai|qinxue|jspdanqi|"..sgs.dont_kongcheng_skill
	
	sgs.cardneed_skill = sgs.Active_cardneed_skill.."|"..sgs.notActive_cardneed_skill
	
	sgs.drawpeach_skill = "tuxi|qiaobian"
	
	sgs.recover_hp_skill = "nosrende|rende|tenyearrende|kofkuanggu|kuanggu|tenyearkuanggu|zaiqi|mobilezaiqi|jieyin|"..
						"qingnang|shenzhi|longhun|newlonghun|ytchengxiang|quji|dev_zhiyu|dev_pinghe|dev_qiliao|dev_saodong"
	
	sgs.recover_skill =	"yinghun|hunzi|nosmiji|zishou|newzishou|olzishou|tenyearzishou|ganlu|xueji|shangshi|nosshangshi|"..
						"buqu|miji|"..sgs.recover_hp_skill
	
	sgs.use_lion_skill = "longhun|newlonghun|duanliang|qixi|guidao|noslijian|lijian|jujian|nosjujian|zhiheng|mingce|"..
						"yongsi|fenxun|gongqi|yinling|jilve|qingcheng|neoluoyi|diyyicong"
	
	sgs.need_equip_skill = "shensu|tenyearshensu|mingce|jujian|beige|yuanhu|huyuan|gongqi|nosgongqi|yanzheng|qingcheng|"..
						"neoluoyi|longhun|newlonghun|shuijian|yinbing"
	
	sgs.straight_damage_skill = "qiangxi|nosxuanfeng|duwu|danshou"
	
	sgs.double_slash_skill = "paoxiao|tenyearpaoxiao|olpaoxiao|fuhun|tianyi|xianzhen|zhaxiang|lihuo|jiangchi|shuangxiong|"..
						"qiangwu|luanji"
	
	sgs.need_maxhp_skill = "yingzi|zaiqi|yinghun|hunzi|juejing|ganlu|zishou|miji|chizhong|xueji|quji|xuehen|shude|"..
						"neojushou|tannang|fangzhu|nosshangshi|nosmiji|yisuan|xuhe"
	
	sgs.bad_skills = "benghuai|wumou|shiyong|yaowu|zaoyao|chanyuan|chouhai|tenyearchouhai|lianhuo|ranshang"
	
	sgs.hit_skill = "wushuang|fuqi|tenyearfuqi|zhuandui|tieji|nostieji|dahe|olqianxi|qianxi|tenyearjianchu|oljianchu|"..
					"wenji|tenyearbenxi|mobileliyong|olwushen|tenyearliegong|liegong|kofliegong|tenyearqingxi|wanglie|"..
					"conqueror|zhaxiang|tenyearyijue|yijue|xiongluan|xiying|"
	
	sgs.Friend_All = 0
	sgs.Friend_Draw = 1
	sgs.Friend_Male = 2
	sgs.Friend_Female = 3
	sgs.Friend_Wounded = 4
	sgs.Friend_MaleWounded = 5
	sgs.Friend_FemaleWounded = 6
	sgs.Friend_Weak = 7
end

function SmartAI:initialize(player)
	self.player = player
	self.room = player:getRoom()
	self.role = player:getRole()
	self.lua_ai = sgs.LuaAI(player)
	self.lua_ai.callback = function(full_method_name,...)
		--[[if self.room:getTag("callback"):toBool() then
			self.room:removeTag("callback")
			sgs.callback_time = os.time()
		elseif sgs.callback_time and os.time()-sgs.callback_time>9 then
		 	self.room:writeToConsole(full_method_name)
			return
		end]]
		local method_name = 1
		while true do
			local found = string.find(full_method_name,"::",method_name)
			if type(found)=="number" then method_name = found+2 else break end
		end
		method_name = string.sub(full_method_name,method_name)
		local method = self[method_name]
		if method then
			current_self = self
			local success,result1,result2 = pcall(method,self,...)
			if success then return result1,result2
			else
		 		self.room:writeToConsole(method_name)
				self.room:writeToConsole(result1)
				self.room:outputEventStack()
				for _,w in ipairs({...})do
					if type(w)=="string" then self.room:writeToConsole(w) end
				end
			end
		end
	end
	if self.room:getTag("initialized"):toBool()~=true then
		sgs.defense = {}
		sgs.drawData = {}
		sgs.turncount = 0
		sgs.throwData = {}
		sgs.damageData = {}
		sgs.convertData = {}
		sgs.recoverData = {}
		global_room = self.room
		sgs.sort_time = os.time()
		sgs.getMode = self.room:getMode()
		sgs.AIChat = sgs.GetConfig("AIChat",true)
		global_delay = sgs.GetConfig("OriginAIDelay",0)
		self.room:setTag("initialized",sgs.QVariant(true))
		sgs.ai_humanized = sgs.GetConfig("AIHumanized",true)
		self.room:writeToConsole(version..",Powered by ".._VERSION)
		for i,ap in sgs.qlist(self.room:getAlivePlayers())do
			sgs.ai_role[ap:objectName()] = "neutral"
			sgs.roleValue[ap:objectName()] = {lord=0,loyalist=0,rebel=0,renegade=0}
			sgs.roleValue[ap:objectName()][ap:getRole()] = 0
			if ap:getRole()=="lord" then
				sgs.roleValue[ap:objectName()]["lord"] = 99999
				sgs.roleValue[ap:objectName()]["loyalist"] = 65535
				sgs.ai_role[ap:objectName()] = "loyalist"
			elseif isRolePredictable() then
				if ap:getRole()=="renegade" then sgs.explicit_renegade = true end
				sgs.roleValue[ap:objectName()][ap:getRole()] = 65535
				sgs.ai_role[ap:objectName()] = ap:getRole()
			end
			sgs.defense[ap:objectName()] = i
			sgs.ai_NeedPeach[ap:objectName()] = 0
		end
	end
	self.toUse = {}
	self.keepdata = {}
	self.keepValue = {}
	current_self = self
	self.harsh_retain = true
	self.aiUsing = sgs.IntList()
	self.disabled_ids = sgs.IntList()
	sgs.ais[player:objectName()] = self
	self:updatePlayers(false)
end

function sgs.getPlayerSkillList(player)
	local skills = {}
	for _,skill in sgs.qlist(player:getSkillList(true))do
		if skill:isLordSkill() then if player:hasLordSkill(skill) then table.insert(skills,skill) end
		elseif player:hasSkill(skill) then table.insert(skills,skill) end
	end
	if player:hasSkill("weidi") then
		local gl = global_room:getLord()
		if gl and gl~=player then
			for _,skill in sgs.qlist(gl:getSkillList(true))do
				if skill:isLordSkill() then table.insert(skills,skill) end
			end
		end
	end
	return skills
end

function sgs.getCardNumAtCertainPlace(card,place)
	local num = 0
	if card and card:isVirtualCard() then
		for _,id in sgs.qlist(card:getSubcards())do
			if global_room:getCardPlace(id)==place
			then num = num+1 end
		end
	elseif place==sgs.Player_PlaceHand
	then num = 1 end
	return num
end

function sgs.getValue(player)
	--if type(player)~="userdata" then return 0 end
	return player:getHp()*2+player:getHandcardNum()+player:getHandPile():length()
end

local function gdt(di)
	local n = 0
	for _,x in ipairs(di)do
		n = n+x
	end
	return n/#di
end

function sgs.getDefense(player,start)--状态值
	--if type(player)~="userdata" then return 0 end
	if start~=true or not sgs.ai_humanized or sgs.turncount<1 then
		return sgs.getValue(player)+sgs.defense[player:objectName()]
	end
	local drawData = {}	
	for t,st in pairs(sgs.aiData.drawData)do
		drawData[t] = gdt(st)
	end
	local damageData = {}
	for t,st in pairs(sgs.aiData.damageData)do
		damageData[t] = gdt(st)
	end
	local throwData = {}
	for t,st in pairs(sgs.aiData.throwData)do
		throwData[t] = gdt(st)
	end
	local convertData = {}
	for t,st in pairs(sgs.aiData.convertData)do
		convertData[t] = gdt(st)
	end
	local dt = aiConnect(player)
	local defense = #dt*0.2
	for _,ac in ipairs(dt)do
		local invoke = sgs.ai_skill_defense[ac]
		if type(invoke)=="function"	then
			invoke = invoke(current_self,player)
			if type(invoke)=="number" then defense = defense+invoke end
		elseif type(invoke)=="number" then defense = defense+invoke end
		invoke = sgs.Sanguosha:getTriggerSkill(ac)
		if invoke and invoke:hasEvent(sgs.Damaged)
		then defense = defense+3 end
		invoke = sgs.Sanguosha:getSkill(ac)
		if invoke and string.find(invoke:getDescription(),"已损失")
		then defense = defense+2 end
		invoke = drawData[ac]
		if type(invoke)=="number"
		then defense = defense-invoke end
		invoke = damageData[ac]
		if type(invoke)=="number"
		then defense = defense-invoke end
		invoke = throwData[ac]
		if type(invoke)=="number"
		then defense = defense-invoke end
		invoke = convertData[ac]
		if type(invoke)=="number"
		then defense = defense-invoke end
	end
	for k,n in pairs(current_self.keepdata)do
		for _,c in ipairs(sgs.ais[player:objectName()]:getCard(k,true))do
			if c:getTypeId()<1 or c:isVirtualCard() then defense = defense-n break end
		end
	end
	for _,masochism in ipairs(sgs.masochism_skill:split("|"))do
		if player:hasSkill(masochism) and current_self:isGoodHp(player)
		then defense = defense+1 end
	end
	if player:getMark("@tied")>0 then defense = defense+1 end
	if player:hasSkill("chengxiang") then defense = defense+2 end
	if player:hasSkill("guixin") then defense = defense+player:aliveCount()-1 end
	if player:getMark("xhate")>0 and player:hasLordSkill("shichou") then
		for _,p in sgs.qlist(player:getAliveSiblings())do
			if p:getMark("hate_"..player:objectName())>0 and p:getMark("@hate_to")>0
			then defense = defense+p:getHp() break end
		end
	end
	if player:getHp()>2 then
		if player:getHandcardNum()>1 then
			if player:hasSkill("rende") then defense = defense+1 end
			if player:hasSkill("nosrende") then defense = defense+1 end
		end
	else
		defense = defense-0.4
	end
	if player:getHp()>1 then
		if player:hasSkill("kuanggu") then defense = defense+0.5 end
		if player:hasSkill("kofkuanggu") then defense = defense+1 end
		if player:hasSkill("zaiqi") then defense = defense+player:getLostHp()*0.5 end
	end
	if not player:hasFlag("CurrentPlayer") and player:hasSkill("aocai") then defense = defense+0.5 end
	if not hasManjuanEffect(player) and player:hasSkill("wanrong") then defense = defense+0.5 end
	if player:hasSkill("tianxiang") then defense = defense+player:getHandcardNum()*0.5 end
	if player:getHp()>getBestHp(player) then defense = defense+0.8 end
	if isLord(player) then
		defense = defense-0.4
		if sgs.isLordInDanger()
		then defense = defense-0.7 end
	end
	if not player:faceUp() then defense = defense-1 end
	if player:getMark("@skill_invalidity")>0 then defense = defense-5 end
	if player:hasSkill("qianhuan") then defense = defense+player:getPile("sorcery"):length() end
	if player:hasSkill("qingnang") then defense = defense-2 end
	if player:hasSkill("qiaobian") then defense = defense-2.4 end
	if player:hasSkill("jieyin") then defense = defense-2.3 end
	if player:hasSkill("xiliang") then defense = defense+2 end
	if player:hasSkill("guhuo") then
		for _,p in sgs.qlist(player:getAliveSiblings())do
			if p:hasSkill("chanyuan") then defense = defense+1 end
		end
	end
	if player:hasSkill("yishe") then defense = defense+2 end
	if player:hasSkill("paiyi") then defense = defense+1.5 end
	if player:hasSkill("yongsi") then defense = defense-2 end
	drawData = global_room:getCurrent()
	if drawData then
		defense = defense+(player:aliveCount()-(player:getSeat()-drawData:getSeat())%player:aliveCount())/4
	end
	return defense
end

function SmartAI:assignKeep(start)
	if start then
		self.keepdata = {}
		for k,v in pairs(sgs.ai_keep_value)do
			self.keepdata[k] = v
		end
		for _,sk in ipairs(sgs.getPlayerSkillList(self.player))do
			local kv = sgs[sk:objectName().."_keep_value"]
			if kv then
				for k,v in pairs(kv)do
					self.keepdata[k] = v
				end
			end
		end
	end
	if sgs.turncount<=1 or #self.enemies<1 then self.keepdata.Jink = 4.2 end
	if self:isWeak(nil,false) then
		self.keepdata.Analeptic = (self.keepdata.Jink or 5.2)+2
		if self:hasSkills("buyi",self.friends) then
			self.keepdata.EquipCard = 7.9
			self.keepdata.TrickCard = 8
			self.keepdata.Peach = 10
		end
	else
		if self.player:getHandcardNum()>3 then
			for _,f in ipairs(self.friends_noself)do
				if f:getJudgingArea():length()>0 and getCardsNum("Nullification",f,self.player)<1--self:willSkipDrawPhase(f) or self:willSkipPlayPhase(f)
				then self.keepdata.Nullification = 5.5 break end
			end
		end
		if not self:isGoodTarget(self.player,self.friends)
		or self:needToLoseHp() then
			self.keepdata.Slash = 5
			self.keepdata.FireSlash = 5.1
			self.keepdata.ThunderSlash = 5.2
			self.keepdata.Jink = 4.5
		end
	end
	for _,enemy in ipairs(self.enemies)do
		if enemy:hasSkill("nosqianxi",true)
		and enemy:distanceTo(self.player)==1
		then self.keepdata.Jink = 6 end
	end
	self.keepValue = {}
	for _,c in sgs.qlist(self.player:getCards("he"))do
		self.keepValue[c:toString()] = self:getKeepValue(c,true)
	end
	local kept = {}
	for _,h in ipairs(self:sortByKeepValue(self.player:getHandcards(),true))do
		self.keepValue[h:toString()] = self:getKeepValue(h,kept)
		table.insert(kept,h)
	end
end

function SmartAI:getKeepValue(card,kept)
	--if type(card)~="userdata" then return 0 end
	if kept==nil then
		return self.keepValue[card:toString()] or self.keepdata[card:getClassName()] or sgs.ai_keep_value[card:getClassName()] or 0
	end
	local id = card:getEffectiveId()
	local class = card:getClassName()
	local owner = self.room:getCardOwner(id) or self.player
	local x = self.keepdata[class] or sgs.ai_keep_value[class] or 0
	if type(kept)=="table" then
		x = self.keepValue[card:toString()] or x
		for _,kc in ipairs(kept)do
			if card:isKindOf("Slash") and isCard("Slash",kc,owner)
			or isCard(class,kc,owner) then x = x-1.1 end
		end
		return x
	end
	for k,v in pairs(self.keepdata)do
		if v>x and isCard(k,card,owner)
		then x = v end
	end
	if self.room:getCardPlace(id)==sgs.Player_PlaceEquip then
		if self:loseEquipEffect(owner) or #self:poisonCards({card},owner)>0 then
			if card:isKindOf("OffensiveHorse") then x = x-9
			elseif card:isKindOf("Weapon") then x = x-8
			else x = x-7 end
		elseif self:needKongcheng() then x = x+5 end
		if card:isKindOf("Armor") then
			x = x+(self:isWeak() and 3.2 or 2.2)
			if self:needToThrowArmor() then x = x-7 end
		elseif card:isKindOf("Weapon") then x = x+(owner:getPhase()<=sgs.Player_Play and self:slashIsAvailable() and 1.4 or 1.2)
		elseif card:isKindOf("WoodenOx") then x = x+(owner:getPile("wooden_ox"):length()*2.2)
		elseif card:isKindOf("DefensiveHorse") then x = x+(self:isWeak() and 2.3 or 1.9)
		elseif card:isKindOf("OffensiveHorse") then x = x+1.7
		else x = x+1.8 end
		return x+3
	end
	local si,ni,ci,vs,vn = 0,0,0,0,0
	for _,sk in sgs.qlist(owner:getVisibleSkillList(true))do
		class = sgs[sk:objectName().."_suit_value"]
		if class then
			class = class[card:getSuitString()]
			if type(class)=="number" then
				vs = vs+class
				si = si+1
			end
		end
		class = sgs[sk:objectName().."_number_value"]
		if class then
			class = class[card:getNumberString()]
			if type(class)=="number" then
				vn = vn+class
				ni = ni+1
			end
		end
		class = sgs.card_value[sk:objectName()]
		if class then
			local cv = class[card:getSuitString()]
			if type(cv)=="number" then x = x+cv end
			cv = class[card:getNumberString()]
			if type(cv)=="number" then x = x+cv end
			cv = class[card:getClassName()]
			if type(cv)=="number" then x = x+cv end
			cv = class[card:objectName()]
			if type(cv)=="number" then x = x+cv end
			cv = class[card:getColorString()]
			if type(cv)=="number" then x = x+cv end
			cv = class[card:getType()]
			if type(cv)=="number" then x = x+cv end
			ci = ci+1
		end
	end
	if ci>0 then x = x/ci end
	if si>0 then x = x+vs/si end
	if ni>0 then x = x+vn/ni end

	if card:isKindOf("Slash") then
		if card:isKindOf("NatureSlash") then x = x+0.03 end
		if card:isRed() then
			if owner:hasSkill("jiang") then x = x+0.04 end
			if card:getSuit()==sgs.Card_Heart and owner:hasSkills("wushen|olwushen") then x = x+0.03 end
			x = x+0.02
		end
		if id>=0 and sgs.Sanguosha:getEngineCard(id):isKindOf("Analeptic")
		and owner:hasSkills("jinjiu|mobilejinjiu") then x = x-0.02 end
	end
	class = owner:getPileName(id)
	if class=="wooden_ox" or class:contains("&")
	then x = x-0.1 end
	class = {}
	for i,s in ipairs({"club","spade","diamond","heart","no_suit"})do
		class[s] = i
	end
	x = x+(class[card:getSuitString()] or 6)/100
	return x+card:getNumber()/100
end

function SmartAI:getUseValue(card)
	--if type(card)~="userdata" then return 0 end
	local v = sgs.ai_use_value[card:getClassName()] or 0
	if card:isKindOf("LuaSkillCard")
	then v = sgs.ai_use_value[card:objectName()] or v
	elseif card:isKindOf("EquipCard") then
		if self.player:hasEquip(card) then
			if #self:poisonCards({card})>0 then v = 0
			elseif card:isKindOf("OffensiveHorse")
			and self.player:getAttackRange()>2 then v = v+2.5
			elseif card:isKindOf("DefensiveHorse")
			and self.player:hasArmorEffect("EightDiagram")
			then v = v+2.5 else v = v+3 end
		else
			if self:loseEquipEffect() then v = v+10
			elseif not self:getSameEquip(card) then v = v+2.7 end
			if card:isKindOf("Weapon")
			and table.contains(self.toUse,card)
			then v = v+2 end
		end
		if card:isKindOf("EightDiagram")
		and self.role=="loyalist" and self.player:getKingdom()=="wei"
		and getLord(self.player) and getLord(self.player):hasLordSkill("hujia")
		and not self.player:hasSkill("bazhen") then v = v+4 end
	elseif card:isKindOf("BasicCard") then
		if card:isKindOf("Slash") then
			if self.player:hasFlag("TianyiSuccess")
			or self.player:hasFlag("JiangchiInvoke")
			then v = 8.7 end
			if self.player:getPhase()==sgs.Player_Play
			and #self.enemies>0 and card:isAvailable(self.player)
			and self:getCardsNum("Slash")==1
			then v = v+5 end
			if self:hasCrossbowEffect()
			then v = v+4 end
		elseif card:isKindOf("Jink") then
			if self:getCardsNum("Jink")>1
			then v = v-6 end
		elseif card:isKindOf("Peach")
		and self.player:isWounded()
		then v = v+6 end
	elseif card:isKindOf("TrickCard") then
		if card:isKindOf("Duel") then v = v+self:getCardsNum("Slash")*2
		elseif card:isKindOf("Collateral") and self.player:getWeapon()
		and not self:loseEquipEffect() then v = 2 end
	end
	if self.player:isLastHandCard(card)
	and self.player:hasSkills(sgs.need_kongcheng)
	then v = v+9 end
	if self.player:getPhase()<=sgs.Player_Play then
		self.useValue = true
		v = self:adjustUsePriority(card,v)
		self.useValue = false
	end
	return v
end

function SmartAI:getUsePriority(card)
	--if type(card)~="userdata" then return 0 end
	local upv = sgs.ai_use_priority[card:getClassName()] or 0
	if card:isKindOf("EquipCard") then
		if self:getSameEquip(card) then
		elseif card:isKindOf("Weapon") then upv = upv+2
		elseif card:isKindOf("Armor") then upv = upv+2.2
		elseif card:isKindOf("DefensiveHorse") then upv = upv+2.8
		elseif card:isKindOf("OffensiveHorse") then upv = upv+2.5 end
		if self:loseEquipEffect() then upv = upv+6 end
	elseif card:getTypeId()<1 then
		if card:isKindOf("LuaSkillCard")
		then upv = sgs.ai_use_priority[card:objectName()] or upv end
		return upv
	end
	return self:adjustUsePriority(card,upv)
end

function SmartAI:adjustUsePriority(card,v)
	local suits_value,suits = false,{"club","spade","diamond","heart"}
	for _,s in ipairs(sgs.getPlayerSkillList(self.player))do
		local cb = sgs.ai_card_priority[s:objectName()]
		if type(cb)=="table" then
			cb = cb[card:getSuitString()]
			if type(cb)=="number" then v = v+cb end
			cb = cb[card:getNumberString()]
			if type(cb)=="number" then v = v+cb end
			cb = cb[card:getClassName()]
			if type(cb)=="number" then v = v+cb end
			cb = cb[card:objectName()]
			if type(cb)=="number" then v = v+cb end
			cb = cb[card:getColorString()]
			if type(cb)=="number" then v = v+cb end
			cb = cb[card:getSkillName()]
			if type(cb)=="number" then v = v+cb end
		elseif type(cb)=="function" then
			cb = cb(self,card,v)
			if type(cb)=="number" then v = v+cb end
		end
		if suits_value then continue end
		cb = sgs.ai_suit_priority[s]
		if type(cb)=="function" then
			suits_value = true
			suits = cb(self,card):split("|")
		elseif type(cb)=="string" then
			suits_value = true
			suits = cb:split("|")
		end
	end
	table.insert(suits,"no_suit")
	suits_value = self.player:getPileName(card:getEffectiveId())
	if suits_value=="wooden_ox" or suits_value:startsWith("&") then v = v+0.2 end
	if card:targetFixed() then
		if card:getTypeId()<2 then
			suits_value = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,card,self.player);
			if suits_value~=0 then v = v-math.min(suits_value*0.1,1) end
		end
	elseif card:isDamageCard() then
		for _,p in ipairs(self.enemies)do
			suits_value = sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,self.player,card,p);
			if suits_value~=0 then v = v-math.min(suits_value*0.1,1) end
		end
	end
	if card:isVirtualCard() then v = v-card:subcardsLength()*0.15 end
	suits_value = {}
	for i,suit in ipairs(suits)do
		suits_value[suit] = -i
	end
	v = v+(suits_value[card:getSuitString()] or 0)/100
	return v+(13-card:getNumber())/100
end

function SmartAI:getDynamicUsePriority(card)
	--if type(card)~="userdata" then return 0 end
	if card:hasFlag("AIGlobal_KillOff") then return 15
	elseif card:isKindOf("DelayedTrick") and #card:getSkillName()>0
	then return (sgs.ai_use_priority[card:getClassName()] or 0.1)-0.1
	elseif card:isKindOf("Duel") then
		if self:hasCrossbowEffect()
		or self.player:canSlashWithoutCrossbow()
		then return sgs.ai_use_priority.Slash-0.1 end
	end
	local value = self:getUsePriority(card)
	if card:isKindOf("Weapon") and #self.enemies>0
	and self.player:getPhase()<=sgs.Player_Play then
		local vw,inAttackRange = self:evaluateWeapon(card)
		if inAttackRange then value = value+0.5 end
		value = value+vw/10
	elseif card:isKindOf("AmazingGrace") then
		local dv = 10
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			dv = dv-1
			if self:isEnemy(p) then dv = dv-((p:getHandcardNum()+p:getHp())/p:getHp())*dv
			else dv = dv+((p:getHandcardNum()+p:getHp())/p:getHp())*dv end
		end
		value = value+dv
	end
	return value
end

function SmartAI:cardNeed(card)
	--if type(card)~="userdata" then return 0 end
	local value = self:getUseValue(card)
	if isCard("Peach",card,self.player) then
		if self:isWeak(self.friends) then value = value+4 end
		if (self.player:getHp()<3 or self.player:getLostHp()>1)
		or self.player:hasSkills("kurou|benghuai") then value = value+6 end
	end
	if card:getTypeId()>1 and self:hasSkills("buyi",self.friends) then
		if (self.player:getHp()<3 or self.player:getLostHp()>1 and self:isWeak())
		or self.player:hasSkills("kurou|benghuai") then value = value+5 end
	end
	local i = 1
	for _,askill in sgs.qlist(self.player:getVisibleSkillList(true))do
		local v = sgs[askill:objectName().."_keep_value"]
		if type(v)=="table" then
			v = v[card:getClassName()]
			if type(v)=="number" then
				value = value+v/i
				i = i+1
			end
		end
		v = sgs[askill:objectName().."_suit_value"]
		if type(v)=="table" then
			v = v[card:getSuitString()]
			if type(v)=="number" then
				value = value+v/i
				i = i+1
			end
		end
	end
	if card:getTypeId()<3 then
		value = value-(self:getCardsNum(card:getClassName())/4)*value
		if sgs.ai_poison_card[card:objectName()] then value = value-10 end
	end
	if self:isWeak() and isCard("Jink,Analeptic",card,self.player) then value = value+5
	elseif isCard("Slash",card,self.player) and (self:getCardsNum("Crossbow")>0 or self:hasCrossbowEffect()) then value = value+3
	elseif card:isKindOf("Crossbow") then
		if self.player:hasSkills("luoshen|yongsi|kurou|keji|wusheng|tenyearwusheng|wushen|olwushen|chixin")
		then value = value+self:getCardsNum("Slash")*2 end
	elseif card:isKindOf("Axe") then
		if self.player:hasSkills("luoyi|jiushi|jiuchi|pojun")
		then value = value+5 end
	elseif card:isKindOf("Nullification")
	and self:getCardsNum("Nullification")<2 then
		for _,friend in ipairs(self.friends)do
			if friend:getJudgingArea():length()>0
			then value = value+5 break end
		end
	end
	return value
end

sgs.ai_compare_funcs = {
	value = function(a,b)
		return sgs.getValue(a)<sgs.getValue(b)
	end,
	chaofeng = function(a,b)
		return sgs.getDefense(a)<sgs.getDefense(b)
	end,
	threat = function(a,b)
		local d1 = a:getHandcardNum()
		for _,p in sgs.qlist(a:getAliveSiblings())do
			if a:canSlash(p) then d1 = d1+10/sgs.getDefense(p) end
		end
		local d2 = b:getHandcardNum()
		for _,p in sgs.qlist(b:getAliveSiblings())do
			if b:canSlash(p) then d2 = d2+10/sgs.getDefense(p) end
		end
		return d1>d2
	end
}

function SmartAI:sort(players,key,anti)
	players = sgs.QList2Table(players)
	if #players<2 then return players end
	if os.time()-sgs.sort_time>1 then
		for _,p in ipairs(players)do
			sgs.defense[p:objectName()] = sgs.getDefense(p,true)
		end
		sgs.sort_time = os.time()
	end
	local func = function(a)
		return self:getDefenseSlash(a)
	end
	if key=="hp" then
		func = function(a)
			return a:getHp()+a:getHujia()
		end
	elseif key=="HP" then
		func = function(a)
			return a:getHp()
		end
	elseif key=="handcard" then
		func = function(a)
			return a:getHandcardNum()
		end
	elseif key=="card" then
		func = function(a)
			return a:getCardCount()
		end
	elseif key=="handcard_defense" then
		func = function(a)
			return a:getHandcardNum()+self:getDefenseSlash(a)
		end
	elseif key=="equip" then
		func = function(a)
			return a:getEquips():length()
		end
	elseif key=="maxhp" then
		func = function(a)
			return a:getMaxHp()
		end
	elseif key=="maxcards" then
		func = function(a)
			return a:getMaxCards()
		end
	elseif key=="skill" then
		func = function(a)
			return a:getVisibleSkillList():length()
		end
	elseif key=="chaofeng" then
		table.sort(players,sgs.ai_compare_funcs.chaofeng)
		if anti then players = sgs.reverse(players) end
		return players
	end
	local bcv = {}
	for _,p in ipairs(players)do
		bcv[p:objectName()] = func(p) or 0
	end
	local function compare_func(a,b)
		local va = bcv[a:objectName()]
		local vb = bcv[b:objectName()]
		if va==vb then
			return sgs.ai_compare_funcs.chaofeng(a,b)
		end
		if anti then return va>vb end
		return va<vb
	end
	table.sort(players,compare_func)
	return players
end

function SmartAI:sortByKeepValue(cards,inverse,flags)
	cards = sgs.QList2Table(cards)
	if flags=="j" then
		for i=#cards,1,-1 do
			if self.player:isJilei(cards[i]) then table.remove(cards,i) end
		end
	end
	if #cards<2 then return cards end
	for _,c in ipairs(cards)do
		local v = self:getKeepValue(c)
		if table.contains(self.toUse,c) then v = v+2 end
		if self.player:isLocked(c) then v = v/2 end
		self.keepValue[c:toString()] = v
	end
	local function compare_func(a,b)
		if inverse then return self:getKeepValue(a)>self:getKeepValue(b) end
		return self:getKeepValue(a)<self:getKeepValue(b)
	end
	table.sort(cards,compare_func)
	return cards
end

function SmartAI:sortByUseValue(cards,inverse,flags)
	cards = sgs.QList2Table(cards)
	if flags=="j" then
		for i=#cards,1,-1 do
			if self.player:isJilei(cards[i]) then table.remove(cards,i) end
		end
	end
	if #cards<2 then return cards end
	local bcv = {}
	for _,c in ipairs(cards)do
		bcv[c:toString()] = self:getUseValue(c) or 0
	end
	local function compare_func(a,b)
		if inverse then return bcv[a:toString()]<bcv[b:toString()] end
		return bcv[a:toString()]>bcv[b:toString()]
	end
	table.sort(cards,compare_func)
	return cards
end

function SmartAI:sortByUsePriority(cards,inverse,flags)
	cards = sgs.QList2Table(cards)
	if flags=="j" then
		for i=#cards,1,-1 do
			if self.player:isJilei(cards[i]) then table.remove(cards,i) end
		end
	end
	if #cards<2 then return cards end
	local bcv = {}
	for _,c in ipairs(cards)do
		bcv[c:toString()] = self:getUsePriority(c) or 0
	end
	local function compare_func(a,b)
		if inverse then return bcv[a:toString()]<bcv[b:toString()] end
		return bcv[a:toString()]>bcv[b:toString()]
	end
	table.sort(cards,compare_func)
	return cards
end

function SmartAI:sortByDynamicUsePriority(cards,inverse,flags)
	cards = sgs.QList2Table(cards)
	if flags=="j" then
		for i=#cards,1,-1 do
			if self.player:isJilei(cards[i]) then table.remove(cards,i) end
		end
	end
	if #cards<2 then return cards end
	local bcv = {}
	for _,c in ipairs(cards)do
		bcv[c:toString()] = self:getDynamicUsePriority(c) or 0
	end
	local function compare_func(a,b)
		if inverse then return bcv[a:toString()]<bcv[b:toString()] end
		return bcv[a:toString()]>bcv[b:toString()]
	end
	table.sort(cards,compare_func)
	return cards
end

function SmartAI:sortByCardNeed(cards,inverse,flags)
	cards = sgs.QList2Table(cards)
	if flags=="j" then
		for i=#cards,1,-1 do
			if self.player:isJilei(cards[i]) then table.remove(cards,i) end
		end
	end
	if #cards<2 then return cards end
	local bcv = {}
	for _,c in ipairs(cards)do
		bcv[c:toString()] = self:cardNeed(c) or 0
	end
	local function compare_func(a,b)
		if inverse then return bcv[a:toString()]>bcv[b:toString()] end
		return bcv[a:toString()]<bcv[b:toString()]
	end
	table.sort(cards,compare_func)
	return cards
end

function SmartAI:getPriorTarget()
	if #self.enemies<1 then return end
	self:sort(self.enemies)
	return self.enemies[1]
end

function SmartAI:compareRoleEvaluation(player,first,second)
	if player:getRole()=="lord" then return "loyalist" end
	if isRolePredictable() then return player:getRole() end
	if (first=="renegade" or second=="renegade") and sgs.ai_role[player:objectName()]=="renegade" then return "renegade" end
	if sgs.ai_role[player:objectName()]==second then return second end
	if sgs.ai_role[player:objectName()]==first then return first end
	return "neutral"
end

function isRolePredictable(classical)
	return sgs.getMode=="02p" or string.sub(sgs.getMode,3,3)~="p"
	or not classical and sgs.GetConfig("RolePredictable",false)
end

function outputRoleValues(p,level)
	global_room:writeToConsole(p:getLogName().." "..level..
	" "..sgs.Sanguosha:translate(sgs.ai_role[p:objectName()])..
	" 忠值:"..sgs.roleValue[p:objectName()].loyalist..
	" 内值:"..sgs.roleValue[p:objectName()].renegade..
	" "..sgs.Sanguosha:translate(sgs.gameProcess())..
	","..string.format("%3.3f",sgs.gameProcess(true)))
end

function sgs.updateIntention(from,to,level)
	if sgs.ai_doNotUpdateIntenion then
		sgs.ai_doNotUpdateIntenion = false
		level = 0
	end
	if not from or not to
	or from:objectName()==to:objectName() then return end
	level = level+to:getMark("Intention"..from:objectName())
	to:setMark("Intention"..from:objectName(),0)
	level = level*math.random(0.3,0.5)
	if from:getRole()=="lord" then
		if level>0 and sgs.ai_role[to:objectName()]~="rebel" and math.random()<0.2
		and AIChat(to) and sgs.roleValue[to:objectName()].loyalist>-level/2 then
			local intention = {
				"。。。。。。",
				"我特么....",
				"<#"..math.random(1,56).."#>",
				"<#"..math.random(1,56).."#>",
				"<#44#>",
				"小心我跳反！"
			}
			if from:getPhase()<=sgs.Player_Play then
				table.insert(intention,"不要乱来啊")
				table.insert(intention,"主公别乱打啊")
				table.insert(intention,"盲狙轻点....")
			end
			if level>22 then
				table.insert(intention,"<#20#>")
				table.insert(intention,"主公你这样就不好了")
				table.insert(intention,"你乱来，就不要怪我乱来了")
				to:setMark("revenge"..from:objectName(),level/3)
			end
			to:speak(intention[math.random(1,#intention)])
		end
	else
		if sgs.ai_role[to:objectName()]=="neutral" then
			if level>0 and AIChat(to) then
				if sgs.ai_role[to:objectName()]==sgs.ai_role[from:objectName()] then
					if math.random()<0.3 then
						local intention = {
							"嗯？",
							"。。。。",
							"<#"..math.random(1,56).."#>",
							"<#"..math.random(1,56).."#>",
							"<#12#>",
							"我记下了"
						}
						if from:getPhase()<=sgs.Player_Play then
							table.insert(intention,"警告你")
							table.insert(intention,"喂喂喂！")
							table.insert(intention,"我特么....")
						end
						if level>22 then
							table.insert(intention,"你等着！")
							table.insert(intention,"乱来是吧")
							table.insert(intention,"找茬是吧")
							table.insert(intention,"<#19#>")
							to:setMark("revenge"..from:objectName(),level/2)
						end
						to:speak(intention[math.random(1,#intention)])
					end
				elseif math.random()<0.2 then
					local intention = {
						"看好身份",
						"<#"..math.random(1,56).."#>",
						"<#"..math.random(1,56).."#>",
						"<#6#>",
						"<#44#>",
						"。。。。"
					}
					if from:getPhase()<=sgs.Player_Play then
						table.insert(intention,"点读机是吧")
						table.insert(intention,"我招你惹你了....")
						table.insert(intention,"我特么....")
					end
					if level>22 then
						table.insert(intention,"你等着！")
						table.insert(intention,"你歌姬吧")
						table.insert(intention,"*****")
						table.insert(intention,"<#7#>")
						table.insert(intention,"<#15#>")
						to:setMark("revenge"..from:objectName(),level/2)
					end
					to:speak(intention[math.random(1,#intention)])
				end
			end
		elseif sgs.ai_role[to:objectName()]=="rebel" then
			sgs.roleValue[from:objectName()].loyalist = sgs.roleValue[from:objectName()].loyalist+level
			if sgs.playerRoles.renegade>0 and (sgs.ai_role[from:objectName()]~="loyalist" and level>0 or sgs.ai_role[from:objectName()]~="rebel" and level<0)
			then sgs.roleValue[from:objectName()].renegade = sgs.roleValue[from:objectName()].renegade+math.abs(level) end
		else
			if to:getRole()~="lord" and (sgs.UnknownRebel or sgs.roleValue[from:objectName()].renegade>0 and not sgs.explicit_renegade) then
			elseif sgs.playerRoles.rebel+sgs.playerRoles.renegade>0 then sgs.roleValue[from:objectName()].loyalist = sgs.roleValue[from:objectName()].loyalist-level end
			if sgs.playerRoles.renegade>0 then
				if sgs.UnknownRebel and level>0 and sgs.playerRoles.loyalist>0 then --反装忠
				elseif to:getRole()~="lord" and not sgs.explicit_renegade
				and sgs.playerRoles.rebel<1 and sgs.playerRoles.loyalist>0
				then--进入主忠内,但此时没人跳过内，则忠臣之间相互攻击，不更新内奸值
				elseif sgs.ai_role[from:objectName()]~="rebel" and level>0 or sgs.ai_role[from:objectName()]~="loyalist" and level<0 then
					sgs.roleValue[from:objectName()].renegade = sgs.roleValue[from:objectName()].renegade+math.abs(level)
				end
			end
		end
		for i,p in sgs.qlist(global_room:getAlivePlayers())do
			sgs.ais[p:objectName()]:updatePlayers(i<1)
		end
		outputRoleValues(from,level)
	end
end

function sgs.updateIntentions(from,tos,intention)
	for _,to in sgs.list(tos)do
		sgs.updateIntention(from,to,intention)
	end
end

function sgs.isLordHealthy()
	for _,p in sgs.qlist(global_room:getAlivePlayers())do
		if p:getRole()=="lord" then
			local lord_hp = p:getHp()
			if lord_hp>4 and p:hasSkill("benghuai") then lord_hp = 4 end
			return lord_hp>3 or lord_hp>2 and sgs.getDefense(p)>3
		end
	end
	return true
end

function sgs.isLordInDanger()
	for _,p in sgs.qlist(global_room:getAlivePlayers())do
		if p:getRole()=="lord" then
			local lord_hp = p:getHp()
			if lord_hp>4 and p:hasSkill("benghuai")
			then lord_hp = 4 end
			return lord_hp<3
		end
	end
	return false
end

function sgs.gameProcess(arg,update)
	if not update then
		if arg then
			if sgs.ai_gameProcess_arg then return sgs.ai_gameProcess_arg end
		elseif sgs.ai_gameProcess then return sgs.ai_gameProcess end
	end
	if sgs.playerRoles.rebel<1 and sgs.playerRoles.loyalist>0 then
		if arg then sgs.ai_gameProcess_arg = 99 return 99
		else sgs.ai_gameProcess = "loyalist" return "loyalist" end
	elseif sgs.playerRoles.loyalist<1 and sgs.playerRoles.rebel>1 then
		if arg then sgs.ai_gameProcess_arg = -99 return -99
		else sgs.ai_gameProcess = "rebel" return "rebel" end
	end
	local diff = (sgs.playerRoles.loyalist+1-sgs.playerRoles.rebel)*3
	for _,ap in sgs.qlist(global_room:getAlivePlayers())do
		local role = sgs.ai_role[ap:objectName()]--ap:getRole()
		local hp = ap:getHp()
		if hp>4 and ap:hasSkill("benghuai") then hp = 4 end
		if role=="rebel" then
			local lord = global_room:getLord()
			diff = diff-hp+math.max(sgs.getDefense(ap)-hp*2,0)*0.5
			if lord and ap:inMyAttackRange(lord) then diff = diff-0.4 end
		elseif role=="loyalist" or role=="lord" then
			diff = diff+hp+math.max(sgs.getDefense(ap)-hp*2,0)*0.5
		end
	end
	sgs.ai_gameProcess_arg = diff
	local process = "neutral"
	if diff>=4 then
		if sgs.isLordHealthy() then process = "loyalist"
		else process = "dilemma" end
	elseif diff>=2 then
		if sgs.isLordHealthy() then process = "loyalish"
		elseif sgs.isLordInDanger() then process = "dilemma"
		else process = "rebelish" end
	elseif diff<=-4 then process = "rebel"
	elseif diff<=-2 then
		if sgs.isLordHealthy() then process = "rebelish"
		else process = "rebel" end
	elseif not sgs.isLordHealthy() then process = "rebelish" end
	sgs.ai_gameProcess = process
	return arg and diff or process
end

function SmartAI:objectiveLevel(to)
	--if type(to)~="userdata" then return 0 end
	if to:objectName()==self.player:objectName() then return -3 end
	local players = self.room:getAlivePlayers()
	players:removeOne(self.player)
	players = sgs.QList2Table(players)
	if #players<2 then
		if self.role~="renegade" and sgs.ai_role[to:objectName()]==sgs.ai_role[self.player:objectName()]
		then return -1 else return 5 end
	elseif self.player:getMark("revenge"..to:objectName())>0 then--报复仇恨
		self.player:removeMark("revenge"..to:objectName())
		return 3
	elseif isRolePredictable(true) then--明身份
		to = BeMan(self.room,to)
		if self.lua_ai:isFriend(to) then return -2
		elseif self.lua_ai:isEnemy(to) then return 5
		elseif self.lua_ai:relationTo(to)==sgs.AI_Neutrality
		and self.lua_ai:getEnemies():isEmpty() then return 4 end
	elseif self.player:getMark("roleRobot")>0 then--添加点随机仇恨
		self.player:removeMark("roleRobot")
		if sgs.ai_role[to:objectName()]==sgs.ai_role[self.player:objectName()]
		then return 4-to:getHp() end
	end
	local process = sgs.gameProcess(nil,true)
	if self.role=="renegade" then
		if to:getRole()=="lord" and sgs.getMode~="couple" and to:hasFlag("Global_Dying")
		and not sgs.GetConfig("EnableHegemony",false) then return -2 end
		if sgs.playerRoles.loyalist<1 or sgs.playerRoles.rebel<1 then
			if sgs.playerRoles.rebel>0 then
				if sgs.playerRoles.rebel>1 then
					if to:getRole()=="lord" then return -2
					elseif sgs.ai_role[to:objectName()]=="rebel" then return 5 end
					return 0
				elseif sgs.playerRoles.renegade>1 then
					if to:getRole()=="lord" then return 0
					elseif sgs.ai_role[to:objectName()]=="renegade" then return 3 end
				else
					if process=="loyalist" then
						if to:getRole()=="lord" then
							return sgs.isLordHealthy() and 1 or -1
						elseif sgs.ai_role[to:objectName()]=="rebel" then return 0 end
					elseif process:contains("rebel") then
						if sgs.ai_role[to:objectName()]~="rebel" then return -1 end
					elseif to:getRole()=="lord" then return 0 end
				end
			elseif sgs.playerRoles.loyalist>0 then
				if sgs.explicit_renegade and sgs.playerRoles.renegade>1
				and sgs.ai_role[self.player:objectName()]~="renegade"
				then return sgs.ai_role[to:objectName()]~="loyalist" and 5 or -1 end
				if to:getRole()=="lord" then
					if not sgs.explicit_renegade and sgs.roleValue[self.player:objectName()].renegade<30
					or not sgs.isLordHealthy() then return 0 else return 1 end
				elseif sgs.ai_role[to:objectName()]=="renegade" and sgs.playerRoles.renegade>1
				then return 3 end
			else
				if to:getRole()=="lord" then
					if sgs.isLordInDanger() then return 0
					elseif not sgs.isLordHealthy() then return 3 end
				elseif sgs.isLordHealthy() then return 3 end
				process = sgs.getDefense(to)
				return sgs.getDefense(self.player)<process and process or 0
			end
			return 5
		end
		if process=="neutral"
		or sgs.turncount<2 and sgs.isLordHealthy() then
			if process~="neutral" then
				if sgs.playerRoles.renegade>1 or self:getOverflow()<0 then return 0 end
				if sgs.ai_role[to:objectName()]=="loyalist" and to:getRole()~="lord" and sgs.playerRoles.loyalist+1>=sgs.playerRoles.rebel then return 3.5
				elseif sgs.ai_role[to:objectName()]=="rebel" and sgs.playerRoles.loyalist+1<sgs.playerRoles.rebel then return 3.5 end
			end
			if to:getRole()=="lord" then return -1 end
			for _,p in ipairs(players)do
				if p:getRole()~="lord"
				and p:hasSkills("buqu|nosbuqu|"..sgs.priority_skill.."|"..sgs.save_skill.."|"..sgs.recover_skill.."|"..sgs.drawpeach_skill)
				then return 5 end
			end
			return self:getOverflow()>0 and 3 or 0
		elseif process:contains("rebel") then
			return sgs.ai_role[to:objectName()]=="rebel" and 5
			or sgs.ai_role[to:objectName()]=="neutral" and 0 or -1
		elseif process=="dilemma" then
			if to:getRole()=="lord" then return -2
			elseif sgs.ai_role[to:objectName()]=="neutral"
			or sgs.ai_role[to:objectName()]=="rebel" then return 5 end
		elseif process=="loyalish" then
			if sgs.ai_role[to:objectName()]~="neutral" and sgs.playerRoles.loyalist+1>=sgs.playerRoles.rebel and to:getRole()~="lord"
			then return 3.5 end
		else
			if to:getRole()=="lord" or sgs.ai_role[to:objectName()]=="renegade" then return 0 end
			return sgs.ai_role[to:objectName()]=="rebel" and -2 or 5
		end
	elseif self.player:getRole()=="lord" or self.role=="loyalist" then
		if to:getRole()=="lord" then return -2 end
		if self.role=="loyalist" and sgs.playerRoles.loyalist<2 and (sgs.playerRoles.renegade<1 or sgs.playerRoles.rebel<1)
		or sgs.playerRoles.loyalist+sgs.playerRoles.renegade<1 then return 5 end
		if sgs.ai_role[to:objectName()]=="neutral" then
			if sgs.playerRoles.rebel>0 then
				local fn,en,rn = 1,0,0
				for _,ap in sgs.qlist(self.player:getAliveSiblings())do
					if sgs.ai_role[ap:objectName()]=="loyalist" then fn = fn+1
					elseif sgs.ai_role[ap:objectName()]=="renegade" then rn = rn+1
					elseif sgs.ai_role[ap:objectName()]=="rebel" then en = en+1 end
				end
				local rebelish = process:contains("rebel")
				local cr = sgs.getMode=="05p" or sgs.getMode=="07p" or sgs.getMode=="09p"
				if fn+((cr or rebelish) and rn or 0)>=sgs.playerRoles.loyalist+((rebelish or cr) and sgs.playerRoles.renegade or 0)+1
				then return self:getOverflow()>-1 and 5 or 3
				elseif en+(cr and rn or rebelish and 0 or rn)>=sgs.playerRoles.rebel+(cr and sgs.playerRoles.renegade or rebelish and 0 or sgs.playerRoles.renegade)
				then return -1
				elseif en<=1 and en/sgs.playerRoles.rebel<0.35
				and fn+((cr or rebelish) and rn or 0)+1==sgs.playerRoles.loyalist+((rebelish or cr) and sgs.playerRoles.renegade or 0)+1
				and self:getOverflow()>-1
				then return 1 end
			elseif sgs.explicit_renegade
			and sgs.playerRoles.renegade==1
			then return -1 end
		end
		self:sort(players,sgs.turncount<1 and "hp" or "chaofeng")
		if sgs.playerRoles.rebel<1 then
			if #players==2 and self.role=="loyalist" and to:getRole()~="lord" then return 5 end
			if self.player:getRole()=="lord" and not self.player:hasFlag("stack_overflow_jijiang")
			and players[1]==to then return 0 end
			if sgs.explicit_renegade then
				if self.player:getRole()=="lord" then
					if sgs.ai_role[to:objectName()]=="loyalist" then return -2
					elseif sgs.ai_role[to:objectName()]=="renegade" and sgs.roleValue[to:objectName()].renegade>50
					then return 5 else return to:getHp()>1 and 4 or 0 end
				else
					if self.role=="loyalist"
					and sgs.ai_role[self.player:objectName()]=="renegade" then
						for _,p in ipairs(players)do
							if sgs.roleValue[p:objectName()].renegade>0
							then return to:objectName()==p:objectName() and 5 or -2 end
						end
					else
						if sgs.ai_role[to:objectName()]=="loyalist"
						then return -2 end
					end
					return 4
				end
			else
				local maxhp = players[#players]:getRole()=="lord" and players[#players-1]:getHp() or players[#players]:getHp()
				if maxhp>2 then return to:getHp()>=maxhp and 5 or 0 end
				if maxhp==2 then return self.player:getRole()=="lord" and 0 or (to:getHp()>=maxhp and 5 or 1) end
				return self.player:getRole()=="lord" and 0 or 5
			end
		end
		if sgs.playerRoles.loyalist<1
		and sgs.ai_role[to:objectName()]=="renegade" then
			if sgs.playerRoles.rebel>2 then return -1
			elseif sgs.playerRoles.rebel>1 then return to:getHp()-1 end
			return sgs.isLordInDanger() and -1 or to:getHp()+1
		end
		local rn = 0
		for _,p in ipairs(players)do
			if sgs.ai_role[p:objectName()]=="rebel"
			then rn = rn+1 end
		end
		sgs.UnknownRebel = rn<sgs.playerRoles.rebel
		if sgs.playerRoles.renegade<1 then
			if sgs.ai_role[to:objectName()]=="loyalist" then return -2 end
			if sgs.playerRoles.rebel>0 and sgs.turncount>1 then
				if not sgs.UnknownRebel then
					rn = players[#players]:getRole()=="lord" and players[#players-1]:getHp() or players[#players]:getHp()
					if rn>2 then return to:getHp()>=rn and 5 or 0 end
					if rn==2 then return self.player:getRole()=="lord" and 0 or (to:getHp()>=rn and 5 or 1) end
					return self.player:getRole()=="lord" and 0 or 5
				end
			end
		end
		if sgs.ai_role[to:objectName()]=="rebel" then return 5
		elseif sgs.ai_role[to:objectName()]=="loyalist" then return -2
		elseif sgs.ai_role[to:objectName()]=="renegade" then
			if process:contains("rebel") then return -2 end
			return sgs.isLordInDanger() and 0 or to:getHp()+1
		end
	elseif self.role=="rebel" then
		if sgs.playerRoles.loyalist+sgs.playerRoles.renegade<1
		then return to:getRole()=="lord" and 5 or -2 end
		if sgs.ai_role[to:objectName()]=="neutral" then
			local fn,en,rn = 1,0,0
			for _,ap in sgs.qlist(self.player:getAliveSiblings())do
				if sgs.ai_role[ap:objectName()]=="rebel" then fn = fn+1
				elseif sgs.ai_role[ap:objectName()]=="renegade" then rn = rn+1
				elseif sgs.ai_role[ap:objectName()]=="loyalist" then en = en+1 end
			end
			local loyalish = process:contains("loyal")
			local cr = sgs.getMode=="05p" or sgs.getMode=="07p" or sgs.getMode=="09p"
			if fn+((cr or loyalish) and rn or 0)>=sgs.playerRoles.rebel+((cr or loyalish) and sgs.playerRoles.renegade or 0)
			then return self:getOverflow()>-1 and 5 or 3
			elseif en+(cr and rn or loyalish and 0 or rn)>=sgs.playerRoles.loyalist+(cr and sgs.playerRoles.renegade or loyalish and 0 or sgs.playerRoles.renegade)+1
			then return -1
			elseif sgs.playerRoles.loyalist+sgs.playerRoles.renegade>0
			and fn+((cr or loyalish) and rn or 0)+1==sgs.playerRoles.rebel+((cr or loyalish) and sgs.playerRoles.renegade or 0)
			and en<=1 and en/(sgs.playerRoles.loyalist+sgs.playerRoles.renegade)<0.35
			and self:getOverflow()>=0
			then return 1 end
		end
		if to:getRole()=="lord" then return 5
		elseif sgs.ai_role[to:objectName()]=="loyalist" then return 4 end
		if sgs.ai_role[to:objectName()]=="rebel" then
			return (sgs.playerRoles.rebel>1 or sgs.playerRoles.renegade>0 and process:contains("loyal")) and -2 or 5
		elseif sgs.ai_role[to:objectName()]=="renegade" then
			if sgs.playerRoles.loyalist<1 then return to:getHp() end
			return process:contains("loyal") and -1 or to:getHp()+1
		end
	end
	return 0
end

function SmartAI:isFriend(other,another)
	if another then return self:isFriend(other)==self:isFriend(another) end
	return table.contains(self.friends,other,true)--self:objectiveLevel(other)<0
end

function SmartAI:isEnemy(other,another)
	if another then return self:isFriend(other)==self:isEnemy(another) end
	return table.contains(self.enemies,other,true)--self:objectiveLevel(other)>0
end

function SmartAI:getFriends(player,no_self)
	player = player or self.player
	local friends = {}
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if not(no_self and p~=player) and self:isFriend(p,player)
		then table.insert(friends,p) end
	end
	return friends
end

function SmartAI:getEnemies(player)
	player = player or self.player
	local enemies = {}
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if self:isEnemy(p,player) then table.insert(enemies,p) end
	end
	return enemies
end

function SmartAI:sortEnemies(players)
	local pvl,pds = {},{}
	for _,p in ipairs(players)do
		pvl[p:objectName()] = self:objectiveLevel(p)
		pds[p:objectName()] = self:getDefenseSlash(p)
	end
	local function comp_func(a,b)
		if pvl[a:objectName()]~=pvl[b:objectName()] then
			return pvl[a:objectName()]>pvl[b:objectName()]
		end
		return pds[a:objectName()]<pds[b:objectName()]
	end
	table.sort(players,comp_func)
end

function updateAlivePlayerRoles()
	for _,ap in sgs.qlist(global_room:getPlayers())do
		sgs.playerRoles[ap:getRole()] = 0
	end
	for _,ap in sgs.qlist(global_room:getAlivePlayers())do
		sgs.playerRoles[ap:getRole()] = sgs.playerRoles[ap:getRole()]+1
	end
end

function SmartAI:updatePlayers(update)
	if self.role~=self.player:getRole() then
		if self.player:getRole()~="lord" then
			sgs.roleValue[self.player:objectName()]["loyalist"] = 0
			sgs.roleValue[self.player:objectName()]["renegade"] = 0
			sgs.roleValue[self.player:objectName()]["rebel"] = 0
		end
		self.role = self.player:getRole()
	end
	updateAlivePlayerRoles()
	if update~=false then sgs.gameProcess(true,true) end
	local neutrality = {}
	self.enemies = {}
	self.friends = {}
	self.friends_noself = {}
	if isRolePredictable(true) then
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if self.lua_ai:isFriend(p) then
				table.insert(self.friends,p)
				if p~=self.player then table.insert(self.friends_noself,p) end
			elseif self.lua_ai:isEnemy(p) then table.insert(self.enemies,p)
			elseif self.lua_ai:relationTo(p)==sgs.AI_Neutrality
			then table.insert(neutrality,p) end
		end
		self.harsh_retain = false
		if #self.enemies<#neutrality then
			table.sort(neutrality,sgs.ai_compare_funcs.chaofeng)
			table.insert(self.enemies,neutrality[1])
		end
	else
		if sgs.GetConfig("RolePredictable",false) then
			if sgs.ai_role[self.player:objectName()]~=self.player:getRole()
			and self.player:getRole()~="lord" then self:adjustAIRole() end
		elseif update~=false then evaluateAlivePlayersRole() end
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			local n = self:objectiveLevel(p)
			if n<0 then
				table.insert(self.friends,p)
				if p~=self.player then table.insert(self.friends_noself,p) end
			elseif n>0 then table.insert(self.enemies,p)
			else table.insert(neutrality,p) end
		end
		if #self.enemies<#neutrality
		and #self.toUse<3 and self:getOverflow()>0 then
			table.sort(neutrality,sgs.ai_compare_funcs.chaofeng)
			table.insert(self.enemies,neutrality[#neutrality])
		end
	end
end

function evaluateAlivePlayersRole()
	local function cmp(a,b)
		return sgs.roleValue[a:objectName()].loyalist>sgs.roleValue[b:objectName()].loyalist
	end
	sgs.explicit_renegade = false
	local rebel,loyalist,renegade = 0,0,0
	local aps = sgs.QList2Table(global_room:getAlivePlayers())
	table.sort(aps,cmp)
	for _,p in ipairs(aps)do
		if p:hasShownRole() then
			sgs.ai_role[p:objectName()] = p:getRole()
			if p:getRole()=="lord" then sgs.ai_role[p:objectName()] = "loyalist" end
		elseif sgs.playerRoles.rebel+sgs.playerRoles.loyalist<1 then
			sgs.explicit_renegade = true
			sgs.ai_role[p:objectName()] = "renegade"
		elseif sgs.playerRoles.renegade+sgs.playerRoles.loyalist<1 then
			sgs.ai_role[p:objectName()] = "rebel"
			rebel = rebel+1
		elseif sgs.roleValue[p:objectName()].loyalist>5
		and sgs.playerRoles.loyalist>loyalist then
			sgs.ai_role[p:objectName()] = "loyalist"
			loyalist = loyalist+1
		elseif sgs.playerRoles.renegade>renegade
		and sgs.roleValue[p:objectName()].loyalist>-15
		and sgs.roleValue[p:objectName()].renegade>10 then
			sgs.explicit_renegade = sgs.roleValue[p:objectName()].renegade>(sgs.playerRoles.rebel<1 and 30 or 50)
			sgs.ai_role[p:objectName()] = "renegade"
			renegade = renegade+1
		elseif sgs.roleValue[p:objectName()].loyalist<-10
		and sgs.playerRoles.rebel>rebel then
			sgs.ai_role[p:objectName()] = "rebel"
			rebel = rebel+1
		else
			sgs.ai_role[p:objectName()] = "neutral"
			if sgs.playerRoles.renegade>renegade and sgs.roleValue[p:objectName()].renegade>10 then
				sgs.explicit_renegade = sgs.roleValue[p:objectName()].renegade>40
				sgs.ai_role[p:objectName()] = "renegade"
				renegade = renegade+1
			end
		end
	end
	if rebel<sgs.playerRoles.rebel then
		for _,p in ipairs(sgs.reverse(aps))do
			if sgs.ai_role[p:objectName()]=="rebel"
			or p:hasShownRole() then continue end
			if sgs.roleValue[p:objectName()].loyalist<-5
			and sgs.roleValue[p:objectName()].renegade>5 then
				sgs.roleValue[p:objectName()].loyalist = math.min(-sgs.roleValue[p:objectName()].renegade,sgs.roleValue[p:objectName()].loyalist)
				sgs.roleValue[p:objectName()].renegade = 0
				sgs.ai_role[p:objectName()] = "rebel"
				outputRoleValues(p,0)
				rebel = rebel+1
				global_room:writeToConsole("rebel:"..p:getGeneralName().." Modified Success!")
				if rebel>=sgs.playerRoles.rebel then break end
			end
		end
	end
end

function getTrickIntention(trick_class,target)
	local intention = sgs.ai_card_intention[trick_class]
	if type(intention)=="number" then return intention
	elseif type(intention)=="function" then
		if trick_class=="IronChain"
		then if target:isChained() then return -60 else return 60 end
		elseif trick_class=="Drowning" then
			if target:getArmor() and target:hasSkills("yizhong|bazhen") then return 0 end
			if target:isChained() then return -60 else return 60 end
		end
	end
	if sgs.dynamic_value.damage_card[trick_class] then return 70 end
	if sgs.dynamic_value.benefit[trick_class] then return -40 end
	if ("Snatch|Dismantlement|Zhujinqiyuan"):match(trick_class) then
		if target:getJudgingArea():isEmpty() then
			if not(target:hasArmorEffect("SilverLion")
			and target:isWounded())
			then return 80 end
		end
	end
	return 0
end

sgs.ai_choicemade_filter.Nullification = function(self,player,promptlist)
	if promptlist[2]=="Nullification" then
		sgs.filter_level = sgs.filter_level+1
		if sgs.filter_level%2==0 then sgs.updateIntention(player,sgs.filter_source,sgs.filter_intention)
		else sgs.updateIntention(player,sgs.filter_source,-sgs.filter_intention) end
	else
		sgs.filter_level = 1
		sgs.filter_source = BeMan(self.room,promptlist[3])
		sgs.filter_intention = getTrickIntention(promptlist[2],sgs.filter_source)
		sgs.updateIntention(player,sgs.filter_source,-sgs.filter_intention)
	end
end

sgs.ai_choicemade_filter.playerChosen = function(self,from,promptlist)
	if from:objectName()==promptlist[3] then return end
	local reason = string.gsub(promptlist[2],"%-","_")
	local to = BeMan(self.room,promptlist[3])
	local callback = sgs.ai_playerchosen_intention[reason]
	if type(callback)=="number" then sgs.updateIntention(from,to,sgs.ai_playerchosen_intention[reason])
	elseif type(callback)=="function" then callback(self,from,to) end
end

sgs.ai_choicemade_filter.viewCards = function(self,from,promptlist)
	local to = BeMan(self.room,promptlist[2])
	if to then
		for _,h in sgs.qlist(to:getHandcards())do
			self.room:setCardFlag(h,"visible_"..from:objectName().."_"..promptlist[2])
		end
	end
end

sgs.ai_choicemade_filter.Yiji = function(self,from,promptlist)
	local to = BeMan(self.room,promptlist[3])
	if to then
		local cards = {}
		for _,id in sgs.list(promptlist[4]:split("+"))do
			local c = sgs.Sanguosha:getCard(id)
			self.room:setCardFlag(c,"visible_"..from:objectName().."_"..promptlist[3])
			table.insert(cards,c)
		end
		local callback = sgs.ai_Yiji_intention[promptlist[2]]
		if type(callback)=="number" and not hasManjuanEffect(to)
		and not(self:needKongcheng(to,true) and #cards==1) then sgs.updateIntention(from,to,callback)
		elseif type(callback)=="function" then callback(self,from,to,cards)
		elseif not(self:needKongcheng(to,true) and #cards==1 or hasManjuanEffect(to))
		then sgs.updateIntention(from,to,-10) end
	end
end

sgs.aiResponse = {Slash = "Jink"}
sgs.cardEffect = nil

function SmartAI:filterEvent(event,player,data)
	for _,callback in pairs(sgs.ai_event_callback[event])do
		--if type(callback)=="function" then callback(self,player,data) end
		callback(self,player,data)
	end
	if sgs.aiHandCardVisible and sgs.turncount>0 then
		local file = io.open("lua/ai/cstringEvent", "w")
		file:write("event-"..event.."|"..player:getLogName().."|"..data:toString())
		file:close()
	end
	if event==sgs.Death then
		local de = data:toDeath()
		sgs.ai_role[de.who:objectName()] = de.who:getRole()
		if de.damage and de.damage.from==player and sgs.turncount>1 then
			local intention = 99
			if de.damage.transfer or de.damage.chain then intention = intention/3 end
			sgs.updateIntention(player,de.who,intention)
		end
	elseif event==sgs.BeforeGameOverJudge or event==sgs.Revived then
		for i,p in sgs.qlist(self.room:getAlivePlayers())do
			sgs.ais[p:objectName()]:updatePlayers(i<1)
		end
	--[[elseif event==sgs.AskForPeaches then
		local dying = data:toDying()
		if sgs.DebugMode_Niepan then endlessNiepan(dying.who) end]]
	elseif event==sgs.TargetSpecified then
		local struct = data:toCardUse()
		if struct.card:isDamageCard() then
			if sgs.ai_role[player:objectName()]=="rebel"
			and not self:isFriend(player:getNextAlive()) then
				for _,p in sgs.qlist(struct.to)do
					if p:getHp()<2 and p:isKongcheng() and sgs.ai_role[p:objectName()]=="rebel"
					and self:isFriend(p) and self:isGoodTarget(p,nil,struct.card) and getCardsNum("Peach,Analeptic",p,player)<1
					and self:getEnemyNumBySeat(player,p)>0 then p:setFlags("AI_doNotSave") end
				end
			end
			if struct.card:isKindOf("AOE") then
				self.aoeTos = struct.to
				local lord = getLord(player)
				if lord and lord:getHp()<2 and struct.to:contains(lord) and self:aoeIsEffective(struct.card,lord,player)
				then sgs[struct.card:getClassName().."HasLord"] = true end
				self.aoeTos = nil
			end
		end
		sgs.ai_collateral = struct.card:objectName()=="collateral"
		if struct.to:length()>0 and sgs.UsedData.card==struct.card and sgs.UsedData.from==player then
			local callback = sgs.ai_card_intention[struct.card:getClassName()]
			if callback then
				if type(callback)=="number" then sgs.updateIntentions(player,struct.to,callback)
				else callback(self,struct.card,player,sgs.QList2Table(struct.to)) end
			end
		end
	elseif event==sgs.ChoiceMade then
		local struct = data:toString()
		if struct=="" then return end
		local list = struct:split(":")
		local call = sgs.ai_choicemade_filter[list[1]]
		if type(call)=="table" then
			local i = 2
			if list[1]=="cardResponded"
			or list[1]=="cardUsed" then i = 3 end
			call = call[list[i]]
		end
		if type(call)=="function" then call(self,player,list) end
		if struct:contains("fenxin:yes") then
			for _,ap in sgs.qlist(self.room:getAlivePlayers())do
				if ap:hasFlag("FenxinTarget") then
					sgs.roleValue[player:objectName()] = sgs.roleValue[ap:objectName()]
					sgs.ai_role[player:objectName()] = sgs.ai_role[ap:objectName()]
					self:updatePlayers(false)
					break
				end
			end
		elseif struct:contains("cardChosen") then
			if list[5]=="visible" then
				call = BeMan(self.room,list[4])
				for _,h in sgs.qlist(call:getHandcards())do
					self.room:setCardFlag(h,"visible_"..player:objectName().."_"..list[4])
				end
			end
		end
	elseif event==sgs.CardEffect then
		local struct = data:toCardEffect()
		if struct.card:isKindOf("AOE") and struct.to:getRole()=="lord"
		then sgs[struct.card:getClassName().."HasLord"] = false end
		if sgs.cardEffect and sgs.cardEffect.card then sgs.cardEffect.card:deleteLater() end
		if struct.card:getTypeId()>0 then
			struct.card = sgs.Sanguosha:cloneCard(struct.card)
			sgs.cardEffect = struct
		end
	elseif event==sgs.DamageInflicted then
		local damage = data:toDamage()
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			sgs.ai_NeedPeach[p:objectName()] = 0
		end
		if damage.nature~=sgs.DamageStruct_Normal
		and not damage.chain and player:isChained() then
			local n = 0
			for _,p in sgs.qlist(player:getAliveSiblings())do
				if p:isChained() then
					sgs.ai_NeedPeach[p:objectName()] = damage.damage-p:getHp()
					n = n+1
				end
			end
			self.room:setTag("is_chained",ToData(n))
		end
		local r = damage.reason
		if damage.card then
			sgs.card_damage_nature[damage.card:getClassName()] = damage.nature
			if sgs.ai_card_intention[damage.card:getClassName()] then return end
			if r=="" then r = damage.card:getSkillName() end
		end
		local intention = damage.damage*40
		local from = self.room:findPlayerBySkillName(r,true) or damage.from
		if r~="" then sgs.damageData[r] = (sgs.damageData[r] or 0)+damage.damage end
		if sgs.ai_quhu_effect or r:contains("quhu") then sgs.ai_quhu_effect = false intention = damage.damage*30
		elseif from and (from:hasFlag("ShenfenUsing") or from:hasFlag("FenchengUsing") or r:contains("zhendu")) then intention = damage.damage*10 end
		if damage.transfer or damage.chain then intention = damage.damage*20 end
		if from then sgs.updateIntention(from,player,intention) end
	elseif event==sgs.PreCardUsed then
		local struct = data:toCardUse()
		local sn = getLord(player)
		if sn and struct.card:isKindOf("Duel") then sn:setFlags("-AIGlobal_NeedToWake") end
		sn = struct.card:getSkillName()
		if sn~="" and struct.card:getTypeId()>0 then sgs.convertData[sn] = (sgs.convertData[sn] or 0)+self:getUseValue(struct.card) end
		if struct.whocard then sgs.aiResponse[struct.whocard:getClassName()] = struct.card:getClassName() end
		if struct.m_reason==sgs.CardUseStruct_CARD_USE_REASON_PLAY then
			if struct.card:isKindOf("Slash") then struct.from:setFlags("hasUsedSlash")
			else struct.from:setFlags("hasUsed"..struct.card:getClassName()) end
		end
		sgs.UsedData = struct
		sn = struct.card:objectName()
		sn = sgs.ai_choicemade_filter[sn=="" and struct.card:getClassName() or sn]
		if type(sn)=="function" then sn(self,player,struct) end
	elseif event==sgs.HpRecover then
		local rec = data:toRecover()
		local aci = rec.card and sgs.ai_card_intention[rec.card:getClassName()]
		if type(aci)=="function" or type(aci)=="number" then return end
		if rec.who then sgs.updateIntention(rec.who,player,-66*rec.recover) end
	elseif event==sgs.ShowCards then
		local struct = data:toString()
		local lists = struct:split(":")
		if #lists>1 then
			for _,id in ipairs(lists[1]:split("+"))do
				if player:handCards():contains(tonumber(id)) then
					self.room:setCardFlag(tonumber(id),"visible_"..lists[2].."_"..player:objectName())
				end
			end
		end
	elseif event==sgs.CardsMoveOneTime then
		local move = data:toMoveOneTime()
		local mf = BeMan(self.room,move.from)
		if move.reason.m_skillName=="fenji" then sgs.ai_fenji_target = mf end
		for i,id in sgs.qlist(move.card_ids)do
			local mp = move.from_places:at(i)
			local mc = sgs.Sanguosha:getCard(id)
			if mp==sgs.Player_PlaceHand and player==self.room:getCurrent() then
				for _,f in ipairs(mc:getFlags())do
					if f:contains("visible_") then
						self.room:setCardFlag(mc,"-"..f)
					end
				end
			end
			if move.to_place==sgs.Player_PlaceHand and player:objectName()==move.to:objectName() then
				if not mc:hasFlag("visible") then
					if mp==sgs.Player_PlaceHand and player:handCards():contains(id) then
						self.room:setCardFlag(mc,"visible_"..mf:objectName().."_"..player:objectName())
					end
				end
			end
			if move.to and move.to:objectName()==player:objectName()
			and (move.reason.m_reason==sgs.CardMoveReason_S_REASON_GIVE or move.to:getTag("PresentCard"):toString()==mc:toString()) then
				local m_player = BeMan(self.room,move.reason.m_playerId)
				if m_player then
					local mpl = -33
					if move.to_place~=sgs.Player_PlaceHand or mc:hasFlag("visible") then
						if #self:poisonCards({mc})>0 then mpl = 33
						else mpl = -self:getUseValue(mc)*2 end
					end
					sgs.updateIntention(m_player,player,mpl)
				end
			end
			if mf==nil then continue end
			if bit32.band(move.reason.m_reason,sgs.CardMoveReason_S_MASK_BASIC_REASON)==sgs.CardMoveReason_S_REASON_DISCARD then
				if move.reason.m_playerId~=player:objectName() then
					if mf==player then
						local m_player = BeMan(self.room,move.reason.m_playerId)
						if m_player then
							if mp==sgs.Player_PlaceEquip and (sgs.ai_poison_card[mc:objectName()] or self:evaluateArmor(mc)<-5)
							or mp==sgs.Player_PlaceDelayedTrick then mc = -55 else mc = 55 end
							sgs.updateIntention(m_player,player,mc)
						end
					end
				elseif move.reason.m_reason==sgs.CardMoveReason_S_REASON_RULEDISCARD
				and mf:getPhase()<=sgs.Player_Discard then
					if sgs.ai_role[mf:objectName()]=="neutral"
					and not mf:isSkipped(sgs.Player_Play) and CanUpdateIntention(mf)
					and not mf:hasSkill("baiyin",true) then
						mp = isCard("DelayedTrick",mc,mf)
						if mp and not mp:targetFixed() then
							local zhanghe = self.room:findPlayerBySkillName("qiaobian")
							for _,p in ipairs(self.enemies)do
								if p==zhanghe or p:containsTrick("YanxiaoCard")
								or zhanghe and self:playerGetRound(zhanghe)<=self:playerGetRound(p) and self:isFriend(zhanghe,p) then continue end
								if mf:canUse(mp,p) then player:addMark("Intention"..mf:objectName(),35) break end
							end
						end
						if mf:hasFlag("JiangchiInvoke")
						or mf:hasFlag("hasUsed"..mc:getClassName())
						or mc:isKindOf("Slash") and mf:hasFlag("hasUsedSlash") then
						elseif mc:isDamageCard() or not mc:targetFixed() then
							for _,p in ipairs(self.enemies)do
								if mf:canUse(mc,p) and self:isGoodTarget(p,nil,mc)
								then player:addMark("Intention"..mf:objectName(),35) break end
							end
							for _,p in ipairs(self.friends)do
								if sgs.ai_role[p:objectName()]~="neutral" and mf:canUse(mc,p) and self:isGoodTarget(p,nil,mc)
								then player:addMark("Intention"..mf:objectName(),-35) break end
							end
						end
					end
				end
			elseif move.reason.m_skillName:contains("qiaobian") and move.to and self.room:getCurrent()==player then
				if #self:poisonCards({mc},mf)>0 then mp = -70 else mp = 70 end
				sgs.updateIntention(player,mf,mp)
				if #self:poisonCards({mc},move.to)>0 then mp = 70 else mp = -70 end
				sgs.updateIntention(player,BeMan(self.room,move.to),mp)
			end
		end
		if move.to_place==sgs.Player_PlaceHand and move.to:objectName()==player:objectName() then
			local cstring = {}
			if sgs.aiHandCardVisible
			and player:getPhase()<=sgs.Player_Play then
				for _,c in sgs.qlist(player:getHandcards())do
					table.insert(cstring,sgs.Sanguosha:translate(c:objectName()).."["..sgs.Sanguosha:translate(c:getSuitString().."_char")..c:getNumberString().."]")
				end
				cstring = player:getLogName()..":HC="..table.concat(cstring,"、")
				--self.room:writeToConsole(cstring)
				local file = io.open("lua/ai/cstring", "r")
				local _file = file:read("*all")
				file:close()
				file = io.open("lua/ai/cstring", "w")
				file:write(_file.."\n"..cstring)
				file:close()
			end
			cstring = move.reason.m_skillName
			if cstring~="" then sgs.drawData[cstring] = (sgs.drawData[cstring] or 0)+move.card_ids:length() end
			self:assignKeep(player:getPhase()<sgs.Player_Play)
		elseif move.to_place==sgs.Player_PlaceEquip and move.to:objectName()==player:objectName() then
			self:assignKeep()
		end
	elseif event==sgs.StartJudge then
		local judge = data:toJudge()
		if judge.reason:contains("beige") then
			local caiwenji = self.room:findPlayerBySkillName(judge.reason)
			sgs.updateIntention(caiwenji,player,-60)
		end
		sgs.ai_judgestring[judge.reason] = {judge.pattern,judge.good}
		sgs.ai_judgeGood[judge.reason] = judge:isGood()
		if judge:isGood() and math.random()<0.4 then
			if self:speak(judge.reason.."IsGood") or judge.pattern=="."
			or math.random()<0.4 then return end
			self:speak("judgeIsGood")
		end
	elseif event==sgs.AskForRetrial then
		local judge = data:toJudge()
		local intention = sgs.ai_retrial_intention[judge.reason]
		if type(intention)=="function" then intention = intention(self,judge,sgs.ai_judgeGood[judge.reason]) end
		if type(intention)~="number" then
			if sgs.ai_judgeGood[judge.reason]
			then if judge:isBad() then intention = 30 end
			elseif judge:isGood() then intention = -30 end
		end
		if type(intention)=="number" then sgs.updateIntention(player,judge.who,intention) end
		sgs.ai_judgeGood[judge.reason] = judge:isGood()
	elseif event==sgs.RoundStart then
		if player==self.room:getCurrent() then
			sgs.turncount = data:toInt()
			if sgs.turncount<=1 then
				local hc = 0
				for _,ap in sgs.qlist(self.room:getPlayers())do
					if ap:getState()~="robot" then hc = hc+1 end
				end
				self.room:setTag("humanCount",ToData(hc))
				if sgs.aiHandCardVisible then
					local file = io.open("lua/ai/cstring", "w")
					file:write("humanCount:"..hc)
					file:close()
				end
			end
			sgs.aiData = GetAiData() or sgs.aiData
			saveItemData("drawData")
			saveItemData("convertData")
			saveItemData("damageData")
			saveItemData("throwData")
			if not sgs.aiData["aiResponse"] then sgs.aiData["aiResponse"] = {} end
			for c,r in pairs(sgs.aiResponse)do
				sgs.aiData["aiResponse"][c] = r
			end
			sgs.aiResponse = sgs.aiData["aiResponse"]
			if not sgs.aiData["card_damage_nature"] then sgs.aiData["card_damage_nature"] = {} end
			for c,n in pairs(sgs.card_damage_nature)do
				sgs.aiData["card_damage_nature"][c] = n
			end
			sgs.card_damage_nature = sgs.aiData["card_damage_nature"]
			SetAiData(sgs.aiData)--[[
			if sgs.aiHandCardVisible then
				local allp = sgs.Sanguosha:getSkillNames()--self.room:getTag("AllGenerals"):toStringList()
				local ai_files = sgs.GetFileNames("audio/skill")
				for _,g in sgs.list(ai_files)do
					local has = false
					for _,tg in sgs.list(allp)do
						if g:startsWith(tg) then
							has = true
							break
						end
					end
					if has then continue end
					self.room:writeToConsole("noAudio-"..g)
				end
			end]]
		elseif sgs.ai_humanized and math.random()<0.2-sgs.turncount*0.01
		then player:addMark("roleRobot",math.random(1,11)) end
	elseif event==sgs.GameReady and player:getRole()=="lord" then
		sgs.debugmode = io.open("lua/ai/debug")
		if sgs.debugmode then
			sgs.debugmode:close()
			logmsg("ai.html","<meta charset='utf-8'/>")
		end
	end
end

function SetAiData(td)
	local file = io.open("lua/ai/data/AiData","w")
	file:write(json.encode(td))
	file:close()
end

function GetAiData()
	local file = io.open("lua/ai/data/AiData","r")
	if file then
		local _file = file:read("*all")
		file:close()
		return json.decode(_file)
	else
		local td = {}
		for _,item in ipairs({"drawData","convertData","damageData","throwData"})do
			td[item] = {}
			local st = io.open("lua/ai/data/"..item,"r")
			if st==nil then continue end
			local _st = st:read("*all"):split("\n")
			st:close()
			for _,tm in ipairs(_st)do
				if tm=="" then continue end
				local t = tm:split(":")
				td[item][t[1]] = {}
				for _,t2 in ipairs(t[2]:split(","))do
					if t2=="" then continue end
					table.insert(td[item][t[1]],t2)
				end
			end
			st = io.open("lua/ai/data/"..item,"w")
			st:write()
			st:close()
		end
		file = io.open("lua/ai/data/AiData","w")
		file:write(json.encode(td))
		file:close()
		return td
	end
end

function saveItemData(dataName)
	for s,n in pairs(sgs[dataName])do
		if not sgs.aiData[dataName][s] then sgs.aiData[dataName][s]={} end
		for _,ap in sgs.qlist(global_room:getAlivePlayers())do
			if ap:hasSkill(s,true) then
				table.insert(sgs.aiData[dataName][s],n)
				if #sgs.aiData[dataName][s]>99 then table.remove(sgs.aiData[dataName][s],1) end
				sgs[dataName][s] = 0
				break
			end
		end
	end
end

function SmartAI:askForSuit(reason)
	local callback = sgs.ai_skill_suit[reason]
	if type(callback)=="function" then callback = callback(self) end
	if type(callback)=="number" then return callback end
	return sgs.ai_skill_suit.nosfanjian(self)
end

function SmartAI:askForSkillInvoke(skill_name,data)
	skill_name = string.gsub(skill_name,"%-","_")
	local invoke = sgs.ai_skill_invoke[skill_name]
	if type(invoke)=="function" then
		invoke = invoke(self,data)
	elseif type(invoke)~="boolean" then
		invoke = sgs.Sanguosha:getSkill(skill_name)
		invoke = invoke and invoke:getFrequency()==sgs.Skill_Frequent
	end
	if sgs.jl_bingfen and math.random()>0.8 then
		if jl_bingfen1 and math.random()>0.6
		then self.player:speak(jl_bingfen1[math.random(1,#jl_bingfen1)]) end
		sgs.JLBFto = self.player
		invoke = not invoke
	end
	return invoke 
end

function SmartAI:askForChoice(skill_name,choices,data)
	local choice = sgs.ai_skill_choice[skill_name]
	if type(choice)=="function" then choice = choice(self,choices,data) end
	if type(choice)=="string" or type(choice)=="number" then return choice end
	choice = choices:split("+")
	table.removeOne(choice,"benghuai")
	return choice[math.random(1,#choice)]
end

function getChoice(choices,choice_name,n)
	if type(choices)~="table" then
		choices = choices:split("+")
	end
	n = n or 1
	for _,choice in sgs.list(choices)do
		local new_choices = choice:split("=")
		if #new_choices<n then continue end
		if new_choices[n]==choice_name
		then return choice end
	end
end

function SmartAI:askForDiscard(reason,max_num,min_num,optional,equiped,pattern)
	local exchange = self.player:hasFlag("Global_AIDiscardExchanging")
	if not(optional or exchange) then sgs.throwData[reason] = (sgs.throwData[reason] or 0)+min_num end
	if type(pattern)=="string" then self:assignKeep() else pattern = "." end
	local callback = sgs.ai_skill_discard[reason]
	if type(callback)=="function" then
		callback = callback(self,max_num,min_num,optional,equiped,pattern)
	end
	local to_discard = {}
	if type(callback)=="number" then 
		if exchange or self.player:canDiscard(self.player,callback)
		then to_discard = {callback} end
	elseif type(callback)=="table" then
		for _,id in ipairs(callback)do
			if type(id)~="number" then id = id:getId() end
			if exchange or self.player:canDiscard(self.player,id)
			then table.insert(to_discard,id) end
		end
	elseif optional and equiped then
		for _,c in ipairs(self:poisonCards("e"))do
			if (exchange or self.player:canDiscard(self.player,c:getId()))
			and sgs.Sanguosha:matchExpPattern(pattern,self.player,c)
			then table.insert(to_discard,c:getId()) end
			if #to_discard>=max_num then break end
		end
	end
	if #to_discard<min_num then
		if optional then return {} end
		callback = self.player:getCards(equiped and "he" or "h")
		if exchange then
			for _,c in ipairs(self:poisonCards(callback))do
				if #to_discard>=min_num or table.contains(to_discard,c:getId()) then continue end
				table.insert(to_discard,c:getId())
			end
		end
		local temp = {}
		for _,c in ipairs(self:sortByKeepValue(callback,nil,not exchange and "j"))do
			if #to_discard>=min_num or table.contains(to_discard,c:getId()) then continue end
			if sgs.Sanguosha:matchExpPattern(pattern,self.player,c) then
				if self.player:hasEquip(c) and self:loseEquipEffect() or self:getUseValue(c)<6
				then table.insert(to_discard,c:getId()) else table.insert(temp,c:getId()) end
			end
		end
		for _,id in ipairs(temp)do
			if #to_discard>=min_num or table.contains(to_discard,id) then continue end
			table.insert(to_discard,id)
		end
	end
	return to_discard
end

sgs.ai_skill_discard.gamerule = function(self,x,n)
	local discard = {}
	for _,c in ipairs(self:sortByKeepValue(self.player:getHandcards()))do
		if self.player:isCardLimited(c,sgs.Card_MethodDiscard,true) then continue end
		table.insert(discard,c:getEffectiveId())
		if #discard>=n then break end
	end
	return discard
end

function aiConnect(owner)
	local cts = {}
	for _,s in ipairs(sgs.getPlayerSkillList(owner))do
		table.insert(cts,s:objectName())
		if s:inherits("ViewAsEquipSkill") then
			local va = sgs.Sanguosha:getViewAsEquipSkill(cts[#cts]):viewAsEquip(owner)
			if va=="" then continue end
			for _,en in ipairs(va:split(","))do
				table.insert(cts,en)
			end
		end
	end
	for _,m in ipairs(owner:getMarkNames())do
		if m:startsWith("&") or m:startsWith("@")
		then table.insert(cts,m:split("+")[1]) end
	end
	for _,pn in ipairs(owner:getPileNames())do
		if table.contains(cts,pn) then continue end
		table.insert(cts,pn)
	end
	return cts
end

function dummy(is_dummy,et,ct)
	return {isDummy = is_dummy~=false, extra_target = et or 0, current_targets = ct or {}, to = sgs.SPlayerList()}
end





--ai：谋攻篇
--更新Lua教程--将旧写法改为新写法，现有的lua技能加入其中，
--剧情模式不更新角色Flags














---询问无懈可击--
function SmartAI:askForNullification(trick,from,to,positive)
	if from and from:isDead() or to:isDead() or to:hasFlag("AIGlobal_NeedToWake")
	or ("snatch|dismantlement|zhujinqiyuan"):match(trick:objectName()) and to:isAllNude() then return end
	self.null_num = self:getCard("Nullification",true)
	if #self.null_num<1 or from and not self:isFriend(from) and from:hasSkill("funan")
	or self.player:getHp()<2 and self:hasSkills("jgjingmiao",self.enemies) then return end
	local null_card = self.null_num[1]
	self.null_num = #self.null_num
	if trick:isDamageCard() and positive then
		if self:needToLoseHp(to,from,trick)
		or self:canDamageHp(from,trick,to) then
			if math.random()>=1/self.null_num
			and from~=self.player and self:isEnemy(to)
			and not self:isWeak(to) then
				self:speak("null_card")
				return null_card
			end
			return nil
		elseif from and self:isFriend(to) then
			local adn = math.abs(self:ajustDamage(from,to,1,trick))
			if adn>1 or adn>=to:getHp() then return null_card end
		end
	end
	self.to = to
	self.from = from
	self.trick = trick
	self.positive = positive
	for _,ac in ipairs(aiConnect(to))do
		local can = sgs.ai_nullification[ac]
		if type(can)=="function" then
			can = can(self,trick,from,to,positive,self.null_num)
			if can then return null_card elseif can~=nil then return end
		end
	end
	local callback = sgs.ai_nullification[trick:getClassName()]
	if type(callback)=="function" then
		callback = callback(self,trick,from,to,positive,self.null_num)
		if callback then return null_card elseif callback~=nil then return end
	end
	if positive then
		if from and trick:isDamageCard() and self:isFriend(from)
		and (self:needDeath(to) or self:cantbeHurt(to,from)) then return null_card
		elseif ("snatch|dismantlement|zhujinqiyuan"):match(trick:objectName())
		and self:isEnemy(from) and sgs.ai_role[from:objectName()]~="neutral" then
			if self:isFriend(to) then--敌方拆友方威胁牌、价值牌、最后一张手牌->命中
				if to==self.player or to:containsTrick("YanxiaoCard")
				or self:getDangerousCard(to) or self:getValuableCard(to) then return null_card end
				if to:getHandcardNum()==1 and not self:needKongcheng(to) then
					if getKnownCard(to,self.player,"TrickCard,EquipCard,Slash")==1
					then return else return null_card end
				end
			elseif self:isEnemy(to) then--敌方顺手牵羊、过河拆桥敌方判定区延时性锦囊->命中
				if to:getJudgingArea():length()>0 then return null_card end
			end
		end
		if trick:isKindOf("AOE") and self:isFriend(to) then--多目标攻击性锦囊
			local lord,current = getLord(self.player),self.room:getCurrent()
			if lord and self:isFriend(lord) and self:isWeak(lord) and self:aoeIsEffective(trick,lord)
			and (lord:getSeat()-current:getSeat())%to:aliveCount()>(to:getSeat()-current:getSeat())%to:aliveCount()
			and not(self.player==to and self.player:getHp()<2 and not self:canAvoidAOE(trick)) then return end--主公
			if self.player==to and not self:canAvoidAOE(trick) then return null_card end--自己
			if self:isWeak(to) and self:aoeIsEffective(trick,to) then--队友
				if self.null_num>1 or self.player:getHp()>1 or isLord(to) and self.role=="loyalist" or self:canAvoidAOE(trick)
				or (to:getSeat()-current:getSeat())%to:aliveCount()>(self.player:getSeat()-current:getSeat())%to:aliveCount()
				then return null_card end
			end
		end
	else
		if from then
			if trick:isDamageCard() and self:isEnemy(from) and (self:needDeath(to) or self:cantbeHurt(to,from))
			or trick:targetFixed() and trick:isKindOf("SingleTargetTrick") and self:isFriend(to) then return null_card end
			if trick:isKindOf("SingleTargetTrick") and self:isFriend(from) and not self:isFriend(to) then
				if ("snatch|dismantlement|zhujinqiyuan"):match(trick:objectName()) and to:isNude()
				then else return null_card end
			end
		elseif self:isEnemy(to)
		and math.random()>1/(self.null_num+1)
		then return null_card end
	end
end

function SmartAI:getCardRandomly(who,flags,no_dis)
	if who==self.player then
		for _,c in sgs.list(self:sortByKeepValue(who:getCards(flags)))do
			local id = c:getEffectiveId()
			if not self.disabled_ids:contains(id)
			and (no_dis~=false or self.player:canDiscard(who,id))
			then return id end
		end
		return -1
	end
	local ids = {}
	for _,c in sgs.qlist(who:getCards(flags))do
		local id = c:getEffectiveId()
		if not self.disabled_ids:contains(id)
		and (no_dis~=false or self.player:canDiscard(who,id))
		then table.insert(ids,id) end
	end
	if #ids<1 then return -1 end
	local mr = math.random(1,#ids)
	local id = ids[mr]
	if sgs.Sanguosha:getCard(id):isKindOf("SilverLion")
	and who:isWounded() and self:isEnemy(who) and who:hasArmorEffect("SilverLion") then
		if mr~=#ids then id = ids[mr+1] elseif mr>1 then id = ids[mr-1] end
	end
	return id
end

function SmartAI:askForCardChosen(who,flags,reason,method)
	self.disabled_ids = self.player:getTag("cardChosenForAI"):toIntList()
	local no_dis = false
	if method~=sgs.Card_MethodDiscard then no_dis = true
	else sgs.throwData[reason] = (sgs.throwData[reason] or 0)+1 end
	for _,s in sgs.list(sgs.getPlayerSkillList(who))do
		local chosen = sgs.ai_skill_cardchosen["#"..s:objectName()]
		if type(chosen)=="function" then
			local cid = chosen(self,who,flags,method)
			if cid then
				if type(cid)~="number" then cid = cid:getEffectiveId() end
				if not self.disabled_ids:contains(cid) then return cid end
				return cid
			end
		end
	end
	local chosen = sgs.ai_skill_cardchosen[string.gsub(reason,"%-","_")]
	if type(chosen)=="function" then
		local cid = chosen(self,who,flags,method)
		if cid then
			if type(cid)~="number" then cid = cid:getEffectiveId() end
			if not self.disabled_ids:contains(cid) then return cid end
		end
	elseif type(chosen)=="number" then
		sgs.ai_skill_cardchosen[string.gsub(reason,"%-","_")] = nil
		if not self.disabled_ids:contains(chosen) then return chosen end
	end
	if reason=="dismantlement" and sgs.getMode=="02_1v1"
	and sgs.GetConfig("1v1/Rule","Classical")=="2013" then
		local cards,jink = who:getHandcards(),nil
		for _,c in sgs.qlist(cards)do
			if self.disabled_ids:contains(c:getId()) then continue end
			if isCard("Peach",c,who) then return c:getId() end
			if isCard("Jink",c,who) then jink = c:getId() end
		end
		if jink then return jink end
		for _,c in sgs.list(self:sortByKeepValue(cards,true))do
			if self.disabled_ids:contains(c:getEffectiveId()) then continue end
			return c:getEffectiveId()
		end
	end
	if self:isFriend(who) then
		if flags:contains("j") and not(who:hasSkill("qiaobian") and who:getHandcardNum()>0) then
			local lightning,indulgence,supply_shortage
			for _,trick in sgs.qlist(who:getJudgingArea())do
				local id = trick:getEffectiveId()
				if self:doDisCard(who,id,no_dis) then
					if trick:isDamageCard() then lightning = id
					elseif trick:isKindOf("Indulgence") then indulgence = id
					elseif not trick:isKindOf("Disaster") then supply_shortage = id end
				end
			end
			if lightning and self:hasWizard(self.enemies) then return lightning
			elseif supply_shortage and who:getHp()>=who:getHandcardNum() then return supply_shortage
			elseif indulgence then return indulgence
			elseif supply_shortage then return supply_shortage end
		end
		if flags:contains("e") then
			local cid = who:getEquips()
			cid = sgs.ais[who:objectName()]:sortByKeepValue(cid)
			for _,e in sgs.list(cid)do
				if self:doDisCard(who,e:getId(),no_dis)
				then return e:getId() end
			end
			if #cid>0 and self:loseEquipEffect(who) and self:isWeak(who)
			and self:doDisCard(who,cid[1]:getId(),no_dis)
			then return cid[1]:getId() end
		end
		if flags:contains("j") then
			for _,id in sgs.qlist(who:getJudgingAreaID())do
				if self:doDisCard(who,id,no_dis)
				then return id end
			end
		end
		if flags:contains("h") and who:getHandcardNum()>0
		then return self:getCardRandomly(who,"h",no_dis) end
	else
		if flags:contains("e") then
			local cid = self:getDangerousCard(who)
			if cid and self:doDisCard(who,cid,no_dis)
			then return cid end
			cid = self:getValuableCard(who)
			if cid and self:doDisCard(who,cid,no_dis)
			then return cid end
			for _,e in sgs.list(sgs.ais[who:objectName()]:sortByKeepValue(who:getEquips(),true))do
				if self:doDisCard(who,e:getId(),no_dis) then return e:getId() end
			end
		end
		if flags:contains("h") and who:getHandcardNum()<=2 and self:doDisCard(who,"h",no_dis) then
			if who:hasSkills("jijiu|qingnang|qiaobian|jieyin|beige|buyi|manjuan")
			then return self:getCardRandomly(who,"h",no_dis) end
			for _,h in sgs.qlist(who:getHandcards())do
				if (h:hasFlag("visible") or h:hasFlag("visible_"..self.player:objectName().."_"..who:objectName()))
				and isCard("Peach,Analeptic",h,who) then return self:getCardRandomly(who,"h",no_dis) end
			end
		end
		if flags:contains("j") then
			local lightning,yanxiao
			for _,trick in sgs.qlist(who:getJudgingArea())do
				local id = trick:getEffectiveId()
				if trick:isDamageCard() and self:doDisCard(who,id,no_dis) then lightning = id
				elseif trick:isKindOf("YanxiaoCard") and self:doDisCard(who,id,no_dis) then yanxiao = id end
			end
			if lightning and self:getFinalRetrial(who)>1 then return lightning
			elseif yanxiao then return yanxiao end
		end
		if flags:contains("h") and self:doDisCard(who,"h",no_dis) then
			if who:getHandcardNum()<=2 or who:hasSkills(sgs.cardneed_skill)
			or (who:getHandcardNum()==1 and who:getHp()<=2 and self:getDefenseSlash(who)<3)
			then return self:getCardRandomly(who,"h",no_dis) end
		end
	end
	return self:getCardRandomly(who,flags,no_dis)
end

function SmartAI:doDisCard(to,fid,obtain,n)
	fid = fid or "hej"
	if not(obtain or self.player:canDiscard(to,fid)) then return end
	if type(fid)=="number" then
		if not obtain and to:getEquips():length()<2 and to:getEquipsId():contains(fid) and to:hasSkill("xixiu")
		or self.disabled_ids:contains(fid) then return end
		if self:isFriend(to) then
			if to:getJudgingAreaID():contains(fid) then
				local c = sgs.Sanguosha:getCard(fid)
				if c:isKindOf("Xumou") or to:containsTrick("YanxiaoCard")
				or c:isDamageCard() and self:getFinalRetrial(to)==1
				then return end
				return true
			end
			return #self:poisonCards({fid},to)>0
		else
			if to:getJudgingAreaID():contains(fid) then
				local c = sgs.Sanguosha:getCard(fid)
				if c:isKindOf("Xumou") then return end
				if c:isDamageCard() and self:getFinalRetrial(to)>1
				or c:isKindOf("YanxiaoCard") then return true end
			else
				if to:getEquipsId():contains(fid) and (self:loseEquipEffect(to) or to:getMark("&dev_die")>0)
				or #self.enemies>1 and not to:hasFlag("CurrentPlayer") and to:hasSkill("zaoxian",true)
				then return end
			end
			return #self:poisonCards({fid},to)<1
		end
	else
		n = n or 1
		local is_friend = self:isFriend(to)
		if not is_friend and #self.enemies>1 and not to:hasFlag("CurrentPlayer")
		and to:hasSkill("zaoxian",true) then return end
		if fid:contains("e") then
			if not obtain and to:getEquips():length()==1 and to:getCards(fid):length()<2
			and to:hasSkill("xixiu") then return end
			if is_friend then
				if #self:poisonCards("e",to)>=n/2
				then return true end
			else
				if fid=="he" then
					if to:hasEquip() then
						if to:getHandcardNum()<=n/2 and (self:loseEquipEffect(to) or to:getMark("&dev_die")>0)
						then return end
					else
						if to:getHandcardNum()<=n and self:needKongcheng(to)
						or self:getLeastHandcardNum(to)>=n then return end
					end
					return to:getCardCount()>n/2
				end
				if not obtain and #self.enemies>1 and to:hasSkill("lirang") then return end
				if #self:poisonCards("e",to)<to:getEquips():length()
				and not(self:loseEquipEffect(to) or to:getMark("&dev_die")>0)
				then return true end
			end
		end
		if fid:contains("h") and to:getHandcardNum()>0 then
			if is_friend then
				if to:getHandcardNum()<=n and self:needKongcheng(to)
				or not obtain and to:hasSkill("lirang") and #self:getFriends(to,true)>0
				or self:getLeastHandcardNum(to)>=n then return true end
			else
				if not self:needKongcheng(to)
				or self:getLeastHandcardNum(to)<n
				then return true end
			end
		end
		if fid:contains("j") then
			if is_friend then
				return #self:poisonCards("j",to)>0
			else
				for _,j in sgs.qlist(to:getJudgingAreaID())do
					if self:doDisCard(to,j,obtain)
					then return true end
				end
			end
		end
	end
end

function sgs.ai_skill_cardask.nullfilter(self,data,pattern,target)
	if self.player:getHp()>2 and self:needBear() then return "." end
	local effect = type(data)=="userdata" and data:toCardEffect()
	if effect and effect.card then
		if effect.card:hasFlag("nosjiefan-slash")
		and self:isFriend(self.room:getTag("NosJiefanTarget"):toPlayer())
		and not self:isEnemy(self.room:findPlayerBySkillName("nosjiefan"))
		then return "." end
	end
	if target then
		if self.player:getRole()=="lord" and target:hasSkill("guagu")
		or self:ajustDamage(target,nil,1,effect and effect.card)==0 or self:needDeath()
		or sgs.ai_role[target:objectName()]=="rebel" and self.role=="rebel" and self.player:hasFlag("AI_doNotSave")
		or self:needToLoseHp(self.player,target,effect and effect.card)
		then return "." end
	end
end

function SmartAI:askForCard(pattern,prompt,data,method)
	local compulsive,parsed = pattern:endsWith("!"),prompt:split(":")
	if compulsive then pattern = string.sub(pattern,1,-2) end
	local callback = type(data)=="userdata" and data:toCardEffect()
	if callback and callback.from and not compulsive and callback.card:isDamageCard()
	and self:canDamageHp(callback.from,callback.card) then return "." end
	if #parsed>=2 then
		for _,p in sgs.qlist(self.room:getPlayers())do
			if p:getGeneralName()==parsed[2] or p:objectName()==parsed[2]
			then self.target = p break end
		end
		if #parsed>=3 then
			for _,p in sgs.qlist(self.room:getPlayers())do
				if p:getGeneralName()==parsed[3] or p:objectName()==parsed[3]
				then self.target2 = p break end
			end
		end
	end
	callback = sgs.ai_skill_cardask[parsed[1]]
	if type(callback)=="function" then
		local call = callback(self,data,pattern,self.target,self.target2,parsed[4],parsed[5])
		if type(call)=="number" or type(call)=="string" then
			if not(compulsive and call==".") then return call end
		else
			if call then compulsive = true
			elseif compulsive==call then return "." end
		end
	end
	if (method==sgs.Card_MethodUse or method==sgs.Card_MethodResponse)
	and sgs.ai_skill_cardask.nullfilter(self,data,pattern,self.target)~="." then
		parsed = {}
		for _,c in ipairs(self:addHandPile("he"))do
			callback = c:isKindOf("Slash") and "Slash" or c:getClassName()
			if table.contains(parsed,callback) then continue end
			if pattern:contains(c:objectName()) or sgs.Sanguosha:matchPattern(pattern,self.player,c)
			then table.insert(parsed,callback) end
		end
		for cn,pn in pairs(patterns(true))do
			if table.contains(parsed,cn) then continue end
			callback = dummyCard(pn)
			if callback then
				if pattern:contains(pn) or sgs.Sanguosha:matchPattern(pattern,self.player,callback)
				then table.insert(parsed,cn) end
			end
		end
		for _,cn in ipairs(parsed)do
			for _,c in ipairs(self:getCard(cn,true))do
				callback = c
				if c:getTypeId()<1 then
					callback = dummyCard(cn)
					if callback then
						callback:setSkillName(c:getSkillName())
						if not c:willThrow() then callback:addSubcards(c:getSubcards()) end
						if self.player:isCardLimited(callback,method) then continue end
					else continue end
				end
				if sgs.Sanguosha:matchPattern(pattern,self.player,callback)
				then return c:toString() end
			end
		end
	end
	if compulsive then
		for _,c in ipairs(self:sortByKeepValue(self:addHandPile("he")))do
			if sgs.Sanguosha:matchPattern(pattern,self.player,c)
			and not self.player:isCardLimited(c,method)
			then return c:toString() end
		end
	end
	return "."
end

for cn,name in pairs(patterns(true))do
	sgs.ai_skill_use[name] = function(self,prompt,method)
		for _,c in ipairs(self:getCard(cn,true))do
			local dc = c
			if c:getTypeId()<1 then
				dc = c:toString():split(":")
				dc = dummyCard(dc[#dc])
				if dc then
					if not c:willThrow() then dc:addSubcards(c:getSubcards()) end
					if self.player:isLocked(dc) then continue end
				end
			end
			if dc then
				local d = self:aiUseCard(dc)
				if d.card then
					if dc:canRecast() and d.to:length()<1
					and method~=sgs.Card_MethodRecast
					then continue end
					dc = {}
					for _,p in sgs.list(d.to)do
						table.insert(dc,p:objectName())
					end
					return c:toString().."->"..table.concat(dc,"+")
				end
			end
		end
	end
	sgs.ai_skill_use[cn] = function(self,prompt,method)
		for _,c in ipairs(self:getCard(cn,true))do
			local dc = c
			if c:getTypeId()<1 then
				dc = c:toString():split(":")
				dc = dummyCard(dc[#dc])
				if dc then
					if not c:willThrow() then dc:addSubcards(c:getSubcards()) end
					if self.player:isLocked(dc) then continue end
				end
			end
			if dc then
				local d = self:aiUseCard(dc)
				if d.card then
					if dc:canRecast() and d.to:length()<1
					and method~=sgs.Card_MethodRecast
					then continue end
					dc = {}
					for _,p in sgs.list(d.to)do
						table.insert(dc,p:objectName())
					end
					return c:toString().."->"..table.concat(dc,"+")
				end
			end
		end
	end
end

sgs.armorName = {}

for id=0,sgs.Sanguosha:getCardCount()-1 do
	local c = sgs.Sanguosha:getEngineCard(id)
	if c:isDamageCard() then sgs.dynamic_value.damage_card[c:getClassName()] = true
	elseif c:targetFixed() then sgs.dynamic_value.benefit[c:getClassName()] = true end
	if c:isKindOf("Weapon") then sgs.weapon_range[c:getClassName()] = c:toWeapon():getRange()
	elseif c:isKindOf("Armor") then sgs.armorName[c:objectName()] = true end
	sgs.ai_skill_use[c:toString()] = function(self,prompt,method,pattern)
		local dc = sgs.Sanguosha:getCard(id)
		if self.player:isLocked(dc) then return "." end
		local d = self:aiUseCard(dc)
		if d.card then
			if dc:canRecast() and d.to:length()<1
			and method~=sgs.Card_MethodRecast
			then return "." end
			local c_tos = {}
			for _,p in sgs.list(d.to)do
				table.insert(c_tos,p:objectName())
			end
			return dc:toString().."->"..table.concat(c_tos,"+")
		end
	end
end

function SmartAI:askForUseCard(pattern,prompt,method)
	local func = sgs.ai_skill_use[pattern]
	local compulsive = pattern:endsWith("!")
	if compulsive then pattern = string.sub(pattern,1,-2) end
	if type(func)=="function" then
		local tofunc = func(self,prompt,method,pattern)
		if type(tofunc)=="string" and not(compulsive and tofunc==".")
		then return tofunc end
	end
	if sgs.cardEffect and not(compulsive or pattern:startsWith("@")) then
		local effect = sgs.cardEffect
		if effect.card and string.find(prompt,effect.card:objectName()) and effect.card:isDamageCard()
		and effect.from and effect.to==self.player and self:canDamageHp(effect.from,effect.card)
		then return "." end
	end
	func = sgs.ai_skill_use[prompt:split(":")[1]]
	if type(func)=="function" then
		local tofunc = func(self,prompt,method,pattern)
		if type(tofunc)=="string" and not(compulsive and tofunc==".")
		then return tofunc end
	end
	if pattern:startsWith("@") or method~=sgs.Card_MethodUse then return "." end
	local cns = {}
	for _,c in ipairs(self:addHandPile("he"))do
		local cn = c:isKindOf("Slash") and "Slash" or c:getClassName()
		if table.contains(cns,cn) then continue end
		if pattern:contains(c:objectName()) or sgs.Sanguosha:matchExpPattern(pattern,self.player,c)
		then table.insert(cns,cn) end
	end
	for cn,pn in pairs(patterns(true))do
		if table.contains(cns,cn) then continue end
		local dc = dummyCard(pn)
		if dc then
			if pattern:contains(pn) or sgs.Sanguosha:matchExpPattern(pattern,self.player,dc)
			then table.insert(cns,cn) end
		end
	end
	for _,pn in ipairs(cns)do
		for _,c in ipairs(self:getCard(pn,true))do
			if c:getTypeId()<1 then
				func = dummyCard(pn)
				if func then
					func:setSkillName(c:getSkillName())
					if not c:willThrow() then func:addSubcards(c:getSubcards()) end
					if self.player:isLocked(func) then continue end
				else continue end
			else func = c end
			if sgs.Sanguosha:matchExpPattern(pattern,self.player,func) then
				local d = self:aiUseCard(func)
				if d.card then
					if func:canRecast() and d.to:length()<1
					and method~=sgs.Card_MethodResponse
					then continue end
					local tos = {}
					for _,p in sgs.qlist(d.to)do
						table.insert(tos,p:objectName())
					end
					return c:toString().."->"..table.concat(tos,"+")
				end
			end
		end
	end
	return "."
end

function SmartAI:askForAG(card_ids,refusable,reason)
	if #card_ids<1 then return -1 end
	local cardchosen = sgs.ai_skill_askforag[string.gsub(reason,"%-","_")]
	if type(cardchosen)=="function" then cardchosen = cardchosen(self,card_ids) end
	if type(cardchosen)=="number" then
		if table.contains(card_ids,cardchosen) then return cardchosen
		elseif refusable then return -1 end
	end
	if refusable and reason=="xinzhan" then
		cardchosen = self.player:getNextAlive()
		if self:isFriend(cardchosen) and cardchosen:containsTrick("indulgence")
		and not cardchosen:containsTrick("YanxiaoCard")
		then if #card_ids==1 then return -1 end end
		for _,id in sgs.list(card_ids)do
			if sgs.Sanguosha:getCard(id):isKindOf("Shit")
			then else return id end
		end
		return -1
	end
	for _,id in sgs.list(card_ids)do
		if isCard("Peach",id,self.player)
		then return id end
	end
	for _,id in sgs.list(card_ids)do
		if isCard("Indulgence",id,self.player)
		and not(self:isWeak() and self:getCardsNum("Jink")<1)
		then return id end
		if isCard("AOE",id,self.player)
		and not(self:isWeak() and self:getCardsNum("Jink")<1)
		then return id end
	end
	return sgs.ai_skill_askforag.amazing_grace(self,card_ids)
end

sgs.ai_skill_askforag.AgCardsToName = function(self,card_ids)
	for _,id in sgs.list(card_ids)do
		if self.ACTN==sgs.Sanguosha:getCard(id):objectName()
		then return id end
	end
end

function SmartAI:askForCardShow(requestor,reason)
	local func = sgs.ai_cardshow[reason]
	if func then
		func = func(self,requestor)
		if func then return func end
	end
	return self.player:getRandomHandCard()
end

function sgs.ai_cardneed.bignumber(to,card,self)
	if not self:willSkipPlayPhase(to) and self:getUseValue(card)<6
	then return card:getNumber()>10 end
end

function sgs.ai_cardneed.equip(to,card,self)
	if not self:willSkipPlayPhase(to) then
		return card:isKindOf("EquipCard")
	end
end

function sgs.ai_cardneed.weapon(to,card,self)
	if not self:willSkipPlayPhase(to) then
		return card:isKindOf("Weapon")
	end
end

function SmartAI:getEnemyNumBySeat(from,to,target,include_neutral)
	local players = self.room:getAlivePlayers()
	local to_seat,enemynum = (to:getSeat()-from:getSeat())%players:length(),0
	target = target or from
	for _,p in sgs.qlist(players)do
		if (p:getSeat()-from:getSeat())%players:length()<to_seat
		and (self:isEnemy(target,p) or include_neutral and not self:isFriend(target,p))
		then enemynum = enemynum+1 end
	end
	return enemynum
end

function SmartAI:getFriendNumBySeat(from,to)
	local players,friendnum = sgs.QList2Table(self.room:getAlivePlayers()),0
	for _,p in sgs.list(players)do
		if self:isFriend(from,p) and (p:getSeat()-from:getSeat())%#players<(to:getSeat()-from:getSeat())%#players
		then friendnum = friendnum+1 end
	end
	return friendnum
end

function SmartAI:needKongcheng(player,keep)
	player = player or self.player
	if keep then
		return player:getHandcardNum()<1
		and (player:hasSkill("kongcheng") or player:hasSkill("zhiji") and player:getMark("zhiji")<1
		or player:hasSkill("mobilezhiji") and player:getMark("mobilezhiji")<1 or player:hasSkill("olzhiji") and player:getMark("olzhiji")<1)
	end
	if not player:hasFlag("stack_overflow_xiangle")
	and player:getHandcardNum()>0 and player:hasSkill("beifa")
	and sgs.ais[player:objectName()]:aiUseCard(dummyCard()).card
	then return true end
	if player:getHandcardNum()>0 and not self:hasLoseHandcardEffective(player) then return true end
	if player:getMark("zhiji")<1 and player:hasSkill("zhiji") then return true end
	if player:getMark("mobilezhiji")<1 and player:hasSkill("mobilezhiji") then return true end
	if player:getMark("olzhiji")<1 and player:hasSkill("olzhiji") then return true end
	if player:getPhase()==sgs.Player_Play and player:hasSkill("shude") then return true end
	return player:hasSkills(sgs.need_kongcheng)
end

function SmartAI:needToThrowLastHandcard(player,handnum)
	handnum = handnum or 1
	player = player or self.player
	if player:getHandcardNum()>handnum then return false end
	return player:hasSkill("kongcheng")
	or player:hasSkill("zhiji") and player:getMark("zhiji")<1
	or player:hasSkill("mobilezhiji") and player:getMark("mobilezhiji")<1
	or player:hasSkill("olzhiji") and player:getMark("olzhiji")<1
end

function SmartAI:getLeastHandcardNum(player)
	local least = 0
	player = player or self.player
	if player:hasSkills("lianying|noslianying|kezhuanmanjuan") then least = 1 end
	if least<1 and self:hasSkills("shoucheng",self:getFriends(player)) then least = 1 end
	if least<math.min(2,player:getLostHp()) and player:hasSkill("shangshi") then least = math.min(2,player:getLostHp()) end
	if least<player:getLostHp() and player:hasSkill("nosshangshi") then least = player:getLostHp() end
	return least
end

function SmartAI:hasLoseHandcardEffective(player,num)
	player = player or self.player
	num = num or player:getHandcardNum()
	return num>self:getLeastHandcardNum(player)
end

function SmartAI:hasCrossbowEffect(player)
	player = player or self.player
	if player:hasSkills("paoxiao|tenyearpaoxiao|olpaoxiao")
	or player:hasWeapon("crossbow") then return true end
	local num,slashs,tps = 0,0,self:getEnemies(player)
	for _,kc in ipairs(getKnownCards(player,self.player,"&he"))do
		local s = isCard("Slash",kc,player)
		if s then
			slashs = slashs+1
			for _,p in ipairs(tps)do
				num = num+sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_Residue,player,s,p)
			end
		end
	end
	return num>slashs/2
end

function SmartAI:getCardNeedPlayer(cards,include_self,tos)
	cards = cards or self.player:getHandcards()
	tos = tos or include_self and self.room:getAlivePlayers() or self.room:getOtherPlayers(self.player)
	tos = sgs.QList2Table(tos)	
	if #tos<1 then return end
	
	local pcs = self:poisonCards(cards)
	
	local friends = {}
	for _,p in sgs.list(tos)do
		local exclude = self:needKongcheng(p) or self:willSkipPlayPhase(p)
		if p:getHp()-p:getHandcardNum()>=3 or p:hasSkills("keji|qiaobian|shensu")
		or p:getRole()=="lord" and self:isWeak(p) and self:getEnemyNumBySeat(self.player,p)>=1
		then exclude = false end
		local pl = self:objectiveLevel(p)
		if pl<=-2 and not hasManjuanEffect(p) and not exclude
		then table.insert(friends,p)
		elseif pl>=0 then
			for _,c in sgs.list(pcs)do
				if c:getTypeId()<3 then return c,p end
			end
		end
	end

	local AssistTarget = self:AssistTarget()
	if AssistTarget and (self:needKongcheng(AssistTarget,true) or self:willSkipPlayPhase(AssistTarget) or AssistTarget:hasSkill("manjuan"))
	then AssistTarget = nil end

	if self.role~="renegade" then
		local R_num = sgs.playerRoles.renegade
		if R_num>0 and #friends>R_num then
			local k,temp_friends,new_friends = 0,{},{}
			for _,p in sgs.list(friends)do
				if k<R_num and sgs.explicit_renegade and sgs.ai_role[p:objectName()]=="renegade" then
					k = k+1
					if AssistTarget and p:objectName()==AssistTarget:objectName() then AssistTarget = nil end
				else table.insert(temp_friends,p) end
			end
			if k==R_num then
				friends = temp_friends
			else
				local cmp = function(a,b)
					local ar_value,br_value = sgs.roleValue[a:objectName()]["renegade"],sgs.roleValue[b:objectName()]["renegade"]
					local al_value,bl_value = sgs.roleValue[a:objectName()]["loyalist"],sgs.roleValue[b:objectName()]["loyalist"]
					return ar_value>br_value or ar_value==br_value and al_value>bl_value
				end
				table.sort(temp_friends,cmp)
				for _,p in sgs.list(temp_friends)do
					if k<R_num and sgs.roleValue[p:objectName()]["renegade"]>0 then
						if AssistTarget and p:objectName()==AssistTarget:objectName()
						then AssistTarget = nil end
						k = k+1
					else table.insert(new_friends,p) end
				end
				friends = new_friends
			end
		end
	end
	local specialnum = 0
	-- special move between liubei and xunyu and huatuo
	for _,p in sgs.list(friends)do
		if p:hasSkill("jieming") or p:hasSkill("jijiu")
		then specialnum = specialnum+1 end
		for _,c in sgs.list(pcs)do
			if c:getTypeId()>2 then return c,p end
		end
	end
	if specialnum>1 and self.player:getPhase()==sgs.Player_Play
	and self.player:hasSkill("nosrende") then
		local xunyu = self.room:findPlayerBySkillName("jieming")
		local keptslash,cardtogivespecial = 0,{}
		for _,c in sgs.list(cards)do
			if isCard("Slash",c,self.player) then
				if xunyu and self.player:canSlash(xunyu)
				and self:slashIsEffective(c,xunyu) then keptslash = keptslash+1 end
				if keptslash>0 then table.insert(cardtogivespecial,c) end
			elseif isCard("Duel",c,self.player)
			then table.insert(cardtogivespecial,c) end
		end
		local redcardnum = 0
		for _,c in sgs.list(cardtogivespecial)do
			if c:isRed() then redcardnum = redcardnum+1 end
		end
		local huatuo = self.room:findPlayerBySkillName("jijiu")
		if redcardnum>0 and xunyu and huatuo
		and self.player:getHandcardNum()>#cardtogivespecial then
			for _,c in sgs.list(cardtogivespecial)do
				if c:isRed() then return c,huatuo end
				return c,xunyu
			end
		end
	end

	self:sort(friends)
	local cardtogive,keptjink = {},0
	for _,acard in sgs.list(cards)do
		if isCard("Jink",acard,self.player)
		and keptjink<1 then keptjink = keptjink+1
		else table.insert(cardtogive,acard) end
		for _,friend in sgs.list(friends)do
			if friend:getHandcardNum()<3 and self:isWeak(friend) then
				if acard:isKindOf("Shit") then
				elseif isCard("Peach,Analeptic",acard,friend)
				or isCard("Jink",acard,friend) and self:getEnemyNumBySeat(self.player,friend)>0
				then return acard,friend end
			end
		end
	end

	if self.player:isWounded() and self.player:getMark("nosrende")<2 and self.player:hasSkill("nosrende") then
		if self.player:getHandcardNum()<2 and self.player:getMark("nosrende")<1 then return end
	end
	if self.player:isWounded() and self.player:getMark("rende")<2
	and self.player:hasSkill("rende") and not self.player:hasUsed("RendeCard") then
		if self.player:getHandcardNum()<2 and self.player:getMark("rende")<1 then return end

		if (self.player:getHandcardNum()==2 and self.player:getMark("rende")==0
		or self.player:getHandcardNum()==1 and self.player:getMark("rende")==1)
		and self:getOverflow()<=0 then
			for _,enemy in sgs.list(self.enemies)do
				if enemy:hasWeapon("GudingDlade")
				and (enemy:canSlash(self.player) or enemy:hasSkill("shensu") or enemy:hasSkills("wushen|olwushen") or enemy:hasSkill("jiangchi")) then return end
				if enemy:canSlash(self.player,nil,true) and enemy:hasSkill("nosqianxi") and enemy:distanceTo(self.player)==1 then return end
			end
		end
	end

	-- Armor,DefensiveHorse
	for _,friend in sgs.list(friends)do
		if friend:getHp()<=2 and friend:faceUp() then
			for _,hcard in sgs.list(cards)do
				if hcard:isKindOf("Armor") and not friend:getArmor() and not friend:hasSkills("yizhong|bazhen")
				or hcard:isKindOf("DefensiveHorse") and not friend:getDefensiveHorse()
				then return hcard,friend end
			end
		end
	end

	-- jijiu,jieyin
	cards = self:sortByUseValue(cards,true)
	for _,friend in sgs.list(friends)do
		if friend:getHandcardNum()<4 and friend:hasSkills("jijiu|jieyin") then
			for _,h in sgs.list(cards)do
				if h:isRed() and friend:hasSkill("jijiu")
				or friend:hasSkill("jieyin")
				then return h,friend end
			end
		end
	end

	--Crossbow
	for _,friend in sgs.list(friends)do
		if friend:getHandcardNum()>=2 and friend:hasSkills("longdan|ollongdan|wusheng|tenyearwusheng|keji|chixin")
		and not self:hasCrossbowEffect(friend) then
			for _,hcard in sgs.list(cards)do
				if hcard:isKindOf("Crossbow")
				then return hcard,friend end
			end
		end
	end

	for _,friend in sgs.list(friends)do
		if getKnownCard(friend,self.player,"Crossbow")>0 then
			for _,p in sgs.list(self.enemies)do
				if self:isGoodTarget(p,self.enemies,dummyCard())
				and friend:distanceTo(p)<=1 then
					for _,hcard in sgs.list(cards)do
						if isCard("Slash",hcard,friend)
						then return hcard,friend end
					end
				end
			end
		end
	end
	local cmpByAction = function(a,b)
		return a:getRoom():getFront(a,b)==a
	end
	table.sort(friends,cmpByAction)
	for _,friend in sgs.list(friends)do
		if friend:faceUp() then
			local can_slash = false
			for _,p in sgs.list(self.enemies)do
				if self:isGoodTarget(p,self.enemies,dummyCard()) and friend:distanceTo(p)<=friend:getAttackRange()
				then can_slash = true break end
			end
			if not can_slash then
				local flag = string.format("weapon_done_%s_%s",self.player:objectName(),friend:objectName())
				for _,p in sgs.list(self.enemies)do
					if self:isGoodTarget(p,self.enemies,dummyCard())
					and friend:distanceTo(p)>friend:getAttackRange() then
						for _,hcard in sgs.list(cardtogive)do
							if hcard:isKindOf("Weapon") and friend:distanceTo(p)<=friend:getAttackRange()+(sgs.weapon_range[hcard:getClassName()] or 0)
							and not friend:getWeapon() and not friend:hasFlag(flag) then
								friend:setFlags(flag)
								return hcard,friend
							end
							if hcard:isKindOf("OffensiveHorse") and friend:distanceTo(p)<=friend:getAttackRange()+1
							and not friend:getOffensiveHorse() and not friend:hasFlag(flag) then
								friend:setFlags(flag)
								return hcard,friend
							end
						end
					end
				end
			end
		end
	end

	local cmpByNumber = function(a,b)
		return a:getNumber()>b:getNumber()
	end
	table.sort(cardtogive,cmpByNumber)

	for _,friend in sgs.list(friends)do
		if friend:faceUp() and not self:needKongcheng(friend,true) then
			for _,askill in sgs.list(friend:getVisibleSkillList(true))do
				local callback = sgs.ai_cardneed[askill:objectName()]
				if type(callback)=="function" then
					for _,hcard in sgs.list(cardtogive)do
						if callback(friend,hcard,self)
						then return hcard,friend end
					end
				end
			end
		end
	end

	-- shit
	for _,shit in sgs.list(cardtogive)do
		if shit:isKindOf("Shit") then
			for _,friend in sgs.list(friends)do
				if self:isWeak(friend) then
				elseif shit:getSuit()==sgs.Card_Spade
				or friend:hasSkill("jueqing") then
					if friend:hasSkill("zhaxiang") then return shit,friend end
				elseif friend:hasSkills("guixin|jieming|yiji|nosyiji|chengxiang|noschengxiang|jianxiong")
				then return shit,friend end
			end
		end
	end

	-- slash
	if self.role=="lord" and self.player:hasLordSkill("jijiang") then
		for _,friend in sgs.list(friends)do
			if friend:getKingdom()=="shu" and friend:getHandcardNum()<3 then
				for _,hcard in sgs.list(cardtogive)do
					if isCard("Slash",hcard,friend)
					then return hcard,friend end
				end
			end
		end
	end

	-- kongcheng
	self:sort(self.enemies)
	if #self.enemies>0 and self.enemies[1]:isKongcheng() and self.enemies[1]:hasSkill("kongcheng")
	and not hasManjuanEffect(self.enemies[1]) then
		for _,acard in sgs.list(cardtogive)do
			if acard:isKindOf("Lightning") or acard:isKindOf("Collateral") or (acard:isKindOf("Slash") and self.player:getPhase()==sgs.Player_Play)
			or acard:isKindOf("OffensiveHorse") or acard:isKindOf("Weapon") or acard:isKindOf("AmazingGrace")
			then return acard,self.enemies[1] end
		end
	end

	if AssistTarget and table.contains(tos,AssistTarget) then
		for _,hcard in sgs.list(cardtogive)do
			return hcard,AssistTarget
		end
	end

	self:sort(friends)
	for _,friend in sgs.list(friends)do
		if #cardtogive<1 then break end
		if not self:needKongcheng(friend,true) and not friend:hasSkill("manjuan")
		and not self:willSkipPlayPhase(friend) and self:hasSkills(sgs.priority_skill,friend)
		and (self:getOverflow()>0 or self.player:getHandcardNum()>3) and friend:getHandcardNum()<=3
		then return cardtogive[1],friend end
	end

	local shoulduse = self.player:isWounded() and (self.player:hasSkill("rende") and not self.player:hasUsed("RendeCard") and self.player:getMark("rende")<2)
	or (self.player:hasSkill("nosrende") and self.player:getMark("nosrende")<2)

	if #cardtogive<1 and shoulduse then cardtogive = cards end

	self:sort(friends,"handcard")
	for _,friend in sgs.list(friends)do
		if #cardtogive<1 then break end
		if not self:needKongcheng(friend,true) and not friend:hasSkill("manjuan") then
			if friend:getHandcardNum()<=3 and (self:getOverflow()>0 or self.player:getHandcardNum()>3 or shoulduse)
			then return hcard,friend end
		end
	end

	for _,friend in sgs.list(friends)do
		if #cardtogive<1 then break end
		if (not self:needKongcheng(friend,true) or #friends==1) and not friend:hasSkill("manjuan") then
			if shoulduse or self.player:getHandcardNum()>3 or self:getOverflow()>0
			then return hcard,friend end
		end
	end

	for _,friend in sgs.list(tos)do
		if #cardtogive<1 then break end
		if sgs.turncount>1 and (not self:needKongcheng(friend,true) or #tos==1)
		and not self:isEnemy(friend) and not hasManjuanEffect(friend) then
			if self:getOverflow()>0 or shoulduse
			then return hcard,friend end
		end
	end

	if #cardtogive>0 then cards = cardtogive end

	if #cards>0 and shoulduse then
		if sgs.playerRoles.rebel<1 and sgs.playerRoles.loyalist>0 and self.player:isWounded()
		or sgs.playerRoles.rebel>0 and sgs.playerRoles.renegade>0 and sgs.playerRoles.loyalist<1 and self:isWeak() then
			self:sort(tos)
			return cards[1],tos[1]
		end
	end
end

function SmartAI:askForYiji(card_ids,reason)
	local targets = sgs.SPlayerList()
	local pns = self.player:getTag("yijiForAI"):toStringList()
	for _,p in sgs.qlist(self.room:getPlayers())do
		if table.contains(pns,p:objectName())
		then targets:append(p) end
	end
	local callback = sgs.ai_skill_askforyiji[string.gsub(reason,"%-","_")]
	if type(callback)=="function" then
		local p,cid = callback(self,card_ids,targets)
		if p and table.contains(card_ids,cid)
		then return p,cid end
	end
	pns = {}
	for _,id in sgs.list(card_ids)do
		table.insert(pns,sgs.Sanguosha:getCard(id))
	end
	local c,p = self:getCardNeedPlayer(pns,true,targets)
	if c and p then return p,c:getEffectiveId() end
	return nil,-1
end

function SmartAI:askForPindian(requestor,reason)
	local passive = "mizhao|lieren"
	if self.player==requestor
	and not passive:match(reason) then
		passive = self[reason.."_card"]
		if passive then
			if type(passive)=="number" then passive = sgs.Sanguosha:getCard(passive) end
			return passive
		end
		self.room:writeToConsole("Pindian card for "..reason.." not found!!")
		return self:getMaxCard()
	end
	local cards = self.player:getHandcards()
	cards = self:sortByUseValue(cards)
	local maxcard,mincard = cards[1],cards[1]
	function compare_func(a,b)
		return a:getNumber()<b:getNumber()
	end
	table.sort(cards,compare_func)
	for _,c in sgs.list(cards)do
		if self:getUseValue(c)<6
		then mincard = c break end
	end
	for _,c in sgs.list(sgs.reverse(cards))do
		if self:getUseValue(c)<6 then maxcard = c break end
	end
	self:sortByUseValue(cards)
	passive = sgs.ai_skill_pindian[reason]
	if type(passive)=="function" then
		passive = passive(cards[1],self,requestor,maxcard,mincard)
		if passive then return passive end
	end
	passive = cards[1]
	local sameclass = true
	for _,c in sgs.list(cards)do
		if passive:getClassName()~=c:getClassName()
		then sameclass = false break end
	end
	if sameclass then
		if self:isFriend(requestor) then return self:getMinCard()
		else return self:getMaxCard() end
	end
	if self:isFriend(requestor) then return mincard
	else return maxcard end
end

sgs.ai_skill_playerchosen.damage = function(self,targets)
	local targetlist = self:sort(targets,"hp")
	for _,p in sgs.list(targetlist)do
		if self:isEnemy(p) and self:damageIsEffective(p,"N",self.player)
		then return p end
	end
	for _,p in sgs.list(targetlist)do
		if not self:isFriend(p) and self:damageIsEffective(p,"N",self.player)
		then return p end
	end
	return targetlist[#targetlist]
end

function SmartAI:askForPlayerChosen(targets,reason)
	local chosen = sgs.ai_skill_playerchosen[string.gsub(reason,"%-","_")]
	if sgs.jl_bingfen and math.random()>0.67 then chosen = nil end
	if type(chosen)=="function" then
		chosen = chosen(self,targets)
	end
	if type(chosen)=="userdata" and targets:contains(chosen)
	then return chosen end
end

function SmartAI:askForPlayersChosen(targets,reason,max_num,min_num)
	local chosen = sgs.ai_skill_playerschosen[reason]
	local tos = {}
	if type(chosen)=="function" then
		chosen = chosen(self,targets,max_num,min_num)
		if type(chosen)==type(targets)
		or type(chosen)=="table" then
			for _,p in sgs.list(chosen)do
				if targets:contains(p)
				then table.insert(tos,p) end
			end
			if sgs.jl_bingfen and math.random()>0.67 then tos = {} end
			if #tos>=min_num then return tos end
		end
	end
	for _,p in sgs.list(RandomList(targets))do
		if #tos>=min_num or table.contains(tos,p)
		then continue end
		table.insert(tos,p)
	end
	return tos
end

function SmartAI:ableToSave(saver,dying)
	local current = self.room:getCurrent()
	if current and current~=saver and current~=dying and current:hasSkill("wansha")
	and not saver:hasSkills("jiuzhu|chunlao|tenyearchunlao|secondtenyearchunlao|nosjiefan|renxin")
	then return false end
	if not saver:hasSkills("jiuzhu|chunlao|tenyearchunlao|secondtenyearchunlao|nosjiefan|renxin")
	and saver:isCardLimited(dummyCard("peach"),sgs.Card_MethodUse)
	then return false end
	return true
end

function SmartAI:askForSinglePeach(dying)
	if self:needDeath(dying) then return "." end
	local peach_str
	function usePeachTo(str)
		if peach_str then return peach_str end
		for _,c in ipairs(self:getCard(str or "Peach",true))do
			if self.player:isProhibited(dying,c) then continue end
			return c:toString()
		end
	end
	if self.player==dying then
		peach_str = usePeachTo("Peach,Analeptic")
		if peach_str and dying:getState()=="robot" and sgs.ai_humanized and math.random()<sgs.turncount*0.1
		and #self.friends<dying:aliveCount()/2 and #self:getCard("Analeptic,Peach",true)<2 then
			self.room:getThread():delay(global_delay*2)
			self:speak("no_peach",dying:isFemale())
			peach_str = "."
		end
		return peach_str or "."
	elseif sgs.ai_humanized then
		self.room:getThread():delay(global_delay*math.random(1.5,2.5))
	end
	local lord = getLord(self.player)
	local process = sgs.gameProcess()
	local cp = self.room:getCurrent()
	if lord==self.player and self:isWeak() and self:getEnemyNumBySeat(cp,self.player,self.player)>0
	or self.role=="renegade" and dying~=lord and (cp==self.player or process=="neutral" or sgs.playerRoles.loyalist>=sgs.playerRoles.rebel)
	or hasBuquEffect(dying) then return "." end
	if sgs.ai_role[dying:objectName()]=="renegade" then
		if self.role=="loyalist" or self.role=="renegade" or self.role=="lord" then
			if sgs.playerRoles.loyalist+sgs.playerRoles.renegade>=sgs.playerRoles.rebel
			or process=="loyalist" or process=="loyalish" or process=="dilemma" then return "." end
		end
		if self.role=="rebel" or self.role=="renegade" then
			if sgs.playerRoles.rebel+sgs.playerRoles.renegade-1>=sgs.playerRoles.loyalist+1
			or process=="rebelish" or process=="dilemma" or process=="rebel" then return "." end
		end
	end
	if self:isFriend(dying) then
		if dying==lord then peach_str = usePeachTo()
		elseif lord then
			local pn = self:getCardsNum("Peach")
			if (self.role=="loyalist" or self.role=="renegade" and dying:aliveCount()>2)
			and pn<=sgs.ai_NeedPeach[lord:objectName()]
			or (pn<2 and (sgs.SavageAssaultHasLord and getCardsNum("Slash",lord,self.player)<1
			or sgs.ArcheryAttackHasLord and getCardsNum("Jink",lord,self.player)<1))
			then return "." end
			for _,friend in sgs.list(self.friends_noself)do
				if self:playerGetRound(friend)<self:playerGetRound(self.player)
				or not self:ableToSave(friend,dying) then continue end
				pn = pn+getCardsNum("Peach",friend,self.player)
			end
			if pn+dying:getHp()<1 then return "." end
			if dying~=lord and lord:getHp()<2 and self:isFriend(lord)
			and self:isEnemy(cp) and cp:canSlash(lord)
			and getCardsNum("Peach,Analeptic",lord,self.player)<1
			and #self.friends_noself<=2 and self:slashIsAvailable(cp)
			and pn<=self:getEnemyNumBySeat(cp,lord,self.player)+1
			and self:damageIsEffective(cp,nil,lord)
			then return "." end
			if lord:getHp()<2 and not hasBuquEffect(lord)
			and (self:isFriend(lord) or self.role=="renegade")
			or self:getAllPeachNum()+dying:getHp()>0
			then peach_str = usePeachTo() end
		elseif dying:hasFlag("Kurou_toDie") and getCardsNum("Crossbow",dying,self.player)<1
		or dying:faceUp() and dying:getHp()>=0 and dying:hasSkill("jiushi")
		or self:doNotSave(dying)
		then return "." end
	else --救对方的情形
		if dying:hasSkill("wuhun") --濒死者有技能“武魂”
		and self.role~=sgs.ai_role[dying:objectName()] then --可能有救的必要
			if lord and self.player:aliveCount()>2
			or sgs.playerRoles.rebel+sgs.playerRoles.renegade>1 then
				for _,target in ipairs(self:getWuhunRevengeTargets(dying))do --武魂复仇目标
					if target:getRole()=="lord" then--主公会被武魂带走，真的有必要……
						local finalRetrial,wizard = self:getFinalRetrial(nil,"wuhun")
						if finalRetrial==0 --没有判官，需要考虑观星、心战、攻心的结果（已忽略）
						or finalRetrial==2 --对方后判，这个一定要救了……
						then peach_str = usePeachTo()
						elseif finalRetrial==1 then --己方后判，需要考虑最后的判官是否有桃或桃园结义改判
							local flag = wizard:hasSkill("huanshi") and "he" or "h"
							if getKnownCard(wizard,self.player,"Peach,GodSalvation",false,flag)>0
							then return "." else peach_str = usePeachTo() end
						end
					end
				end
			end
		elseif sgs.ai_humanized and lord
		and not dying:hasSkills(sgs.masochism_skill)
		and not canNiepan(dying) then -- 鞭尸...
			local mode = string.lower(sgs.getMode)
			if mode=="06_3v3" and #self.enemies<2 and dying:isNude()
			and #self.friends>2 and not self:isWeak(self.friends) then
				peach_str = usePeachTo()
				if peach_str then
					self:speak("bianshi",dying:isFemale())
					sgs.ai_doNotUpdateIntenion = true
				end
			elseif string.find(mode,"p") and sgs.playerRoles.renegade<1
			and mode>="03p" and dying:isNude() and #self.friends>2
			and #self.enemies<2 and cp:getNextAlive()~=dying then
				if (self.role=="lord" or self.role=="loyalist")
				and sgs.playerRoles.rebel==1 then
					peach_str = usePeachTo()
					if peach_str then
						self:speak("bianshi",dying:isFemale())
						sgs.ai_doNotUpdateIntenion = true
					end
				elseif self.role=="rebel"
				and sgs.playerRoles.loyalist<1
				and not self:isWeak(self.friends) then
					peach_str = usePeachTo()
					if peach_str then
						self:speak("bianshi",dying:isFemale())
						sgs.ai_doNotUpdateIntenion = true
					end
				end
			end
			if sgs.jl_bingfen
			and math.random()<0.30 then
				peach_str = usePeachTo()
				if peach_str and jl_bingfen3 then
					sgs.JLBFto = self.player
					self.room:getThread():delay(global_delay*2)
					self.player:speak(jl_bingfen3[math.random(1,#jl_bingfen3)])
				end
			end
		end
	end
	return peach_str or "."
end

function SmartAI:addHandPile(cards,player)
	player = player or self.player
	cards = sgs.QList2Table(type(cards)=="string" and player:getCards(cards) or cards or player:getHandcards())
	for _,id in sgs.qlist(player:getHandPile())do
		table.insert(cards,sgs.Sanguosha:getCard(id))
	end
	return cards
end

function SmartAI:getTurnUse()
	local turnUse = {}
	function canMethodUse(c)
		if c:getTypeId()~=1 or c:hasFlag("AIGlobal_KillOff") then return c:isAvailable(self.player) end
		local cn = c:isKindOf("Slash") and "Slash" or c:getClassName()
		local n = 0
		for _,tc in ipairs(turnUse)do
			if tc:isKindOf(cn) then n = n+1 end
		end
		self.player:addHistory(cn,n)
		local canA = c:isAvailable(self.player)
		self.player:addHistory(cn,-n)
		return canA
	end
	self.use_to = {}
	for _,c in ipairs(self:sortByDynamicUsePriority(self:fillSkillCards(self:addHandPile())))do
		if canMethodUse(c) then
			if sgs.aiHandCardVisible then
				local file = io.open("lua/ai/cstring", "r")
				local _file = file:read("*all")
				file:close()
				file = io.open("lua/ai/cstring", "w")
				if c:isVirtualCard() then file:write(_file.."\n"..c:toString())
				else file:write(_file.."\n"..c:getFullName(true)) end
				file:close()
				--self.room:writeToConsole(c:getFullName(true))
			end
			local d = self:aiUseCard(c,dummy(false))
			if d.card then
				if d.card~=c and self.player:isCardLimited(d.card,d.card:getHandlingMethod()) then continue end
				self.use_to[d.card:toString()] = d.to
				table.insert(turnUse,d.card)
			elseif c:canRecast() and c:getTypeId()>0 then
				if self.player:getHandPile():contains(c:getEffectiveId())
				or self.player:isCardLimited(c,sgs.Card_MethodRecast) then continue end
				self.use_to[c:toString()] = sgs.SPlayerList()
				table.insert(turnUse,c)
			end
			if #turnUse>9 then break end
		end
	end
	self.toUse = turnUse
	return turnUse
end

function SmartAI:activate(use)
	if sgs.aiHandCardVisible then
		local file = io.open("lua/ai/cstring", "r")
		local _file = file:read("*all")
		file:close()
		file = io.open("lua/ai/cstring", "w")
		file:write(_file.."\nTurnUse：")
		file:close()
		--self.room:writeToConsole("TurnUse：")
	end
	if #self.toUse<1 then self:getTurnUse() end
	for _,c in ipairs(self:getTurnUse())do
		use.to = self.use_to[c:toString()]
		use.card = c
		--[[if c:isAvailable(self.player) then
			if self:aiUseCard(c,use).card then break
			elseif c:canRecast() then use.card = c break end
		end--]]
		break
	end
end

function SmartAI:getOverflow(player,isMax)
	player = player or self.player
	local m = player:getMaxCards()
	if player:hasSkill("qiaobian") then m = math.max(self.player:getHandcardNum()-1,m) end
	if player:hasSkill("keji") and not player:hasFlag("KejiSlashInPlayPhase")
	or player:hasSkill("zaoyao") then m = self.player:getHandcardNum() end
	if isMax and m>0 then return m end
	if player:getPhase()<sgs.Player_Finish and player:hasSkill("yongsi")
	and not(player:hasSkill("keji") and not player:hasFlag("KejiSlashInPlayPhase"))
	and not player:hasSkill("conghui") then
		local kingdom_num = {}
		for _,ap in sgs.qlist(self.room:getAlivePlayers())do
			if table.contains(kingdom_num,ap:getKingdom()) then continue end
			table.insert(kingdom_num,ap:getKingdom())
		end
		kingdom_num = #kingdom_num
		if kingdom_num>0 then
			if player:getCardCount()>kingdom_num
			then m = math.min(m,player:getCardCount()-kingdom_num)
			else m = 0 end
		end
	end
	if isMax then return m end
	for _,c in ipairs(getKnownCards(player,self.player))do
		if player:isCardLimited(c,sgs.Card_MethodIgnore)
		then m = m+1 end
	end
	return player:getHandcardNum()-m
end

function SmartAI:isWeak(player,getAP)
	if type(player)=="table" then
		for _,p in ipairs(player)do
			if self:isWeak(p,getAP)
			then return true end
		end
		return false
	end
	player = player or self.player
	if hasBuquEffect(player) or player:inYinniState() or player:hasSkill("dev_shanbi")
	or player:getCardCount()>2 and player:hasSkills("longhun|newlonghun")
	or player:getHandcardNum()>1 and player:hasSkills("yingba+pinghe")
	then return false end
	local hp = player:getHp()
	if player:getMark("hunzi")<1 and hp>1 and player:hasSkill("hunzi")
	or player:getMark("mobilehunzi")<1 and hp>2 and player:hasSkill("mobilehunzi")
	then return false end
	for _,m in ipairs(player:getMarkNames())do
		if m:contains("ai_hp") then hp = hp+player:getMark(m) end
	end
	if self:hasSkills("dev_nvshen",self:getEnemies(player))
	then else hp = hp+player:getHujia() end
	if hp>2 then return false end
	if getAP~=false then
		if player==self.player then hp = hp+self:getCardsNum("Analeptic,Peach")
		else hp = hp+getCardsNum("Analeptic,Peach",player,self.player) end
	end
	return hp<=1 or hp<=2 and player:getHandcardNum()+player:getHandPile():length()<=2
end

function SmartAI:hasWizard(players,onlyharm)
	local skill = onlyharm and sgs.wizard_harm_skill or sgs.wizard_skill
	for _,player in sgs.list(players)do
		if player:hasSkills(skill)
		then return true end
	end
end

function SmartAI:canRetrial(player,reason)
	player = player or self.player
	if player:hasSkill("guidao") and reason~="wuhun" then
		local blacknum = #self:addHandPile(nil,player)
		for _,e in sgs.qlist(player:getEquips())do
			if e:isBlack() then blacknum = blacknum+1 end
		end
		if blacknum>0 then return true end
	end
	if player:hasSkill("nosguicai") and #self:addHandPile(nil,player)>0
	or player:hasSkill("jilve") and player:getCardCount()>0 and player:getMark("&bear")>0
	or player:hasSkill("midao") and player:getPile("rice"):length()>0
	or player:hasSkill("zhenyi") and player:getMark("@flziwei")>0
	then return true end
	for _,sk in ipairs(sgs.getPlayerSkillList(player))do
		sk = sgs.Sanguosha:getTriggerSkill(sk:objectName())
		if sk and sk:hasEvent(sgs.AskForRetrial)
		and player:getCardCount()>0
		then return true end
	end
end

function SmartAI:getFinalRetrial(owner,reason)
	local maxenemyseat,wizardf,players = 0,nil,{}
	owner = owner or self.room:getCurrent()
	table.insert(players,owner)
	local na = owner:getNextAlive()
	while na:objectName()~=owner:objectName()do
		table.insert(players,na)
		na = na:getNextAlive()
	end
	for _,ap in sgs.list(players)do
		if self:canRetrial(ap,reason) then
			maxenemyseat = self:isFriend(ap) and 1 or 2
			wizardf = ap
		end
	end
	return maxenemyseat,wizardf
end

--- Determine that the current judge is worthy retrial
-- @param judge The JudgeStruct that contains the judge information
-- @return True if it is needed to retrial
function SmartAI:needRetrial(judge)
	local lord = getLord(self.player)
	self.isWeak_judge = false
	if judge.reason=="lightning" then
		if lord and (judge.who:getRole()=="lord" or judge.who:isChained() and lord:isChained())
		and self:objectiveLevel(lord)<=3 then
			if lord:hasArmorEffect("SilverLion") and lord:getHp()>=2
			and self:isGoodChainTarget(lord,"T") then return false end
			return judge:isBad() and self:damageIsEffective(lord,"T")
		end
		if judge.who:getHp()>1
		and judge.who:hasArmorEffect("SilverLion")
		then return false end
		if self:isFriend(judge.who) then
			if judge.who:isChained()
			and self:isGoodChainTarget(judge.who,"T",nil,3)
			then return false end
		else
			if judge.who:isChained()
			and not self:isGoodChainTarget(judge.who,"T",nil,3)
			then return judge:isGood() end
		end
	elseif judge.reason=="indulgence" then
		if judge.who:isKongcheng() and judge.who:isSkipped(sgs.Player_Draw) then
			if judge.who:hasSkill("shenfen") and judge.who:getMark("&wrath")>=6
			or judge.who:hasSkill("jixi") and judge.who:getPile("field"):length()>2
			or judge.who:hasSkill("lihun") and self:isLihunTarget(self:getEnemies(judge.who),0)
			or judge.who:hasSkill("xiongyi") and judge.who:getMark("@arise")>0
			or judge.who:hasSkill("kurou") and judge.who:getHp()>=3 then
				if self:isFriend(judge.who) then return judge:isBad()
				else return judge:isGood() end
			end
		end
		if self:isFriend(judge.who) then
			if judge.who:getHp()-judge.who:getHandcardNum()>=self:ImitateResult_DrawNCards(judge.who,judge.who:getVisibleSkillList(true))
			and self:getOverflow()<0 then return false end
			if judge.who:hasSkill("tuxi") and judge.who:getHp()>2
			and self:getOverflow()<0 then return false end
			return judge:isBad()
		else return judge:isGood() end
	elseif judge.reason=="supply_shortage" then
		if self:isFriend(judge.who) then
			if self:hasSkills("guidao|tiandu",judge.who)
			then return false end
			return judge:isBad()
		else return judge:isGood() end
	elseif judge.reason:contains("luoshen") then
		if self:isFriend(judge.who) then
			if judge.who:getHandcardNum()>30
			then return false end
			if self:hasCrossbowEffect(judge.who)
			or getKnownCard(judge.who,self.player,"Crossbow",false)>0
			then return judge:isBad() end
			if self:getOverflow(judge.who)>1
			and self.player:getHandcardNum()<3
			then return false end
			return judge:isBad()
		else return judge:isGood() end
	elseif judge.reason:contains("tuntian") then
		if not judge.who:hasSkill("zaoxian")
		and judge.who:getMark("zaoxian")<1
		then return false end
	elseif judge.reason:contains("beige")
	then return true end
	local callback = sgs.ai_need_retrial_func[judge.reason]
	if type(callback)=="function" then
		callback = callback(self,judge,judge:isGood(),judge.who,self:isFriend(judge.who),lord)
	end
	if type(callback)=="boolean" then return callback end
	if self:isFriend(judge.who) then return judge:isBad()
	elseif self:isEnemy(judge.who) then return judge:isGood()
	else
		self.isWeak_judge = true
		if self:isWeak(judge.who) then return judge:isBad()
		else return judge:isGood() end
	end
end

--- Get the retrial cards with the lowest keep value
-- @param cards the table that contains all cards can use in retrial skill
-- @param judge the JudgeStruct that contains the judge information
-- @return the retrial card id or -1 if not found
function SmartAI:getRetrialCardId(cards,judge,self_card,exchange)
	local can_use,other_suit,hasSpade = {},{},false
	for _,c in sgs.list(cards)do
		local x = CardFilter(c,judge.who)
		if judge.reason:contains("beige") and not isCard("Peach",c,self.player) then
			local damage = self.room:getTag("CurrentDamageStruct"):toDamage()
			if damage.from then
				if self:isFriend(damage.from) then
					if not self:toTurnOver(damage.from,0)
					and judge.card:getSuit()~=0
					and x:getSuit()==0 then
						hasSpade = true
						table.insert(can_use,c)
					elseif (self_card==false or self:getOverflow()>0)
					and judge.card:getSuit()~=x:getSuit() then
						if judge.card:getSuit()==2 and judge.who:isWounded() and self:isFriend(judge.who)
						or judge.card:getSuit()==3 and self:isEnemy(judge.who) and hasManjuanEffect(judge.who)
						or judge.card:getSuit()==1 and self:needToThrowArmor(damage.from) then 
						elseif (self:isFriend(judge.who) and x:getSuit()==2 and judge.who:isWounded()
						or x:getSuit()==3 and self:isEnemy(judge.who) and hasManjuanEffect(judge.who)
						or x:getSuit()==3 and self:isFriend(judge.who) and not hasManjuanEffect(judge.who)
						or x:getSuit()==1 and (self:needToThrowArmor(damage.from) or damage.from:isNude()))
						or judge.card:getSuit()==1 and self:toTurnOver(damage.from,0)
						then table.insert(other_suit,c) end
					end
				else
					if not self:toTurnOver(damage.from,0)
					and x:getSuit()~=0 and judge.card:getSuit()==0
					then table.insert(can_use,c) end
				end
			end
		elseif (self:isFriend(judge.who) and judge:isGood(x) or self:isEnemy(judge.who) and not judge:isGood(x))
		and not(self_card~=false and (self:getFinalRetrial(nil,judge.reason)>1 or self:dontRespondPeachInJudge(judge)) and isCard("Peach",c,self.player) and not self:isWeak(self.friends))
		then table.insert(can_use,c) end
	end
	if not hasSpade and #other_suit>0
	then InsertList(can_use,other_suit) end
	if judge.reason~="lightning" then
		for _,ap in sgs.qlist(self.room:getAllPlayers())do
			if ap:containsTrick("lightning") then
				for i=#can_use,1,-1 do
					local dc = CardFilter(can_use[i],ap)
					if dc:getSuit()==sgs.Card_Spade
					and dc:getNumber()>=2 and dc:getNumber()<=9
					then table.remove(can_use,i) break end
				end
			end
		end
	end
	if #can_use<1 and exchange then
		for _,c in sgs.list(cards)do
			if judge:isGood(CardFilter(c,judge.who))
			then if judge:isGood() then table.insert(can_use,c) end
			elseif judge:isBad() then table.insert(can_use,c) end
			if judge.pattern=="." then table.insert(can_use,c) end
		end
	end
	for _,c in sgs.list(can_use)do
		local id = c:getEffectiveId()
		if self:doDisCard(self.player,id)
		then return id end
	end
	if #can_use>0 then
		self:sortByKeepValue(can_use)
		return can_use[1]:getEffectiveId()
	end
	return -1
end

function CardFilter(cid,owner,place)
	local gc,tc = cid,nil
	if type(cid)=="userdata" then cid = cid:getEffectiveId()
	else gc = sgs.Sanguosha:getCard(cid) end
	for _,s in ipairs(sgs.getPlayerSkillList(owner))do
		if s:inherits("FilterSkill") then
			local cp = global_room:getCardPlace(cid)
			if cp==sgs.Player_PlaceEquip then break end
			owner = BeMan(global_room,owner)
			local co,cl = global_room:getCardOwner(cid),sgs.CardList()
			global_room:setCardMapping(cid,owner,place or sgs.Player_PlaceJudge)
			local vas = sgs.Sanguosha:getViewAsSkill(s:objectName())
			if vas:viewFilter(cl,gc) then
				cl:append(gc)
				tc = vas:viewAs(cl)
				if tc then
					tc = sgs.Sanguosha:cloneCard(tc)
					tc:setId(-1)
					tc:deleteLater()
				end
			end
			global_room:setCardMapping(cid,co,cp)
			if cl:length()>0 then global_room:filterCards(co or owner,cl,true) end
		end
	end
	return tc or gc
end

function SmartAI:damageIsEffective(to,card_nature,from)
	local struct = {}
	struct.to = to or self.player
	struct.from = from or self.room:getCurrent() or self.player
	if type(card_nature)=="userdata" then struct.card = card_nature
	else struct.nature = card_nature end
	return self:damageStruct(struct)
end

sgs.card_damage_nature = {}

function SmartAI:damageStruct(struct)
	if type(struct)~="table" and type(struct)~="userdata"
	or type(struct.to)~="userdata" then self.room:writeToConsole(debug.traceback())
	elseif self:ajustDamage(struct.from,struct.to,struct.damage,struct.card,struct.nature)>0
	then return not sgs.ai_humanized or math.random()<0.95 end
end

function prohibitUseDirectly(card,player)
	return player:isCardLimited(card,card:getHandlingMethod())
end

function SmartAI:cardsView(class_name)
	local card_name = patterns(class_name)
	if (class_name=="Peach" or class_name=="Analeptic")
	and global_room:getCurrentDyingPlayer()==self.player
	then card_name = "peach+analeptic" end
	local cvs = {}
	for _,s in ipairs(sgs.getPlayerSkillList(self.player))do
		local cv = sgs.ai_cardsview_valuable[s:objectName()]
		if type(cv)=="function" then
			local vs = sgs.Sanguosha:getViewAsSkill(s:objectName())
			if vs and vs:isEnabledAtResponse(self.player,card_name) then
				vs = cv(self,class_name,self.player)
				if vs then
					if type(vs)=="table" then
						for _,c in ipairs(vs)do
							table.insert(cvs,c)
						end
					else
						table.insert(cvs,vs)
					end
				end
			end
		end
		cv = sgs.ai_cardsview[s:objectName()]
		if type(cv)=="function" then
			local vs = sgs.Sanguosha:getViewAsSkill(s:objectName())
			if vs and vs:isEnabledAtResponse(self.player,card_name) then
				vs = cv(self,class_name,self.player)
				if vs then
					if type(vs)=="table" then
						for _,c in ipairs(vs)do
							table.insert(cvs,c)
						end
					else
						table.insert(cvs,vs)
					end
				end
			end
		end
	end
	return cvs
end

function getSkillViewCard(class_name,card,player,place)
	for _,s in ipairs(sgs.getPlayerSkillList(player))do
		local va = sgs.ai_view_as[s:objectName()]
		if type(va)=="function" then
			local vs = sgs.Sanguosha:getViewAsSkill(s:objectName())
			if vs==nil then continue end
			local card_name = patterns(class_name)
			if (class_name=="Peach" or class_name=="Analeptic")
			and global_room:getCurrentDyingPlayer()==player then card_name = "peach+analeptic" end
			if vs:isEnabledAtResponse(player,card_name) then
				place = place or global_room:getCardPlace(card:getId())
				if place==sgs.Player_PlaceSpecial and player:getHandPile():contains(card:getId()) then
					if sgs.Sanguosha:getCurrentCardUseReason()==sgs.CardUseStruct_CARD_USE_REASON_RESPONSE_USE
					or sgs.Sanguosha:getCurrentCardUseReason()==sgs.CardUseStruct_CARD_USE_REASON_RESPONSE
					then place = sgs.Player_PlaceHand end
				end
				vs = va(card,player,place,class_name)
				if vs==nil then continue end
				vs = sgs.Card_Parse(vs)
				if vs and vs:isKindOf(class_name)
				then return vs end
			end
		end
	end
end

function isCard(class_name,card,player)
	if type(class_name)~="string" then return end
	if class_name:contains(",") then
		for _,cn in ipairs(class_name:split(","))do
			local c = isCard(cn,card,player)
			if c then return c end
		end
	else
		if type(card)~="userdata" then card = sgs.Sanguosha:getCard(card) end
		if card:isKindOf(class_name) then return card end
		local cf = CardFilter(card,player,sgs.Player_PlaceHand)
		if cf:isKindOf(class_name) then return card end
		cf = global_room:getCardOwner(card:getEffectiveId())
		cf = cf~=player and sgs.Player_PlaceHand
		return getSkillViewCard(class_name,card,player,cf)
	end
end

function string:isCard(card,player)
	return isCard(self,card,player)
end

function SmartAI:getMaxCard(player,cards)
	player = player or self.player
	cards = cards or player:getHandcards()
	cards = sgs.QList2Table(cards)
	if #cards<1 then return end
	local max_card,max_point = nil,0
	for _,card in ipairs(cards)do
		if player==self.player and self:isValuableCard(card) then continue end
		if self.player:canSeeHandcard(player) or card:hasFlag("visible")
		or card:hasFlag("visible_"..self.player:objectName().."_"..player:objectName()) then
			local point = card:getNumber()
			if player:hasSkill("tianbian") and card:getSuit()==sgs.Card_Heart then point = 13 end
			if point>max_point then max_point = point max_card = card end
		end
	end
	if player==self.player and not max_card then
		for _,card in ipairs(cards)do
			local point = card:getNumber()
			if player:hasSkill("tianbian") and card:getSuit()==sgs.Card_Heart then point = 13 end
			if point>max_point then max_point = point max_card = card end
		end
	end
	if player~=self.player then return max_card end
	if max_point>0 then
		if self.player:hasFlag("AI_XiechanUsing") or player:hasSkills("tianyi|dahe|xianzhen") then
			for _,card in ipairs(cards)do
				if card:getNumber()==max_point
				and not isCard("Slash",card,player)
				then return card end
			end
		end
		if player:hasSkill("qiaoshui") then
			for _,card in ipairs(cards)do
				if card:getNumber()==max_point
				and not card:isNDTrick()
				then return card end
			end
		end
	end
	return max_card
end

function SmartAI:getMinCard(player,cards)
	player = player or self.player
	cards = cards or player:getHandcards()
	cards = sgs.QList2Table(cards)
	if #cards<1 then return end
	local min_card,min_point = nil,14
	for _,card in ipairs(cards)do
		if card:hasFlag("visible") or self.player:canSeeHandcard(player)
		or card:hasFlag("visible_"..self.player:objectName().."_"..player:objectName()) then
			local point = card:getNumber()
			if card:getSuit()==sgs.Card_Heart and player:hasSkill("tianbian") then point = 13 end
			if point<min_point then min_point = point min_card = card end
		end
	end
	return min_card
end

function SmartAI:getKnownNum(player)
	if player and player~=self.player
	then return #getKnownCards(player,self.player)
	else return #self:addHandPile() end
end

function getKnownNum(player,ap)
	return #getKnownCards(player,ap)
end

function getKnownCard(player,from,class_name,viewas,flags)
	if type(class_name)~="string" then return 0 end
	local known = 0
	if class_name:contains(",") then
		for _,cn in ipairs(class_name:split(","))do
			known = known+getKnownCard(player,from,cn,viewas,flags)
		end
		return known
	end
	local kcs = getKnownCards(player,from,flags)
	for _,kc in ipairs(kcs)do
		if kc:isKindOf(class_name) or kc:getColorString()==class_name or kc:getSuitString()==class_name
		or viewas and isCard(class_name,kc,player) then known = known+1 end
	end
	local n = player:getHandcardNum()
	if #kcs<n/2 and n>2 and known<n/3
	and not(from or global_room:getCurrent()):hasFlag("Global_Dying")
	then known = known+1 end
	return known
end

function string:getKnownCard(to,from,viewas,flags)
	return getKnownCard(player,from,self,viewas,flags)
end

function getKnownCards(player,from,flags)
	if type(player)~="userdata" then global_room:writeToConsole(debug.traceback()) return {} end
	from = from or global_room:getCurrent() or current_self.player
	if type(flags)~="string" then flags = "&h" end
	local cards = {}
	if flags:contains("h") then
		for _,h in sgs.qlist(player:getHandcards())do
			if h:hasFlag("visible") or from:canSeeHandcard(player)
			or h:hasFlag("visible_"..from:objectName().."_"..player:objectName())
			then table.insert(cards,h) end
		end
		if flags:contains("&") then
			for _,id in sgs.qlist(player:getHandPile())do
				if player:pileOpen(player:getPileName(id),from:objectName())
				then table.insert(cards,sgs.Sanguosha:getCard(id)) end
			end
		end
	end
	if flags:contains("e") then
		for _,e in sgs.qlist(player:getEquips())do
			table.insert(cards,e)
		end
	end
	return cards
end

function SmartAI:getCard(class_name,islist)
	if type(class_name)~="string" then return islist and {} end
	local cardArrs = {}
	if class_name:contains(",") then
		sgs.isSplit = 1
		for _,cn in ipairs(class_name:split(","))do
			table.insertTable(cardArrs,self:getCard(cn,true))
		end
		sgs.isSplit = 0
		self:sortByUsePriority(cardArrs)
		return islist and cardArrs or cardArrs[1]
	end
	for _,cs in ipairs(self:getGuhuoCard(class_name))do
		table.insert(cardArrs,sgs.Card_Parse(cs))
	end
	for _,cs in ipairs(self:cardsView(class_name))do
		table.insert(cardArrs,sgs.Card_Parse(cs))
	end
	local cards = self.player:getCards("he")
	local handp = self.player:getHandPile()
	for _,id in sgs.qlist(handp)do
		cards:append(sgs.Sanguosha:getCard(id))
	end
	for _,c in sgs.qlist(cards)do
		if c:hasFlag("using") then continue end
		local cp = self.room:getCardPlace(c:getId())
		if (c:isKindOf(class_name) or class_name==".")
		and (cp~=sgs.Player_PlaceSpecial or handp:contains(c:getId()))
		then table.insert(cardArrs,c)
		else
			cp = getSkillViewCard(class_name,c,self.player,cp)
			if cp then table.insert(cardArrs,cp) end
		end
	end
	for i=#cardArrs,1,-1 do
		if cardArrs[i]:isKindOf("Jinchantuoqiao")
		or cardArrs[i]:toString():contains("jinchantuoqiao") then
			local ids = self.player:handCards()
			for _,id in sgs.qlist(cardArrs[i]:getSubcards())do
				if ids:contains(id) then ids:removeOne(id) end
			end
			handp = 0
			for _,id in sgs.qlist(ids)do
				if sgs.Sanguosha:getCard(id):isKindOf("Jinchantuoqiao") then continue end
				handp = 1
				break
			end
			if handp==1 then
				table.remove(cardArrs,i)
				continue
			end
		end
		if self.player:isLocked(cardArrs[i])
		then table.remove(cardArrs,i) end
	end
	if sgs.isSplit~=1 then self:sortByUsePriority(cardArrs) end
	return islist and cardArrs or cardArrs[1]
end

function SmartAI:getCardId(class_name,islist)
	local cards = self:getCard(class_name,islist)
	if cards then
		if islist then
			local viewArr = {}
			for _,c in ipairs(cards)do
				table.insert(viewArr,c:toString())
			end
			return viewArr
		end
		return cards:toString()
	end
end

function SmartAI:getCards(class_name,flags,acards)
	local cards = {}
	if type(class_name)~="string" then return cards
	elseif class_name:contains(",") then
		for _,cn in ipairs(class_name:split(","))do
			table.insertTable(cards,self:getCards(cn,flags,acards))
		end
		return cards
	end
	local haspile = type(flags)~="string"
	flags = self.player:getCards(haspile and "he" or flags)
	if acards then
		flags = acards
		for _,c in sgs.list(acards)do
			self.player:addCard(c,sgs.Player_PlaceHand)
		end
	elseif haspile then
		for _,id in sgs.qlist(self.player:getHandPile())do
			flags:append(sgs.Sanguosha:getCard(id))
		end
	end
	haspile = self.player:getHandPile()
	for _,c in sgs.list(flags)do
		if c:hasFlag("using") then continue end
		local cp = self.room:getCardPlace(c:getId())
		if (c:isKindOf(class_name) or class_name==".")
		and (cp~=sgs.Player_PlaceSpecial or haspile:contains(c:getId()))
		then table.insert(cards,c)
		else
			cp = getSkillViewCard(class_name,c,self.player,cp)
			if cp then table.insert(cards,cp) end
		end
	end
	for _,cs in ipairs(self:cardsView(class_name))do
		table.insert(cards,sgs.Card_Parse(cs))
	end
	if acards then
		for _,c in sgs.list(acards)do
			self.player:removeCard(c,sgs.Player_PlaceHand)
		end
	end
	--self.aiUsing = sgs.IntList()
	for i=#cards,1,-1 do
		if self.player:isLocked(cards[i]) then table.remove(cards,i) end
	end
	return cards
end

function getCardsNum(class_name,to,from)
	return getKnownCard(to,from,class_name,true,"he")
end

function string:getCardsNum(to,from)
	return getCardsNum(self,to,from)
end

function SmartAI:getCardsNum(class_name,flag,selfonly)
	local n = 0
	if type(class_name)=="table" then
		for _,class in ipairs(class_name)do
			n = n+self:getCardsNum(class,flag,selfonly)
		end
		return n
	end
	if type(class_name)~="string" then return 0 end
	for _,c in ipairs(self:getCards(class_name,flag))do
		if ("spear|fuhun"):match(c:getSkillName())
		then n = n+math.floor(#self:addHandPile()/2)
		elseif c:getSkillName()=="jiuzhu"
		then n = math.max(n,math.max(0,math.min(self.player:getCardCount(),self.player:getHp()-1)))
		elseif c:getSkillName():contains("chunlao")
		then n = n+self.player:getPile("wine"):length()
		else n = n+1 end
	end
	if selfonly then return n end
	if class_name:contains("Jink") then
		if self.player:hasLordSkill("hujia") then
			for _,liege in sgs.list(self.room:getLieges("wei",self.player))do
				if self:isFriend(liege) then n = n+getCardsNum("Jink",liege,self.player) end
			end
		end
	elseif class_name:contains("Slash") then
		if self.player:hasLordSkill("jijiang") then
			for _,liege in sgs.list(self.room:getLieges("shu",self.player))do
				if self:isFriend(liege) then n = n+getCardsNum("Slash",liege,self.player) end
			end
		end
	end
	return n
end

function SmartAI:getAllPeachNum(player)
	local pn = 0
	player = player or self.player
	local ws = (self.room:getCurrent() or self.player):hasSkill("wansha")
	for _,friend in ipairs(self:getFriends(player))do
		local cn = "Peach"
		if player==friend then cn = "Peach,Analeptic"
		elseif ws then continue end
		pn = pn+getCardsNum(cn,friend,self.player)
	end
	return pn
end

function SmartAI:getRestCardsNum(class_name,yuji)
	sgs.discard_pile = self.room:getDiscardPile()
	yuji = yuji or self.player
	local knownnum = 0
	for _,id in sgs.qlist(sgs.discard_pile)do
		if sgs.Sanguosha:getCard(id):isKindOf(class_name)
		then knownnum = knownnum+1 end
	end
	for _,ap in sgs.qlist(self.room:getOtherPlayers(yuji))do
		knownnum = knownnum+getKnownCard(ap,yuji,class_name)
	end
	local m = #PatternsCard(class_name,true)
	return m-knownnum-(sgs.ai_humanized and math.random(0,m/2) or 0)
end

function SmartAI:hasSuit(suit_strings,include_equip,player)
	return self:getSuitNum(suit_strings,include_equip,player)>0
end

function SmartAI:getSuitNum(suit_strings,include_equip,player)
	player = player or self.player
	local n,allcards = 0,player:getCards(include_equip and "he" or "h")
	if player~=self.player then
		allcards = getKnownCards(player,self.player,include_equip and "he" or "h")
	end
	for _,c in sgs.list(allcards)do
		for _,s in ipairs(suit_strings:split("|"))do
			if c:getColorString()==s
			or c:getSuitString()==s
			then n = n+1 end
		end
	end
	return n
end

function SmartAI:hasSkill(skill)
	if type(skill)=="table" then skill = skill.name
	elseif type(skill)~="string" then skill = skill:objectName() end
	local gs = sgs.Sanguosha:getSkill(skill)
	if gs and gs:isLordSkill() then return self.player:hasLordSkill(skill)
	else return self.player:hasSkill(skill) end
end

function SmartAI:hasSkills(skill_names,player)
	player = player or self.player
	if type(player)=="table" then
		for _,p in ipairs(player)do
			if p:hasSkills(skill_names)
			then return p end
		end
	else
		return player:hasSkills(skill_names)
	end
end

sgs.ai_fill_skill = {}

function SmartAI:fillSkillCards(cards)
	--[[
	for _,skill in ipairs(sgs.ai_skills)do
		if (self:hasSkill(skill.name) or self.player:getMark("ViewAsSkill_"..skill.name.."Effect")>0)
		and sgs.Sanguosha:getViewAsSkill(skill.name):isEnabledAtPlay(self.player) then
			local gc = skill.getTurnUseCard(self,#cards<1)
			if type(gc)=="table" then InsertList(cards,gc)
			elseif gc then table.insert(cards,gc) end
		end
	end--]]
	for _,skill in ipairs(sgs.getPlayerSkillList(self.player))do
		local fs = sgs.ai_fill_skill[skill:objectName()]
		if type(fs)=="function" then
			local vs = sgs.Sanguosha:getViewAsSkill(skill:objectName())
			if vs==nil or vs:isEnabledAtPlay(self.player) then
				vs = fs(self,#cards<1)
				if type(vs)=="userdata"
				then table.insert(cards,vs)
				elseif type(vs)=="table" then
					for _,c in ipairs(vs)do
						table.insert(cards,c)
					end
				end
			end
		end
	end
	for _,m in ipairs(self.player:getMarkNames())do
		if self.player:getMark(m)>0 and m:startsWith("ViewAsSkill_") then
			m = string.gsub(m,"ViewAsSkill_","")
			m = string.gsub(m,"Effect","")
			local fs = sgs.ai_fill_skill[m]
			if type(fs)=="function" then
				local vs = sgs.Sanguosha:getViewAsSkill(m)
				if vs==nil or vs:isEnabledAtPlay(self.player) then
					vs = fs(self,#cards<1)
					if type(vs)=="userdata"
					then table.insert(cards,vs)
					elseif type(vs)=="table" then
						for _,c in ipairs(vs)do
							table.insert(cards,c)
						end
					end
				end
			end
		end
	end
	return cards
end

function SmartAI:useSkillCard(card,use)
	local name = card:getClassName()
	if card:isKindOf("LuaSkillCard")
	then name = "#"..card:objectName() end
	local invoke = sgs.ai_skill_use_func[name]
	if type(invoke)=="function"
	then invoke(card,use,self)
	else
		invoke = self["useCard"..name]
		if invoke then invoke(self,card,use) end
	end
	if use.card and use.card:willThrow()
	and use.card:subcardsLength()>0 then
		invoke = self.player:getHp()
		for _,id in sgs.qlist(use.card:getSubcards())do
			local c = sgs.Sanguosha:getCard(id)
			if c:isKindOf("Poison") or c:isKindOf("Shit")
			then invoke = invoke-1 end
		end
		if invoke<1 and invoke+self:getAllPeachNum()<1
		then use.card = nil end
	end	
end

function SmartAI:aoeIsEffective(card,to,player)
	if self:hasHuangenEffect(to) then return end
	return self:hasTrickEffective(card,to,player)
end

function SmartAI:hasHuangenEffect(to)
	for _,lx in sgs.qlist(self.room:findPlayersBySkillName("huangen"))do
		local friends = self:getFriends(lx)
		if math.min(lx:getHp(),#friends)>0 then
			to = to or self.player
			if type(to)~="table" then to = {to} end
			for _,p in ipairs(to)do
				if table.contains(friends,p)
				then return lx end
			end
		end
	end
end

function SmartAI:canAvoidAOE(card)
	if not self:aoeIsEffective(card,self.player)
	or card:isKindOf("SavageAssault") and self:getCardsNum("Slash")>0
	then return true end
	if card:isKindOf("ArcheryAttack") then
		if self:getCardsNum("Jink")>0
		or self.player:getHp()>1 and self.player:hasArmorEffect("EightDiagram")
		then return true end
	end
end

function SmartAI:exclude(players,card,from)
	local excluded = {}
	for _,p in sgs.list(players)do
		if (from or self.player):canUse(card,p)
		then table.insert(excluded,p) end
	end
	return excluded
end

function SmartAI:getJiemingChaofeng(player)
	local max_x,chaofeng = 0,0
	for _,friend in sgs.list(self:getFriends(player))do
		local x = math.min(friend:getMaxHp(),5)-friend:getHandcardNum()
		if x>max_x then max_x = x end
	end
	if max_x<2 then chaofeng = 5-max_x*2
	else chaofeng = -max_x*2 end
	return chaofeng
end

function SmartAI:getAoeValueTo(card,to,from)
	local value,sj_num = 0,0
	if card:isKindOf("ArcheryAttack") then
		value = -50
		sj_num = getCardsNum("Jink",to,self.player)
		if sj_num<1 or to:isCardLimited(dummyCard("jink"),sgs.Card_MethodResponse)
		then value = -70 end
		if to:hasSkills("leiji|nosleiji|olleiji")
		and (sj_num>=1 or to:hasArmorEffect("EightDiagram"))
		and self:findLeijiTarget(to,50,self.player) then
			value = value+100
			if self:hasSuit("spade",true,to) then value = value+150
			else value = value+to:getHandcardNum()*35 end
		elseif to:hasArmorEffect("EightDiagram") then
			value = value+20
			if self:getFinalRetrial()==2 then value = value-15
			elseif self:getFinalRetrial()==1 then value = value+10 end
		end
		if sj_num>=1 and to:hasSkills("mingzhe|gushou") then value = value+8 end
		if sj_num>=1 and to:hasSkill("xiaoguo") then value = value-4 end
	elseif card:isKindOf("SavageAssault") then
		value = -50
		sj_num = getCardsNum("Slash",to,self.player)
		if sj_num<1 or to:isCardLimited(dummyCard(),sgs.Card_MethodResponse)
		then value = -70 end
		if sj_num>=1 and to:hasSkill("gushou") then value = value+8 end
		if sj_num>=1 and to:hasSkill("xiaoguo") then value = value-4 end
	end
	value = value+math.min(20,to:getHp()*5)
	if self:needToLoseHp(to,from,card,true) then value = value+30 end
	if to:hasSkill("chongzhen") and self:isEnemy(to)
	and getCardsNum("Slash,Jink",to,self.player)>=1
	then value = value+15 end
	--xiemu
	if to:hasSkill("xiemu") and to:getMark("@xiemu_"..from:getKingdom())>0 and card:isBlack() then value = value+35 end
	if to:getHp()<2 and self:getAllPeachNum(to)<1
	then value = value-30 end
	if not hasJueqingEffect(from,to) then
		if sgs.getMode~="06_3v3" and sgs.getMode~="06_XMode" and to:getHp()<2
		and isLord(from) and sgs.ai_role[to:objectName()]=="loyalist" and self:getCardsNum("Peach")<1
		then value = value-from:getCardCount()*20 end
		if to:getHp()>1 then
			if to:hasSkill("quanji") then value = value+10 end
			if to:hasSkill("langgu") and self:isEnemy(to) then value = value-15 end
			if to:hasSkill("jianxiong") then
				value = value+(card:isVirtualCard() and card:subcardsLength()*10 or 10)
			end
			if to:hasSkills("fenyong+xuehen") and to:getMark("@fenyong")<1
			then value = value+30 end
			if to:hasSkill("shenfen") and to:hasSkill("kuangbao") then
				value = value+math.min(25,to:getMark("&wrath")*5)
			end
			if to:hasSkill("beifa") and to:getHandcardNum()==1 and self:needKongcheng(to) then
				if sj_num==1 or getCardsNum("Nullification",to,self.player)==1 then value = value+20
				elseif self:getKnownNum(to)<1 then value = value+5 end
			end
			if to:hasSkill("tanlan") and self:isEnemy(to) and from:getCardCount()>0 then value = value+10 end
		end
	end
	if to:hasSkill("juxiang") and not card:isVirtualCard()
	or to:hasSkill("danlao") and to:aliveCount()>2
	or self:hasHuangenEffect(to)
	then value = value+20 end
	return value
end

function getLord(player)
	if sgs.GetConfig("EnableHegemony",false) then return end
	local gl = global_room:getLord()
	if gl then return gl
	elseif player:getRole()~="renegade" then
		for _,p in sgs.list(global_room:getOtherPlayers(player))do
			if p:getRole()==player:getRole() then return p end
		end
	end
	return player
end

function isLord(player)
	return getLord(player)==player
end

function SmartAI:getAoeValue(card,from)
	local lord,good = getLord(self.player),0
	from = from or self.player
	function canHelpLord()
		local goodnull,kd = 0,{}
		if card:isVirtualCard() and card:subcardsLength()>0
		and from==self.player then
			for _,id in sgs.qlist(card:getSubcards())do
				if isCard("Nullification",id,from) then goodnull = goodnull-1 end
			end
		end
		if card:isKindOf("SavageAssault") then
			for _,s in ipairs(sgs.getPlayerSkillList(lord))do
				if s:objectName():contains("jijiang")
				or s:objectName():contains("qinwang") and lord:getCardCount()>0
				then kd["shu"] = "Slash" break end
			end
		elseif card:isKindOf("ArcheryAttack") then
			for _,s in ipairs(sgs.getPlayerSkillList(lord))do
				if s:objectName():contains("hujia")
				then kd["wei"] = "Jink" break end
			end
		end
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if self:isFriend(lord,p) then
				if getCardsNum("Peach",p,self.player)>0 then return true end
				if kd[p:getKingdom()] and getCardsNum(kd[p:getKingdom()],p,self.player)>0 then return true end
				goodnull = goodnull+getCardsNum("Nullification",p,self.player)
			else goodnull = goodnull-getCardsNum("Nullification",p,self.player) end
		end
		return goodnull>=2
	end
	local isEffective_F,isEffective_E,enemy_number = 0,0,0
	self.aoeTos = self.room:getOtherPlayers(from)
	for _,p in sgs.qlist(self.aoeTos)do
		if self:hasTrickEffective(card,p,from) then
			if self:isFriend(p) then
				isEffective_F = isEffective_F+1
				good = good+self:getAoeValueTo(card,p,from)
				if lord==p and not canHelpLord()
				and sgs.isLordInDanger() and not hasBuquEffect(p)
				then good = good-(p:getHp()<2 and 2013 or 250) end
				if p:hasSkill("dushi") and not from:hasSkill("benghuai")
				and self:isWeak(p) then good = good-40 end
			else
				enemy_number = enemy_number+1
				if self:isEnemy(p) then
					isEffective_E = isEffective_E+1
					good = good-self:getAoeValueTo(card,p,from)
					if lord==p and not canHelpLord()
					and sgs.isLordInDanger() and not hasBuquEffect(p) then
						good = good+300-p:getHp()*100
						if #self.enemies==1 or p:isKongcheng()
						then good = good+200 end
					end
				end
			end
			if self:cantbeHurt(p,from) then
				if p:hasSkill("wuhun") and not self:isWeak(p) and from:getMark("&nightmare+#"..p:objectName())<1 then
					if from==self.player and self.role~="renegade" and self.role~="lord"
					or from~=self.player and not(self:isFriend(from) and from==lord)
					then else good = good-250 end
				else good = good-250 end
			end
		end
	end
	self.aoeTos = nil
	if from:hasSkills("nosjizhi|jizhi") then good = good+50 end
	if isEffective_F+isEffective_E<1 then return good
	elseif isEffective_E<1 then good = good-500 end
	if from:hasSkills("shenfen+kuangbao") then
		good = good+3*enemy_number
		if not from:hasSkill("wumou") then good = good+3*enemy_number end
		if from:getMark("&wrath")>0 then good = good+enemy_number end
	end
	if from:hasSkills("jianxiong|luanji|qice|manjuan") then good = good+2*enemy_number end
	if from:getMark("AI_fangjian-Clear")>0 then good = good+300 end
	local xiahou = self.room:findPlayerBySkillName("yanyu")
	if xiahou and self:isEnemy(xiahou) and xiahou:getMark("YanyuDiscard2")>0 then good = good-50 end
	return good
end

function SmartAI:hasTrickEffective(card,to,from)
	from = from or self.room:getCurrent() or self.player
	to = to or self.player
	if from:isProhibited(to,card)
	or card:isDamageCard() and self:ajustDamage(from,to,1,card)==0 then return end
	local use = {card=card,from=from,to=self.aoeTos or sgs.SPlayerList()}
	if not use.to:contains(to) then use.to:append(to) end
	for _,sk in ipairs(aiConnect(to))do
		local tr = sgs.ai_target_revises[sk]
		if type(tr)=="function"
		and tr(to,card,self,use)
		then return end
	end
	return true
end

function SmartAI:hasEightDiagramEffect(owner)
	owner = owner or self.player
	return owner:hasArmorEffect("EightDiagram")
end

sgs.ai_weapon_value = {}

function SmartAI:evaluateWeapon(card,owner,target)
	if type(card)~="userdata" or not card:isKindOf("Weapon") then return -1 end
	local currentRange = sgs.weapon_range[card:getClassName()] or 1
	owner = owner or self.player
	local deltaSelfThreat,inAttackRange = 0,false
	local w_enemies = target and {target} or self:getEnemies(owner)
	self:sort(w_enemies)
	for i,enemy in ipairs(w_enemies)do
		if owner:distanceTo(enemy)<=currentRange then
			local w_def = self:getDefenseSlash(enemy)/2
			if w_def<0 then w_def = 6-w_def
			elseif w_def<=1 then w_def = 6
			else w_def = 6/w_def end
			deltaSelfThreat = deltaSelfThreat+w_def+#w_enemies-i
			inAttackRange = true
		end
	end
	if inAttackRange and card:isKindOf("Crossbow") then
		local w_slash_num = getCardsNum("Slash",owner,self.player)
		deltaSelfThreat = deltaSelfThreat+w_slash_num*2
		if owner:hasSkill("kurou") then deltaSelfThreat = deltaSelfThreat+getCardsNum("Peach,Analeptic",owner,self.player)+self.player:getHp() end
		if w_slash_num>0 and owner:getWeapon() and not self:hasCrossbowEffect(owner) then
			for _,enemy in ipairs(w_enemies)do
				if owner:distanceTo(enemy)<=currentRange
				and (w_slash_num>1 or getCardsNum("Jink",enemy,self.player)<1)
				then deltaSelfThreat = deltaSelfThreat+5 end
			end
		end
	end
	local w_callback = sgs.ai_weapon_value[card:objectName()]
	if type(w_callback)=="function" then
		deltaSelfThreat = deltaSelfThreat+(w_callback(self,nil,owner) or 0)
		for _,enemy in ipairs(w_enemies)do
			if owner:distanceTo(enemy)<=currentRange then
				local added = sgs.ai_slash_weaponfilter[card:objectName()]
				if type(added)=="function" and added(self,enemy,owner) then deltaSelfThreat = deltaSelfThreat+1 end
				deltaSelfThreat = deltaSelfThreat+(w_callback(self,enemy,owner) or 0)
			end
		end
	end
	if owner:hasSkill("jijiu") and card:isRed() then deltaSelfThreat = deltaSelfThreat+0.5 end
	if owner:hasSkills("qixi|guidao") and card:isBlack() then deltaSelfThreat = deltaSelfThreat+0.5 end
	return deltaSelfThreat,inAttackRange
end

sgs.ai_armor_value = {}

function SmartAI:evaluateArmor(card,owner)
	owner = owner or self.player
	if type(card)=="number" then card = sgs.Sanguosha:getCard(card)
	else card = card or owner:getArmor() end
	local a_value = 0
	for _,as in sgs.qlist(owner:getVisibleSkillList(true))do
		local cb = sgs.ai_armor_value[as:objectName()]
		if type(cb)=="function" then a_value = a_value+(cb(owner,self,card) or 0)
		elseif type(cb)=="number" then a_value = a_value+cb end
	end
	if card then
		a_value = a_value+0.1
		if card:isRed() then
			if owner:hasSkill("jijiu") then a_value = a_value+0.5 end
		elseif card:isBlack() then
			if owner:hasSkills("qixi|guidao") then a_value = a_value+0.5 end
		end
		local cb = sgs.ai_armor_value[card:objectName()]
		if type(cb)=="function" then a_value = a_value+(cb(owner,self,card) or 0)
		elseif type(cb)=="number" then a_value = a_value+cb end
	end
	return a_value
end

function SmartAI:getSameEquip(card,owner)
	if card and card:isKindOf("EquipCard") then
		owner = owner or self.player
		return owner:getEquip(card:getRealCard():toEquipCard():location())
	end
end

function SmartAI:damageMinusHp(enemy,type)
	if not enemy then return 0 end
	local trick_effectivenum,slash_damagenum,analepticpowerup,effectivefireattacknum,basicnum = 0,0,0,0,0
	local cards = self.player:getCards("he")
	for _,acard in sgs.list(cards)do
		if acard:getTypeId()==sgs.Card_TypeBasic and not acard:isKindOf("Peach") then basicnum = basicnum+1 end
	end
	for _,acard in sgs.list(cards)do
		if acard:isDamageCard() and not self.player:isProhibited(enemy,acard)
		or acard:isKindOf("AOE") and self:aoeIsEffective(acard,enemy) then
			if acard:isKindOf("FireAttack") then
				if not enemy:isKongcheng()
				then effectivefireattacknum = effectivefireattacknum+1
				else trick_effectivenum = trick_effectivenum-1 end
			end
			trick_effectivenum = trick_effectivenum+1
		elseif acard:isKindOf("Slash") and self:slashIsEffective(acard,enemy)
		and self.player:distanceTo(enemy)<=self.player:getAttackRange()
		and (slash_damagenum<1 or self:hasCrossbowEffect()) then
			if not(basicnum<2 and enemy:hasSkill("xiangle")) then slash_damagenum = slash_damagenum+1 end
			if analepticpowerup<1 and not IgnoreArmor(self.player,enemy) and self:getCardsNum("Analeptic")>0
			and not(enemy:hasArmorEffect("SilverLion") or enemy:hasArmorEffect("EightDiagram")) then
				slash_damagenum = slash_damagenum+1
				analepticpowerup = analepticpowerup+1
			end
			if self.player:hasWeapon("GudingDlade")
			and (enemy:isKongcheng() or self.player:hasSkill("lihun") and enemy:isMale() and not enemy:hasSkill("kongcheng"))
			and not(not IgnoreArmor(self.player,enemy) and enemy:hasArmorEffect("SilverLion"))
			then slash_damagenum = slash_damagenum+1 end
		end
	end
	if type==0 then return trick_effectivenum+slash_damagenum-effectivefireattacknum-enemy:getHp()
	else return trick_effectivenum+slash_damagenum-enemy:getHp() end
	return -10
end

function getBestHp(owner)
	if owner:getCardCount()>2 and owner:hasSkill("longhun") then return 1 end
	if owner:getMark("hunzi")<1 and owner:hasSkill("hunzi") then return 2 end
	local n = owner:getMaxHp()
	for s,d in pairs({ganlu=1,yinghun=2,nosmiji=1,xueji=1,baobian=math.max(0,n-3)})do
		if owner:hasSkill(s) then
			return math.max((owner:getRole()=="lord" and 3 or 2),n-d)
		end
	end
	if owner:getMark("baiyin")<1 and owner:hasSkills("renjie+baiyin")
	or owner:getMark("zili")<1 and owner:hasSkills("quanji+zili")
	then n = n-1 end
	return n
end

function SmartAI:isGoodHp(to)
	to = to or self.player
	if to:getHp()>1 or hasBuquEffect(to) or canNiepan(to)
	or getCardsNum("Peach,Analeptic",to,self.player)>0 then return true
	elseif not(self.room:getCurrent() and self.room:getCurrent():hasSkill("wansha")) then
		for _,p in sgs.list(self:getFriends(to,true))do
			if getCardsNum("Peach",p,self.player)>0
			then return true end
		end
	end
end

function SmartAI:needToLoseHp(to,from,card,passive,recover)
	from = from or self.room:getCurrent() or self.player
	to = to or self.player
	local n = self:ajustDamage(from,to,1,card)
	if n<0 then
		if to:hasSkills(sgs.masochism_skill)
		then return end
	else
		if card and card:isKindOf("Slash") then
			if n>1 or to:hasSkill("sizhan")
			or from:hasWeapon("IceSword") and to:getCardCount()>1 and not self:isFriend(from,to)
			or from:hasSkill("nosqianxi") and from:distanceTo(to)==1 and not self:isFriend(from,to)
			then return end
		end
		if to:hasLordSkill("shichou") then
			return sgs.ai_need_damaged.shichou(self,from,to)==1
		end
		if to:getMark("dev_110_first_time")<1
		and to:getPhase()==sgs.Player_NotActive and from:faceUp()
		and self:isWeak(from) and to:hasSkill("dev_110")
		then return true end--据守、放逐什么的再说吧
		if self:isGoodHp(to) then
			for _,as in ipairs(aiConnect(to))do
				local nd = sgs.ai_need_damaged[as]
				if type(nd)=="function" and nd(self,from,to,card)
				then return not sgs.ai_humanized or math.random()<0.95 end
			end
		end
	end
	if n>=to:getHp() then return end
	n = getBestHp(to)
	if not passive and to:getMaxHp()>2 then
		local of = self:getOverflow()
		if to:hasSkills("longluo|miji|yinghun") and self:findFriendsByType(sgs.Friend_Draw,to)
		or to:hasSkills("nosrende|rende") and not self:willSkipPlayPhase(to) and self:findFriendsByType(sgs.Friend_Draw,to)
		or of==0 and to:hasSkill("jspdanqi") then n = math.min(n,to:getMaxHp()-1) end
		local count = self.room:getPlayers():length()
		if (of==1 and count>=7 or of==2 and count<7) and to:hasSkill("qinxue")
		or count>=3 and to:hasSkill("canshi") then n = math.min(n,to:getMaxHp()-1) end
	end
	local xiangxiang = self.room:findPlayerBySkillName("jieyin")
	if xiangxiang and xiangxiang:isWounded() and not to:isWounded()
	and to:isMale() and self:isFriend(xiangxiang,to) then
		local need_jieyin = true
		for _,f in ipairs(self:getFriends(to,true))do
			if f:isMale() and f:isWounded() then need_jieyin = false break end
		end
		if need_jieyin then n = math.min(n,to:getMaxHp()-1) end
	end
	return (to:getHp()>n or recover and to:getHp()>=n)
	and (not sgs.ai_humanized or math.random()<0.95)
end

function IgnoreArmor(from,to)
	return from:hasWeapon("QinggangSword")
	or not to:hasArmorEffect(nil)
end

function SmartAI:needToThrowArmor(player,reason)
	player = player or self.player
	if type(player)~="userdata" or not player:hasArmorEffect(nil) then return false end
	if player:getArmor() and not player:getArmor():isKindOf("EightDiagram") and player:hasSkills("bazhen|yizhong")
	or self:evaluateArmor(nil,player)<=-2 then return true end
	if player:isWounded() and player:hasArmorEffect("SilverLion") then
		if player~=self.player and self:isFriend(player)
		then return self:isWeak(player) and not player:hasSkills(sgs.use_lion_skill) end
		return true
	end
	return reason~="moukui"
	and self.player:getPhase()==sgs.Player_Play
	and getCardsNum("Jink",player,self.player)<1
	and self:isEnemy(player) and player:hasArmorEffect("Vine")
	and not self:slashProhibit(dummyCard("fire_slash"),player)
	and self:slashIsAvailable() and not IgnoreArmor(self.player,player)
	and (self:getCard("FireSlash") or (self:getCard("Slash") and (self.player:hasSkills("lihuo|zonghuo") or self:getCardsNum("fan")>0)))
end

function SmartAI:loseEquipEffect(to)
	to = to or self.player
	if to:hasSkills(sgs.lose_equip_skill) then return true end
	for _,sk in ipairs(sgs.getPlayerSkillList(to))do
		local dp = sk:getDescription()
		if string.find(dp,"装备区") and string.find(dp,"失去") then
			local t = sgs.Sanguosha:getTriggerSkill(sk:objectName())
			if t and t:hasEvent(sgs.CardsMoveOneTime)
			then return true end
		end
	end
end

function SmartAI:findPlayerToDiscard(flags,include_self,no_dis,players,reason)
	local friends,enemies = {},{}
	if players then
		for _,p in sgs.list(players)do
			if self:isEnemy(p) then table.insert(enemies,p)
			elseif self:isFriend(p) and (include_self or p:objectName()~=self.player:objectName())
			then table.insert(friends,p) end
		end
	else
		friends = include_self and self.friends or self.friends_noself
		enemies = self.enemies
	end
	flags = flags or "he"
	reason = reason or ""
	local player_table = {}
	no_dis = no_dis==false
	function IsDis(from,to)
		if reason=="zhujinqiyuan" then
			if from:distanceTo(to)>1
			then no_dis = false else no_dis = true end
		end
	end
	if type(flags)~="string" then return {} end
	self:sort(enemies)
	if flags:contains("e") then
		for _,enemy in sgs.list(enemies)do
			IsDis(self.player,enemy)
			local dangerous = self:getDangerousCard(enemy)
			if dangerous and self:doDisCard(enemy,dangerous,no_dis)
			then table.insert(player_table,enemy) end
		end
		for _,enemy in sgs.list(enemies)do
			IsDis(self.player,enemy)
			if enemy:hasArmorEffect("EightDiagram")
			and enemy:getArmor() and not self:needToThrowArmor(enemy) then
				if self:doDisCard(enemy,enemy:getArmor():getEffectiveId(),no_dis)
				then table.insert(player_table,enemy) end
			end
		end
	end
	if flags:contains("j") then
		for _,friend in sgs.list(friends)do
			IsDis(self.player,friend)
			if (friend:containsTrick("indulgence") and not friend:hasSkill("keji") or friend:containsTrick("supply_shortage"))
			and not friend:containsTrick("YanxiaoCard") and not (friend:hasSkill("qiaobian") and not friend:isKongcheng())
			and self:doDisCard(friend,"j",no_dis)
			then table.insert(player_table,friend) end
		end
		for _,friend in sgs.list(friends)do
			IsDis(self.player,friend)
			if self:hasWizard(enemies,true)
			and friend:containsTrick("lightning")
			and self:doDisCard(friend,"j",no_dis)
			then table.insert(player_table,friend) end
		end
		for _,enemy in sgs.list(enemies)do
			IsDis(self.player,enemy)
			if self:hasWizard(enemies,true)
			and enemy:containsTrick("lightning")
			and self:doDisCard(enemy,"j",no_dis)
			then table.insert(player_table,enemy) end
		end
	end
	if flags:contains("e") then
		for _,friend in sgs.list(friends)do
			IsDis(self.player,friend)
			if self:doDisCard(friend,"e",no_dis)
			then table.insert(player_table,friend) end
		end
		for _,enemy in sgs.list(enemies)do
			IsDis(self.player,enemy)
			if self:doDisCard(enemy,"e",no_dis) then
				local valuable = self:getValuableCard(enemy)
				if valuable and self:doDisCard(enemy,valuable,no_dis)
				then table.insert(player_table,enemy) end
			end
		end
		for _,enemy in sgs.list(enemies)do
			IsDis(self.player,enemy)
			if enemy:hasSkills("jijiu|beige|mingce|weimu|qingcheng") then
				for _,e in sgs.list(enemy:getEquips())do
					if self:doDisCard(enemy,e:getId(),no_dis)
					then table.insert(player_table,enemy) break end
				end
			end
		end
	end
	if flags:contains("h") then
		for _,enemy in sgs.list(enemies)do
			IsDis(self.player,enemy)
			if enemy:getHandcardNum()<=2 and enemy:getHandcardNum()>0
			and not(enemy:hasSkills("tuntian+zaoxian") and not enemy:hasFlag("CurrentPlayer")) then
				local flag = string.format("%s_%s_%s","visible",self.player:objectName(),enemy:objectName())
				for _,h in sgs.list(enemy:getHandcards())do
					if (h:hasFlag("visible") or h:hasFlag(flag))
					and (h:isKindOf("Peach") or h:isKindOf("Analeptic")) 
					and self:doDisCard(enemy,h:getEffectiveId(),no_dis)
					then table.insert(player_table,enemy) end
				end
			end
		end
	end
	if flags:contains("e") then
		for _,enemy in sgs.list(enemies)do
			IsDis(self.player,enemy)
			if enemy:hasEquip()
			and self:doDisCard(enemy,"e",no_dis)
			then table.insert(player_table,enemy) end
		end
	end
	if flags:contains("j") then
		for _,enemy in sgs.list(enemies)do
			IsDis(self.player,enemy)
			if self:doDisCard(enemy,"j",no_dis)
			then table.insert(player_table,enemy) end
		end
	end
	if flags:contains("h") then
		self:sort(enemies,"handcard")
		for _,enemy in sgs.list(enemies)do
			IsDis(self.player,enemy)
			if self:doDisCard(enemy,"h",no_dis)
			then table.insert(player_table,enemy) end
		end
		local zhugeliang = self.room:findPlayerBySkillName("kongcheng")
		if zhugeliang then
			IsDis(self.player,zhugeliang)
			if self:isFriend(zhugeliang) and zhugeliang:getHandcardNum()==1
			and self:getEnemyNumBySeat(self.player,zhugeliang)>0 and zhugeliang:getHp()<=2
			and self:doDisCard(zhugeliang,"h",no_dis)
			then table.insert(player_table,zhugeliang) end
		end
	end
 	local new_player_table = {}	--有的角色重复加入了，需要去重
	for _,p in sgs.list(player_table)do
		if table.contains(new_player_table,p)
		then else table.insert(new_player_table,p) end
	end
	return new_player_table
end

function SmartAI:findPlayerToDraw(include_self,drawnum,count)
	drawnum = drawnum or 1
	local friends,tos = {},{}
	for _,p in sgs.list(include_self and self.room:getAlivePlayers() or self.room:getOtherPlayers(self.player))do
		if self:isFriend(p) and not hasManjuanEffect(p)
		and not(drawnum<=2 and p:isKongcheng() and p:hasSkill("kongcheng"))
		then table.insert(friends,p) end
	end
	if #friends<1 then return count and {} end

	self:sort(friends)
	for _,friend in sgs.list(friends)do
		if friend:getHandcardNum()<2 and not self:needKongcheng(friend) and not self:willSkipPlayPhase(friend) then
			if not table.contains(tos,friend) then table.insert(tos,friend) end
		end
	end

	local at = self:AssistTarget()
	if at and table.contains(friends,at) and not self:willSkipPlayPhase(at)
	and (at:getHandcardNum()<at:getMaxCards()*2 or at:getHandcardNum()<self.player:getHandcardNum()) then
		if not table.contains(tos,at) then table.insert(tos,at) end
	end

	for _,friend in sgs.list(friends)do
		if friend:hasSkills(sgs.cardneed_skill) and not self:willSkipPlayPhase(friend) then
			if not table.contains(tos,friend) then table.insert(tos,friend) end
		end
	end

	self:sort(friends,"handcard")
	for _,friend in sgs.list(friends)do
		if not self:needKongcheng(friend) and not self:willSkipPlayPhase(friend) then
			if not table.contains(tos,friend) then table.insert(tos,friend) end
		end
	end
	return count and tos or tos[1]
end

function SmartAI:findPlayerToDamage(damage,player,nature,targets,include_self,base_value)
	damage = damage or 1
	nature = nature or "N"
	base_value = base_value or 0	
	targets = targets or include_self~=false and self.room:getOtherPlayers(player) or self.room:getAlivePlayers()
	targets = sgs.QList2Table(targets)
	if #targets<2 then return targets end
	
	function getDamageValue(target,self_only)
		local value,count = 0,self:ajustDamage(player,target,damage,nil,nature)
		if count>0 then
			value = value+count*20 --设1牌价值为10，且1体力价值2牌，1回合价值2.5牌，下同
			local hp = target:getHp()
			local deathFlag = count>=hp and count>=hp+self:getAllPeachNum(target)
			if deathFlag then
				value = value+500
			else
				if hp>=getBestHp(target)+count then value = value-2 end
				if self:isWeak(target) then value = value+15
				else value = value+12-sgs.getDefense(target) end
				if hp<=count then
					if target:faceUp() and target:hasSkill("jiushi")
					then value = value+25 end
				end
				if self:needToLoseHp(target,player) then
					value = value-5
					if target:hasSkill("nosyiji") then value = value-20*count end
					if target:hasSkill("yiji") then value = value-10*count end
					if target:hasSkill("jieming") then
						local chaofeng = self:getJiemingChaofeng(target)
						if chaofeng>0 then value = value-(5-chaofeng)*5
						else value = value+chaofeng*5 end
					end
					if target:hasSkill("guixin") then
						local x = 0
						value = value+25
						for _,p in sgs.list(self.room:getOtherPlayers(target))do
							if p:getCardCount(true,true)>0 then
								if self:isFriend(p,target)
								then if p:getJudgingArea():length()>0 then value = value-5 end
								elseif p:isNude() then value = value+5 end
								x = x+1
							end
						end
						if not hasManjuanEffect(target)
						then value = value-x*10 end
					end
					if target:hasSkill("chengxiang") then value = value+15 end
					if target:hasSkill("noschengxiang") then value = value+15 end
				end
			end
			if self:isFriend(target) then value = -value
			elseif not self:isEnemy(target) then value = value/2 end
			if self_only or nature=="N" then
			elseif target:isChained() then
				for _,p in sgs.list(self.room:getOtherPlayers(target))do
					if p:isChained() then
						value = value+getDamageValue(p,true)
					end
				end
			end
			if self:cantbeHurt(target,player,count) then value = value-800 end
			if deathFlag and self.role=="renegade" and target:getRole()=="lord"
			and target:aliveCount()>2 then value = value-1000 end
		end
		return value
	end
	local bcv = {}
	for _,p in sgs.list(targets)do
		bcv[p:objectName()] = getDamageValue(p)
	end
	local function func(a,b)
		return bcv[a:objectName()]>bcv[b:objectName()]
	end
	table.sort(targets,func)
	local result = {}
	for _,p in sgs.list(targets)do
		if bcv[p:objectName()]>base_value
		then table.insert(result,p) end
	end
	return result
end

function SmartAI:dontRespondPeachInJudge(judge)
	local peach_num = self:getCardsNum("Peach")
	if peach_num<1 then return false end
	if self:willSkipPlayPhase() and peach_num>self:getOverflow(self.player,true) then return false end
	local card = self:getCard("Peach")
	local dummy = dummy()
	self:useBasicCard(card,dummy)
	if dummy.card then return true end
	if peach_num<=self.player:getLostHp()
	or self:isWeak(self.friends)
	then return true end
	--judge.reason:baonue,neoganglie,ganglie,caizhaoji_hujia
	if judge.reason=="tuntian" and judge.who:getMark("zaoxian")<1
	and judge.who:getPile("field"):length()<2 then return true
	elseif (judge.reason=="EightDiagram" or judge.reason=="bazhen")
	and (not self:isWeak(judge.who) or judge.who:hasSkills(sgs.masochism_skill))
	and self:isFriend(judge.who) then return true
	elseif judge.reason=="nosmiji" and judge.who:getLostHp()==1
	then return true
	elseif judge.reason=="shaoying"
	and sgs.shaoying_target then
		if sgs.shaoying_target:hasArmorEffect("Vine") and sgs.shaoying_target:getHp()>3
		or sgs.shaoying_target:getHp()>2 then return true end
	elseif judge.reason:contains("tieji")
	or judge.reason:contains("qianxi")
	or judge.reason:contains("beige")
	then return true end
	return false
end

function CanUpdateIntention(p)
	local rn = 0
	for _,ap in sgs.qlist(global_room:getAlivePlayers())do
		if sgs.ai_role[ap:objectName()]=="rebel" then rn = rn+1 end
	end
	return not(sgs.ai_role[p:objectName()]=="rebel" and rn>=sgs.playerRoles.rebel
	or sgs.ai_role[p:objectName()]=="neutral" and rn>sgs.playerRoles.rebel/2)
end

function SmartAI:AssistTarget()
	if self.ai_AssistTarget_off then return end
	if not self.ai_AssistTarget or self.ai_AssistTarget:isDead() then
		local human_count = 0
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if p:getState()~="robot" then
				human_count = human_count+1
				self.ai_AssistTarget = p
			end
		end
		self.ai_AssistTarget_off = human_count~=1
	end
	if self.ai_AssistTarget and self.ai_AssistTarget~=self.player and self:isFriend(self.ai_AssistTarget)
	and self:getOverflow(self.ai_AssistTarget)==2 and not self.ai_AssistTarget:hasSkill("nosjuejing")
	then return self.ai_AssistTarget end
end

function SmartAI:findFriendsByType(prompt,player)
	player = player or self.player
	local friends = self:getFriends(player,true)
	if #friends<1 then return false end
	if prompt==sgs.Friend_Draw then
		for _,friend in sgs.list(friends)do
			if not friend:hasSkill("manjuan") and not self:needKongcheng(friend,true) then return true end
		end
	elseif prompt==sgs.Friend_Male then
		for _,friend in sgs.list(friends)do
			if friend:isMale() then return true end
		end
	elseif prompt==sgs.Friend_MaleWounded then
		for _,friend in sgs.list(friends)do
			if friend:isMale() and friend:isWounded() then return true end
		end
	elseif prompt==sgs.Friend_Weak then
		for _,friend in sgs.list(friends)do
			if self:isWeak(friend) then return true end
		end
	end
	return prompt==sgs.Friend_All
end

function SmartAI:willSkipPlayPhase(player,NotContains_Null)
	player = player or self.player
	if player:isSkipped(sgs.Player_Play) then return true end
	local fhh = self.room:findPlayerBySkillName("noszhuikong")
	if fhh and fhh~=player and fhh:getHandcardNum()>1 and fhh:isWounded()
	and self:isEnemy(player,fhh) and not player:isKongcheng() and not self:isWeak(fhh) then
		local max_card = self:getMaxCard(fhh)
		local player_max_card = self:getMaxCard(player)
		if max_card and player_max_card and max_card:getNumber()>player_max_card:getNumber()
		or max_card and max_card:getNumber()>=12 then return true end
	end
	local friend_snatch_dismantlement,friend_null = 0,0
	if self.player:getPhase()<=sgs.Player_Play and self.player~=player and self:isFriend(player) then
		for _,h in sgs.qlist(self.player:getCards("he"))do
			if isCard("Snatch",h,self.player) and self.player:distanceTo(player)==1
			or isCard("Dismantlement",h,self.player) then
				local trick = dummyCard("dismantlement")
				trick:addSubcard(h)
				if self:hasTrickEffective(trick,player,self.player)
				then friend_snatch_dismantlement = friend_snatch_dismantlement+1 end
			end
		end
	end
	if NotContains_Null~=true then
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if self:isFriend(p,player) then friend_null = friend_null+getCardsNum("Nullification",p,self.player)
			elseif self:isEnemy(p,player) then friend_null = friend_null-getCardsNum("Nullification",p,self.player) end
		end
	end
	if player:containsTrick("indulgence") then
		if player:containsTrick("YanxiaoCard") or player:hasSkills("keji|conghui")
		or player:getHandcardNum()>0 and player:hasSkill("qiaobian") then return false end
		if friend_null+friend_snatch_dismantlement>1 then return false end
		local i,to = self:getFinalRetrial(player)
		if i==1 and self:getSuitNum("heart",true,to)>0
		then return false end
		return true
	end
	return false
end

function SmartAI:willSkipDrawPhase(player,NotContains_Null)
	player = player or self.player
	if player:getMark("&yizheng")+player:getMark("&zhibian")+player:getMark("kuanshi_skip")>0
	then return true end
	local friend_null,friend_snatch_dismantlement = 0,0
	if NotContains_Null~=true then
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if self:isFriend(p,player) then friend_null = friend_null+getCardsNum("Nullification",p,self.player)
			elseif self:isEnemy(p,player) then friend_null = friend_null-getCardsNum("Nullification",p,self.player) end
		end
	end
	if self.player:getPhase()<=sgs.Player_Play
	and self.player~=player and self:isFriend(player) then
		for _,h in sgs.qlist(self.player:getCards("he"))do
			if isCard("Snatch",h,self.player) and self.player:distanceTo(player)==1
			or isCard("Dismantlement",h,self.player) then
				local trick = dummyCard("dismantlement")
				trick:addSubcard(h)
				if self:hasTrickEffective(trick,player,self.player)
				then friend_snatch_dismantlement = friend_snatch_dismantlement+1 end
			end
		end
	end
	if player:containsTrick("supply_shortage") then
		if player:containsTrick("YanxiaoCard") or player:hasSkills("shensu|jisu")
		or player:getHandcardNum()>0 and player:hasSkill("qiaobian") then return false end
		if friend_null+friend_snatch_dismantlement>1 then return false end
		local i,to = self:getFinalRetrial(player)
		if i==1 and self:getSuitNum("club",true,to)>0
		then return false end
		return true
	end
	return false
end

function SmartAI:willSkipDiscardPhase(player)
	player = player or self.player
	--克己、巧变、界神速、奋励
	if player:getMark("&mobilexinyinju")>0
	or player:hasSkill("conghui")
	then return true end
	if player:hasSkill("xingzhao") then
		local wounded = 0
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if p:isWounded() then wounded = wounded+1 end
			if wounded>=3 then return true end
		end
	end
	return false
end

function hasBuquEffect(player)
	return player:getPile("buqu"):length()<=4 and player:hasSkill("buqu")
	or player:getPile("nosbuqu"):length()<=4 and player:hasSkill("nosbuqu")
end

function canNiepan(player)
	return player:getMark("@nirvana")>0 and player:hasSkill("niepan")
	or player:getMark("@mobileniepanMark")>0 and player:hasSkill("mobileniepan")
	or player:getMark("@olniepanMark")>0 and player:hasSkill("olniepan")
	or player:getMark("@laoji")>0 and player:hasSkill("fuli")
end

function SmartAI:adjustAIRole()
	sgs.explicit_renegade = false
	for _,ap in sgs.qlist(self.room:getAlivePlayers())do
		if ap:getRole()~="lord" then
			sgs.roleValue[ap:objectName()]["renegade"] = 0
			if ap:getRole()=="renegade" then sgs.explicit_renegade = true end
			if ap:getRole()=="rebel" then sgs.roleValue[ap:objectName()]["loaylist"] = -65535
			else sgs.roleValue[ap:objectName()][ap:getRole()] = 65535 end
			sgs.ai_role[ap:objectName()] = ap:getRole()
		end
	end
end

function hasWulingEffect(element)
	if not element:startsWith("@") then element = "@"..element end
	for _,p in sgs.list(global_room:getAlivePlayers())do
		if p:getMark(element)>0 and p:hasSkill("wuling")
		then return true end
	end
end

function hasTuntianEffect(to,need_zaoxian)
	if to:hasSkills("tuntian|mobiletuntian|oltuntian") and to:getPhase()==sgs.Player_NotActive
	then return not need_zaoxian or to:hasSkills("zaoxian|olzaoxian") end
end

function SmartAI:isValueSkill(skill_name,player,HighValue)
	player = player or self.player
	if string.find(sgs.bad_skills,skill_name) then return false end
	if (skill_name=="buqu" or skill_name=="nosbuqu") and hasBuquEffect(player) and (not HighValue or player:getHp()<=1) then return true end
	if not HighValue then
		local powerful_skills = {"zhiheng","tenyearzhiheng","jijiu"}	--待补充
		if table.contains(powerful_skills,skill_name) then return true end
	end
	local skill = sgs.Sanguosha:getSkill(skill_name)
	if not skill then return false end
	if skill:isLimitedSkill() and skill:getLimitMark()~="" and player:getMark(skill:getLimitMark())>0 then return true end
	if skill:getFrequency()==sgs.Skill_Wake and player:getMark(skill_name)==0 then
		if not HighValue then return true end
		if skill_name=="fengliang" then return true end
		if skill_name=="baiyin" and (player:getMark("&bear")>=4 or player:hasSkill("renjie")) then return true end
		if skill_name=="chuyuan" and (player:getPile("cychu"):length()>=3 or player:hasSkill("chuyuan")) then return true end
		if skill_name=="tianxing" and (player:getPile("cychu"):length()>=3 or player:hasSkill("chuyuan")) then return true end
		if skill_name=="baoling" and player:getMark("HengzhengUsed")>=1 then return true end
		if skill_name=="poshi" and (not player:hasequipArea() or player:getHp()==1) then return true end
		if (skill_name=="hongju" or skill_name=="olhongju" or skill_name=="mobilehongju")	--暂且不管需不需要有死亡角色了
		and (player:getPile("rong"):length()>=3 or player:hasSkills("zhengrong|olzhengrong|mobilezhengrong")) then return true end
		if (skill_name=="zhiji" or skill_name=="mobilezhiji" or skill_name=="olzhiji") and player:isKongcheng() then return true end
		if skill_name=="mobilehunzi" and player:getHp()<=2 then return true end
		if skill_name=="hunzi" and player:getHp()==1 then return true end
		if skill_name=="mobilechengzhang" and player:getMark("&mobilechengzhang")+player:getMark("mobilechengzhang_num")>=7 then return true end
		if (skill_name=="zaoxian" or skill_name=="olzaoxian")
		and (player:getPile("field"):length()>=3 or player:hasSkills("tuntian|mobiletuntian|oltuntian")) then return true end
		if (skill_name=="ruoyu" or skill_name=="olruoyu") and player:isLowestHpPlayer() then return true end
		if skill_name=="nosbaijiang" and player:getEquips():length()>=3 then return true end
		if skill_name=="noszili" and (player:getPile("nospower"):length()>=3 or player:hasSkill("nosyexin")) then return true end
		if skill_name=="zili" and (player:getPile("power"):length()>=3 or player:hasSkills("quanji|mobilequanji")) then return true end
		if skill_name=="zhiri" and (player:getPile("burn"):length()>=3 or player:hasSkill("fentian")) then return true end
		if skill_name=="oljixi" and player:getMark("oljixi_turn")>=2 then return true end
		if (skill_name=="wuji" or skill_name=="newwuji") and player:getMark("damage_point_round")>=3 then return true end
		if skill_name=="juyi" and player:isWounded() and player:getMaxHp()>player:aliveCount() then return true end
		if skill_name=="choujue" and math.abs(player:getHandcardNum()-player:getHp())>=3 then return true end
		if skill_name=="beishui" and (player:getHandcardNum()<2 or player:getHp()<2) then return true end
		if skill_name=="longyuan" and player:getMark("&yizan")>=3 then return true end
		if skill_name=="zhanyuan" and player:getMark("&zhanyuan_num")+player:getMark("zhanyuan_num")>7 then return true end
		if skill_name=="secondzhanyuan" and player:getMark("&secondzhanyuan_num")+player:getMark("secondzhanyuan_num")>7 then return true end
		if skill_name=="baijia" and player:getMark("&baijia_num")+player:getMark("baijia_num")>=7 then return true end
		if skill_name=="diaoling" and player:getMark("&mubing")+player:getMark("mubing_num")>=6 then return true end
		if skill_name=="shanli" and player:getTag("BaiyiUsed"):toBool() and #player:getTag("Jinglve_targets"):toStringList()>=2 then return true end
		if skill_name=="zhanshen" and player:isWounded() and (player:getMark("zhanshen_fight")>0 or player:getMark("@fight")>0) then return true end
		if skill_name=="qianxin" and player:isWounded() then return true end
		if skill_name=="qinxue" then
			n = player:getHandcardNum()-player:getHp()
			if sgs.Sanguosha:getPlayerCount(sgs.getMode)>=7 and n>=2
			or n>=3 then return true end
		end
		if skill_name=="jiehuo" and player:getMark("@shouye")>=7 then return true end
		if skill_name=="kegou" and player:getKingdom()=="wu" then
			for _,p in sgs.list(player:getAliveSiblings())do
				if p:getKingdom()=="wu" and not p:getRole()=="lord" then return false end
			end
			return true
		end
		if skill_name=="fanxiang" then
			for _,p in sgs.list(self.room:getAlivePlayers())do
				if p:getTag("liangzhu_draw"..player:objectName()):toBool() and p:isWounded() then return true end
			end
		end
		if skill_name=="jspdanqi" and player:getHandcardNum()>player:getHp() and self.room:getLord()~=nil and not string.find(self.room:getLord():getGeneralName(),"liubei")
		and (not self.room:getLord():getGeneral2() or not string.find(self.room:getLord():getGeneral2Name(),"liubei")) then return true end
		if skill_name=="danji" and player:getHandcardNum()>player:getHp() and self.room:getLord()~=nil and not string.find(self.room:getLord():getGeneralName(),"caocao")
		and (not self.room:getLord():getGeneral2() or not string.find(self.room:getLord():getGeneral2Name(),"caocao")) then return true end
		if skill_name=="jinsanchen" and (player:getMark("&jinsanchen")>=3 or player:hasSkill("jinsanchen")) then return true end
		if skill_name=="mobilezhisanchen" and (player:getMark("&mobilezhiwuku")>=3 or player:hasSkill("mobilezhiwuku")) then return true end
		if skill_name=="zongfan" and player:getMark("tunjiang_skip_play-Clear")<=0 and player:getMark("mouni-Clear")>0 then return true end
		if skill_name=="tenyearmoucheng" and player:getMark("tenyearlianji_choice_1")>0 and player:getMark("tenyearlianji_choice_2")>0 then return true end
		if skill_name=="olmoucheng" and (player:getMark("&ollianji")>=3 or player:hasSkill("ollianji")) then return true end
		if skill_name=="secondolmoucheng" and player:getMark("&ollianjidamage")>0 then return true end
		if skill_name=="mobilemoucheng" and player:getMark("&mobilelianji")>2 then return true end
	end
	return false
end

function SmartAI:needToThrowCard(to,flags,dis,give,draw)
	to = to or self.player
	flags = flags or "he"
	if not give and not draw then dis = true end
	if flags:contains("h") and not to:isKongcheng() then
		if not self:hasLoseHandcardEffective(to) and not dis
		or (dis or give) and self:needKongcheng(to,false,true)
		or draw and to:hasSkill("lirang") and self:findFriendsByType(sgs.Friend_Draw,to)
		or draw and to:hasSkill("shangjian") and to:getMark("shangjian-Clear")<to:getHp()
		or hasTuntianEffect(to)
		then return true end
	end
	if flags:contains("e") and to:hasEquip()
	and not(to:getEquips():length()==1 and self:keepWoodenOx(to)) then
		if self:loseEquipEffect(to) and (to:getOffensiveHorse() or to:getWeapon())
		or draw and to:hasSkill("shangjian") and to:getMark("shangjian_lose_card_num-Clear")<to:getHp() and (to:getOffensiveHorse() or to:getWeapon())
		or not dis and to:hasArmorEffect("SilverLion") and to:isWounded() and not self:needToLoseHp(to,self.player,false,true,true) and not to:hasSkill("dingpan")
		or draw and to:hasSkill("shanjia") and to:getMark("&shanjia")<3
		or self:needToThrowArmor(to)
		or hasTuntianEffect(to)
		then return true end
	end
	if flags:contains("j") then
		if (to:containsTrick("indulgence") or to:containsTrick("supply_shortage")) and not to:containsTrick("YanxiaoCard") then return true end
	end
	return false
end

function SmartAI:keepCard(c,p,disPeach,asPeach)
	if not c then return true end
	p = p or self.player
	if c:isKindOf("WoodenOx")
	and p:getPile("wooden_ox"):length()>0 then 
		if asPeach then 
			for _,id in sgs.list(p:getPile("wooden_ox"))do
				if isCard("Peach",id,p) then return true end
			end
			return false
		end
		return true 
	end
	if not disPeach and c:isKindOf("Peach") then return true end
	return false
end

function SmartAI:keepWoodenOx(p,flag)
	p = p or self.player
	if not p:hasTreasure("wooden_ox") or p:getPile("wooden_ox"):length()<1 then return false end
	return p:getCards(flag or "e"):length()==1
end

function SmartAI:hasZhenguEffect(to,from)
	if not to or not to:hasSkill("zhengu") then return false end
	if to:getHp()<2 and to:getHandcardNum()<2 and self:isWeak(to)
	then return false end
	from = from or self.player
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if p:getMark("&zhengu")>0 and self:isEnemy(p,from) and not self:needKongcheng(p,true)
		then return true end
	end
	return false
end

function SmartAI:needDraw(to,notDraw)
	if not to then return false end
	if to:hasSkills("manjuan|zishu") and to:getPhase()~=sgs.Player_NotActive then return true end
	if not notDraw and to:hasSkill("zhanji") and to:getPhase()==sgs.Player_Play then return true end
	if to:getHandcardNum()<5 and to:hasSkill("zhengu") then
		for _,p in sgs.list(self.room:getAlivePlayers())do
			if p:getMark("&zhengu")>0 and self:isFriend(p,to)
			then return true end
		end
	end
	return false
end

function SmartAI:hasJieyingyEffect(player)
	player = player or self.player
	if player:getMark("&jygying")>0 and not player:hasSkill("jieyingg") then
		for _,shenganning in sgs.list(self.room:findPlayersBySkillName("jieyingg"))do
			if not self:isFriend(shenganning,player) then return true end
		end
	end
	for _,gexuan in sgs.list(self.room:findPlayersBySkillName("zhafu"))do
		if table.contains(player:property("zhafu_from"):toStringList(),gexuan:objectName()) then
			if not self:isFriend(gexuan,player)
			then return true end
		end
	end
	return false
end

function SmartAI:canDraw(to,from)
	to = to or self.player
	if self:needKongcheng(to,true) or hasManjuanEffect(to)
	or self:hasJieyingyEffect(to) and to:getPhase()>4 and to:getPhase()<7
	then return false end
	from = from or self.player
	return not self:hasZhenguEffect(to,from)
end

function SmartAI:noChoice(targets,reason)
	if reason=="damage" then
		for _,p in sgs.list(targets)do
			if self:needToLoseHp(p,self.player,nil,true)
			or sgs.ai_role[p:objectName()]=="neutral" then continue end
			if self:damageIsEffective(p,nil,self.player)
			then sgs.updateIntention(self.player,p,-10) end
		end
	elseif reason=="change" then
		for _,p in sgs.list(targets)do
			if hasManjuanEffect(p) or p:isNude()
			or sgs.ai_role[p:objectName()]=="neutral"
			then continue end
			sgs.updateIntention(self.player,p,10)
		end
	elseif reason=="discard" then
		for _,p in sgs.list(targets)do
			if self:doDisCard(p)
			or sgs.ai_role[p:objectName()]=="neutral"
			then continue end
			sgs.updateIntention(self.player,p,-10)
		end
	elseif reason=="letDis" then
		for _,p in sgs.list(targets)do
			if self:needToThrowCard(p) or p:isNude()
			or sgs.ai_role[p:objectName()]=="neutral"
			then continue end
			sgs.updateIntention(self.player,p,-10)
		end
	else
		for _,p in sgs.list(targets)do
			if sgs.ai_role[p:objectName()]=="neutral"
			or not self:canDraw(p)
			then continue end
			sgs.updateIntention(self.player,p,10)
		end
	end
end

function SmartAI:canDamage(to,from,slash)
	from = from or self.room:getCurrent() or self.player
	to = to or self.player
	if not self:damageIsEffective(to,slash,from) then return false
	elseif self:isEnemy(to) then return not(self:needToLoseHp(to,from,slash,true) or self:isFriend(from) and self:cantbeHurt(to,from))
	elseif self:isFriend(to) then return self:needToLoseHp(to,from,slash,true) else return true end
end

function SmartAI:findPlayerToLoseHp(must)
	local second
	self:sort(self.enemies,"hp")
	for _,enemy in sgs.list(self.enemies)do
		if not hasZhaxiangEffect(enemy) and not self:needToLoseHp(enemy,self.player,false,true) then return enemy end
		if must and not second then second = enemy end
	end
	if must then
		self:sort(self.friends_noself,"hp",true)
		for _,friend in sgs.list(self.friends_noself)do
			if hasZhaxiangEffect(friend) and not self:isWeak(friend)
			or self:needToLoseHp(friend,self.player,false,true)
			then return friend end
		end
	end
	return second
end

function SmartAI:disEquip(hand_only,equip_only)
	if self:needToThrowArmor() then
		return self.player:getArmor():getEffectiveId()
	end
	if hand_only and equip_only then return -1 end
	local cards = self.player:getCards("he")
	if hand_only then cards = self.player:getHandcards() end
	if equip_only then cards = self.player:getEquips() end
	local equips = {}
	for _,c in sgs.list(cards)do
		if c:isKindOf("EquipCard") then
			if c:isKindOf("WoodenOx") and self.player:getPile("wooden_ox"):length()>0 then continue end
			table.insert(equips,c)
		end
	end
	if #equips<1 then return end
	self:sortByKeepValue(equips)
	return equips[1]:getEffectiveId()
end

function hasOrangeEffect(player)
	return global_room:findPlayerBySkillName("huaiju") and player:getMark("&orange")>0
end

function SmartAI:cantDamageMore(from,to)
	from = from or self.room:getCurrent() or self.player
	to = to or self.player
	if not hasJueqingEffect(from,to) then
		if to:hasArmorEffect("SilverLion") and not IgnoreArmor(from,to)
		or to:hasSkill("gongqing") and from:getAttackRange()<3
		or hasOrangeEffect(to)
		then return true end
		local jiaren_zidan = self.room:findPlayerBySkillName("jgchiying")
		if jiaren_zidan and jiaren_zidan:getRole()==to:getRole()
		then return true end
		if getSpecialMark("&tiansuan4",to)+getSpecialMark("&tiansuan5",to)<1 then
			if getSpecialMark("&tiansuan2",to)>0 or getSpecialMark("&tiansuan3",to)>0
			then return true end --受到火焰伤害会加伤待补充
		end
	end
	return false
end

function canJiaozi(player)
	if player:hasSkill("jiaozi") then
		for _,p in sgs.qlist(player:getAliveSiblings())do
			if p:getHandcardNum()>=player:getHandcardNum() then return end
		end
		return true
	end
end

function beFriend(to,from)
	if not(from and to) then return false end
	if from:objectName()==to:objectName() then return true end
	if from:getRole()=="lord" or from:getRole()=="loyalist" then
		if sgs.turncount<=1 and to:getRole()=="renegade" and from:aliveCount()>7
		or to:getRole()=="lord" or to:getRole()=="loyalist"
		then return true end
	end	
	if from:getRole()=="rebel" and to:getRole()=="rebel" then return true end
	if from:getRole()=="renegade" then
		if sgs.turncount<=1 and to:getRole()=="loyalist" and from:aliveCount()>7
		or to:getRole()=="lord" and from:aliveCount()>2
		then return true end
	end
	return false
end

sgs.ai_ajustdamage_to = {}
sgs.ai_ajustdamage_from = {}

function SmartAI:ajustDamage(from,to,dmg,card,nature)
	from = from or self.room:getCurrent() or self.player
	to = to or self.player
	dmg = dmg or 1
	if hasJueqingEffect(from,to) then return -dmg end
	if self:cantDamageMore(from,to) then return 1 end
	if getSpecialMark("&tiansuan1",to)>0 then return 0 end
	if type(nature)~="string" then
		if type(nature)~="number" then
			nature = card and sgs.card_damage_nature[card:getClassName()] or sgs.DamageStruct_Normal
		end
		local na = {
			[sgs.DamageStruct_Normal]="N",
			[sgs.DamageStruct_Fire]="F",
			[sgs.DamageStruct_Thunder]="T",
			[sgs.DamageStruct_Ice]="I",
			[sgs.DamageStruct_Poison]="P",
			[sgs.DamageStruct_God]="G",
		}
		nature = na[nature] or "N"
	end
	if nature=="F" or hasWulingEffect("@fire") then
		if hasWulingEffect("@wind") then dmg = dmg+1 end
		if hasWulingEffect("@earth") and not to:hasSkill("ranshang") then return 1 end
		nature = "F"
	end
	if to:getMark("&shouli_debuff-Clear")>0 then
		dmg = dmg+to:getMark("&shouli_debuff-Clear")
		nature = "T"
	end
	self.to = to
	self.from = from
	self.card = card
	self.nature = nature
	for _,s in ipairs(aiConnect(from))do
		local ad = sgs.ai_ajustdamage_from[s]
		if type(ad)=="function" then
			ad = ad(self,from,to,card,nature)
			if type(ad)=="number" then dmg = dmg+ad end
		end
	end
	if nature=="T" then
		if hasWulingEffect("@earth") then return 1 end
		if hasWulingEffect("@thunder") then dmg = dmg+1 end
	end
	if canJiaozi(from) then dmg = dmg+1 end
	if canJiaozi(to) then dmg = dmg+1 end
	if card then
		if card:isKindOf("Duel") then
			if from:hasFlag("luoyi") then dmg = dmg+1 end
			if from:hasFlag("neoluoyi") then dmg = dmg+1 end
		elseif card:isKindOf("Slash") then
			if card:hasFlag("drank") then dmg = dmg+math.max(1,card:getTag("drank"):toInt()) end
			if from:hasFlag("nosluoyi") then dmg = dmg+1 end
			if from:hasFlag("neoluoyi") then dmg = dmg+1 end
			dmg = dmg+from:getMark("&olpaoxiao_missed-Clear")+to:getMark("&yise")
			if card:hasFlag("yj_nvzhuang_debuff") then dmg = dmg+1 end
			local guanyu = self.room:findPlayerBySkillName("zhongyi")
			if guanyu and guanyu:getPile("loyal"):length()>0 and self:isFriend(guanyu,from) then dmg = dmg+1 end
		elseif card:isKindOf("AOE") and card:hasFlag("hanyong") then dmg = dmg+1 end
		if card:hasFlag("yb_canqu1_add_damage") then dmg = dmg+1 end
		if card:hasFlag("cuijinAddDamage_1") then dmg = dmg+1 end
		if card:hasFlag("cuijinAddDamage_2") then dmg = dmg+2 end
	end
	dmg = dmg+getSpecialMark("&tiansuan4",to)+getSpecialMark("&tiansuan5",to)
	for _,s in ipairs(aiConnect(to))do
		local ad = sgs.ai_ajustdamage_to[s]
		if type(ad)=="function" then
			ad = ad(self,from,to,card,nature)
			if type(ad)=="number" then dmg = dmg+ad end
		end
	end
	return dmg<-10 and 0 or dmg
end

function SmartAI:hasHeavyDamage(from,card,to,nature)
	return self:ajustDamage(from,to,1,card,nature)>1
end

function hasYinshiEffect(to,hasArmor)
	if hasArmor then return to:getMark("&dragon_signet")+to:getMark("&phoenix_signet")<1 and to:hasSkill("yinshi") end
	return to:getMark("&dragon_signet")+to:getMark("&phoenix_signet")<1 and not to:getArmor() and to:hasSkill("yinshi")
end

function hasJueqingEffect(from,to)
	if from and from:hasSkills("jueqing|gangzhi") then return true end
	if from and from:hasSkill("tenyearjueqing") and from:getMark("tenyearjueqing")>0 then return true end
	if to and to:hasSkill("gangzhi") then return true end
	return false
end

function hasZhaxiangEffect(to)
	if to:hasSkill("zhaxiang") then return true end
end

function SmartAI:hasGuanxingEffect(owner)
	owner = owner or self.player
	if owner:aliveCount()>3 and owner:hasSkills("guanxing|tenyearguanxing")
	or owner:getCardCount()>2 and owner:hasSkill("zhiming")
	then return true end
end

function SmartAI:throwEquipArea(choices,player)
	player = player or self.player
	local items = choices:split("+")
	if self:isFriend(player) then--友方有sgs.lose_equip_skill待补充
		if self:needToThrowArmor(player) and player:hasEquipArea(1) and table.contains(items,"1")
		then return "1"
		elseif player:hasEquipArea(4) and not player:getTreasure() and table.contains(items,"4")
		then return "4"
		elseif player:hasEquipArea(1) and not player:getArmor() and table.contains(items,"1")
		then return "1"	
		elseif player:hasEquipArea(0) and not player:getWeapon() and table.contains(items,"0")
		then return "0"
		elseif player:hasEquipArea(3) and not player:getOffensiveHorse() and table.contains(items,"3")
		then return "3"	
		elseif player:hasEquipArea(2) and not player:getDefensiveHorse() and table.contains(items,"2")
		then return "2"
		elseif player:hasEquipArea(4) and not self:keepWoodenOx(player) and table.contains(items,"4")
		then return "4"
		elseif player:hasEquipArea(1) and table.contains(items,"1")
		then return "1"
		elseif player:hasEquipArea(0) and table.contains(items,"0")
		then return "0"
		elseif player:hasEquipArea(3) and table.contains(items,"3")
		then return "3"
		elseif player:hasEquipArea(2) and table.contains(items,"2")
		then return "2"
		else
			return items[1]
		end
	else
		--待补充
	end
	return items[#items]
end

function SmartAI:moveField(player,flag,froms,tos)
	froms = froms or self.room:getAlivePlayers()
	tos = tos or self.room:getAlivePlayers()
	player = player or self.player
	flag = flag or "ej"
	local from_friends,from_enemies,to_friends,to_enemies = {},{},{},{}
	for _,p in sgs.list(froms)do
		if self:isFriend(p) then table.insert(from_friends,p)
		else table.insert(from_enemies,p) end
	end
	for _,p in sgs.list(tos)do
		if self:isFriend(p) then table.insert(to_friends,p)
		else table.insert(to_enemies,p) end
	end
	local from_friends_noself = {}
	for _,p in sgs.list(from_friends)do
		if p:objectName()==player:objectName() then continue end
		table.insert(from_friends_noself,p)
	end
	self:sort(from_enemies)
	self:sort(from_friends)
	self:sort(from_friends_noself)
	if flag:contains("j") then
		for _,friend in sgs.list(from_friends)do
			if friend:getJudgingArea():length()>0
			and not friend:containsTrick("YanxiaoCard") then
				local to,c = self:card_for_qiaobian(friend,flag,to_friends,to_enemies)
				if to and c then return friend,c,to end
			end
		end
		for _,enemy in sgs.list(from_enemies)do
			if enemy:getJudgingArea():length()>0
			and enemy:containsTrick("YanxiaoCard") then
				local to,c = self:card_for_qiaobian(enemy,flag,to_friends,to_enemies)
				if to and c then return enemy,c,to end
			end
		end
	end
	if flag:contains("e") then
		for _,friend in sgs.list(from_friends_noself)do
			if friend:hasEquip() and self:loseEquipEffect(friend) then
				local to,c = self:card_for_qiaobian(friend,flag,to_friends,to_enemies)
				if to and c then return friend,c,to end
			end
		end
		local targets = {}
		for _,enemy in sgs.list(self.enemies)do
			if not self:loseEquipEffect(enemy)
			and self:card_for_qiaobian(enemy,flag,to_friends,to_enemies)
			then table.insert(targets,enemy) end
		end
		if #targets>0 then
			self:sort(targets)
			local to,c = self:card_for_qiaobian(targets[1],flag,to_friends,to_enemies)
			if to and c then return targets[1],c,to end
		end
	end
end

function SmartAI:dontHurt(to,from)	--针对队友
	if hasJueqingEffect(from,to)
	or hasOrangeEffect(to)
	then return true end
	if to:hasSkills("sizhan|lixun")
	then return true end
	return false
end

function SmartAI:justDamage(to,from,isSlash,lock)	--针对敌人
	if from:hasFlag("NosJiefanUsed") then return true end
	if self:isFriend(to,from) then return false end
	if self:dontHurt(to,from) then return true end
	if from:hasSkill("nosdanshou") then return true end
	if #from:property("duorui_skills"):toStringList()<1 and from:hasSkill("duorui") then return true end
	if from:hasSkill("olduorui") then return true end
	if to:getHp()<=2 and to:hasSkill("chanyuan") then return true end
	if isSlash and from:hasSkill("chuanxin") then
		if to:getEquips():length()>0 and not hasZhaxiangEffect(to)
		and self:doDisCard(to,"e") then return true end
		if to:getMark("@chuanxin")<1 and not to:hasSkills(sgs.bad_skills) then
			if to:getVisibleSkillList():length()>1 then return true end
		end		
	end
	if from:hasSkills("zhiman|tenyearzhiman")
	and self:doDisCard(to,"e") then return true end
	if isSlash then
		if not lock and from:hasSkill("tieji") then return true end
		if from:hasSkill("nosqianxi") and from:distanceTo(to)==1
		then return true end
		if from:hasWeapon("IceSword") and self:doDisCard(to,"he")
		then return true end
	end
	return false
end

function SmartAI:goodJudge(p,reason,c)
	if not reason then return false end
	if p:hasSkills("qianxi|olqianxi") and not self:isFriend(p) then return false end
	c = CardFilter(c,p)
	if reason=="lightning" and not p:hasSkills("wuyan") then
		if self:isFriend(p) then return not (c:getSuit()==sgs.Card_Spade and c:getNumber()>=2 and c:getNumber()<=9)
		elseif self:isEnemy(p) then return c:getSuit()==sgs.Card_Spade and c:getNumber()>=2 and c:getNumber()<=9 end
	elseif reason=="indulgence" then
		if self:isFriend(p) then return c:getSuit()==sgs.Card_Heart
		elseif self:isEnemy(p) then return c:getSuit()~=sgs.Card_Heart end
	elseif reason=="supply_shortage" then
		if self:isFriend(p) then return c:getSuit()==sgs.Card_Club			
		elseif self:isEnemy(p) then return c:getSuit()~=sgs.Card_Club end
	elseif reason=="black" then
		if self:isFriend(p) then return c:isBlack()
		elseif self:isEnemy(p) then return c:isRed() end
	end
	return false
end

function SmartAI:canUse(card,players,from)
	players = players or self.room:getAlivePlayers()
	from = from or self.player
	if type(players)=="table" then
		local new_players = sgs.SPlayerList()
		for _,p in sgs.list(players)do
			new_players:append(p)
		end
		players = new_players
	end
	if players:isEmpty() then return false end
	return from:canUse(card,players)
end

function SmartAI:willUse(player,card,ignoreDistance,disWeapon,play)
	from = player or self.player
	if play then
		if self:aiUseCard(card).card
		then return true end
	else
		if card:isKindOf("Slash") then
			for _,to in sgs.list(self.enemies)do
				local far = false
				if from:hasSkill("tenyearliegong") or from:hasFlag("TianyiSuccess")
				or from:hasFlag("JiangchiInvoke") or from:hasFlag("InfinityAttackRange")
				or from:getMark("InfinityAttackRange")>0 then far = true end
				if disWeapon then
					if from:distanceTo(to)>1 and not far
					or to:hasArmorEffect("renwang_shield") and card:isBlack()
					or to:hasArmorEffect("Vine") and not card:isKindOf("NatureSlash")
					then continue end
				end
				if (from:canSlash(to,card,not ignoreDistance) or far)
				and self:slashIsEffective(card,to,from) and self:isGoodTarget(to,self.enemies,card) 
				and not self:slashProhibit(card,to,from)
				then return true end
			end
		elseif card:isKindOf("SupplyShortage") then
			if from:hasSkill("jizhi") then ignoreDistance = true end
			for _,to in sgs.list(self.enemies)do
				if not ignoreDistance and from:distanceTo(to)>1
				or to:containsTrick("supply_shortage")
				or to:containsTrick("YanxiaoCard")
				then continue end
				if self.room:isProhibited(from,to,card)
				then else return true end
			end
		elseif card:isKindOf("Snatch") then
			if from:hasSkill("jizhi") then ignoreDistance = true end
			for _,to in sgs.list(self.enemies)do
				if not ignoreDistance and from:distanceTo(to)>1 then continue end
				if self:hasTrickEffective(card,to,from) and self:doDisCard(to) then return true end
			end
		end
		if card:getTypeId()>1 and from:objectName()==self.player:objectName()
		and self:aiUseCard(card).card then return true end
	end
	return false
end

function SmartAI:GetAskForPeachActionOrderSeat(player)
	local another_seat,nextAlive = {},self.room:getCurrent()
	player = player or self.player
	for i=1,self.room:alivePlayerCount()do
		table.insert(another_seat,nextAlive)
		nextAlive = nextAlive:getNextAlive()
	end
	for i=1,#another_seat do
		if another_seat[i]:objectName()==player:objectName()
		then return i end
	end
	return -1
end

function ZishuEffect(player)
	local n = 0
	if player:getPhase()==sgs.Player_Play and player:hasSkill("zhanji") then n = n+1 end
	if player:getPhase()~=sgs.Player_NotActive and player:hasSkill("zishu") then n = n+1 end
	return n
end

function canAiSkills(name)
	if type(name)=="string" then
		for fname,fs in pairs(sgs.ai_fill_skill)do
			if fname==name then return {name=fname,ai_fill_skill=fs} end
		end
	else
		local cas,bp = {},sgs.Sanguosha:getBanPackages()
		for _,g in sgs.list(sgs.Sanguosha:getAllGenerals())do
			if table.contains(bp,g:getPackage()) or g:isHidden()
			or g:isTotallyHidden() then continue end
			for _,s in sgs.list(g:getSkillList())do
				local fs = {name=s:objectName()}
				fs.ai_fill_skill = sgs.ai_fill_skill[s:objectName()]
				if fs.ai_fill_skill then table.insert(cas,fs) end
			end
		end
		return cas
	end
end

sgs.ai_can_damagehp = {}

function SmartAI:canDamageHp(from,card,to)
	to = to or self.player
	for _,s in ipairs(aiConnect(to))do
		local d = sgs.ai_can_damagehp[s]
		if type(d)=="function" then
			d = d(self,from,card,to)
			if d then return true
			elseif d~=nil then return end
		end
	end
	if to:inYinniState() and to:objectName()==self.player:objectName() then
		local general = sgs.Sanguosha:getGeneral(to:property("yinni_general"):toString())
		if general then
			for _,s in sgs.list(general:getSkillList())do
				local d = sgs.ai_can_damagehp[s:objectName()]
				if type(d)=="function" then
					d = d(self,from,card,self.player)
					if d then return true
					elseif d~=nil then return end
				end
			end
		end
		general = sgs.Sanguosha:getGeneral(to:property("yinni_general2"):toString())
		if general then
			for _,s in sgs.list(general:getSkillList())do
				local d = sgs.ai_can_damagehp[s:objectName()]
				if type(d)=="function" then
					d = d(self,from,card,self.player)
					if d then return true
					elseif d~=nil then return end
				end
			end
		end
	end
	for _,m in sgs.list(to:getMarkNames())do
		if m:startsWith("canDamage") and to:getMark(m)>0 then
			if m:startsWith("canDamage_") then
				if from and m:endsWith(from:objectName())
				then else continue end
			end
			return self:canLoseHp(from,card,to)
		end
	end
end

function SmartAI:canLoseHp(from,card,to)
	from = from or self.room:getCurrent() or self.player
	to = to or self.player
	local n = self:ajustDamage(from,to,1,card)
	if n<0 then return end
	if not self:isFriend(to,from) then
		if from:getMark("jinyimie-Clear")<1
		and from:hasSkill("jinyimie")
		then return end
		if from:getHp()<=to:getHp()
		and from:getHandcardNum()>0
		and from:hasSkill("jieyuan")
		then return end
		if self:isWeak(to) then
			if from:getMark("@flyuqing")>0
			and from:hasSkill("zhenyi")
			then return end
			if from:hasSkill("pojun")
			then return end
		end
		if from:getMark("jiedao-Clear")>0
		and from:getLostHp()>=to:getHp()
		and from:hasSkill("jiedao")
		then return end
		if to:getJudgingArea():isEmpty()
		and from:hasSkill("shanzhuan")
		then return end
		if #from:property("duorui_skills"):toStringList()<1
		and from:hasSkill("duorui")
		then return end
		if from:hasSkill("nosdanshou")
--		and self:isWeak(to)
		then return end
		if from:hasSkill("chuanxin")
		then return end
		if from:distanceTo(to)==1 then
			if card and card:isKindOf("Slash")
			and from:hasSkill("nosqianxi")
			then return end
			if from:hasSkill("kuanggu")
			then return end
		end
	end
	if self:isWeak(to) then
		if to:getMark("@brutal")>0
		and from:hasSkill("xionghuo")
		then return end
		if from:hasSkill("zhuixi")
		and (from:faceUp() and not to:faceUp() or to:faceUp() and not from:faceUp())
		then return end
	end
	if n>=to:getHp() then return end
	if card and card:isKindOf("Slash")
	and from:getMark("&kannan")>=to:getHp()
	then return end
	if from:hasSkill("jiaozi")
	and from:getHandcardNum()>to:getHandcardNum()
	then return end
	if from:getPhase()~=sgs.Player_NotActive
	and from:hasSkill("ov_equan")
	then return end
	return true
end

sgs.ai_poison_card.shit = true

function SmartAI:poisonCards(flags,owner)
	owner = owner or self.player
	local cards = {}
	flags = flags or "h"
	if type(flags)=="string" then flags = owner:getCards(flags) end
	for _,cid in sgs.list(flags)do
		local c,id = cid,cid
		if type(cid)=="number" then c = sgs.Sanguosha:getCard(cid)
		else id = cid:getEffectiveId() end
		if owner:handCards():contains(id) then
			local has = false
			for _,kc in ipairs(getKnownCards(owner,self.player))do
				has = kc:getId()==id
				if has then break end
			end
			if has==false then continue end
		end
		local ap = sgs.ai_poison_card[c:objectName()]
		if ap==true or type(ap)=="function" and ap(self,c,owner)
		or owner:getEquipsId():contains(id) and self:evaluateArmor(c,owner)<-5
		or owner:getJudgingAreaID():contains(id) and not(owner:containsTrick("YanxiaoCard") or c:isKindOf("Xumou"))
		then table.insert(cards,c) end
	end
	return cards
end

function addAiSkills(sk)
	local ai_sk = {}
	ai_sk.name=sk
	table.insert(sgs.ai_skills,ai_sk)
	return ai_sk
end

function SmartAI:useCardByClassName(card,use)
	local usefunc = self["useCard"..card:getClassName()]
	if usefunc then
		self.aiUsing = card:getSubcards()
		return usefunc(self,card,use)
	end
end

function SmartAI:targetRevises(use)
	if use.card:getTypeId()==3 then return end
	local tos = {}
	if use.card:isKindOf("AOE") then
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if self.player~=p and not self.player:isProhibited(p,use.card) then table.insert(tos,p) end
		end
	elseif use.card:isKindOf("GlobalEffect") then
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if not self.player:isProhibited(p,use.card) then table.insert(tos,p) end
		end
	elseif use.to:length()>0 then
		for _,p in sgs.qlist(use.to)do
			table.insert(tos,p)
		end
	elseif use.card:targetFixed()
	then tos = {self.player} end
	local utr,uc = {},use.card
	for i,to in ipairs(tos)do
		local tx = use.to:length()>1
		if tx and use.card:isKindOf("SingleTargetTrick")
		and use.to:length()>1+sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,use.card,to) then
			if i%2~=1 then continue end
			tx = use.to:length()>2
		end
		if tx and use.card:isKindOf("TrickCard") and (not sgs.ai_humanized or math.random()<0.95)
		and self:isEnemy(to) and self:hasHuangenEffect(to) then table.insert(utr,to)
		else
			tx = use.card:hasFlag("Qinggang") or not to:hasArmorEffect(nil)
			for _,ac in ipairs(aiConnect(to))do
				if tx and sgs.armorName[ac] then continue end
				local tr = sgs.ai_target_revises[ac]
				if type(tr)=="function" and tr(to,use.card,self,use)
				and (not sgs.ai_humanized or math.random()<0.95)
				then table.insert(utr,to) break end
				if use.card~=uc then return end
			end
		end
	end
	if #utr<1 then return end
	for _,p in ipairs(utr)do
		p:setProperty("aiNoTo",sgs.QVariant(true))
	end
	use.card = nil
	use.to = sgs.SPlayerList()
	self.aiUsing = uc:getSubcards()
	self["use"..sgs.ai_type_name[uc:getTypeId()+1]](self,uc,use)
	for _,p in ipairs(utr)do
		p:setProperty("aiNoTo",sgs.QVariant(false))
	end
end

function SmartAI:aiUseCard(card,use)
	if type(card)~="userdata" then global_room:writeToConsole(debug.traceback()) return end
	use = use or dummy()
	if card:getTypeId()<1 then
		self:useSkillCard(card,use)
		return use
	end
	local ai_connect = aiConnect(self.player)
	for _,ac in ipairs(ai_connect)do
		local invoke = sgs.ai_use_revises[ac]
		if type(invoke)=="function"	then
			invoke = invoke(self,card,use)
			if type(invoke)=="boolean" then
				if invoke then
					if use.card then break end
					self:useCardByClassName(card,use)
				else return use end
			end
		end
	end
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		for _,ac in sgs.list(aiConnect(p))do
			local invoke = sgs.ai_useto_revises[ac]
			if type(invoke)=="function" then
				invoke = invoke(self,card,use,p)
				if type(invoke)=="boolean" then
					if invoke then
						if use.card then break end
						self:useCardByClassName(card,use)
					else return use end
				end
			end
		end
		for _,j in sgs.qlist(p:getJudgingArea())do
			local invoke = sgs.ai_useto_revises[j:objectName()]
			if type(invoke)=="function" then
				invoke = invoke(self,card,use,p)
				if type(invoke)=="boolean" then
					if invoke then
						if use.card then break end
						self:useCardByClassName(card,use)
					else return use end
				end
			end
		end
	end
	if not use.card then
		self.aiUsing = card:getSubcards()
		self["use"..sgs.ai_type_name[card:getTypeId()+1]](self,card,use)
	end
	if use.card then
		self:targetRevises(use)
		if use.card and not use.isDummy then
			for _,ac in ipairs(ai_connect)do
				local tr = sgs.ai_used_revises[ac]
				if type(tr)=="function" and (not sgs.ai_humanized or math.random()<0.95)
				then tr(self,use) end
			end
		end
	end
	return use
end

function SmartAI:getGuhuoCard(class_name)
	local ghs = {}
	local card_name = patterns(class_name)
	for _,s in ipairs(sgs.getPlayerSkillList(self.player))do
		local gc = sgs.ai_guhuo_card[s:objectName()]
		if type(gc)=="function" then
			local vs = sgs.Sanguosha:getViewAsSkill(s:objectName())
			if vs==nil then continue end
			if (card_name=="peach" or card_name=="analeptic")
			and self.room:getCurrentDyingPlayer()==self.player
			then card_name = "peach+analeptic" end
			if vs:isEnabledAtResponse(self.player,card_name) then
				vs = gc(self,patterns(class_name),class_name)
				if vs then
					if type(vs)=="table" then
						for _,st in ipairs(vs)do
							table.insert(ghs,st)
						end
					else
						table.insert(ghs,vs)
					end
				end
			end
		end
	end
	return ghs
end

function SmartAI:useBasicCard(card,use)
	self:useCardByClassName(card,use)
end

function SmartAI:useEquipCard(card,use)
	local ea = self:evaluateArmor(card)
	if self.player:getHandcardNum()<=1 and ea>-5 and self:needKongcheng()
	or ea>-5 and #self.enemies>1 and self:loseEquipEffect()
	then use.card = card return end
	local same = self:getSameEquip(card)
	if same and self:hasSkills("guzheng",self.friends) and self:evaluateArmor(same)>-5
	or self:useCardByClassName(card,use) then return end
	local gof = self:getOverflow()
	if card:isKindOf("Weapon") then
		local canUseSlash = self:slashIsAvailable()
		if gof<=(canUseSlash and 1 or 0) then
			if canUseSlash or self.player:hasWeapon("Crossbow")
			or self:needKongcheng() then else return end
		end
		if same and gof>0 and #self.toUse<2 and self:evaluateWeapon(card)>=self:evaluateWeapon(same)
		then use.card = card return end
	elseif card:isKindOf("Armor") then
		if card:isKindOf("SilverLion") and self.player:isWounded() and not self.player:hasArmorEffect(nil)
		or ea>0 and self.player:hasArmorEffect("SilverLion") and self:aiUseCard(dummyCard("peach")).card
		or not same and ea>0 and self:isWeak() then use.card = card return end
	elseif card:isKindOf("OffensiveHorse") then
		if same and not(self:slashIsAvailable() or self:getCard("Snatch")) then return end
	elseif card:isKindOf("DefensiveHorse") then
		if not same and ea>0 and self:isWeak()
		then use.card = card return end
	elseif card:isKindOf("Treasure") then
		for _,p in sgs.list(self.friends_noself)do
			if p:getPile("wooden_ox"):length()>1
			then return end
		end
	end
	if ea>-5 and gof>1
	and self:hasSkills("guzheng",self.enemies)
	then use.card = card
	else
		same = same and self:evaluateArmor(same)
		if type(same)=="number" then if gof<1 and same>1 then return end
		else same = card:isKindOf("Armor") and self:evaluateArmor() or -1 end
		if ea>same or ea>1 then use.card = card end
	end
end

function SmartAI:useTrickCard(card,use)
	if self:useCardByClassName(card,use)
	or use.card then return end
	if card:isKindOf("AOE") then
		if sgs.getMode:find("p")
		and sgs.getMode>="04p" then
			if card:isKindOf("ArcheryAttack") and self.player:getMark("AI_fangjian-Clear")>0 and self:getOverflow()<1
			or sgs.turncount<2 and (self.role=="loyalist" and card:isKindOf("ArcheryAttack") or self.role=="rebel" and card:isKindOf("SavageAssault"))
			then return end
		end
		if self:getAoeValue(card)>0
		then use.card = card end
	end
end

do
sgs.ai_use_revises.god_sword = function(self,card,use)
	if card:isKindOf("Slash") then
		card:setFlags("Qinggang")
	end
end

sgs.ai_use_revises.qinggang_sword = function(self,card,use)
	if card:isKindOf("Slash") then
		card:setFlags("Qinggang")
	end
end

sgs.ai_nullification.FireAttack = function(self,trick,from,to,positive,null_num)
	if to:isKongcheng() or from:isKongcheng() or self.player==from and self.player:getHandcardNum()==1
	then return false
	elseif positive then
		if self:isEnemy(from)
		and self:isFriend(to) then
			if from:getHandcardNum()>2
			or to:isChained() and not self:isGoodChainTarget(to,trick,from)
			then return true end
		end
	else
		
	end
end

sgs.ai_nullification.ExNihilo = function(self,trick,from,to,positive,null_num)
	if positive then
		if self:isEnemy(to) and (self:isWeak(to) or to:hasSkills(sgs.cardneed_skill) or ZishuEffect(to)>0 or to:hasSkill("manjuan"))
		and not(self.role=="rebel" and not self:hasExplicitRebel() and sgs.turncount<1 and self.room:getCurrent():getNextAlive()~=self.player)
		then return true end--敌方在虚弱、需牌技、漫卷中使用无中生有->命中
	else
		
	end
end

sgs.ai_nullification.IronChain = function(self,trick,from,to,positive,null_num)
	if positive then
		if self:isEnemy(from) and self:isFriend(to)
		and not to:isChained() and self:hasHeavyDamage(from,nil,to,"F")
		then return true end--铁索连环的目标有加伤->命中
	else
		
	end
end

sgs.ai_nullification.Duel = function(self,trick,from,to,positive,null_num)
	if positive then
		if to==self.player then
			if self:hasSkills(sgs.masochism_skill)
			and (self.player:getHp()>1 or self:getCardsNum("Analeptic,Peach")>0)
			then elseif self:getCardsNum("Slash")<1 then return true end
		end
		if to:getHp()<2 and sgs.ai_role[to:objectName()]=="rebel"
		and sgs.ai_role[from:objectName()]=="rebel"
		then return end
		if self:isFriend(to) then
			if self:isEnemy(from) and self:isWeak(to)
			or (self:isWeak(to) or null_num>1 or self:getOverflow()>0 or not self:isWeak())
			then return true end
		end
	else
		if self:isEnemy(to)
		and (self:isWeak(to) or null_num>1 or self:getOverflow()>0 or not self:isWeak())
		then return true end
	end
end

sgs.ai_nullification.Indulgence = function(self,trick,from,to,positive,null_num)
	if positive then
		if self:isFriend(to)
		and not(self:hasGuanxingEffect(to) or to:isSkipped(sgs.Player_Play))
		then--无观星友方判定区有乐不思蜀->视“突袭”、“巧变”情形而定
			if to:getHp()-to:getHandcardNum()>=2
			or to:getHp()>2 and to:hasSkill("tuxi")
			or null_num<2 and self:getOverflow(to)<-1
			or not to:isKongcheng() and to:hasSkill("qiaobian")
			and (to:containsTrick("supply_shortage") or self:willSkipDrawPhase(to))
			then else return true end
		end
	else
		
	end
end

sgs.ai_nullification.SupplyShortage = function(self,trick,from,to,positive,null_num)
	if positive then
		if self:isFriend(to)
		and not(self:hasGuanxingEffect(to) or to:isSkipped(sgs.Player_Draw))
		then--无观星友方判定区有兵粮寸断->视“鬼道”、“天妒”、“溃围”、“巧变”情形而定
			if self:hasSkills("guidao|tiandu",to)
			or to:getMark("@kuiwei")<1
			or null_num<=1 and self:getOverflow(to)>1
			or not to:isKongcheng() and to:hasSkill("qiaobian")
			and (to:containsTrick("indulgence") or self:willSkipPlayPhase(to))
			then else return true end
		end
	else
		
	end
end

sgs.ai_nullification.GodSalvation = function(self,trick,from,to,positive,null_num)
	if positive then
		if self:isEnemy(to) and self:isWeak(to)
		and to:getLostHp()>0
		then return true end
	else
		
	end
end

sgs.ai_nullification.AmazingGrace = function(self,trick,from,to,positive,null_num)
	if positive then
		local use = self.room:getUseStruct(trick)
		if use.to:last()~=to and self:isEnemy(to) then
			for i=1,null_num do
				local NP = to:getNextAlive(i)
				while not use.to:contains(NP) do
					NP = NP:getNextAlive()
				end
				if self:isFriend(NP) then
					local t = {p=0,e=0,s=0,a=0,c=0,i=0}
					for _,id in sgs.list(self.room:getTag("AmazingGrace"):toIntList())do
						local c = sgs.Sanguosha:getCard(id)
						if isCard("Peach",c,NP) then t.p = t.p+1 end
						if isCard("ExNihilo",c,NP) then t.e = t.e+1 end
						if isCard("Snatch",c,NP) then t.s = t.s+1 end
						if isCard("Analeptic",c,NP) then t.a = t.a+1 end
						if isCard("Crossbow",c,NP) then t.c = t.c+1 end
						if isCard("Indulgence",c,NP) then t.i = t.i+1 end
						if c:isDamageCard() then
							for _,friend in sgs.list(self.friends)do
								if to:canUse(c,friend) and to:getHandcardNum()>2
								and self:ajustDamage(to,friend,1,c)>1
								then return true end
							end
						end
					end
					if t.p==1 and to:getHp()<getBestHp(to)
					or t.p>0 and (self:isWeak(to) or NP:getHp()<getBestHp(NP) and self:getOverflow(NP)<1)
					then return true end
					if t.p<1 and not self:willSkipPlayPhase(NP) then
						if t.p>0 then
							if NP:hasSkills("nosjizhi|jizhi|nosrende|zhiheng")
							or NP:hasSkill("rende") and not NP:hasUsed("RendeCard")
							or NP:hasSkill("jilve") and NP:getMark("&bear")>0
							then return true end
						else
							for _,enemy in sgs.list(self.enemies)do
								if t.i>0 and not self:willSkipPlayPhase(enemy,true)
								or t.s>0 and to:distanceTo(enemy)==1 and (self:willSkipPlayPhase(enemy,true) or self:willSkipDrawPhase(enemy,true))
								or t.a>0 and (enemy:hasWeapon("Axe") or getCardsNum("Axe",enemy,self.player)>0)
								then return true
								elseif t.c>0
								and getCardsNum("Slash",enemy,self.player)>2 then
									for _,friend in sgs.list(self.friends)do
										if enemy:distanceTo(friend)==1
										and self:slashIsEffective(dummyCard(),friend,enemy)
										then return true end
									end
								end
							end
						end
					end
				end
			end
		end
	else
		
	end
end

sgs.ai_can_damagehp.zili = function(self,from,card,to)
	return not(self:isWeak(to) or to:hasSkill("paiyi"))
	and self:ajustDamage(from,to,1,card)>0
end

sgs.ai_can_damagehp.wumou = function(self,from,card,to)
	return to:getMark("&wrath")<7 and not self:isWeak(to)
	and self:ajustDamage(from,to,1,card)>0
end

sgs.ai_can_damagehp.longhun = function(self,from,card,to)
	return to:getHp()>1 and self:ajustDamage(from,to,1,card)~=0
end

sgs.ai_can_damagehp.tianxiang = function(self,from,card,to)
	local d = {damage=1}
	d.nature = card and sgs.card_damage_nature[card:getClassName()] or sgs.DamageStruct_Normal
	return sgs.ai_skill_use["@@tianxiang"](self,d,sgs.Card_MethodDiscard)~="."
end

sgs.ai_guhuo_card.guhuo = function(self,cname,class_name)
	local handcards = self:addHandPile("h")
	if #handcards>0 then
		handcards = self:sortByKeepValue(handcards) -- 按保留值排序
		local hc,fake,question = {},{},#self.enemies
		local all = sgs.getMode=="_mini_48"
		for _,enemy in sgs.list(self.enemies)do
			if enemy:hasSkill("chanyuan")
			or enemy:hasSkill("hunzi") and enemy:getMark("hunzi")<1
			then question = question-1 end
			if question<1 then all = true end
		end
		for _,h in sgs.list(handcards)do
			if h:isKindOf(class_name) then table.insert(hc,h)
			elseif self:getCardsNum(class_name)>0
			then table.insert(fake,h) end
		end
		if all then hc = handcards end
		if #hc>1 or #hc>0 and all then
			local index = 1
			if not all and (class_name=="Peach" or class_name=="Analeptic" or class_name=="Jink")
			then index = #hc end
			return "@GuhuoCard="..hc[index]:getEffectiveId()..":"..cname
		end
	 	question = question<1 and 100 or #self.enemies/question
		if #fake>0 and math.random(1,5)<=question
		then return "@GuhuoCard="..fake[1]:getEffectiveId()..":"..cname end
		if #hc>0 then return "@GuhuoCard="..hc[1]:getEffectiveId()..":"..cname end
		if self:isWeak() then
			if class_name=="Analeptic" and self:getCardsNum("Peach,Analeptic")<1
			then return "@GuhuoCard="..handcards[1]:getEffectiveId()..":"..cname end
			if class_name=="Jink" then return "@GuhuoCard="..handcards[1]:getEffectiveId()..":"..cname end
		end
		if class_name=="Jink" and math.random(1,#handcards+1)<(#handcards+1)/2
		then return "@GuhuoCard="..handcards[1]:getEffectiveId()..":"..cname
		elseif class_name=="Slash" and math.random(1,#handcards+2)>(#handcards+1)/2
		then return "@GuhuoCard="..handcards[1]:getEffectiveId()..":"..cname
		elseif class_name=="Peach" or class_name=="Analeptic" then
			question = self.room:getCurrentDyingPlayer()
			if question and self:isFriend(question) and math.random(1,#handcards+1)>(#handcards+1)/2
			then return "@GuhuoCard="..handcards[1]:getEffectiveId()..":"..cname end
		end
	end
end

sgs.ai_guhuo_card.nosguhuo = function(self,cname,class_name)
	if self.player:hasFlag(cname.."nosguhuoFailed") then return end
	local handcards = self:addHandPile("h")
	if #handcards>0 then
		handcards = self:sortByKeepValue(handcards) -- 按保留值排序
		local hc,fake,question = {},{},#self.enemies
		for _,enemy in sgs.list(self.enemies)do
			if enemy:getHp()<2 then question = question-1 end
		end
		for _,h in sgs.list(handcards)do
	 		if h:isKindOf(class_name) and h:getSuit()==2
			then table.insert(hc,h) end
		end
		for _,h in sgs.list(handcards)do
			if h:isKindOf(class_name)
			then if h:getSuit()~=2 then table.insert(hc,h) end
			elseif self:getCardsNum(class_name)>0
			then table.insert(fake,h) end
		end
		if question<1 then hc = handcards end
		self.player:setFlags(cname.."nosguhuoFailed")
		question = self.room:getCurrentDyingPlayer()
		if question and self:isFriend(question) and #hc>0
		then return "@NosGuhuoCard="..hc[1]:getEffectiveId()..":"..cname end
		if self:isWeak() then
			for _,h in sgs.list(handcards)do
				if class_name=="Analeptic" and not isCard("Peach,Analeptic",h,self.player)
				then return "@NosGuhuoCard="..h:getEffectiveId()..":"..cname end
			end
			if #hc>0 then return"@NosGuhuoCard="..hc[1]:getEffectiveId()..":"..cname end
			if class_name=="Jink" then return"@NosGuhuoCard="..handcards[1]:getEffectiveId()..":"..cname end
		end
		if #fake>0 and self:isWeak()
		then return "@NosGuhuoCard="..fake[1]:getEffectiveId()..":"..cname end
	 	if #hc>1 or #hc>0 and hc[1]:getSuit()==2 then
			local index = 1
			if class_name=="Peach" or class_name=="Jink"
			or class_name=="Analeptic" then index = #hc end
			return "@NosGuhuoCard="..hc[index]:getEffectiveId()..":"..cname
		end
	end
end

sgs.ai_guhuo_card.taoluan = function(self,cname,class_name)
	if self:getCardsNum(class_name)<1 and self.player:getCardCount()>0 then
		local he = self.player:getCards("he")
		he = self:sortByKeepValue(he,nil,"l") -- 按保留值排序
	 	self:sort(self.friends_noself,"card",true)
		if #self.friends_noself>0 and self.friends_noself[1]:getCardCount()>1 or self.player:getHp()>1
		then return "@TaoluanCard="..he[1]:getEffectiveId()..":"..cname end
	end
end

sgs.ai_ajustdamage_to.shenjun = function(self,from,to,card,nature)
	if to:getGender()~=from:getGender() and nature~="T"
	then return -99 end
end

sgs.ai_ajustdamage_to.fenyong = function(self,from,to,card,nature)
	if to:getMark("@fenyong")>0
	then return -99 end
end

sgs.ai_ajustdamage_to["&dawu"] = function(self,from,to,card,nature)
	if nature~="T"
	then return -99 end
end

sgs.ai_ajustdamage_to["&ollie"] = function(self,from,to,card,nature)
	--return -99
end

sgs.ai_ajustdamage_to.jgyuhuo = function(self,from,to,card,nature)
	if nature=="F" then return -99 end
end

sgs.ai_ajustdamage_to.shixin = function(self,from,to,card,nature)
	if nature=="F" then return -99 end
end

sgs.ai_ajustdamage_to["&jinxuanmu-Clear"] = function(self,from,to,card,nature)
	return -99
end

sgs.ai_ajustdamage_from.tenyearzishou = function(self,from,to,card,nature)
	if from:objectName()~=to:objectName()
	then return -99 end
end

sgs.ai_ajustdamage_from.olzishou = function(self,from,to,card,nature)
	if from:hasFlag("olzishou") and from:objectName()~=to:objectName()
	then return -99 end
end

sgs.ai_ajustdamage_from.olchezheng = function(self,from,to,card,nature)
	if from:getPhase()==sgs.Player_Play and not to:inMyAttackRange(from)
	then return -99 end
end

sgs.ai_ajustdamage_to.shichou = function(self,from,to,card,nature)
	if to:getMark("xhate")>0 then
		for _,p in sgs.list(self.room:getOtherPlayers(to))do
			if p:getMark("hate_"..to:objectName())>0 and p:getMark("@hate_to")>0
			then return self:ajustDamage(from,p,0,card,nature) end
		end
	end
end

sgs.ai_ajustdamage_to.yuce = function(self,from,to,card,nature)
	if not to:isKongcheng() and to:getHp()>1 then
		if self:isFriend(to,from) then return -1
		else
			if from:objectName()~=self.player:objectName() then
				if from:getHandcardNum()<=2
				then return -1 end
			else
				if getKnownCard(to,self.player,"TrickCard,EquipCard",false,"h")<to:getHandcardNum()
				and getCardsNum("TrickCard,EquipCard",from,self.player)-from:getEquips():length()<1
				or getCardsNum("BasicCard",from,self.player)<2
				then return -1 end
			end
		end
	end
end

sgs.ai_ajustdamage_to.shibei = function(self,from,to,card,nature)
	if getKnownCard(from,self.player,"TrickCard")>1
	or getKnownCard(from,self.player,"Slash")>1 and (getKnownCard(from,self.player,"Crossbow")>0 or from:hasSkills(sgs.double_slash_skill))
	or from:hasSkills(sgs.straight_damage_skill)
	or self:getOverflow(from)>0 then
	else
		return to:getMark("shibei")<1
	end
end

sgs.ai_card_priority.jianying = function(self,card,v)
	if self.player:getMark("JianyingSuit")==card:getSuit()+1
	or self.player:getMark("JianyingNumber")==card:getNumber()
	then return 10 end
end

sgs.ai_card_priority.yanxiao = function(self,card,v)
	if card:isKindOf("YanxiaoCard") and self.player:containsTrick("YanxiaoCard")
	then v = 0.10 end
end

sgs.ai_card_priority.luanji = function(self,card,v)
	if card:isKindOf("ArcheryAttack")
	then return 6 end
end

sgs.ai_card_priority.sp_moonspear = function(self,card,v)
	if card:isBlack() and self.player:getPhase()==sgs.Player_NotActive
	then return 5 end
end

sgs.ai_card_priority.moon_spear = function(self,card,v)
	if card:isBlack() and self.player:getPhase()==sgs.Player_NotActive
	then return 5 end
end

sgs.ai_card_priority.shuangxiong = function(self,card,v)
	if card:isKindOf("Duel")
	then return 6.3 end
end

sgs.ai_card_priority.cihuai = function(self,card,v)
	if card:isKindOf("Slash")
	and self.player:getMark("@cihuai")>0
	then return 9 end
end

sgs.ai_card_priority.danshou = function(self,card,v)
	if (card:isDamageCard() or sgs.dynamic_value.damage_card[card:getClassName()])
	and not self.player:hasSkill("jueqing")
	then v = 0 end
end

sgs.ai_card_priority.kuanggu = function(self,card,v)
	if card:isKindOf("Peach")
	then v = 1.09 end
end

sgs.ai_card_priority.kofkuanggu = function(self,card,v)
	if card:isKindOf("Peach")
	then v = 1.09 end
end

sgs.ai_ajustdamage_to.mingshi = function(self,from,to,card,nature)
	local x = self.equipsToDec or 0
	if card then x = sgs.getCardNumAtCertainPlace(card,sgs.Player_PlaceEquip) end
	if from:getEquips():length()-x<=to:getEquips():length() then return -1 end
end

sgs.ai_ajustdamage_to.ranshang = function(self,from,to,card,nature)
	if nature=="F"
	then return 1 end
end

sgs.ai_ajustdamage_to["&kuangfeng"] = function(self,from,to,card,nature)
	if nature=="F"
	then return 1 end
end

sgs.ai_ajustdamage_to.vine = function(self,from,to,card,nature)
	if nature=="F" and not IgnoreArmor(from,to)
	then return 1 end
end

sgs.ai_ajustdamage_to.gongqing = function(self,from,to,card,nature)
	if from:getAttackRange()>3
	then return 1 end
end

sgs.ai_ajustdamage_from.xionghuo = function(self,from,to,card,nature)
	if to:getMark("@brutal")>0
	then return 1 end
end

sgs.ai_ajustdamage_from.jinjian = function(self,from,to,card,nature)
	if from:getMark("&jinjianreduce-Clear")>0
	then return -from:getMark("&jinjianreduce-Clear") end
	if not self:isFriend(from)
	then return 1 end
end

sgs.ai_ajustdamage_to.jinjian = function(self,from,to,card,nature)
	if to:getMark("&jinjianadd-Clear")>0
	then return to:getMark("&jinjianadd-Clear") end
end

sgs.ai_ajustdamage_from.jieyuan = function(self,from,to,card,nature)
	if not beFriend(to,from)
	and (to:getHp()>=from:getHp() or from:getMark("jieyuan_rebel-Keep")>0)
	and (from:getHandcardNum()>=3 or (from:getMark("jieyuan_renegade-Keep")>0 and not from:isNude()))
	then return 1 end
end

sgs.ai_ajustdamage_to.chouhai = function(self,from,to,card,nature)
	if to:isKongcheng()
	then return 1 end
end

sgs.ai_ajustdamage_from.jiedao = function(self,from,to,card,nature)
	if from:getLostHp()>0 and not beFriend(to,from)
	then return from:getLostHp() end
end

sgs.ai_ajustdamage_from.lingren = function(self,from,to,card,nature)
	if from:getPhase()==sgs.Player_Play and to:hasFlag("lingren_damage_to")
	then return 1 end
end

sgs.ai_ajustdamage_from.zhuixi = function(self,from,to,card,nature)
	if from:faceUp()~=to:faceUp()
	then return 1 end
end

sgs.ai_ajustdamage_to.zhuixi = function(self,from,to,card,nature)
	if from:faceUp()~=to:faceUp()
	then return 1 end
end

sgs.ai_ajustdamage_from["@luoyi"] = function(self,from,to,card,nature)
	if card and card:isKindOf("Duel")
	then return 1 end
end

sgs.ai_ajustdamage_to.yj_yinfengyi = function(self,from,to,card,nature)
	if card and card:isKindOf("TrickCard")
	then return 1 end
end

sgs.ai_ajustdamage_to.zl_yinfengjia = function(self,from,to,card,nature)
	if card and card:isKindOf("TrickCard")
	then return 1 end
end

sgs.ai_ajustdamage_to.yinshi = function(self,from,to,card,nature)
	if card and hasYinshiEffect(to) and (card:getTypeId()==2 or card:isKindOf("NatureSlash") or nature~="N")
	then return -99 end
end

sgs.ai_ajustdamage_from.wenjiu = function(self,from,to,slash,nature)
	if slash and slash:isKindOf("Slash") and slash:isBlack()
	then return 1 end
end

sgs.ai_ajustdamage_from.shenli = function(self,from,to,slash,nature)
	if slash and slash:isKindOf("Slash") and from:getMark("@struggle")>0
	then return math.min(3,from:getMark("@struggle")) end
end

sgs.ai_ajustdamage_from["&mobileliyong"] = function(self,from,to,slash,nature)
	if slash and slash:isKindOf("Slash")
	then return 1 end
end

sgs.ai_ajustdamage_from.wangong = function(self,from,to,slash,nature)
	if slash and slash:isKindOf("Slash") and from:getMark("&wangong")+from:getMark("wangong")>0
	then return 1 end
end

sgs.ai_ajustdamage_from["&kannan"] = function(self,from,to,slash,nature)
	if slash and slash:isKindOf("Slash")
	then return from:getMark("&kannan") end
end

sgs.ai_ajustdamage_from.jie = function(self,from,to,slash,nature)
	if slash and slash:isKindOf("Slash") and slash:isRed()
	then return 1 end
end

sgs.ai_ajustdamage_from.zonghuo = function(self,from,to,slash,nature)
	if slash and slash:isKindOf("Slash")
	then nature = "F" end
end

sgs.ai_ajustdamage_from.anjian = function(self,from,to,slash,nature)
	if slash and slash:isKindOf("Slash") and not to:inMyAttackRange(from)
	then return 1 end
end

sgs.ai_ajustdamage_to.jiaojin = function(self,from,to,slash,nature)
	if slash and slash:isKindOf("Slash") and from:isMale() and to:getEquips():length()>0
	then return -1 end
end

sgs.ai_ajustdamage_from.guding_blade = function(self,from,to,slash,nature)
	if slash and slash:isKindOf("Slash") and to:isKongcheng()
	then return 1 end
end

sgs.ai_card_priority.zhulou = function(self,card,v)
	if self.useValue
	and card:isKindOf("Weapon")
	then v = 2 end
end

sgs.ai_card_priority.taichen = function(self,card,v)
	if self.useValue
	and card:isKindOf("Weapon")
	then v = 2 end
end

sgs.ai_card_priority.qiangxi = function(self,card,v)
	if self.useValue
	and card:isKindOf("Weapon")
	then v = 2 end
end

sgs.ai_card_priority.kurou = function(self,card,v)
	if self.useValue
	and card:isKindOf("Crossbow")
	then return 9 end
end

sgs.ai_card_priority.yizhong = function(self,card,v)
	if self.useValue
	and card:isKindOf("Armor")
	then v = 2 end
end

sgs.ai_card_priority.bazhen = function(self,card,v)
	if self.useValue
	and card:isKindOf("Armor")
	then v = 2 end
end

sgs.ai_card_priority.kuanggu = function(self,card,v)
	if self.useValue
	and card:inherits("Shit")
	and card:getSuit()~=sgs.Card_Spade
	then v = 0.1 end
end

sgs.ai_card_priority.jizhi = function(self,card,v)
	if self.useValue
	and card:isKindOf("TrickCard")
	then v = v+3 end
end

sgs.ai_card_priority.nosjizhi = function(self,card,v)
	if self.useValue
	and card:isNDTrick()
	then v = v+4 end
end

sgs.ai_card_priority.shuangxiong = function(self,card,v)
	if self.useValue
	and card:getSkillName()=="shuangxiong"
	then v = 6 end
end

sgs.ai_card_priority.wumou = function(self,card,v)
	if self.useValue and card:isNDTrick() and not card:isKindOf("AOE")
	and not (card:isKindOf("Duel") and self.player:hasUsed("WuqianCard"))
	then v = 1 end
end

sgs.ai_card_priority.tenyearchouhai = function(self,card,v)
	if self.useValue
	and self.player:getHandcardNum()<=1
	then v = 1 end
end

sgs.ai_card_priority.chouhai = function(self,card,v)
	if self.useValue
	and self.player:hasFlag("canshi")
	and self.player:getHandcardNum()<=2
	then v = 1 end
end

sgs.ai_card_priority.canshi = function(self,card,v)
	if self.useValue and self.player:hasFlag("canshi")
	then v = v-2 end
end

sgs.ai_card_priority.halberd = function(self,card,v)
	if self.useValue and card:isKindOf("Slash")
	and self.player:isLastHandCard(card)
	then v = 10 end
end

sgs.ai_card_priority.spear = function(self,card)
	if card:getSkillName()=="spear"
	then
		if self.useValue
		then return -1 end
		return -0.2
	end
end

sgs.ai_card_priority.jie = function(self,card)
	if card:isKindOf("Slash") and card:isRed()
	then return 0.16 end
end

sgs.ai_card_priority.chongzhen = function(self,card)
	if card:getSkillName()=="longdan"
	then
		if self.useValue
		then return 1 end
		return 0.08
	end
end

sgs.ai_card_priority.fuhun = function(self,card)
	if card:getSkillName()=="fuhun"
	then
		if self.useValue
		then return self.player:getPhase()==sgs.Player_Play and 1 or -1 end
		return self.player:getPhase()==sgs.Player_Play and 0.06 or -0.05
	end
end

sgs.ai_card_priority.jiang = function(self,card)
	if card:isKindOf("Slash") and card:isRed()
	then return 0.05 end
end

sgs.ai_card_priority.wushen = function(self,card)
	if card:isKindOf("Slash") and card:getSuit()==sgs.Card_Heart
	then return 0.03 end
end

sgs.ai_card_priority.olwushen = function(self,card)
	if card:isKindOf("Slash") and card:getSuit()==sgs.Card_Heart
	then return 0.03 end
end

sgs.ai_card_priority.lihuo = function(self,card)
	if card:getSkillName()=="lihuo"
	then return -0.02 end
end

sgs.ai_card_priority.mingzhe = function(self,card)
	if card:isRed() then return self.player:getPhase()~=sgs.Player_NotActive and 0.05 or -0.05 end
end

sgs.ai_card_priority.zenhui = function(self,card)
	if card:isBlack() and (card:isKindOf("Slash") or card:isNDTrick())
	and sgs.Sanguosha:correctCardTarget(sgs.TargetModSkill_ExtraTarget,self.player,card)<1
	and not self.player:hasFlag("zenhui")
	then return 0.1 end
end

sgs.ai_target_revises.god_diagram = function(to,card)
	if card:isKindOf("Slash")
	then return true end
end

sgs.ai_target_revises.vine = function(to,card)
	if card:isKindOf("SavageAssault")
	or card:isKindOf("ArcheryAttack")
	or card:isKindOf("Chuqibuyi")
	or card:objectName()=="slash"
	then return true end
end

sgs.ai_nullification.kongcheng = function(self,trick,from,to,positive,null_num)
	if positive and from and self:isEnemy(from) and self:isFriend(to)
	and sgs.ai_role[from:objectName()]~="neutral"
	and self.player:getHandcardNum()<=null_num 
	then return true end
end

sgs.ai_nullification.wumou = function(self,trick,from,to,positive,null_num)
	if self.player:getMark("&wrath")<1 and self:isWeak()
	or not self:isWeak() and to:objectName()==self.player:objectName() and trick:isDamageCard()
	then return false end
end

sgs.ai_target_revises["dream"] = function(to,card)
	if to:isLocked(card) then return true end
end

sgs.ai_target_revises.huoshou = function(to,card)
	if card:isKindOf("SavageAssault")
	then return true end
end

sgs.ai_target_revises.danlao = function(to,card,self,use)
	if card:isKindOf("TrickCard") and use.to:length()>1
	and not self:isFriend(to)
	then return true end
end

sgs.ai_target_revises.xiemu = function(to,card,self,use)
	if card:getTypeId()>0 and card:isBlack() and self:isEnemy(to)
	and to:getMark("@xiemu_"..self.player:getKingdom())>0
	and not(self:isWeak(to) and card:isDamageCard())
	then return true end
end

sgs.ai_target_revises.juxiang = function(to,card)
	if card:isKindOf("SavageAssault")
	then return true end
end

sgs.ai_target_revises.manyi = function(to,card)
	if card:isKindOf("SavageAssault")
	then return true end
end

sgs.ai_target_revises.olzhennan = function(to,card)
	if card:isKindOf("SavageAssault")
	then return true end
end

sgs.ai_target_revises.tenyearzongshi = function(to,card)
	if (card:isKindOf("DelayedTrick") or not card:isBlack() and not card:isRed())
	and to:getPhase()==sgs.Player_NotActive and to:getHandcardNum()>=to:getMaxCards()
	then return true end
end

sgs.ai_target_revises["@late"] = function(to,card,self)
	return card:isKindOf("Slash") or card:isNDTrick()
end

sgs.ai_target_revises.noswuyan = function(to,card,self)
	return card:isNDTrick() and self.player~=to
	and not hasJueqingEffect(self.player,to)
end

sgs.ai_target_revises.wuyan = function(to,card,self)
	return card:isNDTrick() and card:isDamageCard()
	and not hasJueqingEffect(self.player,to)
end

sgs.ai_use_revises.wuyan = function(self,card,use)
	if card:isKindOf("SavageAssault") then
		local menghuo = self.room:findPlayerBySkillName("huoshou")
		if menghuo and (not menghuo:hasSkill("wuyan") or hasJueqingEffect(menghuo))
		then else return false end
	elseif card:isKindOf("AOE") then
		if self:hasHuangenEffect(self.friends_noself)
		then else return false end
	elseif card:isNDTrick() and card:isDamageCard()
	and not hasJueqingEffect(self.player)
	then return false end
end

sgs.ai_use_revises.noswuyan = function(self,card,use)
	if card:isKindOf("AOE") then
		if self:hasHuangenEffect(self.friends_noself)
		then else return false end
	elseif card:isKindOf("AmazingGrace") then use.card = card
	elseif card:isKindOf("GlobalEffect") then
		if self.player:isWounded() or self.player:hasSkills("nosjizhi|jizhi")
		then use.card = card else return false end
	elseif card:isNDTrick() and not card:targetFixed()
	then use.card = nil return false end
end

sgs.ai_use_revises.luanji = function(self,card,use)
	if card:isKindOf("AOE") and self.player:getRole()=="lord" and sgs.turncount<2 and math.random()>0.7
	then self.player:addMark("AI_fangjian-Clear") end
end

sgs.ai_use_revises.nosgongqi = function(self,card,use)
	if card:isKindOf("EquipCard") and self:getSameEquip(card)
	and self:slashIsAvailable() then return false end
end

sgs.ai_use_revises.xiaoji = function(self,card,use)
	if card:isKindOf("EquipCard") and self:evaluateArmor(card)>-5
	then use.card = card return true end
end

sgs.ai_use_revises.kofxiaoji = function(self,card,use)
	if card:isKindOf("EquipCard") and self:evaluateArmor(card)>-5
	then use.card = card return true end
end

sgs.ai_use_revises.yongsi = function(self,card,use)
	if card:isKindOf("EquipCard") and self:getOverflow()<2
	then return false end
end

sgs.ai_use_revises.qixi = function(self,card,use)
	if card:isKindOf("EquipCard")
	and card:isBlack()
	then
		local same = self:getSameEquip(card)
		if same and same:isBlack()
		then return false end
	end
end

sgs.ai_use_revises.duanliang = function(self,card,use)
	if card:isKindOf("EquipCard")
	and card:isBlack()
	then
		local same = self:getSameEquip(card)
		if same and same:isBlack()
		then return false end
	end
end

sgs.ai_use_revises.yinling = function(self,card,use)
	if card:isKindOf("EquipCard")
	and card:isBlack()
	then
		local same = self:getSameEquip(card)
		if same and same:isBlack()
		then return false end
	end
end

sgs.ai_use_revises.guose = function(self,card,use)
	if card:isKindOf("EquipCard")
	and card:getSuit()==sgs.Card_Diamond
	then
		local same = self:getSameEquip(card)
		if same and same:getSuit()==sgs.Card_Diamond
		then return false end
	end
end

sgs.ai_use_revises.longhun = function(self,card,use)
	if card:isKindOf("EquipCard")
	and card:getSuit()==sgs.Card_Diamond
	then
		local same = self:getSameEquip(card)
		if same and same:getSuit()==sgs.Card_Diamond
		then return false end
	end
end

sgs.ai_use_revises.jijiu = function(self,card,use)
	if card:isKindOf("EquipCard") and card:isRed() then
		local same = self:getSameEquip(card)
		if same and same:isRed() then return false end
	end
end

sgs.ai_use_revises.guidao = function(self,card,use)
	if card:isKindOf("EquipCard")
	and card:isBlack()
	then
		local same = self:getSameEquip(card)
		if same and same:isBlack()
		then return false end
	end
end

sgs.ai_use_revises.junxing = function(self,card,use)
	if card:isKindOf("EquipCard")
	and self:getSameEquip(card)
	then return false end
end

sgs.ai_use_revises.jiehuo = function(self,card,use)
	if (card:isKindOf("Weapon") or card:isKindOf("OffensiveHorse"))
	and self.player:getMark("jiehuo")<0 and card:isRed()
	then return false end
end

sgs.ai_use_revises.zhulou = function(self,card,use)
	if card:isKindOf("Weapon") and self:getSameEquip(card)
	then return false end
end

sgs.ai_use_revises.taichen = function(self,card,use)
	if card:isKindOf("Weapon")
	then
		local same = self:getSameEquip(card)
		if same
		then
			same = sgs.Card_Parse("@TaichenCard="..same:getEffectiveId())
			if self:aiUseCard(same).card
			then return false end
		end
	end
end

sgs.ai_use_revises.qiangxi = function(self,card,use)
	if card:isKindOf("Weapon")
	and not self.player:hasUsed("QiangxiCard")
	then
		local same = self:getSameEquip(card)
		if same
		then
			same = sgs.Card_Parse("@QiangxiCard="..same:getEffectiveId())
			same = self:aiUseCard(same)
			if same.card and same.card:subcardsLength()==1
			then return false end
		end
	end
end

sgs.ai_use_revises.paoxiao = function(self,card,use)
	if card:isKindOf("Crossbow")
	then return false end
end

sgs.ai_use_revises.tenyearpaoxiao = function(self,card,use)
	if card:isKindOf("Crossbow")
	then return false end
end

sgs.ai_use_revises.olpaoxiao = function(self,card,use)
	if card:isKindOf("Crossbow")
	then return false end
end

sgs.ai_use_revises.nosfuhun = function(self,card,use)
	if card:isKindOf("Crossbow")
	then return false end
end

sgs.ai_use_revises.zhiheng = function(self,card,use)
	if card:isKindOf("Weapon")
	and not card:isKindOf("Crossbow")
	and self:getSameEquip(card)
	and not self.player:hasUsed("ZhihengCard")
	then return false end
end

sgs.ai_use_revises.jilve = function(self,card,use)
	if card:isKindOf("Weapon")
	and self.player:getMark("&bear")>0
	and not card:isKindOf("Crossbow")
	and self:getSameEquip(card)
	and not self.player:hasUsed("ZhihengCard")
	then return false end
end

sgs.ai_use_revises.tiaoxin = function(self,card,use)
	if card:isKindOf("DefensiveHorse") then
		local dummy = dummy()
		dummy.defHorse = true
		if self:aiUseCard(sgs.Card_Parse("@TiaoxinCard=."),dummy).card
		then return false end
	end
end

end


dofile"lua/ai/debug-ai.lua"
dofile"lua/ai/imagine-ai.lua"
dofile"lua/ai/standard_cards-ai.lua"
dofile"lua/ai/maneuvering-ai.lua"
dofile"lua/ai/classical-ai.lua"
dofile"lua/ai/standard-ai.lua"
dofile"lua/ai/chat-ai.lua"
dofile"lua/ai/basara-ai.lua"
dofile"lua/ai/hegemony-ai.lua"
dofile"lua/ai/hulaoguan-ai.lua"
dofile"lua/ai/jiange-defense-ai.lua"
dofile"lua/ai/boss-ai.lua"

local loaded = "standard|standard_cards|maneuvering|sp"

local ai_files = sgs.GetFileNames("lua/ai")

for _,aextension in ipairs(sgs.Sanguosha:getExtensions())do
	if loaded:match(aextension) then continue end
	local sl = string.lower(aextension).."-ai.lua"
	for _,ai_file in ipairs(ai_files)do
		if sl==string.lower(ai_file)
		then dofile("lua/ai/"..sl) break end
	end
end

dofile"lua/ai/sp-ai.lua"
dofile"lua/ai/special3v3-ai.lua"

for _,ascenario in ipairs(sgs.Sanguosha:getModScenarioNames())do
	if loaded:match(ascenario) then continue end
	local sl = string.lower(ascenario).."-ai.lua"
	for _,ai_file in ipairs(ai_files)do
		if sl==string.lower(ai_file)
		then dofile("lua/ai/"..sl) break end
	end
end

for _,skill in ipairs(sgs.ai_skills)do
	sgs.ai_fill_skill[skill.name] = skill.getTurnUseCard
end