#!/usr/bin/env python3
"""Compare LLVM's constant folding of defined GEP wrap cases.

The unusual layouts are compiler-level tests; a host JIT need not implement
these address-space index widths, so executing them with lli is not an oracle.
"""
import pathlib
import re
import subprocess
import sys
import tempfile


IR = '''
target datalayout = "e-p:64:64-p2:64:64:64:{width}"
@v = internal addrspace(2) constant [2 x i32] [i32 11, i32 22]
define i32 @read(i64 %idx) {{
  %p = getelementptr [2 x i32], [2 x i32] addrspace(2)* @v, i64 0, i64 %idx
  %x = load i32, i32 addrspace(2)* %p
  ret i32 %x
}}
define i32 @main() {{
  %a = call i32 @read(i64 0)
  %b = call i32 @read(i64 1)
  %c = call i32 @read(i64 {period})
  %d = call i32 @read(i64 {next})
  %e = call i32 @read(i64 -{period})
  %f = call i32 @read(i64 {twice_next})
  %ca = icmp eq i32 %a, 11
  %cb = icmp eq i32 %b, 22
  %cc = icmp eq i32 %c, 11
  %cd = icmp eq i32 %d, 22
  %ce = icmp eq i32 %e, 11
  %cf = icmp eq i32 %f, 22
  %ab = and i1 %ca, %cb
  %cdok = and i1 %cc, %cd
  %ef = and i1 %ce, %cf
  %abcd = and i1 %ab, %cdok
  %all = and i1 %abcd, %ef
  %result = select i1 %all, i32 0, i32 1
  ret i32 %result
}}
'''


def run(*command):
    return subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def main():
    rewriter, opt = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="dxmt-icb-index-layout-") as tmp:
        for width in (8, 16):
            period = 1 << width
            source = pathlib.Path(tmp) / f"source-{width}.ll"
            rewritten = pathlib.Path(tmp) / f"rewritten-{width}.ll"
            source.write_text(IR.format(width=width, period=period, next=period + 1,
                                        twice_next=2 * period + 1))
            run(rewriter, "--rewrite-ir", str(source), str(rewritten))
            text = rewritten.read_text()
            if "icb.constant.fallback" not in text or "!dxmt.icb.constant.fallback" not in text:
                raise AssertionError("original load is missing from fallback")
            for module in (source, rewritten):
                result = run(opt, "-S", "-passes=default<O2>", str(module), "-o", "-").decode()
                function = re.search(r"define\b[^\n]*@main\([^\n]*\).*?\n}", result, re.S)
                if not function or not re.search(r"\bret i32 0\b", function.group()):
                    raise AssertionError(f"unexpected optimized result for {module}:\n{result}")
            broken_text, replacements = re.subn(r"  %x = load i32[^\n]*", "  %x = add i32 0, 0", text)
            if replacements != 1:
                raise AssertionError("could not construct the deliberately broken control")
            broken = pathlib.Path(tmp) / f"broken-{width}.ll"
            broken.write_text(broken_text)
            result = run(opt, "-S", "-passes=default<O2>", str(broken), "-o", "-").decode()
            function = re.search(r"define\b[^\n]*@main\([^\n]*\).*?\n}", result, re.S)
            if not function or not re.search(r"\bret i32 1\b", function.group()):
                raise AssertionError("broken fallback control was not detected")
    print("8- and 16-bit pointer index wrap cases agree after constant folding")


if __name__ == "__main__":
    main()
