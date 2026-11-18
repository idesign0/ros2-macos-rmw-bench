"""rclpy Float64 publisher, ~2 kHz for 4 s (sleep-paced). usage: py_pub.py [topic=fbench]"""
import sys
import time

import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy
from std_msgs.msg import Float64

topic = sys.argv[1] if len(sys.argv) > 1 else 'fbench'
rclpy.init()
node = rclpy.create_node('py_pub')
pub = node.create_publisher(Float64, topic, QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT))
time.sleep(1.5)  # discovery
t0 = time.time()
sent = 0
while time.time() - t0 < 4:
    msg = Float64()
    msg.data = float(time.clock_gettime_ns(time.CLOCK_UPTIME_RAW))  # unique per message
    pub.publish(msg)
    sent += 1
    time.sleep(0.0005)
print(f'py_pub sent={sent}')
