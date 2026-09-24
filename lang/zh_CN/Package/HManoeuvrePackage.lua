--[[********************************************************************
  Ported xxyheaven Hegemony translation; donor license/header retained.
  -- source: TODO/QSanguosha-For-Hegemony-xxyheaven/lang/zh_CN/Package/LordEXPackage.lua (manoeuvre boundary)
*********************************************************************]]
return {
	["heg_manoeuvre"] = "纵横捭阖",
	
	["#heg_huaxin"] = "渊清玉洁",
	["heg_huaxin"] = "华歆[国]",
	["designer:heg_huaxin"] = "韩旭",
	["illustrator:heg_huaxin"] = "秋呆呆",
	["heg_wanggui"] = "望归",
	[":heg_wanggui"] = "当你造成或受到伤害后，若你于当前回合内未发动过此技能且此武将牌处于明置状态且你的另一张武将牌：处于明置状态，你可令与你势力相同的角色各摸一张牌；"..
		"处于暗置状态，你可对与你势力不同的一名角色造成1点普通伤害。",
	["heg_wanggui:prompt"] = "是否使用“望归”，令与你势力相同的角色各摸一张牌",
	["heg_wanggui-invoke"] = "是否使用“望归”，对一名与你势力不同的角色造成1点伤害",
	["heg_xibing"] = "息兵",
	[":heg_xibing"] = "当黑色【杀】或黑色普通锦囊牌于其他角色的出牌阶段内指定目标后，若使用者为该角色且其于此回合内于使用此牌之前未使用过黑色【杀】或黑色普通锦囊牌且"..
		"目标对应的角色数为1，你可发动此技能▶{若其手牌数小于体力值，其将手牌补至X张（X为其体力值），其于此回合内不能使用对应的实体牌均是其手牌区的牌的牌}。"..
		"若你与其所有武将牌均处于明置状态，你可暗置你的一张不为君主武将牌且不为士兵牌的武将牌▷其暗置你选择的其一张不为君主武将牌且不为士兵牌的武将牌，"..
		"你与其于此回合内不能明置以此法暗置的武将牌。",
	["@xibing-hide"] = "息兵：选择要暗置的%dest的武将牌",
	["heg_xibing:head"] = "暗置主将",
	["heg_xibing:deputy"] = "暗置副将",
	
	
	["#heg_luyusheng"] = "义姑",
	["heg_luyusheng"] = "陆郁生[国]",
	["designer:heg_luyusheng"] = "韩旭",
	["illustrator:heg_luyusheng"] = "君桓文化",
	["heg_zhente"] = "贞特",
	[":heg_zhente"] = "当你成为黑色基本牌或黑色普通锦囊牌的目标后，若使用者不为你且你于当前回合内未发动过此技能，你可令其选择：1.其于此回合内不能使用黑色的牌；2.此牌对此目标无效。",
	["heg_zhiwei"] = "至微",
	[":heg_zhiwei"] = "当你明置此武将牌后，你可选择一名其他角色▶(→)直到你暗置或移除此武将牌前，{当其造成伤害后，你摸一张牌；当其受到伤害后，你随机弃置一张手牌；弃牌阶段结束时，你令其获得你于此阶段内因弃置而置入弃牌堆的牌；当其死亡时，若你的所有武将牌均处于明置状态，你暗置此武将牌}。",
	
	["heg_zhente-ask"] = "贞特：1.令【%arg】对%src无效；2.本回合不能使用黑色牌",
	["heg_zhente:nullified"] = "此牌对其无效",
	["heg_zhente:cardlimited"] = "本回合不能使用黑色牌",
	
	["#heg_ZhenteChoice1"] = "%from 选择：【%arg】对 %to 无效",
	["#heg_ZhenteChoice2"] = "%from 选择：本回合不能使用黑色牌",
	
	["heg_zhiwei-invoke"] = "是否使用“至微”，选择一名其他角色",
	
	["#heg_ZhiweiEffect1"] = "%to 造成伤害，%from 因 %arg 效果摸一张牌",
	["#heg_ZhiweiEffect2"] = "%to 受到伤害，%from 因 %arg 效果随机弃置一张手牌",
	["#heg_ZhiweiEffect3"] = "%to 因 %arg 效果获得 %from 弃置的牌",
	["#heg_ZhiweiEffect4"] = "%to 死亡，%from 因 %arg 效果暗置武将牌",
	
	["#heg_ZhiweiFinsh"] = "%from 对 %to 的 %arg 效果终止",

	["#heg_zongyux"] = "九酝鸿胪",
	["heg_zongyux"] = "宗预[国]",
	["designer:heg_zongyux"] = "韩旭",
	["illustrator:heg_zongyux"] = "铁杵",
	["heg_qiao"] = "气傲",
	[":heg_qiao"] = "当你成为牌的目标后，若使用者为其他势力角色且你于当前回合内发动此技能的次数＜2，你可弃置其一张牌▶你弃置一张牌。",
	["heg_chengshang"] = "承赏",
	[":heg_chengshang"] = "当对应的实体牌数（曾）为1的牌于你的出牌阶段内结算完成后，若使用者为你且此牌的目标列表中有对应的角色为其他势力角色的目标且此牌未造成过伤害，"..
		"你可发动此技能▶你获得牌堆中所有与此牌花色和点数均相同的牌▷此技能于此阶段内无效。",

	["@qiao-discard"] = "气傲：选择一张牌弃置",
	
	
	["#heg_miheng"] = "狂傲奇人",
	["heg_miheng"] = "祢衡[国]",
	["designer:heg_miheng"] = "韩旭",
	["illustrator:heg_miheng"] = "MuMu",
	["heg_kuangcai"] = "狂才",
	[":heg_kuangcai"] = "锁定技，①你于回合内使用牌无距离关系的限制且无次数限制。②弃牌阶段开始时，若你于此回合内："..
		"使用过牌且未造成过伤害，你的手牌上限-1；未使用过牌，你的手牌上限+1。",
	["@heg_kuangcai"] = "狂才",
	["heg_shejian"] = "舌剑",
	[":heg_shejian"] = "当你成为牌的目标后，若使用者不为你且目标对应的角色数为1且所有角色的体力值均大于0且你有手牌，你可发动此技能▶"..
		"你弃置所有手牌，你选择：1.弃置使用者X张牌（X=min{你以此法弃置的牌数, 其能被你弃置的牌数}）；2.对使用者造成1点普通伤害。",
	["heg_decrease"] = "减",
	["heg_increase"] = "加",
	["@shejian-choice"] = "舌剑：选择弃置%dest的%arg张牌，或对%dest造成1点伤害",
	["heg_shejian:discard"] = "弃置其牌",
	["heg_shejian:damage"] = "对其造成伤害",

	["#heg_fengxi"] = "东吴苏武",
	["heg_fengxi"] = "冯熙[国]",
	["designer:heg_fengxi"] = "韩旭",
	["illustrator:heg_fengxi"] = "匠人绘",
	["heg_yusui"] = "玉碎",
	[":heg_yusui"] = "当你成为黑色牌的目标后，若使用者与你势力不同且你于当前回合内未发动过此技能，你可失去1点体力▶"..
		"你选择：1.令其弃置X张手牌（X为其体力上限）；2.令其失去Y点体力（Y=max{其体力值-你的体力值，0}）。",
	["heg_boyan"] = "驳言",
	[":heg_boyan"] = "出牌阶段限一次，你可选择一名其他角色▶其将手牌补至X张（X为其体力上限），其于此回合内不能使用或打出对应的实体牌均为其手牌区里的牌的牌。"..
		"你可令其于其下个回合结束之前拥有〖驳言（纵横）〗。",
	["@yusui-choice"] = "玉碎：选择一项令%dest执行",
	["heg_yusui:losehp"] = "失去体力至与你相同",
	["heg_yusui:discard"] = "弃置体力上限张手牌",
	
	["@boyan-zongheng"] = "是否让%dest获得技能：驳言（纵横）",
	
	["heg_boyanzongheng"] = "驳言",
	[":heg_boyanzongheng"] = "出牌阶段限一次，你可选择一名其他角色▶其于此回合内不能使用或打出对应的实体牌均为其手牌区里的牌的牌。",

	["#heg_dengzhi"] = "绝境的外交家",
	["heg_dengzhi"] = "邓芝[国]",
	["designer:heg_dengzhi"] = "韩旭",
	["illustrator:heg_dengzhi"] = "凝聚永恒",
	["heg_jianliang"] = "简亮",
	[":heg_jianliang"] = "摸牌阶段开始时，若你是手牌数最小的角色，你可令与你势力相同的角色各摸一张牌。",
	["heg_weimeng"] = "危盟",
	[":heg_weimeng"] = "出牌阶段限一次，你可选择一名有手牌的其他角色▶你获得其至多X张手牌（X为你的体力值），将等量的牌交给其。你可令其于其下个回合结束之前拥有〖危盟（纵横）〗。",

	["heg_weimengzongheng"] = "危盟",
	[":heg_weimengzongheng"] = "出牌阶段限一次，你可选择一名有手牌的其他角色▶你获得其一张手牌，将一张牌交给其。",

	["@weimeng-num"] = "危盟：选择获得%dest的牌的数量",
	
	["@weimeng-give"] = "危盟：选择交给%dest的%arg张牌",
	
	["@weimeng-zongheng"] = "是否让%dest获得技能：危盟（纵横）",
	
	["#heg_xunchen"] = "三公谋主",
	["heg_xunchen"] = "荀谌[国]",
	["designer:heg_xunchen"] = "韩旭",
	["illustrator:heg_xunchen"] = "凝聚永恒",
	["heg_fenglve"] = "锋略",
	[":heg_fenglve"] = "出牌阶段限一次，你可与一名角色拼点▶{若：你赢，其将区域里两张牌交给你；其赢，你将一张牌交给其}。你可令其于其下个回合结束之前拥有〖锋略（纵横）〗。",
	["heg_anyong"] = "暗涌",
	[":heg_anyong"] = "当与你势力相同的角色A对另一名其他角色B造成伤害时，若你于当前回合内未发动过此技能，你可令伤害值+X（X为伤害值）▶"..
		"若B：所有武将牌均处于明置状态，你失去1点体力，失去此技能；仅有一张武将牌处于明置状态，你弃置两张手牌。",
	["@fenglve-give1"] = "锋略：选择一张牌交给%dest",
	["@anyong-discard"] = "暗涌：选择两张手牌弃置",
	
	["heg_fenglvezongheng"] = "锋略",
	[":heg_fenglvezongheng"] = "出牌阶段限一次，你可与一名角色拼点▶若：你赢，其将区域里一张牌交给你；其赢，你将两张牌交给其。",
	["@fenglve-give2"] = "锋略：选择两张牌交给%dest",
	
	["@fenglve-zongheng"] = "是否让%dest获得技能：锋略（纵横）",
	
	["#heg_yanghu"] = "制纮同轨",
	["heg_yanghu"] = "羊祜[国]",
	["designer:heg_yanghu"] = "韩旭",
	["illustrator:heg_yanghu"] = "匠人绘",
	["heg_deshao"] = "德劭",
	[":heg_deshao"] = "当黑色牌指定目标后，若使用者不为你且其处于明置状态的武将牌数不大于你且此目标对应的角色为你且此牌的目标对应的角色数为1且"..
		"你于此回合内发动此技能的次数小于你的体力值，你可弃置其一张牌。",
	["heg_mingfa"] = "明伐",
	[":heg_mingfa"] = "出牌阶段限一次，你可选择一名其他势力角色▶(→)其下个回合结束前，若其手牌数：小于你，你对其造成1点普通伤害，获得其一张手牌；"..
		"大于你，你摸X张牌（X=min{你与其手牌数之差,5}）。",

--	["mingfazongheng"] = "明伐",
--	[":mingfazongheng"] = "出牌阶段限一次，你可弃置一张牌并选择一名其他势力角色▶(→)其下个回合结束前，若其手牌数小于你，{你对其造成1点伤害，获得其一张手牌}。",

--	["@mingfa-zongheng"] = "是否让%dest获得技能：明伐（纵横）",

	["#heg_MingfaEffect"] = "%from 的 %arg 效果生效",
	
	
	
	
	
	
	
	
	
	
	
}
