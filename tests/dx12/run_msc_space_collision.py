#!/usr/bin/env python3
"""Observe ordinary CBV interpretation when its space is reserved by MSC."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('shader', 'converter', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--fetch-shader', required=True, type=Path)
    args = parser.parse_args()
    shader, converter = args.shader.resolve(strict=True), args.converter.resolve(strict=True)
    fetch_shader = args.fetch_shader.resolve(strict=True)
    out = args.output.absolute()
    out.mkdir(parents=True, exist_ok=False)
    cases = {}
    for mode in ('normal', 'special'):
        library, reflection = out / (mode + '.metallib'), out / (mode + '.json')
        command = [str(converter), str(shader), '--output-reflection-file=' + str(reflection), '-o', str(library)]
        if mode == 'special':
            command.append('--function-constant-register-space=2147420894')
        subprocess.run(command, check=True, timeout=120)
        data = json.loads(reflection.read_text())
        if mode == 'normal':
            table = data.get('TopLevelArgumentBuffer', [])
            ok = (not data.get('NeedsFunctionConstants') and data.get('ResourceCount') == 1 and
                  len(table) == 1 and table[0].get('Type') == 'CBV' and
                  table[0].get('Slot') == 1 and table[0].get('Space') == 2147420894)
        else:
            ok = (data.get('NeedsFunctionConstants') and data.get('ResourceCount') == 0 and
                  data.get('FunctionConstants') == [{'name': 'MSC_FC_1', 'type': 32}] and
                  data.get('TopLevelArgumentBuffer') == [])
        if not ok:
            raise SystemExit(f'{mode}: unexpected reflection; see {reflection}')
        cases[mode] = {'command': command, 'metallib_sha256': sha(library), 'reflection_sha256': sha(reflection)}
        print(mode, 'PASS', flush=True)
    for mode in ('normal', 'special'):
        library, reflection = out / ('fetch-' + mode + '.metallib'), out / ('fetch-' + mode + '.json')
        command = [str(converter), str(fetch_shader), '--output-reflection-file=' + str(reflection), '-o', str(library)]
        if mode == 'special':
            command.append('--framebuffer-fetch-register-space=2147420893')
        subprocess.run(command, check=True, timeout=120)
        data = json.loads(reflection.read_text())
        table = data.get('TopLevelArgumentBuffer', [])
        if mode == 'normal':
            ok = (data.get('ResourceCount') == 1 and len(table) == 1 and table[0].get('Type') == 'SRV' and
                  table[0].get('Slot') == 0 and table[0].get('Space') == 2147420893)
        else:
            ok = data.get('ResourceCount') == 0 and data.get('UsedResources') == [] and table == []
        if not ok:
            raise SystemExit(f'fetch-{mode}: unexpected reflection; see {reflection}')
        cases['fetch-' + mode] = {'command': command, 'metallib_sha256': sha(library),
                                  'reflection_sha256': sha(reflection)}
        print('fetch-' + mode, 'PASS', flush=True)
    summary = {'shader_sha256': sha(shader), 'fetch_shader_sha256': sha(fetch_shader), 'converter_sha256': sha(converter),
               'converter_cli_version': subprocess.check_output([str(converter), '--version'], text=True).strip(),
               'runtime_dylib_identity': 'NOT_MEASURED', 'cases': cases}
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
