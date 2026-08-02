#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Warrior Evaluation System Builder for QSanguosha-v2.

Extracts warrior definitions, skills, and assets from the project,
then generates a self-contained HTML evaluation system.

Usage:
  python warrior_eval_builder.py -o warrior_eval_system.html
  python warrior_eval_builder.py -o warrior_eval_system.html --limit 100
"""

import os, re, json, argparse, sys
from pathlib import Path
from collections import defaultdict, OrderedDict

if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

PROJECT_ROOT = Path(os.path.dirname(os.path.abspath(__file__))).parent
PACKAGE_DIR  = PROJECT_ROOT / "lang" / "zh_CN" / "Package"
EXT_DIR      = PROJECT_ROOT / "extensions"
CONFIG_LUA   = PROJECT_ROOT / "lua" / "config.lua"

IMG_EXTS = {".jpg", ".jpeg", ".png", ".webp", ".bmp"}

# File:// URL prefix for absolute image paths
FILE_PREFIX = "file:///" + str(PROJECT_ROOT).replace("\\", "/").rstrip("/") + "/"

# ── Lua Parsing ────────────────────────────────────────────────────

def strip_comments(text):
    text = re.sub(r"--\[\[.*?\]\]--", "", text, flags=re.DOTALL)
    text = re.sub(r"--.*$", "", text, flags=re.MULTILINE)
    return text


def parse_lua_dict(content):
    result = OrderedDict()
    content = strip_comments(content)
    pat = re.compile(
        r"""\["([^"]+)"\]\s*=\s*(?:\[\[(.*?)\]\]|"((?:[^"\\]|\.)*)"|\'([^\']*)\'|([^,\n}]+))""",
        re.DOTALL,
    )
    for m in pat.finditer(content):
        key = m.group(1)
        val = next((v for v in m.groups()[1:] if v is not None), "").strip()
        val = val.replace('\\"', '"').replace("\n", "\n")
        result[key] = val
    return result


def parse_package_names(config_path):
    if not config_path.exists():
        return set()
    text = strip_comments(config_path.read_text(encoding="utf-8"))
    m = re.search(r"package_names\s*=\s*\{(.*?)\}", text, re.DOTALL)
    if not m:
        return set()
    return {s.strip("\"' ") for s in re.findall(r'"([^"]+)"', m.group(1))}


def parse_kingdom_colors(config_path):
    if not config_path.exists():
        return {}
    text = strip_comments(config_path.read_text(encoding="utf-8"))
    m = re.search(r"kingdom_colors\s*=\s*\{(.*?)\}", text, re.DOTALL)
    if not m:
        return {}
    colors = {}
    for k, v in re.findall(r'(\w+)\s*=\s*"([^"]+)"', m.group(1)):
        colors[k] = v
    return colors


def parse_package_order(config_path):
    """Extract package_names list order from config.lua for default sorting."""
    if not config_path.exists():
        return {}
    text = strip_comments(config_path.read_text(encoding="utf-8"))
    m = re.search(r"package_names\s*=\s*\{(.*?)\}", text, re.DOTALL)
    if not m:
        return {}
    names = re.findall(r'"([^"]+)"', m.group(1))
    return {name: i for i, name in enumerate(names)}


# ── Faction Name Translations ──────────────────────────────────────

FACTION_NAMES = {
    "wei": "魏", "shu": "蜀", "wu": "吳", "qun": "群", "jin": "晉", "god": "神",
    "magic": "魔法", "science": "科學", "kancolle": "艦娘", "touhou": "東方",
    "ark": "方舟", "demon": "魔族", "diva": "歌姬", "qh": "秦漢",
    "real": "史實", "sevendevil": "七魔", "sy_god": "神話", "sgk_magic": "魔法",
    "CB": "CB", "EFSF": "聯邦", "ZEON": "吉翁", "ZAFT": "ZAFT",
    "ORB": "奧布", "TEKKADAN": "鐵華團", "SLEEVE": "袖章", "OTHERS": "其他",
    "Erciyuan": "二次元", "htms_feng": "風", "htms_huo": "火",
    "htms_lin": "林", "htms_shan": "山", "htms_wu": "無",
    "kegui": "可貴", "kesheng": "可勝", "kexian": "可賢", "keyao": "可耀",
}

# ── Package Display Names ──────────────────────────────────────────

PACKAGE_DISPLAY = {
    # Official / Core packages
    "StandardGeneralPackage": "標準版",
    "WindPackage": "風包",
    "FirePackage": "火包",
    "MountainPackage": "山包",
    "GodPackage": "神將",
    "YinPackage": "陰包",
    "LeiPackage": "雷包",
    "JiePackage": "界包",
    "LingPackage": "靈包",
    "LiPackage": "戾包",
    "BeiPackage": "備包",
    "GuoPackage": "國包",
    "YuePackage": "約包",
    "YitianPackage": "倚天包",
    "WisdomPackage": "智包",
    "ThicketPackage": "叢包",
    "HMomentumPackage": "勢包",
    "HFormationPackage": "陣包",
    "MaotuPackage": "謀圖包",
    "AssassinsPackage": "刺包",
    # SP / OL / Mobile / Nostalgia
    "SPPackage": "SP",
    "Mobile": "手殺",
    "OLPackage": "OL",
    "OLStrengthenPackage": "OL界限突破",
    "OLJXTPPackage": "OL界限突破",
    "MobileJXTPPackage": "手殺界限突破",
    "MobileStrengthenPackage": "手殺界限突破",
    "NostalgiaPackage": "懷舊",
    "BGMPackage": "BGM",
    "TenyearStrengthenPackage": "十年界限突破",
    "JXTPPackage": "界限突破",
    "GeneralConversion": "轉換技將",
    "Special1v1Package": "特殊1v1",
    "Special3v3Package": "特殊3v3",
    "Happy2v2Package": "歡樂2v2",
    "DoudizhuPackage": "鬥地主",
    "HegemonyPackage": "國戰",
    # Yi Jiang Cheng Ming (一將成名) series
    "YJCMPackage": "一將成名",
    "YJCM2012Package": "一將成名2012",
    "YJCM2013Package": "一將成名2013",
    "YJCM2014Package": "一將成名2014",
    "YJCM2015Package": "一將成名2015",
    "YJCM2022Package": "一將成名2022",
    "YJCM2023Package": "一將成名2023",
    "YCZH2016Package": "原創之魂2016",
    "YCZH2017Package": "原創之魂2017",
    # Community / Crossover
    "dongmanbao": "動漫包",
    "touhouproject": "東方Project",
    "erciyuan": "二次元",
    "arknights": "明日方舟",
    "typemoon": "型月",
    "gundamboss": "高達Boss",
    "dmpkancolle": "艦隊Collection",
    "dmpdiva": "歌姬",
    "dmptouhou": "東方(動漫)",
    "kancolle": "艦娘",
    # Community series
    "htms": "HTMS",
    "meizl": "梅子系列",
    "scarlet": "時語",
    "sgs10th": "三國殺十週年",
    "gaoda": "高達",
    "biaofeng": "標風",
    "biaofengold": "標風(舊)",
    "sk": "SK",
    "sy": "SY",
    "hunlie": "魂烈",
    "hunliesp": "魂烈SP",
    "fcDIY": "FC DIY",
    "srkill": "SR技能",
    "mojiang": "魔將",
    "mojiangprevious": "魔將(前)",
    "olddiy": "舊DIY",
    "newgenerals": "新武將",
    "newstar": "新星",
    "new": "NEW",
    "shadow": "影",
    "rushB": "Rush B",
    "mcompetition": "M競賽",
    "kearnews": "KEAR新",
    "kearcane": "KEAR魔",
    "keguibao": "可貴包",
    "keshengbao": "可勝包",
    "kexianbao": "可賢包",
    "keyaobao": "可耀包",
    "kearwangushengxiang": "KEAR萬古聖像",
    "YinhuPackage": "銀狐",
    "nybeauty": "NY美",
    "mountainrumour": "山謠",
    "sihaigaizao": "四海改造",
    "feiDIY": "飛DIY",
    "blood": "血",
    "berserk": "狂暴",
    "goddess": "女神",
    "moshen": "魔神",
    "animic": "動畫",
    "meow": "喵",
    "ck": "CK",
    "du": "DU",
    "lol2015": "LOL 2015",
    "NyarzFirst": "Nyarz一期",
    "NyarzSecond": "Nyarz二期",
    "NyarzThird": "Nyarz三期",
    "OverseasVersion": "海外版",
    "olClan": "OL氏族",
    "Loong": "龍",
    "LuaOldEnemyGirls": "Lua舊敵(女)",
    "DragonLoke": "龍洛",
    "AIgeneral": "AI武將",
    "Christmas": "聖誕",
    "Dan": "丹",
    "extendEffects": "擴展特效",
    "extra": "額外",
    "nMobileEffect": "手殺特效",
    "offline": "離線",
    "qhstandard": "秦漢標準",
    "RAFTOM": "RAFTOM",
    "yy": "YY",
    "Zombine": "殭屍",
    "zhenghuoCMT": "正火CMT",
    "kurosakiichigo": "黑崎一護",
    "wolf1411": "狼1411",
}

# ── Standard General Skills (for C++-defined warriors) ────────────

STANDARD_SKILLS = {
    # Wei (魏) — Standard
    "caocao": ["jianxiong", "hujia"],
    "simayi": ["fankui", "guicai"],
    "xiahoudun": ["ganglie"],
    "zhangliao": ["tuxi"],
    "xuchu": ["luoyi"],
    "guojia": ["tiandu", "yiji"],
    "zhenji": ["luoshen", "qingguo"],
    "lidian": ["wangxi", "xunxun"],
    "caoren": ["jushou"],
    "xunyu": ["jiyu"],
    "dianwei": ["yaowu"],
    "xuhuang": ["fenwei"],
    "zhanghe": ["qiaomeng"],
    "caopi": ["jiefang"],
    "dengai": ["yijue"],
    "zhonghui": ["jili"],
    "wangyi": ["wangzun"],
    "manchong": ["tongji"],
    "caochong": ["tishen"],
    "yujin": ["jieyue"],
    "caozhi": ["guicai_plus"],
    "guohuai": ["liyu"],
    "haozhao": ["guzheng"],
    "caohong": ["qinxue", "qinxueWake"],
    "caozhang": ["zhuhai"],
    # Shu (蜀) — Standard
    "liubei": ["rende", "jijiang"],
    "guanyu": ["wusheng"],
    "zhangfei": ["paoxiao"],
    "zhaoyun": ["longdan"],
    "machao": ["mashu", "tieji"],
    "huangzhong": ["lianzhu"],
    "zhugeliang": ["guanxing", "kongcheng"],
    "huangyueying": ["jizhi", "qicai"],
    "jiangwei": ["wunao", "tianfu"],
    "weiyan": ["kuanggu"],
    "liushan": ["jiuyuan"],
    "fazheng": ["xuanhuo"],
    "masu": ["xinman"],
    "xushu": ["jijian"],
    "pangtong": ["pangtong_lianhuan"],
    "menghuo": ["zaiqi"],
    "zhurong": ["juxiang"],
    "liufeng": ["jisi"],
    # Wu (吳) — Standard
    "sunquan": ["zhiheng"],
    "zhouyu": ["yingzi", "fanjian"],
    "ganning": ["qixi"],
    "lvmeng": ["keji"],
    "huanggai": ["kurou"],
    "luxun": ["qianxun", "lianying"],
    "daqiao": ["guose", "liuli"],
    "xiaoqiao": ["tianxiang"],
    "sunshangxiang": ["xiaoji", "jieyin"],
    "taishici": ["tianyi"],
    "zhoutai": ["buqu"],
    "lingtong": ["xuanlve"],
    "dingfeng": ["duanbing"],
    "sunce": ["bazhan"],
    "sunjian": ["yinghun"],
    "sunliang": ["qianxin", "qianxinWake"],
    # Qun (群) — Standard
    "huatuo": ["jijiu", "qingjian"],
    "lvbu": ["wushuang"],
    "diaochan": ["lijian", "biyue"],
    "yuanshao": ["luanji"],
    "yuanshu": ["gongwei"],
    "dongzhuo": ["jiuchi"],
    "zhangjiao": ["leiji"],
    "pangde": ["juesha"],
    "jiaxu": ["wansha"],
    "mateng": ["mashu"],
    "kongrong": ["mingshi"],
    "jiling": ["zhaxiang"],
    "gongsunzan": ["yicong"],
    "liubiao": ["zishou"],
    # Jin (晉)
    "simashi": ["quanli"],
    "simazhao": ["quanmou"],
    "wangyuanji": ["xianshi"],
    "duyu": ["wuku"],
}


# ── Standard General Fallbacks (kingdom, hp for C++-defined warriors) ─

_STANDARD_GENERALS = {
    "caocao": ("wei", 4), "xiahoudun": ("wei", 4), "xiahouyuan": ("wei", 4),
    "zhanghe": ("wei", 4), "zhangliao": ("wei", 4), "xuchu": ("wei", 4),
    "dianwei": ("wei", 4), "xunyu": ("wei", 3), "guojia": ("wei", 3),
    "simayi": ("wei", 3), "zhenji": ("wei", 3), "caopi": ("wei", 3),
    "caoren": ("wei", 4), "lietian": ("wei", 4), "dengai": ("wei", 4),
    "zhonghui": ("wei", 3), "wangyi": ("wei", 3), "manchong": ("wei", 3),
    "caochong": ("wei", 3), "xunchen": ("wei", 3), "xuhuang": ("wei", 4),
    "yujin": ("wei", 4), "yuejin": ("wei", 4), "lidian": ("wei", 3),
    "caozhi": ("wei", 3), "guohuai": ("wei", 4), "haozhao": ("wei", 3),
    "caohong": ("wei", 4), "caozhang": ("wei", 4), "chenyu": ("wei", 3),
    "liubei": ("shu", 4), "guanyu": ("shu", 4), "zhangfei": ("shu", 4),
    "zhaoyun": ("shu", 4), "machao": ("shu", 4), "huangzhong": ("shu", 4),
    "zhugeliang": ("shu", 3), "weiyan": ("shu", 4), "huangyueying": ("shu", 3),
    "jiangwei": ("shu", 4), "liushan": ("shu", 3), "fazheng": ("shu", 3),
    "masu": ("shu", 3), "xushu": ("shu", 3), "pangtong": ("shu", 3),
    "guanxingzhangbao": ("shu", 4), "shamoke": ("shu", 4), "wangping": ("shu", 4),
    "jiexuzi": ("shu", 3), "menghuo": ("shu", 4), "zhurong": ("shu", 4),
    "liufeng": ("shu", 4), "jianyong": ("shu", 3), "sunqian": ("shu", 3),
    "yiji": ("shu", 3), "qiaozhou": ("shu", 3), "zhangbao": ("shu", 4),
    "guansuo": ("shu", 4), "zhangxingcai": ("shu", 3),
    "sunquan": ("wu", 4), "zhouyu": ("wu", 3), "ganning": ("wu", 4),
    "lvmeng": ("wu", 4), "luxun": ("wu", 3), "huanggai": ("wu", 4),
    "daqiao": ("wu", 3), "xiaoqiao": ("wu", 3), "sunshangxiang": ("wu", 3),
    "taishici": ("wu", 4), "zhoutai": ("wu", 4), "lingtong": ("wu", 4),
    "dingfeng": ("wu", 4), "sunce": ("wu", 4), "sunjian": ("wu", 4),
    "zhangzhao": ("wu", 3), "guzhigang": ("wu", 4), "luyusheng": ("wu", 4),
    "xushi": ("wu", 3), "bulianshi": ("wu", 3),
    "panzhangmazhong": ("wu", 4), "jiangqin": ("wu", 4),
    "sunliang": ("wu", 3), "sunxiu": ("wu", 3), "sunhao": ("wu", 4),
    "cenhun": ("wu", 3), "zhugejin": ("wu", 3), "kanze": ("wu", 3),
    "zhuran": ("wu", 4), "quancong": ("wu", 4), "lukang": ("wu", 3),
    "huatuo": ("qun", 3), "lvbu": ("qun", 4), "diaochan": ("qun", 3),
    "yuanshao": ("qun", 4), "yuanshu": ("qun", 4), "dongzhuo": ("qun", 4),
    "zhangjiao": ("qun", 3), "pangde": ("qun", 4), "jiaxu": ("qun", 3),
    "yanliangwenchou": ("qun", 4), "tianfeng": ("qun", 3), "jikangren": ("qun", 3),
    "mateng": ("qun", 4), "kongrong": ("qun", 3), "jiling": ("qun", 4),
    "zoushi": ("qun", 3), "gongsunzan": ("qun", 4), "liubiao": ("qun", 3),
    "caijifu": ("qun", 3), "huangjinleishi": ("qun", 4),
    "simashi": ("jin", 3), "simazhao": ("jin", 3),
    "wangyuanji": ("jin", 3), "duyu": ("jin", 4),
    "shenguanyu": ("god", 5), "shenlumeng": ("god", 4), "shenzhouyu": ("god", 4),
    "shenzhugeliang": ("god", 4), "shencaocao": ("god", 4), "shenlvbu": ("god", 5),
    "shenzhaoyun": ("god", 4), "shensimayi": ("god", 4), "shenganning": ("god", 4),
    "shenguojia": ("god", 3), "shenluxun": ("god", 4), "shenliubei": ("god", 4),
    "shenzhangfei": ("god", 4), "shenhuangzhong": ("god", 4),
    "shenmachao": ("god", 4), "shendengai": ("god", 4),
    "shenxunyu": ("god", 3), "shenjiangwei": ("god", 4),
    "dou_caocao": ("wei", 4), "dou_xiahoudun": ("wei", 4),
    "dou_liubei": ("shu", 4), "dou_guanyu": ("shu", 4), "dou_zhangfei": ("shu", 4),
    "dou_sunquan": ("wu", 4), "dou_lvbu": ("qun", 4),
}


def _apply_standard_fallbacks(warriors):
    for wid, (kingdom, hp) in _STANDARD_GENERALS.items():
        if wid in warriors:
            w = warriors[wid]
            if not w.get("kingdom"):
                w["kingdom"] = kingdom
            if not w.get("hp"):
                w["hp"] = hp


def _apply_standard_skills(warriors):
    """Fill in skills for standard C++-defined warriors that lack Lua skill associations."""
    for wid, skills in STANDARD_SKILLS.items():
        if wid in warriors:
            w = warriors[wid]
            if not w.get("skills"):
                w["skills"] = list(skills)


# ── Warrior Extraction ─────────────────────────────────────────────

def extract_warriors():
    """Extract all warrior definitions, preserving package file order."""
    warriors = OrderedDict()
    known_pkgs = parse_package_names(CONFIG_LUA)
    pkg_order = parse_package_order(CONFIG_LUA)

    # Global sort index for preserving game order
    sort_idx = [0]

    for lua_file in sorted(PACKAGE_DIR.glob("*.lua")):
        try:
            raw_text = lua_file.read_text(encoding="utf-8")
            tbl = parse_lua_dict(raw_text)
        except Exception:
            continue
        pkg = lua_file.stem
        file_skills = {k[1:] for k in tbl if k.startswith(":") and not k.startswith("::")}

        # Collect skill translations from this file
        skill_names = {}
        for key, val in tbl.items():
            if key.startswith(":") and not key.startswith("::"):
                continue
            if key in file_skills or (not key.startswith("#") and not key.startswith(":") and key not in known_pkgs):
                # This could be a skill name (non-prefixed key, not a known package, not starting with special chars)
                pass

        candidate_ids = set()
        for key in tbl:
            if key.startswith("#") and not key.startswith("#$") and not key.startswith("#@"):
                wid = key[1:]
                if not re.match(r"^[A-Z]", wid) and not wid.startswith("@"):
                    candidate_ids.add(wid)
            for prefix in ("illustrator:", "designer:", "cv:", "&"):
                if key.startswith(prefix):
                    candidate_ids.add(key[len(prefix):])

        candidate_ids -= known_pkgs

        for wid in candidate_ids:
            if wid in file_skills or len(wid) <= 1:
                continue
            if wid not in warriors:
                warriors[wid] = dict(
                    id=wid, name=None, title=None, short_name=None,
                    illustrator=None, designer=None, cv=None,
                    files=[], skills=[], kingdom=None, hp=None,
                    sort_idx=sort_idx[0],
                )
                sort_idx[0] += 1
            w = warriors[wid]
            if pkg not in w["files"]:
                w["files"].append(pkg)
            if wid in tbl and tbl[wid]:
                w["name"] = tbl[wid]
            k = "#" + wid
            if k in tbl and tbl[k]:
                w["title"] = tbl[k]
            k = "&" + wid
            if k in tbl and tbl[k]:
                w["short_name"] = tbl[k]
            k = "illustrator:" + wid
            if k in tbl and tbl[k]:
                w["illustrator"] = tbl[k]
            k = "designer:" + wid
            if k in tbl and tbl[k]:
                w["designer"] = tbl[k]
            k = "cv:" + wid
            if k in tbl and tbl[k]:
                w["cv"] = tbl[k]

    # Supplement from extension Lua files (kingdom, hp, skills, translations)
    # Match: var = sgs.General(pkg_var, "id", "kingdom", hp, ...)
    ext_pat = re.compile(
        r"""(\w+)\s*=\s*sgs\.General\s*\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*"(\w*)"\s*,\s*(\d+)"""
    )

    for ext_file in sorted(EXT_DIR.glob("*.lua")):
        try:
            # IMPORTANT: Do NOT strip block comments for extension files.
            # Many extension files store translations inside --[[...]]-- blocks
            # that the game engine parses via custom loaders. Only strip line comments.
            raw_text = ext_file.read_text(encoding="utf-8", errors="ignore")
            text = re.sub(r"--(?!\[\[).*$", "", raw_text, flags=re.MULTILINE)
        except Exception:
            continue

        pkg_name = ext_file.stem

        # ── Parse all LoadTranslationTable blocks for warrior names ──
        trans_tables = {}
        for tt_match in re.finditer(
            r"""sgs\.LoadTranslationTable\s*\{((?:[^{}]|\{(?:[^{}]|\{[^{}]*\})*\})*)\}""",
            text, re.DOTALL
        ):
            tt_text = tt_match.group(1)
            # Parse key = value pairs
            for kv in re.finditer(
                r"""\["([^"]+)"\]\s*=\s*"((?:[^"\\]|\\.)*)"\s*,?""",
                tt_text
            ):
                trans_tables[kv.group(1)] = kv.group(2).replace('\\"', '"')

        # ── Build var -> skill name map ──
        var_to_skill = {}
        skill_def_pat = re.compile(
            r"""(\w+)\s*=\s*sgs\.(?:Create|Lua)\w*Skill\s*\{[^}]*?name\s*=\s*"([^"]+)"[^}]*\}""",
            re.DOTALL,
        )
        for sm in skill_def_pat.finditer(text):
            var_to_skill[sm.group(1)] = sm.group(2)

        # ── Extract sgs.General calls ──
        for m in ext_pat.finditer(text):
            var_name = m.group(1)
            wid = m.group(3)
            kingdom = m.group(4) or ""
            hp_val = int(m.group(5)) if m.group(5) else 0

            if wid not in warriors:
                # Use translation table for name if available.
                # Try exact ID first, then without $ suffix (lord variants)
                lookup = wid.rstrip("$")
                display_name = trans_tables.get(wid) or trans_tables.get(lookup) or wid
                title = trans_tables.get("#" + wid) or trans_tables.get("#" + lookup)
                illustrator = trans_tables.get("illustrator:" + wid) or trans_tables.get("illustrator:" + lookup)
                designer = trans_tables.get("designer:" + wid) or trans_tables.get("designer:" + lookup)

                warriors[wid] = dict(
                    id=wid,
                    name=display_name,
                    title=title,
                    short_name=None,
                    illustrator=illustrator,
                    designer=designer,
                    cv=None,
                    files=[],
                    skills=[],
                    kingdom=kingdom if kingdom else None,
                    hp=hp_val if hp_val > 0 else None,
                    sort_idx=sort_idx[0],
                )
                sort_idx[0] += 1

            w = warriors[wid]

            # Fill in missing data from translation table
            lookup = wid.rstrip("$")
            if w.get("name") == wid or not w.get("name"):
                name = trans_tables.get(wid) or trans_tables.get(lookup)
                if name:
                    w["name"] = name
            if not w.get("title"):
                title = trans_tables.get("#" + wid) or trans_tables.get("#" + lookup)
                if title:
                    w["title"] = title
            if not w.get("illustrator"):
                ill = trans_tables.get("illustrator:" + wid) or trans_tables.get("illustrator:" + lookup)
                if ill:
                    w["illustrator"] = ill
            if not w.get("designer"):
                des = trans_tables.get("designer:" + wid) or trans_tables.get("designer:" + lookup)
                if des:
                    w["designer"] = des

            # Fill kingdom/hp if missing
            if not w.get("kingdom") and kingdom:
                w["kingdom"] = kingdom
            if not w.get("hp") and hp_val > 0:
                w["hp"] = hp_val

            if pkg_name not in w["files"]:
                w["files"].append(pkg_name)

            # ── Find addSkill calls for this warrior ──
            addskill_pat = re.compile(
                re.escape(var_name) + r""":addSkill\s*\(\s*(?:"([^"]+)"|(\w+))\s*\)"""
            )
            for sm in addskill_pat.finditer(text):
                sk = sm.group(1) if sm.group(1) else sm.group(2)
                if sk in var_to_skill:
                    sk = var_to_skill[sk]
                if sk and sk not in w["skills"]:
                    w["skills"].append(sk)

    # Apply standard fallbacks
    _apply_standard_fallbacks(warriors)
    _apply_standard_skills(warriors)

    return warriors


def extract_skill_translations():
    """Extract skill name and description translations from Lua files."""
    skills = {}
    for lua_file in sorted(PACKAGE_DIR.glob("*.lua")):
        try:
            tbl = parse_lua_dict(lua_file.read_text(encoding="utf-8"))
        except Exception:
            continue

        # Skills are keys that have a ":" prefix for descriptions
        desc_keys = {k[1:] for k in tbl if k.startswith(":") and not k.startswith("::")}

        for sk in desc_keys:
            if sk not in skills:
                name = tbl.get(sk)
                desc = tbl.get(":" + sk)
                # Only include if it looks like a skill (has a description and a name)
                if name and desc and len(sk) > 1 and not sk.startswith("$"):
                    skills[sk] = {"id": sk, "name": name, "desc": desc}

    return skills


# ── Image Path Resolution ──────────────────────────────────────────

def resolve_image_paths(warriors):
    """Check and resolve image paths using file:/// absolute URLs."""
    card_dir = PROJECT_ROOT / "image" / "generals" / "card"
    full_dir = PROJECT_ROOT / "image" / "fullskin" / "generals" / "full"

    # Pre-scan
    card_files = {}
    if card_dir.is_dir():
        for fp in card_dir.iterdir():
            if fp.is_file() and fp.suffix.lower() in IMG_EXTS:
                card_files[fp.stem.lower()] = fp.name

    full_files = {}
    if full_dir.is_dir():
        for fp in full_dir.iterdir():
            if fp.is_file() and fp.suffix.lower() in IMG_EXTS:
                full_files[fp.stem.lower()] = fp.name

    def find_img(wid, file_map, dir_name):
        clean = wid.rstrip("$").lower()
        for stem in (clean, wid.lower()):
            if stem in file_map:
                return FILE_PREFIX + dir_name + "/" + file_map[stem]
            # Try _skin1 variant
            skin_key = stem + "_skin1"
            if skin_key in file_map:
                return FILE_PREFIX + dir_name + "/" + file_map[skin_key]
        return ""

    result = []
    for wid, w in warriors.items():
        card_img = find_img(wid, card_files, "image/generals/card")
        full_img = find_img(wid, full_files, "image/fullskin/generals/full")
        if not full_img:
            full_img = card_img  # fallback

        # Get primary package display name
        primary_pkg = w["files"][0] if w["files"] else ""
        pkg_display = PACKAGE_DISPLAY.get(primary_pkg, primary_pkg)

        entry = {
            "id": wid,
            "name": w.get("name") or wid,
            "title": w.get("title") or "",
            "kingdom": w.get("kingdom") or "",
            "kingdomName": FACTION_NAMES.get(w.get("kingdom", ""), w.get("kingdom", "")),
            "hp": w.get("hp") or 0,
            "illustrator": w.get("illustrator") or "",
            "designer": w.get("designer") or "",
            "cv": w.get("cv") or "",
            "packages": w.get("files", []),
            "primaryPackage": primary_pkg,
            "packageDisplay": pkg_display,
            "cardImg": card_img,
            "fullImg": full_img,
            "skills": w.get("skills", []),
            "sortIdx": w.get("sort_idx", 0),
        }
        result.append(entry)

    return result


# ── HTML Template ──────────────────────────────────────────────────

HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>武將評價系統 — QSanguosha v2</title>
<style>
/* ═══════════════════════════════════════════════════════════════════
   DESIGN SYSTEM: Blue Archive × Princess Connect × Star Tower
   蔚藍檔案(清爽科技) +公主連結(明亮幻想) +星塔旅人(角色展示)
   關鍵詞: 角色主導、天藍色、柔和光暈、六邊形、浮動玻璃、大量留白
   ═══════════════════════════════════════════════════════════════════ */

:root {
  --bg-page: #eef3f9;
  --bg-card: rgba(255, 255, 255, 0.72);
  --bg-glass: rgba(255, 255, 255, 0.48);
  --bg-glass-hover: rgba(255, 255, 255, 0.88);
  --bg-sidebar: rgba(242, 247, 252, 0.82);
  --bg-input: rgba(255, 255, 255, 0.58);
  --text-primary: #1a2233;
  --text-secondary: #5a6d88;
  --text-dim: #90a0bb;
  --accent-sky: #4db8e8;
  --accent-sky-light: #e6f5fc;
  --accent-sky-glow: rgba(77, 184, 232, 0.18);
  --accent-pink: #f0a0b8;
  --accent-pink-light: #fef0f4;
  --accent-gold: #e8c560;
  --accent-gold-light: #fef9ed;
  --accent-deep: #2d5a7a;
  --border-subtle: rgba(160, 180, 200, 0.25);
  --border-active: rgba(77, 184, 232, 0.45);
  --border-glow: rgba(77, 184, 232, 0.3);
  --shadow-sm: 0 1px 4px rgba(0,0,0,0.03), 0 0 8px rgba(77,184,232,0.05);
  --shadow-md: 0 4px 20px rgba(0,0,0,0.05), 0 0 16px rgba(77,184,232,0.06);
  --shadow-glow: 0 0 28px rgba(77, 184, 232, 0.14);
  --shadow-pink: 0 0 20px rgba(240, 160, 184, 0.12);
  --radius-sm: 8px;
  --radius-md: 14px;
  --radius-lg: 22px;
  --radius-xl: 30px;
  --font-main: "PingFang SC","Microsoft YaHei","Hiragino Sans GB","Noto Sans CJK SC",sans-serif;
  --transition-fast: 180ms ease-out;
  --transition-normal: 300ms cubic-bezier(0.25, 0.8, 0.25, 1.2);
  --glass-blur: saturate(180%) blur(20px);
  --sidebar-width: 300px;
}

*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

html { font-size: 14px; scroll-behavior: smooth; }

body {
  font-family: var(--font-main);
  background: var(--bg-page);
  background-image:
    radial-gradient(ellipse at 15% 20%, rgba(77,184,232,0.08) 0%, transparent 55%),
    radial-gradient(ellipse at 85% 60%, rgba(240,160,184,0.06) 0%, transparent 50%),
    radial-gradient(ellipse at 50% 90%, rgba(232,197,96,0.04) 0%, transparent 45%);
  color: var(--text-primary);
  overflow: hidden; height: 100vh; width: 100vw;
  -webkit-font-smoothing: antialiased;
}

/* ── Hexagonal & Cross Decorations ───────────────────────────────── */

#decorations { position: fixed; inset: 0; z-index: 0; pointer-events: none; overflow: hidden; }
.deco-hex {
  position: absolute;
  width: 80px; height: 92px;
  background: rgba(77,184,232,0.04);
  clip-path: polygon(50% 0%, 100% 25%, 100% 75%, 50% 100%, 0% 75%, 0% 25%);
}
.deco-cross {
  position: absolute;
  width: 24px; height: 24px;
  opacity: 0.12;
}
.deco-cross::before, .deco-cross::after {
  content: ''; position: absolute;
  background: var(--accent-sky);
}
.deco-cross::before { width: 100%; height: 2px; top: 50%; transform: translateY(-50%); }
.deco-cross::after { height: 100%; width: 2px; left: 50%; transform: translateX(-50%); }
.deco-circle {
  position: absolute; border-radius: 50%;
  border: 1.5px solid rgba(77,184,232,0.08);
  background: transparent;
}
.deco-dot {
  position: absolute; border-radius: 50%;
  background: var(--accent-sky);
  opacity: 0.18;
  animation: decoFloat 6s ease-in-out infinite;
}
@keyframes decoFloat {
  0%, 100% { transform: translateY(0); opacity: 0.12; }
  50% { transform: translateY(-12px); opacity: 0.25; }
}

/* ── Scrollbar ───────────────────────────────────────────────────── */

::-webkit-scrollbar { width: 4px; }
::-webkit-scrollbar-track { background: transparent; }
::-webkit-scrollbar-thumb { background: rgba(0,0,0,0.08); border-radius: 10px; }
::-webkit-scrollbar-thumb:hover { background: rgba(0,0,0,0.16); }

/* ── App Shell ───────────────────────────────────────────────────── */

#app {
  position: relative; z-index: 1;
  display: grid;
  grid-template-rows: 52px 1fr;
  grid-template-columns: var(--sidebar-width) 1fr;
  grid-template-areas: "nav nav" "sidebar main";
  height: 100vh; width: 100vw;
}

/* ── Top Nav ─────────────────────────────────────────────────────── */

#top-nav {
  grid-area: nav; z-index: 100;
  display: flex; align-items: center; gap: 10px;
  padding: 0 20px;
  background: var(--bg-glass);
  backdrop-filter: var(--glass-blur);
  -webkit-backdrop-filter: var(--glass-blur);
  border-bottom: 1px solid var(--border-subtle);
}

#top-nav .logo {
  font-size: 1.1rem; font-weight: 700;
  background: linear-gradient(135deg, var(--accent-sky), #7dd0f5);
  -webkit-background-clip: text; -webkit-text-fill-color: transparent;
  background-clip: text;
  white-space: nowrap; margin-right: 8px; letter-spacing: 0.02em;
}

#search-box {
  flex: 1; max-width: 300px;
  display: flex; align-items: center;
  background: var(--bg-input);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-xl);
  padding: 5px 14px; gap: 8px;
  transition: all var(--transition-fast);
}
#search-box:focus-within {
  border-color: var(--accent-sky);
  box-shadow: var(--shadow-glow);
  background: rgba(255,255,255,0.88);
}
#search-box input {
  flex: 1; border: none; outline: none;
  background: transparent; color: var(--text-primary);
  font-size: 0.85rem; font-family: var(--font-main);
}
#search-box input::placeholder { color: var(--text-dim); }

.nav-btn {
  padding: 5px 14px; border-radius: var(--radius-md);
  font-size: 0.76rem; font-weight: 600; cursor: pointer;
  border: 1px solid var(--border-subtle);
  background: var(--bg-input);
  color: var(--text-secondary);
  transition: all var(--transition-fast);
  white-space: nowrap; font-family: var(--font-main);
}
.nav-btn:hover {
  border-color: var(--accent-sky);
  color: var(--accent-sky);
  background: var(--accent-sky-light);
  box-shadow: var(--shadow-glow);
}

#sort-select {
  background: var(--bg-input);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-md);
  color: var(--text-secondary);
  padding: 5px 10px; font-size: 0.76rem;
  font-family: var(--font-main); cursor: pointer; outline: none;
}
#sort-select:focus { border-color: var(--accent-sky); }

/* ── Sidebar ─────────────────────────────────────────────────────── */

#sidebar {
  grid-area: sidebar;
  display: flex; flex-direction: column;
  background: var(--bg-sidebar);
  backdrop-filter: var(--glass-blur);
  -webkit-backdrop-filter: var(--glass-blur);
  border-right: 1px solid var(--border-subtle);
  overflow: hidden;
}

#sidebar-header {
  padding: 14px 16px 10px;
  border-bottom: 1px solid var(--border-subtle);
  display: flex; justify-content: space-between; align-items: center;
  flex-shrink: 0;
  font-size: 0.8rem; font-weight: 600; color: var(--text-secondary);
}

#package-filter {
  display: flex; gap: 3px; padding: 8px 8px;
  overflow-x: auto; flex-shrink: 0; flex-wrap: wrap;
  border-bottom: 1px solid var(--border-subtle);
}
#package-filter::-webkit-scrollbar { height: 2px; }

.pkg-pill {
  padding: 3px 9px; border-radius: var(--radius-xl);
  font-size: 0.68rem; cursor: pointer; white-space: nowrap;
  border: 1px solid var(--border-subtle);
  background: var(--bg-input); color: var(--text-dim);
  transition: all var(--transition-fast);
  font-family: var(--font-main); user-select: none;
}
.pkg-pill:hover { border-color: var(--accent-sky); color: var(--accent-sky); }
.pkg-pill.active {
  border-color: var(--accent-sky); background: var(--accent-sky-light);
  color: var(--accent-sky); font-weight: 600;
}

.kingdom-filters {
  display: flex; gap: 3px; padding: 6px 8px;
  flex-wrap: wrap; flex-shrink: 0;
  border-bottom: 1px solid var(--border-subtle);
}

.kd-pill {
  width: 26px; height: 26px; border-radius: var(--radius-sm);
  font-size: 0.68rem; font-weight: 700; cursor: pointer;
  display: flex; align-items: center; justify-content: center;
  border: 2px solid transparent; background: var(--bg-input);
  color: var(--text-dim); transition: all var(--transition-fast);
  user-select: none;
}
.kd-pill:hover { transform: scale(1.12); }
.kd-pill.active {
  border-color: currentColor;
  box-shadow: 0 0 10px rgba(0,0,0,0.06);
}

#warrior-count { font-size: 0.72rem; color: var(--accent-sky); font-weight: 700; }

#warrior-list { flex: 1; overflow-y: auto; padding: 4px 6px; }

/* ── Package Group Header ────────────────────────────────────────── */

.pkg-group-header {
  padding: 8px 10px 3px;
  font-size: 0.66rem; font-weight: 700;
  color: var(--accent-sky);
  text-transform: uppercase; letter-spacing: 0.07em;
  opacity: 0.65;
}

/* ── Warrior Card ────────────────────────────────────────────────── */

.warrior-card {
  display: flex; align-items: center; gap: 9px;
  padding: 6px 9px; border-radius: var(--radius-md);
  cursor: pointer; position: relative;
  transition: all var(--transition-fast);
  margin-bottom: 2px;
  border: 1px solid transparent;
  background: transparent;
}
.warrior-card:hover {
  background: var(--bg-glass-hover);
  border-color: var(--border-subtle);
  box-shadow: var(--shadow-sm);
}
.warrior-card.selected {
  background: var(--accent-sky-light);
  border-color: rgba(77,184,232,0.35);
  box-shadow: 0 0 16px rgba(77,184,232,0.1);
}

.warrior-card .card-thumb {
  width: 40px; height: 40px; border-radius: var(--radius-sm);
  object-fit: cover; flex-shrink: 0;
  background: #e4eaf2;
  border: 1px solid var(--border-subtle);
}
.warrior-card .card-thumb.placeholder {
  display: flex; align-items: center; justify-content: center;
  color: var(--text-dim); font-size: 1rem; background: #edf1f8;
}
.warrior-card .card-info { flex: 1; min-width: 0; }
.warrior-card .card-name {
  font-size: 0.83rem; font-weight: 600; color: var(--text-primary);
  white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
}
.warrior-card .card-subtitle {
  font-size: 0.66rem; color: var(--text-dim);
  white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  display: flex; gap: 5px; align-items: center;
}
.warrior-card .card-kd {
  font-size: 0.58rem; padding: 0px 5px; border-radius: 4px;
  font-weight: 700;
}
.warrior-card .card-rating-badge {
  font-size: 0.68rem; font-weight: 700; flex-shrink: 0;
  padding: 2px 7px; border-radius: var(--radius-xl);
  background: var(--accent-sky-light);
  color: var(--accent-sky);
}

/* ── Main Content ────────────────────────────────────────────────── */

#main-content {
  grid-area: main;
  overflow-y: auto; overflow-x: hidden;
  position: relative;
  padding: 28px 48px 48px;
}

#empty-state {
  display: flex; flex-direction: column; align-items: center;
  justify-content: center; height: 100%; gap: 14px;
  color: var(--text-dim);
}
#empty-state .empty-icon { font-size: 4.5rem; opacity: 0.2; }
#empty-state .empty-text { font-size: 1rem; font-weight: 500; }

/* ── Hero Block (portrait card + info side-by-side) ─ */

#hero-block {
  display: flex; gap: 0; margin-bottom: 28px;
  border-radius: var(--radius-lg);
  overflow: hidden;
  background: #e8edf4;
  box-shadow: var(--shadow-md);
  align-items: stretch;
  min-height: 380px;
}

#hero-visual {
  flex: 0 0 380px;
  position: relative;
  background: #dce3ed;
  display: flex; align-items: center; justify-content: center;
  overflow: hidden;
}

#hero-visual .hero-img {
  width: 100%; height: 100%;
  object-fit: contain;
  object-position: center center;
  position: absolute; inset: 0;
  padding: 12px;
}

#hero-visual .hero-overlay {
  position: absolute; bottom: 0; left: 0; right: 0;
  height: 35%;
  background: linear-gradient(to top, rgba(220,227,237,0.9) 0%, transparent 100%);
  pointer-events: none;
}

#hero-visual .hero-standalone-name {
  position: absolute; bottom: 16px; left: 20px; right: 20px;
  z-index: 2;
  text-align: center;
}

#hero-visual .hero-standalone-name .hsn-title {
  font-size: 0.78rem; color: var(--text-dim); font-weight: 500;
  letter-spacing: 0.03em; text-shadow: 0 1px 2px rgba(255,255,255,0.6);
}

#hero-visual .hero-standalone-name .hsn-name {
  font-size: 2rem; font-weight: 800;
  background: linear-gradient(180deg, var(--text-primary) 0%, var(--accent-deep) 100%);
  -webkit-background-clip: text; -webkit-text-fill-color: transparent;
  background-clip: text;
  letter-spacing: 0.04em; line-height: 1.2;
  filter: drop-shadow(0 1px 2px rgba(255,255,255,0.4));
}

/* Hero Info Card (right side) */
#hero-info-card {
  flex: 1;
  background: var(--bg-card);
  backdrop-filter: var(--glass-blur);
  -webkit-backdrop-filter: var(--glass-blur);
  border-left: 1px solid var(--border-subtle);
  padding: 28px;
  display: flex; flex-direction: column;
  gap: 14px;
  overflow-y: auto;
  min-width: 0;
}

.hero-info-row {
  display: flex; gap: 8px; flex-wrap: wrap; align-items: center;
}

.hero-badge {
  padding: 4px 12px; border-radius: var(--radius-xl);
  font-size: 0.76rem; font-weight: 600;
  background: rgba(255,255,255,0.7);
  border: 1px solid var(--border-subtle);
  color: var(--text-secondary);
}
.hero-badge.kingdom-badge {
  font-weight: 700; font-size: 0.85rem;
  padding: 5px 15px;
  letter-spacing: 0.03em;
}
.hero-badge.hp-badge {
  color: #d0607a; font-weight: 700;
}

#hero-skills {
  display: flex; flex-direction: column; gap: 8px;
}

.skill-item {
  background: var(--accent-sky-light);
  border: 1px solid rgba(77,184,232,0.15);
  border-radius: var(--radius-md);
  padding: 8px 14px;
  transition: all var(--transition-fast);
}
.skill-item:hover {
  border-color: rgba(77,184,232,0.35);
  box-shadow: var(--shadow-glow);
}

.skill-name {
  font-size: 0.82rem; font-weight: 700;
  color: var(--accent-sky);
  margin-bottom: 3px;
}

.skill-desc {
  font-size: 0.76rem; color: var(--text-secondary);
  line-height: 1.5;
}

/* ── Context Selector ────────────────────────────────────────────── */

#context-selector {
  display: flex; gap: 20px; margin-bottom: 24px;
  flex-wrap: wrap;
  background: var(--bg-card);
  backdrop-filter: var(--glass-blur);
  -webkit-backdrop-filter: var(--glass-blur);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-lg);
  padding: 16px 22px;
  box-shadow: var(--shadow-sm);
}
.context-group { display: flex; flex-direction: column; gap: 6px; }
.context-group .ctx-label {
  font-size: 0.68rem; color: var(--text-dim);
  text-transform: uppercase; letter-spacing: 0.06em; font-weight: 700;
}
.context-pills { display: flex; gap: 4px; }

.ctx-pill {
  padding: 6px 15px; border-radius: var(--radius-xl);
  font-size: 0.78rem; cursor: pointer;
  border: 1px solid var(--border-subtle);
  background: rgba(255,255,255,0.6); color: var(--text-secondary);
  transition: all var(--transition-fast);
  font-family: var(--font-main); white-space: nowrap; user-select: none;
}
.ctx-pill:hover { border-color: var(--accent-sky); color: var(--accent-sky); }
.ctx-pill.active {
  border-color: var(--accent-sky);
  background: var(--accent-sky-light);
  color: var(--accent-sky);
  font-weight: 600;
  box-shadow: var(--shadow-glow);
}

/* ── Rating Section ──────────────────────────────────────────────── */

#rating-section {
  display: grid;
  grid-template-columns: 260px 1fr;
  gap: 32px;
  margin-bottom: 32px;
  background: var(--bg-card);
  backdrop-filter: var(--glass-blur);
  -webkit-backdrop-filter: var(--glass-blur);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-lg);
  padding: 28px;
  box-shadow: var(--shadow-sm);
}

#radar-container {
  display: flex; flex-direction: column; align-items: center;
  gap: 8px;
}
#radar-container .radar-label {
  font-size: 0.7rem; color: var(--text-dim); text-align: center; line-height: 1.4;
}
#radar-svg { width: 240px; height: 240px; }

#radar-svg .grid-poly { fill: none; stroke: rgba(0,0,0,0.05); stroke-width: 1; }
#radar-svg .axis-line { stroke: rgba(0,0,0,0.06); stroke-width: 1; }
#radar-svg .data-poly { fill: rgba(77,184,232,0.12); stroke: var(--accent-sky); stroke-width: 2; }
#radar-svg .data-dot { fill: var(--accent-sky); cursor: pointer; transition: r 0.15s; }
#radar-svg .data-dot:hover { r: 7; fill: var(--accent-pink); }
#radar-svg .axis-label { fill: var(--text-dim); font-size: 10.5px; text-anchor: middle; font-family: var(--font-main); }

#bars-container { display: flex; flex-direction: column; gap: 7px; }

.rating-bar-row { display: flex; align-items: center; gap: 10px; }
.rating-bar-label {
  width: 52px; text-align: right; font-size: 0.78rem;
  color: var(--text-secondary); font-weight: 600; flex-shrink: 0;
}
.rating-bar-track {
  flex: 1; height: 18px; display: flex; gap: 3px;
  background: #e8edf4; border-radius: var(--radius-sm);
  padding: 2px; cursor: pointer;
}
.rating-bar-seg {
  flex: 1; border-radius: 3px;
  background: rgba(0,0,0,0.02);
  transition: all var(--transition-fast);
}
.rating-bar-seg.filled {
  background: linear-gradient(135deg, var(--accent-sky), #7dd0f5);
  box-shadow: 0 0 6px rgba(77,184,232,0.25);
}
.rating-bar-seg:hover { background: rgba(77,184,232,0.12); }
.rating-bar-seg.filled:hover { filter: brightness(1.08); }
.rating-bar-val {
  width: 18px; text-align: center; font-size: 0.8rem;
  font-weight: 700; color: var(--accent-sky);
  font-variant-numeric: tabular-nums;
}

/* ── Comments ────────────────────────────────────────────────────── */

#comments-section {
  background: var(--bg-card);
  backdrop-filter: var(--glass-blur);
  -webkit-backdrop-filter: var(--glass-blur);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-lg);
  padding: 22px 26px;
  box-shadow: var(--shadow-sm);
}

#comments-section h3 {
  font-size: 0.92rem; font-weight: 700; color: var(--text-primary);
  margin-bottom: 12px; padding-bottom: 10px;
  border-bottom: 1px solid var(--border-subtle);
}

.comment-card {
  background: rgba(255,255,255,0.55);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-md);
  padding: 11px 15px; margin-bottom: 7px;
  transition: all var(--transition-fast);
}
.comment-card:hover {
  background: rgba(255,255,255,0.8);
  box-shadow: var(--shadow-sm);
}
.comment-header {
  display: flex; justify-content: space-between; align-items: center;
  margin-bottom: 4px; flex-wrap: wrap; gap: 6px;
}
.comment-author { font-size: 0.78rem; font-weight: 600; color: var(--text-secondary); }
.comment-meta { display: flex; gap: 7px; align-items: center; }
.comment-time { font-size: 0.68rem; color: var(--text-dim); }
.comment-ctx-badge {
  font-size: 0.63rem; padding: 2px 7px; border-radius: var(--radius-xl);
  background: var(--accent-pink-light); color: var(--accent-pink); font-weight: 600;
}
.comment-text { font-size: 0.84rem; line-height: 1.5; color: var(--text-primary); }
.comment-delete {
  font-size: 0.68rem; color: var(--text-dim); cursor: pointer;
  background: none; border: none; font-family: var(--font-main);
  transition: color var(--transition-fast);
}
.comment-delete:hover { color: #d0607a; }

.no-comments { text-align: center; color: var(--text-dim); padding: 18px; font-size: 0.8rem; }

#comment-form { display: flex; gap: 7px; margin-top: 12px; }
#comment-form input {
  width: 85px; padding: 8px 11px;
  background: rgba(255,255,255,0.6); border: 1px solid var(--border-subtle);
  border-radius: var(--radius-md); color: var(--text-primary);
  font-family: var(--font-main); font-size: 0.8rem; outline: none;
}
#comment-form textarea {
  flex: 1; padding: 8px 11px;
  background: rgba(255,255,255,0.6); border: 1px solid var(--border-subtle);
  border-radius: var(--radius-md); color: var(--text-primary);
  font-family: var(--font-main); font-size: 0.8rem; outline: none; resize: none;
  min-height: 36px; max-height: 78px;
}
#comment-form input:focus, #comment-form textarea:focus {
  border-color: var(--accent-sky); box-shadow: var(--shadow-glow);
}
#comment-form button {
  padding: 7px 16px; border-radius: var(--radius-md);
  font-size: 0.76rem; font-weight: 600; cursor: pointer;
  border: 1px solid var(--accent-sky);
  background: var(--accent-sky-light); color: var(--accent-sky);
  transition: all var(--transition-fast); font-family: var(--font-main);
}
#comment-form button:hover { background: var(--accent-sky); color: #fff; box-shadow: var(--shadow-glow); }

/* ── Toast ────────────────────────────────────────────────────────── */

#toast {
  position: fixed; bottom: 28px; left: 50%; transform: translateX(-50%);
  z-index: 9999; padding: 10px 24px; border-radius: var(--radius-xl);
  background: rgba(26,34,51,0.9); color: #eef3f9;
  font-size: 0.8rem; font-weight: 600;
  pointer-events: none; opacity: 0; transition: opacity 0.3s ease;
  backdrop-filter: blur(8px);
}
#toast.show { opacity: 1; }

/* ── Modal ────────────────────────────────────────────────────────── */

.modal-overlay {
  position: fixed; inset: 0; z-index: 5000;
  background: rgba(0,0,0,0.2); backdrop-filter: blur(6px);
  display: flex; align-items: center; justify-content: center;
}
.modal-box {
  background: #fff; border: 1px solid var(--border-subtle);
  border-radius: var(--radius-lg); padding: 22px;
  max-width: 390px; width: 90%; box-shadow: var(--shadow-md);
}
.modal-box h4 { font-size: 0.95rem; margin-bottom: 7px; }
.modal-box p { font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 14px; line-height: 1.5; }
.modal-box .modal-actions { display: flex; gap: 7px; justify-content: flex-end; }
.modal-box button {
  padding: 6px 16px; border-radius: var(--radius-md);
  font-size: 0.76rem; cursor: pointer; font-family: var(--font-main);
  font-weight: 600; border: 1px solid var(--border-subtle);
  background: rgba(255,255,255,0.8); color: var(--text-secondary);
  transition: all var(--transition-fast);
}
.modal-box button:hover { border-color: var(--accent-sky); }
.modal-box button.btn-primary {
  border-color: var(--accent-sky); color: var(--accent-sky);
  background: var(--accent-sky-light);
}
.modal-box button.btn-primary:hover { background: var(--accent-sky); color: #fff; }
.modal-box button.btn-danger { border-color: #d0607a; color: #d0607a; }
.modal-box button.btn-danger:hover { background: #d0607a; color: #fff; }

/* ── Responsive ──────────────────────────────────────────────────── */

@media (max-width: 1100px) {
  :root { --sidebar-width: 260px; }
  #hero-visual { flex: 0 0 300px; }
  #hero-visual .hero-standalone-name .hsn-name { font-size: 1.6rem; }
  #hero-info-card { padding: 20px; gap: 10px; }
  #rating-section { grid-template-columns: 1fr; gap: 20px; }
  #main-content { padding: 20px 24px 24px; }
}

@media (max-width: 768px) {
  #app { grid-template-columns: 1fr; grid-template-areas: "nav" "main"; }
  #sidebar {
    position: fixed; top: 52px; left: 0; bottom: 0; width: 275px;
    transform: translateX(-100%); transition: transform var(--transition-normal);
    z-index: 200;
  }
  #sidebar.open { transform: translateX(0); box-shadow: 4px 0 20px rgba(0,0,0,0.1); }
  #mobile-sidebar-toggle { display: flex !important; }
  #hero-block { flex-direction: column; min-height: auto; border-radius: var(--radius-md); }
  #hero-visual { flex: 0 0 280px; }
  #hero-info-card {
    border-left: none; border-top: 1px solid var(--border-subtle);
    border-radius: 0 0 var(--radius-md) var(--radius-md); padding: 16px;
  }
  #hero-visual .hero-standalone-name { bottom: 12px; left: 12px; right: 12px; }
  #hero-visual .hero-standalone-name .hsn-name { font-size: 1.5rem; }
  #rating-section { padding: 16px; }
  #main-content { padding: 10px; }
  #comments-section { padding: 14px; }
  #context-selector { padding: 10px 14px; gap: 10px; border-radius: var(--radius-md); }
  #top-nav { padding: 0 10px; }
  #search-box { max-width: 150px; }
}

#mobile-sidebar-toggle {
  display: none;
  padding: 5px 9px; border-radius: var(--radius-md);
  border: 1px solid var(--border-subtle);
  background: var(--bg-input); color: var(--text-secondary);
  cursor: pointer; font-size: 1rem; font-family: var(--font-main);
}

#import-input { display: none; }
</style>
</head>
<body>

<div id="decorations"></div>
<div id="toast"></div>
<input type="file" id="import-input" accept=".json">

<div id="app">
  <!-- Top Nav -->
  <nav id="top-nav">
    <button id="mobile-sidebar-toggle" onclick="toggleSidebar()" title="武將列表">☰</button>
    <span class="logo">✦ 武將評價</span>
    <div id="search-box">
      <span style="opacity:0.4;">🔍</span>
      <input type="text" id="search-input" placeholder="搜尋武將…" oninput="onSearch()">
    </div>
    <select id="sort-select" onchange="onSortChange()">
      <option value="default">預設排序</option>
      <option value="name">名稱排序</option>
      <option value="rating">評分最高</option>
      <option value="kingdom">勢力排序</option>
      <option value="hp">體力排序</option>
    </select>
    <button class="nav-btn" onclick="exportData()">📤 匯出</button>
    <button class="nav-btn" onclick="document.getElementById('import-input').click()">📥 匯入</button>
  </nav>

  <!-- Sidebar -->
  <aside id="sidebar">
    <div id="sidebar-header">
      <span style="font-weight:600;">武將列表</span>
      <span id="warrior-count">0</span>
    </div>
    <div id="package-filter"></div>
    <div class="kingdom-filters" id="kingdom-filters"></div>
    <div id="warrior-list"></div>
  </aside>

  <!-- Main Content -->
  <main id="main-content">
    <div id="empty-state">
      <div class="empty-icon">✦</div>
      <div class="empty-text">從左側選擇一位武將開始評價</div>
    </div>

    <div id="detail-panel" style="display:none;">
      <!-- Hero Block -->
      <div id="hero-block">
        <div id="hero-visual">
          <img class="hero-img" id="hero-img" src="" alt="">
          <div class="hero-overlay"></div>
          <div class="hero-standalone-name">
            <div class="hsn-title" id="hero-title-block"></div>
            <div class="hsn-name" id="hero-name-block"></div>
          </div>
        </div>
        <div id="hero-info-card">
          <div class="hero-info-row" id="hero-meta"></div>
          <div id="hero-skills"></div>
        </div>
      </div>

      <!-- Context Selector -->
      <div id="context-selector">
        <div class="context-group">
          <span class="ctx-label">模式</span>
          <div class="context-pills" id="mode-pills"></div>
        </div>
        <div class="context-group">
          <span class="ctx-label">角色</span>
          <div class="context-pills" id="role-pills"></div>
        </div>
        <div class="context-group">
          <span class="ctx-label">環境</span>
          <div class="context-pills" id="env-pills"></div>
        </div>
      </div>

      <!-- Rating Section -->
      <div id="rating-section">
        <div id="radar-container">
          <svg id="radar-svg" viewBox="0 0 240 240"></svg>
          <span class="radar-label">拖曳頂點或點擊下方評分條<br>進行評分 (1-5)</span>
        </div>
        <div id="bars-container"></div>
      </div>

      <!-- Comments -->
      <div id="comments-section">
        <h3>✦ 評價留言</h3>
        <div id="comments-list"></div>
        <div id="comment-form">
          <input type="text" id="comment-author" placeholder="名字" maxlength="20">
          <textarea id="comment-text" placeholder="寫下評價…" rows="1" maxlength="500"></textarea>
          <button onclick="addComment()">發布</button>
        </div>
      </div>
    </div>
  </main>
</div>

<script>
// ═══════════════════════════════════════════════════════════════════
// EMBEDDED DATA
// ═══════════════════════════════════════════════════════════════════

const WARRIOR_DATA = __WARRIOR_DATA__;
const KINGDOM_COLORS = __KINGDOM_COLORS__;
const SKILL_DATA = __SKILL_DATA__;
const FACTION_NAMES = __FACTION_NAMES__;

const CONTEXTS = {
  modes: [
    { id: "08p", label: "八人軍爭" },
    { id: "04_2v2", label: "2v2 歡樂" },
    { id: "02_1v1", label: "1v1 KOF" },
    { id: "06_3v3", label: "3v3" },
    { id: "hegemony", label: "國戰" }
  ],
  roles: [
    { id: "all", label: "綜合" },
    { id: "lord", label: "主公" },
    { id: "loyalist", label: "忠臣" },
    { id: "rebel", label: "反賊" },
    { id: "renegade", label: "內奸" }
  ],
  envs: [
    { id: "early", label: "早期軍爭" },
    { id: "unlimited", label: "無限制" }
  ]
};

const DIMENSIONS = [
  { id: "survival", label: "生存", desc: "存活能力、避免死亡" },
  { id: "output", label: "輸出", desc: "每回合傷害輸出" },
  { id: "burst", label: "爆發", desc: "單回合爆炸性發揮的潛力" },
  { id: "development", label: "發育", desc: "隨時間成長變強的能力" },
  { id: "control", label: "控制", desc: "限制敵方行動/手牌的能力" },
  { id: "support", label: "輔助", desc: "幫助隊友的能力" },
  { id: "defense", label: "防禦", desc: "抵銷或減少受到的傷害" },
  { id: "difficulty", label: "操作性", desc: "操作難度/上手門檻" }
];

// ═══════════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════════

let state = {
  selectedWarriorId: null,
  context: { mode: "08p", role: "all", env: "early" },
  searchQuery: "",
  activeKingdoms: new Set(),
  activePackage: "",  // "" = all
  sortBy: "default",
  ratings: {},
  comments: {},
};

// ═══════════════════════════════════════════════════════════════════
// LOCAL STORAGE
// ═══════════════════════════════════════════════════════════════════

const STORAGE_KEY = "sgs_warrior_eval";
const STORAGE_VERSION = 1;

function loadData() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return;
    const data = JSON.parse(raw);
    if (data.version === STORAGE_VERSION) {
      state.ratings = data.ratings || {};
      state.comments = data.comments || {};
    }
  } catch (e) { console.warn("Failed to load ratings", e); }
}

function saveData() {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify({
      version: STORAGE_VERSION,
      lastModified: new Date().toISOString(),
      ratings: state.ratings,
      comments: state.comments,
    }));
  } catch (e) { showToast("⚠ 儲存空間已滿，請匯出備份"); }
}

function exportData() {
  const blob = new Blob([JSON.stringify({
    version: STORAGE_VERSION,
    exportedAt: new Date().toISOString(),
    ratings: state.ratings,
    comments: state.comments,
  }, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = "sgs_warrior_eval_" + new Date().toISOString().slice(0,10) + ".json";
  a.click();
  URL.revokeObjectURL(url);
  showToast("✅ 已匯出");
}

function importData(file) {
  const reader = new FileReader();
  reader.onload = function(e) {
    try {
      const data = JSON.parse(e.target.result);
      if (!data.ratings && !data.comments) throw new Error("Invalid");
      const rc = Object.keys(data.ratings || {}).length;
      const cc = Object.values(data.comments || {}).reduce((a,b)=>a+b.length,0);
      showConfirm("匯入資料", "將匯入 " + rc + " 筆評分和 " + cc + " 條評論。現有資料將被合併。", () => {
        Object.assign(state.ratings, data.ratings || {});
        for (const [wid, cmts] of Object.entries(data.comments || {})) {
          if (!state.comments[wid]) state.comments[wid] = [];
          state.comments[wid] = state.comments[wid].concat(cmts);
        }
        saveData();
        if (state.selectedWarriorId) renderDetail();
        renderList();
        showToast("✅ 已匯入");
      });
    } catch (e) { showToast("❌ 無效的檔案格式"); }
  };
  reader.readAsText(file);
}

// ═══════════════════════════════════════════════════════════════════
// CONTEXT & RATINGS
// ═══════════════════════════════════════════════════════════════════

function getContextKey() {
  return state.context.mode + "|" + state.context.role + "|" + state.context.env;
}

function getRatings(wid) {
  const wr = state.ratings[wid] || {};
  return wr[getContextKey()] || {};
}

function setRating(wid, dimId, value) {
  if (!state.ratings[wid]) state.ratings[wid] = {};
  const ck = getContextKey();
  if (!state.ratings[wid][ck]) state.ratings[wid][ck] = {};
  state.ratings[wid][ck][dimId] = value;
  saveData();
}

function getAvgRating(wid) {
  const wr = state.ratings[wid];
  if (!wr) return 0;
  let sum = 0, count = 0;
  for (const ctx of Object.values(wr)) {
    for (const v of Object.values(ctx)) {
      if (typeof v === "number") { sum += v; count++; }
    }
  }
  return count > 0 ? (sum / count) : 0;
}

// ═══════════════════════════════════════════════════════════════════
// FILTERING & SORTING
// ═══════════════════════════════════════════════════════════════════

function getFilteredWarriors() {
  let list = [...WARRIOR_DATA];

  if (state.activeKingdoms.size > 0) {
    list = list.filter(w => state.activeKingdoms.has(w.kingdom));
  }

  if (state.activePackage) {
    list = list.filter(w => w.primaryPackage === state.activePackage);
  }

  if (state.searchQuery) {
    const q = state.searchQuery.toLowerCase();
    list = list.filter(w =>
      w.name.toLowerCase().includes(q) ||
      w.title.toLowerCase().includes(q) ||
      w.id.toLowerCase().includes(q)
    );
  }

  switch (state.sortBy) {
    case "default":
      list.sort((a, b) => a.sortIdx - b.sortIdx);
      break;
    case "name":
      list.sort((a, b) => a.name.localeCompare(b.name, "zh"));
      break;
    case "rating":
      list.sort((a, b) => getAvgRating(b.id) - getAvgRating(a.id));
      break;
    case "kingdom":
      list.sort((a, b) => (a.kingdom||"zzz").localeCompare(b.kingdom||"zzz"));
      break;
    case "hp":
      list.sort((a, b) => (b.hp||0) - (a.hp||0));
      break;
  }

  return list;
}

// ═══════════════════════════════════════════════════════════════════
// PACKAGE FILTER
// ═══════════════════════════════════════════════════════════════════

function getPackages() {
  const pkgs = new Map();
  for (const w of WARRIOR_DATA) {
    if (w.primaryPackage && !pkgs.has(w.primaryPackage)) {
      pkgs.set(w.primaryPackage, {
        id: w.primaryPackage,
        display: w.packageDisplay || w.primaryPackage,
        count: 0
      });
    }
    if (w.primaryPackage) {
      pkgs.get(w.primaryPackage).count++;
    }
  }
  return [...pkgs.values()].sort((a,b) => b.count - a.count);
}

function renderPackageFilter() {
  const container = document.getElementById("package-filter");
  const pkgs = getPackages();
  container.innerHTML = '<span class="pkg-pill' + (!state.activePackage ? ' active' : '') + '" onclick="state.activePackage=\'\';renderPackageFilter();renderList();">全部</span>';
  for (const p of pkgs) {
    const active = state.activePackage === p.id ? ' active' : '';
    container.innerHTML += '<span class="pkg-pill' + active + '" onclick="state.activePackage=\'' + p.id + '\';renderPackageFilter();renderList();">' + p.display + '</span>';
  }
}

// ═══════════════════════════════════════════════════════════════════
// KINGDOM FILTERS
// ═══════════════════════════════════════════════════════════════════

function renderKingdomFilters() {
  const container = document.getElementById("kingdom-filters");
  const kingdoms = new Map();
  for (const w of WARRIOR_DATA) {
    if (w.kingdom && !kingdoms.has(w.kingdom)) {
      kingdoms.set(w.kingdom, FACTION_NAMES[w.kingdom] || w.kingdom);
    }
  }
  container.innerHTML = "";
  for (const [kid, kname] of kingdoms) {
    const active = state.activeKingdoms.has(kid) ? ' active' : '';
    const color = KINGDOM_COLORS[kid] || '#889';
    container.innerHTML += '<span class="kd-pill' + active + '" style="color:' + color + '" onclick="toggleKingdom(\'' + kid + '\')" title="' + kname + '">' + kname[0] + '</span>';
  }
}

function toggleKingdom(kid) {
  if (state.activeKingdoms.has(kid)) {
    state.activeKingdoms.delete(kid);
  } else {
    state.activeKingdoms.add(kid);
  }
  renderKingdomFilters();
  renderList();
}

// ═══════════════════════════════════════════════════════════════════
// WARRIOR LIST
// ═══════════════════════════════════════════════════════════════════

const ITEM_HEIGHT = 64;
const BUFFER = 12;

function renderList() {
  const filtered = getFilteredWarriors();
  document.getElementById("warrior-count").textContent = filtered.length;

  const container = document.getElementById("warrior-list");
  container.innerHTML = "";

  // Group by package for display
  let lastPkg = null;
  let topOffset = 0;
  const items = [];

  for (const w of filtered) {
    const pkgDisplay = w.packageDisplay || w.primaryPackage;
    if (state.sortBy === "default" && !state.activePackage && pkgDisplay !== lastPkg) {
      items.push({ type: "header", label: pkgDisplay });
      lastPkg = pkgDisplay;
    }
    items.push({ type: "warrior", data: w });
  }

  // Build total height
  let totalH = 0;
  for (const item of items) {
    totalH += item.type === "header" ? 24 : ITEM_HEIGHT;
  }

  const spacer = document.createElement("div");
  spacer.style.height = totalH + "px";
  spacer.style.position = "relative";
  container.appendChild(spacer);

  // Render visible only
  function renderVisible() {
    const st = container.scrollTop;
    const ch = container.clientHeight;
    const si = Math.max(0, Math.floor(st / ITEM_HEIGHT) - BUFFER);

    // Remove old
    spacer.querySelectorAll(".warrior-card,.pkg-group-header").forEach(e => e.remove());

    let y = 0, idx = 0;
    for (const item of items) {
      const h = item.type === "header" ? 24 : ITEM_HEIGHT;
      if (y + h >= st - BUFFER * ITEM_HEIGHT && y <= st + ch + BUFFER * ITEM_HEIGHT) {
        if (item.type === "header") {
          const el = document.createElement("div");
          el.className = "pkg-group-header";
          el.textContent = item.label;
          el.style.position = "absolute";
          el.style.top = y + "px";
          el.style.left = "8px";
          el.style.right = "8px";
          spacer.appendChild(el);
        } else {
          const card = createWarriorCard(item.data);
          card.style.position = "absolute";
          card.style.top = y + "px";
          card.style.left = "4px";
          card.style.right = "4px";
          card.style.height = (ITEM_HEIGHT - 2) + "px";
          spacer.appendChild(card);
        }
      }
      y += h;
    }
  }

  container.onscroll = () => requestAnimationFrame(renderVisible);
  renderVisible();
}

function createWarriorCard(w) {
  const div = document.createElement("div");
  div.className = "warrior-card";
  if (w.id === state.selectedWarriorId) div.classList.add("selected");
  div.onclick = () => selectWarrior(w.id);

  if (w.cardImg) {
    const img = document.createElement("img");
    img.className = "card-thumb";
    img.src = w.cardImg;
    img.loading = "lazy";
    img.onerror = function() { this.style.display = "none"; this.nextSibling.style.display = "flex"; };
    div.appendChild(img);
    const ph = document.createElement("div");
    ph.className = "card-thumb placeholder";
    ph.style.display = "none"; ph.textContent = "?";
    div.appendChild(ph);
  } else {
    const ph = document.createElement("div");
    ph.className = "card-thumb placeholder";
    ph.textContent = "?";
    div.appendChild(ph);
  }

  const info = document.createElement("div");
  info.className = "card-info";
  const nameEl = document.createElement("div");
  nameEl.className = "card-name";
  nameEl.textContent = w.name;
  info.appendChild(nameEl);

  const sub = document.createElement("div");
  sub.className = "card-subtitle";
  if (w.kingdomName) {
    const kd = document.createElement("span");
    kd.className = "card-kd";
    kd.style.color = KINGDOM_COLORS[w.kingdom] || "#889";
    kd.textContent = w.kingdomName;
    sub.appendChild(kd);
  }
  if (w.hp) {
    const hp = document.createElement("span");
    hp.textContent = "❤" + w.hp;
    hp.style.color = "#d08090";
    sub.appendChild(hp);
  }
  if (w.title) {
    const t = document.createElement("span");
    t.textContent = w.title;
    t.style.maxWidth = "120px";
    t.style.overflow = "hidden";
    t.style.textOverflow = "ellipsis";
    sub.appendChild(t);
  }
  info.appendChild(sub);
  div.appendChild(info);

  const avg = getAvgRating(w.id);
  if (avg > 0) {
    const badge = document.createElement("div");
    badge.className = "card-rating-badge";
    badge.textContent = avg.toFixed(1);
    div.appendChild(badge);
  }

  return div;
}

// ═══════════════════════════════════════════════════════════════════
// SELECTION
// ═══════════════════════════════════════════════════════════════════

function selectWarrior(wid) {
  state.selectedWarriorId = wid;
  renderList();
  renderDetail();
  document.getElementById("sidebar").classList.remove("open");
}

// ═══════════════════════════════════════════════════════════════════
// DETAIL PANEL
// ═══════════════════════════════════════════════════════════════════

function renderDetail() {
  const w = WARRIOR_DATA.find(x => x.id === state.selectedWarriorId);
  if (!w) return;

  document.getElementById("empty-state").style.display = "none";
  document.getElementById("detail-panel").style.display = "block";

  // Hero visual
  const heroImg = document.getElementById("hero-img");
  if (w.fullImg || w.cardImg) {
    heroImg.src = w.fullImg || w.cardImg;
    heroImg.style.display = "";
  } else {
    heroImg.style.display = "none";
  }
  document.getElementById("hero-title-block").textContent = w.title || "";
  document.getElementById("hero-name-block").textContent = w.name;

  // Hero info card
  const meta = document.getElementById("hero-meta");
  meta.innerHTML = "";
  if (w.kingdom) {
    const kb = document.createElement("span");
    kb.className = "hero-badge kingdom-badge";
    kb.textContent = (FACTION_NAMES[w.kingdom] || w.kingdom);
    kb.style.color = KINGDOM_COLORS[w.kingdom] || "#667788";
    kb.style.background = (KINGDOM_COLORS[w.kingdom] || "#667788") + "15";
    meta.appendChild(kb);
  }
  if (w.hp) {
    const hb = document.createElement("span");
    hb.className = "hero-badge hp-badge";
    hb.textContent = "❤ " + w.hp + " HP";
    meta.appendChild(hb);
  }
  if (w.packageDisplay) {
    const pb = document.createElement("span");
    pb.className = "hero-badge";
    pb.textContent = "📦 " + w.packageDisplay;
    meta.appendChild(pb);
  }
  if (w.illustrator) {
    const ib = document.createElement("span");
    ib.className = "hero-badge";
    ib.textContent = "🎨 " + w.illustrator;
    meta.appendChild(ib);
  }

  // Skills — always visible with name + description
  const skillsContainer = document.getElementById("hero-skills");
  skillsContainer.innerHTML = "";
  if (w.skills && w.skills.length > 0) {
    for (const skId of w.skills) {
      const sk = SKILL_DATA[skId];
      const item = document.createElement("div");
      item.className = "skill-item";
      const nameEl = document.createElement("div");
      nameEl.className = "skill-name";
      nameEl.textContent = sk ? sk.name : skId;
      item.appendChild(nameEl);
      if (sk && sk.desc) {
        const descEl = document.createElement("div");
        descEl.className = "skill-desc";
        descEl.textContent = sk.desc;
        item.appendChild(descEl);
      }
      skillsContainer.appendChild(item);
    }
  } else {
    skillsContainer.innerHTML = '<span style=\"font-size:0.78rem;color:var(--text-dim);\">尚無技能資料</span>';
  }

  // Context pills
  renderContextPills("mode-pills", "modes", "mode");
  renderContextPills("role-pills", "roles", "role");
  renderContextPills("env-pills", "envs", "env");

  // Rating
  renderRadarChart();
  renderRatingBars();

  // Comments
  renderComments();
}

function renderContextPills(containerId, ctxKey, stateKey) {
  const container = document.getElementById(containerId);
  container.innerHTML = "";
  for (const item of CONTEXTS[ctxKey]) {
    const pill = document.createElement("button");
    pill.className = "ctx-pill";
    if (state.context[stateKey] === item.id) pill.classList.add("active");
    pill.textContent = item.label;
    pill.onclick = () => {
      state.context[stateKey] = item.id;
      renderDetail();
    };
    container.appendChild(pill);
  }
}

// ═══════════════════════════════════════════════════════════════════
// RADAR CHART
// ═══════════════════════════════════════════════════════════════════

let radarDragState = null;

function renderRadarChart() {
  const svg = document.getElementById("radar-svg");
  svg.innerHTML = "";
  const w = state.selectedWarriorId;
  const ratings = getRatings(w);
  const cx = 120, cy = 120, maxR = 100;
  const n = DIMENSIONS.length;

  // Grid rings
  for (let level = 1; level <= 5; level++) {
    const r = (level / 5) * maxR;
    const pts = [];
    for (let i = 0; i < n; i++) {
      const a = (Math.PI * 2 / n) * i - Math.PI / 2;
      pts.push((cx + r * Math.cos(a)) + "," + (cy + r * Math.sin(a)));
    }
    const poly = document.createElementNS("http://www.w3.org/2000/svg", "polygon");
    poly.setAttribute("points", pts.join(" "));
    poly.setAttribute("class", "grid-poly");
    svg.appendChild(poly);
  }

  // Axes
  for (let i = 0; i < n; i++) {
    const a = (Math.PI * 2 / n) * i - Math.PI / 2;
    const x = cx + maxR * Math.cos(a), y = cy + maxR * Math.sin(a);
    const line = document.createElementNS("http://www.w3.org/2000/svg", "line");
    line.setAttribute("x1", cx); line.setAttribute("y1", cy);
    line.setAttribute("x2", x); line.setAttribute("y2", y);
    line.setAttribute("class", "axis-line");
    svg.appendChild(line);

    const lx = cx + (maxR + 16) * Math.cos(a);
    const ly = cy + (maxR + 16) * Math.sin(a);
    const text = document.createElementNS("http://www.w3.org/2000/svg", "text");
    text.setAttribute("x", lx); text.setAttribute("y", ly);
    text.setAttribute("class", "axis-label");
    text.setAttribute("dominant-baseline", "middle");
    text.textContent = DIMENSIONS[i].label;
    svg.appendChild(text);
  }

  // Data polygon
  const dataPts = [];
  for (let i = 0; i < n; i++) {
    const val = ratings[DIMENSIONS[i].id] || 0;
    const r = (val / 5) * maxR;
    const a = (Math.PI * 2 / n) * i - Math.PI / 2;
    dataPts.push({
      x: cx + r * Math.cos(a), y: cy + r * Math.sin(a),
      val, dimId: DIMENSIONS[i].id, i
    });
  }

  if (dataPts.some(p => p.val > 0)) {
    const poly = document.createElementNS("http://www.w3.org/2000/svg", "polygon");
    poly.setAttribute("points", dataPts.map(p => p.x + "," + p.y).join(" "));
    poly.setAttribute("class", "data-poly");
    svg.appendChild(poly);
  }

  for (const dp of dataPts) {
    const circle = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    circle.setAttribute("cx", dp.val > 0 ? dp.x : cx + (maxR * 0.05) * Math.cos((Math.PI*2/n)*dp.i - Math.PI/2));
    circle.setAttribute("cy", dp.val > 0 ? dp.y : cy + (maxR * 0.05) * Math.sin((Math.PI*2/n)*dp.i - Math.PI/2));
    circle.setAttribute("r", dp.val > 0 ? "5" : "3");
    circle.setAttribute("class", "data-dot");
    circle.setAttribute("data-dim", dp.dimId);
    circle.style.cursor = "pointer";
    circle.addEventListener("click", (e) => {
      e.stopPropagation();
      const cur = ratings[dp.dimId] || 0;
      setRating(w, dp.dimId, cur >= 5 ? 0 : cur + 1);
      renderRadarChart();
      renderRatingBars();
      renderList();
    });

    circle.addEventListener("mousedown", (e) => {
      e.preventDefault();
      radarDragState = { dimId: dp.dimId, svg };
    });
    svg.appendChild(circle);
  }

  document.onmousemove = (e) => {
    if (!radarDragState) return;
    const rect = radarDragState.svg.getBoundingClientRect();
    const mx = (e.clientX - rect.left) / rect.width * 240;
    const my = (e.clientY - rect.top) / rect.height * 240;
    const dist = Math.sqrt((mx-cx)**2 + (my-cy)**2);
    const val = Math.max(0, Math.min(5, Math.round(dist / maxR * 5)));
    setRating(w, radarDragState.dimId, val);
    renderRadarChart();
    renderRatingBars();
    renderList();
  };
  document.onmouseup = () => { radarDragState = null; };
}

// ═══════════════════════════════════════════════════════════════════
// RATING BARS
// ═══════════════════════════════════════════════════════════════════

function renderRatingBars() {
  const container = document.getElementById("bars-container");
  container.innerHTML = "";
  const w = state.selectedWarriorId;
  const ratings = getRatings(w);

  for (const dim of DIMENSIONS) {
    const val = ratings[dim.id] || 0;
    const row = document.createElement("div");
    row.className = "rating-bar-row";

    const label = document.createElement("div");
    label.className = "rating-bar-label";
    label.textContent = dim.label;
    label.title = dim.desc;
    row.appendChild(label);

    const track = document.createElement("div");
    track.className = "rating-bar-track";
    track.title = dim.desc;
    for (let i = 1; i <= 5; i++) {
      const seg = document.createElement("div");
      seg.className = "rating-bar-seg" + (i <= val ? " filled" : "");
      seg.onclick = () => {
        setRating(w, dim.id, val === i ? i - 1 : i);
        renderRadarChart();
        renderRatingBars();
        renderList();
      };
      track.appendChild(seg);
    }
    row.appendChild(track);

    const valEl = document.createElement("div");
    valEl.className = "rating-bar-val";
    valEl.textContent = val > 0 ? val : "-";
    row.appendChild(valEl);
    container.appendChild(row);
  }
}

// ═══════════════════════════════════════════════════════════════════
// COMMENTS
// ═══════════════════════════════════════════════════════════════════

function renderComments() {
  const container = document.getElementById("comments-list");
  const w = state.selectedWarriorId;
  const all = state.comments[w] || [];
  const ck = getContextKey();
  const filtered = all.filter(c => !c.contextKey || c.contextKey === ck);

  if (filtered.length === 0) {
    container.innerHTML = '<div class="no-comments">尚無評價留言 ✨</div>';
  } else {
    container.innerHTML = filtered.map((c, idx) =>
      '<div class="comment-card"><div class="comment-header">' +
      '<span class="comment-author">' + esc(c.author||"匿名") + '</span>' +
      '<div class="comment-meta">' +
      '<span class="comment-ctx-badge">' + (c.contextKey||"") + '</span>' +
      '<span class="comment-time">' + fmtTime(c.timestamp) + '</span>' +
      '<button class="comment-delete" onclick="deleteComment(\'' + w + '\',' + idx + ')">✕</button>' +
      '</div></div>' +
      '<div class="comment-text">' + esc(c.text) + '</div></div>'
    ).join("");
  }
}

function addComment() {
  const w = state.selectedWarriorId;
  if (!w) return;
  const author = document.getElementById("comment-author").value.trim() || "匿名";
  const text = document.getElementById("comment-text").value.trim();
  if (!text) return;
  if (!state.comments[w]) state.comments[w] = [];
  state.comments[w].push({
    id: crypto.randomUUID ? crypto.randomUUID() : Date.now().toString(36),
    author, text,
    timestamp: new Date().toISOString(),
    contextKey: getContextKey(),
  });
  saveData();
  document.getElementById("comment-text").value = "";
  renderComments();
}

function deleteComment(wid, idx) {
  if (!state.comments[wid]) return;
  state.comments[wid].splice(idx, 1);
  saveData();
  renderComments();
}

function fmtTime(ts) {
  try {
    const d = new Date(ts);
    return d.toLocaleDateString("zh-CN",{month:"short",day:"numeric"}) + " " +
           d.toLocaleTimeString("zh-CN",{hour:"2-digit",minute:"2-digit"});
  } catch(e) { return ts; }
}

function esc(str) {
  const d = document.createElement("div");
  d.textContent = str;
  return d.innerHTML;
}

// ═══════════════════════════════════════════════════════════════════
// SEARCH, SORT
// ═══════════════════════════════════════════════════════════════════

function onSearch() {
  state.searchQuery = document.getElementById("search-input").value;
  renderList();
}

function onSortChange() {
  state.sortBy = document.getElementById("sort-select").value;
  renderList();
}

// ═══════════════════════════════════════════════════════════════════
// TOAST, MODAL, SIDEBAR
// ═══════════════════════════════════════════════════════════════════

let toastTimer;
function showToast(msg) {
  const el = document.getElementById("toast");
  el.textContent = msg;
  el.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove("show"), 2500);
}

function showConfirm(title, msg, onConfirm) {
  const overlay = document.createElement("div");
  overlay.className = "modal-overlay";
  overlay.innerHTML = '<div class="modal-box"><h4>' + title + '</h4><p>' + msg +
    '</p><div class="modal-actions"><button class="btn-cancel">取消</button><button class="btn-primary">確認</button></div></div>';
  overlay.querySelector(".btn-cancel").onclick = () => overlay.remove();
  overlay.querySelector(".btn-primary").onclick = () => { overlay.remove(); onConfirm(); };
  overlay.onclick = (e) => { if (e.target === overlay) overlay.remove(); };
  document.body.appendChild(overlay);
}

function toggleSidebar() {
  document.getElementById("sidebar").classList.toggle("open");
}

// ═══════════════════════════════════════════════════════════════════
// DECORATIONS
// ═══════════════════════════════════════════════════════════════════

function initDeco() {
  const c = document.getElementById("decorations");
  // Hexagonal decorations (Blue Archive style)
  for (let i = 0; i < 6; i++) {
    const d = document.createElement("div");
    d.className = "deco-hex";
    const s = 50 + Math.random() * 110;
    d.style.width = s + "px"; d.style.height = (s * 1.15) + "px";
    d.style.left = Math.random() * 92 + "%";
    d.style.top = Math.random() * 92 + "%";
    d.style.opacity = (0.15 + Math.random() * 0.3);
    d.style.transform = "rotate(" + (Math.random() * 360) + "deg)";
    c.appendChild(d);
  }
  // Cross decorations (Blue Archive UI motif)
  for (let i = 0; i < 8; i++) {
    const d = document.createElement("div");
    d.className = "deco-cross";
    const s = 12 + Math.random() * 28;
    d.style.width = s + "px"; d.style.height = s + "px";
    d.style.left = Math.random() * 92 + "%";
    d.style.top = Math.random() * 92 + "%";
    d.style.animationDelay = Math.random() * 3 + "s";
    c.appendChild(d);
  }
  // Glowing dots
  for (let i = 0; i < 12; i++) {
    const d = document.createElement("div");
    d.className = "deco-dot";
    const s = 3 + Math.random() * 6;
    d.style.width = s + "px"; d.style.height = s + "px";
    d.style.left = Math.random() * 94 + "%";
    d.style.top = Math.random() * 94 + "%";
    d.style.animationDelay = Math.random() * 6 + "s";
    d.style.animationDuration = (4 + Math.random() * 8) + "s";
    c.appendChild(d);
  }
  // Empty circle rings
  for (let i = 0; i < 3; i++) {
    const d = document.createElement("div");
    d.className = "deco-circle";
    const s = 40 + Math.random() * 120;
    d.style.width = s + "px"; d.style.height = s + "px";
    d.style.left = Math.random() * 90 + "%";
    d.style.top = Math.random() * 90 + "%";
    c.appendChild(d);
  }
}

// ═══════════════════════════════════════════════════════════════════
// IMPORT FILE
// ═══════════════════════════════════════════════════════════════════

document.getElementById("import-input").addEventListener("change", function(e) {
  if (this.files && this.files[0]) {
    importData(this.files[0]);
    this.value = "";
  }
});

// ═══════════════════════════════════════════════════════════════════
// INIT
// ═══════════════════════════════════════════════════════════════════

function init() {
  loadData();
  initDeco();
  renderPackageFilter();
  renderKingdomFilters();
  renderList();
}

document.addEventListener("DOMContentLoaded", init);
</script>
</body>
</html>"""


# ── HTML Generation ─────────────────────────────────────────────────

def generate_html(warriors_data, kingdom_colors, skill_data):
    """Inject warrior data, kingdom colors, and skill data into HTML template."""
    warriors_json = json.dumps(warriors_data, ensure_ascii=False)
    skills_json = json.dumps(skill_data, ensure_ascii=False)
    faction_names_json = json.dumps(FACTION_NAMES, ensure_ascii=False)

    kc = {k: v for k, v in kingdom_colors.items() if k in FACTION_NAMES or k in ["red"]}
    kc_json = json.dumps(kc, ensure_ascii=False)

    html = HTML_TEMPLATE
    html = html.replace("__WARRIOR_DATA__", warriors_json)
    html = html.replace("__KINGDOM_COLORS__", kc_json)
    html = html.replace("__SKILL_DATA__", skills_json)
    html = html.replace("__FACTION_NAMES__", faction_names_json)

    return html


# ── CLI ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="QSanguosha Warrior Evaluation System Builder",
    )
    parser.add_argument("-o", "--output", default="warrior_eval_system.html",
                        help="Output HTML file path")
    parser.add_argument("--limit", type=int, default=0,
                        help="Limit warriors for testing (0=all)")
    args = parser.parse_args()

    out_path = Path(args.output)
    if not out_path.is_absolute():
        out_path = Path.cwd() / out_path

    print("Extracting warrior definitions...", file=sys.stderr)
    warriors = extract_warriors()
    print(f"  {len(warriors)} warriors found", file=sys.stderr)

    print("Extracting skill translations...", file=sys.stderr)
    skill_data = extract_skill_translations()
    print(f"  {len(skill_data)} skills found", file=sys.stderr)

    print("Resolving image paths...", file=sys.stderr)
    warriors_data = resolve_image_paths(warriors)
    with_img = sum(1 for w in warriors_data if w["cardImg"])
    with_skills = sum(1 for w in warriors_data if w["skills"])
    print(f"  {with_img} warriors with card images", file=sys.stderr)
    print(f"  {with_skills} warriors with skill data", file=sys.stderr)

    if args.limit > 0:
        warriors_data = warriors_data[:args.limit]
        print(f"  Limited to {len(warriors_data)} warriors", file=sys.stderr)

    print("Parsing kingdom colors...", file=sys.stderr)
    kingdom_colors = parse_kingdom_colors(CONFIG_LUA)

    print("Generating HTML...", file=sys.stderr)
    html = generate_html(warriors_data, kingdom_colors, skill_data)

    out_path.write_text(html, encoding="utf-8")
    file_size_mb = len(html) / (1024 * 1024)
    print(f"✅ Written {out_path} ({file_size_mb:.1f} MB, {len(warriors_data)} warriors, {len(skill_data)} skills)",
          file=sys.stderr)


if __name__ == "__main__":
    main()
