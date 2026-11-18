// rmw_rate_bench: inter-process topic rate / latency benchmark.
//
// Float64 (fixed-size, like a scalar sensor):
//   bench fsub <secs>                       subscriber: prints uniq/dup counts, rate, latency, gaps
//   bench fpub <hz> <secs> [timer|sleep|max] publisher, payload = send time (steady clock, ns)
// UInt8MultiArray (variable size):
//   bench sub <secs>
//   bench pub <timer|sleep|max|burst> <hz> <bytes> <secs>
//   bench intra <timer|sleep> <hz> <bytes> <secs>   pub+sub in one process
// No DDS:
//   bench timeronly <hz> <secs>   rclcpp wall timer alone
//   bench sleeponly <hz> <secs>   std::this_thread::sleep_until alone
// Environment:
//   BDEPTH   subscriber KEEP_LAST depth (default 100)     PDEPTH  fpub publisher depth (default 1)
//   BREL=1   fsub uses RELIABLE instead of BEST_EFFORT    BNOIPC=1 intra without intra-process comms
//   BPER     burst period in ms for `pub burst` (default 16)  DISC_MS discovery wait (default 1500)
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>
#include <std_msgs/msg/float64.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>
using namespace std::chrono;
using Msg = std_msgs::msg::UInt8MultiArray;
static int64_t now_ns() {return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();}

struct Stats {
  std::vector<int64_t> lat, gap; int64_t last = 0, first = 0; uint64_t n = 0, lastseq = 0, lost = 0;
  void add(int64_t t_sent, uint64_t seq) {
    int64_t t = now_ns(); if (!n) first = t; else {gap.push_back(t - last); if (seq > lastseq + 1) lost += seq - lastseq - 1;}
    last = t; lastseq = seq; lat.push_back(t - t_sent); n++;
  }
  static double pct(std::vector<int64_t> v, double p) {if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (size_t)(p * v.size()))] / 1e3;}
  void print(const char * tag) {
    double secs = (last - first) / 1e9;
    printf("%s n=%llu rate=%.0f Hz lost=%llu | latency us p50=%.0f p99=%.0f max=%.0f | gap us p50=%.0f p99=%.0f max=%.0f\n", tag,
      (unsigned long long)n, secs > 0 ? (n - 1) / secs : 0, (unsigned long long)lost, pct(lat, .5), pct(lat, .99), pct(lat, 1.0),
      pct(gap, .5), pct(gap, .99), pct(gap, 1.0));
    fflush(stdout);
  }
};

static void run_pub(rclcpp::Node::SharedPtr node, std::string mode, double hz, size_t bytes, double secs, rclcpp::Executor & ex) {
  auto pub = node->create_publisher<Msg>("bench", rclcpp::QoS(getenv("BDEPTH") ? atoi(getenv("BDEPTH")) : 100).best_effort());
  auto msg = std::make_shared<Msg>(); msg->data.resize(std::max<size_t>(bytes, 16));
  uint64_t seq = 0; int64_t end = now_ns() + (int64_t)(secs * 1e9);
  auto send = [&]() {seq++; int64_t t = now_ns(); memcpy(msg->data.data(), &t, 8); memcpy(msg->data.data() + 8, &seq, 8); pub->publish(*msg);};
  std::this_thread::sleep_for(1500ms);  // discovery
  int64_t t0 = now_ns();
  if (mode == "timer") {
    auto tm = node->create_wall_timer(nanoseconds((int64_t)(1e9 / hz)), [&]() {send(); if (now_ns() > end) rclcpp::shutdown();});
    ex.spin();
  } else {
    auto next = steady_clock::now(); auto per = nanoseconds((int64_t)(1e9 / hz));
    if (mode == "burst") {  // FTDI-like: every BPER ms, publish hz*BPER/1000 msgs back-to-back
      double per_ms = getenv("BPER") ? atof(getenv("BPER")) : 16.0; int k = (int)(hz * per_ms / 1000.0);
      auto nx = steady_clock::now();
      while (now_ns() < end && rclcpp::ok()) {for (int i = 0; i < k; i++) send(); nx += microseconds((int64_t)(per_ms * 1000)); std::this_thread::sleep_until(nx);}
    }
    while (mode != "burst" && now_ns() < end && rclcpp::ok()) {send(); if (mode == "sleep") {next += per; std::this_thread::sleep_until(next);}}
  }
  printf("pub sent=%llu achieved=%.0f Hz\n", (unsigned long long)seq, seq / ((now_ns() - t0) / 1e9)); fflush(stdout);
}

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  std::string what = argv[1];
  if (what == "sleeponly") {
    double hz = atof(argv[2]), secs = atof(argv[3]); Stats s; auto next = steady_clock::now(); int64_t end = now_ns() + secs * 1e9; uint64_t i = 0;
    while (now_ns() < end) {s.add(now_ns(), ++i); next += nanoseconds((int64_t)(1e9 / hz)); std::this_thread::sleep_until(next);}
    s.print("sleeponly"); return 0;
  }
  if (what == "timeronly") {
    double hz = atof(argv[2]), secs = atof(argv[3]); auto node = std::make_shared<rclcpp::Node>("t"); Stats s; uint64_t i = 0; int64_t end = now_ns() + secs * 1e9;
    auto tm = node->create_wall_timer(nanoseconds((int64_t)(1e9 / hz)), [&]() {s.add(now_ns(), ++i); if (now_ns() > end) rclcpp::shutdown();});
    rclcpp::executors::SingleThreadedExecutor ex; ex.add_node(node); ex.spin(); s.print("timeronly"); return 0;
  }
  if (what == "fsub") {  // std_msgs/Float64 carrying the send time (ns as double)
    double secs = atof(argv[2]); auto node = std::make_shared<rclcpp::Node>("fsub"); Stats s; uint64_t q = 0; std::vector<double> vals; vals.reserve(4000000);
    auto fq = rclcpp::QoS(getenv("BDEPTH") ? atoi(getenv("BDEPTH")) : 100); if (getenv("BREL")) {fq.reliable();} else {fq.best_effort();}
    auto sub = node->create_subscription<std_msgs::msg::Float64>("fbench", fq,
      [&](std_msgs::msg::Float64::ConstSharedPtr m) {s.add((int64_t)m->data, ++q); vals.push_back(m->data);});
    rclcpp::executors::SingleThreadedExecutor ex; ex.add_node(node);
    auto end = steady_clock::now() + duration<double>(secs);
    while (rclcpp::ok() && steady_clock::now() < end) ex.spin_some(100ms);
    {auto u = vals; std::sort(u.begin(), u.end()); size_t uq = std::unique(u.begin(), u.end()) - u.begin(); printf("uniq=%zu dup=%zu ", uq, vals.size() - uq);}
    s.print("fsub"); return 0;
  }
  if (what == "fpub") {  // fpub <hz> <secs> [timer|sleep|max]
    auto node = std::make_shared<rclcpp::Node>("fpub"); double hz = atof(argv[2]), secs = atof(argv[3]);
    std::string mode = argc > 4 ? argv[4] : "timer";
    auto pub = node->create_publisher<std_msgs::msg::Float64>("fbench", rclcpp::QoS(getenv("PDEPTH") ? atoi(getenv("PDEPTH")) : 1).best_effort());
    std::this_thread::sleep_for(std::chrono::milliseconds(getenv("DISC_MS") ? atoi(getenv("DISC_MS")) : 1500));
    uint64_t n = 0; int64_t end = now_ns() + (int64_t)(secs * 1e9), t0 = now_ns();
    auto send = [&] {std_msgs::msg::Float64 m; m.data = (double)now_ns(); pub->publish(m); n++;};
    if (mode == "timer") {
      auto tm = node->create_wall_timer(nanoseconds((int64_t)(1e9 / hz)), [&]() {send(); if (now_ns() > end) rclcpp::shutdown();});
      rclcpp::executors::SingleThreadedExecutor ex; ex.add_node(node); ex.spin();
    } else {
      auto nx = steady_clock::now(); auto per = nanoseconds((int64_t)(1e9 / hz));
      while (now_ns() < end) {send(); if (mode == "sleep") {nx += per; std::this_thread::sleep_until(nx);}}
    }
    printf("pub sent=%llu achieved=%.0f Hz\n", (unsigned long long)n, n / ((now_ns() - t0) / 1e9)); return 0;
  }
  if (what == "sub") {
    double secs = atof(argv[2]); auto node = std::make_shared<rclcpp::Node>("bsub"); Stats s;
    auto sub = node->create_subscription<Msg>("bench", rclcpp::QoS(getenv("BDEPTH") ? atoi(getenv("BDEPTH")) : 100).best_effort(), [&](Msg::ConstSharedPtr m) {
      int64_t t; uint64_t q; memcpy(&t, m->data.data(), 8); memcpy(&q, m->data.data() + 8, 8); s.add(t, q);});
    rclcpp::executors::SingleThreadedExecutor ex; ex.add_node(node);
    auto end = steady_clock::now() + duration<double>(secs);
    while (rclcpp::ok() && steady_clock::now() < end) ex.spin_some(100ms);
    s.print("sub"); return 0;
  }
  if (what == "pub") {
    auto node = std::make_shared<rclcpp::Node>("bpub"); rclcpp::executors::SingleThreadedExecutor ex; ex.add_node(node);
    run_pub(node, argv[2], atof(argv[3]), atoi(argv[4]), atof(argv[5]), ex); rclcpp::shutdown(); return 0;
  }
  if (what == "intra") {
    auto node = std::make_shared<rclcpp::Node>("bintra", rclcpp::NodeOptions().use_intra_process_comms(!getenv("BNOIPC")));
    Stats s;
    auto sub = node->create_subscription<Msg>("bench", rclcpp::QoS(getenv("BDEPTH") ? atoi(getenv("BDEPTH")) : 100).best_effort(), [&](Msg::ConstSharedPtr m) {
      int64_t t; uint64_t q; memcpy(&t, m->data.data(), 8); memcpy(&q, m->data.data() + 8, 8); s.add(t, q);});
    rclcpp::executors::SingleThreadedExecutor ex; ex.add_node(node);
    std::thread spinner;
    if (std::string(argv[2]) != "timer") spinner = std::thread([&] {ex.spin();});
    run_pub(node, argv[2], atof(argv[3]), atoi(argv[4]), atof(argv[5]), ex);
    rclcpp::shutdown(); if (spinner.joinable()) spinner.join(); s.print("intra-sub"); return 0;
  }
  return 1;
}
