#!/bin/bash
# core: build one tree in full on a run node, run every UNIT test, then the named run tests.
# usage (submitted, not run here): XEON_HOST=<node> bash tools/xeon.sh submit --role core --name NAME
#   --threads 8 --mem 6 --in core/tools/run/core-xeon-units-plus.sh -- env JOB_THREADS=8 [RUNS=regex]
#   bash in/core-xeon-units-plus.sh NAME
# The source must be on the node first: `XEON_HOST=<node> bash tools/xeon.sh src NAME core <branch>`.
# TRAPS it guards: a source name is used ONCE per node - with two /src/NAME-* it refuses (it would
# build the lexically first, an older tree, 13 Oct 2026). The CHECK scripts are not here: read the
# ctest tally in ctest.txt and the build errors in build.txt.
set -u
if [ "$(ls -d /src/"$1"-* 2>/dev/null | wc -l)" -ne 1 ]; then
  echo "NOT MEASURED: not exactly one /src/$1-* on the server"
  exit 1
fi
SRC=$(ls -d /src/"$1"-*)
echo "source: $SRC"
cp -a "$SRC" /job/core
cd /job/core || exit 2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ -DKOLKHOZ_SANITIZERS="" > /job/out/configure.txt 2>&1
cmake --build build -j "${JOB_THREADS:-8}" -- -k 0 > /job/out/build.txt 2>&1 || {
  echo "NOT MEASURED: build"
  grep -E "error:" /job/out/build.txt | head -20
  exit 1
}
ctest --test-dir build -L unit --output-on-failure > /job/out/ctest.txt 2>&1
echo "ctest rc=$?"
grep -E "^FAIL|tests passed|Failed|\*\*\*" /job/out/ctest.txt | head -40
ctest --test-dir build -R "${RUNS:-save_continue|determinism}" -V > /job/out/runs.txt 2>&1
echo "runs rc=$?"
grep -E "^FAIL|tests passed|Failed|\*\*\*|save_continue:|determinism:" /job/out/runs.txt | head -40
