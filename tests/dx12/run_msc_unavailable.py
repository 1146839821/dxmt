#!/usr/bin/env python3
"""Check MSC loader errors through a paired WineMetal PE/Unix bridge."""
import argparse
import hashlib
import json
import os
import struct
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from run_msc_lod_experiment import pe_export_bytes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'prefix', 'build', 'converter', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                       for name in ('stage', 'prefix', 'build', 'converter'))
    pairs = [('src/winemetal/winemetal.dll', 'winemetal.dll'),
             ('src/winemetal/unix/winemetal.so', 'wine/lib/wine/x86_64-unix/winemetal.so'),
             ('tests/dx12/dx12_msc_unavailable.exe', 'dx12_msc_unavailable.exe'),
             ('tests/dx12/compute_sm6.cs.cso', 'compute_sm6.cs.cso')]
    hashes = {}
    for source, target in pairs:
        data = (stage / target).read_bytes()
        if data != (build / source).read_bytes():
            raise SystemExit('stage/build mismatch: ' + target)
        hashes[target] = hashlib.sha256(data).hexdigest()
    fake = (build / 'tests/dx12/libmsc_incomplete_runtime.dylib').resolve(strict=True)
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('DYLD_', 'DXMT_TEST_'))}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / 'wine/lib/wine'),
               WINEDLLOVERRIDES='winemetal=b', WINEDEBUG='+loaddll', DYLD_PRINT_LIBRARIES='1')
    identity = 'LOADED_CODE winemetal.dll DXMTMSCCompileDXIL ' + pe_export_bytes(stage / 'winemetal.dll', 'DXMTMSCCompileDXIL')
    path = ('Z:' + str(stage / 'winemetal.dll').replace('/', '\\')).replace('\\', '\\\\').lower()
    cases = [('available', 'available', converter, 'compute_sm6.cs.cso'),
             ('incompatible', 'incompatible', fake, 'compute_sm6.cs.cso')]
    for size in (1, 2, 3):
        fixture = output / f'truncated-{size}.dxil'
        fixture.write_bytes(b'\0' * size)
        cases.append((f'invalid-{size}', 'invalid', converter, 'Z:' + str(fixture).replace('/', '\\')))
        if size == 1:
            cases.append(('incompatible-short', 'incompatible', fake, 'Z:' + str(fixture).replace('/', '\\')))
    shader = (build / 'tests/dx12/compute_sm6.cs.cso').read_bytes()
    if shader[:4] != b'DXBC' or len(shader) < 40:
        raise SystemExit('expected a DXBC-wrapped DXIL fixture')
    malformed = {f'container-short-{size}': shader[:size] for size in (4, 8, 16, 31, 32)}
    for label, offset, value in (('bad-size', 24, len(shader) + 1),
                                  ('bad-count', 28, 0xffffffff),
                                  ('bad-offset', 32, 0xffffffff)):
        data = bytearray(shader)
        struct.pack_into('<I', data, offset, value)
        malformed[label] = data
    data = bytearray(shader)
    first_offset = struct.unpack_from('<I', data, 32)[0]
    struct.pack_into('<I', data, first_offset + 4, 0xffffffff)
    malformed['bad-part-size'] = data
    for label, data in malformed.items():
        fixture = output / (label + '.dxil')
        fixture.write_bytes(data)
        cases.append((label, 'invalid', converter, 'Z:' + str(fixture).replace('/', '\\')))
    for label, mode, library, fixture in cases:
        env['DXMT_METALIRCONVERTER_PATH'] = str(library)
        result = subprocess.run([str(stage / 'wine/bin/wine'), 'dx12_msc_unavailable.exe', mode, fixture],
                                cwd=stage, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        log = result.stdout.decode(errors='replace')
        (output / (label + '.log')).write_text(log)
        checks = [identity in log, str(stage / 'wine/lib/wine/x86_64-unix/winemetal.so') in log,
                  str(library) in log, 'MSC thunk ' + mode + ' PASS' in log,
                  any(('loaded l"' + path + '"') in line.lower() and ': builtin' in line.lower()
                      for line in log.splitlines())]
        if mode == 'incompatible':
            checks.append('missing symbol IRCompilerDestroy' in log and 'incompatible runtime' in log)
        if result.returncode or not all(checks):
            raise SystemExit(f'{label}: exit={result.returncode} checks={checks}; see {output / (label + ".log")}')
        print(label, 'PASS', flush=True)
    hashes.update(converter=hashlib.sha256(converter.read_bytes()).hexdigest(),
                  incomplete_runtime=hashlib.sha256(fake.read_bytes()).hexdigest())
    (output / 'summary.json').write_text(json.dumps({'artifacts_sha256': hashes, 'result': 'PASS'}, indent=2) + '\n')


if __name__ == '__main__':
    main()
