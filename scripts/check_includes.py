#!/usr/bin/env python3
"""Checks include discipline: `#include "core_x/..."` only along declared edges.

The include path is one for everyone (include/), so the build cannot catch a
foreign #include — this script does (manual/54-modules.md, section 5).

An edge is legal when the including module links the included module in its
CMakeLists (PUBLIC or PRIVATE), directly or through the transitive closure of
PUBLIC links — exactly CMake's usage-requirement rule. A module always sees
itself. tests/ links the whole `core` archive and is not checked.

Exit: 0 clean, 1 violations found.
"""

import re
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
MODULE_RE = re.compile(r"kolkhoz_add_module\(\s*(core_\w+)")
LINK_RE = re.compile(r"target_link_libraries\(\s*(core_\w+)(.*?)\)", re.S)
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"(core_\w+)/', re.M)
SOURCE_SUFFIXES = {".h", ".hpp", ".inl", ".cpp", ".cc"}


def declared_links():
    """module -> (public deps, private deps), from subprojects CMakeLists."""
    links = {}
    for cmake in sorted(PROJECT.glob("subprojects/*/CMakeLists.txt")):
        text = cmake.read_text(encoding="utf-8")
        declared = MODULE_RE.search(text)
        if not declared:
            continue
        module = declared.group(1)
        public, private = set(), set()
        for target, body in LINK_RE.findall(text):
            if target != module:
                continue
            bucket, mode = public, "PUBLIC"  # plain links behave as PUBLIC
            for token in body.split():
                if token in ("PUBLIC", "PRIVATE", "INTERFACE"):
                    mode = token
                    continue
                if token.startswith("core_"):
                    (public if mode != "PRIVATE" else private).add(token)
        links[module] = (public, private)
    return links


def allowed_includes(module, links):
    """Self, direct links, and the PUBLIC closure of every direct link."""
    public, private = links.get(module, (set(), set()))
    allowed = {module}
    frontier = list(public | private)
    while frontier:
        dep = frontier.pop()
        if dep in allowed:
            continue
        allowed.add(dep)
        frontier.extend(links.get(dep, (set(), set()))[0])
    return allowed


def module_sources(module):
    for root in (PROJECT / "include" / module, PROJECT / "subprojects" / module):
        if root.is_dir():
            for path in sorted(root.rglob("*")):
                if path.suffix in SOURCE_SUFFIXES:
                    yield path


def main():
    links = declared_links()
    violations = 0
    for module in sorted(links):
        allowed = allowed_includes(module, links)
        for source in module_sources(module):
            for included in INCLUDE_RE.findall(source.read_text(encoding="utf-8")):
                if included not in allowed:
                    print(
                        f"{source.relative_to(PROJECT)}: включает {included}/, "
                        f"но {module} это ребро не объявляет"
                    )
                    violations += 1
    if violations:
        print(f"\nНарушений дисциплины включений: {violations}")
        return 1
    print(f"Дисциплина включений соблюдена ({len(links)} модулей).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
