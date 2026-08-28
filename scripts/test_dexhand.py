#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty
"""
roboparty_dexhand manual hardware test.

Usage:
    /usr/bin/python3 scripts/test_dexhand.py --confirm-motion

Use the system Python that matches the configured dexhand_py extension.
"""

import argparse
import ctypes
from pathlib import Path
import platform
import sys
import time


def run_hand_sequence(
    create_hand,
    hand_model,
    *,
    sleep_fn=time.sleep,
    output=print,
):
    """Run the authorized motion sequence with an injected factory."""
    output('=' * 60)
    output('  roboparty_dexhand RP_Hand manual motion test')
    output('=' * 60)

    output('\n[1] Creating hand driver...')
    hand = create_hand(
        hand_type='RP_Hand',
        interface_type='canfd',
        interface='can0',
        hand_model=hand_model,
        canfd_node_id=1,
    )

    try:
        output(f'  Driver created: {hand}')
        output(
            '\n[2] Initializing hand '
            '(connect, enable, and home; about 7 s)...'
        )
        initialized = hand.init_hand(
            enable_motors=True,
            home_motors=True,
            home_wait_time=6.0,
        )
        status = 'succeeded' if initialized else 'failed'
        output(f'  Initialization: {status}')
        if not initialized:
            return 1

        total, active = hand.get_dof()
        output(f'  DOF: total={total}, active={active}')

        output('\n[3] Running three close/open cycles...')
        for cycle in range(3):
            output(f'  [{cycle + 1}/3] Closing...')
            for joint in range(1, active + 1):
                hand.set_target_position(joint, 5000)
                hand.set_position_velocity(joint, 15000)
            hand.move_motors(0)
            sleep_fn(2.0)

            closed_position = hand.get_now_position(1)
            output(f'    Finger 1 position: {closed_position}')

            output(f'  [{cycle + 1}/3] Opening...')
            for joint in range(1, active + 1):
                hand.set_target_position(joint, 0)
                hand.set_position_velocity(joint, 15000)
            hand.move_motors(0)
            sleep_fn(2.0)

            open_position = hand.get_now_position(1)
            output(f'    Finger 1 position: {open_position}')

        output('\n[4] Returning to the open position...')
        for joint in range(1, active + 1):
            hand.set_target_position(joint, 0)
            hand.set_position_velocity(joint, 15000)
        hand.move_motors(0)
        sleep_fn(1.0)

        output('\nMotion test completed.')
        return 0
    finally:
        hand.deinit_hand()
        output('  Hand deinitialized')


def main(argv=None):
    """Validate consent, load the selected SDK, and run the manual test."""
    parser = argparse.ArgumentParser(description='Manual RP_Hand motion test')
    parser.add_argument(
        '--confirm-motion',
        action='store_true',
        help=(
            'acknowledge that this script enables, homes, and moves '
            'real hardware'
        ),
    )
    args = parser.parse_args(argv)
    if not args.confirm_motion:
        parser.error('refusing to move hardware without --confirm-motion')

    machine = platform.machine().lower()
    sdk_arches = {
        'x86_64': 'x86_64',
        'amd64': 'x86_64',
        'aarch64': 'aarch64',
        'arm64': 'aarch64',
    }
    try:
        sdk_arch = sdk_arches[machine]
    except KeyError:
        parser.error(f'unsupported machine architecture: {machine}')

    project_root = Path(__file__).resolve().parent.parent
    so_path = (
        project_root
        / 'thirdparty'
        / 'lib'
        / sdk_arch
        / 'libLHandProLib.so'
    )
    sys.path.insert(0, str(project_root / 'build'))

    try:
        ctypes.CDLL(str(so_path))
        print('libLHandProLib.so loaded')
    except OSError as error:
        print(f'unable to load libLHandProLib.so: {error}')
        return 1

    from dexhand_py import HandDriver, HandModel

    return run_hand_sequence(
        HandDriver.create_hand,
        HandModel.RP_HAND_6DOF,
    )


if __name__ == '__main__':
    raise SystemExit(main())
