#!/usr/bin/env python3
"""Verify stage-in output retries or conversion-cache replay with GPU readback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
from run_msc_lod_experiment import pe_export_bytes
from run_msc_concurrent import parse_cache_trace


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_cache_replay(log, negative=None):
    expected = [(phase, layout) for phase in range(2) for layout in range(3)]
    begins = [(int(a), int(b), int(c)) for a, b, c in re.findall(
        r'^CACHE_GRAPHICS_BEGIN (\d+) (\d+) selected=(\d+)\s*$', log, re.M)]
    ends = [(int(a), int(b)) for a, b in re.findall(r'^CACHE_GRAPHICS_END (\d+) (\d+)\s*$', log, re.M)]
    if begins != [(a, b, 0 if negative == 'layout' and (a, b) == (1, 2) else b) for a, b in expected]:
        raise AssertionError('unexpected graphics replay cases or selected layouts')
    if ends != (expected[:-1] if negative else expected):
        raise AssertionError('missing graphics replay completions')
    snapshots = parse_cache_trace(log, entries=5, hits=13, misses=5)
    segments = re.split(r'^CACHE_GRAPHICS_BEGIN [^\n]+\n', log, flags=re.M)[1:]
    if len(segments) != 6:
        raise AssertionError('unexpected graphics replay markers')
    last_stage_in = 0
    for segment, (phase, layout) in zip(segments, expected):
        events = re.findall(r'MSC_CACHE event=(\w+)', segment)
        wanted = (['miss', 'insert'] * 3 if layout == 0 else ['miss', 'insert', 'hit', 'hit']) if phase == 0 else ['hit'] * 3
        calls = (3 if layout == 0 else 1) if phase == 0 else 0
        if events != wanted or segment.count('DXMT_MSC_TRACE_BEGIN') != calls or segment.count('DXMT_MSC_TRACE_END ok') != calls:
            raise AssertionError('graphics case did not use the expected conversion/cache path')
        stage_in = [int(n) for n in re.findall(r'MSC_CACHE [^\r\n]*? stage_in_bytes=(\d+)', segment)]
        if len(stage_in) != len(wanted) or (phase == 0 and stage_in[1] <= last_stage_in):
            raise AssertionError('new vertex key did not retain stage-in bytecode')
        if phase == 1 and any(n != last_stage_in for n in stage_in):
            raise AssertionError('stage-in storage changed on warm replay')
        last_stage_in = stage_in[-1]
        wrong = bool(negative) and (phase, layout) == (1, 2)
        rows = re.findall(r'^GS differential kind=triangle index=none pass=(\d) primitives=66 '
                          r'draw_instances=2 exact_bytes=3072 passed\s*$', segment, re.M)
        if rows != ([] if wrong else ['0', '1', '2']):
            raise AssertionError('graphics readback sequence was incomplete')
        if segment.count('GS replay/fenced reset/pending reset rejection/released refs passed') != int(not wrong):
            raise AssertionError('graphics submission-lifetime checks missing')
        failures = re.findall(r'^FAIL GS [^\r\n]*', segment, re.M)
        mismatch = ('FAIL GS kind=triangle index=none pass=0 byte=1 got=31 expected=79' if negative == 'layout' else
                    'FAIL GS kind=triangle index=none pass=0 byte=0 got=1 expected=0')
        if failures != ([mismatch] if wrong else []):
            raise AssertionError('wrong graphics output was not rejected at the intended byte')
        reflection = re.findall(r'^GS MSC reflection vertex_bytes=(\d+) max_primitives=(\d+) instances=1\s*$', segment, re.M)
        if len(reflection) != 1 or any(int(n) == 0 for n in reflection[0]):
            raise AssertionError('MSC geometry backend/reflection not verified')
    if log.count('MSC_CACHE_GRAPHICS layouts=3 phases=2 passed') != int(not negative):
        raise AssertionError('graphics replay final status mismatch')
    return {'layouts': [{'slot': 0, 'color_offset': 8, 'stride': 24},
                        {'slot': 3, 'color_offset': 8, 'stride': 32},
                        {'slot': 0, 'color_offset': 24, 'stride': 48}],
            'negative_control': negative, 'completed_cases': len(ends),
            'readbacks_per_case': 3, 'bytes_per_readback': 3072,
            'cache_snapshots': snapshots}


def validate_vertex_cache(log, negative=None):
    modes = ('packed', 'vertex', 'instance', 'instance-zero', 'indexed', 'indirect', 'lifetime')
    expected = [(phase, mode) for phase in range(2) for mode in modes]
    begins = [(int(a), b, int(c)) for a, b, c in re.findall(
        r'^CACHE_VERTEX_BEGIN (\d+) (\S+) wrong_stride=(\d+)\s*$', log, re.M)]
    ends = [(int(a), b) for a, b in re.findall(r'^CACHE_VERTEX_END (\d+) (\S+)\s*$', log, re.M)]
    if begins != [(phase, mode, int(negative == 'stride' and (phase, mode) == (1, 'lifetime')))
                  for phase, mode in expected]:
        raise AssertionError('unexpected vertex cache cases/control selection')
    if ends != (expected[:-1] if negative else expected):
        raise AssertionError('vertex cache case completion mismatch')
    snapshots = parse_cache_trace(log, entries=2, hits=34 if negative else 38, misses=2)
    if any(row['stage_in_bytes'] or row['stage_in_capacity_bytes'] for row in snapshots):
        raise AssertionError('ordinary vertex fetch unexpectedly retained stage-in')
    segments = re.split(r'^CACHE_VERTEX_BEGIN [^\n]+\n', log, flags=re.M)[1:]
    if len(segments) != len(expected):
        raise AssertionError('unexpected vertex cache markers')
    for segment, (phase, mode) in zip(segments, expected):
        first = (phase, mode) == (0, 'packed')
        wrong = bool(negative) and (phase, mode) == (1, 'lifetime')
        psos = (2 if wrong else 4) if mode == 'lifetime' else 1
        events = re.findall(r'MSC_CACHE event=(\w+)', segment)
        if events != (['miss', 'insert'] * 2 if first else ['hit'] * (2 * psos)):
            raise AssertionError('unexpected vertex conversion/cache events')
        if (segment.count('DXMT_MSC_TRACE_BEGIN') != (2 if first else 0) or
                segment.count('DXMT_MSC_TRACE_END ok') != (2 if first else 0)):
            raise AssertionError('unexpected conversion during vertex replay')
        creations = re.findall(r'^VERTEX_CACHE_PSO (\S+) (\d+) msc_vertex_fetch=1\s*$', segment, re.M)
        if creations != [(mode, str(i)) for i in range(1, psos + 1)]:
            raise AssertionError('ordinary MSC PSO creation/backend checks missing')
        rows = re.findall(r'^Vertex stride mode=(\S+) pass=(\d) exact_bytes=4096 passed\s*$', segment, re.M)
        if rows != ([] if wrong else [(mode, str(i)) for i in range(3)]):
            raise AssertionError('missing vertex byte-exact readbacks')
        if segment.count('Vertex stride replay/reset/released pipeline refs passed') != int(not wrong):
            raise AssertionError('missing vertex submission-lifetime checks')
        failures = re.findall(r'^FAIL stride [^\r\n]*', segment, re.M)
        mismatch = ('FAIL stride mode=lifetime pass=0 row=0 x=1 actual=1,2,3 expected=2,5,8'
                    if negative == 'stride' else
                    'FAIL stride mode=lifetime pass=0 row=0 x=0 actual=1,2,3 expected=0,2,3')
        if failures != ([mismatch] if wrong else []):
            raise AssertionError('vertex negative control failed at the wrong readback')
    if log.count('MSC_CACHE_VERTEX modes=7 phases=2 passed') != int(not negative):
        raise AssertionError('vertex replay final status mismatch')
    return {'modes': modes, 'negative_control': negative, 'completed_cases': len(ends),
            'pso_creations': 18 if negative else 20, 'readbacks_per_case': 3,
            'bytes_per_readback': 4096, 'cache_snapshots': snapshots}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("stage", "prefix", "build", "converter", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--mode", choices=("baseline", "grow", "metallib", "cache-replay", "vertex-cache"), required=True)
    parser.add_argument("--large-metallib", type=Path)
    args = parser.parse_args()
    if (args.mode == "metallib") != bool(args.large_metallib):
        parser.error("--large-metallib is required only for metallib mode")
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                       for name in ("stage", "prefix", "build", "converter"))
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    repository = Path(__file__).resolve().parents[2]
    pairs = (("src/d3d12/d3d12.dll", "d3d12.dll"), ("src/dxgi/dxgi.dll", "dxgi.dll"),
             ("src/winemetal/winemetal.dll", "winemetal.dll"),
             ("src/winemetal/unix/winemetal.so", "wine/lib/wine/x86_64-unix/winemetal.so"),
             ("tests/dx12/dx12_graphics_sm6.exe", "dx12_graphics_sm6.exe"))
    vertex_cache = args.mode == 'vertex-cache'
    fixtures = ([f'vertex_stride.{stage_name}.cso' for stage_name in ('vs', 'ps')] if vertex_cache else
                [f'geometry_differential_3.{stage_name}.cso' for stage_name in ('vs', 'ps', 'gs')])
    pairs += tuple(('tests/dx12/' + name, name) for name in fixtures)
    for relative, deployed in pairs:
        built = (build / relative).read_bytes()
        if relative in ("src/d3d12/d3d12.dll", "src/dxgi/dxgi.dll") and built[64:80] == b"Wine builtin DLL":
            built = built[:64] + b" " * 16 + built[80:]
        if built != (stage / deployed).read_bytes():
            raise AssertionError(f"stage/build mismatch: {relative}")
    if (stage / 'wine/lib/wine/x86_64-windows/winemetal.dll').read_bytes() != (stage / 'winemetal.dll').read_bytes():
        raise AssertionError('WineMetal builtin/app-local mismatch')
    wrapper = output / "wrapper.dylib"
    subprocess.run(["xcrun", "clang", "-arch", "x86_64", "-dynamiclib", "-I", str(repository / "include"),
                    str(repository / "tests/dx12/msc_conversion_trace_wrapper.c"), str(converter),
                    "-Wl,-rpath," + str(converter.parent), "-o", str(wrapper)], check=True, timeout=30)
    symbols = (("d3d12.dll", "D3D12CreateDevice"), ("winemetal.dll", "DXMTMSCPatchSamplerLOD"))
    identities = [f"LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}" for dll, symbol in symbols]
    env = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_") and
           not key.startswith("DXMT_TEST_TRACE_")}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / "wine/lib/wine"),
               WINEDLLOVERRIDES="d3d12=n;dxgi=n;winemetal=b", WINEDEBUG="+loaddll",
               DXMT_METALIRCONVERTER_PATH=str(wrapper), DXMT_TEST_PRINT_LOADED_CODE="1",
               DXMT_SHADER_CACHE_PATH=str(output / "cache"), DXMT_LOG_PATH=str(output),
               DXMT_MSC_CACHE_TRACE="1" if args.mode in ("cache-replay", "vertex-cache") else "0", DXMT_LOG_LEVEL="info",
               MTL_DEBUG_LAYER="1", MTL_SHADER_VALIDATION="1", MTL_SHADER_VALIDATION_REPORT_TO_STDERR="1",
               DYLD_PRINT_LIBRARIES="1")
    env.pop("DXMT_TEST_TRACE_GROW_ENTRY", None)
    env.pop("DXMT_TEST_TRACE_LATE_IMAGE", None)
    if args.mode == "grow":
        env["DXMT_TEST_TRACE_GROW_STAGE_IN"] = "1"
    else:
        env.pop("DXMT_TEST_TRACE_GROW_STAGE_IN", None)
    if args.mode == "metallib":
        small = args.large_metallib.resolve(strict=True)
        verifier = output / "verify-metallib"
        subprocess.run(["xcrun", "clang", "-arch", "x86_64", "-framework", "Foundation", "-framework", "Metal",
                        str(repository / "tests/dx12/validate_test_metallib.m"), "-o", str(verifier)],
                       check=True, timeout=30)
        result = subprocess.run([str(verifier), str(small)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=30, check=True)
        (output / "verify-metallib.log").write_bytes(result.stdout)
        env["DXMT_TEST_TRACE_LARGE_METALLIB"] = str(small)
    else:
        env.pop("DXMT_TEST_TRACE_LARGE_METALLIB", None)
    command = [str(stage / "wine/bin/wine"), "dx12_graphics_sm6.exe", "--gs-differential",
               "geometry_differential_3.vs.cso", "geometry_differential_3.ps.cso",
               "geometry_differential_3.gs.cso", "triangle", "none"]
    if args.mode == "cache-replay":
        command = command[:6]
        command[2] = '--gs-cache-replay'
    elif vertex_cache:
        command = command[:2] + ['--vertex-cache-replay', *fixtures]
    results = []
    cases = [("readback", [], 0), ("wrong", ["--wrong-expected"], 1)]
    if args.mode == 'cache-replay':
        cases.append(('wrong-layout', ['--wrong-layout'], 1))
    elif vertex_cache:
        cases.append(('wrong-stride', ['--wrong-stride'], 1))
    for label, option, expected_exit in cases:
        runenv = dict(env, DXMT_SHADER_CACHE_PATH=str(output / (label + '-cache')))
        result = subprocess.run(command + option, cwd=stage, env=runenv, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=120)
        log = result.stdout.decode(errors="replace")
        (output / (label + ".log")).write_text(log)
        loaded = []
        for dll, load_type in (("d3d12.dll", "native"), ("dxgi.dll", "native"), ("winemetal.dll", "builtin")):
            path = ("Z:" + str(stage / dll).replace("/", "\\")).replace("\\", "\\\\").lower()
            loaded.append(any(('loaded l"' + path + '"') in line.lower() and (": " + load_type) in line.lower()
                              for line in log.splitlines()))
        if (result.returncode != expected_exit or any(item not in log for item in identities) or
                not all(loaded) or
                str(stage / "wine/lib/wine/x86_64-unix/winemetal.so") not in log or str(wrapper) not in log or
                str(converter) not in log or "Metal API Validation Enabled" not in log or
                "Metal GPU Validation Enabled" not in log or
                re.search(r"(?i)(metal (?:api|gpu|shader) validation (?:error|warning)|validation error:|invalid metal usage:|failed assertion)", log)):
            raise AssertionError(f"{label}: execution, identity or validation failed")
        replay = None
        if args.mode == 'cache-replay':
            replay = validate_cache_replay(log, None if label == 'readback' else 'layout' if label == 'wrong-layout' else 'reference')
        elif vertex_cache:
            replay = validate_vertex_cache(log, None if label == 'readback' else 'stride' if label == 'wrong-stride' else 'reference')
        elif label == "readback" and (log.count("exact_bytes=3072 passed") != 3 or "released refs passed" not in log):
            raise AssertionError("exact geometry readback did not pass")
        if label == "wrong" and not vertex_cache and "FAIL GS kind=triangle index=none pass=0 byte=0 got=1 expected=0" not in log:
            raise AssertionError("deliberately wrong readback was not rejected")
        injected = "DXMT_MSC_TRACE_LARGE_STAGE_IN" in log
        if injected != (args.mode == "grow"):
            raise AssertionError(f"{label}: stage-in size control was not observed")
        small_injected = "DXMT_MSC_TRACE_LARGE_METALLIB" in log
        if small_injected != (args.mode == "metallib"):
            raise AssertionError(f"{label}: metallib size control was not observed")
        if log.count("DXMT_MSC_TRACE_END ok") < (2 if vertex_cache else 4 if injected or small_injected else 3):
            raise AssertionError(f"{label}: expected compilation and retry count was not observed")
        results.append({"case": label, "exit": result.returncode, "stage_in_injected": injected,
                        "metallib_injected": small_injected,
                        "successful_conversions": log.count("DXMT_MSC_TRACE_END ok"),
                        "cache_replay": replay})
    (output / "summary.json").write_text(json.dumps({"mode": args.mode, "converter_sha256": sha(converter),
        "wrapper_sha256": sha(wrapper), "artifacts": {path: sha(stage / path) for _, path in pairs},
        "fixtures": {name: sha(stage / name) for name in fixtures},
        "large_metallib_sha256": sha(small) if args.mode == "metallib" else None,
        "command": command, "prefix": str(prefix), "build": str(build),
        "hardware": subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
        "macos": subprocess.check_output(['sw_vers', '-productVersion'], text=True).strip(),
        "limitations": ("Controlled ordinary vertex-fetch modes; no native stride-variant cache hit counters, arbitrary shader/layout, "
                        "cross-device, persistent-cache or performance coverage" if vertex_cache else
                        "Controlled GS input layouts; no TS/mesh/cross-device/persistent-cache coverage or performance claim"),
        "results": results}, indent=2) + "\n")
    print("MSC stage-in", args.mode, "passed:", output)


if __name__ == "__main__":
    main()
