"""Run the production Setup-result block in a harmless NSIS test installer.

No input method, production registry key, or live service is touched. The child
is cmd.exe with a chosen exit code, and successful continuation writes only
scratch marker files. Requires makensis (or --makensis PATH).
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
MAKENSIS = None


def find_makensis():
    found = shutil.which("makensis")
    if found:
        return found
    for variable in ("ProgramFiles(x86)", "ProgramFiles"):
        base = os.environ.get(variable)
        if base:
            for relative in ("NSIS/makensis.exe", "NSIS/Bin/makensis.exe"):
                candidate = Path(base) / relative
                if candidate.is_file():
                    return str(candidate)
    raise RuntimeError("makensis not found; use --makensis PATH")


def setup_result_block():
    source = (REPO / "output/install.nsi").read_text(encoding="utf-8-sig")
    command = '  ExecWait \'"$INSTDIR\\WeaselSetup.exe" $R2\''
    start = source.index(command)
    if source[:start].endswith("  ClearErrors\n"):
        start -= len("  ClearErrors\n")
    # The production success path jumps into the committed-install phase. The
    # harmless harness has no such label; stop immediately before that jump so
    # success naturally falls through to its scratch success marker while all
    # failure/cancel rollback + Abort branches remain byte-for-byte production.
    end = source.index("  GoTo install_commit", start)
    return source[start:end]


class InstallResultTests(unittest.TestCase):
    def run_case(self, child_exit=None, launch_failure=False):
        scratch = os.environ.get("TMPDIR")
        with tempfile.TemporaryDirectory(
            prefix="weasel-installer-test-", dir=scratch
        ) as folder:
            root = Path(folder)
            prior_data = root / "existing-user-data.txt"
            prior_data.write_text("preserve me", encoding="utf-8")
            block = setup_result_block()
            if not launch_failure:
                block = block.replace(
                    "$INSTDIR\\WeaselSetup.exe", "$SYSDIR\\cmd.exe"
                )
            script = "\n".join(
                [
                    '!include "LogicLib.nsh"',
                    "Unicode true",
                    'Name "Weasel installer regression test"',
                    f'OutFile "{root / "test-installer.exe"}"',
                    "RequestExecutionLevel user",
                    "SilentInstall silent",
                    'LangString SETUPFAILED 1033 "Setup failed"',
                    'LangString SETUPCANCELLED 1033 "Setup cancelled"',
                    "Var RollbackFailed",
                    "Function RollbackPreviousInstall",
                    '  StrCpy $RollbackFailed "0"',
                    '  FileOpen $0 "$INSTDIR\\rollback-marker.txt" w',
                    '  FileWrite $0 "rollback"',
                    "  FileClose $0",
                    "FunctionEnd",
                    "Section",
                    f'  StrCpy $INSTDIR "{root}"',
                    f'  StrCpy $R2 "/c exit {child_exit or 0}"',
                    block,
                    '  FileOpen $0 "$INSTDIR\\success-marker.txt" w',
                    '  FileWrite $0 "success"',
                    "  FileClose $0",
                    "SectionEnd",
                    'Section "Following section"',
                    '  FileOpen $0 "$INSTDIR\\following-marker.txt" w',
                    '  FileWrite $0 "success"',
                    "  FileClose $0",
                    "SectionEnd",
                ]
            )
            nsi = root / "test.nsi"
            nsi.write_text(script, encoding="utf-8")
            compiled = subprocess.run(
                [MAKENSIS, "/V2", str(nsi)], capture_output=True, timeout=30
            )
            self.assertEqual(
                compiled.returncode, 0, compiled.stdout + compiled.stderr
            )
            result = subprocess.run(
                [str(root / "test-installer.exe"), "/S"],
                capture_output=True,
                timeout=30,
            )
            continued = (root / "success-marker.txt").exists()
            following = (root / "following-marker.txt").exists()
            rolled_back = (root / "rollback-marker.txt").exists()
            self.assertEqual(
                prior_data.read_text(encoding="utf-8"), "preserve me"
            )
            return result.returncode, continued, following, rolled_back

    def test_success_continues_without_rollback(self):
        self.assertEqual(self.run_case(0), (0, True, True, False))

    def test_failure_rolls_back_and_stops_success_and_later_sections(self):
        code, continued, following, rolled_back = self.run_case(1)
        self.assertNotEqual(code, 0)
        self.assertFalse(continued)
        self.assertFalse(following)
        self.assertTrue(rolled_back)

    def test_cancel_rolls_back_and_propagates(self):
        self.assertEqual(self.run_case(1223), (1223, False, False, True))

    def test_launch_failure_rolls_back_and_stops(self):
        code, continued, following, rolled_back = self.run_case(
            launch_failure=True
        )
        self.assertNotEqual(code, 0)
        self.assertFalse(continued)
        self.assertFalse(following)
        self.assertTrue(rolled_back)

    def test_install_directory_is_written_only_after_setup_succeeds(self):
        source = (REPO / "output/install.nsi").read_text(encoding="utf-8-sig")
        setup = source.index('  ExecWait \'"$INSTDIR\\WeaselSetup.exe" $R2\'')
        directory = source.index(
            'WriteRegStr HKLM SOFTWARE\\Rime\\Weasel "InstallDir"', setup
        )
        self.assertGreater(directory, setup)

    def test_previous_version_cleanup_is_committed_only_after_setup_succeeds(self):
        source = (REPO / "output/install.nsi").read_text(encoding="utf-8-sig")
        setup = source.index('  ExecWait \'"$INSTDIR\\WeaselSetup.exe" $R2\'')
        commit = source.index("install_commit:", setup)
        guard = source.index(
            "StrCmp $PreviousInstallDir $INSTDIR cleanup_upgrade_backup 0",
            commit,
        )
        cleanup = source.index('RMDir /r "$PreviousInstallDir"', guard)
        self.assertGreater(commit, setup)
        self.assertGreater(cleanup, commit)
        self.assertNotIn("/REBOOTOK", source)

    def test_replacement_uses_the_canonical_version_directory(self):
        source = (REPO / "output/install.nsi").read_text(encoding="utf-8-sig")
        self.assertIn(
            'StrCpy $NewInstallDir "$INSTDIR\\weasel-${WEASEL_VERSION}"',
            source,
        )
        self.assertIn('StrCpy $INSTDIR "$NewInstallDir"', source)
        self.assertNotIn(".upgrade-$UpgradeSlotId", source)
        self.assertNotIn('Rename "$PreviousInstallDir"', source)

    def test_setup_failure_paths_call_rollback_before_abort(self):
        block = setup_result_block()
        self.assertGreaterEqual(block.count("Call RollbackPreviousInstall"), 3)
        for marker in (
            'Abort "$(SETUPFAILED)"',
            'Abort "$(SETUPCANCELLED)"',
        ):
            self.assertIn(marker, block)
        self.assertLess(
            block.index("Call RollbackPreviousInstall"),
            block.index('Abort "$(SETUPFAILED)"'),
        )

    def test_setup_distinguishes_cancel_from_failure(self):
        source = (REPO / "WeaselSetup/WeaselSetup.cpp").read_text(
            encoding="utf-8-sig"
        )
        cancel = source.split("if (IDOK != dlg.DoModal())", 1)[1].split(
            "profile =", 1
        )[0]
        self.assertIn("return ERROR_CANCELLED;", cancel)

    def test_upgrade_backs_up_registry_and_program_tree_before_old_uninstall(self):
        source = (REPO / "output/install.nsi").read_text(encoding="utf-8-sig")
        prepare = source.split("Function PreparePreviousInstall", 1)[1].split(
            "FunctionEnd", 1
        )[0]
        old_uninstall = prepare.index(
            '  ExecWait \'"$PreviousInstallDir\\WeaselSetup.exe" /u\' $R2'
        )
        delete = prepare.index(
            '  Delete "$TEMP\\weasel-hkcu-backup.reg"'
        )
        export = prepare.index(
            '  ExecWait \'reg export HKCU\\Software\\Rime\\Weasel '
            '"$TEMP\\weasel-hkcu-backup.reg" /y\' $R2'
        )
        backup = prepare.index("prepare_backup_program:")
        stop = prepare.index(
            '  ExecWait \'"$PreviousInstallDir\\WeaselServer.exe" /quit\''
        )
        self.assertLess(delete, export)
        self.assertLess(export, backup)
        self.assertLess(backup, stop)
        self.assertLess(stop, old_uninstall)

        backup_guard = prepare[backup:stop]
        self.assertIn(
            '"$SYSDIR\\robocopy.exe" "$PreviousInstallDir" '
            '"$PreviousProgramBackupDir"',
            backup_guard,
        )
        self.assertIn(
            'IfFileExists "$PreviousProgramBackupDir\\WeaselSetup.exe"',
            backup_guard,
        )
        self.assertIn(
            'IfFileExists "$PreviousProgramBackupDir\\WeaselServer.exe"',
            backup_guard,
        )

    def test_upgrade_skips_registry_export_when_no_user_state_exists(self):
        source = (REPO / "output/install.nsi").read_text(encoding="utf-8-sig")
        prepare = source.split("Function PreparePreviousInstall", 1)[1].split(
            "FunctionEnd", 1
        )[0]
        start = prepare.index(
            'EnumRegValue $R4 HKCU "Software\\Rime\\Weasel" 0'
        )
        end = prepare.index("prepare_export_user_config:", start)
        probe = prepare[start:end]
        self.assertIn(
            'EnumRegValue $R4 HKCU "Software\\Rime\\Weasel" 0', probe
        )
        self.assertIn(
            'EnumRegKey $R4 HKCU "Software\\Rime\\Weasel" 0', probe
        )
        self.assertIn(
            'StrCmp $R4 "" prepare_check_user_config_subkey '
            "prepare_export_user_config",
            probe,
        )
        self.assertIn(
            'StrCmp $R4 "" prepare_backup_program prepare_export_user_config',
            probe,
        )

    def test_same_version_locked_files_fail_closed_before_payload_extraction(self):
        source = (REPO / "output/install.nsi").read_text(encoding="utf-8-sig")
        prepare = source.split("Function PreparePreviousInstall", 1)[1].split(
            "FunctionEnd", 1
        )[0]
        same_path = prepare.split("prepare_remove_same_path:", 1)[1].split(
            "prepare_locked:", 1
        )[0]
        locked = prepare.split("prepare_locked:", 1)[1].split(
            "prepare_failed_before_mutation:", 1
        )[0]
        self.assertIn(
            "StrCmp $PreviousInstallDir $INSTDIR 0 prepare_done", same_path
        )
        self.assertIn('RMDir /r "$INSTDIR"', same_path)
        self.assertIn("GoTo prepare_locked", same_path)
        self.assertIn(
            'IfFileExists "$INSTDIR\\*.*" prepare_locked 0', same_path
        )
        self.assertIn("Call RollbackPreviousInstall", locked)
        self.assertIn('StrCpy $R9 "2"', locked)

        section = source.split('Section "Weasel"', 1)[1].split(
            "SectionEnd", 1
        )[0]
        self.assertLess(
            section.index("Call PreparePreviousInstall"),
            section.index("SetOverwrite on"),
        )

    def test_init_failures_are_visible_and_non_destructive(self):
        source = (REPO / "output/install.nsi").read_text(encoding="utf-8-sig")
        macro = source.split("!macro AbortInitFailure", 1)[1].split(
            "!macroend", 1
        )[0]
        self.assertIn("IfSilent +2", macro)
        self.assertIn(
            'MessageBox MB_OK|MB_ICONSTOP "$(SETUPFAILED)"', macro
        )
        self.assertIn("Abort", macro)

        init = source.split("Function .onInit", 1)[1].split(
            "FunctionEnd", 1
        )[0]
        self.assertNotIn('Abort "$(SETUPFAILED)"', init)
        self.assertIn("!insertmacro AbortInitFailure", init)
        self.assertNotIn("WeaselServer.exe\" /quit", init)
        self.assertNotIn("WeaselSetup.exe\" /u", init)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--makensis")
    args, remaining = parser.parse_known_args()
    MAKENSIS = args.makensis or find_makensis()
    unittest.main(argv=[__file__, *remaining])