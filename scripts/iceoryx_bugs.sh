#!/usr/bin/env bash
# Reproduces two Cyclone DDS + iceoryx behaviours (2 kHz Float64), each with and without iceoryx:
#  1) QoS: a RELIABLE subscriber receives from a BEST_EFFORT publisher over iceoryx
#  2) duplicates: rclcpp <-> rclpy endpoints receive every sample twice over iceoryx
source "$(dirname "$0")/common.sh"
PY=$HERE/../python
for mode in cyclone-iceoryx cyclone; do
  use_config $mode
  echo "== $mode"
  BREL=1 BDEPTH=100 $B fsub 7 > /tmp/rmw_bench_q.$$ 2>&1 & s=$!; sleep 0.3; $B fpub 2000 4 timer >/dev/null 2>&1; wait $s
  echo "  rclcpp BEST_EFFORT pub -> rclcpp RELIABLE sub : $(grep -oE 'uniq=[0-9]+ dup=[0-9]+' /tmp/rmw_bench_q.$$) (expected 0: incompatible QoS)"
  $REPRO pub 1 >/dev/null 2>&1 & p=$!; sleep 1.5
  echo "  rclcpp pub -> rclpy sub                        : $(python3 $PY/py_sub_count.py best_effort 100 2>/dev/null)"
  kill $p; wait $p 2>/dev/null
  BDEPTH=100 $B fsub 7 > /tmp/rmw_bench_q.$$ 2>&1 & s=$!; sleep 0.3; python3 $PY/py_pub.py fbench >/dev/null 2>&1; wait $s
  echo "  rclpy pub -> rclcpp sub                        : $(grep -oE 'uniq=[0-9]+ dup=[0-9]+' /tmp/rmw_bench_q.$$)"
  python3 $PY/py_pub.py repro >/dev/null 2>&1 & p=$!; sleep 1.0
  echo "  rclpy pub -> rclpy sub                         : $(python3 $PY/py_sub_count.py best_effort 100 2>/dev/null)"
  wait $p 2>/dev/null
  done_config $mode
done; rm -f /tmp/rmw_bench_q.$$
