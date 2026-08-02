#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Warrior Asset Checker for QSanguosha-v2.

Scans Lua translation files and asset directories to cross-reference
warriors with their image/audio files. Reports missing or orphaned assets.

Usage:
  python warrior_asset_checker.py                     # JSON report to stdout
  python warrior_asset_checker.py -o report.json      # Save to file
  python warrior_asset_checker.py --format csv        # CSV format
  python warrior_asset_checker.py --missing-only      # Only warriors with missing assets
  python warrior_asset_checker.py --extra-only        # Only orphaned asset files
  python warrior_asset_checker.py --list              # Brief warrior ID list
  python warrior_asset_checker.py --warrior caocao    # Single warrior detail
"""
import os, re, json, argparse, sys
from pathlib import Path
from collections import defaultdict

# Handle Windows console encoding
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

PROJECT_ROOT = Path(os.path.dirname(os.path.abspath(__file__))).parent
PACKAGE_DIR  = PROJECT_ROOT / "lang" / "zh_CN" / "Package"
EXT_DIR      = PROJECT_ROOT / "extensions"
CONFIG_LUA   = PROJECT_ROOT / "lua" / "config.lua"

ASSET_DIRS = {
    "card": "image/generals/card",
    "full": "image/fullskin/generals/full",
    "dual": "image/fullskin/generals/fulldual",
    "compact": "image/compact/generals/small",
    "large": "image/large",
    "death": "audio/death",
    "skill": "audio/skill",
    "win": "audio/win",
}
IMG_EXTS = {".jpg", ".jpeg", ".png", ".webp", ".bmp"}
AUD_EXTS = {".ogg", ".mp3", ".wav"}
REQUIRED = {"card", "full", "death"}

def strip_comments(text):
    """Remove Lua comments (line and block)."""
    text = re.sub(r"--\[\[.*?\]\]--", "", text, flags=re.DOTALL)
    text = re.sub(r"--.*$", "", text, flags=re.MULTILINE)
    return text


def parse_lua_dict(content):
    """Parse a Lua return { [\"key\"] = \"value\", ... } table."""
    result = {}
    content = strip_comments(content)
    pat = re.compile(r"""\["([^"]+)"\]\s*=\s*(?:\[\[(.*?)\]\]|"((?:[^"\\]|\.)*)"|\'([^\']*)\'|([^,\n}]+))""", re.DOTALL)
    for m in pat.finditer(content):
        key = m.group(1)
        val = next((v for v in m.groups()[1:] if v is not None), "").strip()
        val = val.replace('\\"', '"').replace("\'", "'").replace("\n", "\n")

        result[key] = val
    return result


def parse_package_names(config_path):
    """Extract known package names from config.lua."""
    if not config_path.exists():
        return set()
    text = strip_comments(config_path.read_text(encoding="utf-8"))
    m = re.search(r"package_names\s*=\s*\{(.*?)\}", text, re.DOTALL)
    if not m:
        return set()
    return {s.strip("\"' ") for s in re.findall(r'"([^"]+)"', m.group(1))}


def extract_warriors():
    """Extract all warrior definitions from Lua translation files."""
    warriors = {}
    known_pkgs = parse_package_names(CONFIG_LUA)

    for lua_file in sorted(PACKAGE_DIR.glob("*.lua")):
        try:
            tbl = parse_lua_dict(lua_file.read_text(encoding="utf-8"))
        except Exception:
            continue
        pkg = lua_file.stem
        file_skills = {k[1:] for k in tbl if k.startswith(":") and not k.startswith("::")}

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
                    illustrator=None, designer=None, cv=None, files=[],
                )
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

    # Supplement from extension Lua files
    ext_pat = re.compile(r"""(\w+)\s*=\s*sgs\.General\s*\(\s*extension\s*,\s*"([^"]+)"\s*,\s*"(\w+)"\s*,\s*(\d+)""")
    for ext_file in sorted(EXT_DIR.glob("*.lua")):
        try:
            text = strip_comments(ext_file.read_text(encoding="utf-8"))
        except Exception:
            continue
        for m in ext_pat.finditer(text):
            wid = m.group(2)
            if wid not in warriors:
                warriors[wid] = dict(
                    id=wid, name=wid, title=None, short_name=None,
                    illustrator=None, designer=None, cv=None, files=[],
                )
            w = warriors[wid]
            w["kingdom"] = m.group(3)
            w["hp"] = int(m.group(4))
            if ext_file.stem not in w["files"]:
                w["files"].append(ext_file.stem)

    return warriors


def normalize_general_id(raw):
    """Normalize a filename stem to its base warrior ID."""
    s = re.sub(r"\.[^.]+$", "", raw)
    for suffix in ("_Plus", "_Four", "_six", "_Seven", "_Five", "_Two", "_Three", "_Eight", "_1", "_2", "_3", "_extra", "_nw"):
        if suffix in s:
            s = s[:s.index(suffix)]
            break
    s = re.sub(r"_skin\d+$", "", s)
    return s


def scan_assets():
    """Scan all asset directories, group files by base warrior ID."""
    by_id = defaultdict(lambda: defaultdict(list))
    for atype, reldir in ASSET_DIRS.items():
        d = PROJECT_ROOT / reldir
        if not d.is_dir():
            continue
        exts = AUD_EXTS if atype in ("death", "skill", "win") else IMG_EXTS
        for fp in sorted(d.iterdir()):
            if not fp.is_file():
                continue
            if fp.suffix.lower() not in exts:
                continue
            base = normalize_general_id(fp.stem)
            by_id[base][atype].append(fp.name)
    return dict(by_id)


def scan_audio_by_skill(warriors):
    """Match skill audio files to warriors by skill name."""
    warrior_audio = defaultdict(list)
    unmatched = []
    skill_to_wid = {}
    for wid, w in warriors.items():
        for sk in w.get("skills", []):
            skill_to_wid[sk] = wid
    d = PROJECT_ROOT / "audio" / "skill"
    if not d.is_dir():
        return dict(warrior_audio), unmatched
    for fp in sorted(d.iterdir()):
        if not fp.is_file() or fp.suffix.lower() not in AUD_EXTS:
            continue
        stem = fp.stem
        base = re.sub(r"\d+$", "", stem)
        if base in skill_to_wid:
            warrior_audio[skill_to_wid[base]].append(fp.name)
        else:
            unmatched.append(fp.name)
    return dict(warrior_audio), unmatched


def _try_asset_lookup(wid, assets_by_id):
    """Try multiple ID forms when looking up assets for a warrior."""
    # Try exact match first
    if wid in assets_by_id:
        return assets_by_id[wid]
    # Try without trailing $
    if wid.endswith("$") and wid[:-1] in assets_by_id:
        return assets_by_id[wid[:-1]]
    # Try normalized form (like how assets are indexed)
    normalized = normalize_general_id(wid.rstrip("$"))
    if normalized in assets_by_id:
        return assets_by_id[normalized]
    # Try lowercased
    lowered = wid.lower().rstrip("$")
    if lowered in assets_by_id:
        return assets_by_id[lowered]
    return {}


def cross_reference(warriors, assets_by_id, skill_audio, unmatched_skill):
    """Cross-reference warrior definitions with scanned assets."""
    results = {
        "meta": {
            "project_root": str(PROJECT_ROOT),
            "total_warriors": len(warriors),
            "total_asset_files": sum(
                len(files) for a in assets_by_id.values() for files in a.values()
            ),
            "unmatched_skill_audio": len(unmatched_skill),
        },
        "warriors": [],
        "extra_assets": [],
    }
    used_files = {a: set() for a in ASSET_DIRS}

    for wid in sorted(warriors.keys()):
        w = warriors[wid]
        assets = _try_asset_lookup(wid, assets_by_id)
        entry = {
            "id": wid,
            "name": w.get("name") or wid,
            "title": w.get("title"),
            "kingdom": w.get("kingdom"),
            "hp": w.get("hp"),
            "illustrator": w.get("illustrator"),
            "designer": w.get("designer"),
            "cv": w.get("cv"),
            "packages": w.get("files", []),
            "assets": {},
            "missing": [],
            "status": "ok",
        }
        all_ok = True
        for atype in ASSET_DIRS:
            files = sorted(assets.get(atype, []))
            entry["assets"][atype] = files
            for f in files:
                used_files[atype].add(f)
            if atype in REQUIRED and not files:
                entry["missing"].append(atype)
                all_ok = False
        sa = sorted(skill_audio.get(wid, []))
        entry["assets"]["skill"] = sa
        for f in sa:
            used_files["skill"].add(f)
        if not all_ok:
            entry["status"] = "missing"
        results["warriors"].append(entry)

    # Orphan assets: files not referenced by any warrior
    for atype, reldir in ASSET_DIRS.items():
        d = PROJECT_ROOT / reldir
        if not d.is_dir():
            continue
        exts = AUD_EXTS if atype in ("death", "skill", "win") else IMG_EXTS
        for fp in sorted(d.iterdir()):
            if not fp.is_file() or fp.suffix.lower() not in exts:
                continue
            if fp.name not in used_files[atype]:
                base = normalize_general_id(fp.stem)
                results["extra_assets"].append({
                    "type": atype,
                    "file": fp.name,
                    "path": str(fp.relative_to(PROJECT_ROOT)),
                    "base_id": base,
                })

    ok_count = sum(1 for w in results["warriors"] if w["status"] == "ok")
    missing_count = sum(1 for w in results["warriors"] if w["status"] == "missing")
    results["meta"].update({
        "warriors_ok": ok_count,
        "warriors_missing": missing_count,
        "extra_asset_count": len(results["extra_assets"]),
    })
    return results


def output_json(data, pretty=True):
    return json.dumps(data, ensure_ascii=False, indent=2 if pretty else None)


def output_csv_warriors(results):
    h = ["id", "name", "title", "kingdom", "hp", "illustrator", "packages",
         "card_img", "full_img", "death_audio", "skill_audio_count", "status"]
    lines = [",".join(h)]
    for w in results["warriors"]:
        a = w["assets"]
        lines.append(",".join([
            w["id"],
            w["name"] or "",
            w["title"] or "",
            w.get("kingdom", "") or "",
            str(w.get("hp", "")),
            (w.get("illustrator") or "").replace(",", ";"),
            ";".join(w.get("packages", [])),
            str(len(a.get("card", []))),
            str(len(a.get("full", []))),
            str(len(a.get("death", []))),
            str(len(a.get("skill", []))),
            w["status"],
        ]))
    return "\n".join(lines)


def output_csv_extra(results):
    lines = ["type,file,base_id"]
    for e in results["extra_assets"]:
        lines.append(e["type"] + "," + e["file"] + "," + e["base_id"])
    return "\n".join(lines)


def output_list(warriors):
    lines = []
    for wid in sorted(warriors.keys()):
        w = warriors[wid]
        pkgs = ", ".join(w.get("files", []))
        name = w.get("name") or "(none)"
        title = w.get("title") or ""
        lines.append(f"{wid:45s} {name:25s} {title:15s} [{pkgs}]")
    return "\n".join(lines)


def main():
    p = argparse.ArgumentParser(
        description="QSanguosha Warrior Asset Checker",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""Examples:
  python warrior_asset_checker.py                     # Full JSON report to stdout
  python warrior_asset_checker.py -o report.json      # Save to file
  python warrior_asset_checker.py --format csv        # CSV with asset counts per warrior
  python warrior_asset_checker.py --missing-only      # Only warriors lacking required assets
  python warrior_asset_checker.py --extra-only        # Only orphan asset files
  python warrior_asset_checker.py --list              # Simple warrior ID listing
  python warrior_asset_checker.py --warrior caocao    # Single warrior asset detail""")
    p.add_argument("-o", "--output", help="Output file path (default: stdout)")
    p.add_argument("-f", "--format", choices=("json", "csv"), default="json",
                   help="Output format")
    p.add_argument("--missing-only", action="store_true",
                   help="Show only warriors missing required assets")
    p.add_argument("--extra-only", action="store_true",
                   help="Show only orphan asset files")
    p.add_argument("--list", action="store_true",
                   help="Print brief warrior ID and name list")
    p.add_argument("--warrior", type=str,
                   help="Show all assets for a specific warrior ID")
    args = p.parse_args()

    print("Parsing warrior definitions...", file=sys.stderr)
    warriors = extract_warriors()
    print(f"  {len(warriors)} warriors found", file=sys.stderr)

    if args.list:
        print(output_list(warriors))
        return

    if args.warrior:
        wid = args.warrior
        w = warriors.get(wid)
        if not w:
            print(f"ERROR: warrior '{wid}' not found", file=sys.stderr)
            sys.exit(1)
        assets_by_id = scan_assets()
        skill_audio, _ = scan_audio_by_skill(warriors)
        warrior_assets = _try_asset_lookup(wid, assets_by_id)
        print(f"Warrior: {w['name'] or wid}  ({wid})")
        print(f"Title:   {w.get('title') or 'N/A'}")
        print(f"Kingdom: {w.get('kingdom') or 'N/A'}   HP: {w.get('hp') or 'N/A'}")
        print(f"Illustrator: {w.get('illustrator') or 'N/A'}")
        print(f"Source files: {', '.join(w.get('files', []))}")
        print()
        for atype, label in [
            ("card", "Card image"), ("full", "Full image"),
            ("dual", "Dual-general"), ("compact", "Compact icon"),
            ("large", "Large image"), ("death", "Death audio"),
            ("win", "Win audio"), ("skill", "Skill audio"),
        ]:
            files = warrior_assets.get(atype, [])
            if atype == "skill":
                files = skill_audio.get(wid, [])
            mark = "OK  " if files else "MISS"
            print(f"  [{mark}] {label}: {len(files)} files")
            for fn in files[:8]:
                print(f"         {fn}")
            if len(files) > 8:
                print(f"         ... +{len(files) - 8} more")
        return

    print("Scanning asset directories...", file=sys.stderr)
    assets_by_id = scan_assets()
    skill_audio, unmatched = scan_audio_by_skill(warriors)
    total = sum(len(vv) for v in assets_by_id.values() for vv in v.values())
    print(f"  {total} asset files found, {len(unmatched)} unmatched skill audio", file=sys.stderr)

    print("Cross-referencing...", file=sys.stderr)
    results = cross_reference(warriors, assets_by_id, skill_audio, unmatched)
    m = results["meta"]
    print(f"  OK: {m['warriors_ok']}  Missing: {m['warriors_missing']}"
          f"  Orphans: {m['extra_asset_count']}", file=sys.stderr)

    if args.missing_only:
        results["warriors"] = [w for w in results["warriors"] if w["status"] == "missing"]
        results["extra_assets"] = []
    elif args.extra_only:
        results["warriors"] = []

    if args.format == "csv":
        out = output_csv_extra(results) if args.extra_only else output_csv_warriors(results)
    else:
        out = output_json(results)

    if args.output:
        Path(args.output).write_text(out, encoding="utf-8")
        print(f"Saved to {args.output}", file=sys.stderr)
    else:
        print(out)


if __name__ == "__main__":
    main()
