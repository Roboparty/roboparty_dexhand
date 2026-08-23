#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

"""Fail-closed structural guard for the vendor release response."""

import argparse
import hashlib
import re
import sys
from pathlib import Path


HASH_ROW = re.compile(
    r"^\|\s*(x86-64|AArch64)\s*\|\s*`([0-9a-f]{64})`\s*\|$",
    re.MULTILINE,
)
RESPONSE_FIELDS = (
    "Vendor/contact",
    "SDK version and response date",
    "Callback-quiescence confirmation",
    "Redistribution/license authorization",
    "GPL compatibility review",
    "Evidence or agreement reference",
)
PLACEHOLDER = re.compile(r"<[^>]+>|\b(?:TBD|TODO)\b", re.IGNORECASE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--readme", type=Path, required=True)
    parser.add_argument("--x86", type=Path, required=True)
    parser.add_argument("--aarch64", type=Path, required=True)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fail(message: str) -> None:
    print(f"FAIL vendor gate: {message}")


def main() -> int:
    args = parse_args()
    failures: list[str] = []
    if not args.readme.is_file():
        failures.append(f"README not found: {args.readme}")
        text = ""
    else:
        text = args.readme.read_text(encoding="utf-8")

    rows = dict(HASH_ROW.findall(text))
    expected_paths = {"x86-64": args.x86, "AArch64": args.aarch64}
    for architecture, path in expected_paths.items():
        expected = rows.get(architecture)
        if expected is None:
            failures.append(f"missing {architecture} SHA-256 row")
            continue
        if not path.is_file():
            failures.append(f"{architecture} artifact not found: {path}")
            continue
        actual = sha256(path)
        if actual != expected:
            failures.append(
                f"{architecture} SHA-256 mismatch: expected {expected}, got {actual}"
            )

    if "PENDING" in text.upper():
        failures.append("vendor response is still marked PENDING")

    response = text.split("Response record:", 1)[-1]
    for field in RESPONSE_FIELDS:
        match = re.search(
            rf"^- {re.escape(field)}:\s*(.+)$", response, re.MULTILINE
        )
        if match is None:
            failures.append(f"missing response field: {field}")
            continue
        value = match.group(1).strip()
        if not value or PLACEHOLDER.search(value):
            failures.append(f"empty or placeholder response field: {field}")

    if failures:
        for failure in failures:
            fail(failure)
        return 1

    print(
        "PASS vendor gate: artifact hashes and response record are structurally "
        "complete; human/legal review is still required"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
