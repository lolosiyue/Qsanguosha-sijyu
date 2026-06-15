-- translations for Basara mode

return
{
	["Basara"] = "暗将",
	["#BasaraReveal"] = "%from 展示了武将 %arg",
	["#BasaraRevealDual"] = "%from 展示了武将，主将为 %arg，副将为 %arg2",
	["RevealGeneral"] = "展示武将",
	["anjiang"] = "暗将",
	["#BasaraGeneralChosen"] = "你选择的武将为 %arg",
	["#BasaraGeneralChosenDual"] = "你选择的武将为 %arg 和 %arg2",
	["Hegemony"] = "国战",
	["Roles"] = "身份",
	["~anjiang"] = "死不瞑目啊……",

	["Companions"] = "珠联璧合",
	["CompanionEffect"] = "珠联璧合",
	["#CompanionEffect"] = "%from 触发珠联璧合",

	["companion"] = "珠联璧合",
	[":companion"] = "限定技，①出牌阶段，你可摸两张牌。②当你需要使用【桃】时，你可使用无对应的实体牌的【桃】。",
	["companion:peach"] = "视为使用桃",
	["companion:draw"] = "摸两张牌",
	["@companion-choose"] = "珠联璧合发动，请选择要执行的效果",

	["CompanionCard"] = "珠联璧合",
	[":CompanionCard"] = "标记牌\n\n使用方法Ⅰ：\n出牌阶段，你可弃1枚"珠联璧合"，摸两张牌。\n\n使用方法Ⅱ：\n当你需要使用【桃】时，你可弃1枚"珠联璧合"，你使用无对应的实体牌的【桃】。",

	["halfmaxhp"] = "阴阳鱼",
	[":halfmaxhp"] = "限定技，①出牌阶段，你可摸一张牌。②弃牌阶段弃牌时，你可令你的手牌上限于此回合内+2。",
	["@halfmaxhp-use"] = "是否弃置阴阳鱼标记，令你本回合的手牌上限+2",

	["HalfMaxHpCard"] = "阴阳鱼",
	[":HalfMaxHpCard"] = "标记牌\n\n使用方法Ⅰ：\n出牌阶段，你可弃1枚"阴阳鱼"，摸一张牌。\n\n使用方法Ⅱ：\n弃牌阶段开始时，若你的手牌数大于你的手牌上限，你可弃1枚"阴阳鱼"，你的手牌上限于此回合内+2。",

	["firstshow"] = "先驱",
	[":firstshow"] = "限定技，出牌阶段，你可将手牌补至四张，然后可观看一名角色的一张暗置的武将牌。",
	["@firstshow-see"] = "先驱：请选择一名角色，观看其一张暗置武将牌",
	["firstshow_see"] = "先驱",
	["@firstshow-choose"] = "先驱：请选择观看的%dest的武将牌",

	["FirstShowCard"] = "先驱",
	[":FirstShowCard"] = "标记牌\n\n出牌阶段，若你的手牌数小于4或场上有有暗置的武将牌的其他角色，你可弃1枚"先驱"，将手牌补至4张，观看一名其他角色的一张暗置的武将牌。",

	["careerman"] = "野心家",
	["CareermanCard"] = "野心家",
	[":CareermanCard"] = "标记牌\n\n使用方法Ⅰ：\n出牌阶段，你可弃1枚"野心家"，选择：1.摸两张牌；2.摸一张牌；3.将手牌补至四张，观看一名其他角色的一张暗置的武将牌。\n\n使用方法Ⅱ：\n当你需要使用【桃】时，你可弃1枚"野心家"，你使用无对应的实体牌的【桃】。\n\n使用方法Ⅲ：\n弃牌阶段开始时，若你的手牌数大于你的手牌上限，你可弃1枚"野心家"，你的手牌上限于此回合内+2。",
	["careerman:draw1card"] = "摸一张牌",
	["careerman:draw2cards"] = "摸两张牌",
	["careerman:peach"] = "视为使用【桃】",
	["careerman:firstshow"] = "将手牌补至四张，观看一张暗置武将牌",
	["@careerman-target"] = "野心家：选择一名角色发动"先驱"的效果",
	["@careerman-use"] = "是否弃置野心家标记，令你本回合的手牌上限+2",
	["@careerman-choose"] = "野心家发动，请选择要执行的效果",
}