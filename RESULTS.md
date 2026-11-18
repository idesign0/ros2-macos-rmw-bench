# Results

**Machine:** MacBook Pro 13" M2 (Mac14,7, 8 cores), macOS 27.0.1, Apple clang.

**ROS 2:**
- Kilted: Fast DDS 3.2.5, Cyclone DDS 0.10.5, iceoryx 2.0.8, rmw_zenoh. Used for all tables unless noted.
- Jazzy: Fast DDS 2.14.7.

Both from the Homebrew tap `idesign0/homebrew-ros2`. Measured on 2026-11-18. Ranges are over repeated runs.

**"Clean"** means at least 99.9 % of messages received and p99 latency under 1 ms.

## 2 kHz `Float64`, subscriber KEEP_LAST(1), best effort (`scripts/sensor_2khz.sh 1`)

| Config | Received of 2000 Hz | Latency p50 | Latency p99 |
|---|---|---|---|
| Fast DDS, default (SHM + UDP) | 351–407 Hz | 0.5–1.0 ms | 5.0–6.4 ms |
| Fast DDS, default, depth 100 | 1991–2000 Hz | 3.0 ms | 6.2–6.5 ms |
| Fast DDS, UDPv4 only | 1969–1996 Hz | 68–104 µs | 0.19–2.9 ms |
| Cyclone DDS | 1998–2000 Hz | 58–94 µs | 139–163 µs |
| Cyclone DDS + iceoryx | 1976–2000 Hz | 34–39 µs | 74–114 µs |
| Zenoh | 1997–2000 Hz | 95–102 µs | 181–207 µs |
| Same process, intra-process (64 B) | 1989 Hz | 14 µs | 40 µs |

## Maximum rate, two processes, `Float64`, subscriber depth 100 (`scripts/max_rate.sh`)

| Config | Clean up to | p50 / p99 there | Above |
|---|---|---|---|
| Cyclone + iceoryx | 150 kHz | 9 / 24–51 µs | 200 kHz: 99.8–99.9 %, p99 40–92 µs; 300 kHz: 33 % |
| Cyclone DDS | 50 kHz | 22 / 53–161 µs | 100 kHz: 100 %, p99 0.6–9.5 ms; 150 kHz: p99 22 ms |
| Zenoh | 20 kHz | 30 / 48 µs | 50 kHz: p99 2.1 ms; 100 kHz: 81 % |
| Zenoh + SHM | same as Zenoh | | Zenoh SHM only applies to messages > 512 B |
| Fast DDS, UDPv4 only | 5 kHz | 49 / 131 µs | 10–40 kHz: 100 %, p99 0.9–9.7 ms; 50 kHz: 98.4 %; 100 kHz: 47 % |
| Fast DDS, default (SHM) | never | 2.9 / 5.6 ms at every rate | 50 kHz: 78 %; flat out (~114 kHz): 38–104 Hz received (< 0.1 %), ~2 s latency |
| Same process, intra-process | 100 kHz | 7 / 19 µs (Fast DDS), 6 / 41 µs (Cyclone) | 200 kHz: 99.4–99.7 % |

**Duplicate check:** every C++ ↔ C++ run above was checked for duplicate samples (`dup=0` throughout).

## Large messages (`UInt8MultiArray`, two processes)

All RMWs top out around 290 MB/s on this machine. Cyclone, 2 kHz offered:

| Message size | Received |
|---|---|
| 256 KB | ~1.2 kHz |
| 512 KB | ~520–580 Hz |
| 1 MB | ~260–295 Hz |
| 2 MB | 77 Hz |

Zenoh and Fast DDS over UDP gave the same ceiling. Cyclone socket-buffer, `MaxMessageSize` and `FragmentSize` tuning changed it by about 10 % at most.

## Fast DDS SHM on macOS: root cause

- **Profile** (`sample`): the `dds.shm.*` thread sits in `RobustInterprocessCondition::do_timed_wait → try_based_timed_lock<spin_semaphore> → spin_wait::yield → nanosleep`.
- **Why:** the bundled Boost (`workaround.hpp`) disables process-shared POSIX primitives and unnamed semaphores on `__APPLE__`, so `interprocess_semaphore` becomes `spin_semaphore`. That sleeps `tick/2` = 5 ms: `_SC_CLK_TCK` = 100, and the sleep measures 6.04–6.11 ms mean.
- **Rate-independent:** latency p50 is 2.8–3.0 ms from 50 Hz to 5 kHz. Jazzy (2.14.7) and Kilted (3.2.5) behave the same.

**Causation check** (`tools/clamp_nanosleep.c`, KEEP_LAST 1, 2 kHz):

| Sleep per poll | Received | p50 / p99 | Idle listener CPU |
|---|---|---|---|
| 5 ms (stock) | 377 Hz | 0.93 / 5.56 ms | 5.6 % |
| 1 ms | 919 Hz | 0.28 / 1.09 ms | 6.3 % |
| 200 µs | 1992 Hz | 0.13 / 0.31 ms | 10.2 % |
| 50 µs | 1864 Hz | 0.06 / 1.16 ms | 19.0 % |

**Proposed primitive** (`tools/os_sync_wait_demo.c`): two processes, 2 kHz, wake latency 5.9–9.7 µs p50 / 16–26 µs p99, 1–1.5 % CPU (three runs).

**`ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`:** rmw_fastrtps adds SHM itself, so `FASTDDS_BUILTIN_TRANSPORTS=UDPv4` is ignored. Measured 316–332 Hz received with KEEP_LAST 1.

## Cyclone DDS + iceoryx caveats (`scripts/iceoryx_bugs.sh`)

| Case (2 kHz, 4 s) | With iceoryx | Without iceoryx |
|---|---|---|
| rclcpp BEST_EFFORT pub → rclcpp RELIABLE sub (incompatible QoS) | 7992–7998 received | 0 |
| rclcpp pub → rclpy sub | each sample ×2 | no duplicates |
| rclpy pub → rclcpp sub | each sample ×2 | no duplicates |
| rclpy pub → rclpy sub | no duplicates | no duplicates |

- **No RouDi:** if `iox-roudi` isn't running, every node waits and then aborts after ~60 s.
- **Other notes:**
  - `ros2 topic hz` shows 2× the real rate for C++ publishers.
  - `transient_local` isn't supported over SHM (ros2/rmw_cyclonedds#401).

## Other observations

- **Timers aren't the limit on macOS:** an `rclcpp` wall timer and `sleep_until` both reach 10 kHz in a single process.
- **Fast DDS asynchronous mode:** `RMW_FASTRTPS_PUBLICATION_MODE=ASYNCHRONOUS` with UDPv4 made `publish()` throw `cannot publish data` at 5 kHz (KEEP_LAST 1 publisher).
- **USB-serial (FTDI) sensors** arrive in ~16 ms batches on macOS. There is no equivalent of the Linux udev `latency_timer=1` rule. Combined with `KEEP_LAST(1)`, this drops samples on any RMW.
