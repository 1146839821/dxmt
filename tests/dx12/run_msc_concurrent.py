#!/usr/bin/env python3
"""Measure MSC concurrency, warm reuse and opt-in cache-retained storage."""
import argparse
import hashlib
import json
import os
import re
import statistics
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from run_msc_lod_experiment import pe_export_bytes


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_process_memory(log, phases=('before', 'concurrent', 'repeat')):
    memory = {}
    for phase in phases:
        rows = re.findall(r'^PROCESS_MEMORY ' + phase + r' ([^\r\n]*)', log, re.M)
        if len(rows) != 1:
            raise AssertionError(f'expected one {phase} process-memory observation, got {len(rows)}')
        row = rows[0].strip()
        if row == 'NOT_AVAILABLE':
            memory[phase] = None
            continue
        if not re.fullmatch(r'\d+ \d+', row):
            raise AssertionError(f'invalid {phase} process-memory observation')
        working_set, pagefile_usage = map(int, row.split())
        if working_set == 0:
            raise AssertionError(f'zero {phase} working set must be reported as NOT_AVAILABLE')
        memory[phase] = {'working_set_bytes': working_set, 'pagefile_usage_bytes': pagefile_usage}
    return memory


def parse_cache_trace(log, entries, hits, misses):
    fields = ('entries', 'metallib_bytes', 'metallib_capacity_bytes', 'stage_in_bytes',
              'stage_in_capacity_bytes', 'entry_point_chars', 'entry_point_capacity_chars',
              'mapped_value_bytes', 'buckets')
    snapshots = []
    current = None
    for row in re.findall(r'MSC_CACHE ([^\r\n]*)', log):
        parts = row.split()
        if len(parts) != len(fields) + 1 or any(part.count('=') != 1 for part in parts):
            raise AssertionError('malformed cache trace')
        values = dict(part.split('=') for part in parts)
        if set(values) != {'event', *fields} or any(not values[key].isdigit() for key in fields):
            raise AssertionError('invalid cache trace fields')
        event = values.pop('event')
        if event not in ('miss', 'hit', 'hit_after_wait', 'insert'):
            raise AssertionError('unexpected cache event: ' + event)
        data = {key: int(value) for key, value in values.items()}
        for size, capacity in (('metallib_bytes', 'metallib_capacity_bytes'),
                               ('stage_in_bytes', 'stage_in_capacity_bytes'),
                               ('entry_point_chars', 'entry_point_capacity_chars')):
            if data[size] > data[capacity]:
                raise AssertionError('cache size exceeds capacity')
        if current is None:
            if event != 'miss' or any(data[key] for key in fields if key != 'buckets'):
                raise AssertionError('cache trace must begin with an empty miss')
        elif event == 'insert':
            if data['entries'] != current['entries'] + 1 or any(
                    data[key] < current[key] for key in fields if key != 'buckets'):
                raise AssertionError('cache insert did not add exactly one entry')
            if data['metallib_bytes'] == current['metallib_bytes']:
                raise AssertionError('cache insert retained no metallib')
        elif data != current:
            raise AssertionError('cache changed without insertion')
        snapshots.append({'event': event, **data})
        current = data
    if (not snapshots or current['entries'] != entries or
            sum(s['event'] == 'insert' for s in snapshots) != entries or
            sum(s['event'] in ('hit', 'hit_after_wait') for s in snapshots) != hits or
            sum(s['event'] == 'miss' for s in snapshots) != misses):
        raise AssertionError('cache trace event counts do not match the workload')
    return snapshots


def prepare_corpus(repository, output, wine, prefix, count, gpu=False):
    dxc = repository / 'tools/dxc/bin/x64/dxc.exe'
    source = repository / 'tests/dx12' / (
        'compute_sm6_threadgroup_coherency.hlsl' if gpu else 'shader_embedded_compute.hlsl')
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('DYLD_', 'DXMT_', 'WINE'))}
    env.update(WINEPREFIX=str(prefix), WINEDEBUG='-all')
    paths, hashes, expected_hashes = [], [], []
    windows_path = lambda p: 'Z:' + str(p).replace('/', '\\')
    for i in range(count):
        path = output / f'corpus-{i + 1}.cso'
        define = f'DXMT_TEST_CACHE_VARIANT={i}' if gpu else f'DXMT_TEST_THREADS={i + 1}'
        command = [str(wine), str(dxc), '-E', 'main' if gpu else 'cs_main', '-T', 'cs_6_0', '-O3',
                   '-D', define, str(source), '-Fo', str(path)]
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=120)
        (output / f'dxc-{i + 1}.log').write_bytes(result.stdout)
        result.check_returncode()
        if gpu:
            fixture = output / f'fixture-{i}'
            subprocess.run([sys.executable, str(repository / 'tests/dx12/make_threadgroup_coherency_fixtures.py'),
                            str(fixture), '--variant', str(i)], check=True, timeout=30)
            if i and (fixture / 'inputs.bin').read_bytes() != (output / 'fixture-0/inputs.bin').read_bytes():
                raise AssertionError('GPU variants must share identical inputs')
            paths.append(windows_path(path) + '\t' + windows_path(fixture / 'expected.bin'))
            expected_hashes.append(sha(fixture / 'expected.bin'))
        else:
            paths.append(windows_path(path))
        hashes.append(sha(path))
    if len(set(hashes)) != count:
        raise AssertionError('corpus shaders are not distinct')
    manifest = output / 'corpus.txt'
    manifest.write_text('\n'.join(paths) + '\n')
    if gpu and len(set(expected_hashes)) != count:
        raise AssertionError('GPU variant reference outputs must differ')
    return windows_path(manifest), {'shader_sha256': hashes, 'source_sha256': sha(source),
                                    'dxc_sha256': sha(dxc),
                                    'dxcompiler_sha256': sha(dxc.with_name('dxcompiler.dll')),
                                    'profile': 'cs_6_0', 'optimization': '-O3',
                                    'threads_x': 64 if gpu else list(range(1, count + 1)),
                                    'variant_values': list(range(count)) if gpu else None,
                                    'oracle_generator_sha256': sha(repository / 'tests/dx12/make_threadgroup_coherency_fixtures.py') if gpu else None,
                                    'expected_sha256': expected_hashes,
                                    'input_sha256': sha(output / 'fixture-0/inputs.bin') if gpu else None}


def validate_gpu_corpus(log, count, negative):
    expected = [(phase, binding, i) for phase in range(2)
                for binding in ('root', 'table') for i in range(count)]
    begins = re.findall(r'^CACHE_GPU_BEGIN (\d+) (root|table) (\d+) shader=(\d+)\s*$', log, re.M)
    ends = [(int(a), b, int(c)) for a, b, c in
            re.findall(r'^CACHE_GPU_END (\d+) (root|table) (\d+)\s*$', log, re.M)]
    if [(int(a), b, int(c)) for a, b, c, _ in begins] != expected:
        raise AssertionError('missing GPU cache replay case')
    if ends != (expected[:-1] if negative else expected):
        raise AssertionError('unexpected GPU cache replay completion')
    for index, (phase, binding, variant, selected) in enumerate(begins):
        want = 0 if negative == 'variant' and index == len(begins) - 1 else int(variant)
        if int(selected) != want:
            raise AssertionError('unexpected GPU shader selection')
    segments = re.split(r'^CACHE_GPU_BEGIN [^\n]+\n', log, flags=re.M)[1:]
    if len(segments) != len(expected):
        raise AssertionError('unexpected GPU cache replay marker')
    for index, (segment, (phase, binding, _)) in enumerate(zip(segments, expected)):
        cold = phase == 0
        events = re.findall(r'MSC_CACHE event=(\w+)', segment)
        if events != (['miss', 'insert'] if cold else ['hit']):
            raise AssertionError('GPU case did not use the expected cold/warm cache path')
        if segment.count('DXMT_MSC_TRACE_END ok') != int(cold):
            raise AssertionError('unexpected conversion during GPU cache replay')
        rows = re.findall(r'MSC numeric kind=int64 binding=' + binding +
                          r' pass=(\d) cases=32 exact_bytes=3072 guards=64 passed', segment)
        wrong = bool(negative) and index == len(expected) - 1
        if rows != ([] if wrong else ['0', '1', '2']):
            raise AssertionError('missing byte-exact GPU output/guard checks')
        if segment.count('Numeric closed-list replay/fenced reset/pending reset rejection/release passed') != int(not wrong):
            raise AssertionError('missing GPU submission-lifetime checks')
        mismatch = 'FAIL numeric kind=int64 binding=table pass=0 byte=32'
        if (mismatch in segment) != wrong or ('FAIL numeric' in segment) != wrong:
            raise AssertionError('negative control did not fail at the intended readback')
    if ('Metal API Validation Enabled' not in log or 'Metal GPU Validation Enabled' not in log or
            re.search(r'(?i)metal (?:api|gpu|shader) validation (?:error|warning)|failed assertion', log)):
        raise AssertionError('GPU validation missing or reported an error')
    return {'variants': count, 'layouts': ['root', 'table'], 'phases': ['cold', 'cache_hit'],
            'negative_control': negative, 'successful_case_count': len(ends),
            'readbacks_per_successful_case': 3, 'bytes_per_readback': 3072,
            'guard_bytes_per_readback': 64, 'api_and_shader_validation': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("stage", "prefix", "build", "converter", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--expected-calls", type=int, required=True)
    parser.add_argument("--fail-first", action="store_true")
    parser.add_argument("--two-shaders", action="store_true")
    parser.add_argument("--force-serial", action="store_true")
    parser.add_argument("--no-delay", action="store_true", help="omit the test-only compiler delay")
    parser.add_argument("--cache-trace", action="store_true", help="scan cache storage; perturbs timing")
    parser.add_argument("--corpus-count", type=int, default=0,
                        help="2-64 distinct compute shaders, then replay; enables cache tracing")
    parser.add_argument("--gpu-corpus", action="store_true",
                        help="resource-using variants, both root layouts and GPU readback on replay")
    parser.add_argument("--gpu-negative", choices=('reference', 'variant'),
                        help="deliberately fail the final warm GPU case")
    args = parser.parse_args()
    if args.gpu_corpus and not args.corpus_count:
        parser.error("--gpu-corpus requires --corpus-count")
    if args.gpu_negative and not args.gpu_corpus:
        parser.error("--gpu-negative requires --gpu-corpus")
    if args.corpus_count and not 2 <= args.corpus_count <= 64:
        parser.error("--corpus-count must be between 2 and 64")
    if args.corpus_count and (args.fail_first or args.two_shaders or args.force_serial or args.no_delay):
        parser.error("corpus mode is separate from concurrent timing/failure controls")
    cache_trace = args.cache_trace or bool(args.corpus_count)
    injected_delay = not (args.no_delay or args.corpus_count)
    if args.fail_first and args.two_shaders:
        parser.error("--fail-first and --two-shaders are separate controls")
    if args.force_serial and not args.two_shaders:
        parser.error("--force-serial requires --two-shaders")
    if args.no_delay and (args.fail_first or args.two_shaders or args.force_serial):
        parser.error("--no-delay is only supported by the identical-shader timing case")
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                       for name in ("stage", "prefix", "build", "converter"))
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    repository = Path(__file__).resolve().parents[2]
    probe = 'dx12_compute_sm6.exe' if args.gpu_corpus else 'dx12_msc_concurrent.exe'
    pairs = (("src/d3d12/d3d12.dll", "d3d12.dll"), ("src/dxgi/dxgi.dll", "dxgi.dll"),
             ("src/winemetal/winemetal.dll", "winemetal.dll"),
             ("src/winemetal/unix/winemetal.so", "wine/lib/wine/x86_64-unix/winemetal.so"),
             ("tests/dx12/" + probe, probe),
             ("tests/dx12/shader_embedded.compute.cs.cso", "shader_embedded.compute.cs.cso"))
    if args.two_shaders:
        pairs += (("tests/dx12/shader_embedded.compute.second.cs.cso", "shader_embedded.compute.second.cs.cso"),)
    for relative, deployed in pairs:
        built = (build / relative).read_bytes()
        if relative in ("src/d3d12/d3d12.dll", "src/dxgi/dxgi.dll") and built[64:80] == b"Wine builtin DLL":
            built = built[:64] + b" " * 16 + built[80:]
        if built != (stage / deployed).read_bytes():
            raise AssertionError(f"stage/build mismatch: {relative}")
    corpus_identity = None
    corpus_manifest = None
    if args.corpus_count:
        corpus_manifest, corpus_identity = prepare_corpus(
            repository, output, stage / 'wine/bin/wine', prefix, args.corpus_count, args.gpu_corpus)
    wrapper = output / "wrapper.dylib"
    subprocess.run(["xcrun", "clang", "-arch", "x86_64", "-dynamiclib", "-I", str(repository / "include"),
                    str(repository / "tests/dx12/msc_conversion_trace_wrapper.c"), str(converter),
                    "-Wl,-rpath," + str(converter.parent), "-o", str(wrapper)], check=True, timeout=30)
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("DYLD_", "DXMT_TEST_", "MTL_DEBUG_", "MTL_SHADER_VALIDATION"))}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / "wine/lib/wine"),
               WINEDLLOVERRIDES="d3d12=n;dxgi=n;winemetal=b", WINEDEBUG="+loaddll",
               DXMT_METALIRCONVERTER_PATH=str(wrapper), DYLD_PRINT_LIBRARIES="1",
               DXMT_SHADER_CACHE_PATH=str(output / "cache"), DXMT_LOG_PATH=str(output),
               DXMT_MSC_CACHE_TRACE="1" if cache_trace else "0", DXMT_LOG_LEVEL="info")
    if args.gpu_corpus:
        env.update(MTL_DEBUG_LAYER="1", MTL_SHADER_VALIDATION="1", MTL_SHADER_VALIDATION_REPORT_TO_STDERR="1")
    if injected_delay:
        env["DXMT_TEST_TRACE_DELAY"] = "1"
    if args.fail_first:
        env["DXMT_TEST_TRACE_FAIL_FIRST"] = "1"
    if args.force_serial:
        env["DXMT_TEST_TRACE_FORCE_SERIAL"] = "1"
    command = [str(stage / "wine/bin/wine"), "dx12_msc_concurrent.exe", "shader_embedded.compute.cs.cso"]
    if args.corpus_count:
        command = [str(stage / "wine/bin/wine"), "dx12_msc_concurrent.exe", "--corpus", corpus_manifest]
    if args.gpu_corpus:
        command = [str(stage / "wine/bin/wine"), probe, corpus_manifest, "--threadgroup-cache-replay",
                   'Z:' + str(output / 'fixture-0/inputs.bin').replace('/', '\\')]
        if args.gpu_negative:
            command.append('--wrong-expected' if args.gpu_negative == 'reference' else '--wrong-variant')
    if args.fail_first:
        command.append("--fail-first")
    if args.two_shaders:
        command.extend(("--second", "shader_embedded.compute.second.cs.cso"))
    result = subprocess.run(command,
                            cwd=stage, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    log = result.stdout.decode(errors="replace").replace("\r\n", "\n")
    (output / "run.log").write_text(log)
    identities = [f"LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}" for dll, symbol in
                  (("d3d12.dll", "D3D12CreateDevice"), ("winemetal.dll", "DXMTMSCPatchSamplerLOD"))]
    loaded = []
    for dll, load_type in (("d3d12.dll", "native"), ("dxgi.dll", "native"), ("winemetal.dll", "builtin")):
        path = ("Z:" + str(stage / dll).replace("/", "\\")).replace("\\", "\\\\").lower()
        loaded.append(any(('loaded l"' + path + '"') in line.lower() and (": " + load_type) in line.lower()
                          for line in log.splitlines()))
    calls = log.count("DXMT_MSC_TRACE_END ok")
    timings = {}
    for phase in (() if args.corpus_count else ("CONCURRENT", "REPEAT")):
        matches = re.findall(r'^PSO_' + phase + r'_NS (\d+) (\d+)\s*$', log, re.M)
        if [int(index) for index, _ in matches] != list(range(8)):
            raise AssertionError(f"missing {phase} PSO timing samples")
        values = [int(value) for _, value in matches]
        if any(value <= 0 for value in values):
            raise AssertionError(f"invalid {phase} PSO timing sample")
        ordered = sorted(values)
        timings[phase.lower()] = {"samples_ns": values, "median_ns": statistics.median(values),
                                  "p95_nearest_rank_ns": ordered[-1],
                                  "min_ns": ordered[0], "max_ns": ordered[-1]}
    gpu_replay = None
    if args.gpu_corpus:
        gpu_replay = validate_gpu_corpus(log, args.corpus_count, args.gpu_negative)
        after_concurrent = log.split("CACHE_GPU_BEGIN 1 root 0 shader=0\n", 1)[1]
        memory = None
    elif args.corpus_count:
        actual = [(int(a), int(b)) for a, b in re.findall(r'^CORPUS_PSO (\d+) (\d+)\s*$', log, re.M)]
        if actual != [(a, b) for a in range(2) for b in range(args.corpus_count)]:
            raise AssertionError('missing corpus PSO creations')
        after_concurrent = log.split(f"CORPUS_PSO 0 {args.corpus_count - 1}\n", 1)[1]
        memory = parse_process_memory(log, ('before', 'corpus', 'repeat'))
    else:
        after_concurrent = log.split("PSO_CONCURRENT_NS 7 ", 1)[1]
        memory = parse_process_memory(log)
    active = max_active = 0
    for line in log.splitlines():
        if line == "DXMT_MSC_TRACE_BEGIN":
            active += 1
            max_active = max(max_active, active)
        elif line.startswith("DXMT_MSC_TRACE_END ") or line == "DXMT_MSC_TRACE_FORCED_FAILURE":
            active -= 1
    expected_failures = 1 if args.fail_first else 0
    marker = (f"MSC_CORPUS shaders={args.corpus_count} passes=2" if args.corpus_count else
              f"concurrent MSC PSOs=8 failures={expected_failures}")
    expected_exit = int(bool(args.gpu_negative))
    if args.gpu_corpus:
        marker = ('FAIL numeric kind=int64 binding=table pass=0 byte=32' if args.gpu_negative else
                  f'MSC_CACHE_GPU variants={args.corpus_count} layouts=2 phases=2 passed')
    cache_snapshots = []
    if cache_trace:
        entries = (2 * args.corpus_count if args.gpu_corpus else
                   args.corpus_count or (2 if args.two_shaders else 1))
        successes = 2 * entries if args.corpus_count else 16 - expected_failures
        cache_snapshots = parse_cache_trace(log, entries, successes - entries, entries + expected_failures)
    elif 'MSC_CACHE ' in log:
        raise AssertionError('cache tracing was enabled unexpectedly')
    if (result.returncode != expected_exit or marker not in log or
            any(value not in log for value in identities) or not all(loaded) or
            str(stage / "wine/lib/wine/x86_64-unix/winemetal.so") not in log or
            str(wrapper) not in log or str(converter) not in log or calls != args.expected_calls or
            "DXMT_MSC_TRACE_BEGIN" in after_concurrent or
            log.count("DXMT_MSC_TRACE_FORCED_FAILURE") != expected_failures or active != 0 or
            (args.two_shaders and not args.force_serial and max_active < 2) or
            (args.force_serial and max_active != 1)):
        raise AssertionError(f"concurrent test failed: exit={result.returncode}, compile calls={calls}")
    (output / "summary.json").write_text(json.dumps({"compile_calls": calls, "max_overlapping_calls": max_active,
        "exit": result.returncode, "injected_delay": injected_delay, "pso_creation_timing": timings,
        "cache_trace_enabled": cache_trace, "cache_snapshots": cache_snapshots,
        "cache_storage_scope": "vector capacity and mapped values; excludes allocator/node/key overhead, GPU memory and PSO copies; string capacity may be inline",
        "corpus": corpus_identity, "gpu_cache_replay": gpu_replay, "expected_exit": expected_exit,
        "process_memory": memory,
        "process_memory_source": None if args.gpu_corpus else "GetProcessMemoryInfo / PROCESS_MEMORY_COUNTERS",
        "process_memory_scope": "NOT_MEASURED" if args.gpu_corpus else
                                "whole process; Wine-dependent counters, not MSC cache or GPU memory",
        "converter_sha256": sha(converter), "wrapper_sha256": sha(wrapper),
        "artifacts": {path: sha(stage / path) for _, path in pairs}}, indent=2) + "\n")
    label = 'expected GPU mismatch verified' if args.gpu_negative else marker
    print(f"{label}: {calls} real MSC compilation calls")


if __name__ == "__main__":
    main()
