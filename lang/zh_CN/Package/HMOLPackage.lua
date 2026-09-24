--[[********************************************************************
  Ported xxyheaven Hegemony translation; donor license/header retained.
  -- source: TODO/QSanguosha-For-Hegemony-xxyheaven/lang/zh_CN/Package/LordEXPackage.lua (MOL boundary)
*********************************************************************]]
return {
	["heg_mol"] = "十年经典",

	["#heg_duyu"] = "文成武德",
	["designer:heg_duyu"] = "陈磊",
	["heg_duyu"] = "杜预[国]",
	["illustrator:heg_duyu"] = "鬼画府",
	["heg_wuku"] = "武库",
	[":heg_wuku"] = "锁定技，当装备牌被使用时，若“武库”数＜2且使用者与你势力不同，你获得1枚“武库”。",
	["heg_miewu"] = "灭吴",
	[":heg_miewu"] = "①当你需要使用基本牌/锦囊牌时，若你有“武库”且你于当前回合内未发动过此技能和〖灭吴②〗，你可使用对应的实体牌为你的一张牌的此基本牌/锦囊牌▶你弃1枚“武库”，"..
		"系统继续此基本牌/锦囊牌的使用流程，你摸一张牌。②当你需要打出基本牌/锦囊牌时，若你有“武库”且你于当前回合内未发动过此技能和〖灭吴①〗，"..
		"你可打出对应的实体牌为你的一张牌的此基本牌/锦囊牌▶你弃1枚“武库”，系统继续此基本牌/锦囊牌的打出流程，你摸一张牌。",
	["#heg_armory"] = "武库",

	["heg_lifeng"] = "李丰[国]",
	["#heg_lifeng"] = "继父尽事",
	["illustrator:heg_lifeng"] = "NOVART",
	["food"] = "粮",
	["@tunchu-push"] = "屯储：选择一至两张手牌作为“粮”",
	["heg_tunchu"] = "屯储",
	[":heg_tunchu"] = "摸牌阶段，你可令额定摸牌数+2▶你于此回合内不能使用【杀】→摸牌阶段结束时，你将一至两张手牌置于武将牌上（均称为“粮”）。" ,
	["heg_shuliang"] = "输粮",
	[":heg_shuliang"] = "其他角色的结束阶段开始时，若其与你势力相同且你至其的距离≤“粮”数，你可将一张“粮”置入弃牌堆▶其摸两张牌。" ,
	["@shuliang"] = "是否使用“输粮”，弃置一张“粮”令%src摸两张牌",

    ["heg_lingcao"] = "凌操[国]",
	["#heg_lingcao"] = "激流勇进",
	["illustrator:heg_lingcao"] = "樱花闪乱",
	["heg_dujin"] = "独进",
	[":heg_dujin"] = "①摸牌阶段，你可令额定摸牌数+X（X为你的装备区里的牌数的一半且向上取整）。②当你明置此武将牌后，若你未发动过此技能且没有与你势力相同的{其他角色或已死亡的角色}，你获得1枚“先驱”。" ,

	["#heg_wangji"] = "经行合一",
	--["designer:wangji"] = "",
	["heg_wangji"] = "王基[国]",
	["illustrator:heg_wangji"] = "雪君S",
	["heg_qizhi"] = "奇制",
	[":heg_qizhi"] = "当你于回合内使用非装备牌时，你可以弃置不为此牌目标的一名角色的一张牌（若其与你势力不同则改为“手牌”），令其摸一张牌。每回合限三次。",
	["heg_jinqu"] = "进趋",
	[":heg_jinqu"] = "结束阶段，你可以摸两张牌，然后将手牌弃置至X张（X为本回合你发动“奇制”的次数）。",
	["heg_qizhi-invoke"] = "是否使用“奇制”，选择不是此次使用的牌的目标的一名角色",

	["#heg_yanyan"] = "断头将军",
	--["designer:yanyan"] = "",
	["heg_yanyan"] = "严颜[国]",
	["illustrator:heg_yanyan"] = "琛·美弟奇",
	["heg_juzhan"] = "拒战",
	[":heg_juzhan"] = "当你成为【杀】的目标后，若你发动过此技能的次数为偶数，你可以与使用者各摸一张牌，然后若其武将牌均明置，你可以暗置其一张武将牌，本回合不能再明置。"..
		"当你使用【杀】指定目标后，若你发动过此技能的次数为奇数，你可以获得其一张牌，然后你本回合不能对其使用牌。",
	["heg_juzhan-invoke"] = "是否使用“拒战”，选择其中一名目标角色",
	["@juzhan-hide"] = "拒战：你可以暗置%dest的一张武将牌",
	["heg_juzhan:head"] = "暗置主将",
	["heg_juzhan:deputy"] = "暗置副将",
	
	["heg_SwitchYang"] = "阳",
	["heg_SwitchYin"] = "阴",

	["#heg_zhuran"] = "不动之督",
	--["designer:zhuran"] = "",
	["heg_zhuran"] = "朱然[国]",
	["illustrator:heg_zhuran"] = "Ccat",
	["heg_danshou"] = "胆守",
	[":heg_danshou"] = "每轮限一次，一名角色的准备阶段，你可以弃置区域内所有牌，本回合每阶段开始时（准备阶段和结束阶段除外），若你的手牌数小于等于以此法弃置的牌数，"..
		"你摸一张牌或令你本回合以此法摸牌时多摸一张牌，当你一次性以此法摸四张牌后，你可以对其造成1点伤害。",

	["#heg_danshou-choose"] = "胆守：选择摸%arg张牌，或本回合以此法摸牌时多摸一张牌",
	["heg_danshou:draw"] = "立即摸牌",
	["heg_danshou:exdraw"] = "增加摸牌的张数",
	["#heg_danshou-damage"] = "胆守：选择是否对%dest造成1点伤害",

	["#heg_xugong"] = "独计击流",
	--["designer:xugong"] = "",
	["heg_xugong"] = "许贡[国]",
	["illustrator:heg_xugong"] = "君桓文化",
	["heg_biaozhao"] = "表召",
	[":heg_biaozhao"] = "出牌阶段限一次，你可以选择两名势力不同的其他角色，视为对其中一名角色使用一张【知己知彼】，然后将一张牌交给另一名角色，若如此做，你摸一张牌。",
	["heg_yechou"] = "业仇",
	[":heg_yechou"] = "锁定技，你死亡时，视为对杀死你的角色依次使用三张无距离限制的【杀】，其中：第一张不能被响应，第二张无视目标角色的防具，第三张造成的伤害+1。"..
		"若其以此法进入濒死状态，与其势力相同的其他角色不能对其使用【桃】。",
	["@biaozhao-give"] = "表召：选择一张牌交给%dest",

}
