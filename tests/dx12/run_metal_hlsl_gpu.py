#!/usr/bin/env python3
"""Run the opt-in Metal HLSL pixel probe in an already prepared isolated stage."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import struct
import math
import subprocess
import sys

sys.dont_write_bytecode = True
from run_msc_lod_experiment import pe_export_bytes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'prefix', 'build', 'converter', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--trace-config', action='store_true', help='trace real compatibility flags through a forwarding dylib')
    parser.add_argument('--trace-dxil-validation', action='store_true', help='enable DXIL validation through a test wrapper')
    parser.add_argument('--both-spaces', action='store_true', help='enable both distinct extension spaces')
    parser.add_argument('--combined', action='store_true', help='use one shader containing both extensions')
    parser.add_argument('--combined-root', action='store_true', help='use combined shader with embedded root signature')
    parser.add_argument('--combined-explicit-root', action='store_true', help='pass the root-containing DXIL as explicit root blob')
    args = parser.parse_args()
    if args.trace_config and args.trace_dxil_validation:
        parser.error('trace options are mutually exclusive')
    if sum((args.combined, args.combined_root, args.combined_explicit_root, args.both_spaces)) > 1:
        parser.error('combined, combined-root and both-spaces are separate modes')
    combined = args.combined or args.combined_root or args.combined_explicit_root
    if not 1 <= args.repeats <= 20:
        parser.error('--repeats must be in 1..20')
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                        for name in ('stage', 'prefix', 'build', 'converter'))
    pairs = [('src/winemetal/winemetal.dll', 'winemetal.dll'),
             ('src/winemetal/unix/winemetal.so', 'wine/lib/wine/x86_64-unix/winemetal.so')]
    fragment = ('metal_hlsl_combined_root.ps.cso' if args.combined_root or args.combined_explicit_root else
                'metal_hlsl_combined.ps.cso' if args.combined else 'metal_hlsl_framebuffer.ps.cso')
    pairs += [('tests/dx12/' + name, name) for name in
              ('dx12_metal_hlsl_gpu.exe', 'metal_hlsl_constants.ps.cso', fragment)]
    for source, target in pairs:
        if (build / source).read_bytes() != (stage / target).read_bytes():
            raise SystemExit('stage/build mismatch: ' + target)
    if args.combined_root or args.combined_explicit_root:
        data = (stage / fragment).read_bytes()
        if len(data) < 32 or data[:4] != b'DXBC':
            raise SystemExit('combined-root fixture is not a DXBC container')
        count = struct.unpack_from('<I', data, 28)[0]
        if count > (len(data) - 32) // 4:
            raise SystemExit('invalid combined-root part table')
        offsets = [struct.unpack_from('<I', data, 32 + 4 * i)[0] for i in range(count)]
        if not any(offset <= len(data) - 4 and data[offset:offset + 4] == b'RTS0' for offset in offsets):
            raise SystemExit('combined-root fixture lacks an embedded root signature')
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    wrapper = None
    if args.trace_config or args.trace_dxil_validation:
        repo = Path(__file__).resolve().parents[2]
        wrapper = output / 'msc-trace.dylib'
        source = 'msc_validation_trace.c' if args.trace_dxil_validation else 'msc_compatibility_trace.c'
        subprocess.run(['xcrun', 'clang', '-arch', 'x86_64', '-dynamiclib', '-I', str(repo / 'include'),
                        str(repo / 'tests/dx12' / source), str(converter),
                        '-Wl,-rpath,' + str(converter.parent), '-o', str(wrapper)], check=True, timeout=30)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    identity = 'LOADED_CODE winemetal.dll DXMTMSCCompileDXIL ' + pe_export_bytes(stage / 'winemetal.dll', 'DXMTMSCCompileDXIL')
    env = {key: value for key, value in os.environ.items() if not key.startswith(('DYLD_', 'DXMT_TEST_'))}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / 'wine/lib/wine'),
               WINEDLLOVERRIDES='winemetal=b', WINEDEBUG='+loaddll',
               DXMT_METALIRCONVERTER_PATH=str(wrapper or converter), DYLD_PRINT_LIBRARIES='1',
               MTL_DEBUG_LAYER='1', MTL_SHADER_VALIDATION='1', MTL_SHADER_VALIDATION_REPORT_TO_STDERR='1')
    if args.trace_dxil_validation:
        env['DXMT_TEST_VALIDATION_MODE'] = 'dxil'
    command = [str(stage / 'wine/bin/wine'), 'dx12_metal_hlsl_gpu.exe',
               'metal_hlsl_constants.ps.cso', fragment]
    results = []
    cases = []
    if args.both_spaces or combined:
        option = '--combined-explicit-root' if args.combined_explicit_root else '--combined' if combined else '--both-spaces'
        cases = [(f'positive-{i}', [option], 0) for i in range(args.repeats)]
        cases += [('wrong-oracle', [option + '-wrong'], 10)]
    else:
        for i in range(args.repeats):
            cases += [(f'positive-{i}', [], 0), (f'explicit-zero-{i}', ['--zero-flags'], 0),
                      (f'legacy-zero-{i}', ['--legacy-zero'], 0)]
        cases += [('wrong-oracle', ['--wrong'], 10)]
    for name, extra, expected in cases:
        result = subprocess.run(command + extra, cwd=stage, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180, text=True, errors='replace')
        log = result.stdout
        (output / (name + '.log')).write_text(log)
        dll_path = ('Z:' + str(stage / 'winemetal.dll').replace('/', '\\')).replace('\\', '\\\\').lower()
        checks = [identity in log, any(('loaded l"' + dll_path + '"') in line.lower() and ': builtin' in line.lower()
                                      for line in log.splitlines()),
                  str(stage / 'wine/lib/wine/x86_64-unix/winemetal.so') in log,
                  str(converter) in log, 'Metal API Validation Enabled' in log,
                  'Metal GPU Validation Enabled' in log, 'PIXEL 255,0,0,255' in log,
                  not re.search(r'(?i)metal (?:api|gpu|shader) validation (?:error|warning)|failed assertion', log)]
        if name != 'wrong-oracle':
            checks += ['PIXEL 0,255,0,255' in log, 'PIXEL 64,64,0,128' in log,
                       'MULTIPASS_PIXEL 64,64,0,128' in log,
                       'Metal HLSL function constants and framebuffer fetch GPU passed' in log]
            if combined:
                checks += ['PIXEL 0,128,0,128' in log]
        if args.trace_config:
            flags = re.findall(r'^DXMT_TEST_COMPATIBILITY_FLAGS (\d+)\s*$', log, re.M)
            checks += [str(wrapper) in log, len(flags) == 4,
                       all(int(value) == (0 if name.startswith('explicit-zero-') else 4) for value in flags)]
        if args.trace_dxil_validation:
            checks += [str(wrapper) in log, len(re.findall(r'^VALIDATION_SET dxil 0\s*$', log, re.M)) >= 2]
        if result.returncode != expected or not all(checks):
            raise SystemExit(f'{name}: failed (exit {result.returncode}, checks {checks}); see {output / (name + ".log")}')
        timings = re.findall(r'^MSC_(SIZING|OUTPUT)_MS (constants|fetch) ([0-9.eE+-]+)(?: (\d+))?\s*$', log, re.M)
        if len(timings) < (2 if name == 'wrong-oracle' else 4):
            raise SystemExit(f'{name}: missing timing samples')
        results.append({'case': name, 'exit': result.returncode, 'timings': timings})
        print(name + ': passed', flush=True)
    summaries = {}
    for case in (('positive',) if args.both_spaces or combined else ('positive', 'explicit-zero', 'legacy-zero')):
        for stage_name in ('SIZING', 'OUTPUT'):
            for shader in ('constants', 'fetch'):
                values = [float(value) for result in results if result['case'].startswith(case + '-')
                          for stage_value, name, value, size in result['timings']
                          if stage_value == stage_name and name == shader]
                values.sort()
                summaries[f'{case}_{stage_name}_{shader}'] = {
                    'median_ms': statistics.median(values), 'p95_ms': values[math.ceil(.95 * len(values)) - 1],
                    'min_ms': values[0], 'max_ms': values[-1], 'count': len(values)}
    data = {'command': command, 'converter_sha256': sha(converter),
            'artifacts': {target: sha(stage / target) for _, target in pairs},
            'results': results, 'summaries': summaries, 'trace_wrapper_sha256': sha(wrapper) if wrapper else None,
            'hardware': subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
            'macos': subprocess.check_output(['sw_vers', '-productVersion'], text=True).strip(),
            'wine': subprocess.check_output([str(stage / 'wine/bin/wine'), '--version'], text=True,
                                            env={**os.environ, 'WINEPREFIX': str(prefix)}, stderr=subprocess.DEVNULL).strip(),
            'limitations': 'validation enabled; process cold; sizing and output calls each perform conversion; no GPU timing or LOD semantics measured'}
    (output / 'summary.json').write_text(json.dumps(data, indent=2) + '\n')
    (output / 'summary.md').write_text('# Controlled MSC conversion calls\n\n' + data['limitations'] + '\n\n' +
        '| Case | n | median ms | p95 ms | min ms | max ms |\n| --- | ---: | ---: | ---: | ---: | ---: |\n' +
        ''.join(f"| {name} | {v['count']} | {v['median_ms']:.4f} | {v['p95_ms']:.4f} | {v['min_ms']:.4f} | {v['max_ms']:.4f} |\n"
                for name, v in summaries.items()))
    return 0


if __name__ == '__main__':
    sys.exit(main())
