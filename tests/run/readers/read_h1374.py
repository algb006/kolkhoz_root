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
            text = re.sub(r" (U_by_day|EPS_list|T_by_month).*? (U_end|EPS_end|T_end)", "",
                          match.group(2).split(" — ")[0])
            words = text.split()
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
    variety_gate(base, body, common)
    return 0


def paired(base, body, common, base_key, body_key, needs):
    """Paired body[body_key] - base[base_key] over villages where both `needs` keys are > 0."""
    diffs = [body[v][body_key] - base[v][base_key] for v in common
             if base[v].get(needs[0], 0) > 0 and body[v].get(needs[1], 0) > 0
             and base_key in base[v] and body_key in body[v]]
    if len(diffs) < 3:
        return None
    mean = sum(diffs) / len(diffs)
    se = math.sqrt(sum((d - mean) ** 2 for d in diffs) / (len(diffs) - 1) / len(diffs))
    cut = sorted(diffs)[2:-2] if len(diffs) > 6 else sorted(diffs)
    trim = sum(cut) / len(cut)
    trim_se = math.sqrt(sum((d - trim) ** 2 for d in cut) / max(1, len(cut) - 1) / len(cut))
    movers = sorted((d for d in diffs if abs(d) > 0.05), key=abs, reverse=True)[:4]
    zeros = sum(1 for d in diffs if d == 0)
    return mean, se, trim, trim_se, len(diffs), movers, zeros


def variety_gate(base, body, common):
    """econ's variety gate on Y3's trade-off (boss [25] and after): the family-seasons that END under 5 categories
    (the value the satiety ceiling reads), the pig yards of the body against the goat yards of the base, and
    village-wide. RED if the mean OR the trimmed mean is past 2 SE. The family-days under 5 print beside it."""
    if not any("V_all_seasons_below5_share" in base[v] for v in common):
        print("VARIETY GATE: NOT MEASURED - no V_ fields in these runs")
        return
    print("VARIETY GATE (econ): seasons ENDING under 5 categories, share; RED if mean OR trimmed past 2 SE")
    rows = (
        ("pig yards (body) vs goat yards (base), seasons", "V_goat_seasons_below5_share",
         "V_pig_seasons_below5_share", ("V_goat_seasons", "V_pig_seasons"), True),
        ("village-wide, seasons", "V_all_seasons_below5_share", "V_all_seasons_below5_share",
         ("V_none_family_days", "V_none_family_days"), True),
        ("pig yards (body) vs goat yards (base), family-days", "V_goat_days_below5_share",
         "V_pig_days_below5_share", ("V_goat_family_days", "V_pig_family_days"), False),
        ("village-wide, family-days", "V_all_days_below5_share", "V_all_days_below5_share",
         ("V_none_family_days", "V_none_family_days"), False),
    )
    for title, base_key, body_key, needs, is_gate in rows:
        got = paired(base, body, common, base_key, body_key, needs)
        if got is None:
            print(f"  {title}: NOT MEASURED (under 3 villages with both)")
            continue
        mean, se, trim, trim_se, n, movers, zeros = got
        # DEGENERATE (p81: 21 of 32 village diffs exactly 0, the trimmed mean +0.0000154 against an SE of about
        # 1e-6): over a mostly-zero diff the trimmed SE is no spread at all and any rounding reads past 2 SE. When
        # more than half the diffs are 0 the trimmed half is not judged; the mean is, and the line says so.
        degenerate = zeros * 2 > n
        red = (se > 0 and abs(mean) > 2 * se) or (
            not degenerate and trim_se > 0 and abs(trim) > 2 * trim_se)
        role = ("GATE: " + ("RED" if red else "GREEN")) if is_gate else "diagnosis, not a gate"
        if degenerate:
            role += f" (DEGENERATE: {zeros} of {n} diffs are 0 - the trimmed mean is not judged)"
        print(f"  {title}: diff {mean:+.4f} SE {se:.4f} ({mean / se if se else 0:+.1f} SE), trimmed {trim:+.4f} "
              f"SE {trim_se:.4f}, n {n}, movers {', '.join(f'{d:+.3f}' for d in movers) or 'none'}  {role}")
    print("  RULE: if the seasons are GREEN and the family-days RED, read it as a slower season start, not a loss.")


if __name__ == "__main__":
    sys.exit(main())
