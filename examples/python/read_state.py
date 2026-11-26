#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 IcedVodka
# Copyright (C) 2026 wentywenty
# Copyright (C) 2026 cwl
# Copyright (C) 2026 Guangxin YANG

"""Connect and print one snapshot without enabling, homing or moving the hand."""
from connection import connection_options, parse_options


def main():
    options = parse_options(wave=False)
    from rphand import Hand
    hand = Hand()
    try:
        if not hand.connect(connection_options(options)):
            return 1
        state = hand.get_state()
        print('feedback_valid=', state.feedback_valid)
        print('positions:', state.positions)
        print('fault_codes:', state.fault_codes)
        return 0 if state.feedback_valid and not any(state.fault_codes) else 1
    finally:
        hand.disconnect()


if __name__ == '__main__':
    raise SystemExit(main())
