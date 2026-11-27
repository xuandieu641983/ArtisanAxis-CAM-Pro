#!/usr/bin/env python3
"""Create exact-size binary STL fixtures and benchmark each in fresh processes."""
import argparse
import json
import math
from pathlib import Path
import statistics
import struct
import subprocess
import tempfile


def heightfield(path, total):
    columns = 1000
    rows = total // (2 * columns)
    if 2 * columns * rows != total:
        raise ValueError("fixture requires a whole number of grid cells")

    facet = struct.Struct("<12fH")
    def row(y):
        return [(x, y, 3.0 + 0.7 * math.sin(x * 0.13) * math.cos(y * 0.19))
                for x in (100.0 * i / columns for i in range(columns + 1))]

    with path.open("wb", buffering=1024 * 1024) as stl:
        stl.write(b"ArtisanAXIS BVH benchmark STL".ljust(80, b" "))
        stl.write(struct.pack("<I", total))
        low = row(0.0)
        chunk = bytearray(2 * columns * facet.size)
        for j in range(rows):
            high = row(45.0 * (j + 1) / rows)
            for i in range(columns):
                p0, p1, p2, p3 = low[i], low[i + 1], high[i + 1], high[i]
                facet.pack_into(chunk, 2 * i * facet.size, 0, 0, 1, *p0, *p1, *p2, 0)
                facet.pack_into(chunk, (2 * i + 1) * facet.size, 0, 0, 1, *p0, *p2, *p3, 0)
            stl.write(chunk)
            low = high
    if path.stat().st_size != 84 + 50 * total:
        raise RuntimeError("incorrect fixture length")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--repeat", type=int, default=3)
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error("--repeat must be positive")

    executable = args.executable.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="artisan-bvh-") as output:
        fixture = Path(output) / "fixture.stl"
        for n in (1_000_000, 2_000_000, 3_000_000):
            heightfield(fixture, n)
            runs = []
            for _ in range(args.repeat):
                report = subprocess.run([str(executable), str(fixture)],
                                        capture_output=True, check=True, text=True)
                result = json.loads(report.stdout)
                if result["triangles"] != n:
                    raise RuntimeError(f"incorrect decoded triangle count: {result}")
                runs.append(result)
            medians = {key: statistics.median(run[key] for run in runs)
                       for key in ("decode_and_convert_seconds", "bvh_seconds",
                                   "query_seconds", "queries_per_second")}
            print(json.dumps({"stl_triangles": n, "repeat": args.repeat,
                              "peak_mib_max": max(run["peak_mib"] for run in runs),
                              "median": medians, "runs": runs}), flush=True)


if __name__ == "__main__":
    main()
