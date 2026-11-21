"""Compare benchmark runs without the scene's workload hiding the difference.

scripts/android_bench.py measures whatever part of the track the car reached,
and a race frame costs 3k D3D9 calls in a menu, 33k on an empty street and 50k
in traffic, so two runs of the same build can differ by 10 fps. Every 2 s
sample carries its own call count, so this compares only the samples whose
workload falls in one band (40k-58k calls/frame by default, a race with
traffic) and reports the median frame rate of each build over those.

    python -I scripts/bench_compare.py [--band 40000 58000] [label ...]
"""
import argparse
import json
import pathlib
import statistics

BENCH = pathlib.Path(__file__).resolve().parent.parent / "work" / "bench"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("labels", nargs="*", help="only these labels (default: all)")
    ap.add_argument("--band", nargs=2, type=float, default=(40000, 58000), metavar=("LOW", "HIGH"),
                    help="D3D9 calls/frame window the compared samples must fall in")
    ap.add_argument("--min-samples", type=int, default=3)
    args = ap.parse_args()
    low, high = args.band
    labels = set(args.labels)

    runs = {}
    for path in sorted(BENCH.glob("*.json")):
        try:
            data = json.loads(path.read_text())
        except (ValueError, OSError):
            continue
        label = (data.get("summary") or {}).get("label")
        if not label or (labels and label not in labels):
            continue
        for s in data.get("samples") or []:
            fps, calls = s.get("fps"), s.get("calls")
            if fps and calls and low <= calls <= high:
                runs.setdefault(label, []).append(
                    (float(fps), float(calls), float(s.get("waiting_ms") or 0), float(s.get("bridge_ms") or 0)))

    rows = []
    for label, points in runs.items():
        if len(points) < args.min_samples:
            continue
        rows.append((statistics.median(p[0] for p in points), label, len(points),
                     statistics.median(p[1] for p in points), statistics.median(p[2] for p in points),
                     statistics.median(p[3] for p in points)))
    if not rows:
        raise SystemExit(f"no runs with {args.min_samples}+ samples in {low:.0f}-{high:.0f} calls/frame")
    print(f"{'label':24s} {'n':>3s} {'fps':>6s} {'calls/frame':>12s} {'wait ms':>8s} {'bridge ms':>10s}")
    for fps, label, n, calls, wait, bridge in sorted(rows, reverse=True):
        print(f"{label:24s} {n:3d} {fps:6.1f} {calls:12.0f} {wait:8.2f} {bridge:10.2f}")


if __name__ == "__main__":
    main()
