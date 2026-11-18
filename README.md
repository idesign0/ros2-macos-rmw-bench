# ros2-macos-rmw-bench

Inter-process topic rate and latency benchmark for ROS 2 middlewares (RMWs), built to answer one question: **how fast can two ROS 2 nodes talk on macOS, and with which settings?**

It covers Fast DDS, Cyclone DDS (with and without iceoryx shared memory), Zenoh (with and without SHM and batching) and same-process composition. It also reproduces:
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

The package defaults to a Release (`-O3`) build. That matters: with colcon's default (no build type), header-only rclcpp and message code runs unoptimized, and large-message throughput dropped ~3.5× in my tests (512 KB: ~570 Hz instead of ~1980 Hz).

## Experiments

| Script | What it measures |
|---|---|
| `scripts/sensor_2khz.sh [depth] [configs…]` | A 2 kHz `std_msgs/Float64` stream (like a laser height sensor) between two processes: received rate and latency every 2 s |
| `scripts/max_rate.sh "<rates>" [configs…]` | Sustainable rate sweep, `Float64`, subscriber depth 100. `0` = publish flat out. Reports unique/duplicate counts, loss and latency |
| `scripts/tune_2khz.sh <config>` | Default vs `EventsExecutor` at 2 kHz (add `EXTRA="VAR=val"` to test other settings) |
| `scripts/iceoryx_bugs.sh` | Cyclone + iceoryx: incompatible-QoS delivery and rclcpp ↔ rclpy duplicates, each case with and without iceoryx |
| `scripts/full_sweep.sh` | Everything in RESULTS.md: 2 kHz, rate sweep, large messages, same process |

**Configs:** `fastdds`, `fastdds-udp`, `fastdds-shm`, `cyclone`, `cyclone-iceoryx` (starts `iox-roudi`), `zenoh`, `zenoh-nobatch`, `zenoh-shm` (the Zenoh ones start `rmw_zenohd`).

**Direct use of the binaries:** `bench fsub` / `bench fpub`, `bench pub` / `bench sub` (variable size), `bench intra` (one process), and `repro sub|pub <depth>` (minimal 2 kHz reproducer). See the header of `rmw_rate_bench/src/bench.cpp` for every mode and environment variable.

## Presets

The best measured setting for each option. Two processes on an M2 Mac; full numbers are in RESULTS.md.

| Option | Settings | 2 kHz (p50) | Clean up to (small msgs) | Large messages |
|---|---|---|---|---|
| **Cyclone DDS** (simplest) | `export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp` | ~90 µs | 50 kHz | 1 MB @ 1964 Hz; 4 MB fails (16 Hz) |
| **Zenoh, tuned** | router `ros2 run rmw_zenoh_cpp rmw_zenohd` + `source config/zenoh_nobatch.env` | ~150 µs | ~90 kHz (publisher-bound, p99 ~100 µs) | — |
| **Zenoh + SHM** (large data) | as above + `ZENOH_CONFIG_OVERRIDE='transport/shared_memory/enabled=true'` | — | same as Zenoh | **4 MB @ 1961 Hz, 296 µs** |
| **Cyclone + iceoryx** (fastest, C++ only) | see the guide below | ~35 µs | **200 kHz** (p99 27 µs) | 1 MB @ 1982 Hz, 100 µs; 4 MB needs a bigger RouDi pool |
| **Fast DDS** | `export FASTDDS_BUILTIN_TRANSPORTS=UDPv4`, **without** `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST` | ~85 µs | 20 kHz | 1 MB @ 1780 Hz; 4 MB fails |
| **Composition** | one `component_container`, `use_intra_process_comms: true` | — | 100 kHz (3 / 19 µs) | — |

Tuning results (what helped and what didn't) are in RESULTS.md §6.

**For every option:**
- Don't use `KEEP_LAST(1)` if every sample matters. Bursty delivery (Fast DDS SHM, USB-serial sensors) then drops most of them; use a deeper queue (e.g. 50).
- Background load (Spotlight indexing, builds) raises p99 at high rates on every transport.

## Cyclone DDS + iceoryx: setup guide

Both the Jazzy and Kilted kegs of the tap ship Cyclone DDS built with iceoryx, plus `iox-roudi`, `iox-introspection-client` and `etc/roudi_config_example.toml`.

**1. Start RouDi** (one per machine, before or together with your nodes):

```bash
iox-roudi                                        # default memory pools (chunks up to 4 MB)
iox-roudi -c config/roudi_config_large.toml      # adds 8 MB chunks, needed for messages >= ~4 MB
```

**2. Enable shared memory in Cyclone:**

```bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export CYCLONEDDS_URI=file://$PWD/config/cyclonedds_iceoryx.xml
```

If you already use a Cyclone XML (e.g. for `MaxParticipants`), add the `<SharedMemory>` block to it instead.

**3. Launch file:** `config/launch_iceoryx_example.launch.py` starts `iox-roudi` and the nodes together. Nodes that start before RouDi is ready wait for it.

**4. Verify it's used:**
- Latency is the clearest sign: ~8–10 µs (`bench`) or ~35 µs (`repro`), against ~70–90 µs over UDP.
- `iox-introspection-client --port` lists the iceoryx ports per process.

**Rules** (from [rmw_cyclonedds `shared_memory_support.md`](https://github.com/ros2/rmw_cyclonedds/blob/kilted/shared_memory_support.md), plus what I measured):
- **Types:** zero-copy only for **fixed-size** types: no `string`, no sequences, so nothing with a `Header`. Other types are serialized into shared memory, which gains less.
- **QoS:** KEEP_LAST history; reliable and best effort both work. The doc says publisher depth ≤ 16, but on this build depths 17 and 100 still used shared memory. Keep ≤ 16 to be safe.
- **Limits:** one RouDi per machine; at most 127 subscriptions per process. Only same-machine subscribers use shared memory; remote ones use the network.
- **Memory pool:** if no chunk fits, `publish()` **blocks forever**. A 4 MB message with the default pools hangs the publisher; use a config with bigger chunks.
- **No RouDi:** every node waits for it and aborts after ~60 s.
- **Known problems** (RESULTS.md §7):
  - rclcpp ↔ rclpy endpoints receive every message twice;
  - incompatible-QoS subscribers still receive data;
  - `transient_local` isn't supported over shared memory.

  Prefer it for C++-only pipelines.

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

## License

MIT
