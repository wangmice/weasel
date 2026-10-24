"""Compile the production ARM64 payload detection and early installation gate.

The payload matrix runs anywhere makensis is available. Windows additionally
runs harmless installers with a simulated host architecture: only scratch
markers are written, and no real installer, service or registry is touched.
"""
import argparse
import itertools
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_install_result import find_makensis

REPO = Path(__file__).resolve().parents[2]
MAKENSIS = None
ARM_FILES = ("weaselARM.dll", "weaselARM64.dll", "weaselARM64X.dll")


def production_blocks():
    source = (REPO / "output/install.nsi").read_text(encoding="utf-8-sig")
    preamble = source[:source.index("Unicode true")]
    init = source.split("Function .onInit\n", 1)[1]
    gate = init[:init.index("  ; if not version >= 8.1")]
    payload = re.search(
        r"  \$\{If\} \$\{IsNativeARM64\}\n(?:(?!  \$\{EndIf\}).)*"
        r'File[^\n]*"weaselARM\.dll"(?:(?!  \$\{EndIf\}).)*  \$\{EndIf\}',
        source, re.DOTALL,
    )
    if not payload:
        raise AssertionError("ARM64 payload block not found")
    return preamble, gate, payload.group(0)


class Arm64PayloadTests(unittest.TestCase):
    def make_harness(self, root, present, arm_host=True):
        for name in present:
            (root / name).write_bytes(b"test payload")
        preamble, gate, payload = production_blocks()
        script = "\n".join([
            preamble,
            '!undef IsNativeARM64',
            f'!define IsNativeARM64 "{1 if arm_host else 0} = 1"',
            'Unicode true',
            'Name "Weasel ARM64 packaging regression test"',
            'OutFile "test-installer.exe"',
            'RequestExecutionLevel user',
            'SilentInstall silent',
            'LangString ARM64NOTSUPPORTED 1033 "Use an ARM64-capable package."',
            'Function .onInit',
            gate,
            # Represents the first upgrade side effect after the gate.
            '  FileOpen $0 "$EXEDIR\\old-version-touched.txt" w',
            '  FileWrite $0 "upgrade started"',
            '  FileClose $0',
            'FunctionEnd',
            'Section',
            '  SetOutPath "$EXEDIR\\installed"',
            payload,
            '  FileOpen $0 "$EXEDIR\\installed.txt" w',
            '  FileWrite $0 "installed"',
            '  FileClose $0',
            'SectionEnd',
        ])
        nsi = root / "test.nsi"
        nsi.write_text(script, encoding="utf-8")
        return nsi

    def compile(self, nsi, preprocess=False):
        prefix = "/" if os.name == "nt" else "-"
        args = [MAKENSIS, prefix + "V2"]
        if preprocess:
            args.append(prefix + "PPO")
        args.append(str(nsi))
        result = subprocess.run(args, cwd=nsi.parent, capture_output=True,
                                text=True, encoding="utf-8", errors="replace",
                                timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_every_incomplete_payload_has_early_rejection_and_no_arm_files(self):
        for bits in itertools.product((False, True), repeat=len(ARM_FILES)):
            if all(bits):
                continue
            present = [name for name, bit in zip(ARM_FILES, bits) if bit]
            with self.subTest(present=present), tempfile.TemporaryDirectory() as folder:
                nsi = self.make_harness(Path(folder), present)
                expanded = self.compile(nsi, preprocess=True)
                init = expanded.split("Function .onInit", 1)[1].split("FunctionEnd", 1)[0]
                self.assertIn("SetErrorLevel 1633", init)
                self.assertLess(init.index("Quit"), init.index("FileOpen"))
                self.assertIn("IfSilent", init)
                self.assertNotRegex(expanded, r'(?mi)^\s*File\s+.*weaselARM')
                self.compile(nsi)

    def test_complete_payload_includes_all_arm_files_without_rejection(self):
        with tempfile.TemporaryDirectory() as folder:
            nsi = self.make_harness(Path(folder), ARM_FILES)
            expanded = self.compile(nsi, preprocess=True)
            self.assertNotIn("SetErrorLevel 1633", expanded)
            for name in ARM_FILES:
                self.assertRegex(expanded, rf'(?m)^\s*File\s+"{re.escape(name)}"')
            self.compile(nsi)

    @unittest.skipUnless(os.name == "nt", "requires Windows to run the NSIS harness")
    def test_silent_arm64_rejection_preserves_existing_installation(self):
        for missing in ARM_FILES:
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                nsi = self.make_harness(root, [f for f in ARM_FILES if f != missing])
                self.compile(nsi)
                result = subprocess.run([str(root / "test-installer.exe"), "/S"], timeout=30)
                self.assertEqual(result.returncode, 1633)
                self.assertFalse((root / "old-version-touched.txt").exists())
                self.assertFalse((root / "installed.txt").exists())

    @unittest.skipUnless(os.name == "nt", "requires Windows to run the NSIS harness")
    def test_x86_x64_package_and_complete_arm64_package_can_install(self):
        for present, arm_host in (([], False), (ARM_FILES, True)):
            with self.subTest(arm_host=arm_host), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                nsi = self.make_harness(root, present, arm_host)
                self.compile(nsi)
                result = subprocess.run([str(root / "test-installer.exe"), "/S"], timeout=30)
                self.assertEqual(result.returncode, 0)
                self.assertTrue((root / "installed.txt").exists())
                if arm_host:
                    for name in ARM_FILES:
                        self.assertEqual((root / "installed" / name).read_bytes(), b"test payload")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--makensis")
    args, remaining = parser.parse_known_args()
    MAKENSIS = args.makensis or find_makensis()
    unittest.main(argv=[__file__, *remaining])
