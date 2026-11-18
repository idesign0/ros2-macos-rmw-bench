# Sourced by the other scripts. Requires a sourced ROS 2 environment and the built workspace.
set -u
command -v ros2 >/dev/null || { echo "Source ROS 2 first (setup.bash) and this workspace's install/setup.bash"; exit 1; }
PREFIX=$(ros2 pkg prefix rmw_rate_bench 2>/dev/null) || { echo "rmw_rate_bench not found: build it and source install/setup.bash"; exit 1; }
B=$PREFIX/lib/rmw_rate_bench/bench
REPRO=$PREFIX/lib/rmw_rate_bench/repro
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ICEORYX_URI='<CycloneDDS><Domain><SharedMemory><Enable>true</Enable></SharedMemory></Domain></CycloneDDS>'
export ROS_DOMAIN_ID=${ROS_DOMAIN_ID:-83}
start_roudi() { pkill -x iox-roudi 2>/dev/null; iox-roudi >/tmp/rmw_bench_roudi.log 2>&1 & sleep 2; }
stop_roudi()  { pkill -x iox-roudi 2>/dev/null; sleep 1; }
start_zenohd() { ros2 run rmw_zenoh_cpp rmw_zenohd >/tmp/rmw_bench_zenohd.log 2>&1 & ZPID=$!; sleep 2; }
stop_zenohd()  { pkill -f rmw_zenohd 2>/dev/null; sleep 1; }
# Apply a named config: sets RMW_IMPLEMENTATION and transport env; starts daemons as needed.
use_config() {
  unset FASTDDS_BUILTIN_TRANSPORTS CYCLONEDDS_URI ZENOH_CONFIG_OVERRIDE
  case "$1" in
    fastdds)          export RMW_IMPLEMENTATION=rmw_fastrtps_cpp ;;
    fastdds-udp)      export RMW_IMPLEMENTATION=rmw_fastrtps_cpp FASTDDS_BUILTIN_TRANSPORTS=UDPv4 ;;
    fastdds-shm)      export RMW_IMPLEMENTATION=rmw_fastrtps_cpp FASTDDS_BUILTIN_TRANSPORTS=SHM ;;
    cyclone)          export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ;;
    cyclone-iceoryx)  export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp CYCLONEDDS_URI="$ICEORYX_URI"; start_roudi ;;
    zenoh)            export RMW_IMPLEMENTATION=rmw_zenoh_cpp; start_zenohd ;;
    zenoh-nobatch)    export RMW_IMPLEMENTATION=rmw_zenoh_cpp ZENOH_CONFIG_OVERRIDE='transport/link/tx/queue/batching/enabled=false'; start_zenohd ;;
    zenoh-shm)        export RMW_IMPLEMENTATION=rmw_zenoh_cpp ZENOH_CONFIG_OVERRIDE='transport/shared_memory/enabled=true'; start_zenohd ;;
    *) echo "unknown config $1"; exit 1 ;;
  esac
}
done_config() { case "$1" in cyclone-iceoryx) stop_roudi ;; zenoh*) stop_zenohd ;; esac; }
ALL_CONFIGS="fastdds fastdds-udp cyclone cyclone-iceoryx zenoh zenoh-nobatch"
