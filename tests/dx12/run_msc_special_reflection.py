#!/usr/bin/env python3
"""Compare MSC reflection for special HLSL resources with and without a root table."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'converter', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    build, converter = args.build.resolve(strict=True), args.converter.resolve(strict=True)
    out = args.output.absolute()
    out.mkdir(parents=True, exist_ok=False)
    version = subprocess.check_output([str(converter), '--version'], text=True).strip()
    records = {}
    for mode, fixture in (('linear', 'metal_hlsl_combined.ps.cso'),
                          ('root', 'metal_hlsl_combined_root.ps.cso')):
        dxil = build / 'tests/dx12' / fixture
        library, reflection = out / (mode + '.metallib'), out / (mode + '.json')
        cmd = [str(converter), str(dxil), '--function-constant-register-space=2147420894',
               '--framebuffer-fetch-register-space=2147420893',
               '--output-reflection-file=' + str(reflection), '-o', str(library)]
        subprocess.run(cmd, check=True, timeout=120)
        data = json.loads(reflection.read_text())
        if (data.get('ResourceCount') != 0 or data.get('UsedResources') != [] or
                not data.get('NeedsFunctionConstants') or
                data.get('FunctionConstants') != [{'name': 'MSC_FC_0', 'type': 32}]):
            raise SystemExit(f'{mode}: unexpected reflection; see {reflection}')
        table = data.get('TopLevelArgumentBuffer')
        if not isinstance(table, list) or (mode == 'linear' and table):
            raise SystemExit(f'{mode}: unexpected top-level layout; see {reflection}')
        if mode == 'root' and (len(table) != 1 or table[0].get('Type') != 'Table'):
            raise SystemExit('root: expected one descriptor table')
        records[mode] = {'dxil_sha256': sha(dxil), 'metallib_sha256': sha(library),
                         'reflection_sha256': sha(reflection), 'top_level_argument_buffer': table,
                         'command': cmd}
        print(mode, 'PASS', flush=True)
    (out / 'summary.json').write_text(json.dumps({'converter_sha256': sha(converter),
        'converter_cli_version': version, 'runtime_dylib_identity': 'NOT_MEASURED',
        'scope': 'offline CLI reflection; not D3D12 descriptor binding', 'cases': records}, indent=2) + '\n')


if __name__ == '__main__':
    main()
