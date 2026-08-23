# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
README = ROOT / "thirdparty" / "README.md"
CHECKER = ROOT / "thirdparty" / "check_vendor_gate.py"
X86 = ROOT / "thirdparty" / "lib" / "x86_64" / "libLHandProLib.so"
AARCH64 = ROOT / "thirdparty" / "lib" / "aarch64" / "libLHandProLib.so"


class VendorGateTests(unittest.TestCase):
    def test_checker_exists(self):
        self.assertTrue(CHECKER.is_file(), "release checker is missing")

    def run_checker(self, readme: Path, x86: Path = X86,
                    aarch64: Path = AARCH64) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECKER), "--readme", str(readme),
             "--x86", str(x86), "--aarch64", str(aarch64)],
            text=True,
            capture_output=True,
            check=False,
        )

    def test_pending_record_fails_closed(self):
        result = self.run_checker(README)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("PENDING", result.stdout + result.stderr)

    def test_complete_record_passes_with_matching_artifacts(self):
        text = README.read_text(encoding="utf-8")
        replacements = {
            "Public redistribution is blocked until\n"
            "the vendor's redistribution terms are confirmed and recorded here.":
                "Public redistribution terms are confirmed and recorded here.",
            "status remains **PENDING**": "status is CONFIRMED",
            "- Vendor/contact:\n": "- Vendor/contact: Leadshine technical and legal contact\n",
            "- SDK version and response date:\n":
                "- SDK version and response date: 20260727 / 2026-08-24\n",
            "- Callback-quiescence confirmation:\n":
                "- Callback-quiescence confirmation: confirmed in writing\n",
            "- Redistribution/license authorization:\n":
                "- Redistribution/license authorization: confirmed in writing\n",
            "- GPL compatibility review:\n":
                "- GPL compatibility review: completed by counsel\n",
            "- Evidence or agreement reference:\n":
                "- Evidence or agreement reference: vendor-agreement-2026-01\n",
        }
        for before, after in replacements.items():
            text = text.replace(before, after)
        text = re.sub(r"\bpending\b", "confirmed", text, flags=re.IGNORECASE)

        with tempfile.TemporaryDirectory() as directory:
            candidate = Path(directory) / "README.md"
            candidate.write_text(text, encoding="utf-8")
            result = self.run_checker(candidate)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_hash_mismatch_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = Path(directory) / "libLHandProLib.so"
            fake.write_bytes(b"not the vendor SDK")
            result = self.run_checker(README, x86=fake)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("sha", (result.stdout + result.stderr).lower())


if __name__ == "__main__":
    unittest.main()
