"""Check an installed ROS node using an impossible CAN interface name."""
import argparse
from pathlib import Path
import signal
import subprocess
import tempfile
import time


def main():
    import rclpy
    from roboparty_dexhand_ros.msg import DexHandState
    from std_srvs.srv import Trigger

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", help="installed dexhand_node executable")
    args = parser.parse_args()
    rclpy.init()
    node = rclpy.create_node("rphand_lifecycle_check")
    states = []
    node.create_subscription(DexHandState, "/rphand_check/state", states.append, 1)
    client = node.create_client(Trigger, "/rphand_check/reset")

    def wait_for(predicate, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            rclpy.spin_once(node, timeout_sec=0.02)
        raise AssertionError("timed out waiting for lifecycle transition")

    def reset():
        future = client.call_async(Trigger.Request())
        wait_for(future.done, 2)
        return future.result()

    with tempfile.TemporaryDirectory(prefix="rphand-ros-check-") as directory:
        log_path = Path(directory) / "node.log"
        with log_path.open("w") as output:
            process = subprocess.Popen([
                args.executable, "--ros-args", "-r", "__ns:=/rphand_check",
                "-p", "interface:=rphand_nonexistent_interface",
            ], stdout=output, stderr=subprocess.STDOUT)
            try:
                assert client.wait_for_service(timeout_sec=10)
                wait_for(lambda: len(states) >= 3)
                assert not any(state.feedback_valid for state in states)
                assert "phase" not in DexHandState.get_fields_and_field_types()
                assert "/rphand_check/stop" not in dict(node.get_service_names_and_types())

                assert reset().success
                busy = reset()
                assert not busy.success and busy.message == "Busy"
                wait_for(lambda: "connect initialization attempt=5\n" in log_path.read_text())
                # Initialization owns the session thread, so periodic state is paused.
                count = len(states)
                deadline = time.monotonic() + 0.3
                while time.monotonic() < deadline:
                    rclpy.spin_once(node, timeout_sec=0.02)
                assert len(states) == count
                wait_for(lambda: "Reset failed at connect" in log_path.read_text())
                wait_for(lambda: len(states) > count)
                assert log_path.read_text().count("connect initialization attempt=") == 30

                assert reset().success
                wait_for(lambda: log_path.read_text().count("connect initialization attempt=1\n") == 2)
                process.send_signal(signal.SIGINT)
                assert process.wait(timeout=10) == 0
                log = log_path.read_text()
                assert log.count("connect initialization attempt=") == 60
                assert "SDK session released" in log
                print("ROS idle state, reset acceptance/Busy, failed reconnect and shutdown passed")
            except BaseException:
                print(log_path.read_text())
                raise
            finally:
                if process.poll() is None:
                    process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                node.destroy_node()
                rclpy.shutdown()


if __name__ == "__main__":
    main()
