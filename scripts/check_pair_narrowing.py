#!/usr/bin/env python3
"""MSVC's C4244 inside <utility> on Linux: a std::pair / std::tuple member narrowed in its constructor.

Kept to the pair and tuple headers (PAIR_HEADERS): a narrowing elsewhere in the library (std::from_chars)
builds clean under MSVC.

WHY (0.37.279-284; boss, 18 October 2026): `{0, grams}` into a std::pair<std::uint16_t, Grams> builds under
Clang without a word and fails the host's MSVC Debug build - C4244 «conversion from '_Ty' to '_Ty1'» in
<utility>, an error under /WX. Clang does warn, but INSIDE the standard header, where it silences itself;
-Wconversion alone never sees it. So this check compiles every translation unit of the compile database
syntax-only with ONE warning on (-Wimplicit-int-conversion) and system-header warnings let through, and keeps
only the narrowings in the standard library whose instantiation is requested from a line of THIS tree - the
library's own (<compare>'s int16_t) are not ours.

WHAT IT CANNOT SEE, printed every run: a narrowing MSVC warns about that Clang's -Wimplicit-int-conversion
does not (float to int is -Wfloat-conversion and not asked here); a translation unit outside the compile
database (configure the build first); MSVC's own library, whose constructors differ from libstdc++'s.

Usage: python3 scripts/check_pair_narrowing.py [BUILD_DIR] [-jJOBS] [--root TREE]
Exit: 0 clean, 1 narrowings found, 2 not measured (no compile database).
"""

import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
WARNING_RE = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):\d+: warning: (?P<text>.*)"
                        r"\[-Wimplicit-int-conversion\]")
# The standard headers whose converting constructors MSVC's C4244 names (<utility>'s pair, <tuple>).
PAIR_HEADERS = {"stl_pair.h", "tuple", "utility"}
REQUESTED_RE = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):\d+: note: in instantiation of (?P<what>.*) "
                          r"requested here")


def arguments(entry):
    """The entry's compile command, syntax-only, one warning, system headers let through."""
    args = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    kept = []
    skip = False
    for arg in args:
        if skip:
            skip = False
            continue
        if arg in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if arg in ("-c", "-MD", "-MMD") or arg.startswith("-W") or arg == "-Werror":
            continue
        kept.append(arg)
    # ccache in front of the compiler would cache a syntax-only run as nothing.
    if kept and os.path.basename(kept[0]) == "ccache":
        kept = kept[1:]
    return kept + ["-fsyntax-only", "-Wno-everything", "-Wimplicit-int-conversion", "-Wsystem-headers"]


def in_tree(path):
    try:
        Path(path).resolve().relative_to(PROJECT)
    except ValueError:
        return False
    return "/build/" not in str(Path(path).resolve())


def narrowings(entry):
    """(tree file, line, what) for every narrowing in a system header requested from this tree."""
    run = subprocess.run(arguments(entry), cwd=entry["directory"], capture_output=True, text=True,
                         check=False)
    found = []
    pending = None
    for line in run.stderr.splitlines():
        warning = WARNING_RE.match(line)
        if warning:
            # PAIR AND TUPLE ONLY: the first run also named std::from_chars (<charconv>), which narrows
            # inside libstdc++ and builds clean under MSVC (285, host) - a red MSVC never gives.
            header = Path(warning["file"]).name
            pending = warning["text"].strip() if header in PAIR_HEADERS and not in_tree(
                warning["file"]) else None
            continue
        requested = REQUESTED_RE.match(line)
        if requested and pending is not None and in_tree(requested["file"]):
            rel = Path(requested["file"]).resolve().relative_to(PROJECT)
            found.append((str(rel), int(requested["line"]), pending))
            pending = None
    return run.returncode, found


def main():
    global PROJECT
    argv = sys.argv[1:]
    # --root DIR: the tree whose units count as ours (default this script's); its fault is run on a copy.
    if "--root" in argv:
        at = argv.index("--root")
        PROJECT = Path(argv[at + 1]).resolve()
        argv = argv[:at] + argv[at + 2:]
    sys.argv = [sys.argv[0]] + argv
    args = [a for a in sys.argv[1:] if not a.startswith("-j")]
    # JOB_THREADS (env): the m22 suite's declared threads; the VM's default is 2.
    default_jobs = int(os.environ.get("JOB_THREADS", "2"))
    jobs = next((int(a[2:] or "2") for a in sys.argv[1:] if a.startswith("-j")), default_jobs)
    build = Path(args[0]) if args else PROJECT / "build"
    database = build / "compile_commands.json"
    print("check_pair_narrowing: CANNOT SEE a narrowing Clang's -Wimplicit-int-conversion does not name, "
          "a unit outside the compile database, MSVC's own library - only MSVC sees those")
    if not database.is_file():
        print(f"check_pair_narrowing: NOT MEASURED - no {database} (configure the build first)")
        return 2
    entries = [e for e in json.loads(database.read_text(encoding="utf-8")) if in_tree(e["file"])]
    if not entries:
        print("check_pair_narrowing: NOT MEASURED - the compile database names no unit of this tree")
        return 2
    sites = {}
    failed = 0
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for code, found in pool.map(narrowings, entries):
            failed += code != 0
            for site in found:
                sites[site[:2]] = site[2]
    print(f"check_pair_narrowing: {len(entries)} units compiled syntax-only, {failed} did not compile")
    if failed:
        print("check_pair_narrowing: NOT MEASURED in full - a unit that does not compile hides its sites")
    for (path, line), what in sorted(sites.items()):
        print(f"  {path}:{line}: a standard-library member narrowed ({what}) - MSVC's C4244 under /WX")
    print(f"check_pair_narrowing: {len(sites)} narrowing sites in this tree")
    return 1 if sites or failed else 0


if __name__ == "__main__":
    sys.exit(main())
