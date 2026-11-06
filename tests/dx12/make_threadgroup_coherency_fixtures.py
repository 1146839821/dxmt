#!/usr/bin/env python3
"""Create independent inputs and expected outputs for the threadgroup probe."""
import argparse
import struct
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path, help='new output directory')
parser.add_argument('--groups', type=int, default=32, help='threadgroups per input set')
parser.add_argument('--variant', type=int, default=0, help='compile-time XOR variant (uint32)')
args = parser.parse_args()
if not 1 <= args.groups <= 4096:
    parser.error('--groups must be between 1 and 4096')
if not 0 <= args.variant <= 0xffffffff:
    parser.error('--variant must be a uint32')
args.output.mkdir(parents=True)
inputs, expected = bytearray(), bytearray()
mask = 0xffffffff
# Two independent sets, as required by the differential runner.
for group in range(2 * args.groups):
    seed = (0x12345678 ^ (group * 0x45d9f3b)) & mask
    inputs += struct.pack('<4I', seed, group, ~group & mask, 0xabcdef01)
    for lane in range(12):
        first, second = (lane * 5 + 17) % 64, (lane + 32) % 64
        value_a = seed ^ ((first * 0x9e3779b9) & mask)
        value_b = seed ^ ((second * 0x9e3779b9) & mask)
        expected += struct.pack('<2I', value_a ^ args.variant, value_b ^ args.variant)
(args.output / 'inputs.bin').write_bytes(inputs)
(args.output / 'expected.bin').write_bytes(expected)
