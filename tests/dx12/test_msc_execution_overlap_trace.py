#!/usr/bin/env python3
"""Parser regressions modeled on real Xcode 27.2 exports, not GPU measurements."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
from analyze_msc_execution_overlap_trace import analyze


def write_table(path, name, columns, rows):
    root = ET.Element('trace-query-result')
    node = ET.SubElement(root, 'node')
    schema = ET.SubElement(node, 'schema', name=name)
    for column in columns:
        ET.SubElement(ET.SubElement(schema, 'col'), 'mnemonic').text = column
    for values in rows:
        row = ET.SubElement(node, 'row')
        for column in columns:
            cell = ET.SubElement(row, 'cell')
            if column == 'process':
                ET.SubElement(cell, 'pid').text = str(values[column])
            else:
                cell.text = str(values[column])
    ET.ElementTree(root).write(path)


class TraceTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.paths = {name: self.root / (name + '.xml') for name in ('toc', 'gpu', 'encoders', 'submissions', 'points')}
        self.stdout = self.root / 'stdout.log'
        toc = ET.fromstring('''<trace-toc><run><info><summary><template-name>Metal System Trace</template-name></summary>
          <target><process pid="44" return-exit-status="0" arguments="dx12_msc_execution_overlap.exe msc_execution_overlap.cs.cso msc_execution_overlap.vs.cso msc_execution_overlap.ps.cso all 1 1024 normal trace"/>
          </target></info></run></trace-toc>''')
        ET.ElementTree(toc).write(self.paths['toc'])
        self.stdout.write_text('OVERLAP_BACKEND msc visibility=all iterations=1024\n' +
                               ''.join(f'OVERLAP_SAMPLE {i} gpu_ns=1000 encode_ms=1 submit_ms=1 exact_bytes=65536\n' for i in range(3)) +
                               'OVERLAP_PASS visibility=all samples=1 warmup=2\n')
        self.rows = {name: [] for name in ('gpu', 'encoders', 'submissions', 'points')}
        for sample in range(3):
            command = 100 + sample
            for kind, encoder in [('Compute', 2 * sample + 1), ('Render', 2 * sample + 2)]:
                self.rows['encoders'].append({'process': 44, 'cmdbuffer-id': command, 'encoder-id': encoder, 'encoder-label': kind + ' Command'})
            for stage, channel, offset, duration, encoder in [('Compute', 0x123459, 0, 10, 2 * sample + 1),
                                                            ('Vertex', 0x123457, 20, 10, 2 * sample + 2),
                                                            ('Fragment', 0x123458, 30, 5, 2 * sample + 2)]:
                start = 1000 + sample * 100 + offset
                sid = 1000 + sample * 3 + offset
                self.rows['gpu'].append({'process': 44, 'cmdbuffer-id': command, 'encoder-id': encoder,
                                         'gpu-submission-id': sid, 'state': 'Active', 'event-depth': 0,
                                         'channel-name': stage, 'start': start, 'duration': duration})
                self.rows['submissions'].append({'process': 44, 'pid': 44, 'cmdbuffer-id': command, 'encoder-id': encoder,
                                                 'gpu-submission-id': sid, 'num-coalasced-encoders': 0, 'accelerator-id': 2982})
                for function, timestamp in [(1, start), (2, start + duration)]:
                    self.rows['points'].append({'timestamp': timestamp, 'channel-id': channel, 'function': function,
                                                'slot-id': sid, 'gpu-submission-id': sid, 'accelerator-id': 2982})

    def run_trace(self):
        names = {'gpu': 'metal-gpu-intervals', 'encoders': 'metal-application-encoders-list',
                 'submissions': 'metal-gpu-submission-to-command-buffer-id', 'points': 'metal-gpu-execution-points'}
        for name, rows in self.rows.items():
            write_table(self.paths[name], names[name], list(rows[0]) if rows else [], rows)
        return analyze(self.paths['toc'], self.paths['gpu'], self.paths['encoders'], self.stdout, 'all', 1, 1024,
                       self.paths['submissions'], self.paths['points'])

    def test_layout_depth_has_same_timing(self):
        baseline = self.run_trace()
        self.rows['gpu'][1]['event-depth'] = 2
        self.assertEqual(self.run_trace()['workloads'], baseline['workloads'])

    def test_positive_overlap_uses_point_timing(self):
        self.rows['gpu'][1]['start'] = 1005
        self.rows['points'][2]['timestamp'] = 1005
        self.rows['points'][3]['timestamp'] = 1015
        self.assertEqual(self.run_trace()['workloads'][0]['compute_vertex_overlap_ns'], 5)

    def test_separate_intervals_keep_execution_gap(self):
        self.rows['gpu'][0]['duration'] = 4
        self.rows['gpu'].append({**self.rows['gpu'][0], 'start': 1006})
        self.rows['points'][1]['timestamp'] = 1004
        self.rows['points'].extend([{**self.rows['points'][0], 'timestamp': 1006},
                                   {**self.rows['points'][1], 'timestamp': 1010}])
        self.assertEqual(self.run_trace()['workloads'][0]['intervals_ns']['Compute'], [[1000, 1004], [1006, 1010]])

    def test_negative_layout_depth(self):
        self.rows['gpu'][0]['event-depth'] = -1
        with self.assertRaisesRegex(AssertionError, 'layout depth'):
            self.run_trace()

    def test_duplicate_gpu_interval(self):
        self.rows['gpu'].append(copy.deepcopy(self.rows['gpu'][0]))
        with self.assertRaisesRegex(AssertionError, 'disagree'):
            self.run_trace()

    def test_point_slot_mismatch(self):
        self.rows['points'][1]['slot-id'] += 1
        with self.assertRaisesRegex(AssertionError, 'unpaired'):
            self.run_trace()

    def test_cross_process_submission_collision(self):
        other = {**self.rows['submissions'][0], 'process': 99, 'pid': 99}
        self.rows['submissions'].append(other)
        with self.assertRaisesRegex(AssertionError, 'ambiguous.*submission'):
            self.run_trace()

    def test_missing_end_point(self):
        del self.rows['points'][1]
        with self.assertRaisesRegex(AssertionError, 'unpaired'):
            self.run_trace()

    def test_missing_mapping(self):
        del self.rows['submissions'][0]
        with self.assertRaisesRegex(AssertionError, 'submission'):
            self.run_trace()

    def test_wrong_mapping(self):
        self.rows['submissions'][0]['encoder-id'] = 2
        with self.assertRaises(AssertionError):
            self.run_trace()

    def test_coalesced_encoder(self):
        self.rows['submissions'][0]['num-coalasced-encoders'] = 2
        with self.assertRaisesRegex(AssertionError, 'coalesced'):
            self.run_trace()

    def test_interval_endpoint_disagrees(self):
        self.rows['gpu'][0]['duration'] += 1
        with self.assertRaisesRegex(AssertionError, 'disagree'):
            self.run_trace()

    def test_missing_channel(self):
        del self.rows['gpu'][1]
        del self.rows['points'][2:4]
        with self.assertRaisesRegex(AssertionError, 'all channel data'):
            self.run_trace()

    def test_wrong_channel(self):
        self.rows['points'][0]['channel-id'] = 0x123458
        self.rows['points'][1]['channel-id'] = 0x123458
        with self.assertRaises(AssertionError):
            self.run_trace()

    def test_duplicate_start(self):
        self.rows['points'].append(copy.deepcopy(self.rows['points'][0]))
        with self.assertRaisesRegex(AssertionError, 'duplicate'):
            self.run_trace()

    def test_foreign_interval_does_not_affect_result(self):
        baseline = self.run_trace()
        other = {**self.rows['gpu'][0], 'process': 99, 'start': 0, 'duration': 10000}
        self.rows['gpu'].append(other)
        self.assertEqual(self.run_trace()['workloads'], baseline['workloads'])


if __name__ == '__main__':
    unittest.main()
