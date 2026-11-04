#!/usr/bin/env python3
"""Join xctrace GPU channel intervals to this probe's command encoders.

Export each schema separately: metal-gpu-intervals and
metal-application-encoders-list. Never interpret CPU encoding intervals as GPU
execution. Missing/mismatched events are errors, not zero overlap.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
from run_msc_execution_overlap import parse


def table(path, name):
    root = ET.parse(path).getroot()
    schemas = root.findall('.//schema')
    if len(schemas) != 1 or schemas[0].get('name') != name:
        raise AssertionError('unexpected export schema: ' + str(path))
    columns = [c.findtext('mnemonic') for c in schemas[0].findall('col')]
    ids = {e.get('id'): e for e in root.iter() if e.get('id')}

    def resolve(element):
        return ids[element.get('ref')] if element.get('ref') else element

    rows = []
    for row in root.findall('.//row'):
        if len(row) != len(columns):
            raise AssertionError('incomplete export row')
        cells = {column: resolve(element) for column, element in zip(columns, row)}
        process = cells.get('process')
        pid = process.find('pid') if process is not None else None
        rows.append({'pid': int(resolve(pid).text) if pid is not None else None,
                     'raw': {key: value.text for key, value in cells.items()},
                     'display': {key: value.get('fmt', value.text) for key, value in cells.items()}})
    return rows


def union(intervals):
    merged = []
    for start, end in sorted(intervals):
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(end, merged[-1][1])
        else:
            merged.append([start, end])
    return merged


def overlap(first, second):
    return sum(max(0, min(a1, b1) - max(a0, b0)) for a0, a1 in union(first) for b0, b1 in union(second))


def analyze(toc, gpu, encoders, stdout, visibility, samples, iterations):
    tree = ET.parse(toc).getroot()
    runs = tree.findall('run')
    if len(runs) != 1 or runs[0].findtext('info/summary/template-name') != 'Metal System Trace':
        raise AssertionError('expected one Metal System Trace run')
    target = runs[0].find('info/target/process')
    if target is None or target.get('return-exit-status') != '0':
        raise AssertionError('traced target did not exit successfully')
    pid = int(target.get('pid'))
    expected_args = ['dx12_msc_execution_overlap.exe'] + [f'msc_execution_overlap.{s}.cso' for s in ('cs', 'vs', 'ps')]
    expected_args += [visibility, str(samples), str(iterations), 'normal', 'trace']
    if target.get('arguments', '').split() != expected_args:
        raise AssertionError('trace workload arguments mismatch')
    parse(stdout.read_text(), visibility, samples, iterations)
    known = {}
    for row in table(encoders, 'metal-application-encoders-list'):
        if row['pid'] == pid:
            key = (row['raw']['cmdbuffer-id'], row['raw']['encoder-id'])
            if key in known:
                raise AssertionError('duplicate application encoder')
            known[key] = row['display']['encoder-label']
    workloads = {}
    for row in table(gpu, 'metal-gpu-intervals'):
        if row['pid'] != pid:
            continue
        data = row['raw']
        if data['state'] != 'Active':
            continue
        key = (data['cmdbuffer-id'], data['encoder-id'])
        if key not in known:
            raise AssertionError('GPU interval has no matching application encoder')
        label = known[key]
        if label.startswith('Blit Command'):
            continue  # Blits can execute on the Compute channel.
        # This baseline only accepts unambiguous, top-level channel events.
        # Do not discard nested events and report an incomplete zero overlap.
        if data['event-depth'] != '0':
            raise AssertionError('nested GPU channel attribution is unsupported')
        channel = data['channel-name']
        if not ((label.startswith('Compute Command') and channel == 'Compute') or
                (label.startswith('Render Command') and channel in ('Vertex', 'Fragment'))):
            raise AssertionError('unexpected encoder/channel mapping')
        start, duration = int(data['start']), int(data['duration'])
        if start < 0 or duration <= 0:
            raise AssertionError('invalid GPU interval')
        work = workloads.setdefault(key[0], {'Compute': [], 'Vertex': [], 'Fragment': [], 'encoders': set()})
        work[channel].append([start, start + duration])
        work['encoders'].add(key[1])
    if len(workloads) != samples + 2:
        raise AssertionError('incomplete GPU workload count')
    for work in workloads.values():
        if len(work['encoders']) != 2 or not all(work[stage] for stage in ('Compute', 'Vertex', 'Fragment')):
            raise AssertionError('expected one compute and one render encoder with all channel data')
    results = []
    for command, work in sorted(workloads.items(), key=lambda item: min(x[0] for x in item[1]['Compute'])):
        results.append({'command_buffer': command,
                        'intervals_ns': {stage: union(work[stage]) for stage in ('Compute', 'Vertex', 'Fragment')},
                        'compute_vertex_overlap_ns': overlap(work['Compute'], work['Vertex']),
                        'compute_fragment_overlap_ns': overlap(work['Compute'], work['Fragment']),
                        'compute_end_to_vertex_start_ns': min(x[0] for x in work['Vertex']) - max(x[1] for x in work['Compute'])})
    return {'schema': 1, 'pid': pid, 'visibility': visibility, 'iterations': iterations,
            'scope': 'GPU channel intervals joined to application encoders; not shader instruction/occupancy counters',
            'workloads': results, 'artifacts': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (toc, gpu, encoders, stdout)}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('toc', 'gpu', 'encoders', 'stdout', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--visibility', choices=('all', 'pixel'), required=True)
    parser.add_argument('--samples', type=int, required=True)
    parser.add_argument('--iterations', type=int, required=True)
    args = parser.parse_args()
    result = analyze(args.toc, args.gpu, args.encoders, args.stdout, args.visibility, args.samples, args.iterations)
    with args.output.open('x') as output:
        json.dump(result, output, indent=2)
        output.write('\n')
    print(f"Verified {len(result['workloads'])} GPU channel workloads for {args.visibility}")


if __name__ == '__main__':
    main()
