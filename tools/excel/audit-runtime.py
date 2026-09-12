"""Audit a staged x64 trial's imports and CLI loading without developer PATH."""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--dumpbin', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    system = Path(os.environ['SystemRoot']) / 'System32'
    report = {'scope': 'PE imports and --help only; no Engine/game/VBA acceptance',
              'missing': [], 'wrong_machine': [], 'cli': [], 'pe_files': 0}
    binaries = sorted(p for p in root.rglob('*') if p.suffix.lower() in ('.exe', '.dll'))
    for binary in binaries:
        raw = binary.read_bytes()
        if raw[:2] != b'MZ':
            raise RuntimeError('invalid PE: ' + str(binary))
        offset = struct.unpack_from('<I', raw, 0x3c)[0]
        if raw[offset:offset + 4] != b'PE\0\0':
            raise RuntimeError('invalid PE header: ' + str(binary))
        machine = struct.unpack_from('<H', raw, offset + 4)[0]
        if machine != 0x8664:
            report['wrong_machine'].append(binary.relative_to(root).as_posix())
        result = subprocess.run([str(args.dumpbin), '/dependents', str(binary)],
                                capture_output=True, timeout=15, check=True)
        for name in re.findall(r'^\s+([\w.+-]+\.dll)\s*$', result.stdout.decode(errors='replace'), re.M | re.I):
            # API-set contracts are resolved by Windows, not physical DLL files.
            if name.lower().startswith(('api-ms-', 'ext-ms-')):
                continue
            if not any((directory / name).is_file() for directory in (binary.parent, root, system)):
                report['missing'].append({'binary': binary.relative_to(root).as_posix(), 'dependency': name})
        report['pe_files'] += 1
    env = {k.upper(): v for k, v in os.environ.items()}
    env['PATH'] = str(system) + ';' + str(system.parent)
    for key in list(env):
        if key.startswith(('QT_', 'QML', 'LUA_')) or key in ('QTDIR', 'QSAN_SESSION_SETTINGS', 'QSAN_USER_DATA_ROOT'):
            env.pop(key, None)
    with tempfile.TemporaryDirectory(prefix='excel-release-cli-') as temporary:
        env['QSAN_SESSION_SETTINGS'] = str(Path(temporary) / 'config.ini')
        env['QSAN_USER_DATA_ROOT'] = str(Path(temporary) / 'data')
        for name in ('QSanguoshaExcelBridge.exe', 'QSanguoshaExcelServer.exe'):
            result = subprocess.run([str(root / name), '--help'], cwd=temporary,
                                    env=env, timeout=20, capture_output=True,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            output = result.stdout + result.stderr
            report['cli'].append({'binary': name, 'exit_code': result.returncode,
                                  'help_present': b'Usage:' in output})
    report['passed'] = not report['missing'] and not report['wrong_machine'] and all(
        row['exit_code'] == 0 and row['help_present'] for row in report['cli'])
    target = root / 'diagnostics'
    target.mkdir(exist_ok=True)
    (target / 'runtime-audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
