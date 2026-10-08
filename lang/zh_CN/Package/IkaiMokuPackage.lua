-- translation for IkaiMokuPackage (ported from TouhouTripleSha)

return {
	["ikai-moku"] = "异世界的木阴",
	
--wind
	["#wind008"] = "风华绝代的圣者",
	["wind008"] = "桔梗",--风 - 空 - 4血
	["designer:wind008"] = "游卡桌游",
	["illustrator:wind008"] = "IHET",
	["ikliegong"] = "烈弓",
	[":ikliegong"] = "出牌阶段，当你使用【杀】指定一名角色为目标后，若其手牌数不小于你的体力值或者不大于你的攻击范围，你可以令此【杀】不是【闪】的合法目标，然后令该角色的非锁定技无效，直到回合结束。",
	["ikhuanghun"] = "荒魂",
	[":ikhuanghun"] = "出牌阶段，你可以重铸一张锦囊牌。",

	--thjibu

	["#wind010"] = "神风的不死鸟",
	["wind010"] = "响",--风 - 空 - 3血
	["designer:wind010"] = "游卡桌游",
	["illustrator:wind010"] = "リン☆ユウ",
	["ikfuhua"] = "缚华",
	[":ikfuhua"] = "你可以将一张梅花牌当【铁索连环】使用或重铸；当你使用【铁索连环】仅指定一个目标时，你摸一张牌。",
	["#ikfuhua"] = "缚华",
	["iksuinie"] = "碎涅",
	[":iksuinie"] = "限定技，出牌阶段，或当你处于濒死状态时，你可以弃置你判定区里所有的牌，然后将武将牌恢复至游戏开始时的状态，再摸三张牌，最后将体力值回复至3点。",

	["ikjingnie"] = "净涅",

	["#wind012"] = "堕天的圣黑猫",
	["wind012"] = "五更瑠璃",--风 - 空 - 4血
	["&wind012"] = "瑠璃",
	["designer:wind012"] = "游卡桌游",
	["illustrator:wind012"] = "キムラダイスケ",
	["ikshengtian"] = "圣天",
	[":ikshengtian"] = "觉醒技，准备阶段开始时，若你没有手牌，你须回复1点体力或摸两张牌，然后减少1点体力上限，并获得技能“玄舞”和“墨华”（锁定技，你的方块牌均视为梅花牌。）。",
	["ikshengtian:draw"] = "摸两张牌",
	["ikshengtian:recover"] = "回复1点体力",
	["#IkShengtianWake"] = "%from 没有手牌，触发“%arg”觉醒",
	["ikmohua"] = "墨华",
	[":ikmohua"] = "锁定技，你的方块牌均视为梅花牌。",

	["#wind013"] = "捣蛋万岁",
	["wind013"] = "岁纳京子",--风 - 空 - 4血
	["&wind013"] = "京子",
	["designer:wind013"] = "游卡桌游",
	["illustrator:wind013"] = "みわべさくら",
	["ikhuoshou"] = "祸首",
	["#sa_avoid_ikhuoshou"] = "祸首（无效）",
	["ikzailuan"] = "再乱",
	[":ikzailuan"] = "摸牌阶段开始时，若你已受伤，你可放弃摸牌，改为展示牌堆顶的X张牌（X为你已损失的体力值），其中每有一张红桃牌，你回复1点体力或摸两张牌，然后弃置这些红桃牌，并获得其余的牌。",
	["ikzailuan:recover"] = "回复1点体力",
	["ikzailuan:draw"] = "摸两张牌",

	["#wind014"] = "水神的祝福",
	["wind014"] = "阿库娅",--风 - 空 - 3血
	["designer:wind014"] = "游卡桌游",
	["illustrator:wind014"] = "Tam-U",
	["ikyouji"] = "攸祭",
	["ikruoyu"] = "若愚",
	[":ikruoyu"] = "主公技，觉醒技，准备阶段开始时，若你的体力值是全场最少的（或之一），你须增加1点体力上限，回复1点体力，并获得技能“心契”。",
	["#IkRuoyuWake"] = "%from 的体力值 %arg 为场上最少，触发“%arg2”觉醒",

	["#wind015"] = "绯色菖蒲",
	["wind015"] = "星伽白雪",--风 - 幻 - 4血
	["&wind015"] = "白雪",
	["designer:wind015"] = "游卡桌游",
	["illustrator:wind015"] = "暴力にゃ長",
	["#sa_avoid_ikjugui"] = "巨鬼",
	["iklieren"] = "烈刃",
	[":iklieren"] = "当你使用【杀】指定目标后，你可以与其拼点，若你赢，你获得其一张牌。",

	["#wind029"] = "狐疑之神",
	["wind029"] = "帆楼",--风 - 空 - 3血
	["designer:wind029"] = "游卡桌游",
	["illustrator:wind029"] = "榎宮祐",
	["luminary"] = "曜",
	["@gale"] = "风",
	["ikmiaowu"] = "渺雾",
	[":ikmiaowu"] = "结束阶段开始时，你可以弃置X张“曜”，并选择X名角色，若如此做，每当这些角色受到的非雷电伤害结算开始时，防止此伤害，直到你的下回合开始。",
	["@fog"] = "雾",
	["@ikmiaowu-card"] = "你可以发动“渺雾”",
	["~ikmiaowu"] = "选择若干名角色→点击确定→然后在窗口中选择相应数量的牌",
	["#IkMiaowuProtect"] = "%from 的“<font color=\"yellow\"><b>渺雾</b></font>”效果被触发，防止了 %arg 点伤害[%arg2]",

	["#wind030"] = "冷夜的花火",
	["wind030"] = "灰原哀",--风 - 空 - 2血
	["&wind030"] = "哀",
	["designer:wind030"] = "游卡桌游",
	["illustrator:wind030"] = "栗川鮫弥",
	["ikzhihun"] = "智魂",
	[":ikzhihun"] = "你可以将同花色的X张牌按下列规则使用或打出：红桃当【桃】，黑桃当具火焰伤害的【杀】，梅花当【闪】，方块当【无懈可击】（X为你当前的体力值且至少为1）。",

--bloom
	["#bloom008"] = "黑扬羽蝶",
	["bloom008"] = "黑雪姬",--花 - 空 - 4血
	["designer:bloom008"] = "游卡桌游",
	["illustrator:bloom008"] = "八神",
	["ikxunyu"] = "迅羽",
	[":ikxunyu"] = "你可以选择一至两项：<br />1.跳过你此回合的判定阶段和摸牌阶段<br />2.跳过你此回合出牌阶段并弃置一张非锦囊牌<br />你每选择一项，视为对一名其他角色使用一张无视距离的【杀】。",
	["@ikxunyu1"] = "你可以跳过判定阶段和摸牌阶段发动“迅羽”",
	["@ikxunyu2"] = "你可以跳过出牌阶段并弃置一张非锦囊牌发动“迅羽”",
	["~ikxunyu1"] = "选择【杀】的目标角色→点击确定",
	["~ikxunyu2"] = "选择一张非锦囊牌→选择【杀】的目标角色→点击确定",

	["#bloom010"] = "汲血的死徒",
	["bloom010"] = "弓塚五月",--花 - 空 - 4血
	["&bloom010"] = "五月",
	["designer:bloom010"] = "游卡桌游",
	["illustrator:bloom010"] = "赤毛のUN",
	["ikkujie"] = "枯界",
	[":ikkujie"] = "你可以将一张黑色非锦囊牌当【兵粮寸断】使用。",
	["ikjieying"] = "竭盈",
	[":ikjieying"] = "锁定技，当一名其他角色跳过摸牌阶段后，你摸一张牌；你可以无视距离对手牌数不小于你的角色使用【兵粮寸断】。",

	["#bloom012"] = "金色之暗",
	["bloom012"] = "伊芙",--花 - 空 - 4血
	["designer:bloom012"] = "游卡桌游",
	["illustrator:bloom012"] = "syokuyou-mogura",
	["ikqiangxi"] = "强袭",
	[":ikqiangxi"] = "出牌阶段限一次，你可以失去1点体力或弃置一张武器牌，并对一名其他角色造成1点伤害。",

	["#bloom014"] = "世界之心",
	["bloom014"] = "凉宫春日",--花 - 空 - 3血
	["&bloom014"] = "春日",
	["designer:bloom014"] = "游卡桌游",
	["illustrator:bloom014"] = "cuteg",
	["iksongwei"] = "颂威",
	[":iksongwei"] = "主公技，其他花势力角色的判定牌为黑色且生效后，可以令你摸一张牌。",

	["assassinate"] = "隐",
	
	["#bloom029"] = "军火巨枭",
	["bloom029"] = "蔻蔻•海克梅迪亚",--花 - 空 - 3血
	["&bloom029"] = "蔻蔻",
	["designer:bloom029"] = "游卡桌游",
	["illustrator:bloom029"] = "硯",
	["ikyihuo"] = "易货",
	[":ikyihuo"] = "其他角色的出牌阶段限一次，若你的装备区有装备牌或武将牌背面朝上，该角色可以选择一张手牌并令你观看之，你可以交给其一张装备牌，然后获得此牌并摸一张牌。",
	["ikyihuov"] = "易货",
	[":ikyihuov"] = "出牌阶段限一次，若蔻蔻的装备区有装备牌或武将牌背面朝上，你可以选择一张手牌并令蔻蔻观看之，蔻蔻可以交给你一张装备牌，然后蔻蔻获得之并摸一张牌。",
	["@ikyihuo-equip"] = "你可以交给 %src 一张装备牌，然后获得该牌并摸一张牌",
	["ikguixin"] = "归心",
	[":ikguixin"] = "每当你受到一次伤害后，若场上存活的角色数小于4或你的武将牌正面朝上，你可分别获得所有其他角色区域的一张牌，然后你将武将牌翻面。",
	
	["#bloom030"] = "寡言的观察者",
	["bloom030"] = "长门有希",--花 - 空 - 4血
	["&bloom030"] = "有希",
	["designer:bloom030"] = "游卡桌游",
	["illustrator:bloom030"] = "poはるのいぶきkiki",
	["@fetter"] = "桎",
	["iktiangai"] = "天盖",
	[":iktiangai"] = "觉醒技，准备阶段开始时，若你拥有4枚或更多的“桎”标记，须减少1点体力上限并获得技能“极略”（弃置1枚“桎”标记以发动下列一项技能：“虚视”、“慧泉”、“死噬”或“隙境”）。",
	["#IkTiangaiWake"] = "%from 的“天枷”为 %arg 个，触发“<font color=\"yellow\"><b>天盖</b></font>”觉醒",
	["ikjilve"] = "极略",
	[":ikjilve"] = "弃置1枚“桎”标记以发动下列一项技能：“虚视”、“慧泉”、“死噬”或“隙境”。",

--snow
	["#snow009"] = "地狱之蝶",
	["snow009"] = "阎魔爱",--雪 - 空 - 4血
	["&snow009"] = "爱",
	["designer:snow009"] = "游卡桌游",
	["illustrator:snow009"] = "有河サトル",
	["ikliangban"] = "良坂",
	[":ikliangban"] = "准备阶段开始时，你可以选择一项：令一名其他角色摸X张牌，然后弃置一张牌；或令一名其他角色摸一张牌，然后弃置X张牌（X为你已损失的体力值且至少为1）。",
	["ikliangban-invoke"] = "你可以发动“良坂”<br/> <b>操作提示</b>: 选择一名其他角色→点击确定<br/>",
	["ikliangban:d1tx"] = "摸一张牌，然后弃置X张牌",
	["ikliangban:dxt1"] = "摸X张牌，然后弃置一张牌",
	["ikdiewu"] = "蝶舞",
	[":ikdiewu"] = "当一名其他角色的手牌因弃置进入弃牌堆时，你可以失去一点体力，若该角色有手牌，该角色需将等量的手牌（不足则全部手牌）置于弃牌堆，然后其获得因弃置而失去的手牌。",
	["@ikdiewu"] = "请将 %arg 张手牌置于弃牌堆",

	["#snow010"] = "天缘之现神",
	["snow010"] = "菲雅",--雪 - 空 - 3血
	["designer:snow010"] = "游卡桌游",
	["illustrator:snow010"] = "鳩月つみき",
	["ikyuanjie"] = "缘结",
	[":ikyuanjie"] = "出牌阶段限一次，你可以选择两名手牌数差不大于三的其他角色，并弃置等同于这两名角色手牌数差的牌，然后交换她们的手牌。",
	["#IkYuanjie"] = "%from (原来 %arg 手牌) 与 %to (原来 %arg2 手牌) 交换了手牌",

	["ikchiqiu"] = "赤秋",
	[":ikchiqiu"] = "锁定技，你的黑桃牌均视为红桃牌。",

	["#snow013"] = "紫阳之吻",
	["snow013"] = "散华礼弥",--雪 - 空 - 4血
	["&snow013"] = "礼弥",
	["designer:snow013"] = "游卡桌游",
	["illustrator:snow013"] = "秋の回忆亚",
	["flower"] = "芳",
	["ikhuapan"] = "花磐",
	[":ikhuapan"] = "每当一名角色因另一名角色的弃置或获得而失去手牌后，你可以失去1点体力，令该失去手牌的角色摸两张牌。",

	["#snow014"] = "远方的苇莺",
	["snow014"] = "远野秋叶",--雪 - 空 - 4血
	["&snow014"] = "秋叶",
	["designer:snow014"] = "游卡桌游",
	["illustrator:snow014"] = "ここのび",
	["ikchizhu"] = "赤主",
	[":ikchizhu"] = "觉醒技，准备阶段开始时，若你的体力值为1，你须减少1点体力上限，并回复1点体力或摸两张牌，然后获得技能“沉红”和“良坂”。",
	["#IkChizhuWake"] = "%from 的体力值为 %arg2，触发“%arg”觉醒",
	["ikchizhu:recover"] = "回复1点体力",
	["ikchizhu:draw"] = "摸两张牌",
	["ikbiansheng"] = "遍生",
	[":ikbiansheng"] = "主公技，其他雪势力角色的出牌阶段限一次，可以与你拼点，若该角色没赢，你可以获得双方拼点的牌；“赤主”发动后，你可以拒绝此拼点。",
	["ikbiansheng:pindian"] = "你可以获得双方的拼点牌",
	["ikbiansheng_pindian"] = "遍生",
	[":ikbiansheng_pindian"] = "出牌阶段限一次，你可以与君主拼点，若你没赢，君主可获得双方拼点的牌；“赤主”发动后，君主可以拒绝此拼点。",
	["ikbiansheng_pindian:accept"] = "接受",
	["ikbiansheng_pindian:reject"] = "拒绝",
	["#IkBianshengReject"] = "%from 拒绝 %to 发动“%arg”",

	["greatikyeyan"] = "业焰",
	["smallikyeyan"] = "业焰",
	
--luna
	["#luna001"] = "孢子花的挽歌",
	["luna001"] = "沙耶",--月 - 空 - 8血
	["designer:luna001"] = "游卡桌游",
	["illustrator:luna001"] = "星屑七号",
	["ikhuanbei"] = "幻呗",
	[":ikhuanbei"] = "锁定技，你对其他角色、其他角色对你使用【杀】时，需连续使用两张【闪】才能抵消。",
	["ikwuhua"] = "舞华",
	[":ikwuhua"] = "主公技，每当其他月势力角色造成一次伤害后，可以进行一次判定，若为黑桃，你回复1点体力。",

	["#luna004"] = "辉煌的烈阳",
	["luna004"] = "高町奈叶",--月 - 空 - 4血
	["&luna004"] = "奈叶",
	["designer:luna004"] = "游卡桌游",
	["illustrator:luna004"] = "八城惺架",
	["ikxuzhao"] = "欻照",
	[":ikxuzhao"] = "出牌阶段限一次，你可以失去1点体力并将一张手牌当【调虎离山】使用。",

	["#luna005"] = "印章的白与黑",
	["luna005"] = "玛戈特•奈特＆玛伽•成濑",--月 - 空 - 4血
	["&luna005"] = "玛戈特＆玛伽",
	["designer:luna005"] = "游卡桌游",
	["illustrator:luna005"] = "白鷺六羽",
	["ikjingfa"] = "境法",
	[":ikjingfa"] = "出牌阶段结束时，若你于此阶段没有造成伤害，你可以摸一张牌或弃置场上的一张牌。",
	["@ikjingfa"] = "你可以弃置场上的一张牌，或点“取消”摸一张牌",

	["iksishideng"] = "死噬",
	[":iksishideng"] = "锁定技，在你的回合，除你以外，只有处于濒死状态的角色才能使用【桃】。",
	["#IkSishidengOne"] = "%from 的“%arg”被触发，只能 %from 自救",
	["#IkSishidengTwo"] = "%from 的“%arg”被触发，只有 %from 和 %to 才能救 %to",

	["#luna008"] = "天翔的银狼",
	["luna008"] = "尤丽叶•希格图娜",--月 - 空 - 4血
	["&luna008"] = "尤丽叶",
	--thjibu
	["designer:luna008"] = "游卡桌游",
	["illustrator:luna008"] = "浅葉ゆう",
	["ikkongsa"] = "空飒",
	[":ikkongsa"] = "当你使用的【杀】被目标角色的【闪】抵消时，你可以弃置其一张牌；当你使用红色【杀】对目标角色造成一次伤害后，你可以摸一张牌。",

	["#SetIkHuanshen"] = "%from 声明了 %arg 武将牌上的“%arg2”",
	["#GetIkHuanshen"] = "%from 获得了 %arg 张“形”，现在共有 %arg2 张“形”",
	["#GetIkHuanshenDetail"] = "%from 获得了“形” %arg",
	["iklingqi"] = "灵契",

	["@qihuang"] = "淒煌",

	["#luna014"] = "超电磁炮",
	["luna014"] = "御坂美琴",--月 - 空 - 3血
	["&luna014"] = "美琴",
	["designer:luna014"] = "游卡桌游",
	["illustrator:luna014"] = "三嶋くろね",
	["ikyuji"] = "御姬",
	[":ikyuji"] = "主公技，其他月势力角色的出牌阶段限一次，该角色可以交给你一张【闪】或【八卦阵】。",
	["ikyujiv"] = "御姬",
	[":ikyujiv"] = "出牌阶段限一次，你可以交给君主一张【闪】或【八卦阵】。",

	["@blaze"] = "炽",
	["#@blaze-2"] = "拙火",

	["@iksongwei"] = "“颂威”：请选择令其摸牌的主公",
	["@ikwuhua"] = "“雾华”：请选择发动的主公",
	["@ikxunyu-discard"] = "“迅羽”：请弃置一张非锦囊牌",
	["@suinie"] = "碎涅",
}