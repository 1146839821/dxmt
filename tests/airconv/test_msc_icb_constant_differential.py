#!/usr/bin/env python3
"""Execute defined in-range LLVM inputs before and after the offline rewrite."""
import pathlib
import subprocess
import sys
import tempfile


EXPECTED = b"""00000000 00000000
00000001 80000000
ffffffff 3f800000
80000000 bf800000
7fffffff 7f800000
12345678 7fc00000
0000002a 7fc00001
"""


def run(*args):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout


def main():
    rewriter, lli, fixture = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="dxmt-icb-differential-") as directory:
        output = pathlib.Path(directory) / "rewritten.ll"
        run(rewriter, "--rewrite-ir", fixture, str(output))
        original = run(lli, fixture)
        rewritten = run(lli, str(output))
        if original != EXPECTED or rewritten != EXPECTED:
            raise AssertionError(f"unexpected output: original={original!r}, rewritten={rewritten!r}")
        second_output = pathlib.Path(directory) / "rewritten-twice.ll"
        second = subprocess.run([rewriter, "--rewrite-ir", str(output), str(second_output)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if second.returncode != 5 or second_output.exists():
            raise AssertionError("a serialized rewritten module must not be rewritten again")
        bad = pathlib.Path(directory) / "bad.ll"
        bad.write_text("not LLVM IR\n")
        failed_output = pathlib.Path(directory) / "bad-output.ll"
        failed = subprocess.run([rewriter, "--rewrite-ir", str(bad), str(failed_output)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if failed.returncode == 0 or failed_output.exists():
            raise AssertionError("invalid input must fail without producing output")
        unwritable = subprocess.run([rewriter, "--rewrite-ir", fixture,
                                    str(pathlib.Path(directory) / "missing" / "out.ll")],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if unwritable.returncode != 6:
            raise AssertionError("invalid output path must report an output error")
    print("defined in-range integer and float bit patterns matched")


if __name__ == "__main__":
    main()
