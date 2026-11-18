"""Start iox-roudi together with nodes that use Cyclone DDS + iceoryx.

Nodes that start before RouDi is ready wait for it (up to ~60 s), so starting
both in one launch file is fine. Replace the two example nodes with your own.
"""
import os

from launch import LaunchDescription
from launch.actions import ExecuteProcess, SetEnvironmentVariable
from launch_ros.actions import Node

CYCLONE_XML = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cyclonedds_iceoryx.xml')


def generate_launch_description():
    return LaunchDescription([
        SetEnvironmentVariable('RMW_IMPLEMENTATION', 'rmw_cyclonedds_cpp'),
        SetEnvironmentVariable('CYCLONEDDS_URI', 'file://' + CYCLONE_XML),
        ExecuteProcess(cmd=['iox-roudi'], output='screen'),
        Node(package='rmw_rate_bench', executable='repro', arguments=['pub', '10'], output='screen'),
        Node(package='rmw_rate_bench', executable='repro', arguments=['sub', '10'], output='screen'),
    ])
