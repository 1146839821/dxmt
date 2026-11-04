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


def check_specialization(log, omitted_word=None):
    """Require the exact lookup/readback prefix, including intentional collisions."""
    sequence = [0, 0, 1, 0, 2, 1, 3, 2, 4, 0, 4, 3, 2, 1, 0]
    colors = ['255,0,0,255', '0,0,0,255', '255,255,0,255', '255,0,255,255', '255,0,0,0']
    collision = None if omitted_word is None else [2, 4, 6, 8][omitted_word]
    expected_lookups, expected_pixels, seen = [], [], set()
    for n, variant in enumerate(sequence):
        key = 0 if omitted_word is not None and variant == omitted_word + 1 else variant
        hit = key in seen
        seen.add(key)
        expected_lookups.append((n, variant, 'hit' if hit else 'miss', len(seen), len(seen), len(seen)))
        if n == collision:
            break
        expected_pixels.append(f'SPECIALIZATION_PIXEL {n} {variant} {colors[variant]}')
    lookups = re.findall(r'^SPECIALIZATION_LOOKUP (\d+) (\d+) (hit|miss) (\d+) (\d+) (\d+) ([0-9.eE+-]+)\s*$', log, re.M)
    actual = [(int(n), int(v), kind, int(size), int(functions), int(pipelines))
              for n, v, kind, size, functions, pipelines, _ in lookups]
    if actual != expected_lookups or not all(math.isfinite(float(row[6])) and float(row[6]) >= 0 for row in lookups):
        raise ValueError('specialization lookup sequence/count/timing mismatch')
    lines = [line for line in log.splitlines() if line.startswith('SPECIALIZATION_')]
    pixels = [line for line in lines if line.startswith('SPECIALIZATION_PIXEL ')]
    summary = [line for line in lines if line.startswith('SPECIALIZATION_CACHE ')]
    mismatch = [line for line in lines if line.startswith('SPECIALIZATION_MISMATCH ')]
    expected_mismatch = [] if collision is None else [
        f'SPECIALIZATION_MISMATCH {collision} {omitted_word + 1} {omitted_word} '
        + ('255 0' if omitted_word in (0, 3) else '0 255')]
    if pixels != expected_pixels or mismatch != expected_mismatch or summary != (
            ['SPECIALIZATION_CACHE 5 10 5 5 5'] if collision is None else []):
        raise ValueError('specialization pixel/summary/collision mismatch')
    if len(lines) != len(lookups) + len(pixels) + len(summary) + len(mismatch):
        raise ValueError('unrecognized specialization record')
    return {'lookups': lookups, 'pixels': pixels, 'summary': summary, 'mismatch': mismatch}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'prefix', 'build', 'converter', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--processes', type=int, default=5)
    parser.add_argument('--frequency', action='store_true', help='run repeated low/high update sequences')
    parser.add_argument('--ignore-debug', action='store_true', help='ask MSC to ignore debug information')
    parser.add_argument('--specialization-cache', action='store_true', help='verify test-owned specialization PSO cache and four incomplete-key controls')
    parser.add_argument('--main-fixture', default='metal_hlsl_dynamic_main.ps.cso')
    parser.add_argument('--branch-fixture', default='metal_hlsl_dynamic_branch_main.ps.cso')
    args = parser.parse_args()
    if not 2 <= args.processes <= 20:
        parser.error('processes must be 2..20')
    if sum((args.frequency, args.ignore_debug, args.specialization_cache)) > 1:
        parser.error('frequency, ignore-debug and specialization-cache are separate experiments')
    if any(Path(name).name != name for name in (args.main_fixture, args.branch_fixture)):
        parser.error('fixture names must be basenames')
    stage, prefix, build, converter = (getattr(args, n).resolve(strict=True) for n in ('stage', 'prefix', 'build', 'converter'))
    pairs = [('src/d3d12/d3d12.dll', 'd3d12.dll'), ('src/dxgi/dxgi.dll', 'dxgi.dll'),
             ('src/winemetal/winemetal.dll', 'winemetal.dll'), ('src/winemetal/unix/winemetal.so', 'wine/lib/wine/x86_64-unix/winemetal.so')]
    pairs += [('tests/dx12/' + n, n) for n in ('dx12_msc_cbuffer_compare.exe', args.main_fixture,
              args.branch_fixture, 'metal_hlsl_constants.ps.cso')]
    for src, dst in pairs:
        data = (build / src).read_bytes()
        if src in ('src/d3d12/d3d12.dll', 'src/dxgi/dxgi.dll') and data[64:80] == b'Wine builtin DLL':
            data = data[:64] + b' ' * 16 + data[80:]
        if data != (stage / dst).read_bytes():
            raise SystemExit('stage/build mismatch: ' + dst)
    if (stage / 'wine/lib/wine/x86_64-windows/winemetal.dll').read_bytes() != (stage / 'winemetal.dll').read_bytes():
        raise SystemExit('staged WineMetal builtin/app-local mismatch')
    output = args.output.absolute(); output.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('DYLD_', 'DXMT_TEST_', 'MTL_DEBUG_', 'MTL_SHADER_VALIDATION'))}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / 'wine/lib/wine'), WINEDLLOVERRIDES='d3d12=n;dxgi=n;winemetal=b',
               WINEDEBUG='+loaddll', DXMT_METALIRCONVERTER_PATH=str(converter), DYLD_PRINT_LIBRARIES='1')
    cmd = [str(stage / 'wine/bin/wine'), 'dx12_msc_cbuffer_compare.exe', args.main_fixture,
           args.branch_fixture, 'metal_hlsl_constants.ps.cso']
    identity = [f'LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}' for dll, symbol in
                [('d3d12.dll', 'D3D12CreateDevice'), ('winemetal.dll', 'DXMTMSCCompileDXIL')]]
    samples, controls = [], []
    cases = [(f'run-{i}', False, False) for i in range(args.processes)] + [('validation', True, False), ('wrong', True, True)]
    if args.frequency:
        cases.append(('frequency-wrong', True, True))
    if args.specialization_cache:
        cases += [(f'cache-omit-{channel}', True, True) for channel in 'rgba']
    for name, validation, wrong in cases:
        runenv = dict(env)
        runenv['DXMT_SHADER_CACHE_PATH'] = str(output / (name + '-cache'))
        if validation:
            runenv.update(MTL_DEBUG_LAYER='1', MTL_SHADER_VALIDATION='1', MTL_SHADER_VALIDATION_REPORT_TO_STDERR='1')
        omitted_word = 'rgba'.index(name[-1]) if name.startswith('cache-omit-') else None
        extra = (['--specialization-' + name] if omitted_word is not None else
                 ['--ignore-debug-wrong'] if args.ignore_debug else
                 (['--frequency-wrong'] if name == 'frequency-wrong' else ['--wrong'])) if wrong else (
                     ['--specialization-cache'] if args.specialization_cache else
                     ['--frequency'] if args.frequency else ['--ignore-debug'] if args.ignore_debug else [])
        run = subprocess.run(cmd + extra, cwd=stage, env=runenv,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180, text=True, errors='replace')
        log = run.stdout; (output / f'{name}.log').write_text(log)
        expected_pixels = [f'CBUFFER_PIXEL {mode} {i} {color}' for mode in range(3) for i, color in
                           enumerate(('255,0,0,255', '0,255,0,255'))]
        if name == 'wrong': expected_pixels = expected_pixels[:4]
        rows = re.findall(r'^(FUNCTION_MS|PIPELINE_MS|GPU_NS) (\d+) (\d+) ([0-9.eE+-]+)\s*$', log, re.M)
        conversion = re.findall(r'^CONVERT_MS (\S+) (sizing|output) ([0-9.eE+-]+) (\d+)\s*$', log, re.M)
        frequency = re.findall(r'^FREQUENCY (\d+) (\d+) (\d+) (\d+) ([0-9.eE+-]+) (\d+)\s*$', log, re.M)
        paths = []
        for dll, kind in [('d3d12.dll','native'), ('dxgi.dll','native'), ('winemetal.dll','builtin')]:
            path = ('Z:' + str(stage / dll).replace('/', '\\')).replace('\\', '\\\\').lower()
            paths.append(any(('loaded l"' + path + '"') in line.lower() and ': ' + kind in line.lower() for line in log.splitlines()))
        checks = [run.returncode == (16 if omitted_word is not None else 14 if wrong else 0), all(s in log for s in identity), all(paths),
                  str(converter) in log, str(stage / 'wine/lib/wine/x86_64-unix/winemetal.so') in log,
                  all(s in log for s in expected_pixels), len(rows) == (15 if name == 'wrong' else 18), len(conversion) == 6,
                  not re.search(r'(?i)metal (?:api|gpu|shader) validation (?:error|warning)|failed assertion', log)]
        if validation: checks += ['Metal API Validation Enabled' in log, 'Metal GPU Validation Enabled' in log]
        else: checks += ['Metal API Validation Enabled' not in log, 'Metal GPU Validation Enabled' not in log]
        if args.frequency and not wrong:
            expected = [(m, p, n, (n // 8) % 2 if p == 0 else n % 2) for m in range(3) for p in range(2) for n in range(16)]
            checks += [[tuple(map(int, row[:4])) for row in frequency] == expected,
                       all(float(row[4]) >= 0 and int(row[5]) > 0 for row in frequency)]
        if name == 'frequency-wrong':
            checks += ['FREQUENCY_MISMATCH 2 0 0' in log, len(frequency) == 64]
        specialization = None
        if args.specialization_cache and name != 'wrong':
            try:
                specialization = check_specialization(log, omitted_word)
            except ValueError as error:
                raise SystemExit(f'{name}: {error}; see {output}/{name}.log') from error
            checks += [('MSC cbuffer, dynamic branch and specialization GPU passed' in log) == (omitted_word is None)]
        if not all(checks): raise SystemExit(f'{name}: failed {checks}; exit={run.returncode}, see {output}/{name}.log')
        controls.append({'case':name, 'returncode':run.returncode, 'validation':validation, 'specialization':specialization})
        if name.startswith('run-'): samples.append({'timings': rows, 'frequency': frequency, 'conversion':conversion, 'specialization':specialization})
        print(name + ': passed', flush=True)
    summaries = {}
    for phase in ('FUNCTION_MS', 'PIPELINE_MS', 'GPU_NS'):
        for mode in range(3):
            for index in range(2):
                v = sorted(float(value) for run in samples for kind, m, i, value in run['timings'] if kind == phase and int(m) == mode and int(i) == index)
                summaries[f'{phase}_{mode}_{index}'] = {'n':len(v), 'median':statistics.median(v),
                    'p95':v[math.ceil(.95*len(v))-1], 'min':v[0], 'max':v[-1]}
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    for fixture, label in ((args.main_fixture, 'main'), (args.branch_fixture, 'branch'), ('metal_hlsl_constants.ps.cso', 'specialized')):
        for phase in ('sizing', 'output'):
            v = sorted(float(t) for run in samples for name, part, t, size in run['conversion'] if name == fixture and part == phase)
            if len(v) != len(samples): raise SystemExit(f'missing conversion timings for {fixture} {phase}')
            summaries[f'CONVERT_MS_{label}_{phase}'] = {'n':len(v), 'median':statistics.median(v),
                'p95':v[math.ceil(.95*len(v))-1], 'min':v[0], 'max':v[-1]}
    if args.frequency:
        for mode in range(3):
            for policy in range(2):
                for position, name in ((4, 'encoding_us'), (5, 'gpu_ns')):
                    v = sorted(float(row[position]) for run in samples for row in run['frequency']
                               if int(row[0]) == mode and int(row[1]) == policy and int(row[2]) >= 2)
                    summaries[f'{name}_{mode}_{policy}'] = {'n':len(v), 'median':statistics.median(v),
                        'p95':v[math.ceil(.95*len(v))-1], 'min':v[0], 'max':v[-1]}
    if args.specialization_cache:
        for kind in ('hit', 'miss'):
            values = sorted(float(row[6]) for run in samples for row in run['specialization']['lookups'] if row[2] == kind)
            summaries['specialization_lookup_us_' + kind] = {'n':len(values), 'median':statistics.median(values),
                'p95':values[math.ceil(.95*len(values))-1], 'min':values[0], 'max':values[-1]}
    result = {'samples':samples, 'controls':controls, 'summaries':summaries, 'command':cmd + (
                  ['--specialization-cache'] if args.specialization_cache else ['--frequency'] if args.frequency else ['--ignore-debug'] if args.ignore_debug else []), 'converter_sha256':sha(converter),
              'artifacts':{dst:sha(stage / dst) for _, dst in pairs},
              'hardware':subprocess.check_output(['sysctl','-n','machdep.cpu.brand_string'],text=True).strip(),
              'macos':subprocess.check_output(['sw_vers','-productVersion'],text=True).strip(),
              'limitations':('4x4 pixels; test-owned cache scoped to one device/library/entry/vertex/render configuration, fixed UInt4/index zero; '
                  'five cache-created PSOs in addition to six comparison PSOs, 15 requests per process; driver already warm; '
                  'no cross-configuration, concurrency, in-flight destruction, persistence, D3D12 PSO cache, frame time or memory evidence; '
                  'validation disabled for timing samples' if args.specialization_cache else
                  '4x4 pixels, fixed mode/policy order, explicit two-entry PSO reuse only, no D3D12 PSO cache, frame time or memory; validation disabled for samples')}
    (output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    (output / 'summary.md').write_text('# MSC constant comparison\n\n' + result['limitations'] + '\n\n' +
      '| metric_mode_value | n | median | p95 | min | max |\n| --- | ---: | ---: | ---: | ---: | ---: |\n' +
      ''.join(f"| {k} | {v['n']} | {v['median']} | {v['p95']} | {v['min']} | {v['max']} |\n" for k,v in summaries.items()))
    return 0

if __name__ == '__main__':
    sys.exit(main())
