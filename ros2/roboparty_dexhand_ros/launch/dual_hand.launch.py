from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    defaults = get_package_share_directory('roboparty_dexhand_ros') + '/config/defaults.yaml'
    actions = [DeclareLaunchArgument('config', default_value=defaults),
               DeclareLaunchArgument('joy_mapper', default_value='false'),
               DeclareLaunchArgument('command_rate_hz', default_value='200',
                                     choices=['50', '100', '150', '200'])]
    for side, interface in [('left', 'can0'), ('right', 'can3')]:
        actions.extend([
            DeclareLaunchArgument(side, default_value='true'),
            DeclareLaunchArgument(side + '_interface', default_value=interface),
            DeclareLaunchArgument(side + '_node_id', default_value='1'),
            Node(
                package='roboparty_dexhand_ros', executable='dexhand_node',
                name='dexhand_node', namespace='dexhand_' + side,
                condition=IfCondition(LaunchConfiguration(side)), output='screen',
                parameters=[LaunchConfiguration('config'), {
                    'command_rate_hz': ParameterValue(LaunchConfiguration('command_rate_hz'), value_type=int),
                    'interface': LaunchConfiguration(side + '_interface'),
                    'node_id': ParameterValue(LaunchConfiguration(side + '_node_id'), value_type=int),
                }],
            ),
        ])
    actions.append(Node(
        package='roboparty_dexhand_ros', executable='dexhand_joy_mapper.py',
        name='dexhand_joy_mapper', output='screen',
        condition=IfCondition(LaunchConfiguration('joy_mapper')),
    ))
    return LaunchDescription(actions)
