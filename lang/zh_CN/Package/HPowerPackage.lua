--[[********************************************************************
  Ported xxyheaven Hegemony translation; donor license/header retained.
  -- source: TODO/QSanguosha-For-Hegemony-xxyheaven/lang/zh_CN/Package/PowerPackage.lua
*********************************************************************]]
return {
	["heg_power"] = "君临天下·权",
	["heg_power_equip"] = "君临天下·权",

	["heg_cuiyanmaojie"] = "崔琰＆毛玠[国]",
	["&heg_cuiyanmaojie"] = "崔琰毛玠[国]",
	["#heg_cuiyanmaojie"] = "日出月盛",
	["designer:heg_cuiyanmaojie"] = "Virgopaladin（韩旭）",
	["illustrator:heg_cuiyanmaojie"] = "兴游",
	["heg_zhengbi"] = "征辟",
	[":heg_zhengbi"] = "出牌阶段开始时，你可选择：1.选择一名没有势力的角色▶你于此回合内对其使用牌无距离关系的限制且对包括其在内的角色使用牌无次数限制；2.将一张基本牌交给一名有势力的角色▶若其有牌且牌数：为1，其将所有牌交给你；大于1，其将一张不是基本牌的牌或两张基本牌交给你。",
	["@zhengbi"] = "你可以发动“征辟”",
	["@heg_zhengbi"] = "你可以发动“征辟”",
	["@zhengbi-give"] = "征辟：请选择交给%src的两张基本牌，或一张非基本牌",
	["@heg_zhengbi-give"] = "征辟：请选择交给%src的两张基本牌，或一张非基本牌",
	["heg_fengying"] = "奉迎",
	[":heg_fengying"] = "限定技，出牌阶段，你可对你使用对应的实体牌为你的所有手牌的【挟天子以令诸侯】（无目标的限制）→当此牌被使用时，你选择所有与你势力相同的角色。这些角色各将手牌补至X张（X为其体力上限）。",
	["#heg_fengying-after"] = "奉迎[摸牌]",
	
	["heg_yujin"] = "于禁[国]",
	["#heg_yujin"] = "讨暴坚垒",
	["designer:heg_yujin"] = "Virgopaladin（韩旭）",
	["illustrator:heg_yujin"] = "biou09",
	["heg_jieyue"] = "节钺",
	[":heg_jieyue"] = "准备阶段开始时，你可将一张手牌交给一名不为魏势力或没有势力的一名角色▶其选择是否执行军令。若其选择：是，你摸一张牌；否→摸牌阶段，你令额定摸牌数+3。",
	["@jieyue"] = "你可以发动“节钺”，请选择一张手牌交给一名不是是魏势力的角色",

	["heg_wangping"] = "王平[国]",
	["#heg_wangping"] = "键闭剑门",
	["illustrator:heg_wangping"] = "zoo",
	["heg_jianglve"] = "将略",
	[":heg_jianglve"] = "限定技，出牌阶段，你可选择军令▶与你势力相同的其他角色各选择是否执行此军令。你加1点体力上限，回复1点体力。所有选择是的角色各{加1点体力上限，回复1点体力}。你摸X张牌（X为以此法回复过体力的角色数）。",

	["heg_fazheng"] = "法正[国]",
	["#heg_fazheng"] = "蜀汉的辅翼",
	["illustrator:heg_fazheng"] = "黑白画谱",
	["heg_enyuan"] = "恩怨",
	[":heg_enyuan"] = "锁定技，①当你成为【桃】的目标后，若使用者不为你，其摸一张牌。②当你受到伤害后，你令来源选择：1.将一张手牌交给你；2.失去1点体力。",
	["@enyuan-give"] = "恩怨：请选择一张手牌交给%src，或点取消失去1点体力",
	["@heg_enyuan-give"] = "恩怨：请选择一张手牌交给%src，或点取消失去1点体力",
	["heg_xuanhuo"] = "眩惑",
	[":heg_xuanhuo"] = "与你势力相同的其他角色的出牌阶段限一次，其可将一张手牌交给你▶其弃置一张牌▷其选择下列技能中所有角色均没有的一个：“武圣”、“咆哮”、“龙胆”、“铁骑”、“烈弓”、“狂骨”。其于此回合结束或其明置有以此法选择的技能的武将牌之前拥有其以此法选择的技能。",
	["@xuanhuo-choose"] = "眩惑：请选择要获得的技能",
	["@xuanhuo-discard"] = "眩惑：请弃置一张牌",
	["@heg_xuanhuo-choose"] = "眩惑：请选择要获得的技能",
	["@heg_xuanhuo-discard"] = "眩惑：请弃置一张牌",
	["heg_xuanhuoattach"] = "眩惑",
	["&heg_xuanhuoattach"] = "出牌阶段限一次，你可将一张手牌交给法正▶你弃置一张牌▷你选择下列技能中所有角色均没有的一个：“武圣”、“咆哮”、“龙胆”、“铁骑”、“烈弓”、“狂骨”。你于此回合结束或其明置有以此法选择的技能的武将牌之前拥有其以此法选择的技能。",

	["heg_wusheng_xh"] = "武圣",
	["heg_paoxiao_xh"] = "咆哮",
	["heg_longdan_xh"] = "龙胆",
	["heg_tieqi_xh"] = "铁骑",
	["heg_liegong_xh"] = "烈弓",
	["heg_kuanggu_xh"] = "狂骨",
	
	["heg_kuanggu_xh:draw"] = "摸一张牌",
	["heg_kuanggu_xh:recover"] = "回复体力",
	["heg_liegong_xh:nojink"] = "不可被响应",
	["heg_liegong_xh:adddamage"] = "伤害值基数+1",
	

	["heg_wuguotai"] = "吴国太[国]",
	["#heg_wuguotai"] = "武烈皇后",
	["illustrator:heg_wuguotai"] = "李秀森",
	["heg_ganlu"] = "甘露",
	[":heg_ganlu"] = "出牌阶段限一次，你可令两名装备区里的牌数不均为0且差不大于你已损失的体力值的角色交换装备区里的牌。",
	["#heg_GanluSwap"] = "%from 令 %to 交换了装备区里的牌",
	["heg_buyi"] = "补益",
	[":heg_buyi"] = "当一名角色A因受到伤害而进入的濒死结算结束后，若A与你势力相同且存活且你于此回合内未发动过此技能，你可令来源B选择是否执行军令▶若B选择否，A回复1点体力。",

	["heg_lukang"] = "陆抗[国]",
	["#heg_lukang"] = "孤柱扶厦",
	["illustrator:heg_lukang"] = "王立雄",
	["heg_keshou"] = "恪守",
	[":heg_keshou"] = "当你受到伤害时，你可弃置两张颜色相同的牌▶伤害值-1。若没有与你势力相同的其他角色，你判定，若结果为红色，你摸一张牌。",
	["heg_zhuwei"] = "筑围",
	[":heg_zhuwei"] = "当你进行的判定结果确定后，若判定牌为包含使用者对目标对应的角色造成伤害的效果的牌，你可获得此牌▶你可令当前回合角色使用【杀】的次数上限于此回合内+1且其手牌上限于此回合内+1。",
	["@keshou"] = "是否发动“恪守”，弃置两张颜色相同的牌减少伤害",
	["@zhuwei-choose"] = "筑围：是否令%src使用【杀】的次数上限和手牌上限+1",
	["@heg_zhuwei-choose"] = "筑围：是否令%src使用【杀】的次数上限和手牌上限+1",
	["#heg_ZhuweiBuff"] = "%from 令 %to 本回合使用【杀】的次数及手牌上限+1",

	["heg_yuanshu"] = "袁术[国]",
	["#heg_yuanshu"] = "仲家帝",
	["illustrator:heg_yuanshu"] = "YanBai",
	["heg_weidi"] = "伪帝",
	[":heg_weidi"] = "出牌阶段限一次，你可令一名于此回合内得到过牌堆里的牌的其他角色选择是否执行军令▶若其选择否，你获得其所有手牌，将等量的牌交给该角色。",
	["@weidi-return"] = "伪帝：请选择要交给%src的%arg张牌",
	["@heg_weidi-return"] = "伪帝：请选择要交给%src的%arg张牌",
	["heg_yongsi"] = "庸肆",
	[":heg_yongsi"] = "锁定技，①若所有角色的装备区里均没有【玉玺】，你视为装备着【玉玺】。②当你成为【知己知彼】的目标后，你展示所有手牌。",

	["heg_zhangxiu"] = "张绣[国]",
	["#heg_zhangxiu"] = "北地枪王",
	["designer:heg_zhangxiu"] = "千幻",
	["illustrator:heg_zhangxiu"] = "青岛磐蒲",
	["heg_fudi"] = "附敌",
	[":heg_fudi"] = "当你受到伤害后，你可以将一张手牌交给来源▶你对与其势力相同的所有角色中体力值最大且不小于你的体力值的一名角色造成1点普通伤害。",
	["heg_congjian"] = "从谏",
	[":heg_congjian"] = "锁定技，①当你于回合外造成伤害时，你令伤害值+1。②当你于回合内受到伤害时，你令伤害值+1。",
	["@fudi-give"] = "你可以发动“附敌”，将一张手牌交给伤害来源（%src）",
	["@fudi-damage"] = "附敌：请选择要对其造成伤害的角色",
	["@heg_fudi-give"] = "你可以发动“附敌”，将一张手牌交给伤害来源（%src）",
	["@heg_fudi-damage"] = "附敌：请选择要对其造成伤害的角色",

	["#heg_lord_caocao"] = "凤舞九霄",
	["heg_lord_caocao"] = "君·曹操[国]",
	["&heg_lord_caocao"] = "曹操[国]" ,
	["illustrator:heg_lord_caocao"] = "波子",
	["heg_jianan"] = "建安",
	[":heg_jianan"] = "君主技，锁定技，你拥有\"五子良将纛\"。\n\n#\"五子良将纛\"\n" ..
					"一名魏势力角色的准备阶段开始时，其可弃置一张牌并选择一张暗置的武将牌或暗置两张明置的武将牌中的一张▶其选择下列技能中其他角色均没有的一个：“突袭”、“巧变”、“骁果”、“节钺”、“断粮”。其于你的下个回合开始之前拥有其以此法选择的技能且不能明置其选择的武将牌。",	
	["heg_elitegeneralflag"] = "五子良将纛",
	[":heg_elitegeneralflag"] = "一名魏势力角色的准备阶段开始时，其可弃置一张牌并选择一张暗置的武将牌或暗置两张明置的武将牌中的一张▶其选择下列技能中其他角色均没有的一个：“突袭”、“巧变”、“骁果”、“节钺”、“断粮”。其于你的下个回合开始之前拥有其以此法选择的技能且不能明置其选择的武将牌。",
	["@elitegeneralflag"] = "你可以发动“五子良将纛”，请弃置一张牌",
	["@jianan-hide"] = "五子良将纛：请选择要暗置的武将牌",
	["heg_jianan_hide:head"] = "暗置主将",
	["heg_jianan_hide:deputy"] = "暗置副将",
	["@jianan-skill"] = "五子良将纛：请选择获得的技能",
	["heg_huibian"] = "挥鞭",
	[":heg_huibian"] = "出牌阶段限一次，你可选择一名魏势力角色和另一名已受伤的魏势力角色并对前者造成1点普通伤害▶前者摸两张牌。后者回复1点体力。",
	["heg_zongyu"] = "总御",
	[":heg_zongyu"] = "①当【六龙骖驾】移至其他角色的装备区后，若你的装备区里有坐骑牌，你可交换你与其装备区里的所有坐骑牌。②当坐骑牌被使用时，若使用者为你且{其他角色的装备区或弃牌堆有【六龙骖驾】}，你可将【六龙骖驾】置入你的装备区。",
	["#heg_ZongyuSwap"] = "%from 与 %to 交换了装备区里的坐骑牌",

	["SixDragons"] = "六龙骖驾",
	["heg_SixDragons"] = "六龙骖驾",
	[":heg_SixDragons"] = "装备牌·坐骑\n\n技能：\n" ..
					"1. 锁定技，你至其他角色的距离-1。\n" ..
					"2. 锁定技，其他角色至你的距离+1。\n" ,
	["horse"] = "坐骑",

	["heg_tuxi_egf"] = "突袭",
	["heg_qiaobian_egf"] = "巧变",
	["heg_xiaoguo_egf"] = "骁果",
	["heg_jieyue_egf"] = "节钺",
	["heg_duanliang_egf"] = "断粮",

	["heg_command"] = "军令",

	["@startcommand"] = "%arg：请选择一项军令<br>%arg2；<br>%arg3",
	["@startcommandto"] = "%arg：请选择一项军令，目标是%dest<br>%arg2；<br>%arg3",
	
	["heg_command1"] = "军令一",
	["heg_command2"] = "军令二",
	["heg_command3"] = "军令三",
	["heg_command4"] = "军令四",
	["heg_command5"] = "军令五",
	["heg_command6"] = "军令六",

	["#heg_command1"] = "军令一：对你指定的角色造成1点伤害",
	["#heg_command2"] = "军令二：摸一张牌，然后交给你两张牌",
	["#heg_command3"] = "军令三：失去1点体力",
	["#heg_command4"] = "军令四：本回合不能使用或打出手牌且所有非锁定技失效",
	["#heg_command5"] = "军令五：叠置，本回合不能回复体力",
	["#heg_command6"] = "军令六：选择一张手牌和一张装备区里的牌，弃置其余的牌",
	
	["#heg_CommandChoice"] = "%from 选择了 %arg",
	
	["#heg_commandselect_yes"] = "执行军令",
	["#heg_commandselect_no"] = "不执行军令",

	["#heg_CommandDamage"] = "%from 选择对 %to 造成伤害",
	
	["@command-damage"] = "军令：请选择伤害的目标",
	["@command-give"] = "军令：请选择两张牌交给%src",
	["@command-select"] = "军令：请选择要保留的一张手牌和一张装备",
	
	["@docommand"] = "%arg：请选择是否执行军令（发起者%src）<br>%arg2",
	["@docommand1"] = "%arg：请选择是否执行军令（发起者%src）<br>军令一：对%src指定的角色造成1点伤害",
	["@docommand2"] = "%arg：请选择是否执行军令（发起者%src）<br>军令二：摸一张牌，然后交给%src两张牌",

	["yes"] = "是",
	["no"] = "否",
	
	
	
	
	
	
	
	
	
	
}
