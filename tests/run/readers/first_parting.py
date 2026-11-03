"""Where two worlds of one village first part: the first line naming `seed S` that differs between two runs.

Usage: first_parting.py A_FILE B_FILE SEED [N]   (population_curve-oX.txt files; prints the first N differing
pairs of lines, by line kind, in A's order). A line kind is its tag (the word after «population_curve: », or the
CSV head) - each kind is compared in its own order, so a kind printed more often in one world shows as the parting.
"""
import re
import sys
from collections import defaultdict


def lines_of(path, seed):
    pattern = re.compile(rf"(?:seed[ =]{seed}\b|seed={seed},)")
    kinds = defaultdict(list)
    order = []
    for line in open(path, encoding="utf-8"):
        if not pattern.search(line):
            continue
        body = line.rstrip("\n")
        tag = body.split("population_curve: ", 1)[-1].split(" ", 1)[0].split(",", 1)[0]
        kinds[tag].append(body)
        order.append((tag, len(kinds[tag]) - 1))
    return kinds, order


def main():
    seed = sys.argv[3]
    limit = int(sys.argv[4]) if len(sys.argv) > 4 else 6
    a, order = lines_of(sys.argv[1], seed)
    b, _ = lines_of(sys.argv[2], seed)
    shown = 0
    for tag, index in order:
        left = a[tag][index]
        right = b[tag][index] if index < len(b[tag]) else "(none)"
        if left != right:
            print(f"[{tag} #{index}]\n  A: {left[:230]}\n  B: {right[:230]}")
            shown += 1
            if shown >= limit:
                break
    if shown == 0:
        print(f"seed {seed}: no differing line among {len(order)} (kinds: {len(a)})")


if __name__ == "__main__":
    main()
