sgs.ai_chat = {}

function AIChat(p)
	if p and p:getState()~="robot" then return end
	return global_delay>0 and sgs.ai_humanized--sgs.AIChat
end

sgs.ai_event_callback[sgs.Death].stupid_lord=function(self,player,data)
	if not AIChat(player) then return end
	local damage=data:toDeath().damage
	local chat = {
		"2B了吧",
		"我那么忠诚，竟落得如此下场....",
		"我为主上出过力，呃啊.....",
		"主要臣死，臣不得不死",
		"昏君！",
		"还有更2的吗",
		"真的很无语"
	}
	if damage and damage.from and damage.from:isLord()
	and self.role=="loyalist"and damage.to==player
	then damage.to:speak(chat[math.random(1,#chat)]) end
end

sgs.ai_event_callback[sgs.Dying].fuck_renegade=function(self,player,data)
	local dying = data:toDying()
	if dying.who~=player or not AIChat(player) or math.random()>0.5
	or player:aliveCount()-#self.enemies<2 then return end
	local chat = {
		"999...999...",
		"来捞一下啊...."
	}
	if self.role~="renegade" and sgs.playerRoles["renegade"]>0 then
		table.insert(chat,"9啊，不9就输了")
		table.insert(chat,"小内你还不救，要崩盘了")
		table.insert(chat,"没戏了小内不出手全部托管吧")
		table.insert(chat,"小内，我死了你也赢不了")
	end
	if self:getAllPeachNum()+player:getHp()<1
	and #self.friends_noself>0 then
		table.insert(chat,"寄")
		table.insert(chat,"要寄.....")
		table.insert(chat,"还有救吗.....")
	end
	if #self.enemies<player:aliveCount()-1 then
		table.insert(chat,"不要见死不救啊.....")
		table.insert(chat,"我还不能死.....")
		table.insert(chat,"6了6了")
		table.insert(chat,"6")
	end
	player:speak(chat[math.random(1,#chat)])
end

sgs.ai_event_callback[sgs.DamageForseen].kylin_bow = function(self,player,data)
	local damage = data:toDamage()
	if AIChat(player) and damage.card and damage.from
	and damage.card:isKindOf("Slash") and damage.from:hasWeapon("KylinBow")
	and (player:getOffensiveHorse() or player:getDefensiveHorse())
	and self:isEnemy(damage.from) and math.random()<0.6 then
		local chat = {
			"我靠，5弓",
			"敢杀我马？",
			"我的宝驹.....",
			"蛤！我的千里驹",
			"我马药丸...."
		}
		player:speak(chat[math.random(1,#chat)])
	end
end

sgs.ai_event_callback[sgs.CardFinished].analeptic = function(self,player,data)
	if not AIChat() then return end
	local use = data:toCardUse()
	for _,p in sgs.list(self.room:getAlivePlayers())do
		if math.random()>0.95 and p:getState()=="robot"
		then p:speak("<#"..math.random(1,56).."#>") end
	end
	if use.card:isKindOf("Analeptic") and player:getPhase()<=sgs.Player_Play
	and use.card:getSkillName()~="zhendu" then
		local chat = {
			"!",
			"不要吓我..."
		}
		for _,p in sgs.qlist(self.room:getOtherPlayers(player))do
			if p:getState()=="robot" and not self:isFriend(p)
			and math.random()<0.3 then
				if p:getLostHp()<1 then
					table.insert(chat,"喜闻乐见")
					table.insert(chat,"我满血，不慌")
					table.insert(chat,"来呀，来砍我呀，我满血")
					table.insert(chat,"系兄弟就来啃我！")
				end
				if p:getHp()<3 and #self.enemies>1 then
					table.insert(chat,"队友呢，有桃没？")
					table.insert(chat,"有点慌，但有队友应该可以...")
				elseif p:getHp()>3 then
					table.insert(chat,"我血多，不怕")
					table.insert(chat,"绝对带不走")
					table.insert(chat,"我菊花一紧")
				end
				if p:getHandcardNum()>0 then
					table.insert(chat,"打不中的")
					table.insert(chat,"猜猜我有没有闪")
					table.insert(chat,"不要砍我，我有"..PatternsCard("jink"):getLogName())
					table.insert(chat,"没闪，但是有"..PatternsCard("peach"):getLogName())
				else
					if player:canSlash(p) then
						table.insert(chat,"敢不敢放过我")
						table.insert(chat,"空城..药丸...")
						table.insert(chat,"不要杀我，我给你钱（先打欠条）")
					else
						table.insert(chat,"我没牌，可惜了你")
						table.insert(chat,"哎呀，打不到，哈哈哈")
						table.insert(chat,"前排围观，出售爆米花，矿泉水，花生，瓜子...")
					end
				end
				p:speak(chat[math.random(1,#chat)])
			end
		end
	elseif use.card:isKindOf("Crossbow") then
		local chat = {
			"你小子早该突突了！",
			"哒哒哒！",
			"杀！杀！杀！",
			"AK来了~~",
			"随机送走一位童鞋",
			"看我反手掏出这大家伙",
			"满手的杀哦"
		}
		if player:getState()=="robot"
		and (self:getCardsNum("Slash")>1 or math.random()<0.4 and player:getHandcardNum()>1)
		then player:speak(chat[math.random(1,#chat)]) end
		for _,p in sgs.qlist(self.room:getOtherPlayers(player))do
			if p:getState()=="robot" and math.random()<0.3
			and not self:isFriend(p,player) then
				chat = {"额，AK出现了","有点危险","得找机会拿过来","好家伙"}
				if player:canSlash(p) then
					if p:getLostHp()<1 then
						table.insert(chat,"我满血，不慌")
						table.insert(chat,"来呀，来砍我呀，我满血")
					end
					if getCardsNum("Slash",player,p)<1
					and player:getHandcardNum()<5 then
						table.insert(chat,"我赌你没有杀")
						table.insert(chat,"断杀，啊哈哈哈哈")
						table.insert(chat,"你这趁手的AK怎么没有弹药呢，哈哈哈")
					else
						table.insert(chat,"不要砍我....")
						table.insert(chat,"我是你队友，不要打错了")
						table.insert(chat,"其实我是他们的卧底")
						table.insert(chat,"我可以改邪归正，弃暗投明")
						table.insert(chat,"我愿归降，求您放过我...")
					end
				else
					table.insert(chat,"可惜够不着我呢~~~")
					table.insert(chat,"哎呀呀，距离不够，哈哈哈")
					table.insert(chat,"打不着~~打不着~~")
				end
				p:speak(chat[math.random(1,#chat)])
			end
		end
	elseif use.card:isKindOf("Peach") then
		for chat,p in sgs.qlist(use.to)do
			if p==use.from or p:getHp()<1
			or p:getState()~="robot" then continue end
			if use.from:isFemale() and math.random()<0.2
			and p:getGender()~=use.from:getGender()
			and use.from:getState()=="robot" then
				use.from:speak("复活吧，我的勇士")
				p:speak("为你而战，我的女王")
			elseif math.random()<0.5 then
				chat = {
					"大人功德无量！",
					"杀不死的我，会更加强大！",
					"哼，我还是有队友的",
					"还好有队友",
					"救我苟命，不胜感激",
					"天命在我，不该绝矣",
					"哈哈，活了",
					"差点无了.....",
					"活下来了....",
					"谢了.....",
					"还好还好"
				}
				p:speak(chat[math.random(1,#chat)])
			end
		end
	elseif use.card:isKindOf("OffensiveHorse")
	and player:getState()=="robot" then
		for _,p in sgs.qlist(self.room:getOtherPlayers(player))do
			if self:isEnemy(player,p) and player:distanceTo(p,1)==2 and math.random()<0.2
			then player:speak("妖人"..p:screenName().."你往哪里跑") return end
		end
	end
end

sgs.ai_event_callback[sgs.PreCardUsed].CardUse = function(self,player,data)
	if not AIChat() then return end
	local use = data:toCardUse()
	if player:getState()=="robot" then
		local ac = sgs.ai_chat[use.card:objectName()]
		if player:isFemale() then ac = sgs.ai_chat[use.card:objectName().."_female"] or ac end
		if type(ac)=="table" and math.random()<0.6
		then player:speak(ac[math.random(1,#ac)])
		elseif type(ac)=="function" then ac(self)
		elseif use.card:getSkillName():match("luanji") then
			if not sgs.ai_yuanshao_ArcheryAttack
			or #sgs.ai_yuanshao_ArcheryAttack<1 then
				sgs.ai_yuanshao_ArcheryAttack = {
					"此身，为剑所成",
					"血如钢铁，心似琉璃",
					"跨越无数战场而不败",
					"未尝一度被理解",
					"亦未尝一度有所得",
					"剑之丘上，剑手孤单一人，沉醉于辉煌的胜利",
					"铁匠孑然一身，执著于悠远的锻造",
					"因此，此生没有任何意义",
					"那么，此生无需任何意义",
					"这身体，注定由剑而成"
				}
			end
			player:speak(sgs.ai_yuanshao_ArcheryAttack[1])
			table.remove(sgs.ai_yuanshao_ArcheryAttack,1)
		end
	end
	if use.card:isDamageCard()
	and use.to:length()>player:aliveCount()/2 then
		local chat = {
			"我靠，AOE",
			"喜闻乐见",
			"蛤！"
		}
		self.aoeTos = use.to
		for _,p in sgs.qlist(self.room:getOtherPlayers(use.from))do
			if math.random()<0.8 or p:getState()~="robot" then continue end
			if use.to:contains(p) and self:aoeIsEffective(use.card,p,use.from) then
				if self:isWeak(p) or p:isKongcheng() then
					table.insert(chat,"不要哇")
					table.insert(chat,"不要收割我....")
					table.insert(chat,"有点慌")
				else
					table.insert(chat,"血多，不慌")
				end
				if use.from:usedTimes(use.card:getClassName())>1 then
					table.insert(chat,"哪来这么多AOE啊")
					table.insert(chat,"你特么....")
					table.insert(chat,"怎么还有....")
					table.insert(chat,"还来....")
				end
			else
				table.insert(chat,"呃哈哈")
				table.insert(chat,"此计伤不到我")
				table.insert(chat,"此小计尔")
				table.insert(chat,"哎呀打不动")
				table.insert(chat,"坐山观斗虎")
			end
			p:speak(chat[math.random(1,#chat)])
		end
		self.aoeTos = nil
	elseif use.card:isKindOf("Slash") then
		local chat ={
			"您老悠着点儿阿",
			"泥玛杀我，你等着阿",
			"再杀！老子和你拼命了"
		}
		for _,p in sgs.qlist(use.to)do
			if math.random()>0.3 or p:getState()~="robot" then continue end
			if sgs.ai_role[p:objectName()]=="neutral" then
				table.insert(chat,"盲狙一时爽啊,我泪奔")
				table.insert(chat,"我次奥，盲狙能不能轻点？")
				if use.from:usedTimes("Analeptic")>0 then
					table.insert(chat,"喝醉了吧，乱砍人？")
				end
			end
			if p:getRole()~="lord"
			and self:hasCrossbowEffect(use.from) then
				table.insert(chat,"杀得我也是醉了。。。")
				table.insert(chat,"果然是连弩降智商....")
				table.insert(chat,"杀死我也没牌拿，真2")
			end
			if use.from:getRole()=="lord"
			and sgs.playerRoles.loyalist>0
			and sgs.ai_role[p:objectName()]~="rebel" then
				table.insert(chat,"尼玛眼瞎啊，老子是忠！")
				table.insert(chat,"主公别打我，我是忠")
				table.insert(chat,"主公别开枪，自己人")
				table.insert(chat,"再杀我，你会裸")
			end
			p:speak(chat[1+(os.time()%#chat)])
		end
	end
end

sgs.ai_event_callback[sgs.EventPhaseStart].luanwu = function(self,player,data)
	if not AIChat() then return end
	if player:getPhase()==sgs.Player_Play and self.player:getMark("@chaos")>0
	and self.player:hasSkill("luanwu") then
		local chat = {
			"乱一个，乱一个",
			"要乱了",
			"要死....",
			"完了，没杀"
		}
		local chat1 = {
			"不要紧张",
			"乱世随时开始",
			"月黑风高夜，杀人放火时！",
			"准备好了吗？"
		}
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if p~=player and p:getState()=="robot" and math.random()<0.2
			then p:speak(chat[math.random(1,#chat)])
			elseif p==player and p:getState()=="robot" and math.random()<0.1
			then p:speak(chat1[math.random(1,#chat1)]) end
		end
	end
	if player:getState()~="robot" then return end
	local chat = {
		"有货，可以来搞一下",
		"有闪有黑桃",
		"看我眼色行事",
		"没闪,忠内不要乱来",
		"不爽，来啊！砍我啊",
		"求杀求砍求蹂躏",
	}
	if player:getPhase()==sgs.Player_Finish and not player:isKongcheng()
	and player:hasSkills("leiji|nosleiji|olleiji") and os.time()%10<4
	then player:speak(chat[1+(os.time()%#chat)]) end
	if player:getPhase()==sgs.Player_Start and math.random()<0.4
	and sgs.playerRoles["renegade"]+sgs.playerRoles["loyalist"]<1
	and sgs.playerRoles["rebel"]>=2 then
		if self.role=="rebel" then
			chat = {
				"大家一起围观主公",
				"不要一下弄死了，慢慢来",
				"主公投降吧，免受皮肉之苦",
				"速度，一人一下弄死",
				"怎么成光杆司令了啊",
				"哈哈哈主公阿",
				"包养主公了",
				"投降给全尸"
			}
		else
			if sgs.turncount>4 then
				chat = {
					"看我一人包围你们全部......",
					"这就是所谓一人包围全场",
					"已经是最后的希望了",
					"最后的希望么？",
					"纵死，我亦无惧",
					"我要杀出重围",
					"我将不屈服",
					"（苦笑）",
					"哈哈哈，这是我最后的荣光了"
				}
			else
				chat = {
					"啊这",
					"怎么全都死了",
					"我竟进入如此境地",
					"看我一人包围你们全部......",
					"不好，被包围了",
					"不要轮我",
					"（苦笑）"
				}
			end
		end
		player:speak(chat[math.random(1,#chat)])
	end
	if player:getPhase()==sgs.Player_RoundStart
	and self.room:getMode()=="08_defense"
	and math.random()<0.3 then
		local kingdom = self.player:getKingdom()
		local chat1 = {
			"无知小儿，报上名来，饶你不死！",
			"剑阁乃险要之地，诸位将军须得谨慎行事。",
			"但看后山火起，人马一齐杀出！"
		}
		local chat2 = {
			"嗷~！",
			"呜~！",
			"咕~！",
			"呱~！",
			"发动机已启动，随时可以出发——"
		}
		if kingdom=="shu" then
			table.insert(chat1,"人在塔在！")
			table.insert(chat1,"汉室存亡，在此一战！")
			table.insert(chat1,"星星之火，可以燎原")
			table.insert(chat2,"红色！")
		elseif kingdom=="wei" then
			table.insert(chat1,"众将官，剑阁去者！")
			table.insert(chat1,"此战若胜，大业必成！")
			table.insert(chat1,"一切反动派都是纸老虎")
			table.insert(chat2,"蓝色！")
		end
		if string.find(self.player:getGeneral():objectName(),"baihu") then table.insert(chat2,"喵~！") end
		if string.find(self.player:getGeneral():objectName(),"jiangwei") then  --姜维
			table.insert(chat1,"白水地狭路多，非征战之所，不如且退，去救剑阁")
			table.insert(chat1,"若剑阁一失，是绝路也。")
			table.insert(chat1,"今四面受敌，粮道不同，不如退守剑阁，再作良图。")
		elseif string.find(self.player:getGeneral():objectName(),"dengai") then  --邓艾
			table.insert(chat1,"剑阁之守必还赴涪，则会方轨而进")
			table.insert(chat1,"剑阁之军不还，则应涪之兵寡矣")
			table.insert(chat1,"以愚意度之，可引一军从阴平小路出汉中德阳亭")
			table.insert(chat1,"用奇兵径取成都，姜维必撤兵来救，将军乘虚就取剑阁，可获全功")
		elseif string.find(self.player:getGeneral():objectName(),"simayi") then  --司马懿
			table.insert(chat1,"吾前军不能独当孔明之众，而又分兵为前后，非胜算也")
			table.insert(chat1,"不如留兵守上邽，余众悉往祁山")
			table.insert(chat1,"蜀兵退去，险阻处必有埋伏，须十分仔细，方可追之。")
		elseif string.find(self.player:getGeneral():objectName(),"zhugeliang") then --诸葛亮
			table.insert(chat1,"老臣受先帝厚恩，誓以死报")
			table.insert(chat1,"今若内有奸邪，臣安能讨贼乎？")
			table.insert(chat1,"吾伐中原，非一朝一夕之事")
			table.insert(chat1,"正当为此长久之计")
		end
		if string.find(self.player:getGeneral():objectName(),"machine")
		then p:speak(chat2[math.random(1,#chat2)])
		else p:speak(chat1[math.random(1,#chat1)]) end
	end
	if isRolePredictable() then
	elseif player:getPhase()==sgs.Player_RoundStart
	and math.random()<0.2 then
		local friend_name,enemy_name
		for _,p in sgs.qlist(self.room:getOtherPlayers(self.player))do
			if self:isFriend(p) and math.random()<0.5 then friend_name = p:getLogName()
			elseif self:isEnemy(p) and math.random()<0.5 then enemy_name = p:getLogName() end
		end
		local chat1 = {
			"要记住该跳就跳，不要装身份",
			"装什么身份，跳啊",
			"到底谁是内啊？",
			"都在这装呢？",
			"还在这装呢"
		}
		local quick = {
			"都快点，我还要去吃饭呢",
			"都快点，打完这局我要去取快递",
			"都快点，我要去做面膜的",
			"都快点，打完这局我要去约会呢",
			"都快点，打完这局我要去跪搓衣板",
			"都快点，打完这局我要去混班了",
			"都快点，打完这局我要睡觉了",
			"都快点，打完这局我要去打代码",
			"都快点，打完这局我要去撸啊撸",
			"都快点，打完这局我要去打电动了",
			"都快点，我要开新局",
		}
		local role1 = {
			"孰忠孰反，吾早已明辨",
			"都是反，怎么打！"
		}
		local role2 = {
			"当忠臣嘛，个人能力要强",
			"装个忠我容易嘛我",
			"这主坑内，跳反了",
			"这主坑内，投降算了"
		}
		local role3 = {
			"反贼都集火啊！集火！",
			"我们根本没有输出",
			"输出，加大输出啊！",
			"对这种阵容，我已经没有希望了"
		}
		chat = {}
		if friend_name then
			table.insert(role1,"忠臣"..friend_name.."，你是在坑我吗？")
			table.insert(role1,friend_name.."你不会是奸细吧？")
		end
		if enemy_name then
			table.insert(chat1,enemy_name.."你小子怎么了啊")
			table.insert(chat1,"游戏可以输，你"..enemy_name.."必须死！")
			table.insert(chat1,enemy_name.."你这样坑队友，连我都看不下去了")
		end
		if math.random()<0.2 then
			table.insert(chat,quick[math.random(1,#quick)])
		end
		if math.random()<0.3 then
			table.insert(chat,chat1[math.random(1,#chat1)])
		end
		if player:isLord() then table.insert(chat,role1[math.random(1,#role1)])
		elseif sgs.ai_role[player:objectName()]=="loyalist"
		or sgs.ai_role[player:objectName()]=="renegade" and math.random()<0.2
		then table.insert(chat,role2[math.random(1,#role2)])
		elseif sgs.ai_role[player:objectName()]=="rebel"
		or sgs.ai_role[player:objectName()]=="renegade" and math.random()<0.2
		then table.insert(chat,role3[math.random(1,#role3)]) end
		if #chat>0 and sgs.turncount>=2 then
			player:speak(chat[math.random(1,#chat)])
		end
	end
	if player:getPhase()==sgs.Player_Play
	and player:hasSkill("jieyin") then
		chat = {
			"香香睡我",
			"香香救我",
			"香香，我快没命了"
		}
		local chat1 = {
			"牌不够啊",
			"哼，考虑考虑",
			"让我斟酌斟酌"
		}
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if p~=player and p:getState()=="robot" and self:isFriend(p)
			and p:isMale() and self:isWeak(p) then p:speak(chat[math.random(1,#chat)])
			elseif p==player and p:getState()=="robot" and math.random()<0.1
			then p:speak(chat1[math.random(1,#chat1)]) end
		end
	end
end

sgs.ai_event_callback[sgs.ChoiceMade].state = function(self,player,data)
	for _,p in sgs.qlist(self.room:getAlivePlayers())do
		if math.random()>0.97 and AIChat(p)
		then p:speak("<#"..math.random(1,56).."#>") end
	end
end

sgs.ai_event_callback[sgs.DrawNCards].screen = function(self,player,data)
	local draw = data:toDraw()
	if draw.reason~="InitialHandCards" then return end
	local sn = player:screenName()
	if sn:match("神小杀")
	and AIChat(player) then
		local speaks = {
			sn.."准备就绪",
			sn.."随时可以开始",
			sn.."程序加载完成",
			sn.."准备好了",
			sn.."进入游戏成功"
		}
		player:speak(speaks[math.random(1,#speaks)])
	end
end

sgs.ai_event_callback[sgs.StartJudge].isBad = function(self,player,data)
	local judge = data:toJudge()
	if judge:isBad() and math.random()<0.6 then
		local dc = dummyCard(judge.reason)
		if not(dc and dc:isDamageCard()) then return end
		local chat = {
			"我靠，天谴之子",
			"竞是如此",
			"蛤！炸了",
			"劈哩批哩喽"
		}
		if player:getKingdom()=="wei" then
			table.insert(chat,"大魏招雷术")
			table.insert(chat,"大魏引雷法")
			table.insert(chat,"这就是大魏")
		end
		for _,p in sgs.qlist(self.room:getAlivePlayers())do
			if math.random()>0.77 and AIChat(p) then
				if self:isFriend(p,player) then
					table.insert(chat,"不可能！")
					table.insert(chat,"不可以！")
					table.insert(chat,"那种事情不要啊.....")
					if #self.friends_noself>0 then
						table.insert(chat,"有改判的吗.....")
						table.insert(chat,"判官，我要重新裁决.....")
					end
				else
					table.insert(chat,"该！")
					table.insert(chat,"哦嚯")
					table.insert(chat,"早该劈劈了")
					table.insert(chat,"一次不够，下次我也放雷")
					table.insert(chat,"你小子做了什么伤天害理之事啊")
					table.insert(chat,"这就是天弃之子")
				end
				p:speak(chat[math.random(1,#chat)])
			end
		end
	end
end

function SmartAI:speak(cardtype,isFemale)
	if AIChat(self.player) then else return end
	local ac = sgs.ai_chat[cardtype]
	if type(ac)=="function" then ac(self)
	elseif type(ac)=="table" then
		if isFemale then ac = sgs.ai_chat[cardtype.."_female"] or ac end
		if self.player:getPhase()<=sgs.Player_Play then self.room:getThread():delay(math.random(global_delay*0.5,global_delay*1.5))
		elseif self.player:hasFlag("Global_Dying") then self.room:getThread():delay(math.random(global_delay,global_delay*2)) end
		self.player:speak(ac[math.random(1,#ac)])
		return true
	end
end

sgs.ai_chat.blade={
	"这把刀就是我爷爷传下来的，上斩逗比，下斩傻逼！",
	"尚方宝刀，专戳贱逼!"
}

sgs.ai_chat.no_peach_female = {
	"妾身不玩了！",
	"哇啊欺负我，不玩了" ,
	"放下俗物，飘飘升仙" ,
	"你们好意思欺负我一个女孩子吗....",
	"哼，本姑娘不伺候了"
}

sgs.ai_chat.no_peach = {
	"yoooo少年，不溜等什么",
	"摆烂.....",
	"看我主动走小道",
	"不挣扎了",
	"有桃不吃，先走了",
	"开下一局，走了",
	"围观我？我直接超脱",
	"投个降先"
}

sgs.ai_chat.yiji={
	"再用力一点",
	"再来再来",
	"要死了啊!"
}

sgs.ai_chat.null_card={
	"想卖血？",
	"我不想让你受伤！",
	"我不喜欢你扣血.....",
	"没事掉什么血啊",
	"我来保护你！！",
	"你这血我扣下了",
	"你不能受到1点伤害",
	"只有我可怜你哦~~~",
	"精血很贵的，还是留着吧",
	"看，只有我在意你，不让你受伤",
	"哼哼!"
}

sgs.ai_chat.snatch_female = {
	"啧啧啧，来帮你解决点牌吧",
	"叫你欺负人!" ,
	"看你就不是好人",
	"你留着牌就是祸害"
}

sgs.ai_chat.snatch = {
	"yoooo少年，不来一发么",
	"果然还是看你不爽",
	"你的牌太多辣",
	"摸你一下看看",
	"我看你霸气外露，不可不防啊"
}

sgs.ai_chat.dismantlement_female = sgs.ai_chat.snatch_female

sgs.ai_chat.dismantlement = sgs.ai_chat.snatch

sgs.ai_chat.dismantlement_female = sgs.ai_chat.snatch_female

sgs.ai_chat.zhujinqiyuan = sgs.ai_chat.snatch

sgs.ai_chat.zhujinqiyuan_female = sgs.ai_chat.snatch_female

sgs.ai_chat.respond_hostile={
	"擦，小心菊花不保",
	"内牛满面了",
	"哎哟我去"
}

sgs.ai_chat.friendly={
	"。。。"
}

sgs.ai_chat.respond_friendly={
	"谢了。。。"
}

sgs.ai_chat.duel_female={
	"不要拒绝哦",
	"小女子我也要亲自上场了",
	"哼哼哼，怕了吧"
}

sgs.ai_chat.duel={
	"不要回避！",
	"来直面挑战吧！",
	"哈哈哈，我的杀一定比你多！",
	"来吧！像个勇士一样决斗吧！"
}

sgs.ai_chat.ex_nihilo={
	"哎哟运气好",
	"手气不错",
	"无中复无中！？",
	"抽个大牌",
	"哈哈哈哈哈"
}

sgs.ai_chat.dongzhuxianji = sgs.ai_chat.ex_nihilo

sgs.ai_chat.collateral_female={
	"将军，帮帮妾身吧",
	"就替妾身手刃他吧",
	"这人欺负我，打他"
}

sgs.ai_chat.collateral={
	"你的刀，就是我的刀",
	"你的剑，就是我的剑！",
	"替我————杀了他",
	"借汝之刀一用！"
}

sgs.ai_chat.amazing_grace_female={
	"让我看看，有什么好东西呢",
	"人人有份哟",
	"风调雨顺，五谷丰登",
	"丰收喽~~~~"
}

sgs.ai_chat.amazing_grace={
	"开仓，放粮！",
	"俺颇有家资",
	"一人一口，分而食之",
	"来分牌喽！"
}

sgs.ai_chat.supply_shortage={
	"嘻嘻，不给你摸",
	"你最好不是摸牌白",
	"看我断你口粮",
	"做个饿死鬼去吧！"
}

sgs.ai_chat.supply_shortageIsGood={
	"果然要天过",
	"嚯~~~~",
	"天可怜见",
	"哈哈哈哈哈哈哈哈哈",
	"废兵也就这样了"
}

sgs.ai_chat.collateralNoslash_female={
	"将军，妾身帮不了你",
	"要杀没有，这刀本宫赏给你了",
	"无杀，给你，哼"
}

sgs.ai_chat.collateralNoslash={
	"赏你了，还不快快跪谢！",
	"手残了，无法助你一臂之力",
	"孙子欸，敢收我的刀！",
	"我的趁手宝刀.....",
	"我帮不了你",
	"汝妹啊，吾刀！"
}

sgs.ai_chat.jijiang_female={
	"别指望下次我会帮你哦",
	"只是杀多了，消耗一点"
}

sgs.ai_chat.jijiang={
	"主公，我来啦",
	"我为主公出个力！"
}

sgs.ai_chat.eight_diagramIsGood={
	"哈哈，打不中",
	"八卦闪~！",
	"判红",
	"红色！！！"
}

sgs.ai_chat.judgeIsGood={
	"献祭GK木琴换来的",
	"这就是天意",
	"天助我，宵小不足为惧",
	"好判定",
	"判得好",
	"此天命不可违也，哈哈哈哈",
	"好耶!"
}

sgs.ai_chat.noJink={
	"有桃么!有桃么？",
	".......！",
	"我大意了",
	"要死"
}

--huanggai
sgs.ai_chat.kurou={
	"有桃么!有桃么？",
	"教练，我想要摸桃",
	"桃桃桃我的桃呢",
	"求桃求连弩各种求"
}

--indulgence
sgs.ai_chat.indulgence={
	"乐",
	"哈哈哈，乐",
	"五方诸神，此乐必中，急急如律令！",
	"你小子，就空过一回吧",
	"耶稣佛祖，不要天过",
	"此乐加之于你，定可一战而擒",
	"妖孽，看我封印你！",
	"关你禁闭！",
	"随机选择一位小盆友"
}

sgs.ai_chat.indulgence_female={
	"要懂得劳逸结合啊~~",
	"不要拒接妾身哦",
	"自娱自乐去吧",
	"休息一下吧"
}

sgs.ai_chat.indulgenceIsGood={
	"哈哈哈，乐不中",
	"信吾者，可上天国",
	"哟，天过",
	"我果然天眷者",
	"呀~~居然是假乐",
	"虚惊一场",
	"果然！",
	"此天助我也！"
}

--leiji
sgs.ai_chat.leiji_jink={
	"我有闪我会到处乱说么？",
	"你觉得我有木有闪啊",
	"哈我有闪"
}

--quhu
sgs.ai_chat.quhu={
	"出大的！",
	"来来来拼点了",
	"我要打虎了",
	"我又要打虎了",
	"谁会是狼呢？",
	"哟，拼点吧"
}

--wusheng to yizhong
sgs.ai_chat.wusheng_yizhong={
	"诶你技能是啥来着？",
	"在杀的颜色这个问题上咱是色盲",
	"咦你的技能呢？"
}

--salvageassault
sgs.ai_chat.daxiang={
	"好多大象！",
	"擦，孟获你的宠物又调皮了",
	"内牛满面啊敢不敢少来点AOE"
}

--xiahoudun
sgs.ai_chat.ganglie_death={
	"菊花残，满地伤。。。"
}

sgs.ai_chat.guojia_weak={
	"擦，再卖血会卖死的",
	"虚了，已经虚了",
	"不敢再卖了诶诶诶"
}

sgs.ai_chat.yuanshao_fire={
	"谁去打119啊",
	"别别别烧了别烧了。。。",
	"又烧啊，饶了我吧。。。",
	"救火啊，救一下啊"
}

--xuchu
sgs.ai_chat.luoyi={
	"不脱光衣服干不过你"
}

sgs.ai_chat.bianshi = {
	"据我观察现在可以鞭尸",
	"鞭他，最后一下留给我",
	"我要刷战功，这个人头是我的",
	"这个可以鞭尸",
	"啊笑死"
}

sgs.ai_chat.bianshi_female = {
	"对面是个美女你们慢点",
	"人人有份，永不落空",
	"美人，来香一个"
}

sgs.ai_chat.usepeach = {
	"不好，这桃里有屎",
	"你往这里面掺了什么？"
}
