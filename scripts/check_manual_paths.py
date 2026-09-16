#!/usr/bin/env python3
"""Does the core manual still point at files that exist?

WHY, in one case. `55-project-layout.md` explained the naming rule by the path
`include/core_time/calendar.h`; the calendar moved to `core_common` on
2026-09-15 and the example went on being read for a day, pointing at a file
nobody could open. Nothing complained: the design checker asks about prose and
sections, the compiler never reads a document, and a path in backticks is
exactly as much a fact about the tree as a number in prose — and ages the same
way, silently (boss, 2026-09-16: "число в прозе стареет молча").

Three questions, all of them about THIS tree:
  * a markdown link to a file — does the file exist where the link says;
  * a path in backticks (`subprojects/...`, `tables/x.csv`) — is there such a
    file or directory today;
  * a bare header or source name in backticks (`field_work.h`) — is there a
    file of that name anywhere under core/;
  * an include line in backticks (`#include "core_time/calendar.h"`) — does a
    header end with that path. THE FIRST DRAFT OF THIS GUARD MISSED EXACTLY
    THAT: it skipped every token with a space in it, so the include spelling
    of the very path that prompted the guard went through green. A guard has
    to be shown the defect it was written for, in every spelling the document
    uses it.

WHAT IT DELIBERATELY LETS THROUGH, because these are not claims about what
exists:
  * a pattern with a placeholder or a glob: `include/<модуль>/`,
    `tables/*.csv`, `#include "core_x/..."` — a shape, not a file;
  * names in EXCUSED: files that live on another machine or are produced by a
    build, and one name that is a rule for the next module rather than a file.
    Each carries its reason here, so that the list cannot quietly grow.

Exit: 0 clean, 1 something points nowhere.
"""

import os
import re
import sys

CORE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANUAL = os.path.join(CORE, "manual")
PARENT = os.path.dirname(CORE)

LINK = re.compile(r"\]\(([^)\s]+)\)")
CODE = re.compile(r"`([^`\n]+)`")
PATHY = re.compile(r"^(subprojects|include|tests|tables|scripts|manual|build|claude|db|tools)/")
FILEISH = re.compile(r"^[A-Za-z0-9_.-]+\.(h|cpp|py|sh|bat|csv|txt|lua)$")
PLACEHOLDER = re.compile(r"[*<>]|\.\.\.")

EXCUSED = {
    # Written by the MSVC build on the Windows host into publish/, never here.
    "LAYOUT.txt": "produced on the Windows host (60-windows-host.md)",
    # The rule for the NEXT module, not a file that exists today: an empty
    # module carries one so MSVC does not warn about an empty archive member.
    "placeholder.cpp": "the convention for a module with no sources yet (54-modules.md)",
    # A CMake target of tools/, not a file path.
    "tools/core_layout": "a CMake target (tools/core_layout.cpp)",
    # Doxygen's output directory, which exists only after `make docs`.
    "build/doc": "made by `make docs`",
}


INCLUDE = re.compile(r'^#include\s*[<"]([^>"]+)[>"]$')


def tree_files():
    """Every file's name, and every file's path relative to core/."""
    names = set()
    paths = set()
    for dirpath, dirnames, filenames in os.walk(CORE):
        dirnames[:] = [d for d in dirnames if d not in {".git", "build", "artifacts"}]
        for filename in filenames:
            names.add(filename)
            paths.add(os.path.relpath(os.path.join(dirpath, filename), CORE))
    return names, paths


def included_file_exists(included, paths):
    """A header is named as the compiler sees it — by the tail of its path."""
    tail = "/" + included
    return any(path == included or path.endswith(tail) for path in paths)


def main():
    names, paths = tree_files()
    misses = []
    for dirpath, _, filenames in os.walk(MANUAL):
        for filename in sorted(filenames):
            if not filename.endswith(".md"):
                continue
            full = os.path.join(dirpath, filename)
            shown = os.path.relpath(full, CORE)
            with open(full, encoding="utf-8") as handle:
                lines = handle.readlines()
            for number, line in enumerate(lines, 1):
                for target in LINK.findall(line):
                    if target.startswith(("http://", "https://", "#", "mailto:")):
                        continue
                    path = target.split("#", 1)[0]
                    if not path or PLACEHOLDER.search(path):
                        continue
                    if not os.path.exists(os.path.normpath(os.path.join(dirpath, path))):
                        misses.append(f"{shown}:{number}: ссылка ведёт в никуда: {target}")
                for token in CODE.findall(line):
                    token = token.strip().rstrip(",.;:")
                    if PLACEHOLDER.search(token) or token in EXCUSED:
                        continue
                    included = INCLUDE.match(token)
                    if included is not None:
                        head = included.group(1)
                        # The standard library's headers are not this tree's.
                        if "/" in head and not included_file_exists(head, paths):
                            misses.append(f"{shown}:{number}: включения нет в дереве: {head}")
                        continue
                    if " " in token:
                        continue
                    if PATHY.match(token):
                        if os.path.exists(os.path.join(CORE, token)):
                            continue
                        if os.path.exists(os.path.join(PARENT, token)):
                            continue
                        misses.append(f"{shown}:{number}: пути нет в дереве: {token}")
                    elif FILEISH.match(token) and token not in names:
                        misses.append(f"{shown}:{number}: файла с таким именем нет: {token}")
    if misses:
        print(f"Мануал: {len(misses)} указаний в никуда")
        for miss in misses:
            print("  " + miss)
        return 1
    print(f"Мануал: ссылки и пути на месте (исключений с доводом: {len(EXCUSED)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
