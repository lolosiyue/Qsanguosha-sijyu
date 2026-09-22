-- HStandardCardPackage only owns H-specific objects; ordinary cards reuse
-- the canonical translations from Standard/Maneuvering packages.
return {
    ["original_hegemony"] = "国战",
    ["heg_standard"] = "国战标准",
    ["heg_standard_cards"] = "国战标准牌",

    ["heg_SixSwords"] = "吴六剑",
    [":heg_SixSwords"] = "装备牌·武器\n\n攻击范围：2<br/>技能：锁定技，与你势力相同的其他角色的攻击范围+1。",

    ["heg_Triblade"] = "三尖两刃刀",
    [":heg_Triblade"] = "装备牌·武器\n\n攻击范围：3<br/>技能：每当你使用【杀】对目标角色造成伤害后，你可以弃置一张手牌并选择目标角色距离为1的一名其他角色，对其造成1点伤害。",
    ["@heg_Triblade"] = "你可以发动【三尖两刃刀】的效果",
    ["~heg_Triblade"] = "选择一张牌→选择一名角色→点击确定",

    ["heg_nullification"] = "无懈可击·国",
    [":heg_nullification"] = "锦囊牌\n\n使用方法Ⅰ：\n使用时机：一张锦囊牌对一个目标生效前。\n使用目标：一张对一个目标生效前的锦囊牌。\n作用效果：抵消此锦囊牌。"
        .."\n\n使用方法Ⅱ：\n使用时机：一张锦囊牌对一名目标角色生效前。\n使用目标：一张对一名目标角色生效前的锦囊牌。\n作用效果：抵消此牌，然后你选择所有除目标角色外与目标角色势力相同的角色，令所有角色不能使用【无懈可击】响应对这些角色结算的此牌，若如此做，每当此牌对你选择的这些角色中的一名角色生效前，抵消之。",

    ["await_exhausted"] = "以逸待劳",
    [":await_exhausted"] = "锦囊牌\n\n使用时机：出牌阶段。\n使用目标：你和与你势力相同的所有角色。\n作用效果：每名目标角色摸两张牌，然后每名目标角色弃置两张牌。",

    ["known_both"] = "知己知彼",
    [":known_both"] = "锦囊牌\n\n使用时机：出牌阶段。\n使用目标：一名其他角色。\n作用效果：你选择一项：1.观看目标角色的所有手牌；2.观看目标角色的一张暗置的武将牌。\n◆此牌能重铸。",
    ["#KnownBothView"] = "%from 观看了 %to 的 %arg",
    ["$KnownBothViewGeneral"] = "%from 观看了 %to 的 %arg，为 %arg2",

    ["befriend_attacking"] = "远交近攻",
    [":befriend_attacking"] = "锦囊牌\n\n使用时机：出牌阶段。\n使用目标：与你势力不同的一名有明置的武将牌的角色。\n作用效果：目标角色摸一张牌，然后你摸三张牌。",

    -- Physical names retained only for the two H-specific equipment cards.
    ["SixSwords"] = "吴六剑",
    [":SixSwords"] = "装备牌·武器\n\n攻击范围：2<br/>技能：锁定技，与你势力相同的其他角色的攻击范围+1。",
    ["Triblade"] = "三尖两刃刀",
    [":Triblade"] = "装备牌·武器\n\n攻击范围：3<br/>技能：每当你使用【杀】对目标角色造成伤害后，你可以弃置一张手牌并选择目标角色距离为1的一名其他角色，对其造成1点伤害。",
}
