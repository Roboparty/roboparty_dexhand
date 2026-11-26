#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 IcedVodka
# Copyright (C) 2026 wentywenty
# Copyright (C) 2026 cwl
# Copyright (C) 2026 Guangxin YANG

"""Compatibility entry point. New examples live in examples/python/."""
from pathlib import Path
import runpy
import sys

if __name__ == '__main__':
    examples = Path(__file__).resolve().parent / 'python'
    sys.path.insert(0, str(examples))
    runpy.run_path(str(examples / 'wave_demo.py'), run_name='__main__')
