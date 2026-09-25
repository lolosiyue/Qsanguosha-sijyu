#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Asset audit for QSanguosha-v2: find missing and orphaned (unused) assets.

Two directions are checked, because "missing" alone cannot tell whether an
asset tree is consistent:

  MISSING  a reference the engine or a Lua extension actually builds, whose
           file is absent on disk.
  ORPHAN   a file under the asset roots that no reference and no naming rule
           can reach — dead weight in the package.

Reference sources (the authority, not a guess):
  * literal "image/..." / "audio/..." strings in src/, qml/, lua/,
    extensions/ and ui-script/.
  * general ids from `sgs.General(ext, "id", ...)` (Lua) and
    `new General(this, "id", ...)` (C++). They drive image/generals/card,
    image/fullskin/generals/full, image/generals/avatar,
    image/compact/generals/small and audio/death lookups.
  * skill names: Skill::initMediaSource (src/core/skill.cpp) probes
    audio/skill/<name><n>.ogg then audio/skill/<name>.ogg for the skill's
    object name, so any name that reaches a skill constructor is a reference.
  * card object names: only `setObjectName` calls inside constructors of
    classes derived from Card. A blanket setObjectName sweep is wrong —
    it drags in QWidget names such as "chat_box" or "GameTimer".

Filename matching is case-insensitive: general ids in Lua are lower case
(`baiban`) while the art on disk is camel case (`Baiban.jpg`), and Windows
resolves that transparently. Comparing case-sensitively invents thousands of
phantom orphans.
"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict
from pathlib import Path

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT = Path(os.path.dirname(os.path.abspath(__file__))).parent

IMAGE_EXT = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".svg"}
AUDIO_EXT = {".ogg", ".mp3", ".wav", ".m4a", ".flac"}
MEDIA_EXT = IMAGE_EXT | AUDIO_EXT

# Pure runtime payload: every file here is a candidate for either direction.
ASSET_ROOTS = ("image", "audio", "hero-skin", "font", "resource")

# Where references may legitimately be written. tools/, docs/ and tests/ are
# excluded on purpose — a path printed by a helper script is not a runtime
# lookup, and including it buries the real findings.
REF_SOURCES = {
    "src": (".cpp", ".h", ".hpp"),
    "qml": (".qml", ".js"),
    "lua": (".lua",),
    "extensions": (".lua",),
    "ui-script": (".qml", ".js", ".lua"),
}

LITERAL_RE = re.compile(r"""["']((?:image|audio|hero-skin|font)/[^"'\n]*)["']""")
# MIME type strings look like asset paths ("audio/ogg") but are never opened.
MIME_SKIP = {"audio/mpeg", "audio/ogg", "audio/wav", "audio/mp3", "audio/aac",
             "audio/flac", "audio/webm", "image/png", "image/jpeg", "image/gif",
             "image/webp", "image/bmp", "image/svg+xml"}
# A literal containing any of these is completed at runtime, so it cannot be
# tested for existence as written.
TEMPLATE_MARK = ("%1", "%2", "%3", "arg(", "${", "..", "\\")

# General-driven art trees. `card` and `full` are drawn unconditionally when a
# general is shown, so they are required; the others are optional extras and
# are only reported as orphans, never as gaps.
GENERAL_TREES = {
    "generals/card": ("image/generals/card", IMAGE_EXT),
    "generals/full": ("image/fullskin/generals/full", IMAGE_EXT),
    "generals/avatar": ("image/generals/avatar", IMAGE_EXT),
    "generals/compact": ("image/compact/generals/small", IMAGE_EXT),
    "generals/dual": ("image/fullskin/generals/fulldual", IMAGE_EXT),
    "death": ("audio/death", AUDIO_EXT),
}
GENERAL_REQUIRED = ("generals/card", "generals/full")

CARD_TREES = {
    "card": ("image/card", IMAGE_EXT),
    "equips": ("image/equips", IMAGE_EXT),
    "big-card": ("image/big-card", IMAGE_EXT),
}

SKIN_SUFFIX_RE = re.compile(r"(_skin\d+|_[A-Za-z]*[Ss]kin\d*)$")
VARIANT_SUFFIXES = ("_Plus", "_Four", "_Five", "_Six", "_Seven", "_Eight",
                    "_Two", "_Three", "_extra", "_nw", "_SP")

# The R2 uploader keeps a manifest of every object it ships. Comparing it with
# the working tree separates the two very different meanings of "missing":
# an asset the pack simply has not downloaded yet, versus one that does not
# exist anywhere.
MANIFEST_PATH = "tools/r2/.wrangler/sync-manifest.json"


def rel(path):
    return str(Path(path).relative_to(ROOT)).replace("\\", "/")


def stem_of(name):
    return re.sub(r"\.[^.]+$", "", name)


def fold(name):
    """Case-insensitive comparison key (Windows resolves assets this way)."""
    return name.casefold()


def canon_general(name):
    """Fold a general id or general-art filename to a comparable key.

    Ids carry variant markers that also appear in filenames: `caocao`,
    `caocao$`, `caocao_skin2`, `CaoCao_Four`, `CaoCao_Plus`. The bare
    character name is the only form that matches both sides without inventing
    pairs.
    """
    s = name[:-1] if name.endswith("$") else name
    for suffix in VARIANT_SUFFIXES:
        if s.endswith(suffix):
            s = s[: -len(suffix)]
            break
    s = SKIN_SUFFIX_RE.sub("", s)
    return fold(s)


def canon_skill(name):
    """Fold a skill-audio filename to its skill name (audio is numbered)."""
    return fold(re.sub(r"\d+$", "", name))


# --------------------------------------------------------------------------
# reference collection
# --------------------------------------------------------------------------

def iter_ref_files():
    for sub, exts in REF_SOURCES.items():
        base = ROOT / sub
        if not base.is_dir():
            continue
        for f in base.rglob("*"):
            if f.is_file() and f.suffix.lower() in exts:
                yield sub, f


def read_text(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return ""


def strip_comments(text, lua):
    """Blank out comments while preserving line numbers.

    Commented-out registrations are common here — `--Ange2 = sgs.General(...)`
    next to a live definition two thousand lines later — and treating them as
    real produces warriors that were deliberately disabled, hence phantom
    "missing art" rows.
    """
    line_comment = "--" if lua else "//"
    block_open, block_close = ("--[[", "]]") if lua else ("/*", "*/")
    out = []
    i, n = 0, len(text)
    quote = None
    while i < n:
        c = text[i]
        if quote is not None:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == quote:
                quote = None
            i += 1
            continue
        if c in "\"'":
            quote = c
            out.append(c)
            i += 1
            continue
        if text.startswith(block_open, i):
            end = text.find(block_close, i + len(block_open))
            stop = n if end < 0 else end + len(block_close)
            out.append("\n" * text.count("\n", i, stop))
            i = stop
            continue
        if text.startswith(line_comment, i):
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue
        out.append(c)
        i += 1
    return "".join(out)


def read_source(path):
    """File content with comments removed (line numbers preserved)."""
    return strip_comments(read_text(path), path.suffix.lower() == ".lua")


def collect_literals():
    """Literal asset references, with source location, split by testability."""
    exact = defaultdict(list)
    templated = defaultdict(list)
    for _sub, f in iter_ref_files():
        text = read_source(f)
        origin = rel(f)
        for lineno, line in enumerate(text.splitlines(), 1):
            for m in LITERAL_RE.finditer(line):
                ref = m.group(1).strip().rstrip("\\").strip()
                if not ref or ref.endswith("/") or ref in MIME_SKIP:
                    continue
                if any(mark in ref for mark in TEMPLATE_MARK):
                    templated[ref].append(f"{origin}:{lineno}")
                else:
                    exact[ref].append(f"{origin}:{lineno}")
    return exact, templated


def collect_general_ids():
    ids = defaultdict(list)
    lua_pat = re.compile(r'sgs\.General\s*\([^,]+,\s*"([^"]+)"')
    cxx_pat = re.compile(r'new\s+\w*General\s*\(\s*this\s*,\s*"([^"]+)"')
    for _sub, f in iter_ref_files():
        text = read_source(f)
        pat = lua_pat if f.suffix.lower() == ".lua" else cxx_pat
        for m in pat.finditer(text):
            ids[m.group(1)].append(rel(f))
    return ids


def collect_skill_names():
    """Any string handed to a *Skill* API, plus Lua table name fields."""
    names = defaultdict(list)
    cxx_pat = re.compile(r'\b\w*[Ss]kill\w*\s*\(\s*"([^"]+)"')
    lua_name_pat = re.compile(r'\bname\s*=\s*"([A-Za-z_][\w]*)"')
    for _sub, f in iter_ref_files():
        text = read_source(f)
        origin = rel(f)
        for m in cxx_pat.finditer(text):
            names[m.group(1)].append(origin)
        if f.suffix.lower() == ".lua":
            for m in lua_name_pat.finditer(text):
                names[m.group(1)].append(origin)
    return names


def collect_card_classes():
    """Every class derived from Card, resolved through the inheritance graph.

    A plain `setObjectName("...")` sweep picks up widgets ("chat_box",
    "GameTimer") and the card editor's form fields ("Name", "Title"); only
    Card subclasses produce card object names that reach image/card/<name>.jpg.
    Skill cards are excluded: the UI never draws art for them
    (Card::getTypeId() == TypeSkill, src/core/card.cpp).
    """
    parents = {}
    decl = re.compile(r"class\s+(\w+)\s*(?::\s*public\s+([\w:]+))?")
    for f in list((ROOT / "src").rglob("*.h")) + list((ROOT / "src").rglob("*.cpp")):
        for m in decl.finditer(read_source(f)):
            cls = m.group(1)
            base = (m.group(2) or "").split("::")[-1]
            # A forward declaration ("class Axe;") has no base and must not
            # shadow the real definition that follows it.
            if base or cls not in parents:
                parents[cls] = base

    cache = {}

    def chain(cls, seen=None):
        """Return (direct Card subclass at the top of the chain, is_a_card)."""
        if cls in cache:
            return cache[cls]
        seen = seen or set()
        if cls in seen:
            return ("", False)
        seen.add(cls)
        base = parents.get(cls)
        if base == "Card":
            result = (cls, True)
        elif not base:
            result = ("", False)
        else:
            top, ok = chain(base, seen)
            result = (top, True) if ok else ("", False)
        cache[cls] = result
        return result

    classes = set()
    for cls in parents:
        top, ok = chain(cls)
        if not ok or cls == "Card":
            continue
        # Skill cards are never drawn, so they need no art.
        if top in ("SkillCard",):
            continue
        classes.add(cls)
    return classes


def snake_case(name):
    """CamelCase -> snake_case, the usual card objectName spelling."""
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name)
    s = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", "_", s)
    return s.lower()


def collect_card_names(card_classes):
    """Card names as the engine resolves them.

    A card's objectName comes from the class: source files call
    `setObjectName("slash")`, while many equipment cards derive their name
    from the class name itself (class Axe -> "axe" -> image/card/axe.jpg).
    Both spellings are collected, plus the snake_case form that
    image/card/amazing_grace.jpg uses.
    """
    names = defaultdict(list)

    def add(raw, origin):
        if raw and "/" not in raw and "." not in raw:
            names[raw].append(origin)

    for cls in sorted(card_classes):
        origin = "src/*"
        add(cls, origin)
        add(cls.lower(), origin)
        add(snake_case(cls), origin)
        if cls.startswith("H") and len(cls) > 1:
            # Hegemony variants prefix the class name with H (see
            # Card::getClassName, src/core/card.cpp).
            add(cls[1:], origin)
            add(cls[1:].lower(), origin)
            add(snake_case(cls[1:]), origin)

    setname = re.compile(r'setObjectName\s*\(\s*"([^"]+)"\s*\)')
    for f in (ROOT / "src").rglob("*.cpp"):
        text = read_source(f)
        origin = rel(f)
        # Walk each constructor body only; a file-wide sweep would attribute
        # unrelated widget names to whatever class happens to be nearby.
        for m in re.finditer(r"^(\w+)::(\w+)\s*\([^;{]*\)\s*(?::[^{]*)?\{", text, re.MULTILINE):
            if m.group(1) not in card_classes:
                continue
            end = text.find("\n}", m.end())
            body = text[m.end(): end if end > 0 else len(text)]
            for s in setname.finditer(body):
                add(s.group(1), origin)

    # Lua-defined cards: sgs.Create<...>Card(spec) with a name field.
    lua_card = re.compile(r'sgs\.Create\w*Card\s*[({]')
    lua_name = re.compile(r'\bname\s*=\s*"([^"]+)"')
    for f in list((ROOT / "extensions").rglob("*.lua")) + list((ROOT / "lua").rglob("*.lua")):
        text = read_source(f)
        origin = rel(f)
        for m in lua_card.finditer(text):
            window = text[m.end(): m.end() + 400]
            n = lua_name.search(window)
            if n:
                names[n.group(1)].append(origin)
    return names


# --------------------------------------------------------------------------
# asset index
# --------------------------------------------------------------------------

class AssetIndex:
    def __init__(self):
        self.files = []
        self.by_path = {}                 # casefolded path -> real path
        self.by_dir = defaultdict(list)   # casefolded dir -> [names]
        self._scan()
    def _scan(self):
        for root_name in ASSET_ROOTS:
            base = ROOT / root_name
            if not base.is_dir():
                continue
            for f in base.rglob("*"):
                if not f.is_file():
                    continue
                r = rel(f)
                self.files.append(r)
                self.by_path[fold(r)] = r
                self.by_dir[fold(str(Path(r).parent).replace("\\", "/"))].append(f.name)

    def exists(self, reference):
        return fold(reference) in self.by_path

    def prefix_hits(self, reference):
        """Files starting with `reference`.

        A literal such as "audio/card/common/hzg_" is a fragment the engine
        completes at runtime; when any file shares the prefix the reference is
        satisfied and must not be reported missing.
        """
        low = fold(reference)
        return [p for p in self.by_path if p.startswith(low)]

    def dir_names(self, directory):
        return self.by_dir.get(fold(directory), [])


# --------------------------------------------------------------------------
# remote manifest (what the asset pack is supposed to ship)
# --------------------------------------------------------------------------

def load_manifest():
    """Casefolded object keys from the R2 sync manifest, or None."""
    path = ROOT / MANIFEST_PATH
    if not path.is_file():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None
    keys = {fold(e["key"]) for e in data
            if isinstance(e, dict) and isinstance(e.get("key"), str)}
    return keys or None


def remote_tree_index(remote_keys):
    """dir -> {canonical stem: manifest key}, for artwork-direction lookups."""
    trees = defaultdict(dict)
    if not remote_keys:
        return trees
    for k in remote_keys:
        d = str(Path(k).parent).replace("\\", "/")
        trees[d][canon_general(stem_of(k))] = k
    return trees


def audit_sync(index, remote_keys, sample=15):
    """Manifest vs working tree, in both directions."""
    if remote_keys is None:
        return {"available": False}
    unsynced = sorted(k for k in remote_keys if k not in index.by_path)
    unlisted = sorted(p for p in index.files if fold(p) not in remote_keys)

    def group(rows):
        buckets = defaultdict(list)
        for r in rows:
            buckets["/".join(r.split("/")[:2])].append(r)
        return {d: {"count": len(v), "sample": v[:sample]}
                for d, v in sorted(buckets.items(), key=lambda kv: -len(kv[1]))}

    return {
        "available": True,
        "manifest_total": len(remote_keys),
        "unsynced_count": len(unsynced),
        "unsynced_by_dir": group(unsynced),
        "unlisted_count": len(unlisted),
        "unlisted_by_dir": group(unlisted),
    }


# --------------------------------------------------------------------------
# checks
# --------------------------------------------------------------------------

def audit_literals(index, remote_keys=None):
    exact, templated = collect_literals()
    remote = remote_keys or set()
    missing, ok = [], 0
    for ref, hits in sorted(exact.items()):
        if index.exists(ref) or index.prefix_hits(ref):
            ok += 1
            continue
        missing.append({
            "reference": ref,
            "sources": hits[:4],
            "hits": len(hits),
            # In the shipped pack but not downloaded here => a sync gap,
            # not a broken reference.
            "in_manifest": fold(ref) in remote,
        })
    return {
        "checked": len(exact),
        "ok": ok,
        "missing": missing,
        "templated_count": len(templated),
        "templated": [{"reference": r, "sources": h[:2]}
                      for r, h in sorted(templated.items())[:40]],
    }


def _tree_keys(index, directory, exts):
    """Map canonical asset key -> filenames for one flat asset tree."""
    out = defaultdict(list)
    for name in index.dir_names(directory):
        if Path(name).suffix.lower() in exts:
            out[canon_general(stem_of(name))].append(name)
    return out


def audit_generals(index, remote_keys=None):
    raw_ids = collect_general_ids()
    id_keys = defaultdict(set)
    for raw in raw_ids:
        id_keys[canon_general(raw)].add(raw)

    trees = {key: _tree_keys(index, d, e) for key, (d, e) in GENERAL_TREES.items()}
    rtrees = remote_tree_index(remote_keys)

    missing, orphans = [], []
    for key, raws in sorted(id_keys.items()):
        gap = [t for t in GENERAL_REQUIRED if key not in trees[t]]
        if gap:
            # Distinguish "the pack ships this art, we just do not have it"
            # from "no such art exists".
            in_pack = sorted(t for t in gap
                             if key in rtrees.get(fold(GENERAL_TREES[t][0]), {}))
            missing.append({
                "general": sorted(raws)[0],
                "ids": sorted(raws)[:3],
                "missing": gap,
                "in_manifest": in_pack,
                "sources": sorted(raw_ids[sorted(raws)[0]])[:2],
            })
    for tree, (directory, _ext) in GENERAL_TREES.items():
        for asset_key, files in trees[tree].items():
            if asset_key not in id_keys:
                orphans.append({"tree": directory, "key": asset_key,
                                "files": sorted(files)[:6], "count": len(files)})
    return {
        "general_ids": len(raw_ids),
        "general_keys": len(id_keys),
        "missing": sorted(missing, key=lambda x: x["general"]),
        "orphans": sorted(orphans, key=lambda x: (x["tree"], x["key"])),
    }


def audit_skills(index):
    names = collect_skill_names()
    known = {canon_skill(n) for n in names}
    matched, orphans = 0, []
    for name in index.dir_names("audio/skill"):
        if Path(name).suffix.lower() not in AUDIO_EXT:
            continue
        key = canon_skill(stem_of(name))
        if key in known:
            matched += 1
        else:
            orphans.append({"file": name, "key": key})
    return {
        "skill_names": len(names),
        "audio_files": matched + len(orphans),
        "matched": matched,
        "orphans": sorted(orphans, key=lambda x: x["file"]),
    }


def audit_cards(index, card_names):
    """Only the orphan direction is meaningful for card art.

    image/card holds 382 files while several thousand card classes exist, so a
    missing .jpg is the normal case, not a defect: the card overview falls
    back to image/card/unknown.jpg and the in-game hand goes through the skin
    bank. Reporting those would drown the real findings, so the count is kept
    as context and only unused art is listed.
    """
    keys = set()
    for raw in card_names:
        keys.add(fold(raw))
        keys.add(canon_general(raw))

    orphans = []
    names_without_art = 0
    for tree, (directory, exts) in CARD_TREES.items():
        present = defaultdict(list)
        for name in index.dir_names(directory):
            if Path(name).suffix.lower() in exts:
                present[fold(stem_of(name))].append(name)
        for name, files in sorted(present.items()):
            if name not in keys and canon_general(name) not in keys:
                orphans.append({"tree": directory, "name": name,
                                "files": sorted(files), "count": len(files)})
        if tree == "card":
            for raw in card_names:
                if raw.startswith(("__", "#")):
                    continue
                if fold(raw) not in present and canon_general(raw) not in present:
                    names_without_art += 1
    return {
        "card_names": len(card_names),
        "names_without_art": names_without_art,
        "orphans": sorted(orphans, key=lambda x: (x["tree"], x["name"])),
    }


# --------------------------------------------------------------------------
# report
# --------------------------------------------------------------------------

def build_report():
    index = AssetIndex()
    raw_ids = collect_general_ids()
    card_names = collect_card_names(collect_card_classes())
    remote_keys = load_manifest()
    report = {
        "meta": {
            "project_root": str(ROOT),
            "asset_files": len(index.files),
            "asset_bytes": sum((ROOT / p).stat().st_size for p in index.files),
            "manifest": MANIFEST_PATH if remote_keys else None,
        },
        "literals": audit_literals(index, remote_keys),
        "generals": audit_generals(index, remote_keys),
        "skills": audit_skills(index),
        "cards": audit_cards(index, card_names),
        "sync": audit_sync(index, remote_keys),
    }
    report["meta"]["general_id_strings"] = len(raw_ids)
    return index, report


def print_section(title, rows, formatter, limit):
    print(f"\n== {title} ({len(rows)})")
    for row in rows[:limit]:
        print("   " + formatter(row))
    if len(rows) > limit:
        print(f"   ... +{len(rows) - limit} more")


def write_summary(path, report):
    lit, gen, sk, cd = report["literals"], report["generals"], report["skills"], report["cards"]
    L = []
    L.append("# 素材稽核報告 (asset audit)\n")
    L.append(f"- 素材檔案：{report['meta']['asset_files']:,} 個，"
             f"{report['meta']['asset_bytes'] / 1048576:.1f} MB")
    L.append(f"- 武將 id：{gen['general_ids']:,} 筆（正規化後 {gen['general_keys']:,} 個角色）")
    L.append(f"- 技能名參照：{sk['skill_names']:,} 筆")
    L.append(f"- 卡牌 objectName 參照：{cd['card_names']:,} 筆\n")

    L.append("## 缺失 (missing)\n")
    L.append("標記說明：`[未同步]` = 素材包（R2 manifest）有這個檔案、本地尚未下載；"
             "`[真缺失]` = 素材包也沒有，需要補作。\n")
    L.append(f"### 1. 字面引用缺失 — {len(lit['missing'])} 筆"
             f"（已檢查 {lit['checked']:,} 條完整路徑，"
             f"{lit['templated_count']} 條為執行期拼接不列入）\n")
    for r in lit["missing"][:80]:
        tag = "[未同步]" if r.get("in_manifest") else "[真缺失]"
        L.append(f"- {tag} `{r['reference']}` ← {r['sources'][0]}")
    if len(lit["missing"]) > 80:
        L.append(f"- ... 其餘 {len(lit['missing']) - 80} 筆見 report.json")
    L.append("")
    L.append(f"### 2. 武將必要圖缺失 — {len(gen['missing'])} 筆"
             f"（必要項：{', '.join(GENERAL_REQUIRED)}）\n")
    for r in gen["missing"][:80]:
        tag = "[未同步]" if r.get("in_manifest") else "[真缺失]"
        L.append(f"- {tag} `{r['general']}` 缺 {', '.join(r['missing'])}")
    if len(gen["missing"]) > 80:
        L.append(f"- ... 其餘 {len(gen['missing']) - 80} 筆見 report.json")
    L.append("")
    L.append(f"### 3. 卡牌圖 — 不列缺失（{cd['names_without_art']} 個卡牌名無專屬圖）\n")
    L.append("`image/card` 僅 382 張圖，卡牌類別數以千計，缺圖是常態而非缺陷："
             "卡牌總覽會退回 `image/card/unknown.jpg`，對局中手牌走 skin bank。"
             "故此項只列多餘檔案。\n")

    L.append("## 多餘 (orphan)\n")
    L.append(f"### 1. 技能音效對不到技能名 — {len(sk['orphans'])} / {sk['audio_files']} 筆\n")
    for r in sk["orphans"][:80]:
        L.append(f"- `audio/skill/{r['file']}`")
    if len(sk["orphans"]) > 80:
        L.append(f"- ... 其餘 {len(sk['orphans']) - 80} 筆見 report.json")
    L.append("")
    L.append(f"### 2. 武將圖對不到武將 id — {len(gen['orphans'])} 組\n")
    for r in gen["orphans"][:80]:
        L.append(f"- `{r['tree']}/{r['key']}` ({r['count']}: {', '.join(r['files'])})")
    if len(gen["orphans"]) > 80:
        L.append(f"- ... 其餘 {len(gen['orphans']) - 80} 筆見 report.json")
    L.append("")
    L.append(f"### 3. 卡牌圖對不到卡牌名 — {len(cd['orphans'])} 組\n")
    for r in cd["orphans"][:80]:
        L.append(f"- `{r['tree']}/{r['key']}`" if "key" in r else f"- `{r['tree']}/{r['name']}`")
    if len(cd["orphans"]) > 80:
        L.append(f"- ... 其餘 {len(cd['orphans']) - 80} 筆見 report.json")
    L.append("")

    sync = report.get("sync", {})
    if sync.get("available"):
        L.append("## 同步狀態（R2 manifest vs 本地）\n")
        L.append(f"- manifest 條目：{sync['manifest_total']:,}")
        L.append(f"- 本地缺少（未同步）：{sync['unsynced_count']:,}")
        L.append(f"- 本地有但 manifest 未列：{sync['unlisted_count']:,}\n")
        L.append("### 未同步數量 by 目錄\n")
        for d, info in list(sync["unsynced_by_dir"].items())[:20]:
            L.append(f"- `{d}` — {info['count']:,}（例：{', '.join(info['sample'][:3])}）")
        L.append("")
        L.append("### 本地有但 manifest 未列 by 目錄\n")
        for d, info in list(sync["unlisted_by_dir"].items())[:20]:
            L.append(f"- `{d}` — {info['count']:,}（例：{', '.join(info['sample'][:3])}）")
        L.append("")
    path.write_text("\n".join(L), encoding="utf-8")


def main():
    p = argparse.ArgumentParser(description="QSanguosha asset audit (missing / orphan)")
    p.add_argument("-o", "--output", help="output directory for report.json + summary.md")
    p.add_argument("--limit", type=int, default=20, help="rows printed per section")
    args = p.parse_args()

    print("indexing assets...", file=sys.stderr)
    _index, report = build_report()
    m, lit, gen, sk, cd = (report["meta"], report["literals"], report["generals"],
                           report["skills"], report["cards"])
    print(f"  {m['asset_files']} asset files, {m['asset_bytes'] / 1048576:.1f} MB", file=sys.stderr)
    print(f"  literal refs: {lit['checked']} exact ({lit['ok']} ok, {len(lit['missing'])} missing), "
          f"{lit['templated_count']} templated", file=sys.stderr)
    print(f"  generals: {gen['general_ids']} ids -> {len(gen['missing'])} missing required art, "
          f"{len(gen['orphans'])} orphan groups", file=sys.stderr)
    print(f"  skills: {sk['audio_files']} audio files -> {len(sk['orphans'])} orphans", file=sys.stderr)
    print(f"  cards: {cd['card_names']} names ({cd['names_without_art']} without art) -> "
          f"{len(cd['orphans'])} orphans", file=sys.stderr)
    sync = report.get("sync", {})
    if sync.get("available"):
        print(f"  sync: manifest {sync['manifest_total']} keys -> "
              f"{sync['unsynced_count']} not downloaded locally, "
              f"{sync['unlisted_count']} local files unlisted", file=sys.stderr)

    print_section("MISSING: literal references with no file", lit["missing"],
                  lambda r: f"{r['reference']}   <- {r['sources'][0]}", args.limit)
    print_section("MISSING: general required art", gen["missing"],
                  lambda r: f"{r['general']:28s} {','.join(r['missing'])}", args.limit)
    print_section("ORPHAN: skill audio", sk["orphans"],
                  lambda r: r["file"], args.limit)
    print_section("ORPHAN: general art groups", gen["orphans"],
                  lambda r: f"{r['tree']}/{r['key']} ({r['count']})", args.limit)
    print_section("ORPHAN: card art", cd["orphans"],
                  lambda r: f"{r['tree']}/{r['name']} ({r['count']})", args.limit)

    if args.output:
        outdir = Path(args.output)
        if not outdir.is_absolute():
            outdir = ROOT / outdir
        outdir.mkdir(parents=True, exist_ok=True)
        (outdir / "report.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        write_summary(outdir / "summary.md", report)
        print(f"\nwrote {outdir / 'report.json'}\nwrote {outdir / 'summary.md'}", file=sys.stderr)


if __name__ == "__main__":
    main()
