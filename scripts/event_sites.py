#!/usr/bin/env python3
"""Does every event kind have a place that emits it?

On 2026-09-05 the host counted the call sites and found eleven of
twenty-nine kinds written and eighteen silent. The gap had no single cause
and needed none: every emitter was a local copy of the same seven lines, so
the mechanism was applied FROM MEMORY, and memory covers an arbitrary
subset. `demography.cpp` announced a vacated post on line 195 and created a
child on line 363 without a word.

So the list is taken from the ENUM and not from the code — walk the kinds
and ask which have a site, never the other way round — and it is a check in
the suite rather than a paragraph in a report. Eighteen holes did not appear
at once; they accumulated one at a time, and without a roll-call they will
accumulate again.

A kind may be silent ON PURPOSE. That is declared in the header, on the kind
itself, as `@no_emit <reason>` — the reason is the point: `// not emitted`
says only what is already visible from the absence.
"""

import os
import re
import sys

CORE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(CORE, "include", "core_common", "event_state.h")
SOURCE_DIRS = [os.path.join(CORE, "subprojects")]

# The one function that appends to the outbox (core_common/emit_event.h). A
# file that emits calls it; a file that only reads kinds does not, which is
# what separates an emitter from the boundary's mirror.
EMITTER_CALL = "EmitEvent("


def kinds_and_waivers():
    """Every EventKind value in declaration order, plus its @no_emit reason."""
    text = open(HEADER, encoding="utf-8").read()
    body = text[text.index("enum class EventKind"):]
    body = body[:body.index("kEventKindCount")]
    found = []
    pending_waiver = None
    for line in body.splitlines():
        waiver = re.search(r"@no_emit\s+(.+)", line)
        if waiver:
            pending_waiver = waiver.group(1).strip()
            continue
        name = re.match(r"\s*(k[A-Za-z0-9]+)\s*(=|,)", line)
        if name:
            if name.group(1) != "kNone":
                found.append((name.group(1), pending_waiver))
            pending_waiver = None
    return found


def emitting_files():
    """Files that call the emitter, mapped to the kinds they name."""
    sites = {}
    for root_dir in SOURCE_DIRS:
        for root, _dirs, files in os.walk(root_dir):
            for name in files:
                if not name.endswith((".cpp", ".h", ".inl")):
                    continue
                path = os.path.join(root, name)
                text = open(path, encoding="utf-8", errors="ignore").read()
                if EMITTER_CALL not in text:
                    continue
                for kind in set(re.findall(r"EventKind::(k[A-Za-z0-9]+)", text)):
                    if kind in ("kNone", "kEventKindCount"):
                        continue
                    sites.setdefault(kind, []).append(os.path.relpath(path, CORE))
    return sites


def main() -> int:
    kinds = kinds_and_waivers()
    sites = emitting_files()
    missing = []
    waived = []
    for kind, waiver in kinds:
        if kind in sites:
            if waiver:
                # A kind that is BOTH waived and emitted is a contradiction
                # worth failing on: one of the two is out of date, and which
                # one is a question for whoever reads this.
                print(f"КОНФЛИКТ: {kind} помечен @no_emit «{waiver}», но место вызова есть")
                missing.append(kind)
            continue
        if waiver:
            waived.append((kind, waiver))
        else:
            missing.append(kind)

    for kind, waiver in waived:
        print(f"  молчит по причине  {kind:22s} {waiver}")
    for kind in missing:
        print(f"  НЕТ МЕСТА ВЫЗОВА   {kind}")
    print(f"EVENT_KINDS={len(kinds)} WITH_SITE={len(sites)} "
          f"WAIVED={len(waived)} MISSING={len(missing)}")
    if missing:
        print("починка: место вызова там, где предмет случается, через EmitEvent;")
        print("         намеренное молчание — @no_emit с причиной в event_state.h")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
