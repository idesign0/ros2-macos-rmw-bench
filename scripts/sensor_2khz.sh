#!/usr/bin/env bash
# 2 kHz std_msgs/Float64 between two processes (the `repro` node: rclcpp::spin, wall timer, best effort).
# usage: scripts/sensor_2khz.sh [depth=1] [configs...]
source "$(dirname "$0")/common.sh"
DEPTH=${1:-1}; shift || true; CONFIGS=${*:-$ALL_CONFIGS}
for c in $CONFIGS; do
  use_config $c
  $REPRO sub $DEPTH 2>/dev/null > /tmp/rmw_bench_repro.$$ & s=$!; $REPRO pub $DEPTH >/dev/null 2>&1 & p=$!
  sleep 9; kill $s $p 2>/dev/null; wait $s $p 2>/dev/null
  printf '%-16s depth=%-4s %s\n' $c $DEPTH "$(tail -2 /tmp/rmw_bench_repro.$$ | tr '\n' ';')"
  done_config $c
done; rm -f /tmp/rmw_bench_repro.$$
