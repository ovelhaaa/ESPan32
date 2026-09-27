"""M6.3.5 forensics tail analysis.

Parses [CURVE] and [HIST] rows from a connected capture and reports the
percentiles implied by the raw 25 us histogram bins plus the population of the
residual-tail regions.  Hardware timing only; never substitutes host numbers.
"""
import re
import sys
from pathlib import Path

BIN_US = 25


def read(path):
    curves = {}
    bins = {}
    for line in Path(path).read_text(errors="replace").splitlines():
        if "[CURVE]" in line:
            f = dict(re.findall(r"(\w+)=([^\s\x1b]+)", line))
            curves[(int(f["fixture"]), f["class"])] = f
        elif "[HIST]" in line:
            f = dict(re.findall(r"(\w+)=([^\s\x1b]+)", line))
            key = (int(f["fixture"]), f["class"])
            bins.setdefault(key, {})[int(f["lower_us"])] = int(f["n"])
    return curves, bins


def quantile(bins, count, pct):
    rank = (count * pct + 99) // 100
    cumulative = 0
    for lower in sorted(bins):
        cumulative += bins[lower]
        if cumulative >= rank:
            return lower, lower + BIN_US
    return None, None


def region(bins, lo, hi=None):
    if hi is None:
        return sum(n for lower, n in bins.items() if lower >= lo)
    return sum(n for lower, n in bins.items() if lo <= lower < hi)


def report(path, fixtures):
    curves, bins = read(path)
    print(f"\n### {Path(path).name}\n")
    print("| Fixture | Class | N | Avg us | Max us | P90 | P95 | P99 | P99.5 |")
    print("|---|---|---:|---:|---:|---:|---:|---:|---:|")
    for key in sorted(curves):
        fixture, kind = key
        if fixture not in fixtures:
            continue
        c = curves[key]
        b = bins.get(key, {})
        n = int(c["n"])
        p90 = quantile(b, n, 90)
        p95 = quantile(b, n, 95)
        p99 = quantile(b, n, 99)
        p995 = quantile(b, n, 995)
        label = f'{c["model"]}/{fixture} ({c["voices"]}v)'
        def fmt(q):
            return f"{q[0]}-{q[1]}" if q[0] is not None else "-"
        print(f'| {label} | {kind} | {n} | {c["avg_us"]} | {c["max_us"]} | '
              f'{fmt(p90)} | {fmt(p95)} | {fmt(p99)} | {fmt(p995)} |')
    print("\n| Fixture | Class | 1900-2100 | 2100-2200 | 2200-2300 | 2300-2400 | >2400 |")
    print("|---|---|---:|---:|---:|---:|---:|")
    for key in sorted(curves):
        fixture, kind = key
        if fixture not in fixtures:
            continue
        c = curves[key]
        b = bins.get(key, {})
        label = f'{c["model"]}/{fixture} ({c["voices"]}v)'
        print(f'| {label} | {kind} | {region(b,1900,2100)} | {region(b,2100,2200)} | '
              f'{region(b,2200,2300)} | {region(b,2300,2400)} | {region(b,2400)} |')


if __name__ == "__main__":
    fixtures = set(int(x) for x in sys.argv[1].split(",")) if sys.argv[1:2] else {5, 13, 14}
    for name in sys.argv[2:] if len(sys.argv) > 2 else []:
        report(name, fixtures)
