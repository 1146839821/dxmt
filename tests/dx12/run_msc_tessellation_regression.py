#!/usr/bin/env python3
"""Run the MSC line tessellation image control with a paired isolated build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'prefix', 'build', 'converter', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                       for name in ('stage', 'prefix', 'build', 'converter'))
    pairs = [('src/d3d12/d3d12.dll', 'd3d12.dll'), ('src/dxgi/dxgi.dll', 'dxgi.dll'),
             ('src/winemetal/winemetal.dll', 'winemetal.dll'),
             ('src/winemetal/unix/winemetal.so', 'wine/lib/wine/x86_64-unix/winemetal.so')]
    names = ('dx12_command_list_type.exe', 'stage_binding.vs.cso', 'stage_binding.ps.cso',
             'stage_binding_line_image.hs.cso', 'stage_binding_line_image.ds.cso')
    pairs += [('tests/dx12/' + name, name) for name in names]
    hashes = {}
    for source, target in pairs:
        data = (build / source).read_bytes()
        if target in ('d3d12.dll', 'dxgi.dll') and data[64:80] == b'Wine builtin DLL':
            data = data[:64] + b' ' * 16 + data[80:]
        staged = (stage / target).read_bytes()
        if data != staged:
            raise SystemExit('stage/build mismatch: ' + target)
        hashes[target] = hashlib.sha256(staged).hexdigest()
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('DYLD_', 'DXMT_TEST_'))}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / 'wine/lib/wine'),
               WINEDLLOVERRIDES='d3d12,dxgi=n;winemetal=b', WINEDEBUG='+loaddll',
               DXMT_METALIRCONVERTER_PATH=str(converter), DYLD_PRINT_LIBRARIES='1',
               DXMT_SHADER_CACHE_PATH=str(output / 'cache'), DXMT_LOG_PATH=str(output),
               MTL_DEBUG_LAYER='1', MTL_SHADER_VALIDATION='1', MTL_SHADER_VALIDATION_REPORT_TO_STDERR='1')
    for label, mode, expected in (('correct', 'tess-line-image', 0),
                                  ('wrong', 'tess-line-image-wrong', 1)):
        proc = subprocess.run([str(stage / 'wine/bin/wine'), 'dx12_command_list_type.exe',
                               '--stage-bindings', mode], cwd=stage, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        log = proc.stdout.decode(errors='replace')
        (output / (label + '.log')).write_text(log)
        checks = [str(stage / 'wine/lib/wine/x86_64-unix/winemetal.so') in log,
                  str(converter) in log, 'Metal API Validation Enabled' in log,
                  'Metal GPU Validation Enabled' in log,
                  not re.search(r'(?i)validation error|invalid metal usage|failed assertion|command buffer was aborted', log)]
        for dll, kind in (('d3d12.dll', 'native'), ('dxgi.dll', 'native'), ('winemetal.dll', 'builtin')):
            path = ('Z:' + str(stage / dll).replace('/', '\\')).replace('\\', '\\\\').lower()
            checks.append(any(('loaded l"' + path + '"') in line.lower() and (': ' + kind) in line.lower()
                              for line in log.splitlines()))
        checks.append(log.count('exact_bytes=256') == 3 if label == 'correct' else
                      'FAIL stage mode=tess-line-image-wrong pass=0 byte=4' in log)
        if proc.returncode != expected or not all(checks):
            raise SystemExit(f'{label}: exit={proc.returncode}, checks={checks}; see {output / (label + ".log")}')
        print(label, 'PASS', flush=True)
    (output / 'summary.json').write_text(json.dumps({
        'artifacts_sha256': hashes, 'converter_sha256': hashlib.sha256(converter.read_bytes()).hexdigest(),
        'correct': 'PASS', 'wrong_reference_rejected': 'PASS'}, indent=2) + '\n')


if __name__ == '__main__':
    main()
