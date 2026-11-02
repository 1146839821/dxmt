#!/usr/bin/env python3
"""Run isolated default-vs-zero MSC flags against the existing fragment LOD oracle."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys
import os


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'prefix', 'build', 'converter', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.repeats <= 10:
        parser.error('repeats must be 1..10')
    stage, prefix, build, converter = (getattr(args, k).resolve(strict=True) for k in ('stage', 'prefix', 'build', 'converter'))
    root = args.output.absolute(); root.mkdir(parents=True, exist_ok=False)
    snapshot = root / 'verified-build'
    pairs = [('src/d3d12/d3d12.dll', 'd3d12.dll'), ('src/dxgi/dxgi.dll', 'dxgi.dll'),
             ('src/winemetal/winemetal.dll', 'winemetal.dll'),
             ('src/winemetal/unix/winemetal.so', 'wine/lib/wine/x86_64-unix/winemetal.so'),
             ('tests/dx12/dx12_texture_lod_clamp.exe', 'dx12_texture_lod_clamp.exe')]
    for source, target in pairs:
        data = (build / source).read_bytes()
        if source in ('src/d3d12/d3d12.dll', 'src/dxgi/dxgi.dll') and data[64:80] == b'Wine builtin DLL':
            data = data[:64] + b' ' * 16 + data[80:]
        if data != (stage / target).read_bytes():
            raise SystemExit('stage/build mismatch: ' + target)
        dest = snapshot / source; dest.parent.mkdir(parents=True, exist_ok=True); dest.write_bytes(data)
    repo = Path(__file__).resolve().parents[2]
    wrapper = root / 'lod-flags-wrapper.dylib'
    subprocess.run(['xcrun', 'clang', '-arch', 'x86_64', '-dynamiclib', '-I', str(repo / 'include'),
                    str(repo / 'tests/dx12/msc_lod_flags_trace.c'), str(converter),
                    '-Wl,-rpath,' + str(converter.parent), '-o', str(wrapper)], check=True, timeout=30)
    base = [sys.executable, str(repo / 'tests/dx12/run_msc_lod_experiment.py'), '--stage', str(stage),
            '--prefix', str(prefix), '--build', str(snapshot), '--fragment-ps', 'lod_ps.cso',
            '--fragment-vs', 'lod_vs.cso', '--case', 'fragment', '--case', 'fragment-control']
    observations = []
    for index in range(args.repeats):
        for mode in ('default', 'zero'):
            env = {k:v for k,v in os.environ.items() if not k.startswith(('DYLD_', 'DXMT_TEST_'))}
            env.update(DXMT_METALIRCONVERTER_PATH=str(wrapper), DXMT_TEST_LOD_FLAGS=mode)
            out = root / f'{mode}-{index}'
            result = subprocess.run(base + ['--logs', str(out)], env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, errors='replace', timeout=300)
            (root / f'{mode}-{index}.runner.log').write_text(result.stdout)
            dirs = list(out.glob('msc-lod-*'))
            if len(dirs) != 1: raise SystemExit(f'{mode}-{index}: missing unique result; see runner log')
            folder = dirs[0]
            summary = json.loads((folder / 'summary.json').read_text())
            log = (folder / 'fragment.log').read_text(errors='replace')
            flags = [tuple(map(int, m)) for m in re.findall(r'^LOD_FLAG (\d+) (\d+)\s*$', log, re.M)]
            times = re.findall(r'^LOD_COMPILE_NS (\d+) (ok|failed)\s*$', log, re.M)
            sizes = [int(x) for x in re.findall(r'^LOD_METALLIB_SIZE (\d+)\s*$', log, re.M)]
            outputs = re.findall(r'^fragment (ordinary|reserved) outputs:([^\r\n]*)', log, re.M)
            checks = [str(wrapper) in log, str(converter) in log, bool(flags),
                      all(a == 4 and b == (0 if mode == 'zero' else 4) for a,b in flags),
                      bool(times), all(status == 'ok' for _,status in times), bool(sizes),
                      sorted(name for name, _ in outputs) == ['ordinary', 'reserved']]
            if not all(checks): raise SystemExit(f'{mode}-{index}: failed trace checks {checks}; see {folder}')
            passed = result.returncode == 0 and all(case['pass'] for case in summary['results'])
            if mode == 'default' and not passed:
                raise SystemExit(f'{mode}-{index}: baseline regression failed; see {folder}')
            observations.append({'mode':mode, 'index':index, 'harness_exit':result.returncode, 'pass':passed,
                                 'flags':flags, 'compile_ns':[int(n) for n,_ in times], 'sizes':sizes,
                                 'outputs':outputs, 'harness':summary, 'logs':str(folder)})
            print(f'{mode}-{index}: oracle {"PASS" if passed else "FAIL"}', flush=True)
    summaries = {}
    for mode in ('default', 'zero'):
        for position in range(2):
            values = sorted(o['compile_ns'][position] for o in observations
                            if o['mode'] == mode and len(o['compile_ns']) > position)
            if len(values) != args.repeats:
                raise SystemExit(f'{mode}: missing ordered compilation samples')
            summaries[f'{mode}_compile_{position}'] = {
                'count':len(values), 'median_ns':statistics.median(values),
                'p95_ns':values[math.ceil(.95 * len(values)) - 1],
                'min_ns':values[0], 'max_ns':values[-1]}
    result = {'converter_sha256':sha(converter), 'wrapper_sha256':sha(wrapper),
              'artifacts':{target:sha(stage / target) for _,target in pairs},
              'fixtures':{n:sha(stage / n) for n in ('lod_ps.cso','lod_vs.cso')},
              'observations':observations, 'summaries':summaries,
              'hardware':subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
              'macos':subprocess.check_output(['sw_vers', '-productVersion'], text=True).strip(),
              'git_head':subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
              'wine':observations[0]['harness']['wine_version'],
              'command':base,
              'limitations':'validation enabled; two separate processes per mode; experimental converter wrapper bypasses production cache key; no GPU time, memory or statistically valid speedup claim'}
    (root / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    (root / 'summary.md').write_text('# MSC LOD flags comparison\n\n' + result['limitations'] + '\n\n' +
        '| case | count | median ns | p95 ns | min ns | max ns |\n| --- | ---: | ---: | ---: | ---: | ---: |\n' +
        ''.join(f"| {k} | {v['count']} | {v['median_ns']} | {v['p95_ns']} | {v['min_ns']} | {v['max_ns']} |\n"
                for k,v in summaries.items()))
    return 0

if __name__ == '__main__':
    sys.exit(main())
