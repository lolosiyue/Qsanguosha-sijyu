--[[********************************************************************
  Ported xxyheaven Hegemony translation; donor license/header retained.
  -- source: TODO/QSanguosha-For-Hegemony-xxyheaven/lang/zh_CN/Package/LordEXPackage.lua (newsgs boundary)
*********************************************************************]]
return {
	["heg_newsgs"] = "十周年专属",
	
	
	["#heg_jianggan"] = "锋镝悬信",
	["heg_jianggan"] = "蒋干[国]",
	["illustrator:heg_jianggan"] = "biou09",
	["designer:heg_jianggan"] = "韩旭",
	["heg_weicheng"] = "伪诚",
	[":heg_weicheng"] = "当你的手牌移至其他角色的手牌区后，若你的手牌数小于你的体力值，你可摸一张牌。",
	["heg_daoshu"] = "盗书",
	[":heg_daoshu"] = "出牌阶段限一次，你可以选择一种花色并选择一名有手牌的其他角色▶你获得其一张牌并记录此牌的游戏牌ID。若为此ID的牌："..
		"是你选择的花色，你对其造成1点普通伤害，此技能于此阶段内的发动次数上限+1；"..
		"不是你选择的花色，{若你的手牌均与此牌花色相同，你展示所有手牌}。你将一张与此牌花色花色不同的手牌交给其。",
	["@daoshu-give"] = "盗书：选择一张手牌交给 %dest",
	
	
	
	["#heg_zhouyi"] = "靛情雨黛",
	["heg_zhouyi"] = "周夷[国]",
    ["designer:heg_zhouyi"] = "韩旭",
	["illustrator:heg_zhouyi"] = "Tb罗根",
	["heg_zhukou"] = "逐寇",
	[":heg_zhukou"] = "当你于一名角色的出牌阶段内造成伤害后，若你于此阶段内于造成此伤害之前未造成过伤害，你可摸X张牌（X=min{你于此回合内使用过的牌数,5}）。",
	["heg_duannian"] = "断念",
	[":heg_duannian"] = "出牌阶段结束时，若你有手牌，你可发动此技能▶你弃置所有手牌，将手牌补至X张（X为你的体力上限）。",
	["heg_lianyou"] = "莲佑",
	[":heg_lianyou"] = "你死亡时，你可令一名其他角色获得〖兴火〗。",
	["heg_xinghuo"] = "兴火",
	[":heg_xinghuo"] = "锁定技，当你造成火焰伤害时，你令伤害值+1。",
	
	["@lianyou"] = "是否使用“莲佑”，选择一名角色获得“兴火”",
	
	["#heg_nanhualaoxian"] = "仙人指路",
	["heg_nanhualaoxian"] = "南华老仙[国]",
	["designer:heg_nanhualaoxian"] = "韩旭",
	["illustrator:heg_nanhualaoxian"] = "君桓文化",
	["heg_gongxiu"] = "共修",
	[":heg_gongxiu"] = "摸牌阶段，你可令额定摸牌数-1▶你选择：1.令至多X名角色各摸一张牌，你于下次发动此技能时不能选择此项；2.令至多X名角色各弃置一张牌，你于下次发动此技能时不能选择此项。（X为你的体力上限）",
	["heg_jinghe"] = "经合",
	[":heg_jinghe"] = "出牌阶段限一次，若你于此回合内未发动过此技能，你可以展示至多X张牌名各不相同的手牌并选择等量的有处于明置状态的武将牌的角色（X为你的体力上限）▶"..
	"系统随机从〖雷击〗、〖阴兵〗、〖活气〗、〖鬼助〗、〖仙授〗、〖论道〗、〖观月〗、〖言政〗中选择等量的技能，"..
	"这些角色各可从中选择一个没有角色因执行此次技能的效果而选择过的技能，其拥有此技能直到你的下回合开始。",
	["heg_leiji_tianshu"] = "雷击",
	[":heg_leiji_tianshu"] = "当你使用或打出【闪】时，你可令一名其他角色判定▶若结果为黑桃，你对其造成2点雷电伤害。",
	["heg_yinbing"] = "阴兵",
	[":heg_yinbing"] = "锁定技，①当你因执行你使用的【杀】的效果而造成的伤害的结算结算开始前，你终止此伤害流程▶其失去X点体力（X为伤害值）。②当其他角色失去体力后，你摸一张牌。",
	["heg_huoqi"] = "活气",
	[":heg_huoqi"] = "出牌阶段限一次，你可弃置一张牌并选择体力值最小的一名角色▶其回复1点体力，其摸一张牌。",
	["heg_guizhu"] = "鬼助",
	[":heg_guizhu"] = "当一名角色进入濒死状态时，若你于当前回合内未发动过此技能，你摸两张牌。",
	["heg_xianshou"] = "仙授",
	[":heg_xianshou"] = "出牌阶段限一次，你可选择：1.令一名已受伤的角色摸一张牌；2.令一名未受伤的角色摸两张牌。",
	["heg_lundao"] = "论道",
	[":heg_lundao"] = "当你受到伤害后，若来源的手牌数：大于你，你可弃置其一张牌；小于你，你可摸一张牌。",
	["heg_guanyue"] = "观月",
	[":heg_guanyue"] = "结束阶段开始时，你可观看牌堆顶的两张牌▶你获得其中的—张牌。",
	["heg_yanzheng"] = "言政",
	[":heg_yanzheng"] = "准备阶段开始时，若你的手牌数大于1，你可选择一张手牌并弃置其余的手牌▶你对至多X名角色各造成1点普通伤害（X为你以此法弃置的牌数）。",

	["@gongxiu-choose"] = "共修：选择一项效果发动",
	["heg_gongxiu_choose:draw"] = "令角色摸牌",
	["heg_gongxiu_choose:discard"] = "令角色弃牌",
	["@gongxiu-draw"] = "共修：选择至多%arg名角色各摸一张牌",
	["@gongxiu-discard"] = "共修：选择至多%arg名角色各弃置一张牌",
	["@gongxiu-throw"] = "共修：选择一张牌弃置",
	
	["@jinghe-choose"] = "经合：选择获得一项技能",
	
	["@yanzheng"] = "是否使用“言政”，选择一张手牌，弃置其余手牌",
	["@yanzheng-damage"] = "言政：选择至多%arg名角色",
	
	
	["#heg_lvlingqi"] = "无双虓姬",
	["heg_lvlingqi"] = "吕玲绮[国]",
	["designer:heg_lvlingqi"] = "xat1k",
	["illustrator:heg_lvlingqi"] = "君桓文化",
	["heg_guowu"] = "帼武",
	[":heg_guowu"] = "出牌阶段开始时，你可展示所有手牌▶你获得X枚“帼武”（X为你以此法展示的牌中的类别数）。{你随机获得弃牌堆里的一张【杀】。"..
		"若Y大于1，你于此阶段内使用牌无距离关系的限制}→{当你于此阶段内使用【杀】选择目标后，若Y大于2，你可令至多两名角色也成为此牌的目标▷你弃1枚“帼武”；"..
		"此阶段结束后，你弃所有“帼武”}。（Y为“帼武”数）",
	["heg_zhuangrong"] = "妆戎",
	[":heg_zhuangrong"] = "出牌阶段限一次，你可弃置一张锦囊牌▶你于此阶段内拥有〖无双〗。",
	["heg_wushuang_lvlingqi"] = "无双",
	["heg_shenwei"] = "神威",
	[":heg_shenwei"] = "主将技，锁定技，（此武将牌上单独的阴阳鱼个数-1）①摸牌阶段，若你是体力值最大的角色，你令额定摸牌数+2；②你的手牌上限+2。",
	["@guowu-add"] = "帼武：可为使用的【%arg】增加至多两个目标",
	
    ["heg_$AddCardTarget"] = "%from 发动了“%arg”为 %card 增加了额外目标 %to",
    ["heg_$RemoveCardTarget"] = "%from 发动了“%arg”为 %card 减少了目标 %to",

	["#heg_yangwan"] = "融沫之鲡",
	["heg_yangwan"] = "杨婉[国]",
	--["designer:yangwan"] = "",
	["illustrator:heg_yangwan"] = "木美人",
	["heg_youyan"] = "诱言",
	[":heg_youyan"] = "当你的牌于你的回合内因弃置而置入弃牌堆后，若你于此回合内未发动过此技能，你可亮出牌堆顶的四张牌▶你获得其中与你此次弃置的牌的花色均不相同的所有牌。",
	["heg_zhuihuan"] = "追还",
	[":heg_zhuihuan"] = "回合结束前，你可{选择：1.选择一名角色▶其获得1枚“追还（伤害）”；2.选择一名角色▶其获得1枚“追还（弃牌）”；"..
		"3.选择两名角色▶其中一名角色获得1枚“追还（伤害）”，另一名角色获得1枚“追还（弃牌）”}（“追还”的下标对所有角色均不可见）→"..
		"当这些角色于你的下个回合开始之前受到伤害后，若其存活且“追还”数大于0，"..
		"你令{{其弃1枚“追还（伤害）”▷其对来源造成1点普通伤害}。{其弃1枚“追还（弃牌）”▷来源弃置两张手牌}}。",

	["@zhuihuan-invoke"] = "是否使用“追还”，选择一至两名角色",
	["@zhuihuan-choose"] = "追还：选择对%dest适用的效果",

	["#heg_ZhuihuanEffect"] = "%from 对 %to 的 %arg 效果生效",
	
	["heg_zhuihuan:damage"] = "追还伤害",
	["heg_zhuihuan:discard"] = "追还弃牌",

	["#heg_ty_duyu"] = "文成武德",
	--["designer:ty_duyu"] = "",
	["heg_ty_duyu"] = "杜预[国]",
	["illustrator:heg_ty_duyu"] = "凡果",
	["heg_jianguo"] = "谏国",
	[":heg_jianguo"] = "出牌阶段限一次，你可以选择一项令一名角色执行：1. 摸一张牌然后弃置X张手牌；2. 弃置一张牌然后摸X张牌。（X为其手牌数的一半，向下取整且至多为5）",
	["heg_qingshi"] = "倾势",
	[":heg_qingshi"] = "当你于回合内使用【杀】或锦囊牌指定其他角色为目标后，若此牌为你本回合使用的第X张牌（X为你的手牌数），你可以对此牌的目标角色之一造成1点伤害。每回合限两次。",
	["#heg_jianguo-choice"] = "谏国：选择一项效果令%dest执行",
	["heg_jianguo:d1tx"] = "摸一张牌然后弃置一半的手牌",
	["heg_jianguo:t1dx"] = "弃置一张牌然后摸手牌数量一半的牌",
	["heg_qingshi-invoke"] = "是否使用“倾势”，选择一名目标角色，对其造成1点伤害",

	["#heg_huangquan"] = "忠事三朝",
	--["designer:heg_huangquan"] = "",
	["heg_huangquan"] = "黄权[国]",
	["illustrator:heg_huangquan"] = "匠人绘",
	["heg_quanjian"] = "劝谏",
	[":heg_quanjian"] = "出牌阶段限一次，你可以选择一个“军令”令一名与你势力相同的角色选择是否执行。若其不执行，则其本回合下次受到的伤害+1。",
	["heg_tujue"] = "途绝",
	[":heg_tujue"] = "限定技，当你处于濒死状态时，你可以将所有牌交给一名其他角色，然后你回复X点体力值并摸X张牌（X为你给出的牌数且至多为3）。",
	["heg_tujue-invoke"] = "是否使用“途绝”，将所有的牌交给一名角色",

	["#heg_panjinshu"] = "神女",
	["heg_panjinshu"] = "潘谨淑[国]",
	["illustrator:heg_panjinshu"] = "",
	["heg_zhiren"] = "织纴",
	[":heg_zhiren"] = "当你于回合内使用第一张非转化的红色牌时，你可以选择一项：1.观看牌堆顶的X张牌，然后以任意顺序置于牌堆顶或牌堆底（X为此牌字数）；"..
	"2.弃置一名其他女性角色的装备牌。",
	["heg_yaner"] = "燕尔",
	[":heg_yaner"] = "当一名与你势力相同的其他角色于其出牌阶段失去最后的手牌后，你可以与其各摸一张牌。每回合限一次。",

	["@zhiren-choice"] = "你可以发动“织纴”，选择卜算%arg或弃置其他女性角色的装备",
	["heg_zhiren:busuan"] = "卜算",
	["heg_zhiren:discard"] = "弃置装备",
	["@zhiren-target"] = "你可以发动“织纴”，选择一名女性角色，弃置其一张装备",

}
