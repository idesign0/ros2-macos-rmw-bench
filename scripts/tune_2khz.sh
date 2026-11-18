#!/usr/bin/env bash
# Compare executors (and optional extra env) at 2 kHz with the repro node, KEEP_LAST depth $DEPTH (default 10).
# usage: EXTRA="VAR=val ..." scripts/tune_2khz.sh <config> [label]
source "$(dirname "$0")/common.sh"
c=$1; label=${2:-$1}; DEPTH=${DEPTH:-10}
use_config $c
for ex in single events; do
  env ${EXTRA:-} EXECUTOR=$ex $REPRO sub $DEPTH 2>/dev/null > /tmp/rmw_bench_t.$$ & s=$!
  env ${EXTRA:-} EXECUTOR=$ex $REPRO pub $DEPTH >/dev/null 2>&1 & p=$!
  sleep 9; kill $s $p 2>/dev/null; wait $s $p 2>/dev/null
  printf '%-28s executor=%-7s depth=%-3s %s\n' "$label" $ex $DEPTH "$(tail -3 /tmp/rmw_bench_t.$$ | tr '\n' ';')"
done
done_config $c; rm -f /tmp/rmw_bench_t.$$
