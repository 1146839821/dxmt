#!/usr/bin/env python3
"""Create a new isolated MSC conversion cache with one experimental metallib.

Never use the output as a production cache. The exact input metallib hash and
cache key must be specified to prevent replacing an unrelated conversion.
"""
import argparse
import hashlib
from pathlib import Path
import sqlite3
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--key", required=True, help="hexadecimal cache key")
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--metallib", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    output = args.output.absolute()
    if source == output.resolve():
        parser.error("output must differ from source")
    try:
        key = bytes.fromhex(args.key)
    except ValueError:
        parser.error("key must be hexadecimal")
    patched = args.metallib.read_bytes()
    if not key or len(patched) < 24 or patched[:4] != b"MTLB" or struct.unpack_from("<Q", patched, 16)[0] != len(patched):
        parser.error("invalid key or metallib")
    with output.open("xb"):
        pass
    src = sqlite3.connect(source.as_uri() + "?mode=ro", uri=True)
    dst = sqlite3.connect(output)
    try:
        src.backup(dst)
        row = dst.execute("SELECT value FROM cache_33 WHERE key = ?", (key,)).fetchone()
        if not row:
            raise ValueError("cache key is absent")
        value = row[0]
        offset = value.find(b"MTLB")
        if len(value) < 32 or value[:4] != b"MSCC" or offset < 32:
            raise ValueError("unexpected MSC cache record")
        original_size, stage_size, entry_size = struct.unpack_from("<QQQ", value, 8)
        if offset + original_size + stage_size + entry_size != len(value):
            raise ValueError("inconsistent MSC cache sizes")
        original = value[offset:offset + original_size]
        if hashlib.sha256(original).hexdigest() != args.expected_sha256.lower():
            raise ValueError("original metallib hash differs")
        header = bytearray(value[:offset])
        struct.pack_into("<Q", header, 8, len(patched))
        replacement = bytes(header) + patched + value[offset + original_size:]
        dst.execute("UPDATE cache_33 SET value = ? WHERE key = ?", (replacement, key))
        dst.commit()
    except Exception:
        dst.close()
        src.close()
        output.unlink()
        raise
    finally:
        dst.close()
        src.close()
    print("original", hashlib.sha256(original).hexdigest())
    print("patched", hashlib.sha256(patched).hexdigest())
    print("isolated cache", output)


if __name__ == "__main__":
    main()
