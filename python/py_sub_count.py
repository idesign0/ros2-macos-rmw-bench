"""rclpy Float64 subscriber on 'repro' for 4 s; counts callbacks vs unique values.
usage: py_sub_count.py <best_effort|reliable> [depth=1000]"""
import sys
import time

import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy
from std_msgs.msg import Float64

rel = ReliabilityPolicy.RELIABLE if sys.argv[1] == 'reliable' else ReliabilityPolicy.BEST_EFFORT
depth = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
rclpy.init()
node = rclpy.create_node('py_sub_count')
vals = []
node.create_subscription(Float64, 'repro', lambda m: vals.append(m.data), QoSProfile(depth=depth, reliability=rel))
t0 = time.time()
while time.time() - t0 < 4:
    rclpy.spin_once(node, timeout_sec=0.05)
print(f'rclpy {sys.argv[1]} depth={depth}: callbacks={len(vals)} unique={len(set(vals))}')
