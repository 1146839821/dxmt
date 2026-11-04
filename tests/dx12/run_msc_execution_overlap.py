#!/usr/bin/env python3
"""Validate compute-to-fragment output, then measure whole-workload spans.

The ALL/PIXEL comparison changes application root visibility only. Timestamp
spans are not stage overlap measurements. Use a separate Metal System Trace.
"""
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


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse(log, visibility, samples, iterations, wrong=False, workload='texture'):
    if log.count(f'OVERLAP_BACKEND msc visibility={visibility} iterations={iterations}') != 1:
        raise AssertionError('missing MSC backend/configuration evidence')
    rows = re.findall(r'^OVERLAP_SAMPLE (\d+) gpu_ns=(\S+) encode_ms=(\S+) submit_ms=(\S+) exact_bytes=65536\s*$', log, re.M)
    expected = 2 if wrong else samples + 2
    if [int(row[0]) for row in rows] != list(range(expected)):
        raise AssertionError('readback/timing sample sequence incomplete')
    values = []
    for index, gpu, encode, submit in rows:
        gpu, encode, submit = map(float, (gpu, encode, submit))
        if not all(math.isfinite(x) for x in (gpu, encode, submit)) or gpu <= 0 or min(encode, submit) < 0:
            raise AssertionError('invalid timing value')
        values.append({'index': int(index), 'gpu_ms': gpu / 1e6, 'encode_ms': encode, 'submit_ms': submit})
    failures = re.findall(r'^OVERLAP_MISMATCH sample=(\d+) x=(\d+) y=(\d+) actual=(\d+) expected=(\d+)\s*$', log, re.M)
    if wrong:
        if len(failures) != 1 or failures[0][:3] != ('2', '0', '0') or int(failures[0][3]) ^ int(failures[0][4]) != 1:
            raise AssertionError('wrong-output control failed at an unexpected value')
    elif failures:
        raise AssertionError('unexpected pixel mismatch')
    if log.count(f'OVERLAP_PASS visibility={visibility} samples={samples} warmup=2') != int(not wrong):
        raise AssertionError('completion marker mismatch')
    if 'OVERLAP_ERROR' in log:
        raise AssertionError('probe error')
    config = re.findall(r'^DEPENDENCY_CONFIG workload=(\S+)\s*$', log, re.M)
    encoders = re.findall(r'^DEPENDENCY_ENCODERS sample=(\d+) sequence=(\S+) lists=(\d+)\s*$', log, re.M)
    scratch = re.findall(r'^DEPENDENCY_SCRATCH sample=(\d+) exact_bytes=(\d+)\s*$', log, re.M)
    if workload == 'texture':
        if config or encoders or scratch:
            raise AssertionError('unexpected dependency workload')
    else:
        if workload not in ('vb-direct', 'vb-transitive', 'vb-split-list') or visibility != 'all' or config != [workload]:
            raise AssertionError('dependency configuration mismatch')
        sequence = {'vb-direct': 'CR', 'vb-transitive': 'CBCR', 'vb-split-list': 'C|CR'}[workload]
        lists = '2' if workload == 'vb-split-list' else '1'
        if encoders != [(str(i), sequence, lists) for i in range(3 if wrong else expected)]:
            raise AssertionError('dependency encoder sequence incomplete')
        expected_scratch = [] if workload == 'vb-direct' else [(str(i), '24576') for i in range(expected)]
        if scratch != expected_scratch:
            raise AssertionError('dependency scratch readback incomplete')
    return values


def stats(values):
    values = sorted(values)
    return {'count': len(values), 'median_ms': statistics.median(values),
            'p95_ms': values[math.ceil(len(values) * .95) - 1], 'min_ms': values[0], 'max_ms': values[-1]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'prefix', 'build', 'converter', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--processes', type=int, default=5)
    parser.add_argument('--samples', type=int, default=10)
    parser.add_argument('--iterations', type=int, default=1024)
    parser.add_argument('--vertex-dependency', action='store_true',
                        help='run direct/transitive CS-to-VB correctness cases instead of the texture overlap comparison')
    args = parser.parse_args()
    if not 2 <= args.processes <= 20 or not 2 <= args.samples <= 100 or not 1 <= args.iterations <= 4096:
        parser.error('require 2..20 processes, 2..100 samples, 1..4096 iterations')
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                     for name in ('stage', 'prefix', 'build', 'converter'))
    pairs = [('src/d3d12/d3d12.dll', 'd3d12.dll'), ('src/dxgi/dxgi.dll', 'dxgi.dll'),
             ('src/winemetal/winemetal.dll', 'winemetal.dll'),
             ('src/winemetal/unix/winemetal.so', 'wine/lib/wine/x86_64-unix/winemetal.so'),
             ('tests/dx12/dx12_msc_execution_overlap.exe', 'dx12_msc_execution_overlap.exe')]
    fixture = 'msc_execution_overlap_vb' if args.vertex_dependency else 'msc_execution_overlap'
    pairs += [(f'tests/dx12/{fixture}.{s}.cso', f'{fixture}.{s}.cso') for s in ('cs', 'vs', 'ps')]
    for source, target in pairs:
        expected = (build / source).read_bytes()
        if source in ('src/d3d12/d3d12.dll', 'src/dxgi/dxgi.dll') and expected[64:80] == b'Wine builtin DLL':
            expected = expected[:64] + b' ' * 16 + expected[80:]
        if expected != (stage / target).read_bytes():
            raise AssertionError('stage/build mismatch: ' + target)
    if (stage / 'winemetal.dll').read_bytes() != (stage / 'wine/lib/wine/x86_64-windows/winemetal.dll').read_bytes():
        raise AssertionError('WineMetal builtin/app-local mismatch')
    identities = [f'LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}'
                  for dll, symbol in (('d3d12.dll', 'D3D12CreateDevice'), ('winemetal.dll', 'DXMTMSCGetCapabilities'))]
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    report = {'schema': 1, 'scope': ('D3D12 MSC direct/transitive Compute-to-VB correctness; no scheduling proof'
                                   if args.vertex_dependency else 'D3D12 MSC Compute-to-Fragment whole-workload baseline; no overlap percentage'),
              'baseline_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=Path(__file__).resolve().parents[2], text=True).strip(),
              'hardware': subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
              'os': subprocess.check_output(['sw_vers'], text=True), 'converter_sha256': sha(converter),
              'artifacts': {target: sha(stage / target) for _, target in pairs},
              'iterations': args.iterations, 'runs': [], 'not_measured': ['stage overlap', 'occupancy', 'bandwidth', 'memory']}
    env = {k: v for k, v in os.environ.items() if not k.startswith(('DYLD_', 'DXMT_', 'MTL_'))}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / 'wine/lib/wine'),
               WINEDLLOVERRIDES='d3d12,dxgi=n;winemetal=b', WINEDEBUG='+loaddll',
               DYLD_PRINT_LIBRARIES='1', DXMT_METALIRCONVERTER_PATH=str(converter))
    report['wine'] = subprocess.check_output([str(stage / 'wine/bin/wine'), '--version'], env=env, text=True,
                                             stderr=subprocess.DEVNULL).strip()
    if args.vertex_dependency:
        workloads = ('vb-direct', 'vb-transitive', 'vb-split-list')
        cases = [(f'validation-{w}', 'all', True, False, 2, w) for w in workloads]
        cases += [(f'negative-{w}', 'all', True, True, 2, w) for w in workloads]
        cases += [(f'repeat-{i}-{w}', 'all', False, False, args.samples, w)
                  for i in range(args.processes) for w in workloads]
    else:
        cases = [(f'validation-{vis}', vis, True, False, 2, 'texture') for vis in ('all', 'pixel')]
        cases += [(f'negative-{vis}', vis, True, True, 2, 'texture') for vis in ('all', 'pixel')]
        cases += [(f'timing-{i}-{vis}', vis, False, False, args.samples, 'texture') for i in range(args.processes)
                  for vis in (('all', 'pixel') if i % 2 == 0 else ('pixel', 'all'))]
    for label, visibility, validation, wrong, samples, workload in cases:
        case_env = {**env, 'DXMT_SHADER_CACHE_PATH': str(output / ('cache-' + label)),
                    'DXMT_LOG_PATH': str(output / ('logs-' + label))}
        if validation:
            case_env.update(MTL_DEBUG_LAYER='1', MTL_SHADER_VALIDATION='1', MTL_SHADER_VALIDATION_REPORT_TO_STDERR='1')
        command = [str(stage / 'wine/bin/wine'), 'dx12_msc_execution_overlap.exe'] + [f'{fixture}.{s}.cso' for s in ('cs', 'vs', 'ps')]
        command += [visibility, str(samples), str(args.iterations), 'wrong' if wrong else 'normal', 'run']
        if workload != 'texture':
            command += [workload]
        result = subprocess.run(command, cwd=stage, env=case_env, capture_output=True, text=True, errors='replace', timeout=180)
        log = result.stdout + result.stderr
        (output / (label + '.log')).write_text(log)
        if result.returncode != int(wrong):
            raise AssertionError(f'{label}: exit {result.returncode}; see log')
        rows = parse(log, visibility, samples, args.iterations, wrong, workload)
        for dll, mode in (('d3d12.dll', 'native'), ('dxgi.dll', 'native'), ('winemetal.dll', 'builtin')):
            path = ('Z:' + str(stage / dll).replace('/', '\\')).replace('\\', '\\\\').lower()
            if not any(('loaded l"' + path + '"') in line.lower() and ': ' + mode in line.lower() for line in log.splitlines()):
                raise AssertionError('loaded path mismatch: ' + dll)
        if not all(marker in log for marker in identities) or not all(p in log for p in (str(converter), str(stage / 'wine/lib/wine/x86_64-unix/winemetal.so'))):
            raise AssertionError('loaded code/provider identity missing')
        if (('Metal API Validation Enabled' in log) != validation or
                ('Metal GPU Validation Enabled' in log) != validation or
                re.search(r'(?i)metal (?:api|gpu|shader) validation (?:error|warning)|failed assertion|Invalid Metal usage', log)):
            raise AssertionError('validation state/error mismatch')
        record = {'label': label, 'visibility': visibility, 'workload': workload, 'validation': validation, 'negative_control': wrong,
                  'argv': command, 'exit': result.returncode, 'samples': rows,
                  'environment': {k: v for k, v in case_env.items() if k.startswith(('WINE', 'DXMT_', 'MTL_', 'DYLD_'))}}
        if not validation:
            record['summary'] = {key: stats([r[key] for r in rows[2:]]) for key in ('gpu_ms', 'encode_ms', 'submit_ms')}
        report['runs'].append(record)
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
        print(label, 'PASS', flush=True)
    if args.vertex_dependency:
        print('Verified direct, transitive and split-list vertex dependencies; no performance comparison')
        return
    report['pooled'] = {visibility: {key: stats([s[key] for r in report['runs'] if not r['validation'] and r['visibility'] == visibility
                                               for s in r['samples'][2:]]) for key in ('gpu_ms', 'encode_ms', 'submit_ms')}
                        for visibility in ('all', 'pixel')}
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['pooled'], indent=2))


if __name__ == '__main__':
    main()
