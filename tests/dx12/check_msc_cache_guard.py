#!/usr/bin/env python3
"""Check isolated seeded-cache controls for the temporary MSC cache guard.

The positive run uses the fragment case. The negative run uses
fragment-vertexid and intentionally exposes the known unpatched ICB validation
failure. Both must be produced with run_msc_lod_experiment.py and the same seed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sqlite3


def entries(path):
    with sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True) as db:
        return {key.hex(): hashlib.sha256(value).hexdigest()
                for key, value in db.execute("SELECT key, value FROM cache_33")}


def summary(directory, name):
    data = json.loads((directory / "summary.json").read_text())
    case, = data["results"]
    if case["case"] != name:
        raise AssertionError(f"expected {name}, got {case['case']}")
    for field in ("d3d12_loaded", "dxgi_loaded", "bridge_loaded", "unix_loaded",
                  "loaded_code_matches", "validation_enabled"):
        if not case[field]:
            raise AssertionError(f"{name}: {field} is false")
    return data, case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for argument in ("seed", "positive", "negative"):
        parser.add_argument("--" + argument, type=Path, required=True)
    args = parser.parse_args()
    good, good_case = summary(args.positive, "fragment")
    bad, bad_case = summary(args.negative, "fragment-vertexid")
    seed_hash = hashlib.sha256(args.seed.read_bytes()).hexdigest()
    if good["seed_cache_sha256"] != seed_hash or bad["seed_cache_sha256"] != seed_hash:
        raise AssertionError("runs did not use the specified cache seed")
    if not good_case["pass"] or bad_case["exit"] == 0 or not bad_case["validation_error"]:
        raise AssertionError("expected successful control and known validation failure")
    log = (args.negative / "fragment-vertexid.log").read_text(errors="replace")
    if "Invalid load in 'device' or 'constant' address space" not in log:
        raise AssertionError("negative case failed for an unexpected reason")
    baseline = entries(args.seed)
    if not baseline:
        raise AssertionError("seed contains no MSC cache entries")
    for directory in (args.positive, args.negative):
        if entries(directory / "cache" / "shaders_320.db") != baseline:
            raise AssertionError(f"MSC persistent cache changed: {directory}")
    print("loaded identities, positive output, negative control, and cache contents verified")


if __name__ == "__main__":
    main()
