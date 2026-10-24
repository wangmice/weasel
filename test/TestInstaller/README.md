# Installer regression tests

Run from the repository root on Windows:

```powershell
python test/TestInstaller/test_install_result.py -v
python test/TestInstaller/test_arm64_payload.py -v
powershell -NoProfile -ExecutionPolicy Bypass -File test/TestInstaller/TestFindMakensis.ps1
# Also supported on PowerShell 7:
pwsh -NoProfile -File test/TestInstaller/TestFindMakensis.ps1
```

If NSIS is not on PATH or in its conventional install directory, pass
`--makensis C:/path/to/makensis.exe` to the Python test. Python uses only its
standard library; PowerShell does not require Pester.

The Python test extracts the production Setup-result block from `output/install.nsi`
and compiles it into a silent, non-elevated NSIS harness. It substitutes `cmd.exe`
for the real Setup child and exercises success, failure, cancellation and launch
failure. All output is confined to scratch marker files; the real installer,
input-method registration, service, registry keys and user data are untouched.
It also checks that installation metadata is not written before successful Setup
and that the Setup cancellation branch uses `ERROR_CANCELLED`.

The PowerShell test parses and loads only the real `Find-Makensis` function,
without executing packaging. Local command/filesystem probes cover PATH,
conventional NSIS, missing Scoop, an existing Scoop prefix, and a missing Scoop
package. It does not invoke the host's Scoop or install any dependency.

`test_arm64_payload.py` compiles the production payload detection, early
`.onInit` gate and ARM file block with all eight combinations of the three ARM
DLLs. Incomplete payloads must exclude all ARM files and reject ARM64 before
upgrade side effects; complete payloads must include all three files as required
inputs. These compiler checks also run on Linux with `makensis` installed.
On Windows it additionally runs silent, non-elevated test installers with a
simulated host architecture, checking exit code `1633`, untouched upgrade
markers, successful x86/x64 continuation and complete ARM64 extraction.

These tests verify the changed control-flow contract. They do not certify rollback
of a real interrupted installation or replacement of an existing installed version;
those paths require separate testing on a disposable Windows machine.
