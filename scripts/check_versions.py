#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 IcedVodka
# Copyright (C) 2026 wentywenty
# Copyright (C) 2026 cwl
# Copyright (C) 2026 Guangxin YANG

"""Fail when the rphand version declarations disagree."""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def find(path: str, pattern: str, label: str) -> str:
    match = re.search(pattern, read(path), re.MULTILINE)
    if not match:
        print(f"::error file={path}::Could not find {label}")
        raise SystemExit(1)
    return match.group(1)


def main() -> int:
    versions = {
        "pyproject.toml": find(
            "pyproject.toml",
            r'(?m)^version\s*=\s*"([^"]+)"',
            "project version",
        ),
        "CMakeLists.txt": find(
            "CMakeLists.txt",
            r"project\(\s*rphand\s+VERSION\s+([^\s)]+)",
            "rphand project version",
        ),
        "ros2/rphand_ros/package.xml": find(
            "ros2/rphand_ros/package.xml",
            r"<version>\s*([^<\s]+)\s*</version>",
            "ROS package version",
        ),
    }

    docs = read("docs/sdk-manual.md")
    for label, pattern in (
        ("document version", r"文档版本：\s*([^\s；;]+)"),
        ("SDK version", r"适用 SDK 版本：\s*([^\s；;]+)"),
    ):
        match = re.search(pattern, docs)
        if not match:
            print(f"::error file=docs/sdk-manual.md::Could not find {label}")
            return 1
        versions[label] = match.group(1)

    changelog = read("debian/changelog").splitlines()
    match = re.match(r"roboparty-dexhand\s+\(([^)]+)\)", changelog[0]) if changelog else None
    if not match:
        print("::error file=debian/changelog::Could not find version on the first line")
        return 1
    versions["debian/changelog"] = match.group(1).split("-", 1)[0]

    ref = os.environ.get("GITHUB_REF", "")
    if ref.startswith("refs/tags/v"):
        versions["git tag"] = ref[len("refs/tags/v"):]

    print("Version declarations:")
    for name, version in versions.items():
        print(f"  {name}: {version}")

    unique = set(versions.values())
    if len(unique) != 1:
        print(
            "::error::rphand version declarations disagree: "
            + ", ".join(f"{name}={version}" for name, version in versions.items())
        )
        return 1

    print(f"All declarations match: {unique.pop()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
