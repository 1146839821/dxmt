import unittest
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

sys.dont_write_bytecode = True
import dx12_fl12_gate as gate


class GateTests(unittest.TestCase):
    def probes(self, output="", status=gate.PASS):
        return {name: {"status": status, "output": output}
                for name in ("feature_support", "shader_validation", "shader_container")}

    def test_all_statuses(self):
        for status in gate.STATUSES:
            self.assertEqual(gate.aggregate([gate.row("case", status, "")]), status)
        for rows in ([], [gate.row("case", "SKIP", "")]):
            with self.assertRaises(ValueError):
                gate.aggregate(rows)
        self.assertEqual(gate.aggregate([gate.row("a", gate.FAIL, ""), gate.row("b", gate.PASS, "")]), gate.FAIL)

    def test_api_only_cannot_pass(self):
        output = "options: tiled=2 binding=3 stencilRef=1 logicOp=1 typedUAV=1 ROV=1 conservative=3 heap=2"
        report = gate.build_report(self.probes(output), "normal")
        for name in ("FL12_0_GATE", "FL12_1_GATE"):
            self.assertNotEqual(report[name]["status"], gate.PASS)
        self.assertFalse(report["capability_changes"])

    def test_missing_query_and_failed_query(self):
        self.assertEqual(gate.build_report(self.probes(), "no-private")["FL12_0_GATE"]["status"], gate.UNVERIFIED)
        self.assertEqual(gate.build_report(self.probes("options: tiled=2", gate.FAIL), "no-private")["FL12_0_GATE"]["status"], gate.FAIL)

    def test_current_contract_fails_gate(self):
        output = "options: tiled=0 binding=2 stencilRef=0 logicOp=1 typedUAV=0 ROV=0 conservative=0 heap=2"
        report = gate.build_report(self.probes(output), "normal")
        self.assertEqual(report["FL12_0_GATE"]["status"], gate.FAIL)
        self.assertEqual(report["FL12_1_GATE"]["status"], gate.FAIL)

    def test_isolated_feature_failure_is_not_lost(self):
        probes = self.probes()
        probes["feature_support"]["status"] = gate.FAIL
        self.assertEqual(gate.build_report(probes, "normal")["FL12_0_GATE"]["status"], gate.FAIL)

    def test_provenance_gap_does_not_erase_execution_failure(self):
        probes = self.probes()
        probes["feature_support"]["status"] = gate.FAIL
        provenance = {"status": gate.UNVERIFIED, "reason": "missing evidence"}
        self.assertEqual(gate.build_report(probes, "normal", provenance)["FL12_0_GATE"]["status"], gate.FAIL)
        self.assertEqual(probes["feature_support"]["status"], gate.FAIL)

    def test_missing_fixture_never_runs(self):
        with TemporaryDirectory() as directory, patch.object(gate.subprocess, "run") as run:
            result = gate.run_fixture(Path(directory), None, "missing.exe", (), (), 1)
            self.assertEqual(result["status"], gate.UNVERIFIED)
            run.assert_not_called()

    def test_exit_zero_without_markers_fails(self):
        with TemporaryDirectory() as directory:
            # A real existing file suffices: execution is mocked, not the evidence checks.
            import shutil
            shutil.copy2(__file__, Path(directory) / "probe.exe")
            with patch.object(gate.subprocess, "run") as run:
                run.return_value.returncode = 0
                run.return_value.stdout = ""
                run.return_value.stderr = ""
                result = gate.run_fixture(Path(directory), None, "probe.exe", (), ("passed",), 1)
                self.assertEqual(result["status"], gate.FAIL)

    def test_build_variant_is_verified(self):
        import json
        import shutil
        with TemporaryDirectory() as directory:
            build = Path(directory)
            commands = [{"file": "/src/d3d12_device.cpp", "command": "c++ -DDXMT_NO_PRIVATE_API"},
                        {"file": "/src/cache.c", "command": "cc -DDXMT_NO_PRIVATE_API"}]
            (build / "compile_commands.json").write_text(json.dumps(commands))
            for dll in ("d3d12", "dxgi", "winemetal"):
                target = build / "src" / dll
                target.mkdir(parents=True)
                shutil.copy2(__file__, target / (dll + ".dll"))
            self.assertEqual(gate.verify_build(build, "no-private", None)["status"], gate.PASS)
            self.assertEqual(len(gate.verify_build(build, "no-private", None)["runtime_sha256"]), 3)
            self.assertEqual(gate.verify_build(build, "normal", None)["status"], gate.FAIL)

    def test_missing_compile_evidence_is_unverified(self):
        with TemporaryDirectory() as directory:
            self.assertEqual(gate.verify_build(Path(directory), "normal", None)["status"], gate.UNVERIFIED)


if __name__ == "__main__":
    unittest.main()
