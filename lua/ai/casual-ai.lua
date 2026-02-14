sgs.ai_skill_invoke.casual_qingxian = function(self,data)
	return true
end

sgs.ai_skill_choice.casual_qingxian = function(self,choices,data)
	if self:isWounded() then 
		return "casual_recover" 
	end
	return "casual_draw"
end

sgs.ai_skill_invoke.casual_tianzuo = function(self,data)
	local target = self.room:getCurrent()
	if target:getHp()<=2 and self:isFriend(target) then
		return true
	end
	return false
end


sgs.ai_skill_invoke.casual_huimin = function(self,data)
	local room = self.player:getRoom()
	local players = room:getAlivePlayers()
	for _,p in sgs.qlist(players)do
		if p:getHandcardNum()<=self.player:getMark("&tianzuo_mark") then
			return true
		end
	end
	return false
end

sgs.ai_skill_playerchosen["casual_huimin"] = function(self,targets)
	targets = sgs.QList2Table(targets)
	for _,target in sgs.list(targets)do
		if target:isAlive() and self:isFriend(target) then
			return target
		end
	end
	return nil
end

sgs.ai_skill_invoke.casual_kuizhu = function(self,data)
	local target = self.room:getCurrent()
	if target:getHp()<=2 and not self:isFriend(target) then
		return true
	end
	if target:getHp()>2 and self:isFriend(target) then
		return true
	end
	return false
end

sgs.ai_skill_invoke.casual_cuorui = function(self,data)
	local damage = data:toDamage()
	if self:isFriend(damage.to) then
		return false
	end
	return true
end

sgs.ai_skill_invoke.casual_bijin = function(self,data)
	local target = self.room:getCurrent()
	if not self:isFriend(target) then
		return true
	end
	return false
end

sgs.ai_skill_invoke.casual_ruilveDis = function(self,data)
	local target = data:toPlayer()
	if not self:isFriend(target) then
		return true
	end
	return false
end

sgs.ai_skill_invoke.casual_ruilveDraw = function(self,data)
	local target = data:toPlayer()
	if self:isFriend(target) then
		return true
	end
	return false
end

sgs.ai_skill_invoke.casual_jueqing = function(self,data)
	local target = data:toPlayer()
	if not self:isFriend(target) then
		return true
	end
	return false
end

sgs.ai_skill_invoke.casual_ciwei = function(self,data)
	local target = data:toPlayer()
	if not self:isFriend(target) then
		return true
	end
	return false
end

sgs.ai_skill_invoke.casual_luoyan = function(self,data)
	return true
end

sgs.ai_skill_invoke.casual_fuwei = function(self,data)
	local target = data:toPlayer()
	if self:isFriend(target) then return false end
	if target:getHandcardNum()>=4 then return true end
	return false
end


sgs.ai_skill_invoke.casual_tianxing = function(self,data)
	local target = self.room:getCurrent()
	if self:isFriend(target) then
		return true
	end
	return false
end

sgs.ai_skill_playerchosen["casual_shenfu"] = function(self,targets)
	targets = sgs.QList2Table(targets)
	for _,target in sgs.list(targets)do
		if target:isAlive() and not self:isFriend(target) then
			return target
		end
	end
	return targets[1]
end

sgs.ai_skill_invoke.casual_taixuan = function(self,data)
	local len = self.player:getPile("casual_taixuanPile"):length()
	if len>=data:toInt() or (not self.player:getJudgingArea():isEmpty() and len>=2) then
		return true
	end
	return false
end

sgs.ai_skill_invoke.casual_chenglve = function(self,data)
	local target = data:toPlayer()
	if self:isFriend(target) then return false end
	return true
end

sgs.ai_skill_invoke.casual_mingren = function(self,data)
	return true
end

sgs.ai_skill_invoke.casual_zhenliang = function(self,data)
	return true
end

sgs.ai_skill_invoke.casual_zhouxuan = function(self,data)
	return true
end

sgs.ai_skill_invoke.casual_fengji = function(self,data)
	return true
end

sgs.ai_skill_choice.casual_kuanggu = function(self,choices,data)
	if self:isWounded() then 
		return "casual_recover" 
	end
	return "casual_draw"
end