#!/usr/bin/env python3
"""Do the district's plan positions in campaign.csv still match the start layout?

`plan_positions` in tables/campaign.csv is the share of worked arable the
district asks for each crop — and it is DERIVED: a crop's hectare-years in the
three-year rotation of the working fields over all their hectare-years (the
method is written in campaign.csv above the row). The layout lives in
start_layout.csv, which the design db exports; the shares live in a table the
core writes by hand. Two homes for one number.

Why the check exists: on 2026-09-15 an export re-laid the third rotation year
(69eeaa8) and nobody recomputed the shares. For three days the district asked
23.6 per cent potatoes of a rotation that grew 25.0, and 8.3 per cent oats of
one that grew 10.0 — and no instrument could say so, because each file was
right by itself. A record outliving its subject; this is the invariant that
binds them.

Every position is printed with both numbers, matching or not: a line that
appears only on a mismatch teaches the reader that silence means agreement.
"""

import csv
import os
import sys

CORE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYOUT = os.path.join(CORE, "tables", "start_layout.csv")
CAMPAIGN = os.path.join(CORE, "tables", "campaign.csv")
ROTATION_YEARS = ("rotation_year0", "rotation_year1", "rotation_year2")
# The file holds one decimal; half of its last digit is what rounding can move.
TOLERANCE_POINTS = 0.05


def data_rows(path):
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(line for line in handle if not line.startswith("#")))


def rotation_shares():
    """Percent of hectare-years per crop over the working fields."""
    crop_ha_years = {}
    total = 0.0
    fields = 0
    for row in data_rows(LAYOUT):
        if row["kind"] != "field" or row["is_derelict"] == "1":
            continue
        area = float(row["area_ha"])
        fields += 1
        for year in ROTATION_YEARS:
            total += area
            crop = row[year]
            if crop:
                crop_ha_years[crop] = crop_ha_years.get(crop, 0.0) + area
    if total <= 0.0:
        return None, fields, total
    return {crop: 100.0 * value / total for crop, value in crop_ha_years.items()}, fields, total


def file_positions():
    for row in data_rows(CAMPAIGN):
        if row["key"] == "plan_positions":
            pairs = (item.split("=") for item in row["value"].split())
            return {crop: float(share) for crop, share in pairs}
    return None


def main():
    shares, fields, total = rotation_shares()
    positions = file_positions()
    if shares is None or not positions:
        print(f"plan positions: NOTHING TO COMPARE — {fields} working fields, {total} ha-years, "
              f"positions {positions!r}")
        return 1
    print(f"plan positions against start_layout: {fields} working fields, {total:g} ha-years")
    failures = 0
    for crop, stated in sorted(positions.items()):
        derived = shares.get(crop, 0.0)
        ok = abs(derived - stated) <= TOLERANCE_POINTS
        failures += 0 if ok else 1
        print(f"  {crop:<12} campaign {stated:5.1f}  rotation {derived:6.2f}  "
              f"{'ok' if ok else 'DIFFERS'}")
    if failures:
        print(f"plan positions: {failures} of {len(positions)} differ from the rotation — "
              "recompute plan_positions in tables/campaign.csv (method above the row)")
        return 1
    print(f"plan positions: all {len(positions)} match the rotation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
