#!/usr/bin/env python3
"""Compare supported MSC GPU targets using isolated Metal HLSL pixel tests."""
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'prefix', 'build', 'converter', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    if not 2 <= args.repeats <= 10:
        parser.error('--repeats must be 2..10')
    stage, prefix, build, converter = (getattr(args, n).resolve(strict=True) for n in ('stage', 'prefix', 'build', 'converter'))
    pairs = [('src/winemetal/winemetal.dll', 'winemetal.dll'),
             ('src/winemetal/unix/winemetal.so', 'wine/lib/wine/x86_64-unix/winemetal.so')]
    pairs += [('tests/dx12/' + n, n) for n in ('dx12_metal_hlsl_gpu.exe','metal_hlsl_constants.ps.cso','metal_hlsl_framebuffer.ps.cso')]
    for src, dst in pairs:
        if (build / src).read_bytes() != (stage / dst).read_bytes():
            raise SystemExit('stage/build mismatch: ' + dst)
    root = args.output.absolute(); root.mkdir(parents=True, exist_ok=False)
    repo = Path(__file__).resolve().parents[2]
    wrapper = root / 'target-trace.dylib'
    subprocess.run(['xcrun','clang','-arch','x86_64','-dynamiclib','-I',str(repo / 'include'),
                    str(repo / 'tests/dx12/msc_gpu_target_trace.c'),str(converter),
                    '-Wl,-rpath,' + str(converter.parent),'-o',str(wrapper)],check=True,timeout=30)
    identity = 'LOADED_CODE winemetal.dll DXMTMSCCompileDXIL ' + pe_export_bytes(stage / 'winemetal.dll', 'DXMTMSCCompileDXIL')
    dll_path = ('Z:' + str(stage / 'winemetal.dll').replace('/', '\\')).replace('\\', '\\\\').lower()
    env = {k:v for k,v in os.environ.items() if not k.startswith(('DYLD_', 'DXMT_TEST_', 'MTL_DEBUG_', 'MTL_SHADER_VALIDATION'))}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / 'wine/lib/wine'), WINEDLLOVERRIDES='winemetal=b',
               WINEDEBUG='+loaddll', DXMT_METALIRCONVERTER_PATH=str(wrapper), DYLD_PRINT_LIBRARIES='1')
    cmd = [str(stage / 'wine/bin/wine'), 'dx12_metal_hlsl_gpu.exe', 'metal_hlsl_constants.ps.cso', 'metal_hlsl_framebuffer.ps.cso']
    cases = []
    for i in range(args.repeats):
        cases += [(f'gpu{family}-{i}', family, False) for family in (6,7)]
    cases += [('gpu6-validation',6,True), ('gpu7-validation',7,True), ('gpu9-probe',9,True)]
    observations = []
    for name, family, validation in cases:
        runenv = dict(env)
        if validation:
            runenv.update(MTL_DEBUG_LAYER='1', MTL_SHADER_VALIDATION='1', MTL_SHADER_VALIDATION_REPORT_TO_STDERR='1')
        result = subprocess.run(cmd + [f'--gpu{family}'], cwd=stage, env=runenv, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180, text=True, errors='replace')
        log = result.stdout; (root / (name + '.log')).write_text(log)
        loaded = [identity in log, str(wrapper) in log, str(converter) in log,
                  str(stage / 'wine/lib/wine/x86_64-unix/winemetal.so') in log,
                  any(('loaded l"' + dll_path + '"') in line.lower() and ': builtin' in line.lower() for line in log.splitlines())]
        if not all(loaded): raise SystemExit(f'{name}: identity failed {loaded}')
        if result.returncode == 77:
            if f'GPU_TARGET_SKIP unsupported {1000+family}' not in log and 'GPU_TARGET_SKIP missing MSC target API' not in log:
                raise SystemExit(f'{name}: missing explicit skip reason')
            observations.append({'case':name, 'status':'SKIP', 'reason':next(line for line in log.splitlines() if 'GPU_TARGET_SKIP' in line)})
            print(name + ': skipped', flush=True); continue
        flags = [int(x) for x in re.findall(r'^DXMT_TEST_GPU_TARGET (\d+)\s*$', log, re.M)]
        times = re.findall(r'^MSC_(SIZING|OUTPUT)_MS (constants|fetch) ([0-9.eE+-]+)(?: (\d+))?\s*$', log, re.M)
        gpu = [(int(i),int(a),int(b)) for i,a,b in re.findall(r'^GPU_NS (\d+) (\d+) (\d+)\s*$',log,re.M)]
        checks = [result.returncode == 0, len(flags) == 4, all(f == 1000+family for f in flags),
                  f'GPU_TARGET {1000+family}' in log, len(times) == 4,
                  [i for i,_,_ in gpu] == list(range(25)), all(a > 0 and b > 0 for _,a,b in gpu),
                  log.count('PIXEL 64,64,0,128') >= 26, log.count('MULTIPASS_PIXEL 64,64,0,128') == 26,
                  'PIXEL 255,0,0,255' in log, 'PIXEL 0,255,0,255' in log,
                  'Metal HLSL function constants and framebuffer fetch GPU passed' in log,
                  not re.search(r'(?i)metal (?:api|gpu|shader) validation (?:error|warning)|failed assertion',log)]
        if validation: checks += ['Metal API Validation Enabled' in log, 'Metal GPU Validation Enabled' in log]
        else: checks += ['Metal API Validation Enabled' not in log, 'Metal GPU Validation Enabled' not in log]
        if not all(checks): raise SystemExit(f'{name}: failed checks {checks}, exit={result.returncode}; see {root}/{name}.log')
        observations.append({'case':name,'family':family,'validation':validation,'status':'PASS','times':times,'gpu':gpu})
        print(name + ': passed', flush=True)
    if not any(o.get('family') == 6 and o['status'] == 'PASS' and not o.get('validation') for o in observations) or not any(o.get('family') == 7 and o['status'] == 'PASS' and not o.get('validation') for o in observations):
        raise SystemExit('comparison incomplete: one target was unsupported or unavailable')
    def stats(v):
        v=sorted(v)
        return {'count':len(v),'median':statistics.median(v),'p95':v[math.ceil(.95*len(v))-1],'min':v[0],'max':v[-1]}
    summaries={}
    for family in (6,7):
        group=[o for o in observations if o.get('family')==family and not o['validation'] and o['status']=='PASS']
        for shader in ('constants','fetch'):
            for phase in ('SIZING','OUTPUT'):
                v=[float(t) for o in group for p,s,t,size in o['times'] if p==phase and s==shader]
                summaries[f'gpu{family}_{shader}_{phase}_ms']=stats(v)
        for col,label in ((1,'fetch_gpu_ns'),(2,'multipass_gpu_ns')):
            summaries[f'gpu{family}_{label}']=stats([row[col] for o in group for row in o['gpu'][5:]])
    sha=lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    data={'observations':observations,'summaries':summaries,'converter_sha256':sha(converter),'wrapper_sha256':sha(wrapper),
          'artifacts':{dst:sha(stage / dst) for _,dst in pairs},'command':cmd,
          'hardware':subprocess.check_output(['sysctl','-n','machdep.cpu.brand_string'],text=True).strip(),
          'macos':subprocess.check_output(['sw_vers','-productVersion'],text=True).strip(),
          'wine':subprocess.check_output([str(stage / 'wine/bin/wine'),'--version'],text=True,
                                         env={**os.environ,'WINEPREFIX':str(prefix)},stderr=subprocess.DEVNULL).strip(),
          'git_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
          'limitations':'4x4 controlled fragment shaders, fixed target order, wrapper tracing, synchronous readback, 5 GPU warmups per process; no OS-target comparison, memory or frame-time measurement'}
    (root / 'summary.json').write_text(json.dumps(data,indent=2)+'\n')
    (root / 'summary.md').write_text('# MSC GPU target comparison\n\n'+data['limitations']+'\n\n| case | n | median | p95 | min | max |\n| --- | ---: | ---: | ---: | ---: | ---: |\n'+
        ''.join(f"| {k} | {v['count']} | {v['median']} | {v['p95']} | {v['min']} | {v['max']} |\n" for k,v in summaries.items()))
    return 0

if __name__ == '__main__':
    sys.exit(main())
