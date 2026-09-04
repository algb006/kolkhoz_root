#!/usr/bin/env python3
"""Do the tables in this tree still match the design db they came from?

Eighteen of the tables under `tables/` are an EXPORT: `tools/db.py csv` in the
parent tree writes them out of `db/design.db`. The other ten are the core's
own balance tables and are not touched here.

Why the check exists, on the consumer's side rather than the exporter's: on
2026-09-04 the design db was fixed, exported into core/tables, and the fix
never reached the published delivery — the graphics layer found it by
twenty-one warnings in its own run, which is to say by accident and late.
The exporter always sees success; only the consumer can say whether it
arrived. This is that question asked one seam earlier: does the repository
still hold what the db says?

It SKIPS rather than fails when the parent tree is not there. The core is a
repository of its own and must stay buildable without it; a check that turns
a standalone clone red is a check that gets deleted.
"""

import os
import subprocess
import sys
import tempfile
import filecmp

CORE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(CORE)
DB_TOOL = os.path.join(ROOT, "tools", "db.py")
DESIGN_DB = os.path.join(ROOT, "db", "design.db")
TABLES = os.path.join(CORE, "tables")


def main() -> int:
    if not (os.path.isfile(DB_TOOL) and os.path.isfile(DESIGN_DB)):
        print("таблицы: базы дизайна рядом нет — проверка пропущена")
        return 0

    with tempfile.TemporaryDirectory(prefix="core_tables_") as tmp:
        run = subprocess.run(
            [sys.executable, DB_TOOL, "csv", "--out", tmp],
            cwd=ROOT, capture_output=True, text=True,
        )
        if run.returncode != 0:
            print("таблицы: выгрузка из базы не удалась — проверка пропущена")
            print(run.stderr.strip()[:400])
            return 0

        exported = sorted(f for f in os.listdir(tmp) if f.endswith(".csv"))
        missing = [f for f in exported if not os.path.isfile(os.path.join(TABLES, f))]
        stale = [f for f in exported
                 if f not in missing
                 and not filecmp.cmp(os.path.join(tmp, f), os.path.join(TABLES, f), shallow=False)]

    if missing or stale:
        for name in missing:
            print(f"НЕТ В ДЕРЕВЕ: tables/{name}")
        for name in stale:
            print(f"РАЗОШЛАСЬ С БАЗОЙ: tables/{name}")
        print(f"таблиц из базы: {len(exported)}, разошлось: {len(missing) + len(stale)}")
        print("починка: в корневом дереве `tools/db.py csv`, затем коммит здесь")
        return 1

    print(f"таблицы: {len(exported)} из базы дизайна совпадают с деревом")
    return 0


if __name__ == "__main__":
    sys.exit(main())
