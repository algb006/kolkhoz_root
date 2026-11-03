"""A change's E1 delta by village in two worlds, side by side (the rule «two instruments disagree - look at WHICH
rows differ»; first used for the warm crew, core-boss-1084-funeral-body [13]).

Usage: read_crew_rows.py WORLD1_OFF WORLD1_ON WORLD2_OFF WORLD2_ON
Each dir holds population_curve-o0.txt and -o100.txt. Door year = «Epoch II opened yN dD», y - 1 + d/48; the
builders sent are the WARMLATE line's crew_sent of the ON arms.
"""
import re
import statistics
import sys

DOOR = re.compile(r"seed (\d+) Epoch II opened y(\d+) d(\d+)")
CREW = re.compile(r"WARMLATE seed (\d+) late_warm_works (\d+)(?: freezing_warm_works \d+)? crew_sent (\d+)")


def read(path):
    doors, crew = {}, {}
    for off in ("o0", "o100"):
        for line in open(f"{path}/population_curve-{off}.txt", encoding="utf-8"):
            m = DOOR.search(line)
            if m and int(m.group(1)) not in doors:
                doors[int(m.group(1))] = int(m.group(2)) - 1 + int(m.group(3)) / 48.0
            m = CREW.search(line)
            if m:
                crew[int(m.group(1))] = int(m.group(3))
    return doors, crew


def main():
    t_off, t_on, f_off, f_on = (read(p) for p in sys.argv[1:5])
    seeds = sorted(set(t_off[0]) & set(t_on[0]) & set(f_off[0]) & set(f_on[0]))
    print(f"villages {len(seeds)} (expected 32; a village whose door never opened in one arm drops out)")
    rows = []
    for s in seeds:
        dt = t_on[0][s] - t_off[0][s]
        df = f_on[0][s] - f_off[0][s]
        rows.append((abs(dt - df), s, dt, df, t_off[0][s], t_on[0][s], f_off[0][s], f_on[0][s],
                     t_on[1].get(s, -1), f_on[1].get(s, -1)))
    for label, col in (("world 1", 2), ("world 2", 3)):
        vals = [r[col] for r in rows]
        print(f"  {label}: mean {statistics.mean(vals):+.4f} y, SE {statistics.stdev(vals) / len(vals) ** 0.5:.4f},"
              f" median {statistics.median(vals):+.4f}, moved (|d|>0.01) {sum(1 for v in vals if abs(v) > 0.01)}")
    print("  the 8 villages where the two worlds differ most (door years: off -> on):")
    for r in sorted(rows, reverse=True)[:8]:
        print(f"    seed {r[1]}: world 1 {r[2]:+.3f} ({r[4]:.2f} -> {r[5]:.2f}, crew {r[8]}),"
              f" world 2 {r[3]:+.3f} ({r[6]:.2f} -> {r[7]:.2f}, crew {r[9]}), gap {r[0]:.3f}")
    both_zero = sum(1 for r in rows if abs(r[2]) <= 0.01 and abs(r[3]) <= 0.01)
    print(f"  villages unmoved in both worlds: {both_zero} of {len(rows)}")


if __name__ == "__main__":
    main()
