#!/usr/bin/env python3
"""Every enum a consumer of the boundary can be handed, and whether it
carries its own length.

WHY IT EXISTS. Terminators were handed out from a list somebody wrote, and
the list was short three times in one day (StockLight, NoDataReason, and the
morning's batch). "Every mirrored enum has a terminator" is a claim, and a
claim is proved by enumeration, not by an example. This takes the list from
the headers instead of from memory.

REACHABILITY, and it is the whole point. The seed is the boundary contract
(include/core_boundary/session.h): the types its methods take and return.
From there the walk follows struct fields transitively through every public
header, so an enum buried three structs deep inside WorldState is found the
same way as one named in a signature.

WHAT IT CANNOT DO. It is a regex reader, not a compiler: it understands the
shapes this codebase actually writes. A header in a style it has not seen
would be read as having no fields, which fails SILENTLY QUIET rather than
loudly — so the count it prints is a floor, and the way to keep it honest is
to run it after every boundary change and look at the total, not only at the
verdict.
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "include")

ENUM_RE = re.compile(r"enum class (\w+)\s*(?::\s*[\w:]+\s*)?\{(.*?)\n\};", re.S)
# A waiver, written where the enum is and not in a report: the name of the
# constant that already IS this enum's length. Three of them exist, all in the
# calendar, where the length is declared first and the enum follows it.
WAIVER_RE = re.compile(r"@enum_length (\w+)")
RECORD_RE = re.compile(r"\n(?:struct|class) (\w+)\s*(?:final\s*)?(?::[^{;]*)?\{(.*?)\n\};", re.S)
# Rows travel under aliases (`using UnitTable = StateTable<UnitRow>;`), and a
# walk that does not open them stops one step short of every row layout —
# which is where most of the mirrored enums live.
ALIAS_RE = re.compile(r"\busing (\w+)\s*=\s*([^;]+);")
# A field or a signature line: pick every identifier that could be a type.
WORD_RE = re.compile(r"\b([A-Z]\w+)\b")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def collect():
    enums, records, aliases = {}, {}, {}
    for base, _, files in os.walk(ROOT):
        for name in files:
            if not name.endswith((".h", ".hpp", ".inl")):
                continue
            path = os.path.join(base, name)
            raw = open(path, errors="ignore").read()
            body = strip_comments(raw)
            for match in ENUM_RE.finditer(raw):
                values = [v.strip() for v in strip_comments(match.group(2)).split(",") if v.strip()]
                doc = raw[max(0, match.start() - 1200):match.start()]
                waiver = WAIVER_RE.findall(doc)
                enums[match.group(1)] = (
                    os.path.relpath(path, ROOT), values, waiver[-1] if waiver else None)
            for match in RECORD_RE.finditer(body):
                records[match.group(1)] = match.group(2)
            for match in ALIAS_RE.finditer(body):
                aliases[match.group(1)] = match.group(2)
    return enums, records, aliases


def main():
    enums, records, aliases = collect()
    seed_path = os.path.join(ROOT, "core_boundary", "session.h")
    frontier = set(WORD_RE.findall(strip_comments(open(seed_path, errors="ignore").read())))
    seen = set()
    while frontier:
        name = frontier.pop()
        if name in seen:
            continue
        seen.add(name)
        if name in records:
            frontier |= set(WORD_RE.findall(records[name]))
        if name in aliases:
            frontier |= set(WORD_RE.findall(aliases[name]))

    reached = sorted(name for name in enums if name in seen)
    unreached = sorted(name for name in enums if name not in seen)
    with_terminator, waived, without = [], [], []
    for name in reached:
        path, values, waiver = enums[name]
        if values and re.fullmatch(r"k\w*Count", values[-1]):
            with_terminator.append((name, path, len(values) - 1, ""))
        elif waiver:
            waived.append((name, path, len(values), waiver))
        else:
            without.append((name, path, len(values), ""))

    print(f"BOUNDARY_ENUMS={len(reached)} WITH_TERMINATOR={len(with_terminator)} "
          f"WAIVED={len(waived)} MISSING={len(without)} OUT_OF_REACH={len(unreached)}")
    for name, path, count, _ in with_terminator:
        print(f"  ok       {name:<24} {path} ({count} values)")
    for name, path, count, waiver in waived:
        print(f"  waived   {name:<24} {path} ({count} values) — length is {waiver}")
    for name, path, count, _ in without:
        print(f"  MISSING  {name:<24} {path} ({count} values)")
    for name in unreached:
        print(f"  -        {name:<24} {enums[name][0]} — not reachable from the boundary")
    return 1 if without else 0


if __name__ == "__main__":
    sys.exit(main())
