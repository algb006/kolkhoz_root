#!/bin/bash
# core: wait for the jobs named in the arguments on the node $1 (xeon, xeon16, xeon17, xeon18) to
# leave the queue, then fetch each to /data/tmp/core-fetch-NAME/NAME (core-fetch-jobs.sh).
# usage: bash core/tools/run/core-wait-host.sh <node> NAME [NAME...]   (run it in the background)
# TRAP it does NOT guard: it gives up after 240 one-minute polls and fetches whatever is there,
# finished or not (17 Nov) - read the fetched `status` before the numbers.
cd /home/alex/kolkhoz || exit 1
node=$1
shift
for i in $(seq 1 240); do
  left=0
  for n in "$@"; do
    line=$(XEON_HOST=$node bash tools/xeon.sh status 2>&1 | grep "core-$n\b")
    case "$line" in
      *done*|*failed*|*cancelled*) ;;
      *) left=1 ;;
    esac
  done
  [ $left -eq 0 ] && break
  sleep 60
done
for n in "$@"; do
  XEON_HOST=$node bash /home/alex/kolkhoz/core/tools/run/core-fetch-jobs.sh "$n" "$n" 2>&1 | tail -1
done
