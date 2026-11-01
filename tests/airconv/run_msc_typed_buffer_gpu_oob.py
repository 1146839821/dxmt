#!/usr/bin/env python3
"""Run the offline MSC typed-buffer OOB experiment on a local Metal device."""
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--rewriter', type=Path, required=True)
args = parser.parse_args()
rewriter = args.rewriter.resolve(strict=True)
env = os.environ.copy()
env.update(MTL_DEBUG_LAYER='1', MTL_SHADER_VALIDATION='1',
           MTL_SHADER_VALIDATION_REPORT_TO_STDERR='1')
with tempfile.TemporaryDirectory(prefix='dxmt-msc-buffer-oob-') as directory:
    path = Path(directory)
    exe, library, baseline = path / 'probe', path / 'patched.metallib', path / 'unpatched.metallib'
    subprocess.run(['clang', '-fobjc-arc', '-framework', 'Foundation', '-framework', 'Metal',
                    str(here / 'msc_typed_buffer_gpu_oob.m'), '-o', str(exe)], check=True)
    subprocess.run([str(rewriter), str(here / 'msc_typed_buffer_gpu_fixture.ll'), str(library),
                    '--sidecar', '0', '24', '--sidecar', '8', '32'], check=True)
    subprocess.run([str(rewriter), str(here / 'msc_typed_buffer_gpu_fixture.ll'), str(baseline),
                    '--unpatched'], check=True)
    for selected, extra, expected, marker in [(library, [], 0, 'out=51,2 values=11,20,32,40'),
                                             (library, ['--wrong-expected'], 1, 'out=51,2 values=11,20,32,40'),
                                             (baseline, [], 1, 'out=')]:
        result = subprocess.run([str(exe), str(selected), *extra], env=env,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=60)
        print(result.stdout, end='')
        if (result.returncode != expected or marker not in result.stdout or
                'Metal API Validation Enabled' not in result.stdout or
                'Metal GPU Validation Enabled' not in result.stdout or
                re.search(r'(?i)(validation error|invalid metal usage:|execution of the command buffer was aborted)',
                          result.stdout)):
            raise SystemExit(f'unexpected probe result: exit={result.returncode}, expected={expected}')
        print('unpatched control passed' if selected == baseline else
              'negative control passed' if extra else 'GPU readback passed')
