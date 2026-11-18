// repro: minimal 2 kHz std_msgs/Float64 pub/sub. `repro sub <depth>` and `repro pub <depth>` in two terminals.
// Env: EXECUTOR=events uses rclcpp EventsExecutor instead of the default SingleThreadedExecutor.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/experimental/executors/events_executor/events_executor.hpp>
#include <std_msgs/msg/float64.hpp>
using namespace std::chrono;
static double now_us() {return duration<double, std::micro>(steady_clock::now().time_since_epoch()).count();}
int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  const bool pub = argc > 1 && !strcmp(argv[1], "pub");
  const int depth = argc > 2 ? atoi(argv[2]) : 1;
  auto node = std::make_shared<rclcpp::Node>(pub ? "repro_pub" : "repro_sub");
  auto qos = rclcpp::QoS(depth).best_effort();
  rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr p;
  rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr s;
  rclcpp::TimerBase::SharedPtr t;
  std::vector<double> lat; double t0 = 0;
  if (pub) {  // 2 kHz, payload = send time
    p = node->create_publisher<std_msgs::msg::Float64>("repro", qos);
    t = node->create_wall_timer(500us, [&] {std_msgs::msg::Float64 m; m.data = now_us(); p->publish(m);});
  } else {    // print rate and latency every 2 s
    s = node->create_subscription<std_msgs::msg::Float64>("repro", qos, [&](std_msgs::msg::Float64::ConstSharedPtr m) {
      lat.push_back(now_us() - m->data);});
    t = node->create_wall_timer(2s, [&] {
      if (lat.empty()) {return;}
      std::sort(lat.begin(), lat.end());
      printf("received %.0f Hz  latency p50 %.0f us  p99 %.0f us\n", lat.size() / 2.0, lat[lat.size() / 2], lat[lat.size() * 99 / 100]);
      fflush(stdout); lat.clear();});
  }
  if (getenv("EXECUTOR") && !strcmp(getenv("EXECUTOR"), "events")) {
    rclcpp::experimental::executors::EventsExecutor ex;
    ex.add_node(node);
    ex.spin();
  } else {
    rclcpp::executors::SingleThreadedExecutor ex;
    ex.add_node(node);
    ex.spin();
  }
  rclcpp::shutdown();
}
