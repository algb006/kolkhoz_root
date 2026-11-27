#!/bin/bash
# core: population_curve (the canon: 16 villages a seed offset) on one tree on a run node, with
# table values set in the NODE'S COPY of the tables only (an arm that is a table value on the same
# commit). Writes /job/out/population_curve-oN.txt per offset and prints each offset's rc and CPU.
# usage (submitted): XEON_HOST=<node> bash tools/xeon.sh submit --role core --name NAME --threads 12
#   --mem 12 --in core/tools/run/core-xeon-e1wp.sh -- env JOB_THREADS=12 SEED_OFFSETS=0,100,200,300
#   [knobs] bash in/core-xeon-e1wp.sh NAME
# Knobs (env): E1_EXTRA / E1_EXTRA2 - population_curve flags (--years=N, --avatar=KEY,
#   --catch-arm=X); WP_ROWS="key=value,key=value" (world_params; no spaces, no ';' - a ';' split the
#   submit's command line, 19 Nov); WP_KEY/WP_VALUE, WP_KEY2/3 (single rows); CAMP_KEY/CAMP_VALUE
#   (campaign.csv - world_params keys do NOT go here: it refuses an absent row, 26 Nov);
#   AV_START_AUTHORITY=1 (avatars.csv start_authority, #2164); AV_ZERO=avatar:column; GPROF=1.
# TRAPS: one /src/NAME-* only (a reused name builds an older tree); a run of fewer years than 16
# fails the long-horizon checks by design (rc 4/5 = those checks, read the prints, not the rc).
set -u
if [ "$(ls -d /src/"$1"-* 2>/dev/null | wc -l)" -ne 1 ]; then
  echo "NOT MEASURED: not exactly one /src/$1-* on the server"
  exit 1
fi
SRC=$(ls -d /src/"$1"-*)
export KOLKHOZ_TREE="${SRC##*/}"  # SCALE2118's ground: the source's name-commit
export WP_ROWS="${WP_ROWS:-}"
echo "source: $SRC"
cp -a "$SRC" /job/core
cd /job/core || exit 2
if [ -n "${WP_KEY:-}" ]; then
  if grep -q "^${WP_KEY}," tables/world_params.csv; then
    sed -i "s/^${WP_KEY},[^,]*,/${WP_KEY},${WP_VALUE},/" tables/world_params.csv
  else
    printf '%s,%s,core\n' "$WP_KEY" "$WP_VALUE" >> tables/world_params.csv
  fi
  echo "world_params in the copy: $(grep "^${WP_KEY}," tables/world_params.csv)"
fi
if [ -n "${WP_ROWS:-}" ]; then
  IFS=',' read -ra PAIRS <<< "$WP_ROWS"
  for pair in "${PAIRS[@]}"; do
    k="${pair%%=*}"
    v="${pair#*=}"
    if grep -q "^${k}," tables/world_params.csv; then
      sed -i "s/^${k},[^,]*,/${k},${v},/" tables/world_params.csv
    else
      printf '%s,%s,core\n' "$k" "$v" >> tables/world_params.csv
    fi
    echo "world_params in the copy: $(grep "^${k}," tables/world_params.csv)"
  done
fi
if [ -n "${WP_KEY3:-}" ]; then
  if grep -q "^${WP_KEY3}," tables/world_params.csv; then
    sed -i "s/^${WP_KEY3},[^,]*,/${WP_KEY3},${WP_VALUE3},/" tables/world_params.csv
  else
    printf '%s,%s,core\n' "$WP_KEY3" "$WP_VALUE3" >> tables/world_params.csv
  fi
  echo "world_params in the copy: $(grep "^${WP_KEY3}," tables/world_params.csv)"
fi
if [ -n "${WP_KEY2:-}" ]; then
  if grep -q "^${WP_KEY2}," tables/world_params.csv; then
    sed -i "s/^${WP_KEY2},[^,]*,/${WP_KEY2},${WP_VALUE2},/" tables/world_params.csv
  else
    printf '%s,%s,core\n' "$WP_KEY2" "$WP_VALUE2" >> tables/world_params.csv
  fi
  echo "world_params in the copy: $(grep "^${WP_KEY2}," tables/world_params.csv)"
fi
if [ "${AV_START_AUTHORITY:-0}" = 1 ] && ! grep -q "start_authority" tables/avatars.csv; then
  awk -F, -v OFS=, '
    /^#/ { print; next }
    !head { print $0, "start_authority"; head = 1; next }
    { a["villager"] = 65; a["worker"] = 50; a["acting"] = 50; a["ex_chairman"] = 45;
      a["dealer"] = 40; a["promoted"] = 25; a["old_fighter"] = 25; a["student"] = 15;
      print $0, ($1 in a ? a[$1] : 50) }' tables/avatars.csv > /job/avatars-auth.tmp
  cp /job/avatars-auth.tmp tables/avatars.csv
  echo "avatars in the copy: start_authority added"
fi
if [ -n "${CAMP_KEY:-}" ]; then
  if grep -q "^${CAMP_KEY}," tables/campaign.csv; then
    sed -i "s/^${CAMP_KEY},[^,]*/${CAMP_KEY},${CAMP_VALUE}/" tables/campaign.csv
    echo "campaign in the copy: $(grep "^${CAMP_KEY}," tables/campaign.csv)"
  else
    echo "NOT MEASURED: campaign.csv has no row ${CAMP_KEY}"
    exit 1
  fi
fi
if [ -n "${AV_ZERO:-}" ]; then
  av="${AV_ZERO%%:*}"
  col="${AV_ZERO#*:}"
  awk -F, -v OFS=, -v av="$av" -v col="$col" '
    /^#/ { print; next }
    !head { for (i = 1; i <= NF; i++) if ($i == col) c = i; head = 1; print; next }
    $1 == av && c { $c = 0 } { print }' tables/avatars.csv > /job/avatars.tmp
  cp /job/avatars.tmp tables/avatars.csv
  echo "avatars in the copy: $(grep "^${av}," tables/avatars.csv | head -1)"
fi
PG_FLAGS=()
if [ "${GPROF:-0}" = 1 ]; then
  PG_FLAGS=(-DCMAKE_CXX_FLAGS=-pg -DCMAKE_C_FLAGS=-pg -DCMAKE_EXE_LINKER_FLAGS=-pg)
fi
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ -DKOLKHOZ_SANITIZERS="" "${PG_FLAGS[@]}" > /job/out/configure.txt 2>&1
cmake --build build -j "${JOB_THREADS:-8}" --target population_curve > /job/out/build.txt 2>&1
EXE=$(find build -type f -name population_curve | head -1)
if [ -z "$EXE" ]; then
  echo "NOT MEASURED: population_curve did not build"
  tail -5 /job/out/build.txt
  exit 1
fi
OFFSETS=${SEED_OFFSETS:-0}
for offset in ${OFFSETS//,/ }; do
  # CPU seconds, not only the wall: a shared node's wall time is noisier than a cut (24 Nov).
  TIMEFORMAT="cpu_user %U cpu_sys %S wall %R"
  { time ./"$EXE" --seed-offset="$offset" --walkers="${JOB_THREADS:-8}" ${E1_EXTRA:-} ${E1_EXTRA2:-} > /job/out/population_curve-o"$offset".txt 2>&1 ; } 2> /job/out/time-o"$offset".txt
  echo "population_curve offset $offset rc=$? $(cat /job/out/time-o"$offset".txt)"
  grep -E "E1:|Epoch II opened|mean door" /job/out/population_curve-o"$offset".txt
  if [ "${GPROF:-0}" = 1 ]; then
    if [ -f gmon.out ]; then
      gprof -b -p "$EXE" gmon.out > /job/out/gprof-flat-o"$offset".txt 2>&1
      gprof -b -q "$EXE" gmon.out > /job/out/gprof-graph-o"$offset".txt 2>&1
      mv gmon.out /job/out/gmon-o"$offset".out
      echo "GPROF offset $offset: sampled $(awk 'NR>5 && $1+0>0 {s+=$2} END {print s}' /job/out/gprof-flat-o"$offset".txt) s of $(cat /job/out/time-o"$offset".txt)"
    else
      echo "GPROF NOT MEASURED offset $offset: no gmon.out written"
    fi
  fi
done
