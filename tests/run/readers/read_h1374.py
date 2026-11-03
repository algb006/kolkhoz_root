#!/usr/bin/env python3
"""#1374's pair reader: the H1374 lines of two arms, village by village.

Each H1374 line carries `key value` pairs after `seed S` (tests/run/common/herd_tally.h). For every key the
reader prints the base and body means, the paired difference with its SE, how many SEs that is, and the
trimmed (2+2) mean of the differences - econ's #1473 form. A village in one arm only is shouted, not dropped.

Usage: read_h1374.py BASE_DIR BODY_DIR   (each holds population_curve-o*.txt)
Exit: 0 read, 2 nothing to read.
"""
import math
import pathlib
import re
import sys

LINE = re.compile(r"H1374 seed (\d+) (.*)$")


def villages(folder):
    """{(file offset, seed): {key: float}} from every population_curve-o*.txt under the folder."""
    found = {}
    for path in sorted(pathlib.Path(folder).rglob("population_curve-o*.txt")):
        offset = path.stem.split("-o")[-1]
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = LINE.search(line)
            if not match:
                continue
            words = match.group(2).split(" — ")[0].split()
            values = {}
            for index in range(0, len(words) - 1, 2):
                try:
                    values[words[index]] = float(words[index + 1])
                except ValueError:
                    pass
            if " — " in match.group(2):
                print(f"SHOUT: {path.name} seed {match.group(1)}: {match.group(2).split(' — ')[1]}")
            found[(offset, match.group(1))] = values
    return found


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    base, body = villages(sys.argv[1]), villages(sys.argv[2])
    print(f"villages: base {len(base)}, body {len(body)}, expected 64 each")
    if not base or not body:
        print("NOT MEASURED: an arm has no H1374 line")
        return 2
    for name, other in (("base", body), ("body", base)):
        missing = sorted(set(other) - set(base if name == "base" else body))
        if missing:
            print(f"SHOUT: {len(missing)} village(s) missing from {name}: {missing[:8]}")
    common = sorted(set(base) & set(body))
    keys = list(next(iter(base.values())).keys())
    print(f"{'key':28} {'base':>10} {'body':>10} {'diff':>9} {'SE':>8} {'n SE':>6} {'trim':>9}  verdict")
    for key in keys:
        diffs = [body[v][key] - base[v][key] for v in common if key in base[v] and key in body[v]]
        if len(diffs) < 3:
            print(f"{key:28} NOT MEASURED ({len(diffs)} pairs)")
            continue
        mean_base = sum(base[v][key] for v in common) / len(common)
        mean_body = sum(body[v][key] for v in common) / len(common)
        mean = sum(diffs) / len(diffs)
        var = sum((d - mean) ** 2 for d in diffs) / (len(diffs) - 1)
        se = math.sqrt(var / len(diffs))
        cut = sorted(diffs)[2:-2] if len(diffs) > 6 else sorted(diffs)
        trim = sum(cut) / len(cut)
        trim_var = sum((d - trim) ** 2 for d in cut) / max(1, len(cut) - 1)
        trim_se = math.sqrt(trim_var / len(cut))
        n_se = mean / se if se > 0 else (0.0 if mean == 0 else math.inf)
        red = se > 0 and abs(mean) > 2 * se and trim_se > 0 and abs(trim) > 2 * trim_se
        still = "STILL" if all(d == 0 for d in diffs) else ""
        verdict = "PAST 2 SE" if red else (still or "within")
        print(f"{key:28} {mean_base:10.2f} {mean_body:10.2f} {mean:9.2f} {se:8.2f} {n_se:6.1f} {trim:9.2f}  "
              f"{verdict}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
