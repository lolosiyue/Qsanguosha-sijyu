# 效果外文本系统 (Oracle Text System)

## 概述

效果外文本系统是为武将/技能添加额外规则说明的功能，类似游戏王中的 Oracle Text。这些说明不算在技能描述中，而是独立的补充信息，可以包含 `<a>` 链接来引用游戏概念。

## 功能特点

- 支持**武将级别**和**技能级别**两种效果外文本
- 自动解析 `<a href="#concept">` 格式的链接
- 悬停链接时显示概念翻译
- 在 tooltip 底部自动汇总所有相关概念

## 使用方式

### 1. 效果外文本翻译键

#### 技能级别
使用 `~技能名` 作为翻译键：

```lua
sgs.LoadTranslationTable{
    ["~wushen"] = "出牌阶段，你可以额外使用一张【杀】。",
}
```

#### 武将级别
使用 `oracle:武将名` 作为翻译键：

```lua
sgs.LoadTranslationTable{
    ["oracle:shencaocao"] = "魏武帝曹操，乱世之枭雄。",
}
```

### 2. 概念链接

在效果外文本或技能描述中使用 `<a href="#概念键">文本</a>` 格式：

```lua
sgs.LoadTranslationTable{
    ["~wushen"] = "出牌阶段，你可以额外使用一张【杀】。<a href=\"#play_phase\">出牌阶段</a>",
    ["oracle:shencaocao"] = "<a href=\"#turn_end\">回合结束</a>时，你可以...",
}
```

### 3. 概念翻译

在 Lua 扩展文件中定义概念翻译：

```lua
sgs.LoadTranslationTable{
    -- 游戏阶段概念
    ["#turn_start"] = "回合开始阶段",
    ["#turn_end"] = "回合结束阶段",
    ["#play_phase"] = "出牌阶段",
    ["#judge_phase"] = "判定阶段",
    ["#draw_phase"] = "摸牌阶段",
    ["#discard_phase"] = "弃牌阶段",

    -- 游戏规则概念
    ["#damage"] = "造成伤害",
    ["#recover"] = "回复体力",
    ["#discard"] = "弃牌",
    ["#draw"] = "摸牌",
    ["#peach"] = "使用【桃】",
    ["#slash"] = "使用【杀】",
    ["#duel"] = "使用【决斗】",
}
```

## 显示结构

效果外文本系统修改后的 tooltip 结构：

```
┌─────────────────────────────────────┐
│ 【效果外文本 - Oracle Text】         │  ← 仅当存在时显示
│ 出牌阶段，你可以额外使用一张【杀】。    │
│ <a href="#play_phase">出牌阶段</a>   │  ← 链接，悬停显示"出牌阶段"
├─────────────────────────────────────┤
│ 【技能名称：武神】                    │
│ 你的杀没有距离限制。                  │  ← 技能描述
├─────────────────────────────────────┤
│ 相关概念：                          │  ← 自动汇总所有 <a> 翻译
│ · 出牌阶段                          │
└─────────────────────────────────────┘
```

## 实现细节

### 核心函数

- `Skill::getOracleText(const Player *target)` - 获取技能级别效果外文本
- `General::getOracleText()` - 获取武将级别效果外文本
- `buildOracleTooltip(oracleText, skillDescription)` - 组装完整 tooltip

### 修改的文件

| 文件 | 说明 |
|------|------|
| `src/core/oracle_helper.h/cpp` | 核心辅助函数 |
| `src/core/skill.h/cpp` | 添加 `getOracleText()` |
| `src/core/general.h/cpp` | 添加 `getOracleText()` |
| `src/ui/carditem.cpp` | 卡牌/武将头像 tooltip |
| `src/ui/roomscene.cpp` | 技能按钮 tooltip |
| `src/ui/generic-cardcontainer-ui.cpp` | 玩家容器 tooltip |
| `src/dialog/choosegeneraldialog.cpp` | 选择武将对话框 tooltip |
| `src/dialog/customassigndialog.cpp` | 自定义分配对话框 tooltip |

## 规则说明

1. **链接格式**：必须使用 `<a href="#xxx">text</a>` 格式
2. **翻译键名**：概念翻译键必须以 `#` 开头
3. **翻译查询**：系统自动使用 `Sanguosha->translate()` 查询翻译
4. **去重**：相同的概念翻译只显示一次
5. **悬停行为**：鼠标悬停在链接上时，QTextEdit 会自动显示链接的 tooltip

## 示例

### 完整示例

```lua
-- 在 extensions/extra.lua 中添加

sgs.LoadTranslationTable{
    -- 效果外文本
    ["~wushen"] = "你可以额外使用一张【杀】。<a href=\"#play_phase\">出牌阶段</a>",
    ["~tianyi"] = "当你使用第一张杀时可以指定者+1。<a href=\"#play_phase\">出牌阶段</a>开始时...",
    ["oracle:shenlvbu1"] = "神吕布，暴虐无道。<a href=\"#play_phase\">出牌阶段</a>开始时...",

    -- 概念翻译
    ["#play_phase"] = "出牌阶段",
    ["#turn_end"] = "回合结束阶段",
    ["#damage"] = "造成伤害",
    ["#slash"] = "使用【杀】",
    ["#peach"] = "使用【桃】",
}
```

### 技能描述中的链接

也可以在普通技能描述中使用概念链接：

```lua
sgs.LoadTranslationTable{
    [":wushen"] = "锁定技，你的<font color=\"#FF0000\">杀</font>无距离限制。<a href=\"#play_phase\">出牌阶段</a>开始时...",
}
```

系统会自动从技能描述中提取 `<a>` 链接并显示在 tooltip 底部。
