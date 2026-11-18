# ros2-macos-rmw-bench

Inter-process topic rate and latency benchmark for ROS 2 middlewares (RMWs), built to answer one question: **how fast can two ROS 2 nodes talk on macOS, and with which settings?**

It covers Fast DDS, Cyclone DDS (with and without iceoryx shared memory), Zenoh and same-process composition. It also reproduces:
- a Fast DDS shared-memory latency problem specific to macOS;
- two Cyclone DDS + iceoryx delivery problems.

Measured results are in [RESULTS.md](RESULTS.md).

## Build

Needs ROS 2 Jazzy or Kilted (tested with the Homebrew tap [`idesign0/homebrew-ros2`](https://github.com/idesign0/homebrew-ros2)).

```bash
source /opt/homebrew/opt/ros2-kilted/setup.bash     # or your ROS 2 setup
colcon build --base-paths rmw_rate_bench --cmake-args -DBUILD_TESTING=OFF
source install/setup.bash
```

## Experiments

| Script | What it measures |
|---|---|
| `scripts/sensor_2khz.sh [depth] [configs…]` | A 2 kHz `std_msgs/Float64` stream (like a laser height sensor) between two processes: received rate and latency every 2 s |
| `scripts/max_rate.sh "<rates>" [configs…]` | Sustainable rate sweep, `Float64`, subscriber depth 100. `0` = publish flat out. Reports unique/duplicate counts, loss and latency |
| `scripts/iceoryx_bugs.sh` | Cyclone + iceoryx: incompatible-QoS delivery and rclcpp ↔ rclpy duplicates, each case with and without iceoryx |

**Configs:** `fastdds`, `fastdds-udp`, `fastdds-shm`, `cyclone`, `cyclone-iceoryx` (starts `iox-roudi`), `zenoh` (starts `rmw_zenohd`), `zenoh-shm`.

**Same process:** `bench intra sleep <hz> 16 3` runs publisher and subscriber in one process with intra-process comms. Set `BNOIPC=1` to turn intra-process comms off.

**Direct use of the binaries:** `bench fsub` / `bench fpub`, and `repro sub|pub <depth>` (minimal reproducer). See the header of `rmw_rate_bench/src/bench.cpp` for every mode and environment variable.

## Root-cause tools (macOS)

| File | Purpose |
|---|---|
| `tools/nanosleep_tick.c` | Prints `sysconf(_SC_CLK_TCK)`, the 5 ms sleep Boost.Interprocess requests, and what `nanosleep` actually takes |
| `tools/clamp_nanosleep.c` | `DYLD_INSERT_LIBRARIES` shim that shortens only that 5 ms sleep (`CLAMP_NS`), to prove causation and measure the CPU trade-off |
| `tools/os_sync_wait_demo.c` | Cross-process wake latency with `os_sync_wait_on_address` + `OS_SYNC_WAIT_ON_ADDRESS_SHARED` (macOS 14.4+): the proposed fix primitive |

```bash
cc -O2 -o tools/nanosleep_tick tools/nanosleep_tick.c && tools/nanosleep_tick
cc -O2 -dynamiclib -o tools/libclamp.dylib tools/clamp_nanosleep.c
cc -O2 -mmacosx-version-min=14.4 -o tools/os_sync_wait_demo tools/os_sync_wait_demo.c && tools/os_sync_wait_demo
```

**SIP gotcha:** `DYLD_INSERT_LIBRARIES` is stripped when it passes through `/bin/bash` or other system binaries. Set it on the benchmark binary itself:

```bash
DYLD_INSERT_LIBRARIES=$PWD/tools/libclamp.dylib CLAMP_NS=200000 repro sub 1
```

## Presets (summary)

| Goal | Settings |
|---|---|
| Recommended default | `export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp` |
| Stay on Fast DDS | `export FASTDDS_BUILTIN_TRANSPORTS=UDPv4`, and **don't** set `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`: rmw_fastrtps then re-adds SHM and ignores the variable |
| Maximum speed, two processes (C++ only) | `iox-roudi &` + `CYCLONEDDS_URI='<CycloneDDS><Domain><SharedMemory><Enable>true</Enable></SharedMemory></Domain></CycloneDDS>'`. See the caveats in RESULTS.md |
| Maximum speed, no daemon | Composition: one `component_container`, `use_intra_process_comms: true` |
| High-rate sensors | Don't use `KEEP_LAST(1)` if every sample matters; use a deeper queue (e.g. 50) |

## License

MIT
