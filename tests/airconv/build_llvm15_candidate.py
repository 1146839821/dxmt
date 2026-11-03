#!/usr/bin/env python3
"""Build isolated LLVM reader candidates using the existing native LLVM build.

This does not modify the LLVM checkout or installed toolchain. It builds probe targets in the selected Meson directory.
The output directory must not exist. Run from any directory on macOS.
"""
import argparse
import shlex
import shutil
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LLVM_TAG = "llvmorg-15.0.7"


def run(args, **kwargs):
    print("+", shlex.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="new isolated output directory")
    parser.add_argument("--build-dir", type=Path, default=REPO / "build")
    parser.add_argument("--llvm-source", type=Path, default=REPO / "toolchains/llvm-project")
    parser.add_argument("--llvm-prefix", type=Path, default=REPO / "toolchains/llvm-darwin")
    parser.add_argument("--arch", default="x86_64")
    args = parser.parse_args()
    output = args.output.resolve()
    source = args.llvm_source.resolve()
    prefix = args.llvm_prefix.resolve()
    build = args.build_dir.resolve()
    tag = subprocess.check_output(["git", "-C", str(source), "describe", "--tags", "--exact-match"], text=True).strip()
    if tag != LLVM_TAG:
        raise SystemExit(f"expected {LLVM_TAG}, got {tag}")
    reader_path = Path("llvm/lib/Bitcode/Reader/MetadataLoader.cpp")
    if subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--", str(reader_path)], text=True):
        raise SystemExit("LLVM MetadataLoader.cpp has local changes")
    # mkdir without exist_ok ensures no pre-existing artifacts are overwritten.
    output.mkdir(parents=True)
    reader = output / reader_path
    reader.parent.mkdir(parents=True)
    shutil.copyfile(source / reader_path, reader)
    for patch in ("experimental-llvm15-empty-ptrauth.patch", "experimental-llvm15-placeholder-cleanup.patch"):
        with (REPO / "patches/llvm" / patch).open("rb") as data:
            run(["patch", "--batch", "-p1", "-d", output], stdin=data)
    obj = output / "MetadataLoader-combined.o"
    run(["xcrun", "clang++", "-arch", args.arch, "-std=c++17", "-O2", "-fPIC", "-fno-rtti", "-fno-exceptions", "-I", prefix / "include", "-I", source / reader_path.parent, "-c", reader, "-o", obj])
    run(["meson", "compile", "-C", build, "msc-debug-metallib-probe", "llvm-metadata-error-stress"])
    for target, name in (("msc-debug-metallib-probe", "probe-combined"), ("llvm-metadata-error-stress", "stress-combined")):
        target_path = "src/airconv/darwin/" + target
        commands = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", target_path], text=True).splitlines()
        link = shlex.split(commands[-1])
        link[link.index("-o") + 1] = str(output / name)
        insert_at = next(i for i, arg in enumerate(link) if arg.startswith("-L"))
        link.insert(insert_at, str(obj))
        run(link, cwd=build)
    print("Candidate objects and probes:", output)


if __name__ == "__main__":
    main()
