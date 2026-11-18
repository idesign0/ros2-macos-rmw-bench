# Results

**Machine:** MacBook Pro 13" M2 (Mac14,7, 8 cores), macOS 27.0.1, Apple clang.

**ROS 2:**
- Kilted: Fast DDS 3.2.5, Cyclone DDS 0.10.5, iceoryx 2.0.8, rmw_zenoh. Used for all tables unless noted.
- Jazzy: Fast DDS 2.14.7.

Both from the Homebrew tap `idesign0/homebrew-ros2`.

**Build:** Release (`-O3`). Measured on 2026-11-18. Raw output: `results/full_sweep_kilted_release_run1.txt`.

> An earlier version of this file used an unoptimized (`-O0`) build of the benchmark. Its large-message numbers (~290 MB/s ceiling) were an artifact of that build and have been replaced.

**"Clean"** means at least 99.9 % of messages received and p99 latency under 1 ms. Ranges are over repeated runs.

**Background load matters.** With load average ≥ 4 (e.g. Spotlight indexing), every transport showed occasional 0.1–0.4 s stalls. The tables use runs on a quieter machine (load ≈ 2.5–3.5).

## 1. 2 kHz `Float64`, subscriber KEEP_LAST(1), best effort (`scripts/sensor_2khz.sh 1`)

| Config | Received of 2000 Hz | Latency p50 | Latency p99 |
|---|---|---|---|
| Fast DDS, default (SHM + UDP) | **354–402 Hz** | 0.54–0.96 ms | 5.2–9.8 ms |
| Fast DDS, default, depth 100 (`bench fsub`) | 1998–2002 Hz | **2.86–2.92 ms** | 5.6–6.3 ms |
| Fast DDS, UDPv4 only | 1995 Hz | 82–87 µs | 172–204 µs |
| Cyclone DDS | 1998–2000 Hz | 85–97 µs | 151–163 µs |
| Cyclone DDS + iceoryx | 1998–2000 Hz | 33–36 µs | 69–71 µs |
| Zenoh | 1981–1994 Hz | 127–149 µs | 271–275 µs |
| Zenoh, batching off | 1988–2000 Hz | 150–151 µs | 190–191 µs |

`repro` uses `rclcpp::spin` with the default executor. The rclcpp EventsExecutor (`EXECUTOR=events`, `scripts/tune_2khz.sh`) was within 5–10 % on every RMW.

## 2. Maximum rate, two processes, `Float64`, subscriber depth 100 (`scripts/max_rate.sh`)

| Config | Clean up to | p50 / p99 there | Above |
|---|---|---|---|
| **Cyclone + iceoryx** | **200 kHz** | 10 / 27 µs | 300 kHz: 98.8 %, p99 0.57 ms; flat out (1.6 MHz offered): 19.6 % |
| **Zenoh, batching off** | **~90 kHz** (publisher-bound) | 53 / 97 µs | `publish()` applies back-pressure. Offered 100–300 kHz, the publisher runs at 88–92 kHz and 100 % of that arrives (p99 96–116 µs) |
| Cyclone DDS | 50 kHz | 22 / 52 µs | 100–150 kHz: 100 %, p99 7.6–32 ms; 200 kHz: 64 % |
| Zenoh, default | 20 kHz | 31 / 52 µs | 50 kHz: 100 %, p99 2.1 ms; 100 kHz: 87 % |
| Fast DDS, UDPv4 only | 20 kHz | 48 / 103 µs | 50 kHz: 100 %, p99 20 ms; ≥ 100 kHz: 37–51 % |
| Fast DDS, default (SHM) | never | 2.9 / 5.7 ms at every rate | 50 kHz: 96 %; ≥ 100 kHz: **0.0–1.5 % received**, 0.1–2.9 s latency |

Every C++ ↔ C++ run was checked for duplicate samples (`dup=0` throughout).

## 3. Same process (composition), 16 B `UInt8MultiArray`

| RMW | Intra-process comms | 100 kHz | 200 kHz | 500 kHz |
|---|---|---|---|---|
| Cyclone | on | **100 %, 3 / 19 µs** | 98.9 % | 92.5 % |
| Cyclone | off | 99.1 % | 92.9 % | 1.9 % |
| Fast DDS | on | 99.3 %, 4 / 64 µs | 99.6 % | 98.0 % |
| Fast DDS | off | 98.5 % | 92.6 % | 43 % |

## 4. Large messages (`UInt8MultiArray`, two processes, 2 kHz offered, subscriber depth 10)

| Config | 64 KB | 256 KB | 1 MB | 4 MB |
|---|---|---|---|---|
| **Zenoh + SHM** (`zenoh-shm`) | 1999 Hz, 66 µs | 1999 Hz, 78 µs | 1999 Hz, 128 µs | **1961 Hz, 296 µs** |
| Zenoh | 2000 Hz, 86 µs | 2000 Hz, 173 µs | 2000 Hz, 399 µs | 869 Hz, 1.5 ms |
| Cyclone + iceoryx | 2000 Hz, 22 µs | 1912 Hz, 32 µs | 1982 Hz, 100 µs | publisher **hangs** with default RouDi pools; 2001 Hz, 341 µs with `config/roudi_config_large.toml` |
| Cyclone DDS | 2000 Hz, 209 µs | 1989 Hz, 357 µs | 1964 Hz, 554 µs | 16 Hz |
| Fast DDS, UDPv4 only | 2000 Hz, 131 µs | 1994 Hz, 154 µs | 1780 Hz, 429 µs | 0 received |

Latencies are p50.
- **Cyclone + iceoryx at 6 MB** with the large-pool config: 1788 Hz, 5.9 ms.
- **Why the default pool hangs:** iceoryx's largest default chunk is 4 MB, and a chunk also carries a header. With no fitting chunk, the publisher blocks forever, as documented upstream.

## 5. Fast DDS SHM on macOS: root cause

- **Profile** (`sample`): the `dds.shm.*` thread sits in `RobustInterprocessCondition::do_timed_wait → try_based_timed_lock<spin_semaphore> → spin_wait::yield → nanosleep`.
- **Why:** the bundled Boost (`workaround.hpp`) disables process-shared POSIX primitives and unnamed semaphores on `__APPLE__`, so `interprocess_semaphore` becomes `spin_semaphore`. That sleeps `tick/2` = 5 ms: `_SC_CLK_TCK` = 100, and the sleep measures 6.04–6.11 ms mean.
- **Rate-independent** (Kilted, depth 100):

  | Rate | Latency p50 |
  |---|---|
  | 50 Hz | 3.3 ms |
  | 200 Hz | 2.8 ms |
  | 2 kHz | 2.9 ms |
  | 5 kHz | 2.9 ms |

- **Same on Jazzy** (Fast DDS 2.14.7), 2 kHz:

  | Config | Received | Latency p50 / p99 |
  |---|---|---|
  | Default, depth 1 | 450 Hz | 2.0 / 5.5 ms |
  | Default, depth 100 | 2000 Hz | 2.86 / 5.65 ms |
  | SHM only, depth 100 | 2000 Hz | 2.86 / 5.68 ms |
  | UDPv4 only, depth 1 | 1999 Hz | 78 / 284 µs |

**Causation check** (`tools/clamp_nanosleep.c`, KEEP_LAST 1, 2 kHz):

| Sleep per poll | Received | p50 / p99 | Idle listener CPU |
|---|---|---|---|
| 5 ms (stock) | 377 Hz | 0.93 / 5.56 ms | 5.6 % |
| 1 ms | 919 Hz | 0.28 / 1.09 ms | 6.3 % |
| 200 µs | 1992 Hz | 0.13 / 0.31 ms | 10.2 % |
| 50 µs | 1864 Hz | 0.06 / 1.16 ms | 19.0 % |

**Proposed primitive** (`tools/os_sync_wait_demo.c`): two processes, 2 kHz, wake latency 5.9–9.7 µs p50 / 16–26 µs p99, 1–1.5 % CPU (three runs).

**`ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`:** rmw_fastrtps adds SHM itself, so `FASTDDS_BUILTIN_TRANSPORTS=UDPv4` is ignored. Measured 316–332 Hz received with KEEP_LAST 1.

## 6. Tuning that did / didn't help

| Knob | Result |
|---|---|
| Zenoh `transport/link/tx/queue/batching/enabled=false` | **Helps**: 50 kHz p99 2.1 ms → 65–112 µs; lossless to the ~90 kHz publisher limit |
| Zenoh `transport/unicast/lowlatency=true` (+ `qos/enabled=false`) | Worse: p99 6–8 ms at 50–100 kHz |
| Zenoh SHM (`transport/shared_memory/enabled=true`) | No effect below 512 B; **big win for large messages** (table 4) |
| Fast DDS XML: UDPv4 + 4 MB socket buffers | Same as `FASTDDS_BUILTIN_TRANSPORTS=UDPv4` |
| Fast DDS `RMW_FASTRTPS_PUBLICATION_MODE=ASYNCHRONOUS` | `publish()` threw `cannot publish data` at 5 kHz and aborted the publisher |
| Cyclone socket buffers / `MaxMessageSize` / `FragmentSize` | No meaningful change |
| rclcpp EventsExecutor | Within 5–10 % at 2 kHz |
| iceoryx publisher depth 17 / 100 (doc says ≤ 16) | Still used shared memory on this build (8 µs); keep ≤ 16 to be safe |

## 7. Cyclone DDS + iceoryx caveats (`scripts/iceoryx_bugs.sh`)

| Case (2 kHz, 4 s) | With iceoryx | Without iceoryx |
|---|---|---|
| rclcpp BEST_EFFORT pub → rclcpp RELIABLE sub (incompatible QoS) | 7992–7998 received | 0 |
| rclcpp pub → rclpy sub | each sample ×2 | no duplicates |
| rclpy pub → rclcpp sub | each sample ×2 | no duplicates |
| rclpy pub → rclpy sub | no duplicates | no duplicates |

- **No RouDi:** if `iox-roudi` isn't running, every node waits and then aborts after ~60 s.
- **Default pools:** with RouDi's default pools, a message that doesn't fit a chunk (≥ ~4 MB) blocks the publisher forever.
- **Other notes:**
  - `ros2 topic hz` shows 2× the real rate for C++ publishers.
  - `transient_local` isn't supported over SHM (ros2/rmw_cyclonedds#401).

## 8. Other observations

- **Timers aren't the limit on macOS:** an `rclcpp` wall timer and `sleep_until` both reach 10 kHz in a single process.
- **USB-serial (FTDI) sensors** arrive in ~16 ms batches on macOS. There is no equivalent of the Linux udev `latency_timer=1` rule. Combined with `KEEP_LAST(1)`, this drops samples on any RMW.
