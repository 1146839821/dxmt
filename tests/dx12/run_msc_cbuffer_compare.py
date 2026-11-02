#!/usr/bin/env python3
"""Compare tiny dynamic constants, branch, and specialization with pixel checks."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

sys.dont_write_bytecode = True
from run_msc_lod_experiment import pe_export_bytes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'prefix', 'build', 'converter', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--processes', type=int, default=5)
    args = parser.parse_args()
    if not 2 <= args.processes <= 20:
        parser.error('processes must be 2..20')
    stage, prefix, build, converter = (getattr(args, n).resolve(strict=True) for n in ('stage', 'prefix', 'build', 'converter'))
    pairs = [('src/d3d12/d3d12.dll', 'd3d12.dll'), ('src/dxgi/dxgi.dll', 'dxgi.dll'),
             ('src/winemetal/winemetal.dll', 'winemetal.dll'), ('src/winemetal/unix/winemetal.so', 'wine/lib/wine/x86_64-unix/winemetal.so')]
    pairs += [('tests/dx12/' + n, n) for n in ('dx12_msc_cbuffer_compare.exe', 'metal_hlsl_dynamic_main.ps.cso',
              'metal_hlsl_dynamic_branch_main.ps.cso', 'metal_hlsl_constants.ps.cso')]
    for src, dst in pairs:
        data = (build / src).read_bytes()
        if src in ('src/d3d12/d3d12.dll', 'src/dxgi/dxgi.dll') and data[64:80] == b'Wine builtin DLL':
            data = data[:64] + b' ' * 16 + data[80:]
        if data != (stage / dst).read_bytes():
            raise SystemExit('stage/build mismatch: ' + dst)
    output = args.output.absolute(); output.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('DYLD_', 'DXMT_TEST_', 'MTL_DEBUG_', 'MTL_SHADER_VALIDATION'))}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / 'wine/lib/wine'), WINEDLLOVERRIDES='d3d12=n;dxgi=n;winemetal=b',
               WINEDEBUG='+loaddll', DXMT_METALIRCONVERTER_PATH=str(converter), DYLD_PRINT_LIBRARIES='1')
    cmd = [str(stage / 'wine/bin/wine'), 'dx12_msc_cbuffer_compare.exe', 'metal_hlsl_dynamic_main.ps.cso',
           'metal_hlsl_dynamic_branch_main.ps.cso', 'metal_hlsl_constants.ps.cso']
    identity = [f'LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}' for dll, symbol in
                [('d3d12.dll', 'D3D12CreateDevice'), ('winemetal.dll', 'DXMTMSCCompileDXIL')]]
    samples = []
    cases = [(f'run-{i}', False, False) for i in range(args.processes)] + [('validation', True, False), ('wrong', True, True)]
    for name, validation, wrong in cases:
        runenv = dict(env)
        if validation:
            runenv.update(MTL_DEBUG_LAYER='1', MTL_SHADER_VALIDATION='1', MTL_SHADER_VALIDATION_REPORT_TO_STDERR='1')
        run = subprocess.run(cmd + (['--wrong'] if wrong else []), cwd=stage, env=runenv,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180, text=True, errors='replace')
        log = run.stdout; (output / f'{name}.log').write_text(log)
        expected_pixels = [f'CBUFFER_PIXEL {mode} {i} {color}' for mode in range(3) for i, color in
                           enumerate(('255,0,0,255', '0,255,0,255'))]
        if wrong: expected_pixels = expected_pixels[:4]
        rows = re.findall(r'^(FUNCTION_MS|PIPELINE_MS|GPU_NS) (\d+) (\d+) ([0-9.eE+-]+)\s*$', log, re.M)
        paths = []
        for dll, kind in [('d3d12.dll','native'), ('dxgi.dll','native'), ('winemetal.dll','builtin')]:
            path = ('Z:' + str(stage / dll).replace('/', '\\')).replace('\\', '\\\\').lower()
            paths.append(any(('loaded l"' + path + '"') in line.lower() and ': ' + kind in line.lower() for line in log.splitlines()))
        checks = [run.returncode == (14 if wrong else 0), all(s in log for s in identity), all(paths),
                  str(converter) in log, str(stage / 'wine/lib/wine/x86_64-unix/winemetal.so') in log,
                  all(s in log for s in expected_pixels), len(rows) == (15 if wrong else 18),
                  not re.search(r'(?i)metal (?:api|gpu|shader) validation (?:error|warning)|failed assertion', log)]
        if validation: checks += ['Metal API Validation Enabled' in log, 'Metal GPU Validation Enabled' in log]
        else: checks += ['Metal API Validation Enabled' not in log, 'Metal GPU Validation Enabled' not in log]
        if not all(checks): raise SystemExit(f'{name}: failed {checks}; exit={run.returncode}, see {output}/{name}.log')
        if name.startswith('run-'): samples.append(rows)
        print(name + ': passed', flush=True)
    summaries = {}
    for phase in ('FUNCTION_MS', 'PIPELINE_MS', 'GPU_NS'):
        for mode in range(3):
            for index in range(2):
                v = sorted(float(value) for run in samples for kind, m, i, value in run if kind == phase and int(m) == mode and int(i) == index)
                summaries[f'{phase}_{mode}_{index}'] = {'n':len(v), 'median':statistics.median(v),
                    'p95':v[math.ceil(.95*len(v))-1], 'min':v[0], 'max':v[-1]}
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    result = {'samples':samples, 'summaries':summaries, 'command':cmd, 'converter_sha256':sha(converter),
              'artifacts':{dst:sha(stage / dst) for _, dst in pairs},
              'hardware':subprocess.check_output(['sysctl','-n','machdep.cpu.brand_string'],text=True).strip(),
              'macos':subprocess.check_output(['sw_vers','-productVersion'],text=True).strip(),
              'limitations':'4x4 pixels, one fixed mode order, one pipeline per value, no reuse/cache analysis, no frame time or memory; validation disabled for samples'}
    (output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    (output / 'summary.md').write_text('# MSC constant comparison\n\n' + result['limitations'] + '\n\n' +
      '| metric_mode_value | n | median | p95 | min | max |\n| --- | ---: | ---: | ---: | ---: | ---: |\n' +
      ''.join(f"| {k} | {v['n']} | {v['median']} | {v['p95']} | {v['min']} | {v['max']} |\n" for k,v in summaries.items()))
    return 0

if __name__ == '__main__':
    sys.exit(main())
