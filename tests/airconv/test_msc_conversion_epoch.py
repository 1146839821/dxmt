#!/usr/bin/env python3
"""Observe dyld events around an explicitly supplied MSC and DXIL fixture."""
import hashlib
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile


def main():
    probe_source, epoch_source, include = (Path(p).resolve() for p in sys.argv[1:])
    converter = os.environ.get("DXMT_TEST_MSC_RUNTIME_IMAGE")
    fixture = os.environ.get("DXMT_TEST_MSC_DXIL")
    if not converter or not fixture:
        print("SKIP: set DXMT_TEST_MSC_RUNTIME_IMAGE and DXMT_TEST_MSC_DXIL")
        return 77
    converter, fixture = Path(converter).resolve(strict=True), Path(fixture).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="dxmt-msc-conversion-epoch-") as directory:
        root = Path(directory)
        probe = root / "probe"
        subprocess.run(["xcrun", "clang", "-arch", platform.machine(), "-Wno-deprecated-declarations",
                        "-I", str(epoch_source.parent), "-I", str(include), str(probe_source),
                        str(epoch_source), "-o", str(probe)], check=True, timeout=30)
        env = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_")}
        env["DYLD_PRINT_LIBRARIES"] = "1"
        result = subprocess.run([str(probe), str(converter), str(fixture)], env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=90, check=True)
        log = result.stderr.decode(errors="replace")
        output = result.stdout.decode().strip()
        match = re.fullmatch(r"EPOCH start=(\d+) loaded=(\d+) converted=(\d+) metallib=(\d+)", output)
        if not match or str(converter) not in log:
            raise AssertionError("missing conversion or loaded converter evidence")
        start, loaded, converted, size = map(int, match.groups())
        if not start < loaded <= converted or size == 0:
            raise AssertionError("invalid conversion or epoch observation")
        late_source, late_library = root / "late.c", root / "late.dylib"
        late_source.write_text("int late_marker(void) { return 42; }\n")
        wrapper_source, wrapper = root / "wrapper.c", root / "wrapper.dylib"
        wrapper_source.write_text('''#include "metal_shader_converter/metal_irconverter.h"
#include <dlfcn.h>
#include <stdlib.h>
IRCompiler *force_dependency(void) { return IRCompilerCreate(); }
IRObject *IRCompilerAllocCompileAndLink(IRCompiler *c, const char *entry, const IRObject *input, IRError **error) {
  if (!dlopen(getenv("DXMT_TEST_LATE_IMAGE"), RTLD_NOW | RTLD_LOCAL)) return 0;
  typedef IRObject *(*compile_fn)(IRCompiler *, const char *, const IRObject *, IRError **);
  compile_fn real = (compile_fn)dlsym(RTLD_NEXT, "IRCompilerAllocCompileAndLink");
  return real ? real(c, entry, input, error) : 0;
}
''')
        subprocess.run(["xcrun", "clang", "-arch", platform.machine(), "-dynamiclib", str(late_source),
                        "-o", str(late_library)], check=True, timeout=30)
        subprocess.run(["xcrun", "clang", "-arch", platform.machine(), "-dynamiclib", "-I", str(include),
                        str(wrapper_source), str(converter), "-Wl,-rpath," + str(converter.parent),
                        "-o", str(wrapper)], check=True, timeout=30)
        late_env = {**env, "DXMT_TEST_LATE_IMAGE": str(late_library)}
        late = subprocess.run([str(probe), str(wrapper), str(fixture)], env=late_env, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=90, check=True)
        late_output = late.stdout.decode().strip()
        late_match = re.fullmatch(r"EPOCH start=(\d+) loaded=(\d+) converted=(\d+) metallib=(\d+)", late_output)
        late_log = late.stderr.decode(errors="replace")
        if (not late_match or int(late_match.group(3)) <= int(late_match.group(2)) or
                str(late_library) not in late_log or str(converter) not in late_log):
            raise AssertionError("late-load positive control was not observed")
        bad = root / "bad.dxil"
        bad.write_bytes(b"not a shader")
        invalid = subprocess.run([str(probe), str(converter), str(bad)], env=env, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, timeout=90)
        if invalid.returncode == 0 or b"metallib=" in invalid.stdout:
            raise AssertionError("invalid DXIL unexpectedly passed the conversion control")
        print(f"architecture={platform.machine()} converter_sha256={hashlib.sha256(converter.read_bytes()).hexdigest()}")
        print(f"dxil_sha256={hashlib.sha256(fixture.read_bytes()).hexdigest()} {output}")
        print(f"late-load control: {late_output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
