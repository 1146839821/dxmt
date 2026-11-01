#!/usr/bin/env python3
"""Exercise preflight and failure atomicity for the offline AIR experiment."""
from pathlib import Path
import subprocess
import sys
import tempfile

exe, fixture, sidecar_fixture, multi_fixture = (Path(arg).resolve() for arg in sys.argv[1:])
source = fixture.read_text()
sidecar_source = sidecar_fixture.read_text()
multi_source = multi_fixture.read_text()
with tempfile.TemporaryDirectory(prefix="dxmt-typed-buffer-ir-") as temp:
    root = Path(temp)
    cases = [
        ("dynamic", source, 0, []),
        ("unknown", source.replace("air.get_width_texture_buffer_1d",
                                    "air.unknown_texture_buffer_1d"), 4, []),
        ("bad_field", source.replace("i64 %slot, i32 1", "i64 %slot, i32 0"), 4, []),
        ("sidecar_dynamic", sidecar_source, 0, ["--sidecar", "0", "8"]),
        ("sidecar_wrong_table", sidecar_source, 4, ["--sidecar", "16", "8"]),
        ("sidecar_unaligned", sidecar_source, 2, ["--sidecar", "0", "7"]),
        ("sidecar_negative", sidecar_source, 2, ["--sidecar", "0", "-8"]),
        ("sidecar_overlap", sidecar_source, 2, ["--sidecar", "0", "0"]),
        ("sidecar_unknown", sidecar_source.replace("air.get_width_texture_buffer_1d",
                                                    "air.unknown_texture_buffer_1d"), 4,
         ["--sidecar", "0", "8"]),
        ("multi", multi_source, 0, ["--sidecar", "0", "16", "--sidecar", "8", "24"]),
        ("multi_missing", multi_source, 4, ["--sidecar", "0", "16"]),
        ("multi_duplicate_table", multi_source, 2, ["--sidecar", "0", "16", "--sidecar", "0", "24"]),
        ("multi_duplicate_sidecar", multi_source, 2, ["--sidecar", "0", "16", "--sidecar", "8", "16"]),
        ("multi_cross_overlap", multi_source, 2, ["--sidecar", "0", "8", "--sidecar", "8", "24"]),
    ]
    for name, text, expected, options in cases:
        ir = root / (name + ".ll")
        output = root / (name + ".metallib")
        ir.write_text(text)
        result = subprocess.run([str(exe), str(ir), str(output), *options], capture_output=True, text=True)
        if result.returncode != expected:
            raise SystemExit(f"{name}: exit {result.returncode}; {result.stdout} {result.stderr}")
        if (output.exists() and output.stat().st_size > 0) != (expected == 0):
            raise SystemExit(f"{name}: unexpected output state")
        count = 6 if name == "multi" else 3
        if expected == 0 and f"patched {count} typed-buffer operations" not in result.stdout:
            raise SystemExit(f"{name}: unexpected operation count: {result.stdout}")
        print(name, "passed")
