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
    # Instrument the real MSC AIR to expose every component of the first
    # texture read. The HLSL originally only consumes x; this is an AIR probe,
    # not a claim about a compiled HLSL vector shader.
    original = (here / 'msc_typed_buffer_gpu_fixture.ll').read_text()
    anchor = '  %29 = extractvalue { <4 x i32>, i8 } %28, 0\n'
    if original.count(anchor) != 1:
        raise SystemExit('unexpected MSC fixture shape')
    extra = '  %dxmt_component_base = mul i32 %12, 4\n'
    for i in range(4):
        extra += (f'  %dxmt_component_{i} = extractelement <4 x i32> %29, i64 {i}\n'
                  f'  %dxmt_component_index_{i} = add i32 %dxmt_component_base, {8 + i}\n'
                  f'  %dxmt_component_index64_{i} = zext i32 %dxmt_component_index_{i} to i64\n'
                  f'  %dxmt_component_ptr_{i} = getelementptr i32, i32 addrspace(1)* %..unpack6, i64 %dxmt_component_index64_{i}\n'
                  f'  store i32 %dxmt_component_{i}, i32 addrspace(1)* %dxmt_component_ptr_{i}, align 4\n')
    fixture = path / 'components.ll'
    fixture.write_text(original.replace(anchor, anchor + extra))
    subprocess.run(['clang', '-fobjc-arc', '-framework', 'Foundation', '-framework', 'Metal',
                    str(here / 'msc_typed_buffer_gpu_oob.m'), '-o', str(exe)], check=True)
    subprocess.run([str(rewriter), str(fixture), str(library),
                    '--sidecar', '0', '24', '--sidecar', '8', '32'], check=True)
    subprocess.run([str(rewriter), str(fixture), str(baseline),
                    '--unpatched'], check=True)
    for selected, extra, expected, marker in [(library, [], 0, 'out=51,2 values=11,20,32,40'),
                                             (library, ['--wrong-expected'], 1, 'out=51,2 values=11,20,32,40'),
                                             (library, ['--wrong-components'], 1, 'out=51,2 values=11,20,32,40'),
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
