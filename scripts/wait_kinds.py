#!/usr/bin/env python3
"""Does every wait kind have its rules and a fault test?

Architecture §7ж³ (the human's rule of 2 October 2026: no game agent's state
machine hangs): «сторож набора краснеет, если у вида чего-то из трёх нет или
нет проверки с порчей». The three — an emergency action, whom to nudge and a
journal line — are pure virtual in IWaitKindRules (core_world/watchdog.h): a
kind's class that leaves one out does not compile, and CreateWatchdog refuses
a table with a kind missing. What the build cannot see is a kind with NO
class at all until the world is assembled, and a kind no test ever makes
hang. That is what this checks, kind by kind, the list taken from the ENUM
(core_common/wait_state.h) and not from the code — as scripts/event_sites.py
does for the events.

A kind's rules: a line `WaitKind Kind() const override { return
WaitKind::kX; }` somewhere in subprojects/. Its fault test: a file under
tests/unit/ that names `WaitKind::kX` and drives the dog (`WalkHour(`).
Exits 1 with the kinds that lack either; prints the count of each.
"""
import os
import re
import sys

CORE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(CORE, "include", "core_common", "wait_state.h")


def kinds():
    text = open(HEADER, encoding="utf-8").read()
    body = text[text.index("enum class WaitKind"):]
    body = body[body.index("{") + 1:body.index("kWaitKindCount")]
    body = re.sub(r"//[^\n]*", "", body)
    return re.findall(r"\b(k[A-Z]\w*)\b", body)


def files(root, suffixes):
    for folder, _, names in os.walk(root):
        for name in names:
            if name.endswith(suffixes):
                path = os.path.join(folder, name)
                yield path, open(path, encoding="utf-8").read()


def main():
    listed = kinds()
    sources = list(files(os.path.join(CORE, "subprojects"), (".cpp", ".h")))
    tests = list(files(os.path.join(CORE, "tests", "unit"), (".cpp", ".h")))
    missing = []
    for kind in listed:
        rules = re.compile(r"Kind\(\)\s*const\s*override\s*\{\s*return\s+WaitKind::" + kind + r"\s*;")
        has_rules = any(rules.search(text) for _, text in sources)
        has_fault = any(("WaitKind::" + kind) in text and "WalkHour(" in text for _, text in tests)
        print(f"{kind}: rules {'yes' if has_rules else 'NO'}, fault test {'yes' if has_fault else 'NO'}")
        if not (has_rules and has_fault):
            missing.append(kind)
    print(f"WAIT_KINDS={len(listed)} WITH_RULES_AND_FAULT={len(listed) - len(missing)} "
          f"MISSING={len(missing)}")
    if not listed:
        print("NOT MEASURED: no wait kind read from the header")
        return 1
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
