#!/usr/bin/env python3
"""Compare MSC validation settings on two root signatures."""
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
    for n in ('stage','prefix','build','converter','output'): parser.add_argument('--'+n,type=Path,required=True)
    parser.add_argument('--processes',type=int,default=3)
    args=parser.parse_args()
    if not 2 <= args.processes <= 10: parser.error('processes must be 2..10')
    stage,prefix,build,converter=(getattr(args,n).resolve(strict=True) for n in ('stage','prefix','build','converter'))
    pairs=[('src/d3d12/d3d12.dll','d3d12.dll'),('src/dxgi/dxgi.dll','dxgi.dll'),
           ('src/winemetal/winemetal.dll','winemetal.dll'),('src/winemetal/unix/winemetal.so','wine/lib/wine/x86_64-unix/winemetal.so'),
           ('tests/dx12/dx12_msc_root_validation_compare.exe','dx12_msc_root_validation_compare.exe'),
           ('tests/dx12/metal_hlsl_dynamic_main.ps.cso','metal_hlsl_dynamic_main.ps.cso')]
    for src,dst in pairs:
        data=(build/src).read_bytes()
        if src in ('src/d3d12/d3d12.dll','src/dxgi/dxgi.dll') and data[64:80]==b'Wine builtin DLL': data=data[:64]+b' '*16+data[80:]
        if data != (stage/dst).read_bytes(): raise SystemExit('stage/build mismatch: '+dst)
    root=args.output.absolute(); root.mkdir(parents=True,exist_ok=False)
    repo=Path(__file__).resolve().parents[2]; wrapper=root/'validation-trace.dylib'
    subprocess.run(['xcrun','clang','-arch','x86_64','-dynamiclib','-I',str(repo/'include'),str(repo/'tests/dx12/msc_validation_trace.c'),
                    str(converter),'-Wl,-rpath,'+str(converter.parent),'-o',str(wrapper)],check=True,timeout=30)
    identities=[f'LOADED_CODE {dll} {symbol} {pe_export_bytes(stage/dll,symbol)}' for dll,symbol in
                [('d3d12.dll','D3D12CreateDevice'),('winemetal.dll','DXMTMSCCompileDXIL')]]
    env={k:v for k,v in os.environ.items() if not k.startswith(('DYLD_','DXMT_TEST_','MTL_DEBUG_','MTL_SHADER_VALIDATION'))}
    env.update(WINEPREFIX=str(prefix),WINEDLLPATH=str(stage/'wine/lib/wine'),WINEDLLOVERRIDES='d3d12=n;dxgi=n;winemetal=b',
               WINEDEBUG='+loaddll',DXMT_METALIRCONVERTER_PATH=str(wrapper),DYLD_PRINT_LIBRARIES='1')
    cmd=[str(stage/'wine/bin/wine'),'dx12_msc_root_validation_compare.exe','metal_hlsl_dynamic_main.ps.cso']
    samples=[]
    for n in range(args.processes):
        for mode in ('unset','zero','bound','dxil'):
            result=subprocess.run(cmd,cwd=stage,env={**env,'DXMT_TEST_VALIDATION_MODE':mode},stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT,timeout=180,text=True,errors='replace')
            log=result.stdout; (root/f'{mode}-{n}.log').write_text(log)
            rows=[(int(a),int(b),int(c),int(d),int(e),float(f)) for a,b,c,d,e,f in re.findall(
                r'^VALIDATION_RESULT (\d+) (\d+) (\d+) (\d+) (\d+) ([0-9.eE+-]+)\s*$',log,re.M)]
            target='omitted' if mode=='unset' else mode
            setters=re.findall(r'^VALIDATION_SET (\w+) (\d+)\s*$',log,re.M)
            loaded=[]
            for dll,kind in [('d3d12.dll','native'),('dxgi.dll','native'),('winemetal.dll','builtin')]:
                path=('Z:'+str(stage/dll).replace('/','\\')).replace('\\','\\\\').lower()
                loaded.append(any(('loaded l"'+path+'"') in line.lower() and ': '+kind in line.lower() for line in log.splitlines()))
            checks=[result.returncode==0, all(x in log for x in identities), all(loaded), str(wrapper) in log,
                    str(converter) in log, str(stage/'wine/lib/wine/x86_64-unix/winemetal.so') in log,
                    len(setters)==12 and all(a==target and b=='0' for a,b in setters),
                    [(a,b) for a,b,_,_,_,_ in rows]==[(valid,i) for valid in range(2) for i in range(6)],
                    all(c==0 and d==0 and e>0 for a,b,c,d,e,f in rows if a==1),
                    all(c!=0 and d!=0 and e==0 for a,b,c,d,e,f in rows if a==0),
                    'root validation comparison complete' in log]
            if mode=='bound': checks += [all(c==5 and d==5 and e==0 for a,b,c,d,e,f in rows if a==0),
                                         log.count('root signature error (IRErrorCode=4)')==6]
            if not all(checks): raise SystemExit(f'{mode}-{n}: failed {checks}, exit {result.returncode}; see {root}/{mode}-{n}.log')
            samples.append({'mode':mode,'index':n,'rows':rows}); print(f'{mode}-{n}: passed',flush=True)
    def stats(v):
        v=sorted(v); return {'n':len(v),'median_ms':statistics.median(v),'p95_ms':v[math.ceil(.95*len(v))-1],'min_ms':v[0],'max_ms':v[-1]}
    summaries={}
    for mode in ('unset','zero','bound','dxil'):
        for valid in (0,1):
            summaries[f'{mode}_{"valid" if valid else "missing_b0"}']=stats([f for o in samples if o['mode']==mode for a,b,c,d,e,f in o['rows'] if a==valid and b>0])
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    data={'samples':samples,'summaries':summaries,'converter_sha256':sha(converter),'wrapper_sha256':sha(wrapper),
          'artifacts':{dst:sha(stage/dst) for _,dst in pairs},'command':cmd,
          'hardware':subprocess.check_output(['sysctl','-n','machdep.cpu.brand_string'],text=True).strip(),
          'macos':subprocess.check_output(['sw_vers','-productVersion'],text=True).strip(),
          'wine':subprocess.check_output([str(stage/'wine/bin/wine'),'--version'],text=True,
                                          env={**os.environ,'WINEPREFIX':str(prefix)},stderr=subprocess.DEVNULL).strip(),
          'git_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
          'limitations':'same cbuffer fragment DXIL; repeated sizing calls on fresh MSC compilers; fixed mode order; first sample per root excluded; no GPU or game FPS measurement'}
    (root/'summary.json').write_text(json.dumps(data,indent=2)+'\n')
    (root/'summary.md').write_text('# MSC root validation comparison\n\n'+data['limitations']+'\n\n| case | n | median ms | p95 ms | min ms | max ms |\n| --- | ---: | ---: | ---: | ---: | ---: |\n'+
        ''.join(f"| {k} | {v['n']} | {v['median_ms']} | {v['p95_ms']} | {v['min_ms']} | {v['max_ms']} |\n" for k,v in summaries.items()))
    return 0

if __name__=='__main__': sys.exit(main())
