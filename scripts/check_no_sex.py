#!/usr/bin/env python3
"""The herds have no sex (#1374; livestock.md «Пола у скота нет»): no live code under subprojects/ or include/
names the sires' fields or helpers again. A comment may name them (the history of the change); code may not.

Exit: 0 clean, 1 a live use found (each printed with its file and line).
Usage: scripts/check_no_sex.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WORDS = re.compile(r"\b(adult_male_count|males_share|males_work|sex_choice|TargetMales|MalesAfterLoss|"
                   r"MalesAfterHandOver|herd_males_culled|yard_kind_sexed|yard_young_need_a_sire)\b")


def live_part(line):
    """The line without its // comment (a crude cut: no code here quotes //)."""
    cut = line.find("//")
    return line if cut < 0 else line[:cut]


def main():
    found = []
    for folder in ("subprojects", "include"):
        for path in sorted((ROOT / folder).rglob("*")):
            if path.suffix not in (".h", ".cpp"):
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
                if WORDS.search(live_part(line)):
                    found.append(f"{path.relative_to(ROOT)}:{number}: {line.strip()}")
    for line in found:
        print(f"LIVE SEX: {line}")
    print(f"check_no_sex: {len(found)} live use(s) of the sires' names under subprojects/ and include/")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
