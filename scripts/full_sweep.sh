#!/usr/bin/env bash
# Runs every experiment used in RESULTS.md and prints one section per experiment.
# usage: scripts/full_sweep.sh > results/full_sweep_$(date +%F).txt
source "$(dirname "$0")/common.sh"
CFGS="fastdds fastdds-udp cyclone cyclone-iceoryx zenoh zenoh-nobatch"
echo "### build type: $(grep -h CXX_FLAGS "$HERE/../build/rmw_rate_bench/CMakeFiles/bench.dir/flags.make" 2>/dev/null)"
echo "### $(uname -a)"; echo "### load: $(uptime)"
echo "### 1. sensor_2khz depth=1"; "$HERE/sensor_2khz.sh" 1 $CFGS
echo "### 2. max_rate"; "$HERE/max_rate.sh" "5000 20000 50000 100000 150000 200000 300000 0" $CFGS
echo "### 3. large messages (UInt8MultiArray, 2 kHz offered, sub depth 10)"
for c in fastdds-udp cyclone cyclone-iceoryx zenoh zenoh-shm; do
  use_config $c
  for b in 65536 262144 1048576 4194304; do
    BDEPTH=10 $B sub 7 > /tmp/rmw_bench_l.$$ 2>&1 & s=$!; sleep 0.3; p=$($B pub timer 2000 $b 4 2>&1 | grep -oE 'sent=[0-9]+'); wait $s
    printf '%-16s %8s B | %s | %s\n' $c $b "$p" "$(grep -oE '^sub n=[0-9]+ rate=[0-9]+ Hz|latency us p50=[0-9]+ p99=[0-9]+' /tmp/rmw_bench_l.$$ | tr '\n' ' ')"
  done
  done_config $c
done; rm -f /tmp/rmw_bench_l.$$
echo "### 4. same process (16 B UInt8MultiArray), intra-process on / off"
for rmw in rmw_fastrtps_cpp rmw_cyclonedds_cpp; do
  for ipc in on off; do for hz in 100000 200000 500000; do
    o=; [ $ipc = off ] && o=BNOIPC=1
    printf '%-20s ipc=%-3s %7s Hz | %s\n' $rmw $ipc $hz "$(env $o RMW_IMPLEMENTATION=$rmw $B intra sleep $hz 16 3 2>&1 | grep -E 'achieved|intra-sub' | tr '\n' ' ' | sed -E 's/ \| gap.*//')"
  done; done
done
