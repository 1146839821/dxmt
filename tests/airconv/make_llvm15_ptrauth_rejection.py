#!/usr/bin/env python3
"""Mutate one known unabbreviated metadata record for rejection testing.

Input is the captured debug AIR documented in D3D12_MSC_HLSL_PERFORMANCE_WORK.
This is a synthetic malformed/unsupported fixture, not real pointer-auth data.
"""
import argparse
from pathlib import Path


def vbr6(value):
    result = []
    while value >= 32:
        group = (value & 31) | 32
        result.extend((group >> bit) & 1 for bit in range(6))
        value >>= 5
    result.extend((value >> bit) & 1 for bit in range(6))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--size-flag-14', action='store_true',
                        help='set the size-as-metadata flag on a known 14-operand record')
    args = parser.parse_args()
    data = bytearray(args.input.read_bytes())
    bits = [(byte >> bit) & 1 for byte in data for bit in range(8)]
    operands = (0, 13, 6, 65, 2, 78, 68, 32, 32, 0, 3, 0, 0, 0)
    if not args.size_flag_14:
        operands += (0,)
    pattern = [1, 1, 0, 0] + vbr6(17) + vbr6(len(operands))
    for operand in operands:
        pattern.extend(vbr6(operand))
    matches = [offset for offset in range(len(bits) - len(pattern) + 1)
               if bits[offset:offset + len(pattern)] == pattern]
    if len(matches) != 1:
        raise SystemExit(f'expected exactly one known metadata record, found {len(matches)}')
    bit = (matches[0] + 4 + len(vbr6(17)) + len(vbr6(len(operands))) + 1
           if args.size_flag_14 else matches[0] + len(pattern) - 6)
    data[bit // 8] |= 1 << (bit % 8)
    with args.output.open('xb') as output:
        output.write(data)
    print(f'Changed {"size flag" if args.size_flag_14 else "operand 14"} at bit {bit}')


if __name__ == '__main__':
    main()
