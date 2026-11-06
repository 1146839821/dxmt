#!/usr/bin/env python3
"""Measure command-buffer GPU duration of equivalent tiny fetch/multipass draws."""
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
        parser.error('processes must be between 2 and 20')
    stage, prefix, build, converter = (getattr(args, n).resolve(strict=True) for n in ('stage', 'prefix', 'build', 'converter'))
    pairs = [('src/winemetal/winemetal.dll', 'winemetal.dll'),
             ('src/winemetal/unix/winemetal.so', 'wine/lib/wine/x86_64-unix/winemetal.so')]
    pairs += [('tests/dx12/' + n, n) for n in ('dx12_metal_hlsl_gpu.exe', 'metal_hlsl_constants.ps.cso', 'metal_hlsl_framebuffer.ps.cso')]
    for src, dst in pairs:
        if (build / src).read_bytes() != (stage / dst).read_bytes():
            raise SystemExit('stage/build mismatch: ' + dst)
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('DYLD_', 'DXMT_TEST_', 'MTL_DEBUG_', 'MTL_SHADER_VALIDATION'))}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / 'wine/lib/wine'), WINEDLLOVERRIDES='winemetal=b',
               WINEDEBUG='+loaddll', DXMT_METALIRCONVERTER_PATH=str(converter), DYLD_PRINT_LIBRARIES='1')
    cmd = [str(stage / 'wine/bin/wine'), 'dx12_metal_hlsl_gpu.exe', 'metal_hlsl_constants.ps.cso', 'metal_hlsl_framebuffer.ps.cso', '--benchmark']
    identity = 'LOADED_CODE winemetal.dll DXMTMSCCompileDXIL ' + pe_export_bytes(stage / 'winemetal.dll', 'DXMTMSCCompileDXIL')
    dll_path = ('Z:' + str(stage / 'winemetal.dll').replace('/', '\\')).replace('\\', '\\\\').lower()
    samples = []
    for i in range(args.processes):
        result = subprocess.run(cmd, cwd=stage, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=180, text=True, errors='replace')
        log = result.stdout
        (output / f'run-{i}.log').write_text(log)
        rows = [(int(a), int(b), int(c)) for a, b, c in re.findall(r'^GPU_NS (\d+) (\d+) (\d+)\s*$', log, re.M)]
        checks = [result.returncode == 0, identity in log, str(converter) in log,
                  str(stage / 'wine/lib/wine/x86_64-unix/winemetal.so') in log,
                  any(('loaded l"' + dll_path + '"') in line.lower() and ': builtin' in line.lower() for line in log.splitlines()),
                  [n for n, _, _ in rows] == list(range(25)),
                  all(a > 0 and b > 0 for _, a, b in rows),
                  log.count('MULTIPASS_PIXEL 64,64,0,128') == 26,
                  'Metal HLSL function constants and framebuffer fetch GPU passed' in log,
                  'Metal GPU Validation Enabled' not in log, 'Metal API Validation Enabled' not in log]
        if not all(checks):
            raise SystemExit(f'run {i} failed checks {checks}; see {output}/run-{i}.log')
        samples.append(rows)
        print(f'process {i}: 25 GPU timing pairs passed', flush=True)
    def stats(values):
        values = sorted(values)
        return {'count': len(values), 'median_ns': statistics.median(values),
                'p95_ns': values[math.ceil(.95 * len(values))-1], 'min_ns': values[0], 'max_ns': values[-1]}
    # Discard first five pairs per process as warm-up; retain raw data.
    report = {'samples': samples, 'single_pass': stats([a for run in samples for _, a, b in run[5:]]),
              'two_pass': stats([b for run in samples for _, a, b in run[5:]]),
              'converter_sha256': hashlib.sha256(converter.read_bytes()).hexdigest(),
              'artifacts': {dst: hashlib.sha256((stage / dst).read_bytes()).hexdigest() for _, dst in pairs},
              'hardware': subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
              'macos': subprocess.check_output(['sw_vers', '-productVersion'], text=True).strip(), 'command': cmd,
              'wine': subprocess.check_output([str(stage / 'wine/bin/wine'), '--version'], text=True,
                                              env={**os.environ, 'WINEPREFIX': str(prefix)}, stderr=subprocess.DEVNULL).strip(),
              'git_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True,
                                                  cwd=Path(__file__).resolve().parents[2]).strip(),
              'limitations': '4x4 RGBA8, command-buffer GPU duration, two draws, CPU readback after each sample; not frame time, bandwidth or fragment-only time; validation disabled'}
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    (output / 'summary.md').write_text('# GPU command-buffer duration\n\n' + report['limitations'] + '\n\n' +
        '| Case | n | median ns | p95 ns | min ns | max ns |\n| --- | ---: | ---: | ---: | ---: | ---: |\n' +
        ''.join(f"| {n} | {v['count']} | {v['median_ns']} | {v['p95_ns']} | {v['min_ns']} | {v['max_ns']} |\n" for n, v in [('single pass', report['single_pass']), ('two pass', report['two_pass'])]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
