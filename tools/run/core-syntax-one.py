#!/usr/bin/env python3
"""core: -fsyntax-only on FILE with the flags of a sibling entry in build/compile_commands.json -
the VM's only compile check (um01 builds nothing; full builds and suites go to a run node).
usage: core-syntax-one.py SIBLING_SUFFIX FILE
  SIBLING_SUFFIX: a file key that exists in compile_commands (e.g. population_curve/main.cpp,
  core_residents/food_theft.cpp); a NEW .cpp borrows a sibling's key with its own absolute path.
TRAPS: «NO SIBLING ENTRY» is not a pass; check every new run print here before a node submit
(-Werror -Wdouble-promotion killed node builds three times)."""
import json
import shlex
import subprocess
import sys

sibling, target = sys.argv[1], sys.argv[2]
entries = json.load(open("/home/alex/kolkhoz/core/build/compile_commands.json"))
found = [e for e in entries if e["file"].endswith(sibling)]
if not found:
    print("NO SIBLING ENTRY", sibling)
    sys.exit(2)
entry = found[0]
args = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
out = []
skip = False
for arg in args:
    if skip:
        skip = False
        continue
    if arg in ("-o", "-c", "-MF", "-MT", "-MQ"):
        skip = True
        continue
    if arg in ("-MD", "-MMD") or arg == entry["file"]:
        continue
    out.append(arg)
out += ["-fsyntax-only", target]
result = subprocess.run(out, cwd=entry["directory"])
print("SYNTAX_OK" if result.returncode == 0 else f"SYNTAX_FAILED rc={result.returncode}")
sys.exit(result.returncode)
