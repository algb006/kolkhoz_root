"""Two beach worlds: population_curve's R1373 keys paired by village (#1373, #1405).

Usage: read_beach.py OLD_DIR NEW_DIR   (each with population_curve-o0.txt and -o100.txt)
A family with a beach in reach is always booked kBathBeach (labor_system.cpp, the evening rest close); kBathWild
only with no beach in reach and a wild place within leisure_reach_m.
"""
import re
import statistics
import sys

KEYS = ("bath_beach", "bath_wild", "outdoor_pd_JunAug", "below40", "below20", "walk_offs_y1y3",
        "dayoff_leisure", "dayoff_plain", "teen_pd_JunAug")


def read(path):
    out = {}
    for off in ("o0", "o100"):
        for line in open(f"{path}/population_curve-{off}.txt", encoding="utf-8"):
            m = re.search(r"R1373 seed (\d+) (.*)", line)
            if not m:
                continue
            vals = dict(re.findall(r"(\w+) (-?[\d.]+)", m.group(2)))
            out[int(m.group(1))] = {k: float(vals[k]) for k in KEYS if k in vals}
    return out


def main():
    old, new = read(sys.argv[1]), read(sys.argv[2])
    seeds = sorted(set(old) & set(new))
    print(f"villages {len(seeds)} (expected 32); old-only {len(set(old) - set(new))}, "
          f"new-only {len(set(new) - set(old))}")
    for key in KEYS:
        pairs = [(new[s][key], old[s][key]) for s in seeds if key in new[s] and key in old[s]]
        if len(pairs) < 2:
            print(f"  {key}: NOT MEASURED ({len(pairs)} villages carry it)")
            continue
        d = [a - b for a, b in pairs]
        m, se = statistics.mean(d), statistics.stdev(d) / len(d) ** 0.5
        print(f"  {key}: old {statistics.mean(b for _, b in pairs):.1f} new "
              f"{statistics.mean(a for a, _ in pairs):.1f} a village; paired {m:+.1f} SE {se:.1f} "
              f"({m / se if se else float('nan'):+.1f} SE), worst village {min(d):+.0f}, n {len(d)}")


if __name__ == "__main__":
    main()
