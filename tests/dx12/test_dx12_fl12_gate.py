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
                for name in ("feature_support", "shader_validation", "shader_container", "shader_stage_matrix",
                             "pipeline_library_failure_oracle", "graphics_library_failure_oracle",
                             "tessellation_library_failure_oracle", "geometry_library_failure_oracle",
                             "shader_library_failure_oracle", "state_object_failure_oracle",
                             "state_object_addition_failure_oracle", "ray_synthesis_failure_oracle",
                             "ray_metal_failure_oracle")}

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

    def test_optional_failures_do_not_change_mandatory_requirements(self):
        optional_names = ("mesh_failure_oracle", "shader_library_failure_oracle",
                          "state_object_failure_oracle", "state_object_addition_failure_oracle",
                          "ray_synthesis_failure_oracle", "ray_metal_failure_oracle")
        probes = self.probes()
        for name in ("backend_failure_oracle", "graphics_failure_oracle",
                     "tessellation_failure_oracle", "geometry_failure_oracle", *optional_names):
            probes[name] = {"status": gate.PASS}
        baseline = gate.build_report(probes, "normal")
        for name in optional_names:
            for status in (None, *gate.STATUSES):
                with self.subTest(probe=name, status=status):
                    changed = dict(probes)
                    if status is None: changed.pop(name)
                    else: changed[name] = {"status": status}
                    report = gate.build_report(changed, "normal")
                    for level in ("FL12_0_GATE", "FL12_1_GATE"):
                        self.assertEqual(report[level], baseline[level])
                        self.assertNotEqual(report[level]["status"], gate.PASS)
                    self.assertEqual(report["optional_regressions"]["status"],
                                     gate.UNVERIFIED if status is None else status)
                    self.assertEqual(len(report["optional_regressions"]["requirements"]), 6)

    def test_optional_success_does_not_mask_mandatory_failure(self):
        probes = self.probes()
        for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle",
                     "geometry_failure_oracle", "mesh_failure_oracle"):
            probes[name] = {"status": gate.PASS}
        probes["geometry_failure_oracle"] = {"status": gate.FAIL}
        report = gate.build_report(probes, "no-private")
        self.assertEqual(report["optional_regressions"]["status"], gate.PASS)
        for level in ("FL12_0_GATE", "FL12_1_GATE"):
            self.assertEqual(report[level]["status"], gate.FAIL)

    def test_watchlist_distinguishes_air_implementation_from_msc_gap(self):
        report = gate.build_report(self.probes(), "normal")
        rows = {r["name"]: r for r in report["architecture_watchlist"]}
        self.assertEqual(rows["tiled_raw_structured_buffer"]["status"], gate.BLOCKED)
        self.assertIn("DXIL/MSC", rows["tiled_raw_structured_buffer"]["reason"])
        self.assertEqual(rows["tiled_raw_structured_dxbc"]["status"], gate.UNVERIFIED)
        self.assertEqual(report["schema_version"], 2)

    def test_each_isolation_contract_probe_is_required(self):
        invocations = ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle",
                       "geometry_failure_oracle", "mesh_failure_oracle")
        for name in ("shader_validation", "shader_container", "shader_stage_matrix"):
            for status in (None, *gate.STATUSES):
                with self.subTest(probe=name, status=status):
                    probes = self.probes()
                    probes.update({key: {"status": gate.PASS} for key in invocations})
                    if status is None:
                        probes.pop(name)
                    else:
                        probes[name] = {"status": status}
                    rows = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
                    isolation = next(row for row in rows if row["name"] == "backend_isolation")
                    expected = gate.UNVERIFIED if status is None else gate.PARTIAL if status == gate.PASS else status
                    self.assertEqual(isolation["status"], expected)
        probes = self.probes()
        probes.update({key: {"status": gate.PASS} for key in invocations})
        for name in ("shader_validation", "shader_container", "shader_stage_matrix"):
            probes.pop(name)
        for failed in (False, True):
            with self.subTest(all_contracts_missing=True, failed_invocation=failed):
                if failed:
                    probes["backend_failure_oracle"] = {"status": gate.FAIL}
                rows = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
                isolation = next(row for row in rows if row["name"] == "backend_isolation")
                self.assertEqual(isolation["status"], gate.FAIL if failed else gate.UNVERIFIED)

    def test_ordinary_invocations_do_not_close_emulation_isolation(self):
        for status in (gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes["backend_failure_oracle"] = {"status": status}
            probes["graphics_failure_oracle"] = {"status": gate.PASS}
            probes["tessellation_failure_oracle"] = {"status": gate.PASS}
            probes["geometry_failure_oracle"] = {"status": gate.PASS}
            probes["mesh_failure_oracle"] = {"status": gate.PASS}
            rows = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "compute_backend_failure_invocations"), status)
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "backend_isolation"),
                             gate.PARTIAL if status == gate.PASS else status)

    def test_graphics_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes["backend_failure_oracle"] = {"status": gate.PASS}
            probes["tessellation_failure_oracle"] = {"status": gate.PASS}
            probes["geometry_failure_oracle"] = {"status": gate.PASS}
            probes["mesh_failure_oracle"] = {"status": gate.PASS}
            if status is not None: probes["graphics_failure_oracle"] = {"status": status}
            rows = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "graphics_backend_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "backend_isolation"),
                             gate.PARTIAL if expected == gate.PASS else expected)

    def test_graphics_every_failure_mode_is_required(self):
        def fixture(*args):
            failed = args[3][0] == "graphics-msc-ps-second-pass"
            self.assertEqual(args[3][1:], ("backend_failure.vs.cso", "backend_failure.ps.cso"))
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_graphics_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 30)

    def test_graphics_container_rejection_modes_are_required(self):
        modes = {"graphics-container-" + stage + "-" + rejection for stage in ("vs", "ps")
                 for rejection in ("truncated", "offset", "no-executable", "duplicate", "hybrid")}
        for failed in modes:
            with self.subTest(mode=failed):
                with patch.object(gate, "run_fixture", side_effect=lambda *args:
                                  {"status": gate.FAIL if args[3][0] == failed else gate.PASS}):
                    result = gate.run_graphics_failure_oracle(Path("."), None, 1, None)
                    self.assertEqual({mode for mode in result["cases"] if mode.startswith("graphics-container-")}, modes)
                    self.assertEqual(result["cases"][failed]["status"], gate.FAIL)
                    self.assertEqual(result["status"], gate.FAIL)

    def test_tessellation_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes["backend_failure_oracle"] = probes["graphics_failure_oracle"] = {"status": gate.PASS}
            probes["geometry_failure_oracle"] = {"status": gate.PASS}
            probes["mesh_failure_oracle"] = {"status": gate.PASS}
            if status is not None: probes["tessellation_failure_oracle"] = {"status": status}
            rows = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "tessellation_backend_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "backend_isolation"),
                             gate.PARTIAL if expected == gate.PASS else expected)

    def test_tessellation_every_failure_mode_is_required(self):
        def fixture(*args):
            failed = args[3][0] == "tess-msc-ds-second-pass"
            self.assertEqual(args[3][1:], ("backend_failure.vs.cso", "backend_failure.ps.cso",
                "shader_backend.tessellation.hs.cso", "shader_backend.tessellation.ds.cso", "shader_backend_stages.hlsl"))
            return {"status": gate.FAIL if failed else gate.PASS, "runtime_sha256": {"d3d12": "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_tessellation_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 22)

    def test_invocation_failure_is_not_hidden_by_hash_gap(self):
        def fixture(*args):
            failed = args[3][0] == "msc-second-pass"
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_backend_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 16)

    def test_compute_container_rejection_modes_are_required(self):
        modes = {"container-truncated", "container-offset", "container-no-executable",
                 "container-duplicate", "container-hybrid"}
        for failed in modes:
            with self.subTest(mode=failed):
                with patch.object(gate, "run_fixture", side_effect=lambda *args:
                                  {"status": gate.FAIL if args[3][0] == failed else gate.PASS}):
                    result = gate.run_backend_failure_oracle(Path("."), None, 1, None)
                    self.assertEqual({mode for mode in result["cases"] if mode.startswith("container-")}, modes)
                    self.assertEqual(result["cases"][failed]["status"], gate.FAIL)
                    self.assertEqual(result["status"], gate.FAIL)

    def test_geometry_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes["mesh_failure_oracle"] = {"status": gate.PASS}
            for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle"):
                probes[name] = {"status": gate.PASS}
            if status is not None: probes["geometry_failure_oracle"] = {"status": status}
            rows = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "geometry_backend_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "backend_isolation"),
                             gate.PARTIAL if expected == gate.PASS else expected)

    def test_geometry_every_failure_mode_is_required(self):
        def fixture(*args):
            failed = args[3][0] == "geom-air-gs-object-compile"
            self.assertEqual(args[3][1:], ("backend_failure.vs.cso", "backend_failure.ps.cso",
                "shader_backend.geometry.gs.cso", "shader_backend_stages.hlsl"))
            return {"status": gate.FAIL if failed else gate.PASS, "runtime_sha256": {"d3d12": "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_geometry_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 13)

    def test_invocation_executable_provenance_is_required(self):
        import hashlib
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "dx12_backend_failure.exe").write_bytes(b"probe")
            digest = hashlib.sha256(b"probe").hexdigest()
            for executable_hash in (digest, "stale", None):
                with patch.object(gate, "run_fixture", return_value={
                        "status": gate.PASS, "executable_sha256": executable_hash,
                        "runtime_sha256": {"d3d12": "same"}}):
                    for oracle in (gate.run_backend_failure_oracle, gate.run_graphics_failure_oracle,
                                   gate.run_tessellation_failure_oracle, gate.run_geometry_failure_oracle,
                                   gate.run_mesh_failure_oracle, gate.run_pipeline_library_failure_oracle,
                                   gate.run_graphics_library_failure_oracle, gate.run_tessellation_library_failure_oracle,
                                   gate.run_geometry_library_failure_oracle, gate.run_shader_library_failure_oracle,
                                   gate.run_state_object_failure_oracle, gate.run_state_object_addition_failure_oracle,
                                   gate.run_ray_synthesis_failure_oracle, gate.run_ray_metal_failure_oracle):
                        result = oracle(root, None, 1, None)
                        self.assertEqual(result["status"], gate.PASS if executable_hash == digest else gate.UNVERIFIED)

    def test_mesh_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle", "geometry_failure_oracle"):
                probes[name] = {"status": gate.PASS}
            if status is not None: probes["mesh_failure_oracle"] = {"status": status}
            report = gate.build_report(probes, "normal")
            rows = report["optional_regressions"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "mesh_backend_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in report["FL12_0_GATE"]["requirements"]
                                  if r["name"] == "backend_isolation"), gate.PARTIAL)
            self.assertEqual(report["optional_regressions"]["status"], expected)

    def test_mesh_every_failure_mode_is_required(self):
        def fixture(*args):
            failed = args[3][0] == "mesh-as-second-pass"
            self.assertEqual(args[3][1:], ("mesh_sm6.ms.cso", "mesh_sm6.as.cso", "mesh_sm6.ps.cso"))
            return {"status": gate.FAIL if failed else gate.PASS, "runtime_sha256": {"d3d12": "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_mesh_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 21)

    def test_pipeline_library_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes.pop("pipeline_library_failure_oracle")
            for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle",
                         "geometry_failure_oracle", "mesh_failure_oracle"):
                probes[name] = {"status": gate.PASS}
            if status is not None: probes["pipeline_library_failure_oracle"] = {"status": status}
            rows = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "compute_pipeline_library_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "backend_isolation"),
                             gate.PARTIAL if expected == gate.PASS else expected)

    def test_pipeline_library_every_failure_mode_is_required(self):
        def fixture(*args):
            failed = args[3][0] == "library-msc-second-pass"
            self.assertEqual(args[3][1:], ("compute_sm6.cs.cso",))
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_pipeline_library_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 14)

    def test_graphics_library_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes.pop("graphics_library_failure_oracle")
            for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle",
                         "geometry_failure_oracle", "mesh_failure_oracle"):
                probes[name] = {"status": gate.PASS}
            if status is not None: probes["graphics_library_failure_oracle"] = {"status": status}
            rows = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "ordinary_graphics_pipeline_library_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "backend_isolation"),
                             gate.PARTIAL if expected == gate.PASS else expected)

    def test_graphics_library_every_failure_mode_is_required(self):
        def fixture(*args):
            failed = args[3][0] == "library-graphics-msc-ps-second-pass"
            self.assertEqual(args[3][1:], ("backend_failure.vs.cso", "backend_failure.ps.cso"))
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_graphics_library_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 20)

    def test_tessellation_library_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes.pop("tessellation_library_failure_oracle")
            for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle",
                         "geometry_failure_oracle", "mesh_failure_oracle"):
                probes[name] = {"status": gate.PASS}
            if status is not None: probes["tessellation_library_failure_oracle"] = {"status": status}
            rows = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "tessellation_pipeline_library_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "backend_isolation"),
                             gate.PARTIAL if expected == gate.PASS else expected)

    def test_tessellation_library_every_failure_mode_is_required(self):
        def fixture(*args):
            failed = args[3][0] == "library-tess-msc-ds-second-pass"
            self.assertEqual(args[3][1:], ("backend_failure.vs.cso", "backend_failure.ps.cso",
                "shader_backend.tessellation.hs.cso", "shader_backend.tessellation.ds.cso", "shader_backend_stages.hlsl"))
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_tessellation_library_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 20)

    def test_geometry_library_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes.pop("geometry_library_failure_oracle")
            for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle",
                         "geometry_failure_oracle", "mesh_failure_oracle"):
                probes[name] = {"status": gate.PASS}
            if status is not None: probes["geometry_library_failure_oracle"] = {"status": status}
            rows = gate.build_report(probes, "normal")["FL12_0_GATE"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "geometry_pipeline_library_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "backend_isolation"),
                             gate.PARTIAL if expected == gate.PASS else expected)

    def test_geometry_library_every_failure_mode_is_required(self):
        def fixture(*args):
            failed = args[3][0] == "library-geom-air-gs-object-compile"
            self.assertEqual(args[3][1:], ("backend_failure.vs.cso", "backend_failure.ps.cso",
                "shader_backend.geometry.gs.cso", "shader_backend_stages.hlsl"))
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_geometry_library_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 15)

    def test_shader_library_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes.pop("shader_library_failure_oracle")
            for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle",
                         "geometry_failure_oracle", "mesh_failure_oracle"):
                probes[name] = {"status": gate.PASS}
            if status is not None: probes["shader_library_failure_oracle"] = {"status": status}
            report = gate.build_report(probes, "normal")
            rows = report["optional_regressions"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "shader_library_converter_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in report["FL12_0_GATE"]["requirements"]
                                  if r["name"] == "backend_isolation"), gate.PARTIAL)
            self.assertEqual(report["optional_regressions"]["status"], expected)

    def test_shader_library_every_failure_mode_is_required(self):
        def fixture(*args):
            failed = args[3][0] == "shaderlib-intersection-second-pass"
            self.assertEqual(args[3][1:], ("ray_stages_sm6.lib.cso", "compute_sm6.cs.cso", "ray_payload_qualifiers_sm6.lib.cso"))
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_shader_library_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 34)

    def test_state_object_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes.pop("state_object_failure_oracle")
            for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle",
                         "geometry_failure_oracle", "mesh_failure_oracle"):
                probes[name] = {"status": gate.PASS}
            if status is not None: probes["state_object_failure_oracle"] = {"status": status}
            report = gate.build_report(probes, "normal")
            rows = report["optional_regressions"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "state_object_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in report["FL12_0_GATE"]["requirements"]
                                  if r["name"] == "backend_isolation"), gate.PARTIAL)
            self.assertEqual(report["optional_regressions"]["status"], expected)

    def test_state_object_every_mode_is_required(self):
        seen = []
        def fixture(*args):
            seen.append(args[3][0])
            failed = args[3][0] == "state-hint-anyhit-second-pass"
            self.assertEqual(args[3][1:], ("ray_stages_sm6.lib.cso", "compute_sm6.cs.cso", "ray_payload_qualifiers_sm6.lib.cso"))
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_state_object_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 108)
            self.assertEqual(len(set(seen)), 108)
            expected_loads = {"state-load-" + stage + "-" + operation
                              for stage in ("raygen", "miss", "closesthit", "anyhit", "intersection", "callable",
                                            "hint-anyhit", "hint-closesthit")
                              for operation in ("control", "library", "function")}
            self.assertEqual({mode for mode in seen if mode.startswith("state-load-")}, expected_loads)
            expected_multi = {"state-multi-" + stage + "-" + operation
                              for stage in ("raygen", "miss", "closesthit", "anyhit", "intersection", "callable",
                                            "hint-anyhit", "hint-closesthit")
                              for operation in ("control", "library", "function", "unsupported", "second-pass")}
            self.assertEqual({mode for mode in seen if mode.startswith("state-multi-")}, expected_multi)

    def test_state_object_each_multi_failure_is_required(self):
        for stage in ("raygen", "miss", "closesthit", "anyhit", "intersection", "callable",
                      "hint-anyhit", "hint-closesthit"):
            for operation in ("library", "function", "unsupported", "second-pass"):
                failed = "state-multi-" + stage + "-" + operation
                with self.subTest(mode=failed):
                    def fixture(*args):
                        return {"status": gate.FAIL if args[3][0] == failed else gate.PASS}
                    with patch.object(gate, "run_fixture", side_effect=fixture):
                        result = gate.run_state_object_failure_oracle(Path("."), None, 1, None)
                        self.assertEqual(result["cases"][failed]["status"], gate.FAIL)
                        self.assertEqual(result["status"], gate.FAIL)

    def test_state_object_each_load_failure_is_required(self):
        for stage in ("raygen", "miss", "closesthit", "anyhit", "intersection", "callable",
                      "hint-anyhit", "hint-closesthit"):
            for operation in ("library", "function"):
                failed = "state-load-" + stage + "-" + operation
                with self.subTest(mode=failed):
                    def fixture(*args):
                        return {"status": gate.FAIL if args[3][0] == failed else gate.PASS}
                    with patch.object(gate, "run_fixture", side_effect=fixture):
                        result = gate.run_state_object_failure_oracle(Path("."), None, 1, None)
                        self.assertEqual(result["cases"][failed]["status"], gate.FAIL)
                        self.assertEqual(result["status"], gate.FAIL)

    def test_state_object_addition_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes.pop("state_object_addition_failure_oracle")
            for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle",
                         "geometry_failure_oracle", "mesh_failure_oracle"):
                probes[name] = {"status": gate.PASS}
            if status is not None: probes["state_object_addition_failure_oracle"] = {"status": status}
            report = gate.build_report(probes, "normal")
            rows = report["optional_regressions"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "state_object_addition_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in report["FL12_0_GATE"]["requirements"]
                                  if r["name"] == "backend_isolation"), gate.PARTIAL)
            self.assertEqual(report["optional_regressions"]["status"], expected)

    def test_state_object_addition_every_mode_is_required(self):
        seen = []
        def fixture(*args):
            seen.append(args[3][0])
            failed = args[3][0] == "state-add-hint-anyhit-second-pass"
            self.assertEqual(args[3][1:], ("ray_stages_sm6.lib.cso", "compute_sm6.cs.cso", "ray_payload_qualifiers_sm6.lib.cso"))
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_state_object_addition_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 110)
            self.assertEqual(len(set(seen)), 110)
            self.assertIn("state-add-disallowed", seen)
            self.assertIn("state-add-duplicate", seen)
            expected = {"state-add-load-" + stage + "-" + operation
                        for stage in ("raygen", "miss", "closesthit", "anyhit", "intersection", "callable",
                                      "hint-anyhit", "hint-closesthit")
                        for operation in ("control", "library", "function")}
            self.assertEqual({mode for mode in seen if mode.startswith("state-add-load-")}, expected)
            expected_multi = {"state-add-multi-" + stage + "-" + operation
                              for stage in ("raygen", "miss", "closesthit", "anyhit", "intersection", "callable",
                                            "hint-anyhit", "hint-closesthit")
                              for operation in ("control", "library", "function", "unsupported", "second-pass")}
            self.assertEqual({mode for mode in seen if mode.startswith("state-add-multi-")}, expected_multi)

    def test_addition_each_multi_failure_is_required(self):
        for stage in ("raygen", "miss", "closesthit", "anyhit", "intersection", "callable",
                      "hint-anyhit", "hint-closesthit"):
            for operation in ("library", "function", "unsupported", "second-pass"):
                failed = "state-add-multi-" + stage + "-" + operation
                with self.subTest(mode=failed):
                    with patch.object(gate, "run_fixture", side_effect=lambda *args:
                                      {"status": gate.FAIL if args[3][0] == failed else gate.PASS}):
                        result = gate.run_state_object_addition_failure_oracle(Path("."), None, 1, None)
                        self.assertEqual(result["cases"][failed]["status"], gate.FAIL)
                        self.assertEqual(result["status"], gate.FAIL)

    def test_addition_each_load_failure_is_required(self):
        for stage in ("raygen", "miss", "closesthit", "anyhit", "intersection", "callable",
                      "hint-anyhit", "hint-closesthit"):
            for operation in ("library", "function"):
                failed = "state-add-load-" + stage + "-" + operation
                with self.subTest(mode=failed):
                    def fixture(*args):
                        return {"status": gate.FAIL if args[3][0] == failed else gate.PASS}
                    with patch.object(gate, "run_fixture", side_effect=fixture):
                        result = gate.run_state_object_addition_failure_oracle(Path("."), None, 1, None)
                        self.assertEqual(result["cases"][failed]["status"], gate.FAIL)
                        self.assertEqual(result["status"], gate.FAIL)

    def test_ray_synthesis_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes.pop("ray_synthesis_failure_oracle")
            for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle",
                         "geometry_failure_oracle", "mesh_failure_oracle"):
                probes[name] = {"status": gate.PASS}
            if status is not None: probes["ray_synthesis_failure_oracle"] = {"status": status}
            report = gate.build_report(probes, "normal")
            rows = report["optional_regressions"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "ray_synthesis_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in report["FL12_0_GATE"]["requirements"]
                                  if r["name"] == "backend_isolation"), gate.PARTIAL)
            self.assertEqual(report["optional_regressions"]["status"], expected)

    def test_ray_synthesis_every_mode_is_required(self):
        seen = []
        def fixture(*args):
            seen.append(args[3][0])
            self.assertEqual(args[3][1:], ("ray_stages_sm6.lib.cso",))
            failed = args[3][0] == "synth-intersection-second-pass"
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_ray_synthesis_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 12)
            self.assertEqual(len(set(seen)), 12)

    def test_ray_metal_missing_or_failed_is_required(self):
        for status in (None, gate.PASS, gate.FAIL, gate.UNVERIFIED):
            probes = self.probes()
            probes.pop("ray_metal_failure_oracle")
            for name in ("backend_failure_oracle", "graphics_failure_oracle", "tessellation_failure_oracle",
                         "geometry_failure_oracle", "mesh_failure_oracle"):
                probes[name] = {"status": gate.PASS}
            if status is not None: probes["ray_metal_failure_oracle"] = {"status": status}
            report = gate.build_report(probes, "normal")
            rows = report["optional_regressions"]["requirements"]
            expected = gate.UNVERIFIED if status is None else status
            self.assertEqual(next(r["status"] for r in rows if r["name"] == "ray_metal_failure_invocations"), expected)
            self.assertEqual(next(r["status"] for r in report["FL12_0_GATE"]["requirements"]
                                  if r["name"] == "backend_isolation"), gate.PARTIAL)
            self.assertEqual(report["optional_regressions"]["status"], expected)

    def test_ray_metal_every_mode_is_required(self):
        seen = []
        def fixture(*args):
            seen.append(args[3][0])
            self.assertEqual(args[3][1:], ("ray_stages_sm6.lib.cso",))
            failed = args[3][0] == "metal-intersection-intersection-handle"
            return {"status": gate.FAIL if failed else gate.PASS,
                    "runtime_sha256": {"d3d12": "other" if failed else "same"}}
        with patch.object(gate, "run_fixture", side_effect=fixture):
            result = gate.run_ray_metal_failure_oracle(Path("."), None, 1, None)
            self.assertEqual(result["status"], gate.FAIL)
            self.assertEqual(len(result["cases"]), 19)
            self.assertEqual(len(set(seen)), 19)
            self.assertIn("load-dispatch-dispatch-library", seen)
            self.assertIn("load-dispatch-dispatch-function", seen)
            self.assertIn("load-intersection-intersection-library", seen)
            expected = {"metal-" + path + "-" + operation for path in ("dispatch", "intersection")
                        for operation in ("control", "pso", "vft", "ift", "visible-handle")}
            expected.add("metal-intersection-intersection-handle")
            expected.update("load-" + path + "-" + operation for path in ("dispatch", "intersection")
                            for operation in ("control", "dispatch-library", "dispatch-function"))
            expected.update(("load-intersection-intersection-library", "load-intersection-intersection-function"))
            self.assertEqual(set(seen), expected)

    def test_ray_load_each_failure_is_required(self):
        failures = ["load-" + path + "-" + operation for path in ("dispatch", "intersection")
                    for operation in ("dispatch-library", "dispatch-function")]
        failures.extend(("load-intersection-intersection-library", "load-intersection-intersection-function"))
        for failed in failures:
            with self.subTest(mode=failed):
                def fixture(*args):
                    return {"status": gate.FAIL if args[3][0] == failed else gate.PASS}
                with patch.object(gate, "run_fixture", side_effect=fixture):
                    result = gate.run_ray_metal_failure_oracle(Path("."), None, 1, None)
                    self.assertEqual(result["cases"][failed]["status"], gate.FAIL)
                    self.assertEqual(result["status"], gate.FAIL)

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
        reduction = next(row for row in requirements if row["name"] == "min_max_reduction_filtering")
        self.assertEqual(reduction["status"], gate.BLOCKED)
        self.assertIn("opt-in AIR subset exists", reduction["reason"])
        self.assertIn("GPU acceptance incomplete", reduction["reason"])

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

    def test_qualification_overrides_ambient_experimental_caps(self):
        import shutil
        from types import SimpleNamespace
        with TemporaryDirectory() as directory:
            shutil.copy2(__file__, Path(directory) / "probe.exe")
            for inherited in ("1", "true", "0"):
                with self.subTest(inherited=inherited), patch.dict(gate.os.environ, {
                    "DXMT_EXPERIMENTAL_SM6_6": inherited,
                    "DXMT_EXPERIMENTAL_FL12_0": inherited,
                    "DXMT_SHADER_CACHE": "1",
                    "WINEPREFIX": "preserved-prefix",
                }):
                    def execute(*args, **kwargs):
                        for key in gate.QUALIFICATION_ENVIRONMENT:
                            self.assertEqual(kwargs["env"][key], "0")
                        self.assertEqual(kwargs["env"]["WINEPREFIX"], "preserved-prefix")
                        kwargs["stdout"].write("passed")
                        return SimpleNamespace(returncode=0)
                    with patch.object(gate.subprocess, "run", side_effect=execute):
                        result = gate.run_fixture(Path(directory), None, "probe.exe", (), ("passed",), 1)
                    self.assertEqual(result["status"], gate.PASS)
                    self.assertEqual(result["controlled_environment"], gate.QUALIFICATION_ENVIRONMENT)
                    self.assertEqual(gate.os.environ["DXMT_EXPERIMENTAL_SM6_6"], inherited)

    def test_child_observes_isolated_capability_environment(self):
        import json
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "probe.exe").write_text(
                "import os,json\nprint(json.dumps({k:os.environ.get(k) for k in "
                "['DXMT_EXPERIMENTAL_SM6_6','DXMT_EXPERIMENTAL_FL12_0']}))\nprint('passed')\n")
            with patch.dict(gate.os.environ, {"DXMT_EXPERIMENTAL_SM6_6": "1", "DXMT_EXPERIMENTAL_FL12_0": "1"}):
                result = gate.run_fixture(root, sys.executable, "probe.exe", (), ("passed",), 5)
            self.assertEqual(result["status"], gate.PASS)
            observed = json.loads(result["output"].splitlines()[0])
            self.assertEqual(observed, {"DXMT_EXPERIMENTAL_SM6_6": "0", "DXMT_EXPERIMENTAL_FL12_0": "0"})

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

    def test_file_capture_requires_exit_and_marker(self):
        import shutil
        from types import SimpleNamespace
        with TemporaryDirectory() as directory:
            shutil.copy2(__file__, Path(directory) / "probe.exe")
            for code, marker in ((0, "passed"), (1, "passed"), (0, ""), (77, "passed")):
                def execute(*args, **kwargs):
                    self.assertNotIn("capture_output", kwargs)
                    self.assertEqual(kwargs["stderr"], gate.subprocess.STDOUT)
                    kwargs["stdout"].write(marker)
                    return SimpleNamespace(returncode=code)
                with patch.object(gate.subprocess, "run", side_effect=execute):
                    result = gate.run_fixture(Path(directory), None, "probe.exe", (), ("passed",), 1)
                    expected = gate.UNVERIFIED if code == 77 else gate.PASS if code == 0 and marker else gate.FAIL
                    self.assertEqual(result["status"], expected)
                    self.assertEqual(result["output"], marker)

    def test_helper_output_handle_does_not_delay_probe_completion(self):
        import time
        with TemporaryDirectory() as directory:
            root = Path(directory)
            release = root / "release-helper"
            helper = ("import pathlib,time; p=pathlib.Path(" + repr(str(release)) + "); "
                      "deadline=time.monotonic()+5\n"
                      "while not p.exists() and time.monotonic()<deadline: time.sleep(.01)")
            (root / "probe.exe").write_text(
                "import subprocess,sys\nsubprocess.Popen([sys.executable,'-c'," + repr(helper) +
                "],stdout=sys.stdout,stderr=sys.stderr)\nprint('passed',file=sys.stderr,flush=True)\n")
            try:
                started = time.monotonic()
                result = gate.run_fixture(root, sys.executable, "probe.exe", (), ("passed",), 1)
                self.assertEqual(result["status"], gate.PASS)
                self.assertLess(time.monotonic() - started, 2)
                self.assertTrue(result["executable_sha256"])
            finally:
                release.touch()

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
