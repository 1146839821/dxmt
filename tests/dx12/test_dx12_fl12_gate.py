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

    def test_minmax_rejection_is_not_gpu_acceptance(self):
        probes = self.probes()
        probes["minmax_sampler_contract"] = {"status": gate.PASS}
        requirements = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
        self.assertEqual(next(row["status"] for row in requirements
                              if row["name"] == "min_max_reduction_filtering"), gate.BLOCKED)

    def test_minmax_missing_or_failed_contract_cannot_pass(self):
        for status in (gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes["minmax_sampler_contract"] = {"status": status}
            requirements = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
            self.assertEqual(next(row["status"] for row in requirements
                                  if row["name"] == "min_max_reduction_filtering"), status)

    def test_isolated_feature_failure_is_not_lost(self):
        probes = self.probes()
        probes["feature_support"]["status"] = gate.FAIL
        self.assertEqual(gate.build_report(probes, "normal")["FL12_0_GATE"]["status"], gate.FAIL)

    def test_typed_uav_requires_both_api_and_gpu_matrix(self):
        for advertised in (0, 1):
            for status in (gate.PASS, gate.FAIL, gate.UNVERIFIED):
                output = f"options: tiled=0 binding=2 stencilRef=0 logicOp=1 typedUAV={advertised} ROV=0 conservative=0 heap=2"
                probes = self.probes(output)
                probes["typed_uav_matrix"] = {"status": status}
                rows = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
                combined = next(r for r in rows if r["name"] == "typed_uav_mandatory_gpu_matrix")
                self.assertEqual(combined["status"] == gate.PASS, bool(advertised and status == gate.PASS))
                api = next(r for r in rows if r["name"] == "typed_uav_additional_formats")
                self.assertEqual(api["status"] == gate.PASS, bool(advertised and status == gate.PASS))

    def test_typed_uav_failed_backend_is_not_hidden_by_hash_gap(self):
        def fixture(*args):
            backend = args[3]
            failed = backend == ("--dxil",)
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            self.assertEqual(gate.run_typed_uav_matrix(Path("."), None, 1, None)["status"], gate.FAIL)

    def test_typed_view_contract_failure_is_required_even_if_matrix_passes(self):
        for backend in ("dxbc", "dxil"):
            for mode in ("--view-contract", "--srv-view-contract"):
                def fixture(*args):
                    failed = args[3] == ("--" + backend, mode)
                    return {"status": gate.FAIL if failed else gate.PASS,
                            "runtime_sha256": {"d3d12": "same"}}
                with patch.object(gate, "run_fixture", side_effect=fixture):
                    self.assertEqual(gate.run_typed_uav_matrix(Path("."), None, 1, None)["status"], gate.FAIL)

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

    def test_installed_builtin_dlls_must_match(self):
        import json
        with TemporaryDirectory() as directory:
            root = Path(directory)
            build, wine, prefix = root / "build", root / "wine/bin/wine", root / "prefix"
            build.mkdir(); wine.parent.mkdir(parents=True); wine.write_bytes(b"wine")
            commands = [{"file": "/src/d3d12_device.cpp", "command": "c++"},
                        {"file": "/src/cache.c", "command": "cc"}]
            (build / "compile_commands.json").write_text(json.dumps(commands))
            unix = root / "wine/lib/wine/x86_64-unix/winemetal.so"
            expected_unix = build / "src/winemetal/unix/winemetal.so"
            for path in (unix, expected_unix):
                path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b"unix")
            pe_roots = (root / "wine/lib/wine/x86_64-windows", prefix / "drive_c/windows/system32")
            for dll in ("d3d12", "dxgi", "winemetal"):
                for path in (build / "src" / dll / (dll + ".dll"), *(r / (dll + ".dll") for r in pe_roots)):
                    path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(dll.encode())
            with patch.dict(gate.os.environ, {"WINEPREFIX": str(prefix)}):
                self.assertEqual(gate.verify_build(build, "normal", str(wine))["status"], gate.PASS)
                for pe_root in pe_roots:
                    target = pe_root / "d3d12.dll"
                    target.write_bytes(b"stale")
                    self.assertEqual(gate.verify_build(build, "normal", str(wine))["status"], gate.UNVERIFIED)
                    target.write_bytes(b"d3d12")


if __name__ == "__main__":
    unittest.main()
