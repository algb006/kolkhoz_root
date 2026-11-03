"""E1 doors paired by sample order, both offsets. Usage: read_doors.py A_DIR B_DIR (prints B - A).

A_DIR and B_DIR each hold population_curve-o0.txt and population_curve-o100.txt (a pair's two arms, both seed
offsets). econ's standing rule for every E1 step (econ-boss-e1-heavy-tail-2026-11-03 [1], #1473): beside the mean
and its SE, the median paired difference, the trimmed mean (the 2 highest and 2 lowest villages dropped) with its
own SE, and the villages moved earlier / later by more than 0.3 y. RED only if the mean AND the trimmed mean are
both past 2 SE; GREEN only if both are inside; otherwise TAIL-DRIVEN, with the villages named. A negative
difference is an EARLIER door.
"""
import re
import statistics
import sys

DOORS = re.compile(r"the era corridor — doors in fractional years: ([\d. ]+); mean")
SEEDS = re.compile(r"seed (\d+) Epoch II opened y")
KMOVE = 0.3
KTRIM = 2


def read(path):
    out, seeds = {}, {}
    for off in ("o0", "o100"):
        seeds[off] = []
        for line in open(f"{path}/population_curve-{off}.txt", encoding="utf-8"):
            m = DOORS.search(line)
            if m and off not in out:
                out[off] = [float(v) for v in m.group(1).split()]
            m = SEEDS.search(line)
            if m and int(m.group(1)) not in seeds[off]:
                seeds[off].append(int(m.group(1)))
    return out, seeds


def mean_se(values):
    if len(values) < 2:
        return float("nan"), float("nan")
    return statistics.mean(values), statistics.stdev(values) / len(values) ** 0.5


def main():
    (a, a_seeds), (b, _) = read(sys.argv[1]), read(sys.argv[2])
    for off in ("o0", "o100"):
        print(f"  {off}: A mean {statistics.mean(a[off]):.4f}, B mean {statistics.mean(b[off]):.4f}")
    da = a["o0"] + a["o100"]
    db = b["o0"] + b["o100"]
    names = a_seeds["o0"] + a_seeds["o100"]
    names = names if len(names) == len(da) else [f"#{i}" for i in range(len(da))]
    d = [y - x for x, y in zip(da, db)]
    m, se = mean_se(d)
    print(f"  pooled: A {statistics.mean(da):.4f} B {statistics.mean(db):.4f}; paired B-A "
          f"{m:+.4f} SE {se:.4f} ({m / se if se else float('nan'):+.1f} SE) n {len(d)}")
    order = sorted(range(len(d)), key=lambda i: d[i])
    kept = [d[i] for i in order[KTRIM:len(d) - KTRIM]]
    tm, tse = mean_se(kept)
    print(f"  median {statistics.median(d):+.4f}; trimmed mean (drop {KTRIM}+{KTRIM}) {tm:+.4f} SE {tse:.4f}"
          f" ({tm / tse if tse else float('nan'):+.1f} SE) n {len(kept)}")
    earlier = [(names[i], d[i]) for i in range(len(d)) if d[i] < -KMOVE]
    later = [(names[i], d[i]) for i in range(len(d)) if d[i] > KMOVE]

    def fmt(xs):
        return ", ".join(f"{n} {v:+.2f}" for n, v in sorted(xs, key=lambda t: t[1])) or "none"

    print(f"  moved earlier > {KMOVE} y: {len(earlier)} ({fmt(earlier)}); "
          f"later > {KMOVE} y: {len(later)} ({fmt(later)})")
    past_mean = se > 0 and abs(m) > 2 * se
    past_trim = tse > 0 and abs(tm) > 2 * tse
    verdict = "RED" if past_mean and past_trim else "GREEN" if not past_mean and not past_trim else "TAIL-DRIVEN"
    dropped = [f"{names[i]} {d[i]:+.2f}" for i in order[:KTRIM] + order[len(d) - KTRIM:]]
    print(f"  E1 STEP: {verdict} (mean {'past' if past_mean else 'inside'} 2 SE, trimmed "
          f"{'past' if past_trim else 'inside'} 2 SE); the dropped villages: {', '.join(dropped)}")


if __name__ == "__main__":
    main()
