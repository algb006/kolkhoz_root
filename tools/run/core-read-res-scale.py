#!/usr/bin/env python3
"""core: residents at year Y by village (SCALE2118's year lines), paired arm - base, with the
mean, SE, z, 10 % trimmed mean and median (econ's #1473 form: RED only if the mean AND the trimmed
mean are 2 SE out). usage: core-read-res-scale.py BASE_DIR ARM_DIR [YEAR...]
  BASE_DIR / ARM_DIR: fetched run dirs (/data/tmp/core-fetch-NAME/NAME) holding
  population_curve-o*.txt. «NOT MEASURED - n villages paired» when fewer than 3 pair up (a run that
  did not reach the year, or two different worlds)."""
import glob
import re
import statistics
import sys

LINE = re.compile(r"SCALE2118 seed (\d+) year (\d+) .* residents (\d+) ")


def read(directory):
    found = {}
    for path in sorted(glob.glob(directory + "/population_curve-o*.txt")):
        for line in open(path, encoding="utf-8", errors="replace"):
            match = LINE.search(line)
            if match:
                found[(int(match.group(1)), int(match.group(2)))] = int(match.group(3))
    return found


base = read(sys.argv[1])
arm = read(sys.argv[2])
years = [int(y) for y in sys.argv[3:]] or [4, 6, 16]
for year in years:
    seeds = sorted(s for (s, y) in base if y == year and (s, year) in arm)
    if len(seeds) < 3:
        print(f"y{year}: NOT MEASURED - {len(seeds)} villages paired")
        continue
    diffs = [arm[(s, year)] - base[(s, year)] for s in seeds]
    n = len(diffs)
    se = statistics.stdev(diffs) / n ** 0.5
    cut = int(n * 0.1)
    trimmed = sorted(diffs)[cut:n - cut]
    base_mean = statistics.mean(base[(s, year)] for s in seeds)
    print(f"y{year}: {n} villages, base mean {base_mean:.1f}; arm-base mean {statistics.mean(diffs):+.1f} "
          f"SE {se:.1f} (z {statistics.mean(diffs) / se if se else 0:+.2f}); trimmed "
          f"{statistics.mean(trimmed):+.1f}; median {statistics.median(diffs):+.1f}; "
          f"worst {min(diffs):+d}")
