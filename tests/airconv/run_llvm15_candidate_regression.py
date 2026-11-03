#!/usr/bin/env python3
"""Run candidate LLVM metadata controls in a new isolated output directory."""
import argparse
import re
import shlex
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run(args):
    args = [str(a) for a in args]
    print('+', shlex.join(args), flush=True)
    subprocess.run(args, check=True)


def extract_air(metallib, destination):
    data = metallib.read_bytes()
    if len(data) < 88 or data[:4] != b'MTLB':
        raise ValueError('invalid metallib header')
    offset, size = struct.unpack_from('<QQ', data, 72)
    if offset > len(data) or size > len(data) - offset or size == 0:
        raise ValueError('invalid metallib AIR range')
    destination.write_bytes(data[offset:offset + size])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=Path, required=True,
                        help='directory containing probe-combined and stress-combined')
    parser.add_argument('--captured-metallib', type=Path, required=True)
    parser.add_argument('--llvm-as', type=Path, required=True,
                        help='LLVM 15 llvm-as executable')
    parser.add_argument('--string-ir', type=Path, required=True,
                        help='LLVM 15 distringtype-backward.ll test fixture')
    parser.add_argument('--output', type=Path, required=True,
                        help='new output directory; must not exist')
    args = parser.parse_args()
    candidate = args.candidate.resolve(strict=True)
    captured = args.captured_metallib.resolve(strict=True)
    llvm_as = args.llvm_as.resolve(strict=True)
    string_ir = args.string_ir.resolve(strict=True)
    version = subprocess.check_output([str(llvm_as), '--version'], text=True)
    if not re.search(r'\bLLVM version 15\.0\.7\b', version):
        raise ValueError('llvm-as must be LLVM 15.0.7')
    out = args.output.absolute()
    out.mkdir(parents=True)
    probe, stress = candidate / 'probe-combined', candidate / 'stress-combined'
    generator = HERE / 'make_llvm15_ptrauth_rejection.py'
    original = out / 'captured.air'
    extract_air(captured, original)
    fixture = out / 'double.metallib'
    run([probe, captured, fixture, '--make-double'])
    run([probe, fixture, out / 'lowered.metallib', '--expect-lowered'])
    double = out / 'double.air'
    extract_air(fixture, double)
    string = out / 'string.air'
    run([llvm_as, string_ir, '-o', string])
    cases = [('ptrauth', original, None), ('derived', double, '--size-flag-14'),
             ('basic', double, '--basic-size-flag'),
             ('composite', double, '--composite-size-flag'),
             ('string', string, '--string-size-flag'),
             ('unknown', string, '--unknown-string-record')]
    for label, good, option in cases:
        bad = out / ('negative-' + label + '.air')
        cmd = [sys.executable, generator, good, bad]
        if option:
            cmd.append(option)
        run(cmd)
        run([stress, bad, good, '--concurrent'])
    print('PASS: six metadata controls and debug double lowering', flush=True)


if __name__ == '__main__':
    main()
