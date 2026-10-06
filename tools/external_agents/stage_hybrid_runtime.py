"""Stage all declared synthetic-game rules from exact source checkouts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine', required=True, type=Path)
parser.add_argument('--extensions', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
engine, ext, out = args.engine.resolve(), args.extensions.resolve(), args.output.resolve()
if out in (engine, ext) or out in engine.parents or out in ext.parents:
    raise SystemExit('Separate runtime required')
out.mkdir(parents=True, exist_ok=True)
for name in ('lua', 'lang', 'etc'):
    shutil.copytree(engine / name, out / name, dirs_exist_ok=True)
shutil.copytree(ext / 'ai', out / 'lua/ai', dirs_exist_ok=True)
for name in ('lua', 'lang'):
    if (ext / name).is_dir():
        shutil.copytree(ext / name, out / name, dirs_exist_ok=True)
shutil.copytree(ext / 'extensions', out / 'extensions', dirs_exist_ok=True)
config = (engine / 'lua/config.lua').read_text(encoding='utf-8-sig')
start = config.index('\textension_names = {')
end = config.index('\n\t},', start)
entries = re.findall(r'"(extensions/[^";]+\.lua)(?:;[^"\n]*)?"', config[start:end])
missing = [e for e in entries if not (out / e).is_file()]
if missing:
    raise SystemExit('Missing declared rules: ' + str(missing))
(out / 'record').mkdir(exist_ok=True)
files = sorted(p for d in ('lua', 'lang', 'extensions') for p in (out / d).rglob('*.lua'))
manifest = {'engine': subprocess.check_output(['git', '-C', str(engine), 'rev-parse', 'HEAD'], text=True).strip(),
    'extensions': subprocess.check_output(['git', '-C', str(ext), 'rev-parse', 'HEAD'], text=True).strip(),
    'declared_extension_entries': entries, 'missing': missing,
    'rules_sha256': {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
(out / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps({'runtime': str(out), 'declared_extensions': len(entries), 'lua_files': len(files), 'missing': missing}))
