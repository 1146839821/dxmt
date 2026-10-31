#!/usr/bin/env python3
"""Exercise isolated cache replacement and its refusal paths."""
import hashlib
from pathlib import Path
import sqlite3
import struct
import subprocess
import sys
import tempfile


def invoke(script, source, output, key, expected, metallib):
    return subprocess.run([sys.executable, script, "--source", str(source), "--output", str(output),
                           "--key", key.hex(), "--expected-sha256", expected, "--metallib", str(metallib)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    script = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="dxmt-icb-cache-") as directory:
        root = Path(directory)
        original = b"MTLB" + b"\0" * 12 + struct.pack("<Q", 24)
        patched = original + b"abcd"
        patched = patched[:16] + struct.pack("<Q", len(patched)) + patched[24:]
        key = b"test-key"
        value = b"MSCC" + b"\0" * 4 + struct.pack("<QQQ", len(original), 0, 0) + original
        source = root / "source.db"
        with sqlite3.connect(source) as db:
            db.execute("CREATE TABLE cache_33 (key BLOB PRIMARY KEY, value BLOB)")
            db.execute("INSERT INTO cache_33 VALUES (?, ?)", (key, value))
        original_db_hash = hashlib.sha256(source.read_bytes()).digest()
        metallib = root / "patched.metallib"
        metallib.write_bytes(patched)
        output = root / "output.db"
        digest = hashlib.sha256(original).hexdigest()
        wrong = invoke(script, source, output, key, "0" * 64, metallib)
        if wrong.returncode == 0 or output.exists():
            raise AssertionError("wrong input hash must fail and remove output")
        missing = invoke(script, source, output, b"missing", digest, metallib)
        if missing.returncode == 0 or output.exists():
            raise AssertionError("missing key must fail and remove output")
        success = invoke(script, source, output, key, digest, metallib)
        if success.returncode != 0:
            raise AssertionError(success.stderr.decode())
        with sqlite3.connect(output) as db:
            stored = db.execute("SELECT value FROM cache_33 WHERE key = ?", (key,)).fetchone()[0]
        if stored[32:] != patched or struct.unpack_from("<Q", stored, 8)[0] != len(patched):
            raise AssertionError("cache replacement is incorrect")
        before = hashlib.sha256(output.read_bytes()).digest()
        exists = invoke(script, source, output, key, digest, metallib)
        same = invoke(script, source, source, key, digest, metallib)
        if exists.returncode == 0 or same.returncode == 0 or hashlib.sha256(output.read_bytes()).digest() != before:
            raise AssertionError("existing or source output must be refused without changes")
        if hashlib.sha256(source.read_bytes()).digest() != original_db_hash:
            raise AssertionError("source cache changed")
    print("isolated cache success and refusal paths passed")


if __name__ == "__main__":
    main()
