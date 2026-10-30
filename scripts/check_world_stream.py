#!/usr/bin/env python3
"""Does anything in the run draw off the world's one sequential generator?

WorldState::rng is GENESIS-ONLY since the stream split (#1310, #1273, #1303;
boss, core-boss-1310-stream-split [2]). Until 0.37.341 every death, birth,
wedding, night trade, herd death and three private forks (the district's
delivery delay, fire, the night pasture's camp) drew off it in row order, so
one draw more anywhere earlier in the day - the herd plan's, #1289 - re-dealt
WHO died and WHO bore from then on, and every A/B pair differed by that coin.
Every in-run draw is now a counter hash of (seed, day, entity, site salt).

This check makes the next site that slips back onto the stream red: any
`.rng` member access (`current.rng`, `world.rng`, `x.rng.state`) in the core's
sources outside the files that own the generator - genesis (it deals the
start before day 0) and the save codec (it stores the bits).

It strips `//` comments before matching, and says how many lines it stripped
a match from, so a site hidden behind a comment marker is counted, not lost.
Every allowed file's hits are printed too: a check that prints only on a
mismatch teaches that silence means agreement.

Usage: python3 scripts/check_world_stream.py
Exit: 0 clean, 1 an in-run site draws off the stream, 2 not measured.
"""

import os
import re
import sys

CORE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOTS = ("subprojects", "include")
# The generator's owners: the start is dealt off it, and the save stores it.
ALLOWED = {
    os.path.join("subprojects", "core_world", "genesis.cpp"),
    os.path.join("subprojects", "core_save", "save_blocks.cpp"),
}
MEMBER = re.compile(r"\.rng\b")


def main():
    scanned = 0
    found = []
    allowed_hits = {path: 0 for path in ALLOWED}
    stripped = 0
    for root in ROOTS:
        for directory, _, files in os.walk(os.path.join(CORE, root)):
            for name in files:
                if not name.endswith((".cpp", ".h")):
                    continue
                path = os.path.join(directory, name)
                relative = os.path.relpath(path, CORE)
                scanned += 1
                with open(path, encoding="utf-8") as handle:
                    for number, line in enumerate(handle, start=1):
                        code, marker, comment = line.partition("//")
                        if marker and MEMBER.search(comment):
                            stripped += 1
                        if not MEMBER.search(code):
                            continue
                        if relative in ALLOWED:
                            allowed_hits[relative] += 1
                        else:
                            found.append(f"{relative}:{number}: {line.strip()}")
    if scanned == 0:
        print("check_world_stream: NOT MEASURED - no source file found under", ROOTS)
        return 2
    for site in found:
        print("IN-RUN DRAW OFF THE WORLD'S STREAM:", site)
    for path in sorted(allowed_hits):
        print(f"check_world_stream: allowed {path}: {allowed_hits[path]} line(s)")
    print(
        f"check_world_stream: {scanned} files scanned, {len(found)} in-run site(s) on the stream, "
        f"{stripped} comment line(s) naming .rng stripped"
    )
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
