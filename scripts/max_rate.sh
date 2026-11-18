#!/usr/bin/env bash
# Max sustainable rate sweep, std_msgs/Float64, two processes, subscriber depth BDEPTH (default 100).
# Rate 0 = publish flat out (no sleep).
# usage: scripts/max_rate.sh "<rates>" [configs...]     e.g. scripts/max_rate.sh "5000 20000 50000 100000 0" cyclone
source "$(dirname "$0")/common.sh"
RATES=${1:-"5000 20000 50000 100000 0"}; shift || true; CONFIGS=${*:-$ALL_CONFIGS}
export BDEPTH=${BDEPTH:-100}
for c in $CONFIGS; do
  use_config $c
  for hz in $RATES; do
    m=sleep; r=$hz; [ $hz = 0 ] && m=max && r=1
    $B fsub 7.5 > /tmp/rmw_bench_sub.$$ 2>&1 & sp=$!; sleep 0.3
    p=$($B fpub $r 4 $m 2>&1 | grep achieved); wait $sp
    sent=$(sed -E 's/.*sent=([0-9]+).*/\1/' <<< "$p"); line=$(grep -E '^uniq=' /tmp/rmw_bench_sub.$$)
    n=$(sed -E 's/.*uniq=([0-9]+).*/\1/' <<< "$line")
    printf '%-16s %-6s %-8s %s | received %s%% | %s\n' $c $m $hz "$(sed 's/pub //' <<< "$p")" \
      "$(awk -v a=${n:-0} -v b=${sent:-1} 'BEGIN{printf "%.1f",100*a/b}')" "$(sed -E 's/ \| gap.*//; s/.*(uniq=)/\1/' <<< "$line")"
  done
  done_config $c
done; rm -f /tmp/rmw_bench_sub.$$
