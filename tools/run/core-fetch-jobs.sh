#!/bin/bash
# core: fetch the job named NAME (core-NAME, the LATEST such id on the node XEON_HOST) into
# /data/tmp/core-fetch-DIR/NAME, then clean it from the node (xeon.sh's rule: fetch, then clean).
# usage: XEON_HOST=<node> bash core/tools/run/core-fetch-jobs.sh NAME DIR
# TRAPS: a job name is unique per node only - run it with the node the job ran on (21 Oct: the
# default node fetched my cancelled namesakes). Two jobs of one name on a node (a cancelled
# duplicate): it takes the latest id - fetch the right one by its id with `xeon.sh fetch` (26 Nov).
set -u
cd /home/alex/kolkhoz || exit 1
name=$1
dir=$2
id=$(bash tools/xeon.sh status 2>&1 | grep -oE "[0-9]{4}-[0-9]{6}-core-$name\b" | tail -1)
if [ -z "$id" ]; then
  echo "FETCH: NOT FOUND - no core-$name on ${XEON_HOST:-xeon}"
  exit 1
fi
dest=/data/tmp/core-fetch-$dir/$name
bash tools/xeon.sh fetch "$id" "$dest" > /dev/null 2>&1 || { echo "FETCH FAILED: $id"; exit 1; }
bash tools/xeon.sh clean "$id" > /dev/null 2>&1 || echo "CLEAN FAILED: $id (fetched)"
echo "fetched $id -> $dest"
