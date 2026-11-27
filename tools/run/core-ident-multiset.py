#!/usr/bin/env python3
"""core: identity of two population_curve outputs as MULTISETS of lines (thread order ignored),
dropping the timing lines by name. usage: core-ident-multiset.py BASE_FILE ARM_FILE
Prints the count of differing lines each side and the first few, new-in-ARM first. A new print
or a new column in an existing one differs by construction - name it before calling a world
«changed» (26 Nov: AUTH2164's new deed column was the only difference)."""
import collections
import re
import sys

DROP = re.compile(r"TIME2072|SCALE2118|route_cpu_s|worst_day|seconds|walkers \d+ of|cpu_|wall")


def lines(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return collections.Counter(line.rstrip("\n") for line in handle if not DROP.search(line))


base, arm = lines(sys.argv[1]), lines(sys.argv[2])
only_arm = arm - base
only_base = base - arm
print(f"lines base {sum(base.values())} arm {sum(arm.values())}; "
      f"only in arm {sum(only_arm.values())}, only in base {sum(only_base.values())}")
for name, side in (("ARM", only_arm), ("BASE", only_base)):
    for line, count in list(side.items())[:12]:
        print(f"{name} x{count}: {line[:220]}")
