$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$setup = Get-Content -LiteralPath (Join-Path $repo 'WeaselSetup\WeaselSetup.cpp') -Raw
$transaction = Get-Content -LiteralPath (Join-Path $repo 'WeaselSetup\TransactionalInstall.h') -Raw

if ($setup -notmatch '#include\s+"TransactionalInstall\.h"') {
  throw 'WeaselSetup must include the fresh-install transaction'
}
if ($setup -notmatch 'InstallFreshTransactional\(profile,\s*user_dir,\s*silent\)') {
  throw 'fresh setup must pass profile and RimeUserDir into the transaction'
}
$freshFlow = [regex]::IsMatch(
  $setup,
  'if\s*\(!_has_installed\).*?InstallRequestedProfile\(profile,\s*user_dir,\s*silent\).*?return\s+0;',
  [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $freshFlow) {
  throw 'fresh setup must return after the transaction instead of doing a second registry-write pass'
}

foreach ($value in @('WeaselRoot', 'ServerExecutable', 'Profile', 'Hant', 'RimeUserDir')) {
  if ($transaction -notmatch ('RegistryValueSnapshot[\s\S]*?' + [regex]::Escape('L"' + $value + '"'))) {
    throw "transaction does not snapshot $value"
  }
}
if ($transaction -notmatch 'uninstall_ime_file\(L"\.dll",\s*profile,\s*true,\s*&register_text_service\)') {
  throw 'failed fresh setup must clean partial TSF/system-DLL installation'
}
if ($transaction -notmatch 'FAILED\(detail::ChangeLayout\(profile,\s*LayoutRestoreAction::Install\)\)') {
  throw 'InstallLayoutOrTip failure is not treated as an installation failure'
}
if ($transaction -notmatch 'L"RimeUserDir"[\s\S]*?user_dir\.c_str\(\)') {
  throw 'RimeUserDir is not committed inside the fresh-install transaction'
}
if ($transaction -notmatch 'for\s*\(auto\s+it\s*=\s*registry\.rbegin\(\)') {
  throw 'registry rollback must run in reverse order'
}
if ($transaction -notmatch 'RestoreLayout\(profile,\s*old_layout\)') {
  throw 'layout state is not restored during rollback'
}

$modifyStart = $setup.IndexOf('static int CustomInstall')
$modifyEnd = $setup.IndexOf('LPCTSTR GetParamByPrefix', $modifyStart)
if ($modifyStart -lt 0 -or $modifyEnd -le $modifyStart) {
  throw 'unable to isolate CustomInstall modify-install flow'
}
$modify = $setup.Substring($modifyStart, $modifyEnd - $modifyStart)

foreach ($value in @('RimeUserDir', 'Profile', 'Hant')) {
  if ($modify -notmatch ('RegistryValueSnapshot[\s\S]*?' + [regex]::Escape('L"' + $value + '"'))) {
    throw "modify install does not snapshot $value before writing"
  }
}
if ($modify -notmatch 'restore_registry\s*=\s*\[&\]\(\)') {
  throw 'modify install does not define a registry rollback path'
}
if ($modify -notmatch 'for\s*\(auto\s+it\s*=\s*registry\.rbegin\(\)') {
  throw 'modify-install registry rollback must run in reverse order'
}

$writeUserMatch = [regex]::Match($modify, 'L"RimeUserDir",\s*user_dir\.c_str\(\)')
$writeProfileMatch = [regex]::Match($modify, 'L"Profile",\s*profile\.c_str\(\)')
$writeHantMatch = [regex]::Match($modify, 'L"Hant",\s*\(profile\s*==\s*L"hant"')
$switchProfile = $modify.IndexOf('switch_registered_profile(registered_profile, profile, silent)')
if (-not $writeUserMatch.Success -or -not $writeProfileMatch.Success -or
    -not $writeHantMatch.Success -or $switchProfile -lt 0) {
  throw 'modify install is missing a required persistence or profile-switch step'
}
if (-not ($writeUserMatch.Index -lt $writeProfileMatch.Index -and
          $writeProfileMatch.Index -lt $writeHantMatch.Index -and
          $writeHantMatch.Index -lt $switchProfile)) {
  throw 'modify install must persist all registry values before switching the registered profile'
}
if ($modify -notmatch 'switch_registered_profile\(registered_profile,\s*profile,\s*silent\)[\s\S]*?restore_registry\(\)[\s\S]*?return\s+1;') {
  throw 'modify install must restore registry state when the profile switch fails'
}

if ($setup -match 'Sleep\(500\)') {
  throw 'modify install must not rely on fixed 500ms sleeps for server/deployer sequencing'
}
if ($setup -notmatch 'ShellExecuteExW') {
  throw 'modify install must retain process handles for restart/deploy status checks'
}
if ($setup -notmatch 'WaitForSingleObject') {
  throw 'modify install must wait for child/old-server completion explicitly'
}
if ($setup -notmatch 'WaitNamedPipeW') {
  throw 'modify install must wait until the restarted IPC pipe is ready'
}
if ($setup -notmatch 'GetExitCodeProcess') {
  throw 'modify install must inspect helper/deployer exit codes'
}
$restartCall = $modify.IndexOf('RestartServerAndDeploy(dir)')
$successMessage = $modify.IndexOf('IDS_STR_MODIFY_SUCCESS_INFO')
if ($restartCall -lt 0 -or $successMessage -lt 0 -or $restartCall -ge $successMessage) {
  throw 'modify install must finish restart/deploy successfully before showing the success message'
}
if ($modify -notmatch 'if\s*\(!RestartServerAndDeploy\(dir\)\)\s*return\s+1;') {
  throw 'modify install must propagate restart/deploy failure to its exit code'
}

$installer = Get-Content -LiteralPath (Join-Path $repo 'output\install.nsi') -Raw
$preflightStart = $installer.IndexOf('backup_current_config:')
$preflightEnd = $installer.IndexOf('stop_old_version:', $preflightStart)
if ($preflightStart -lt 0 -or $preflightEnd -le $preflightStart) {
  throw 'unable to isolate installer overwrite preflight'
}
$preflight = $installer.Substring($preflightStart, $preflightEnd - $preflightStart)
foreach ($required in @(
  'EnumRegValue $R4 HKCU "Software\Rime\Weasel" 0',
  'EnumRegKey $R4 HKCU "Software\Rime\Weasel" 0',
  'StrCmp $R4 "" check_user_config_subkey export_user_config',
  'StrCmp $R4 "" backup_user_config_ok export_user_config'
)) {
  if (-not $preflight.Contains($required)) {
    throw "installer overwrite preflight is missing '$required'"
  }
}
if ($preflight -notmatch 'export_user_config:[\s\S]*?reg export HKCU\\Software\\Rime\\Weasel[\s\S]*?IfFileExists "\$TEMP\\weasel-hkcu-backup\.reg" backup_user_config_ok 0') {
  throw 'installer must still verify a requested HKCU export before old uninstall'
}

$oldUninstallStart = $installer.IndexOf('  ExecWait ''"$R1\WeaselSetup.exe" /u'' $R2')
$oldUninstallEnd = $installer.IndexOf('  ; Restore the profile/preferences for new Setup /i.', $oldUninstallStart)
if ($oldUninstallStart -lt 0 -or $oldUninstallEnd -le $oldUninstallStart) {
  throw 'unable to isolate previous-version uninstall result handling'
}
$oldUninstallFlow = $installer.Substring($oldUninstallStart, $oldUninstallEnd - $oldUninstallStart)
if ($oldUninstallFlow -match '(?s)\$\{If\}\s+\$R2\s+!=\s+0.*?!insertmacro\s+AbortInitFailure') {
  throw 'previous WeaselSetup /u nonzero exit must not hard-abort transactional overwrite'
}
if ($oldUninstallFlow -notmatch 'DetailPrint\s+"Previous WeaselSetup /u exited with \$R2; continuing transactional replacement\."') {
  throw 'recoverable previous-uninstall failure must remain observable in installer details'
}

if ($installer -notmatch '(?s)!macro AbortInitFailure.*?IfSilent \+2.*?MessageBox MB_OK\|MB_ICONSTOP "\$\(SETUPFAILED\)".*?Abort.*?!macroend') {
  throw 'interactive .onInit failures must display SETUPFAILED before quitting'
}
$initStart = $installer.IndexOf('Function .onInit')
$initEnd = $installer.IndexOf('FunctionEnd', $initStart)
$init = $installer.Substring($initStart, $initEnd - $initStart)
if ($init.Contains('Abort "$(SETUPFAILED)"')) {
  throw '.onInit must not silently abort with a status-only SETUPFAILED string'
}
if (-not $init.Contains('!insertmacro AbortInitFailure')) {
  throw '.onInit fatal upgrade paths must use the visible failure macro'
}

Write-Host 'Setup transaction policy: PASS'
