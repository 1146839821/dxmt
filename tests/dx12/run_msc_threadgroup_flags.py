#!/usr/bin/env python3
"""Isolated correctness A/B for MSC threadgroup-memory compatibility flag."""
import argparse
import hashlib
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'prefix', 'build', 'converter', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    stage, prefix, build, converter = (getattr(args, n).resolve(strict=True)
                                       for n in ('stage', 'prefix', 'build', 'converter'))
    out = args.output.absolute()
    out.mkdir(parents=True)
    pairs = [('src/d3d12/d3d12.dll', 'd3d12.dll'), ('src/dxgi/dxgi.dll', 'dxgi.dll'),
             ('src/winemetal/winemetal.dll', 'winemetal.dll'),
             ('src/winemetal/unix/winemetal.so', 'wine/lib/wine/x86_64-unix/winemetal.so'),
             ('tests/dx12/dx12_compute_sm6.exe', 'dx12_compute_sm6.exe'),
             ('tests/dx12/compute_sm6_threadgroup_coherency.cs.cso', 'threadgroup.cs.cso')]
    for source, target in pairs:
        data = (build / source).read_bytes()
        if source in ('src/d3d12/d3d12.dll', 'src/dxgi/dxgi.dll') and data[64:80] == b'Wine builtin DLL':
            data = data[:64] + b' ' * 16 + data[80:]
        if data != (stage / target).read_bytes():
            raise SystemExit('stage/build mismatch: ' + target)
    wrapper = out / 'msc-threadgroup-flags.dylib'
    subprocess.run(['xcrun', 'clang', '-arch', 'x86_64', '-dynamiclib', '-I', str(REPO / 'include'),
                    '-Wl,-rpath,' + str(converter.parent), str(REPO / 'tests/dx12/msc_threadgroup_flags_trace.c'),
                    str(converter), '-o', str(wrapper)], check=True)
    subprocess.run([sys.executable, str(REPO / 'tests/dx12/make_threadgroup_coherency_fixtures.py'),
                    str(out / 'fixtures')], check=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('DYLD_', 'DXMT_TEST_', 'MTL_DEBUG_', 'MTL_SHADER_VALIDATION'))}
    env.update(WINEPREFIX=str(prefix), WINEDLLOVERRIDES='d3d12,dxgi=n;winemetal=b', WINEDEBUG='+loaddll',
               DYLD_PRINT_LIBRARIES='1', DXMT_SHADER_CACHE='0', DXMT_METALIRCONVERTER_PATH=str(wrapper),
               MTL_DEBUG_LAYER='1', MTL_SHADER_VALIDATION='1', MTL_SHADER_VALIDATION_REPORT_TO_STDERR='1')
    print('converter sha256', sha(converter), 'wrapper sha256', sha(wrapper), flush=True)
    for _, target in pairs:
        print(target, sha(stage / target), flush=True)
    cases = [('baseline', 'root', False), ('enabled', 'root', False),
             ('enabled', 'table', False), ('baseline', 'table', False),
             ('baseline', 'root', True), ('enabled', 'root', True)]
    for mode, binding, wrong in cases:
        env['DXMT_TEST_THREADGROUP_FLAGS'] = mode
        label = mode + '-' + binding + ('-wrong' if wrong else '')
        cmd = [stage / 'wine/bin/wine', stage / 'dx12_compute_sm6.exe', stage / 'threadgroup.cs.cso',
               '--threadgroup-differential', binding, out / 'fixtures/inputs.bin', out / 'fixtures/expected.bin']
        if wrong:
            cmd.append('--wrong-expected')
        print('+', shlex.join(map(str, cmd)), flush=True)
        result = subprocess.run(list(map(str, cmd)), cwd=stage, env=env, capture_output=True, timeout=120)
        log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        (out / (label + '.log')).write_text(log)
        flags = re.findall(r'DXMT_TEST_THREADGROUP_FLAGS (\d+) (\d+)', log)
        rows = re.findall(r'MSC numeric kind=int64 binding=' + binding + r' pass=\d cases=32 exact_bytes=3072 guards=64 passed', log)
        paths = [str(converter), str(wrapper), str(stage / 'wine/lib/wine/x86_64-unix/winemetal.so')]
        loaded = []
        for dll, kind in [('d3d12.dll', 'native'), ('dxgi.dll', 'native'), ('winemetal.dll', 'builtin')]:
            path = ('Z:' + str(stage / dll).replace('/', '\\')).replace('\\', '\\\\').lower()
            loaded.append(any(('loaded l"' + path + '"') in line.lower() and
                              ': ' + kind in line.lower() for line in log.splitlines()))
        checks = [result.returncode == (1 if wrong else 0), bool(flags),
                  all(int(before) == 4 and int(after) == (1028 if mode == 'enabled' else 4) for before, after in flags),
                  len(rows) == (0 if wrong else 3), 'Metal API Validation Enabled' in log,
                  'Metal GPU Validation Enabled' in log,
                  not re.search(r'(?i)metal (?:api|gpu|shader) validation (?:error|warning)|failed assertion', log),
                  all(x.lower() in log.lower() for x in paths), all(loaded)]
        if not all(checks):
            raise SystemExit(f'{label} failed {checks}; see {out / (label + ".log")}')
        print(label, 'PASS', flush=True)


if __name__ == '__main__':
    main()
