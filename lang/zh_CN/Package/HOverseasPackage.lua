--[[********************************************************************
  Ported xxyheaven Hegemony translation; donor license/header retained.
  -- source: TODO/QSanguosha-For-Hegemony-xxyheaven/lang/zh_CN/Package/LordEXPackage.lua (overseas boundary)
*********************************************************************]]
return {
	["heg_overseas"] = "国际服专属",

    ["heg_beimihu"] = "卑弥呼[国]",
	["#heg_beimihu"] = "邪马台的女王",
	["illustrator:heg_beimihu"] = "聚一_小道恩",
	["heg_guishu"] = "鬼术",
	[":heg_guishu"] = "出牌阶段，若你于此回合内{未发动过此技能或上一次因发动此技能而使用的牌为【知己知彼】/【远交近攻】}，你可使用对应的实体牌为你的一张黑桃手牌的【远交近攻】/【知己知彼】。" ,
	["heg_yuanyu"] = "远域",
	[":heg_yuanyu"] = "锁定技，当你受到伤害时，若你不在来源的攻击范围内，你令伤害值-1。" ,

    ["heg_caozhen"] = "曹真[国]",
	["#heg_caozhen"] = "万载不刊",
	["illustrator:heg_caozhen"] = "鬼画府",
	["heg_sidi"] = "司敌",
	[":heg_sidi"] = "①当一名角色受到伤害后，若其与你势力相同且有牌且你：有“驭”且“驭”的类别数＜3，你可发动此技能▶其可将与所有“驭”类别均不同的一张牌置于你的武将牌上（称为“驭”）；"..
			"没有“驭”，你可发动此技能▶其可将一张牌置于你的武将牌上（称为“驭”）。"..
			"②其他势力角色的回合开始时，若其存活，你可将至多三张“驭”置入弃牌堆▶你选择等量的项：1.你选择一种与以此法置入弃牌堆的“驭”相同的类别，其于此回合内不能使用此类别的牌；"..
			"2.你选择其的一个处于明置状态的武将牌上的技能，此技能于此回合内无效；3.令其选择一名与你势力相同的其他角色，该角色回复1点体力。" ,
	["heg_drive"] = "驭",

	["@sidi-put"] = "司敌：可选一张牌作为%src的“驭”",
	["@sidi-remove"] = "是否对%dest使用“司敌”",
	["@sidi-choice"] = "司敌：选择一项令%dest执行",

	["heg_sidi_choice:cardlimit"] = "选一种类别的牌不能使用",
	["heg_sidi_choice:skilllimit"] = "选一个技能不能使用",
	["heg_sidi_choice:recover"] = "令其选一名角色回复体力",
	
	["@sidi-recover"] = "司敌：选一名角色回复体力",
	["@sidi-cardtype"] = "司敌：令%dest本回合不能使用一种类别的牌",
	["@sidi-skill"] = "司敌：令%dest的一个技能本回合无效",

	["heg_sidi_limit"] = "司敌",
	["heg_sidi_invalidity"] = "司敌",
	["heg_log_BasicCard"] = "基",
	["heg_log_EquipCard"] = "装",
	["heg_log_TrickCard"] = "锦",

	["#heg_liaohua"] = "历尽沧桑",
	["heg_liaohua"] = "廖化[国]",
	["illustrator:heg_liaohua"] = "聚一工作室",
	["heg_dangxian"] = "当先",
	[":heg_dangxian"] = "锁定技，①当你明置此武将牌后，若你未发动过此技能，你获得1枚“先驱”。②回合开始后，你获得一个额外的出牌阶段。",

	["#heg_zhugejin"] = "联盟的维系者",
	["heg_zhugejin"] = "诸葛瑾[国]",
	["illustrator:heg_zhugejin"] = "G.G.G.",
	["heg_huanshi"] = "缓释",
	[":heg_huanshi"] = "当与你势力相同的角色的判定结果确定前，你可打出对应的实体牌为你的一张牌且与此牌牌名相同的牌▶系统将此牌作为判定牌，将原判定牌置入弃牌堆。",
	["heg_hongyuan"] = "弘援",
	[":heg_hongyuan"] = "①当牌因合纵的效果而摸牌而移动至你的手牌区前，你可将目标区域改为一名与你势力相同的其他角色的手牌区。②出牌阶段限一次，你可展示一张手牌▶此牌于此阶段内视为带有“合纵”标识（维系区域为你的手牌区）。",
	["heg_mingzhe"] = "明哲",
	[":heg_mingzhe"] = "①当红色牌于你的回合外被使用/打出时，若使用/打出者为你，你摸一张牌。②当红色装备牌于你的回合外移出你的装备区后，你可摸一张牌。",

	["@huanshi-card"] = CommonTranslationTable["@askforretrial"],
	["heg_hongyuan-invoke"] = "是否使用“弘援”，选择摸牌的角色",

	["#heg_chendao"] = "白毦督",
	["heg_chendao"] = "陈到[国]",
	["designer:heg_chendao"] = "荼蘼",
	["illustrator:heg_chendao"] = "王立雄",
	["heg_wanglie"] = "往烈",
	[":heg_wanglie"] = "①若你于出牌阶段内未使用过牌，你于此阶段内使用的下一张牌无距离关系的限制。②当【杀】或普通锦囊牌于出牌阶段内被使用时，若使用者为你，"..
		"你可发动此技能▶所有角色均不能响应此牌。你于此阶段内不能使用牌。",

	["#heg_yangxiu"] = "恃才放旷",
	["heg_yangxiu"] = "杨修[国]",
	["designer:heg_yangxiu"] = "KayaK",
	["illustrator:heg_yangxiu"] = "张可",
	["heg_danlao"] = "啖酪",
	[":heg_danlao"] = "当你成为锦囊牌的目标后，若目标对应的角色数大于1，你可摸一张牌▶此牌对此目标无效。",
	["heg_jilei"] = "鸡肋",
	[":heg_jilei"] = "当你受到伤害后，若来源存活，你可选择一种牌的类别▶其于当前回合内不能使用或打出对应的实体牌均是其手牌区里的牌的为此类别的牌，"..
		"且于当前回合内不能弃置其的为此类别的手牌。",
	["#heg_Jilei"] = "由于“<font color=\"yellow\"><b>鸡肋</b></font>”效果，%from 本回合不能使用、打出或弃置 %arg",
	["@jilei-choose"] = "鸡肋：选择一种类别的牌令%dest本回合不能使用",

	["#heg_zumao"] = "碧血染赤帻",
	["heg_zumao"] = "祖茂[国]",
	["designer:heg_zumao"] = "红莲的焰神",
	["illustrator:heg_zumao"] = "DH",
	["heg_yinbingx"] = "引兵",
	[":heg_yinbingx"] = "①结束阶段开始时，你可将至少一张不为基本牌的牌置于武将牌上（均称为“帻”）。②当你受到渠道为【杀】或【决斗】的伤害后，你将一张“帻”置入弃牌堆。",
	["@yinbing-put"] = "你可以发动“引兵”，选择任意数量的非基本牌作为“帻”",
	["kerchief"] = "帻",
	["heg_juedi"] = "绝地",
	[":heg_juedi"] = "锁定技，准备阶段开始时，若你有“帻”，你选择：1.将所有“帻”置入弃牌堆▶你将你的手牌补至X张（X为你的体力上限）；"..
		"2.将所有“帻”交给体力值不大于你的一名其他角色▶其回复1点体力，摸Y张牌（Y为你以此法交给该角色的“帻”数）。",
	["heg_juedi:self"] = "弃置帻并摸牌",
	["heg_juedi:give"] = "将帻交给其他角色",
	["@juedi"] = "绝地：请选择一名体力值不大于你的角色",

	["#heg_fuwan"] = "沉毅的国丈",
	["heg_fuwan"] = "伏完[国]",
	["designer:heg_fuwan"] = "嘉言懿行",
	["illustrator:heg_fuwan"] = "LiuHeng",
	["heg_moukui"] = "谋溃",
	[":heg_moukui"] = "当【杀】指定目标后，若使用者为你，你可选择：1.摸一张牌；2.弃置此目标对应的角色的一张牌▶(→)当此【杀】被其使用的【闪】抵消后，你令其弃置你的一张牌。",
	["@moukui-choose"] = "谋溃：选择摸一张牌或弃置 %dest 的一张牌",
	["heg_moukui:draw"] = "摸一张牌",
	["heg_moukui:discard"] = "弃置其一张牌",

	["#heg_MoukuiDiscard"] = "“%arg”的延时效果： %to 弃置 %from 的一张牌",

	["#heg_tianyu"] = "规略明练",
	["heg_tianyu"] = "田豫[国]",
	["illustrator:heg_tianyu"] = "鬼画府",
	["heg_zhenxi"] = "震袭",
	[":heg_zhenxi"] = "当【杀】指定目标后，若使用者为你，你可选择：1.弃置此目标对应的角色的一张牌▶若其有处于暗置状态的武将牌且你的两张武将牌均处于明置状态，"..
		"你可对其使用{对应的实体牌为你的一张不为锦囊牌的方块牌的【乐不思蜀】或对应的实体牌为你的一张不为锦囊牌的梅花牌的【兵粮寸断】（无距离关系的限制）}；"..
		"2.你可对此目标对应的角色使用{对应的实体牌为你的一张不为锦囊牌的方块牌的【乐不思蜀】或对应的实体牌为你的一张不为锦囊牌的梅花牌的【兵粮寸断】（无距离关系的限制）}▶"..
		"若其有处于暗置状态的武将牌且你的两张武将牌均处于明置状态，你可弃置其一张牌。",
	["heg_jiansu"] = "俭素",
	[":heg_jiansu"] = "副将技，（此武将牌上单独的阴阳鱼个数-1）①当你于回合外得到牌后，你可令这些牌均称为“金”（“金”的维系区域为你的手牌区，且对其他角色可见）。"..
		"②出牌阶段开始时，你可选择一名已受伤的角色并弃置至少X张“金”（X为其体力值）▶其回复1点体力。",
	["@zhenxi-choose"] = "震袭：选择转化延时锦囊牌对 %dest 使用，或者弃置其一张牌",
	["heg_zhenxi:usecard"] = "转化延时锦囊牌",
	["heg_zhenxi:discard"] = "弃置其一张牌",
	["@zhenxi-trick"] = "震袭：将方块牌当【乐不思蜀】或梅花牌当【兵粮寸断】对 %dest 使用",
	["@zhenxi-discard"] = "震袭：是否弃置 %dest 的一张牌",
	["@jiansu-card"] = "是否使用“俭素”，弃置明置的牌令一名角色回复体力",
	["money"] = "金",

	["#heg_huaxiong"] = "魔将",
	["heg_huaxiong"] = "华雄[国]",
	["illustrator:heg_huaxiong"] = "地狱许",
	["designer:heg_huaxiong"] = "Loun老萌",
	["heg_yaowu"] = "耀武",
	[":heg_yaowu"] = "限定技，当你造成伤害后，若此武将牌处于暗置状态，你可发动此技能▶你加2点体力上限，回复2点体力→当你死亡后，与你势力相同的角色各失去1点体力。",
	["heg_shiyong"] = "恃勇",
	[":heg_shiyong"] = "锁定技，当你受到渠道为牌伤害时，若你：未发动过〖耀武〗且此牌不为红色，你摸一张牌；发动过〖耀武〗且此牌不为黑色，来源摸一张牌。",

	["#heg_liufuren"] = "酷妒的海棠",
	["heg_liufuren"] = "刘夫人[国]",
	["illustrator:heg_liufuren"] = "Jzeo",
	["heg_zhuidu"] = "追妒",
	[":heg_zhuidu"] = "当你于出牌阶段内对一名角色造成伤害时，若你于此阶段内未发动过此技能，你可发动此技能▶{若其为女性角色，你可弃置一张牌，获得1枚“妒”}。若你："..
		"有“妒”，你弃所有“妒”，其弃置装备区里的所有牌，令伤害值+1；没有“妒”，其选择：1.弃置装备区里的所有牌；2.令伤害值+1。",
	["heg_shigong"] = "示恭",
	[":heg_shigong"] = "限定技，当你于回合外进入濒死状态时，若你的体力值小于1，你可移除副将▶当前回合角色选择：1.获得你以此法移除的武将牌上的一个未带有技能标签的技能，你回复体力至X点（X为你的体力上限）；2.令你回复体力至1点。",

	["@zhuidu-both"] = "追妒：是否弃置一张牌，令%dest执行两项",
	["heg_zhuidu_choice"] = "追妒",
	["heg_zhuidu_choice:throw"] = "弃置装备区里的所有牌",
	["heg_zhuidu_choice:damage"] = "伤害值+1",
	
	["@shigong-choose"] = "示恭：是否获得%arg的一个技能",

	["#heg_xiahoushang"] = "魏胤前驱",
	["heg_xiahoushang"] = "夏侯尚[国]",
	["designer:heg_xiahoushang"] = "豌豆&老萌",
	["illustrator:heg_xiahoushang"] = "M云涯",
	["heg_tanfeng"] = "探锋",
	[":heg_tanfeng"] = "准备阶段开始时，你可弃置一名其他势力角色区域里的一张牌▶其可选择一个除准备阶段外的阶段▷你对其造成1点火焰伤害，跳过此阶段。",
	["@tanfeng-target"] = "是否使用“探锋”，选择一名其他势力角色",
	["@tanfeng-choose"] = "探锋：是否受到伤害令%src跳过一个阶段",

	["#heg_liyan"] = "龙鲤钻云",
	["heg_liyan"] = "李严[国]",
	["illustrator:heg_liyan"] = "sinno",
	["designer:heg_liyan"] = "千幻",
	["heg_jinwu"] = "矜武",
	[":heg_jinwu"] = "出牌阶段开始时，你可选择是否执行军令▶若你选择：是，你使用无对应的实体牌的普【杀】（有距离关系的限制）；否，你结束出牌阶段。",
	["heg_zhuke"] = "筑科",
	[":heg_zhuke"] = "主将技，（此武将牌上单独的阴阳鱼个数-1）①当你选择执行军令后，你可选择军令▶系统将此军令作为你此次执行的军令。②当你叠置后，若你处于叠置状态，"..
		"你可令一名与你势力相同的角色回复1点体力。③当你横置后，你可令一名与你势力相同的角色回复1点体力。",
	["heg_quanjia"] = "劝驾",
	[":heg_quanjia"] = "副将技，当你明置此武将牌后，若你未发动过此技能▶所有没有势力且明置后会与你势力相同的角色各可明置一至两张武将牌"..
		"（该角色于以此法进行的明置流程中确定的势力改为其武将牌上标识的势力）。与你势力相同的角色各摸一张牌。"..
		"处于明置状态的武将牌上拥有〖仁德〗的角色获得〖章武〗和〖授钺〗。",

	["@jinwu-slash"] = "矜武：选择【杀】的目标角色",
	["heg_zhuke-invoke"] = "是否使用“筑科”，选择一名角色令其回复1点体力",
	["@zhuke-select"] = "筑科：选择你要执行的一项军令",

	["#heg_maxiumatie"] = "诛奸义存",
	["heg_maxiumatie"] = "马休＆马铁[国]",
	["&heg_maxiumatie"] = "马休马铁[国]",
	["illustrator:heg_maxiumatie"] = "alien",
	["designer:heg_maxiumatie"] = "偷糖＆归零",
	["heg_mashu_maxiumatie"] = "马术",
	["heg_xiaoqi"] = "骁骑",
	[":heg_xiaoqi"] = "当你使用【杀】选择目标时，你可以令此【杀】改为【决斗】，若如此做，你可在此决斗中视为至多打出X张【杀】（X为明置的有马术的武将牌数）。",

	["#heg_xianglang"] = "校书翾翻",
	["heg_xianglang"] = "向朗[国]",
	["illustrator:heg_xianglang"] = "",
	["heg_kanji"] = "勘集",
	[":heg_kanji"] = "出牌阶段限一次，你可以展示所有手牌，若每张牌花色均不同，你摸两张牌，然后若因此使手牌满足四种花色，则你跳过本回合的弃牌阶段。",
	["heg_qianzheng"] = "愆正",
	[":heg_qianzheng"] = "当你成为其他角色使用普通锦囊牌或【杀】的目标后，你可以重铸两张牌；若这两张牌与使用牌类型均不同，此牌结算后进入弃牌堆时，你可以获得之。每回合限一次。",
	["@qianzheng-cost"] = "你可以发动“愆正”，重铸两张牌",
	
	
	
	
	
}
