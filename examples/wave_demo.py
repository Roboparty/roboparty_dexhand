#!/usr/bin/env python3
"""Compatibility entry point. New examples live in examples/python/."""
from pathlib import Path
import runpy
import sys

if __name__ == '__main__':
    examples = Path(__file__).resolve().parent / 'python'
    sys.path.insert(0, str(examples))
    runpy.run_path(str(examples / 'wave_demo.py'), run_name='__main__')
