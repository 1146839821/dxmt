#!/usr/bin/env python3
"""Compare reflected MSC linear and explicit descriptor-table GPU layouts."""
import argparse
import hashlib
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def run(args, **kwargs):
    args = list(map(str, args))
    print('+', shlex.join(args), flush=True)
    return subprocess.run(args, check=True, **kwargs)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shader', required=True, type=Path,
                        help='compiled threadgroup coherence DXIL fixture')
    parser.add_argument('--converter', required=True, type=Path,
                        help='metal-shaderconverter CLI executable')
    parser.add_argument('--output', required=True, type=Path,
                        help='new output directory')
    args = parser.parse_args()
    shader, converter = args.shader.resolve(strict=True), args.converter.resolve(strict=True)
    output = args.output.absolute()
    output.mkdir(parents=True)
    fixtures = output / 'fixtures'
    run([sys.executable, REPO / 'tests/dx12/make_threadgroup_coherency_fixtures.py', fixtures])
    for name in ('inputs.bin', 'expected.bin'):
        data = (fixtures / name).read_bytes()
        half = len(data) // 2
        (output / ('rotated-' + name)).write_bytes(data[half:] + data[:half])
    for label, expected in [('first', fixtures / 'expected.bin'),
                            ('second', output / 'rotated-expected.bin')]:
        bad = bytearray(expected.read_bytes())
        bad[0] ^= 1
        (output / (label + '-wrong.bin')).write_bytes(bad)
    executable = output / 'probe'
    run(['xcrun', 'clang', '-fobjc-arc', '-Wall', '-Wextra', '-Werror', '-I', REPO / 'include', '-framework', 'Foundation',
         '-framework', 'Metal', REPO / 'tests/dx12/msc_linear_layout_gpu.m', '-o', executable])
    print('converter sha256', sha(converter), 'shader sha256', sha(shader), flush=True)
    run([converter, '--version'])
    env = {k: v for k, v in os.environ.items() if not k.startswith(('MTL_DEBUG_', 'MTL_SHADER_VALIDATION'))}
    env.update(MTL_DEBUG_LAYER='1', MTL_SHADER_VALIDATION='1', MTL_SHADER_VALIDATION_REPORT_TO_STDERR='1')
    for mode in ('linear', 'root'):
        library, reflection = output / (mode + '.metallib'), output / (mode + '.json')
        cmd = [converter, shader, '-o', library, '--output-reflection-file=' + str(reflection)]
        if mode == 'root':
            cmd.append('--root-signature=' + str(REPO / 'tests/dx12/msc_linear_layout_root.json'))
        run(cmd)
        print(mode, 'metallib sha256', sha(library), 'reflection sha256', sha(reflection), flush=True)
        for dataset, wrong in [('first', False), ('second', False), ('first', True)]:
            inputs = fixtures / 'inputs.bin' if dataset == 'first' else output / 'rotated-inputs.bin'
            expected = (output / (dataset + '-wrong.bin') if wrong else
                        fixtures / 'expected.bin' if dataset == 'first' else output / 'rotated-expected.bin')
            cmd = [str(executable), str(library), str(reflection), str(inputs), str(expected), mode]
            result = subprocess.run(cmd, env=env, capture_output=True, timeout=120)
            log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
            label = mode + '-' + dataset + ('-wrong' if wrong else '-correct')
            (output / (label + '.log')).write_text(log)
            checks = [result.returncode == (11 if wrong else 0),
                      ('MISMATCH' if wrong else 'PASS ' + mode) in log,
                      'Metal API Validation Enabled' in log,
                      'Metal GPU Validation Enabled' in log,
                      not re.search(r'(?i)metal (?:api|gpu|shader) validation (?:error|warning)|failed assertion', log)]
            if not all(checks):
                raise SystemExit(f'{label} failed {checks}; see {output / (label + ".log")}')
            print(label, 'PASS', flush=True)


if __name__ == '__main__':
    main()
