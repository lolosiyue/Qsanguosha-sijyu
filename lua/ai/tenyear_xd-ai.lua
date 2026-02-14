
sgs.ai_fill_skill.zhizhe = function(self)
	local cs = {}
	for _,h in sgs.list(self:sortByUseValue(self.player:getCards("h")))do
		if h:getTypeId()<3 and not h:isKindOf("DelayedTrick")
		then table.insert(cs,h) end
	end
	if #cs<4 or self:getUseValue(cs[1])<8 then return end
	return sgs.Card_Parse("@ZhizheCard="..cs[1]:getEffectiveId())
end

sgs.ai_skill_use_func["ZhizheCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.ZhizheCard = 5.4
sgs.ai_use_priority.ZhizheCard = 13.8

sgs.ai_skill_playerschosen.qingshi = function(self,players,x,n)
	local destlist = sgs.QList2Table(players)
	self:sort(destlist,"hp")
	local tos = {}
	for _,to in sgs.list(destlist)do
		if #tos>=x then break end
		if self:isFriend(to) then table.insert(tos,to) end
	end
	for _,to in sgs.list(destlist)do
		if #tos>=x or #tos>self.player:aliveCount()/2 then break end
		if not table.contains(tos,to) and not self:isEnemy(to)
		then table.insert(tos,to) end
	end
	return tos
end

sgs.ai_skill_choice.qingshi = function(self,choices,data)
	local items = choices:split("+")
	if table.contains(items,"draw")
	and #self.friends_noself>self.player:getHp()
	and #self.toUse>3 then return "draw" end
	for _,c in sgs.list(items)do
		if c:startsWith("selfdraw")
		then return c end
	end
	return items[1]
end

sgs.ai_skill_invoke.tenyearsuifu = function(self,data)
	local target = self.room:getCurrent()
	if target then
		return not self:isFriend(target)
		or #self:poisonCards("he",target)>target:getCardCount()/2
	end
end

sgs.ai_skill_playerschosen.tenyearpijing = function(self,players,x,n)
	local destlist = sgs.QList2Table(players)
	self:sort(destlist,"hp")
	local tos = {}
	for _,to in sgs.list(destlist)do
		if #tos>=x then break end
		if self:isFriend(to) then table.insert(tos,to) end
	end
	for _,to in sgs.list(destlist)do
		if #tos>=x or #tos>self.player:aliveCount()/2 then break end
		if not table.contains(tos,to) and not self:isEnemy(to)
		then table.insert(tos,to) end
	end
	return tos
end

sgs.ai_skill_invoke.lieqiong = function(self,data)
	local target = data:toPlayer()
	if target then
		return not self:isFriend(target)
	end
end

sgs.ai_skill_choice.lieqiong = function(self,choices,data)
	local items = choices:split("+")
	local damage = data:toDamage()
	if table.contains(items,"lq_tianchong")
	and self:isEnemy(damage.to)
	then return "lq_tianchong" end
	if table.contains(items,"lq_zhongshu")
	and self.player:getMark("&lqjishang+:+lq_zhongshu-SelfClear")<1
	then return "lq_zhongshu" end
	if table.contains(items,"lq_diji")
	and self.player:getMark("&lqjishang+:+lq_diji-SelfClear")<1
	then return "lq_diji" end
	if table.contains(items,"lq_lifeng")
	then return "lq_lifeng" end
	if table.contains(items,"lq_qihai")
	and self.player:getMark("&lqjishang+:+lq_qihai-SelfClear")<1
	then return "lq_qihai" end
end

sgs.ai_skill_invoke.thzhanjue = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_choice.thzhanjue = function(self,choices,data)
	local items = choices:split("+")
	if table.contains(items,"thzhanjue1")
	and self.player:getLostHp()<self.player:getHp()
	then return "thzhanjue1" end
	if table.contains(items,"thzhanjue2")
	and self.player:getLostHp()>self.player:getHp()
	then return "thzhanjue2" end
end

sgs.ai_used_revises.fengliao = function(self,use)
	if use.to:length()~=1 or use.card:canRecast() then return end
	local tp = self.player
	if self.player:getChangeSkillState("fengliao")==1 then
		if use.card:isSingleTargetCard() and use.to:length()==1 then tp = use.to:at(0) end
		if self:isEnemy(tp) and #self.toUse>1 then use.card = nil end
	else
		if use.card:isSingleTargetCard() and use.to:length()==1 then tp = use.to:at(0) end
		if tp==self.player then
			if self:getCardNum("Peach,Analeptic")<1 then use.card = nil end
		elseif self:isFriend(tp)
		then use.card = nil end
	end
end

sgs.ai_fill_skill.peiniang = function(self)
    local cards = self.player:getCards("he")
    cards = sgs.QList2Table(cards) -- 将列表转换为表
    self:sortByKeepValue(cards) -- 按保留值排序
	for i,c1 in sgs.list(cards)do
		if self.player:property("yitongSuit"):toString()~=c1:getSuitString()
		or table.contains(self.toUse,c1) then continue end
		local dc = dummyCard("analeptic")
		dc:setSkillName("peiniang")
		dc:addSubcard(c1)
		if dc:isAvailable(self.player)
		then return dc end
	end
end

function sgs.ai_cardsview.peiniang(self,class_name,player)
   	local cards = sgs.QList2Table(player:getCards("h"))
	self:sortByKeepValue(cards)
	for _,c in sgs.list(cards)do
       	if c:isKindOf("Analeptic") then
	   		return c:toString()
	   	end
	end
	for _,c in sgs.list(cards)do
       	if player:property("yitongSuit"):toString()==c:getSuitString() then
	   		return ("analeptic:peiniang[no_suit:0]="..c:toString())
	   	end
	end
end

sgs.ai_skill_invoke.chaozhen = function(self,data)
	return self:canDraw()
end

sgs.ai_skill_invoke.lianjie = function(self,data)
	return self:canDraw()
end

sgs.ai_fill_skill.jiangxian = function(self)
	return sgs.Card_Parse("@JiangxianCard=.")
end

sgs.ai_skill_use_func["JiangxianCard"] = function(card,use,self)
	if self:isWeak() then
		if self.player:getMaxHp()<3 then
			sgs.ai_skill_choice.jiangxian = "jiangxian1"
			use.card = card
		end
	end
end

sgs.ai_use_value.JiangxianCard = 5.4
sgs.ai_use_priority.JiangxianCard = 0.8

sgs.ai_skill_cardask.thshenduan0 = function(self,data,pattern)
    local pd = data:toPindian()
	if pd.to==self.player then
		if self:isEnemy(pd.from)
		then return true end
	else
		if self:isEnemy(pd.to)
		then return true end
	end
	return "."
end

sgs.ai_fill_skill.thkegou = function(self)
	return sgs.Card_Parse("@ThKegouCard=.")
end

sgs.ai_skill_use_func["ThKegouCard"] = function(card,use,self)
	self:sort(self.enemies)
	for _,to in sgs.list(self.enemies)do
		if self.player:canPindian(to) then
			use.to:append(to)
			use.card = card
			break
		end
	end
end

sgs.ai_use_value.ThKegouCard = 5.4
sgs.ai_use_priority.ThKegouCard = 3.8

sgs.ai_skill_use["@@thkegou"] = function(self,prompt)
	self:sort(self.enemies)
	for _,to in sgs.list(self.enemies)do
		if self.player:canPindian(to) then
			return string.format("@ThKegouCard=.->%s",to:objectName())
		end
	end
end

sgs.ai_fill_skill.dixian = function(self)
	return sgs.Card_Parse("@DixianCard=.")
end

sgs.ai_skill_use_func["DixianCard"] = function(card,use,self)
	if self:isWeak() and self:getOverflow()<1 then
		use.card = card
	end
end

sgs.ai_use_value.DixianCard = 5.4
sgs.ai_use_priority.DixianCard = 3.8

sgs.ai_skill_playerchosen.ruijun = function(self,players)
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

sgs.ai_skill_discard.thzhiji = function(self)
	local cards = {}
    local handcards = sgs.QList2Table(self.player:getCards("h"))
    self:sortByKeepValue(handcards) -- 按保留值排序
   	for _,h in sgs.list(handcards)do
		if #cards>#handcards/2 or self:getKeepValue(h)>7 then continue end
		table.insert(cards,h:getEffectiveId())
	end
	return cards
end

sgs.ai_skill_playerschosen.thzhiji = function(self,players,x,n)
	local destlist = sgs.QList2Table(players)
	self:sort(destlist)
	local tos = {}
	for _,to in sgs.list(destlist)do
		if #tos>=x then break end
		if self:isEnemy(to) then table.insert(tos,to) end
	end
	for _,to in sgs.list(destlist)do
		if #tos>=x or table.contains(tos,to) then break end
		if not self:isFriend(to) then table.insert(tos,to) end
	end
	return tos
end

sgs.ai_fill_skill.zhongyan = function(self)
	return sgs.Card_Parse("@ZhongyanCard=.")
end

sgs.ai_skill_use_func["ZhongyanCard"] = function(card,use,self)
	self:sort(self.friends)
	for i,to in sgs.list(self.friends)do
		if i>=#self.friends/2 and to:getHandcardNum()>0 then
			use.to:append(to)
			use.card = card
			break
		end
	end
end

sgs.ai_use_value.ZhongyanCard = 5.4
sgs.ai_use_priority.ZhongyanCard = 3.8

sgs.ai_skill_use["@@zhongyan!"] = function(self,prompt)
	self.qianlong_use = false
	local n1 = {}
	for _,id in sgs.list(self.player:getTag("zhongyanForAI"):toIntList())do
		table.insert(n1,sgs.Sanguosha:getCard(id))
	end
	self:sortByKeepValue(n1,true)
	local hs = self:sortByKeepValue(self.player:getHandcards(),true)
	for _,c in sgs.list(n1)do
		local ids = {c:toString()}
		for _,h in sgs.list(hs)do
			if self:getUseValue(c)>self:getUseValue(h) then
				table.insert(ids,c:toString())
				return "@ZhongyanCard="..table.concat(ids,"+")
			end
		end
	end
end

sgs.ai_skill_playerchosen.zhongyan = function(self,players)
	if self:isWeak() and self.player:isWounded() then return end
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for _,target in sgs.list(destlist)do
		if self:isFriend(target) and self:doDisCard(target,"ej",true)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if self:doDisCard(target,"ej",true)
		then return target end
	end
    for _,target in sgs.list(destlist)do
		if not self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_invoke.jinglun = function(self,data)
	local target = data:toPlayer()
	if target then
		return self:isFriend(target)
	end
end

sgs.ai_fill_skill.saying = function(self)
	local es = self:sortByKeepValue(self.player:getEquips())
	for _,h in sgs.list(es)do
		if self.player:getMark("saying_juguan_remove_analeptic_lun")<1 then
			local dc = dummyCard("analeptic")
			dc:setSkillName("saying")
			local d = self:aiUseCard(dc)
			if d.card then
				self.sayingTo = d.to
				return sgs.Card_Parse("@SayingCard="..h:toString()..":analeptic")
			end
		end
		if self.player:getMark("saying_juguan_remove_peach_lun")<1 then
			local dc = dummyCard("peach")
			dc:setSkillName("saying")
			local d = self:aiUseCard(dc)
			if d.card then
				self.sayingTo = d.to
				return sgs.Card_Parse("@SayingCard="..h:toString()..":peach")
			end
		end
	end
	local hs = self:sortByKeepValue(self.player:getHandcards())
	for _,h in sgs.list(hs)do
		if h:getTypeId()==3 and self.player:getMark("saying_juguan_remove_slash_lun")<1
		and h:isAvailable(self.player) and self:aiUseCard(h).card then
			local dc = dummyCard()
			dc:setSkillName("saying")
			local d = self:aiUseCard(dc)
			if d.card then
				self.sayingTo = d.to
				return sgs.Card_Parse("@SayingCard="..h:toString()..":slash")
			end
		end
	end
end

sgs.ai_skill_use_func["SayingCard"] = function(card,use,self)
	if self.sayingTo then
		use.card = card
		use.to = self.sayingTo
	end
end

sgs.ai_use_value.SayingCard = 5.4
sgs.ai_use_priority.SayingCard = 6.8

function sgs.ai_cardsview.saying(self,class_name,player)
   	local cn = patterns(class_name)
	if player:getMark("saying_juguan_remove_"..cn.."_lun")>0 then return end
	if class_name=="Slash" or class_name=="Jink" then
		local cards = sgs.QList2Table(player:getCards("h"))
		self:sortByKeepValue(cards)
		for _,c in sgs.list(cards)do
			if c:getTypeId()==3 and c:isAvailable(player) then
				return "@SayingCard="..c:toString()..":"..cn
			end
		end
	else
		local cards = sgs.QList2Table(player:getEquips())
		self:sortByKeepValue(cards)
		for _,c in sgs.list(cards)do
			return "@SayingCard="..c:toString()..":"..cn
		end
	end
end

sgs.ai_fill_skill.jiaohao = function(self)
	return sgs.Card_Parse("@JiaohaoCard=.")
end

sgs.ai_skill_use_func["JiaohaoCard"] = function(card,use,self)
	local mc = self:getMaxCard()
	if mc:getNumber()>9 then
		self:sort(self.enemies)
		for _,to in sgs.list(self.enemies)do
			if self.player:canPindian(to) and to:getEquips():length()<=self.player:getEquips():length() then
				use.to:append(to)
				use.card = card
				return
			end
		end
	end
	if self:getOverflow()>1 then
		self:sort(self.friends_noself,nil,true)
		for _,to in sgs.list(self.friends_noself)do
			if self.player:canPindian(to) and to:getEquips():length()<=self.player:getEquips():length() then
				use.to:append(to)
				use.card = card
				return
			end
		end
	end
end

sgs.ai_use_value.JiaohaoCard = 5.4
sgs.ai_use_priority.JiaohaoCard = 4.8

sgs.ai_skill_invoke.jiaohao = function(self,data)
	local target = data:toPlayer()
	if target then
		return self:isFriend(target)
	end
end

sgs.ai_fill_skill.shimou = function(self)
	return sgs.Card_Parse("@ShimouCard=.")
end

sgs.ai_skill_use_func["ShimouCard"] = function(card,use,self)
	if self.player:getChangeSkillState("shimou")==1 then
		local n = 999
		for _,p in sgs.list(self.room:getAlivePlayers())do
			if p:getHandcardNum()<n then n = p:getHandcardNum() end
		end
		for _,p in sgs.list(self.friends)do
			if p:getHandcardNum()<=n and p:getHandcardNum()<5 then
				use.card = card
				break
			end
		end
	else
		local n = 0
		for _,p in sgs.list(self.room:getAlivePlayers())do
			if p:getHandcardNum()>n then n = p:getHandcardNum() end
		end
		for _,p in sgs.list(self.enemies)do
			if p:getHandcardNum()>=n and p:getHandcardNum()>5 then
				use.card = card
				break
			end
		end
	end
end

sgs.ai_use_value.ShimouCard = 5.4
sgs.ai_use_priority.ShimouCard = 3.8

sgs.ai_skill_askforag.shimou = function(self,card_ids)
	self.shimouTo = nil
	for _,id in sgs.list(card_ids)do
		local cn = sgs.Sanguosha:getCard(id):objectName()
		local dc = dummyCard(cn)
		dc:setSkillName("shimou")
		local d = self:aiUseCard(dc)
		if d.card then
			self.shimouTo = d.to
			local pnts = self.player:property("shimouPN"):toString():split(":")
			local tp = BeMan(self.room,pnts[2])
			for _,p in sgs.list(d.to)do
				if tp:canUse(dc,p) then continue end
				cn = false
				break
			end
			if cn then
				return id
			end
		end
	end
end

sgs.ai_skill_use["@@shimou!"] = function(self,prompt)
	local tps = {}
	for _,to in sgs.list(self.shimouTo or {})do
		table.insert(tps,to:objectName())
	end
	local pnts = self.player:property("shimouPN"):toString():split(":")
	return "@ShimouCard=.:"..pnts[#pnts].."->"..table.concat(tps,"+")
end

sgs.ai_skill_playerchosen.bizuo = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for i,target in sgs.list(destlist)do
		if i<#destlist/2 and self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_invoke.lieji = function(self,data)
	return true
end

sgs.ai_fill_skill.quzhou = function(self)
	return sgs.Card_Parse("@QuzhouCard=.:slash")
end

sgs.ai_skill_use_func["QuzhouCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.QuzhouCard = 5.4
sgs.ai_use_priority.QuzhouCard = 2.8

sgs.ai_skill_use["@@quzhou"] = function(self,prompt)
	for _,id in sgs.list(self.player:getTag("quzhouForAI"):toIntList())do
		local c = sgs.Sanguosha:getCard(id)
		local d = self:aiUseCard(c)
		if d.card then
			local tps = {}
			for _,p in sgs.list(d.to)do
				table.insert(tps,p:objectName())
			end
			return c:toString().."->"..table.concat(tps,"+")
		end
	end
end

sgs.ai_skill_playerchosen.baojia = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for i,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
    for i,target in sgs.list(destlist)do
		if not self:isEnemy(target)
		then return target end
	end
end

sgs.ai_skill_invoke.baojia = function(self,data)
	local damage = data:toDamage()
	if damage.to then
		return self:isFriend(damage.to) and self:isWeak(damage.to)
	end
end

sgs.ai_skill_choice.baojia = function(self,choices,data)
	local items = choices:split("+")
	return items[#items]
end

sgs.ai_fill_skill.douwei = function(self)
	local hs = self:sortByKeepValue(self.player:getHandcards(),nil,"j")
	for _,h in sgs.list(hs)do
		if h:isDamageCard() then
			local dc = dummyCard(h:objectName())
			dc:setSkillName("douwei")
			local d = self:aiUseCard(dc,dummy(nil,99))
			if d.card then
				self.douweitps = {}
				for i,p in sgs.list(d.to)do
					if self.player:inMyAttackRange(p) then
						table.insert(self.douweitps,p)
					end
				end
				if #self.douweitps<1 then continue end
				return sgs.Card_Parse("@DouweiCard="..h:toString())
			end
		end
	end
end

sgs.ai_skill_use_func["DouweiCard"] = function(card,use,self)
	for i,to in sgs.list(self.douweitps)do
		use.to:append(to)
		use.card = card
	end
end

sgs.ai_use_value.DouweiCard = 5.4
sgs.ai_use_priority.DouweiCard = 3.8

sgs.ai_skill_playerchosen.yingjia = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for i,target in sgs.list(destlist)do
		if self:isEnemy(target)
		then return target end
	end
    for i,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
end

sgs.ai_fill_skill.xianju = function(self)
	return sgs.Card_Parse("@XianjuCard=.")
end

sgs.ai_skill_use_func["XianjuCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.XianjuCard = 5.4
sgs.ai_use_priority.XianjuCard = 3.8

sgs.ai_fill_skill.woheng = function(self)
	return sgs.Card_Parse("@WohengCard=.")
end

sgs.ai_skill_use_func["WohengCard"] = function(card,use,self)
	self:sort(self.friends_noself)
	local n = self.player:getMark("&woheng_lun")
	for i,to in sgs.list(self.friends_noself)do
		if n>=0 and self:canDraw(to) then
			use.to:append(to)
			use.card = card
			return
		end
	end
	self:sort(self.enemies)
	for i,to in sgs.list(self.enemies)do
		if n>0 and to:getCardCount()>=n then
			use.to:append(to)
			use.card = card
			return
		end
	end
end

sgs.ai_use_value.WohengCard = 5.4
sgs.ai_use_priority.WohengCard = 6.8

sgs.ai_skill_choice.woheng = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if target and self:isFriend(target) then
		return items[1]
	end
	return items[#items]
end

sgs.ai_skill_use["@@woheng"] = function(self,prompt)
	self:sort(self.friends_noself)
	local n = self.player:getMark("&woheng_lun")
	for i,to in sgs.list(self.friends_noself)do
		if n>=0 and self:canDraw(to) then
			return string.format("@WohengCard=.->%s",to:objectName())
		end
	end
	self:sort(self.enemies)
	for i,to in sgs.list(self.enemies)do
		if n>0 and to:getCardCount()>=n then
			return string.format("@WohengCard=.->%s",to:objectName())
		end
	end
end

sgs.ai_skill_playerchosen.yugui = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist,nil,true)
    for i,target in sgs.list(destlist)do
		if self:isFriend(target)
		then return target end
	end
end

sgs.ai_skill_cardask["yugui1"] = function(self,data)
    local tp = data:toPlayer()
	if self:isFriend(tp)
   	then return true end
    return "."
end

sgs.ai_skill_playerchosen.juchui = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for i,p in sgs.list(destlist)do
		if self:isEnemy(p) and self:isWeak(p)
		and p:getMaxHp()<=self.player:getMaxHp()
		then return p end
	end
    for i,p in sgs.list(destlist)do
		if self:isFriend(p) and p:isWounded()
		and p:getMaxHp()<=self.player:getMaxHp()
		then return p end
	end
end

sgs.ai_skill_choice.juchui = function(self,choices,data)
	local items = choices:split("+")
	local target = data:toPlayer()
	if table.contains(items,"juchui1") then
		if self:isFriend(target) and table.contains(items,"juchui2") then
			return "juchui2"
		end
		return "juchui1"
	end
	return items[1]
end

sgs.ai_skill_playerchosen.thlinjie = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
    for i,p in sgs.list(destlist)do
		if self:isEnemy(p) and self:damageIsEffective(p,"N",self.player)
		then return p end
	end
    for i,p in sgs.list(destlist)do
		if not self:isFriend(p) and self:damageIsEffective(p,"N",self.player)
		then return p end
	end
    for i,p in sgs.list(destlist)do
		if self:isEnemy(p) 
		then return p end
	end
    for i,p in sgs.list(destlist)do
		if p:getMark("jianghuoDamage")<1
		then return p end
	end
	return destlist[#destlist]
end

sgs.ai_fill_skill.zhanpan = function(self)
	return self:canDraw() and sgs.Card_Parse("@ZhanpanCard=.")
end

sgs.ai_skill_use_func["ZhanpanCard"] = function(card,use,self)
	use.card = card
end

sgs.ai_use_value.ZhanpanCard = 5.4
sgs.ai_use_priority.ZhanpanCard = 6.8

sgs.ai_skill_playerschosen.tiancheng = function(self,players)
	local destlist = sgs.QList2Table(players) -- 将列表转换为表
	self:sort(destlist)
	local tps = {}
    for i,p in sgs.list(destlist)do
		if self:isFriend(p) and p:getHandcardNum()-self.player:getHandcardNum()>3
		then table.insert(tps,p) end
	end
    for i,p in sgs.list(destlist)do
		if self:isFriend(p) and not table.contains(tps,p)
		 and self.player:getHandcardNum()-p:getHandcardNum()<3
		then table.insert(tps,p) end
	end
	return tps
end

sgs.ai_skill_invoke.duhai = function(self,data)
	local use = data:toCardUse()
	if use.from then
		return self:isEnemy(use.from)
	end
end

sgs.ai_fill_skill.lingse = function(self)
	for _,h in sgs.list(self:sortByKeepValue(self.player:getCards("he")))do
		return sgs.Card_Parse("@LingseCard="..h:getEffectiveId())
	end
end

sgs.ai_skill_use_func["LingseCard"] = function(card,use,self)
	self:sort(self.enemies)
    for i,p in sgs.list(self.enemies)do
		if p:getCardCount()>1 then
			use.card = card
			use.to:append(p)
			return
		end
	end
	self:sort(self.friends_noself,nil,true)
    for i,p in sgs.list(self.friends_noself)do
		if p:getCardCount()>1 then
			use.card = card
			use.to:append(p)
			return
		end
	end
end

sgs.ai_use_value.LingseCard = 5.4
sgs.ai_use_priority.LingseCard = 3.8











