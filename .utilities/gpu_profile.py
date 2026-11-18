"""Summarize GPU pass timings exported as CSV.

Usage: python gpu_profile.py capture.csv [capture2.csv ...] [--warmup 30] [--top 12]

Pass markers can be inclusive and nested, so their timings must not be summed.
GPU timings exclude CPU work and presentation. CSV quoting is handled by
csv.DictReader.
"""
import argparse
import csv
import math
import statistics
import sys
from pathlib import Path


def percentile95(values):
    ordered = sorted(values)
    # Nearest-rank p95, with ranks starting at one.
    return ordered[math.ceil(0.95 * len(ordered)) - 1]


def read_files(paths):
    samples = {}
    frames = set()
    seen = set()
    for path in paths:
        try:
            with path.open("r", newline="", encoding="utf-8-sig") as stream:
                reader = csv.DictReader(stream)
                required = {"frame", "pass", "gpu_ms"}
                if reader.fieldnames is None or not required.issubset(reader.fieldnames):
                    raise ValueError(f"{path}: expected CSV columns frame,pass,gpu_ms")
                for line, row in enumerate(reader, 2):
                    try:
                        frame_text = row["frame"]
                        name = row["pass"]
                        value_text = row["gpu_ms"]
                        if frame_text is None or name is None or value_text is None:
                            raise ValueError("missing value")
                        frame = int(frame_text)
                        if str(frame) != frame_text.strip() or frame < 0:
                            raise ValueError("frame must be a nonnegative integer")
                        name = name.strip()
                        if not name:
                            raise ValueError("pass name is empty")
                        value = float(value_text)
                        if not math.isfinite(value) or value < 0:
                            raise ValueError("gpu_ms must be finite and nonnegative")
                    except (TypeError, ValueError) as exc:
                        raise ValueError(f"{path}:{line}: malformed row ({exc})") from exc
                    key = (frame, name)
                    if key in seen:
                        raise ValueError(f"{path}:{line}: duplicate frame/pass {key!r}")
                    seen.add(key)
                    frames.add(frame)
                    samples.setdefault(name, {})[frame] = value
        except OSError as exc:
            raise ValueError(f"{path}: {exc}") from exc
    if not frames:
        raise ValueError("no CSV data rows")
    expected = set(range(min(frames), max(frames) + 1))
    if frames != expected:
        raise ValueError("frame ids must be contiguous")
    for frame in frames:
        if frame not in samples.get("gpu frame", {}):
            raise ValueError(f"frame {frame} is missing required 'gpu frame' pass")
    return samples


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="+", type=Path, metavar="CSV")
    parser.add_argument("--warmup", type=int, default=30, help="exclude frame ids below this value (default: 30)")
    parser.add_argument("--top", type=int, default=12, help="number of passes to show (default: 12)")
    args = parser.parse_args()
    if args.warmup < 0 or args.top < 1:
        parser.error("--warmup must be nonnegative and --top must be at least 1")
    for path in args.csv:
        try:
            report(path, args.warmup, args.top)
        except ValueError as exc:
            parser.error(str(exc))


def report(path, warmup, top):
    samples = read_files([path])
    rows = []
    for name, by_frame in samples.items():
        values = [v for frame, v in by_frame.items() if frame >= warmup]
        if values:
            rows.append((name, values))
    rows.sort(key=lambda row: (row[0] != "gpu frame", -statistics.mean(row[1]), row[0]))
    if not rows:
        raise ValueError(f"{path}: no samples remain after warmup")
    print(f"{path}: frame ids >= {warmup}")
    print(f"{'Pass':<30} {'N':>7} {'Median ms':>12} {'Mean ms':>12} {'P95 ms':>12}")
    for name, values in rows[:top]:
        print(f"{name:<30} {len(values):>7} {statistics.median(values):>12.3f} "
              f"{statistics.mean(values):>12.3f} {percentile95(values):>12.3f}")
    print("Inclusive nested markers overlap; do not sum pass timings. GPU timings exclude CPU work and presentation.")


if __name__ == "__main__":
    main()
