#!/usr/bin/env python3
"""Stage a small, explicit native-role fixture with the real companion SmartAI.

Optional Lua expansion packages are not part of this boundary fixture. The source
checkout and its complete extension manifest are never modified.
"""
import argparse
from pathlib import Path
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('--engine', required=True, type=Path)
parser.add_argument('--extensions', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
engine, companion, output = args.engine.resolve(), args.extensions.resolve(), args.output.resolve()
if output == engine or output == companion or output in engine.parents or output in companion.parents:
    raise SystemExit('Output must be a separate fixture directory')
if not (companion / 'ai/smart-ai.lua').is_file() or not (companion / 'extensions/addFunction.lua').is_file():
    raise SystemExit('The extensions companion checkout is required')
output.mkdir(parents=True, exist_ok=True)
(output / 'record').mkdir(exist_ok=True)
for directory in ('lua', 'lang', 'etc'):
    shutil.copytree(engine / directory, output / directory, dirs_exist_ok=True)
shutil.copytree(companion / 'ai', output / 'lua/ai', dirs_exist_ok=True)
for source in (companion / 'lua').glob('*.lua'):
    shutil.copy2(source, output / 'lua' / source.name)
(output / 'extensions').mkdir(exist_ok=True)
shutil.copy2(companion / 'extensions/addFunction.lua', output / 'extensions/addFunction.lua')
config = engine.joinpath('lua/config.lua').read_text(encoding='utf-8-sig')
start = config.index('\textension_names = {')
end = config.index('\n\t},', start) + len('\n\t},')
config = config[:start] + '\textension_names = { "extensions/addFunction.lua" },' + config[end:]
output.joinpath('lua/config.lua').write_text(config, encoding='utf-8')
print('Staged native-role fixture with companion SmartAI and addFunction:', output)
