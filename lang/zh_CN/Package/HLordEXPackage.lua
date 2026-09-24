--[[********************************************************************
  Ported xxyheaven Hegemony translation; donor license/header retained.
  -- source: TODO/QSanguosha-For-Hegemony-xxyheaven/lang/zh_CN/Package/LordEXPackage.lua (lord_ex boundary)
*********************************************************************]]
return {
	["heg_lord_ex"] = "君临天下·EX",
	["heg_lord_ex_card"] = "君临天下·EX",
	
	
	["#heg_mengda"] = "怠军反复",
	["heg_mengda"] = "孟达[国]",
	["designer:heg_mengda"] = "韩旭",
	["illustrator:heg_mengda"] = "张帅",
	["heg_qiuan"] = "求安",
	[":heg_qiuan"] = "当你受到伤害时，若没有“函”，你可将是此伤害的渠道的牌对应的所有实体牌置于你的武将牌上（均称为“函”）▶你防止此伤害。",
	["heg_liangfan"] = "量反",
	[":heg_liangfan"] = "锁定技，准备阶段开始时，若有“函”，你获得“函”▶你失去1点体力→当你于此回合内因执行牌的效果而对一名角色造成伤害后，"..
		"若此牌对应的实体牌中（曾）有你以此法得到的牌，你可获得其一张牌。",
	["heg_letter"] = "函",
	["#heg_LiangfanEffect"] = "%from 使用%arg牌造成伤害，获得 %to 的一张牌",
	
	["@liangfan"] = "量反：是否获得%dest的一张牌",
	
	
	["#heg_tangzi"] = "得时识风",
	["designer:heg_tangzi"] = "荼蘼（韩旭）",
	["heg_tangzi"] = "唐咨[国]",
	["illustrator:heg_tangzi"] = "凝聚永恒",
	["heg_xingzhao"] = "兴棹",
	[":heg_xingzhao"] = "锁定技，①若X>0，你拥有〖恂恂〗。②当你受到一名角色造成的伤害后，若X>1且其存活且其：手牌数大于你，你摸一张牌；手牌数小于你，你令其摸一张牌。"..
		"③弃牌阶段开始时，若X>2，你的手牌上限于此回合内+4。④当你失去装备区的牌后，若X＞3，你摸一张牌。（X为已受伤的角色的势力数）",
	["heg_xunxun_tangzi"] = "恂恂",
	
	["#heg_zhanglu"] = "政宽教惠",
	["designer:heg_zhanglu"] = "韩旭",
	["heg_zhanglu"] = "张鲁[国]",
	["illustrator:heg_zhanglu"] = "磐蒲",
	["heg_bushi"] = "布施",
	[":heg_bushi"] = "①回合结束前，你获得X枚“义舍”（X为你的体力值）。②其他角色的准备阶段开始时，若你有“义舍”，你可将一张牌交给该角色▶你弃1枚“义舍”，摸两张牌。"..
			"③准备阶段开始时，你{弃置X张牌（X=存活角色数-你的体力值-2），弃所有“义舍”}。",
	["heg_midao"] = "米道",
	[":heg_midao"] = "①结束阶段开始时，若没有“米”，你可摸两张牌▶你将两张牌置于武将牌上（均称为“米”）。"..
			"②当判定结果确定前，你可打出对应的实体牌是一张“米”且与此“米”牌名相同的牌▶系统将此牌作为判定牌，你获得原判定牌。",
	
	["heg_bushi:discard"] = "是否使用“布施”，弃置%arg张牌",
	["heg_bushi:mark"] = "是否使用“布施”，获得义舍标记",
	["heg_yishe"] = "义舍",
	["@bushi-give"] = "是否使用“布施”，将一张牌交给 %src",
	["rice"] = "米",
	["@midao-push"] = "米道：选择两张牌作为“米”",
	["@midao-card"] = CommonTranslationTable["@askforretrial"],
	
	
	["#heg_mifangfushiren"] = "逐驾迎尘",
	["designer:heg_mifangfushiren"] = "Loun老萌",
	["heg_mifangfushiren"] = "糜芳＆傅士仁[国]",
	["&heg_mifangfushiren"] = "糜芳傅士仁[国]",
	["illustrator:heg_mifangfushiren"] = "木美人",
	["heg_fengshix"] = "锋势",
	[":heg_fengshix"] = "①当牌指定目标后，若使用者为你且目标对应的角色数为1且与此目标对应的角色的手牌数小于你，你可发动此技能▶你弃置你与其各一张牌，此牌的伤害值基数+1。"..
		"②当你成为牌的目标后，若目标对应的角色数为1且你的手牌数小于使用者，（你令）其可发动此技能▶你弃置你与其各一张牌，此牌的伤害值基数+1。",
	["@fengshix"] = "是否使用%src的“锋势”",
	
	
	["#heg_liuqi"] = "居外而安",
	["designer:heg_liuqi"] = "荼蘼（韩旭）",
	["heg_liuqi"] = "刘琦[国]",
	["illustrator:heg_liuqi"] = "绘聚艺堂",
	["heg_wenji"] = "问计",
	[":heg_wenji"] = "出牌阶段开始时，你可令一名其他角色将其一张牌交给你（正面朝上移动）▶若其：与你势力不同，你将一张不为你以此法得到的牌以外的牌交给其（正面朝上移动）；"..
		"与你势力相同或没有势力，你于此回合内使用对应的实体牌中有你以此法得到的牌的牌无距离关系的限制且无次数限制→"..
		"当牌于此回合内被使用时，若此牌对应的实体牌中（曾）有你以此法得到的牌，你令所有角色不能响应此次使用的牌。",
	["heg_tunjiang"] = "屯江",
	[":heg_tunjiang"] = "结束阶段开始时，若你于出牌阶段内使用过牌且未对其他角色使用过牌，你可摸X张牌（X为势力数）。",
	["@wenji"] = "是否使用“问计”，选择一名其他角色",
	["@wenji-give"] = "问计：选择一张牌交给 %src",
	["#heg_WenjiEffect"] = "%from 的%arg效果生效，【%arg2】不能被响应",
	
	["#heg_shixie"] = "百粤灵欹",
	["designer:heg_shixie"] = "韩旭",
	["heg_shixie"] = "士燮[国]",
	["illustrator:heg_shixie"] = "磐蒲",
	["heg_biluan"] = "避乱",
	[":heg_biluan"] = "锁定技，其他角色至你的距离+X（X=max{你装备区里的牌数,1}）。",
	["heg_lixia"] = "礼下",
	[":heg_lixia"] = "一名角色的准备阶段开始时，若其与你势力不同，（你令）其可弃置你装备区里的一张牌▶若你以此法被弃置过牌，其选择：1.弃置两张牌；2.失去1点体力；3.令你摸两张牌。",

	["@lixia"] = "是否使用%src的“礼下”",
	["@lixia-choose"] = "礼下：请选择执行的操作",
	["heg_lixia_effect:discard"] = "弃置两张牌",
	["heg_lixia_effect:losehp"] = "失去1点体力",
	["heg_lixia_effect:draw"] = "令%from摸两张牌",

	["#heg_zhonghui"] = "桀骜的野心家",
	["designer:heg_zhonghui"] = "韩旭",
	["heg_zhonghui"] = "钟会[国]",
	["illustrator:heg_zhonghui"] = "磐蒲",
	["heg_quanji"] = "权计",
	[":heg_quanji"] = "①当你受到伤害后，若你于当前回合内未发动过此技能，你可摸一张牌▶你将一张牌置于武将牌上（称为“权”）。"..
		"②当你造成伤害后，若你于当前回合内未发动过此技能，你可摸一张牌▶你将一张牌置于武将牌上（称为“权”）。"..
		"③你的手牌上限+X（X为“权”数）。",
	["heg_paiyi"] = "排异",
	[":heg_paiyi"] = "出牌阶段限一次，你可将一张“权”置入弃牌堆并选择一名角色▶其摸X张牌（X=min{“权”数,7}）。若其手牌数大于你，你对其造成1点普通伤害。",

	["@quanji-push"] = "权计：选择一张牌作为“权”",
	["power_pile"] = "权",

	["#heg_dongzhao"] = "移尊易鼎",
	["heg_dongzhao"] = "董昭[国]",
	["illustrator:heg_dongzhao"] = "小牛",
	["designer:heg_dongzhao"] = "逍遥鱼叔",
	["heg_quanjin"] = "劝进",
	[":heg_quanjin"] = "出牌阶段限一次，你可将一张手牌交给于此阶段内受到过伤害的角色▶其选择是否执行军令。若其选择：是，你摸一张牌；"..
		"否且你不是手牌数最大的角色，你摸X张牌（X=min{手牌数最大的角色的手牌数-你的手牌数,5}）。",
	["heg_zaoyun"] = "凿运",
	[":heg_zaoyun"] = "出牌阶段限一次，你可选择与你势力不同且你至其距离大于1的角色并弃置X张手牌（X为你至其距离-1）▶你至其的距离于此回合内视为1，你对其造成1点普通伤害。",

	["#heg_xushu"] = "难为完臣",
	["heg_xushu"] = "徐庶[国]",
	["illustrator:heg_xushu"] = "YanBai",
	["heg_wuyan"] = "无言",
	[":heg_wuyan"] = "魏势力技。锁定技，当你造成渠道为锦囊牌的伤害时，你防止此伤害。锁定技，当你受到渠道为锦囊牌的伤害时，你防止此伤害。",
	["heg_jianyan"] = "建言",
	[":heg_jianyan"] = "蜀势力技。出牌阶段限一次，你可选择一种类别/颜色▶若牌堆里：有此类别/颜色的牌，{系统亮出牌堆里的第一张为此类别/颜色的牌。你令一名男性角色获得此牌}；没有此类别/颜色的牌且弃牌堆里有此类别/颜色的牌，洗牌，{系统亮出牌堆里的第一张为此类别/颜色的牌。你令一名男性角色获得此牌}。",
	["heg_jujian"] = "举荐",
	[":heg_jujian"] = "结束阶段开始时，你可弃置一张不为基本牌的牌并选择一名与你势力相同的其他角色▶你令其变更，若变更后的武将牌含有锁定技，你与其各摸两张牌。",
	["@jianyan-choice"] = "建言：选择你要检索的卡牌种类（颜色或者类别）",
	["@jianyan-give"] = "建言：请选择一名男性角色，将展示的卡牌【%arg %arg2】交给该角色",
	["#heg_JianyanChoice"] = "%from 选择了 %arg",
	["#heg_JianyanFail"] = "牌堆和弃牌堆中均没有 %arg ，结算终止",
	["@jujian-card"] = "是否使用“举荐”，弃置一张非基本牌并选择一名角色",

	["#heg_wujing"] = "汗马鎏金",
	["heg_wujing"] = "吴景[国]",
	["illustrator:heg_wujing"] = "小牛",
	["designer:heg_wujing"] = "逍遥鱼叔",
	["heg_diaogui"] = "调归",
	[":heg_diaogui"] = "出牌阶段限一次，你可使用对应的实体牌为一张装备牌的【调虎离山】▶若有于你使用此牌之前不相邻且均与你势力相同的相邻的角色，"..
		"你摸X张牌（X为这些角色中与其处于同一队列的角色数的最大值）。",
	["heg_fengyang"] = "风扬",
	[":heg_fengyang"] = "阵法技，当有牌因与你势力不同的角色的弃置/获得而移动前，你取消与你处于同一队列的角色的装备区里的牌的此次移动。",

	["#heg_DiaoguiCheak"] = "调归检测%from的下家和上家分别是%to",

	["#heg_yanbaihu"] = "豺牙落涧",
	["heg_yanbaihu"] = "严白虎[国]",
	["illustrator:heg_yanbaihu"] = "YanBai",
	["designer:heg_yanbaihu"] = "逍遥鱼叔",
	["heg_zhidao"] = "雉盗",
	[":heg_zhidao"] = "锁定技，出牌阶段开始时，你选择一名其他角色▶你至其的距离于此回合内视为1，除其外的的其他角色于此回合内不是你使用牌的合法目标"..
		"(→)你于此回合出牌阶段内对其造成伤害后，若你于此阶段内于造成此伤害之前未对其造成过伤害，你获得其区域里的一张牌。",
	["heg_jilix"] = "寄篱",
	[":heg_jilix"] = "锁定技，①当红色基本牌或除【联军盛宴】外的红色普通锦囊牌结算结束后，若此牌的最终目标数为1且你是此目标对应的角色，"..
		"使用者对你使用无对应的实体牌且与此牌牌名相同的牌（无距离关系和目标的限制）。②当你受到伤害时，若你于此阶段内受到过伤害的次数为1，你防止此伤害▶你移除此武将牌。",

	["@zhidao-target"] = "雉盗：选择一名其他角色",
	["#heg_ZhidaoEffect"] = "%from 因“%arg”的效果，获得 %to 区域里的一张牌",
	
	["heg_jilix:target"] = "是否使用“寄篱”，令%src再视为对你使用一次【%arg】",
	["heg_jilix:damage"] = "是否使用“寄篱”，防止受到的伤害并移除此武将牌",

	["HImperialEdict"] = "诏书",
	["ImperialEdict"] = "诏书",
	["heg_ImperialEdict"] = "诏书",
	[":heg_ImperialEdict"] = "装备牌·宝物\n\n技能：\n" ..
					"1. 与你势力相同的角色的出牌阶段限一次，若其：是小势力角色，其可将至多两张手牌置于你的武将牌上（均称为“诏”）；"..
					"不是小势力角色，其可将一张手牌置于你的武将牌上（称为“诏”）。\n" ..
					"2. 出牌阶段限一次，若有四张不同花色的“诏”，你可将所有“诏”置入弃牌堆，你从势力锦囊牌堆中随机获得一张牌。\n" ,

	["heg_imperialedictattach"] = "放置手牌",
	[":heg_imperialedictattach"] = "出牌阶段限一次，若你：是小势力角色，你可将至多两张手牌置于【诏书】上（均称为“诏”）；不是小势力角色，你可将一张手牌置于【诏书】上（称为“诏”）。",
	["heg_imperialedicttrick"] = "获得锦囊",
	[":heg_imperialedicttrick"] = "出牌阶段限一次，若有四张不同花色的“诏”，你可将所有“诏”置入弃牌堆，你从势力锦囊牌堆中随机获得一张牌。",

	["HRuleTheWorld"] = "号令天下",
	["rule_the_world"] = "号令天下",
	[":heg_rule_the_world"] = "锦囊牌·魏\n\n使用时机：出牌阶段。\n使用目标：一名体力值不是最小的角色。\n作用效果：除目标对应的角色外的角色各选择："..
		"1.{若其势力：为魏，其对目标对应的角色使用无对应的实体牌的普【杀】；不为魏，其弃置一张手牌▷其对目标对应的角色使用无对应的实体牌的普【杀】}；"..
		"2.{若其势力：为魏，其获得目标对应的角色的一张牌；不为魏，其弃置目标对应的角色的一张牌}；3.获得1枚“令”。",
	["heg_rule_the_world:slash"] = "%log视为对%to使用【杀】",
	["heg_rule_the_world:discard"] = "%log%to的一张牌",
	["heg_rule_the_world_slash"] = "弃置一张牌并",
	["heg_rule_the_world_discard"] = "弃置",
	["heg_rule_the_world_getcard"] = "获得",
	["@rule_the_world-slash"] = "号令天下：弃置一张手牌，视为对%dest使用【杀】",

	["HConquering"] = "克复中原",
	["conquering"] = "克复中原",
	[":heg_conquering"] = "锦囊牌·蜀\n\n使用时机：出牌阶段。\n使用目标：至少一名角色。\n作用效果：目标对应的角色选择："..
		"1.使用无对应的实体牌的普【杀】（有距离关系的限制。若其势力为蜀，此【杀】的伤害值基数+1）；2.摸一张牌。",
	["@conquering-slash"] = "克复中原：可视为使用一张【杀】，或点取消摸牌",

	["HConsolidateCountry"] = "固国安邦",
	["consolidate_country"] = "固国安邦",
	[":heg_consolidate_country"] = "锦囊牌·吴\n\n使用时机：出牌阶段。\n使用目标：你。\n作用效果：目标对应的角色摸八张牌，选择能弃置的至少六张手牌。"..
		"{若其势力为吴，其可将其以此法选择的牌中的至多六张交给与其势力相同的其他角色}。其弃置其以此法选择的牌。",

	["@consolidate_country-discard"] = "固国安邦：选择6张手牌弃置",
	["@consolidate_country-give"] = "固国安邦：可将这些牌分配给与你势力相同的角色，其余的弃置",

	["HChaos"] = "文和乱武",
	["chaos"] = "文和乱武",
	[":heg_chaos"] = "锦囊牌·群\n\n使用时机：出牌阶段。\n使用目标：所有角色。\n作用效果：目标对应的角色展示所有手牌▷{你选择：1.若其手牌区里的其能弃置的牌的类别："..
		"{不均相同，你令其弃置两张类别不同的手牌；均相同，你令其弃置一张手牌}；2.观看其手牌并弃置其中的一张}▷若其势力为群且其没有手牌，其将其手牌补至X张（X为其体力值）。",
	
	["heg_chaos:letdiscard"] = "令%to弃置两张不同类别的手牌",
	["heg_chaos:discard"] = "弃置%to的一张手牌",

	["@chaos-select"] = "文和乱武：选择手牌中两张不同类别的牌弃置",

	["@trick-show"] = "是否明置一张武将牌来使【%arg】获得额外的效果",
	["heg_trick_show:show_head"] = "明置主将",
	["heg_trick_show:show_deputy"] = "明置副将",

	["#heg_simazhao"] = "嘲风开天",
	["designer:heg_simazhao"] = "韩旭",
	["heg_simazhao"] = "司马昭[国]",
	["illustrator:heg_simazhao"] = "凝聚永恒",
	["heg_suzhi"] = "夙智",
	[":heg_suzhi"] = "锁定技，①当你于回合内因执行【杀】或【决斗】的效果而造成伤害时，若使用者为你且X＜3，你令伤害值+1。②当非转化的锦囊牌于你的回合内被使用时，"..
		"若使用者为你且X＜3，你摸一张牌。③当其他角色的牌于你的回合内因弃置而置入弃牌堆后，若X＜3，你获得其一张牌。④若X＜3，你于回合内使用非转化的锦囊牌无距离关系的限制。"..
		"⑤回合结束前，若X＜3，你于你的下个回合开始之前拥有〖反馈〗。（X为你于此回合内发动〖夙智①〗、〖夙智②〗和〖夙智③〗的次数之和）",
	["heg_zhaoxin"] = "昭心",
	[":heg_zhaoxin"] = "当你受到伤害后，你可展示所有手牌▶你与一名手牌数不大于你的其他角色交换手牌。",
	["@zhaoxin-exchange"] = "昭心：选择一名手牌数不大于你的角色交换手牌",
	["heg_fankui_simazhao"] = "反馈",
	
	
	["#heg_xuyou"] = "毕方矫翼",
	["heg_xuyou"] = "许攸[国]",
	["designer:heg_xuyou"] = "逍遥鱼叔",
	["illustrator:heg_xuyou"] = "猎枭",
	["heg_chenglve"] = "成略",
	[":heg_chenglve"] = "当牌使用结算结束后，若使用者与你势力相同且此牌的目标数大于1，你可令其摸一张牌▶若你受到过渠道为此牌的伤害，"..
		"你可令一名与你势力相同且两张武将牌均处于明置状态且没有{“先驱”、“珠联璧合”、“阴阳鱼”和“野心家”}的角色获得1枚“阴阳鱼”。",
	["heg_shicai"] = "恃才",
	[":heg_shicai"] = "锁定技，当你受到伤害后，若伤害值：为1，你摸一张牌；大于1，你弃置两张牌。",

	["@chenglve-mark"] = "成略：选择一名角色获得“阴阳鱼”标记",

	["#heg_xiahouba"] = "棘途壮志",
	["heg_xiahouba"] = "夏侯霸[国]",
	["designer:heg_xiahouba"] = "逍遥鱼叔",
	["illustrator:heg_xiahouba"] = "小牛",
	["heg_baolie"] = "豹烈",
	[":heg_baolie"] = "锁定技，①出牌阶段开始时，你选择所有攻击范围内有你且（你明置后会）与你势力不同的角色▶这些角色各{需对包括你在内的角色使用【杀】，否则你弃置其一张牌}。"..
		"②你对体力值不小于你的角色使用【杀】无次数限制且无距离关系的限制。",
	
	["@baolie-slash"] = "豹烈：对%src使用一张【杀】，否则其弃置你一张牌",

	["#heg_zhugeke"] = "兴家赤族",
	["heg_zhugeke"] = "诸葛恪[国]",
	["designer:heg_zhugeke"] = "逍遥鱼叔",
	["illustrator:heg_zhugeke"] = "猎枭",
	["heg_aocai"] = "傲才",
	[":heg_aocai"] = "当你于回合外需要使用/打出基本牌时，你可观看牌堆顶的两张牌并可使用/打出对应的实体牌为其中的与此基本牌牌名相同的一张牌且与此基本牌牌名相同的牌。",
	["heg_duwu"] = "黩武",
	[":heg_duwu"] = "限定技，出牌阶段，你可选择攻击范围内所有（你明置后会）与你势力不同或没有势力的角色并选择军令▶这些角色各"..
		"{选择是否执行军令，若其选择否，你对其造成1点普通伤害，你摸一张牌}，若有于此技能结算中进入过濒死状态且存活的角色，你失去1点体力。",
	
	["#heg_aocai"] = "傲才",
	["@aocai-view"] = "请从“傲才”牌中选择一张合适的牌",

	["#heg_sunchen"] = "食髓的朝堂客",
	["heg_sunchen"] = "孙綝[国]",
	["designer:heg_sunchen"] = "逍遥鱼叔",
	["illustrator:heg_sunchen"] = "depp",
	["heg_shilu"] = "嗜戮",
	[":heg_shilu"] = "①当一名角色死亡后，你可将其所有武将牌置于你的武将牌上（均称为“戮”）▶若其是你杀死的，你随机将武将牌里的两张武将牌置于你的武将牌上（均称为“戮”）。"..
		"②准备阶段开始时，若你有“戮”，你可弃置至多X张牌（X为“戮”数）▶你摸等量的牌。",
	["heg_xiongnve"] = "凶虐",
	[":heg_xiongnve"] = "①出牌阶段开始时，你可将一张“戮”置入武将牌堆▶你选择：1.(→)当你于此阶段内对与你以此法置入武将牌堆的“戮”代表的武将势力相同的角色造成伤害时，你令伤害值+1；"..
		"2.(→)当你于此阶段内对与你以此法置入武将牌堆的“戮”代表的武将势力相同的角色造成伤害时，你获得其一张牌；"..
		"3.你于此阶段内对与你以此法置入武将牌堆的“戮”代表的武将势力相同的角色使用牌无次数限制。"..
		"②出牌阶段结束时，你可将两张“戮”置入武将牌堆▶(→)当你于你的下个回合开始之前受到其他角色造成的伤害时，你令伤害值-1。",

	["heg_massacre"] = "戮",
	["@shilu"] = "是否使用“嗜戮”，弃置至多%arg张牌并摸等量的牌",
	["@xiongnve-continue"] = "凶虐：是否继续弃置“戮”",
	
	["#heg_GetMassacreDetail"] = "%from 获得了“戮” %arg",
	["#heg_dropMassacreDetail"] = "%from 丢弃了“戮” %arg",
	["#heg_GetMassacre"] = "%from 获得了 %arg 张“戮”",
	
	["@xiongnve-choice"] = "凶虐：选择一项效果",
	["heg_xiongnve:adddamage"] = "造成的伤害+1",
	["heg_xiongnve:extraction"] = "造成伤害时获得其一张牌",
	["heg_xiongnve:nolimit"] = "使用牌无次数限制",
	
	["#heg_xiongnveAdddamage"] = "%from 本回合对势力为 %arg 的角色造成的伤害+1",
	["#heg_xiongnveExtraction"] = "%from 本回合对势力为 %arg 的角色造成伤害时获得其一张牌",
	["#heg_xiongnveNolimit"] = "%from 本回合对势力为 %arg 的角色使用牌无次数限制",
	
	["heg_xiongnve_avoid"] = "凶虐减伤",

	["heg_xiongnve:attack"] = "是否使用“凶虐”，弃置一张“戮”来发动对应势力的效果",
	["heg_xiongnve:defence"] = "是否使用“凶虐”，弃置两张“戮”来令受到的伤害-1",




	["#heg_panjun"] = "逆鳞之砥",
	["heg_panjun"] = "潘濬[国]",
	["illustrator:heg_panjun"] = "Domi",
	["designer:heg_panjun"] = "逍遥鱼叔",
	["heg_congcha"] = "聪察",
	[":heg_congcha"] = "①准备阶段开始时，你可选择一名没有势力的角色▶(→)当其于你的下个回合开始之前第一次明置武将牌后，若其与你：势力相同，你与其各摸两张牌；势力不同，其失去1点体力。"..
		"②摸牌阶段，若所有角色均有势力，你可令额定摸牌数+2。",
	["heg_gongqing"] = "公清",
	[":heg_gongqing"] = "锁定技，①当你受到伤害时，若来源的攻击范围小于3且伤害值大于1，你将伤害值改为1。②当你受到伤害时，若来源的攻击范围大于3，你令伤害值+1。",

	["@congcha-target"] = "是否使用“聪察”，选择一名没有势力的角色",

	["#heg_wenqin"] = "勇而无算",
	["heg_wenqin"] = "文钦[国]",
	["illustrator:heg_wenqin"] = "匠人绘-零二",
	["designer:heg_wenqin"] = "逍遥鱼叔",
	["heg_jinfa"] = "矜伐",
	[":heg_jinfa"] = "出牌阶段限一次，你可弃置一张牌并选择一名有牌的其他角色▶其选择：1.令你获得其一张牌；2.将一张装备牌交给你，若此牌在你的手牌区里为黑桃，"..
		"其对你使用无对应的实体牌的普【杀】。",
	["@jinfa-give"] = "矜伐：选择一张装备牌交给%src，或点取消令%src获得你一张牌",

	["#heg_huangzu"] = "遮山扼江",
	["heg_huangzu"] = "黄祖[国]",
	["designer:heg_huangzu"] = "逍遥鱼叔",
	["illustrator:heg_huangzu"] = "YanBai",
	["heg_xishe"] = "袭射",
	[":heg_xishe"] = "其他角色的准备阶段开始时，若其存活，你可弃置装备区里的一张牌▶你对其使用无对应的实体牌的普【杀】（无距离关系的限制，若其体力值小于你，所有角色不能响应此【杀】），"..
		"你可重复此流程→此回合结束前，若你于此回合内因渠道为此【杀】的伤害而杀死过角色，你可变更（以此法作为你的副将的武将牌处于暗置状态）。",
	["@xishe-slash"] = "是否使用“袭射”，弃置装备区里的牌视为对%src使用【杀】",
	
	["#heg_gongsunyuan"] = "狡黠的投机者",
	["heg_gongsunyuan"] = "公孙渊[国]",
	["designer:heg_gongsunyuan"] = "逍遥鱼叔",
	["illustrator:heg_gongsunyuan"] = "猎枭",
	["heg_huaiyi"] = "怀异",
	[":heg_huaiyi"] = "出牌阶段限一次，你可展示所有手牌▶若不为同一颜色，你选择一种颜色，弃置所有为此颜色的手牌，选择至多X名有牌的其他角色，"..
		"这些角色各{你选择其一张牌，若此牌：为装备牌，你将此牌置于武将牌上；不为装备牌，你获得此牌}。",
	["heg_zisui"] = "恣睢",
	[":heg_zisui"] = "锁定技，①摸牌阶段，若有“异”，你多摸X张牌（X为“异”数）。②结束阶段开始时，若“异”数大于你的体力上限，你死亡。",

	["@huaiyi-choose"] = "怀异：选择弃置一种颜色的手牌",
	["@huaiyi-snatch"] = "怀异：选择至多%arg名角色，获得这些角色各一张牌",
	["heg_huaiyi:red"] = "红色",
	["heg_huaiyi:black"] = "黑色",
	
	["heg_disloyalty"] = "异",

	["#heg_pengyang"] = "误身的狂士",
	["heg_pengyang"] = "彭羕[国]",
	["illustrator:heg_pengyang"] = "匠人绘-零一",
	["designer:heg_pengyang"] = "韩旭",
	["heg_tongling"] = "通令",
	[":heg_tongling"] = "当你于出牌阶段内对一名角色A造成伤害后，若A存活且与你势力不同且你于此阶段内未发动过此技能，你可选择一名与你势力相同的角色B▶"..
		"B可对包括A在内的角色使用对应的实体牌是B的一张手牌且与此实体牌牌名相同的牌▷若此牌：造成过伤害，你摸两张牌，若B不为你，B摸两张牌；"..
		"未造成过伤害，A获得是你此次造成的伤害的渠道的牌对应的所有实体牌。",
	["heg_jinxian"] = "近陷",
	[":heg_jinxian"] = "当你明置此武将牌后，你选择所有你至其距离＜2的角色▶这些角色各{若其：所有武将牌均处于明置状态，"..
		"其暗置一张不为君主武将牌且不为士兵牌的武将牌；有不处于明置状态的武将牌，其弃置两张牌}。",

	["@tongling-invoke"] = "是否使用“通令”，选择一名角色，令其对%dest使用一张牌",
	["@tongling-usecard"] = "通令：选择对%dest使用的牌",
	["@jinxian-hide"] = "近陷：选择暗置自己的一张武将牌",
	["heg_jinxian_hide:head"] = "暗置主将",
	["heg_jinxian_hide:deputy"] = "暗置副将",

	["#heg_sufei"] = "诤友投明",
	["heg_sufei"] = "苏飞[国]",
	["designer:heg_sufei"] = "逍遥鱼叔",
	["illustrator:heg_sufei"] = "Domi",
	["heg_lianpian"] = "联翩",
	[":heg_lianpian"] = "①结束阶段开始时，若你于此回合内弃置过所有角色的牌数之和大于你的体力值，你可令一名与你势力相同的角色将手牌补至X张（X为其体力上限）。"..
		"②其他角色的结束阶段开始时，若其于此回合内弃置过所有角色的牌数之和大于你的体力值，（你令）其可选择：1.弃置你的一张牌；2.令你回复1点体力。",

	["@lianpian-target"] = "是否使用“联翩”，选择一名角色将手牌补至体力上限",
	["@lianpian"] = "是否使用%src的“联翩”",
	["heg_lianpian:discard"] = "弃置其一张牌",
	["heg_lianpian:recover"] = "令其回复体力",

	["#heg_liuba"] = "清河一鲲",
	["heg_liuba"] = "刘巴[国]",
	["illustrator:heg_liuba"] = "Mr_Sleeping",
	["designer:heg_liuba"] = "逍遥鱼叔",
	["heg_tongdu"] = "统度",
	[":heg_tongdu"] = "与你势力相同的角色的结束阶段开始时，（你令）其可摸X张牌（X=min{其于此回合的弃牌阶段内弃置过的牌数, 3}）。",
	["heg_qingyin"] = "清隐",
	[":heg_qingyin"] = "限定技，出牌阶段，你可选择所有（你明置后会）与你势力相同的角色▶这些角色各回复X点体力（X为其体力上限-体力值）。你移除此武将牌。",
	["@tongdu"] = "是否使用%src的“统度”",

	["#heg_zhuling"] = "五子之亚",
	["heg_zhuling"] = "朱灵[国]",
	["designer:heg_zhuling"] = "逍遥鱼叔",
	["illustrator:heg_zhuling"] = "YanBai",
	["heg_juejue"] = "决绝",
	[":heg_juejue"] = "①弃牌阶段开始时，你可失去1点体力▶(→)此阶段结束时，若你于此阶段内弃置过你的牌，你令其他角色各选择：1.将X张手牌置入弃牌堆（X为你于此阶段内弃置过你的牌数）；"..
		"2.受到你造成的1点普通伤害。②你杀死与你势力相同的角色不执行奖惩。",
	["heg_fangyuan"] = "方圆",
	[":heg_fangyuan"] = "阵法技，①你为围攻角色的围攻关系中的围攻角色的手牌上限+1。②你为围攻角色的围攻关系中的被围攻角色的手牌上限-1。"..
		"③结束阶段开始时，你对为你为被围攻角色的围攻关系中的围攻角色的一名角色使用无对应的实体牌的普【杀】。",
	["@fangyuan-slash"] = "方圆：选择一名围攻你的角色，视为对其使用【杀】",
	
	["@juejue-discard"] = "决绝：选择%arg张手牌置入弃牌堆，或%src对你造成1点伤害",
	

	["#heg_test"] = "%arg",
}
