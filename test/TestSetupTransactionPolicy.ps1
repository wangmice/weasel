$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$setup = Get-Content -LiteralPath (Join-Path $repo 'WeaselSetup\WeaselSetup.cpp') -Raw
$transaction = Get-Content -LiteralPath (Join-Path $repo 'WeaselSetup\TransactionalInstall.h') -Raw
$imeSetup = Get-Content -LiteralPath (Join-Path $repo 'WeaselSetup\imesetup.cpp') -Raw
$uninstallTransaction = Get-Content -LiteralPath (Join-Path $repo 'WeaselSetup\TsfUninstallTransaction.h') -Raw

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

foreach ($value in @('WeaselRoot', 'ServerExecutable', 'Profile', 'Hant', 'RimeUserDir', 'PendingUninstall')) {
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
if ($transaction -notmatch 'RegDeleteValueW\(user_handle,\s*L"PendingUninstall"\)') {
  throw 'successful fresh install must clear a stale PendingUninstall marker'
}
if ($transaction -notmatch 'for\s*\(auto\s+it\s*=\s*registry\.rbegin\(\)') {
  throw 'registry rollback must run in reverse order'
}
if ($transaction -notmatch 'RestoreLayout\(profile,\s*old_layout\)') {
  throw 'layout state is not restored during rollback'
}

if ($imeSetup -notmatch '#include\s+"TsfUninstallTransaction\.h"') {
  throw 'imesetup must include the TSF uninstall transaction'
}
if ($uninstallTransaction -notmatch 'remove_layouts\(\)[\s\S]*?unregister_services\(\)') {
  throw 'logical uninstall must remove LayoutOrTip while profiles still exist, then unregister TSF'
}
if ($uninstallTransaction -notmatch 'unregister_services\(\)[\s\S]*?restore_services\(\)[\s\S]*?restore_default_profiles\(\)[\s\S]*?restore_profiles\(\)[\s\S]*?restore_layouts\(\)') {
  throw 'failed regsvr32 phase must rebuild registration and machine defaults before restoring user state'
}

$uninstallStart = $imeSetup.IndexOf('int uninstall(bool silent)')
$hasInstalledStart = $imeSetup.IndexOf('bool has_installed()', $uninstallStart)
if ($uninstallStart -lt 0 -or $hasInstalledStart -le $uninstallStart) {
  throw 'unable to isolate direct uninstall flow'
}
$uninstallFlow = $imeSetup.Substring($uninstallStart, $hasInstalledStart - $uninstallStart)
if ($uninstallFlow -notmatch 'uninstall_text_service_transactional\(profile,\s*silent\)') {
  throw 'direct /u must use the transactional TSF uninstall path'
}
if ($uninstallFlow -notmatch 'pending\s*==\s*PendingUninstallPhase::None[\s\S]*?uninstall_text_service_transactional[\s\S]*?pending\s*==\s*PendingUninstallPhase::LogicalRetry[\s\S]*?recover_text_service_uninstall_forward[\s\S]*?delete_tsf_files_after_unregister') {
  throw 'uninstall must route fresh work transactionally, persisted LogicalRetry forward, and FileCleanup physically'
}
if ($uninstallFlow -match 'install_layout_or_tip\(profile,\s*LayoutOrTipAction::Uninstall\)[\s\S]*?uninstall_ime_file') {
  throw 'direct /u must not retain the old layout-first non-transactional sequence'
}

$transactionalStart = $imeSetup.IndexOf('static int uninstall_text_service_transactional')
$installStart = $imeSetup.IndexOf('int install(const std::wstring& profile, bool silent)', $transactionalStart)
if ($transactionalStart -lt 0 -or $installStart -le $transactionalStart) {
  throw 'unable to isolate transactional TSF uninstall implementation'
}
$transactionalFlow = $imeSetup.Substring($transactionalStart, $installStart - $transactionalStart)
if ($transactionalFlow -notmatch 'capture_text_service_user_state\(states\)') {
  throw 'TSF uninstall must snapshot profile/layout state before mutation'
}
if ($imeSetup -notmatch 'enum\s+class\s+PendingUninstallPhase\s*:\s*DWORD[\s\S]*?LogicalRetry\s*=\s*1[\s\S]*?FileCleanup\s*=\s*2') {
  throw 'PendingUninstall must distinguish logical retry from post-commit file cleanup'
}
if ($imeSetup -notmatch 'static\s+bool\s+read_pending_uninstall_phase[\s\S]*?opened\s*!=\s*ERROR_SUCCESS[\s\S]*?return\s+false;' -or
    $uninstallFlow -notmatch 'if\s*\(!read_pending_uninstall_phase\(&pending\)\)\s*return\s+1;') {
  throw 'uninstall must fail closed when the persisted recovery phase cannot be read'
}
if ($transactionalFlow -notmatch 'is_profile_enabled_by_default[\s\S]*?enabled_by_default') {
  throw 'TSF uninstall must snapshot machine default-profile state before unregistering'
}
if ($transactionalFlow -notmatch 'set_pending_uninstall_phase\(PendingUninstallPhase::LogicalRetry\)[\s\S]*?RunTsfUninstallTransaction') {
  throw 'logical retry state must be persisted before the first uninstall mutation'
}
if ($transactionalFlow -notmatch 'RunTsfUninstallTransaction\(ops\)[\s\S]*?set_pending_uninstall_phase\(PendingUninstallPhase::FileCleanup\)') {
  throw 'logical commit must transition to file-cleanup state'
}
if ($transactionalFlow -match 'delete_tsf_files_after_unregister') {
  throw 'transactional logical helper must not perform physical DLL cleanup'
}
if ($transactionalFlow -notmatch 'set_pending_uninstall_phase\(PendingUninstallPhase::FileCleanup\)[\s\S]*?restore_layouts\(\)[\s\S]*?set_pending_uninstall_phase\(PendingUninstallPhase::LogicalRetry\)') {
  throw 'failed file-cleanup phase transition must reassert LogicalRetry after rollback'
}
if ($transactionalFlow -notmatch 'restore_services\(\)[\s\S]*?restore_default_profiles\(\)[\s\S]*?restore_profiles\(\)[\s\S]*?restore_layouts\(\)') {
  throw 'failed phase transition must rollback registration defaults and user state'
}
$forwardStart = $imeSetup.IndexOf('static int recover_text_service_uninstall_forward')
$forwardEnd = $imeSetup.IndexOf('static int uninstall_text_service_transactional', $forwardStart)
if ($forwardStart -lt 0 -or $forwardEnd -le $forwardStart) {
  throw 'unable to isolate cross-process forward recovery'
}
$forwardFlow = $imeSetup.Substring($forwardStart, $forwardEnd - $forwardStart)
if ($forwardFlow -notmatch 'RunTsfUninstallForwardRecovery\(ops\)' -or
    $forwardFlow -notmatch 'process_tsf_registration_files\(L"\.dll",\s*false,\s*profile,\s*silent,\s*true,\s*true\)') {
  throw 'forward recovery must retry every existing architecture and tolerate a missing primary DLL'
}
if ($imeSetup -notmatch 'allow_missing_primary[\s\S]*?unregister_text_service_without_dll\(\)[\s\S]*?return\s+0;') {
  throw 'forward recovery must retain best-effort direct TSF unregister when the primary DLL is missing'
}
if ($uninstallTransaction -notmatch 'RunTsfUninstallForwardRecovery[\s\S]*?remove_layouts_best_effort\(\)[\s\S]*?unregister_services\(\)[\s\S]*?mark_file_cleanup\(\)') {
  throw 'forward recovery must converge from layout cleanup through unregister to FileCleanup'
}
if ($uninstallFlow -notmatch 'committed\s*!=\s*PendingUninstallPhase::FileCleanup[\s\S]*?delete_tsf_files_after_unregister') {
  throw 'DLL deletion must require a persisted FileCleanup phase'
}
if ($transactionalFlow -notmatch 'process_tsf_registration_files[\s\S]*?rollback_registration_profile[\s\S]*?set_profile_enabled_by_default[\s\S]*?set_profile_enabled_for_user[\s\S]*?restore_layout_or_tip') {
  throw 'rollback must use the snapshotted machine default profile before restoring current-user state'
}
if ($transactionalFlow -notmatch 'if\s*\(!state\.layout\.enabled\s*-and\s*!state\.layout\.is_default\)' -and
    $transactionalFlow -notmatch 'if\s*\(!state\.layout\.enabled\s*&&\s*!state\.layout\.is_default\)') {
  throw 'layout rollback must skip profiles that were not present/enabled before uninstall'
}
if ($transactionalFlow -notmatch 'capture_text_service_user_state\(states\)[\s\S]*?set_pending_uninstall_phase\(PendingUninstallPhase::LogicalRetry\)[\s\S]*?recover_text_service_uninstall_forward\(profile,\s*silent\)') {
  throw 'snapshot failure must persist LogicalRetry and fall forward instead of blocking damaged installs'
}
if ($imeSetup -notmatch 'MOVEFILE_DELAY_UNTIL_REBOOT\)\)\s*return\s+TRUE;') {
  throw 'deferred DLL deletion must only report success when reboot cleanup was actually queued'
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

$elevationStart = $setup.IndexOf('int RestartAsAdmin')
if ($elevationStart -lt 0) {
  throw 'unable to isolate RestartAsAdmin elevation flow'
}
$elevation = $setup.Substring($elevationStart)
if ($elevation -match 'WaitForSingleObject\(execInfo\.hProcess,\s*INFINITE\)') {
  throw 'RestartAsAdmin must not wait forever for the elevated child'
}
if ($setup -notmatch 'kElevationCommandTimeoutMs\s*=\s*60\s*\*\s*60\s*\*\s*1000' -or
    $elevation -notmatch 'WaitForSingleObject\(execInfo\.hProcess,\s*kElevationCommandTimeoutMs\)') {
  throw 'command-line elevation must use a finite child wait'
}
if ($elevation -notmatch 'interactive_launch[\s\S]*?CloseHandle\(execInfo\.hProcess\)[\s\S]*?return\s+0;') {
  throw 'interactive elevation must detach the unelevated launcher after successful UAC launch'
}
if ($elevation -match 'TerminateProcess\s*\(\s*execInfo\.hProcess') {
  throw 'the unelevated launcher must not forcibly terminate an elevated setup transaction'
}
if ($elevation -notmatch 'WAIT_TIMEOUT[\s\S]*?return\s+ERROR_TIMEOUT;') {
  throw 'elevation timeout must propagate an explicit failure status'
}

if ($setup -match 'RunProcessAndWait\(deployer,\s*L"/deploy",\s*INFINITE') {
  throw 'modify install must not wait forever for WeaselDeployer'
}
if ($setup -notmatch 'kDeployerOperationTimeoutMs\s*=\s*15\s*\*\s*60\s*\*\s*1000\s*\+\s*10\s*\*\s*1000' -or
    $setup -notmatch 'kDeployerTerminationWaitMs\s*=\s*5000') {
  throw 'modify install must wait slightly beyond the child watchdog and keep termination confirmation bounded'
}

if ($setup -notmatch 'CreateProcessW\(deployer\.c_str\(\)[\s\S]*?CREATE_NO_WINDOW[\s\S]*?&process_info\)') {
  throw 'bounded deployer execution must use a directly owned process handle instead of shell-launch handle semantics'
}
if ($setup -notmatch 'enum\s+class\s+DeployerRunResult\s*\{[\s\S]*?Success[\s\S]*?FailedStopped[\s\S]*?FailedStillRunning') {
  throw 'deployer wait results must distinguish a confirmed stop from a still-running helper'
}
$runDeployerStart = $setup.IndexOf('DeployerRunResult RunDeployer')
$runDeployerEnd = $setup.IndexOf('}  // namespace', $runDeployerStart)
if ($runDeployerStart -lt 0 -or $runDeployerEnd -le $runDeployerStart) {
  throw 'unable to isolate bounded RunDeployer implementation'
}
$runDeployer = $setup.Substring($runDeployerStart, $runDeployerEnd - $runDeployerStart)
if ($runDeployer -notmatch 'WaitForSingleObject\(process,\s*kDeployerOperationTimeoutMs\)' -or
    $runDeployer -notmatch 'TerminateProcess\(process,\s*ERROR_TIMEOUT\)' -or
    $runDeployer -notmatch 'WaitForSingleObject\(process,\s*kDeployerTerminationWaitMs\)') {
  throw 'RunDeployer must time out, terminate, and confirm process exit with bounded waits'
}
$restartCall = $modify.IndexOf('if (!RestartServer(dir))')
$deployCall = $modify.IndexOf('const DeployerRunResult deploy_result = RunDeployer(dir);')
$successMessage = $modify.IndexOf('IDS_STR_MODIFY_SUCCESS_INFO')
if ($restartCall -lt 0 -or $deployCall -lt 0 -or $successMessage -lt 0 -or
    -not ($restartCall -lt $deployCall -and $deployCall -lt $successMessage)) {
  throw 'modify install must finish restart/deploy successfully before showing the success message'
}
if ($modify -notmatch 'if\s*\(!RestartServer\(dir\)\)\s*\{[\s\S]*?rollback_modify_state\(\)[\s\S]*?return\s+1;') {
  throw 'modify install must rollback and fail when server restart fails'
}
if ($modify -notmatch 'const\s+DeployerRunResult\s+deploy_result\s*=\s*RunDeployer\(dir\)' -or
    $modify -notmatch 'deploy_result\s*==\s*DeployerRunResult::FailedStillRunning[\s\S]*?return\s+1;' -or
    $modify -notmatch 'rollback_modify_state\(\)[\s\S]*?RestartServer\(dir\)[\s\S]*?return\s+1;') {
  throw 'modify install must fail closed while a deployer may still run and rollback only after confirmed stop'
}

$installer = Get-Content -LiteralPath (Join-Path $repo 'output\install.nsi') -Raw

$prepareStart = $installer.IndexOf('Function PreparePreviousInstall')
$prepareEnd = $installer.IndexOf('FunctionEnd', $prepareStart)
if ($prepareStart -lt 0 -or $prepareEnd -le $prepareStart) {
  throw 'unable to isolate installer transactional overwrite preparation'
}
$prepare = $installer.Substring($prepareStart, $prepareEnd - $prepareStart)

foreach ($required in @(
  'EnumRegValue $R4 HKCU "Software\Rime\Weasel" 0',
  'EnumRegKey $R4 HKCU "Software\Rime\Weasel" 0',
  'StrCmp $R4 "" prepare_check_user_config_subkey prepare_export_user_config',
  'StrCmp $R4 "" prepare_backup_program prepare_export_user_config'
)) {
  if (-not $prepare.Contains($required)) {
    throw "installer overwrite preflight is missing '$required'"
  }
}
if ($prepare -notmatch 'prepare_export_user_config:[\s\S]*?reg export HKCU\\Software\\Rime\\Weasel[\s\S]*?IfFileExists "\$TEMP\\weasel-hkcu-backup\.reg" prepare_backup_program prepare_failed_before_mutation') {
  throw 'installer must verify a requested HKCU export before mutating the old install'
}

$programBackup = $prepare.IndexOf('prepare_backup_program:')
$stopOld = $prepare.IndexOf('  ExecWait ''"$PreviousInstallDir\WeaselServer.exe" /quit'' $R2')
$oldUninstall = $prepare.IndexOf('  ExecWait ''"$PreviousInstallDir\WeaselSetup.exe" /u'' $R2')
if ($programBackup -lt 0 -or $stopOld -lt 0 -or $oldUninstall -lt 0 -or
    -not ($programBackup -lt $stopOld -and $stopOld -lt $oldUninstall)) {
  throw 'complete program backup must finish before the old service is stopped or unregistered'
}
$serverStopFlow = $prepare.Substring($stopOld, $oldUninstall - $stopOld)
if ($serverStopFlow -notmatch 'ExecWait\s+''"\$PreviousInstallDir\\WeaselServer\.exe" /quit''\s+\$R2[\s\S]*?\$R2\s*!=\s*0[\s\S]*?prepare_failed_before_mutation') {
  throw 'upgrade must stop before TSF uninstall when the previous server does not confirm shutdown'
}
$backupFlow = $prepare.Substring($programBackup, $stopOld - $programBackup)
if ($backupFlow -notmatch 'weasel-program-backup-\$UpgradeSlotId') {
  throw 'installer must use an isolated temporary program backup directory'
}
if ($backupFlow -notmatch 'robocopy\.exe[\s\S]*?"\$PreviousInstallDir"[\s\S]*?"\$PreviousProgramBackupDir"') {
  throw 'installer must copy the complete previous program tree before mutation'
}
foreach ($critical in @('WeaselSetup.exe', 'WeaselServer.exe')) {
  if ($backupFlow -notmatch ('IfFileExists\s+"\$PreviousProgramBackupDir\\' + [regex]::Escape($critical) + '"')) {
    throw "installer must verify $critical in the program backup"
  }
}

$restorePrefs = $prepare.IndexOf('  ; Restore preferences for the replacement Setup.', $oldUninstall)
if ($restorePrefs -lt 0) {
  throw 'unable to isolate previous-version uninstall result handling'
}
$oldUninstallFlow = $prepare.Substring($oldUninstall, $restorePrefs - $oldUninstall)
if ($oldUninstallFlow -notmatch 'ExecWait\s+''"\$PreviousInstallDir\\WeaselSetup\.exe" /u''\s+\$R2[\s\S]*?Call RollbackPreviousInstall[\s\S]*?StrCpy \$R9 "1"[\s\S]*?Return') {
  throw 'previous WeaselSetup /u failure must rollback and stop the upgrade before replacement'
}
if ($oldUninstallFlow -match 'continuing transactional replacement') {
  throw 'upgrade must not continue after previous WeaselSetup /u fails'
}

$samePathStart = $prepare.IndexOf('prepare_remove_same_path:')
$lockedStart = $prepare.IndexOf('prepare_locked:', $samePathStart)
if ($samePathStart -lt 0 -or $lockedStart -le $samePathStart) {
  throw 'unable to isolate same-path locked-file guard'
}
$samePathFlow = $prepare.Substring($samePathStart, $lockedStart - $samePathStart)
foreach ($required in @(
  'StrCmp $PreviousInstallDir $INSTDIR 0 prepare_done',
  'RMDir /r "$INSTDIR"',
  'GoTo prepare_locked',
  'IfFileExists "$INSTDIR\*.*" prepare_locked 0'
)) {
  if (-not $samePathFlow.Contains($required)) {
    throw "same-version overwrite guard is missing '$required'"
  }
}
$lockedEnd = $prepare.IndexOf('prepare_failed_before_mutation:', $lockedStart)
$lockedFlow = $prepare.Substring($lockedStart, $lockedEnd - $lockedStart)
if ($lockedFlow -notmatch 'Call RollbackPreviousInstall[\s\S]*?StrCpy \$R9 "2"') {
  throw 'locked same-version replacement must rollback and report the locked target'
}

$sectionStart = $installer.IndexOf('Section "Weasel"')
$sectionEnd = $installer.IndexOf('SectionEnd', $sectionStart)
if ($sectionStart -lt 0 -or $sectionEnd -le $sectionStart) {
  throw 'unable to isolate Weasel install section'
}
$section = $installer.Substring($sectionStart, $sectionEnd - $sectionStart)
$canonical = '  StrCpy $NewInstallDir "$INSTDIR\weasel-${WEASEL_VERSION}"'
$selectTarget = '  StrCpy $INSTDIR "$NewInstallDir"'
$prepareCall = '  Call PreparePreviousInstall'
$overwrite = '  SetOverwrite on'
$canonicalIndex = $section.IndexOf($canonical)
$targetIndex = $section.IndexOf($selectTarget)
$prepareIndex = $section.IndexOf($prepareCall)
$overwriteIndex = $section.IndexOf($overwrite)
if ($canonicalIndex -lt 0 -or $targetIndex -lt 0 -or $prepareIndex -lt 0 -or
    $overwriteIndex -lt 0 -or
    -not ($canonicalIndex -lt $targetIndex -and $targetIndex -lt $prepareIndex -and
          $prepareIndex -lt $overwriteIndex)) {
  throw 'installer must select the canonical path, prepare it transactionally, then enable overwrite'
}
$replacementStart = $section.IndexOf('StrCpy $ReplacementStarted "1"')
if ($replacementStart -lt 0 -or -not ($prepareIndex -lt $replacementStart -and
                                       $replacementStart -lt $overwriteIndex)) {
  throw 'installer must mark replacement start only after previous-install preparation succeeds and before writing payload'
}
if ($installer -notmatch 'Var OrphanedPreviousInstall' -or
    $init -notmatch 'StrCpy \$OrphanedPreviousInstall "0"') {
  throw 'installer must track orphaned previous-install recovery explicitly'
}
if ($section -notmatch 'StrCmp \$OrphanedPreviousInstall "1" setup_preserve_existing_profile') {
  throw 'orphaned recovery must bypass ambiguous /i setup routing'
}
if ($installer -match '\.upgrade-\$UpgradeSlotId') {
  throw 'installer must not create a side-by-side .upgrade-* replacement slot'
}
if ($installer -match '/REBOOTOK') {
  throw 'installer must not schedule install/version directories for deferred reboot deletion'
}
if ($section -notmatch 'StrCmp \$OrphanedPreviousInstall "1" setup_preserve_existing_profile[\s\S]*?\$\{GetOptions\}\s+\$R0\s+"/S"[\s\S]*?StrCmp \$HadPreviousInstall "1" setup_preserve_existing_profile' -or
    $section -notmatch 'setup_preserve_existing_profile:[\s\S]*?ReadRegStr \$R4 HKCU "Software\\Rime\\Weasel" "Profile"[\s\S]*?setup_profile_hant:[\s\S]*?StrCpy \$R2 "/t"[\s\S]*?setup_profile_hongkong:[\s\S]*?StrCpy \$R2 "/hk"[\s\S]*?setup_profile_macau:[\s\S]*?StrCpy \$R2 "/mc"[\s\S]*?setup_profile_singapore:[\s\S]*?StrCpy \$R2 "/sg"') {
  throw 'silent upgrade and orphaned recovery must preserve the existing profile with an explicit install command'
}
if ($section -notmatch 'setup_profile_legacy:[\s\S]*?StrCpy \$R5 "0"[\s\S]*?ReadRegDWORD \$R5 HKCU "Software\\Rime\\Weasel" "Hant"') {
  throw 'silent upgrade legacy profile fallback must initialize and read Hant safely'
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
if ($init -match 'WeaselServer\.exe"\s+/quit|WeaselSetup\.exe"\s+/u') {
  throw '.onInit must remain non-destructive; old shutdown/uninstall belongs in PreparePreviousInstall'
}
if ($init -notmatch 'previous_invalid:[\s\S]*?StrCpy \$OrphanedPreviousInstall "1"[\s\S]*?StrCpy \$HadPreviousInstall "0"[\s\S]*?GoTo done') {
  throw 'missing or damaged previous program root must enter orphaned forward recovery instead of aborting'
}
$previousInvalidStart = $init.IndexOf('previous_invalid:')
$doneLabel = $init.IndexOf('done:', $previousInvalidStart)
if ($previousInvalidStart -lt 0 -or $doneLabel -le $previousInvalidStart) {
  throw 'unable to isolate orphaned previous-install recovery block'
}
$previousInvalid = $init.Substring($previousInvalidStart, $doneLabel - $previousInvalidStart)
if ($previousInvalid -match 'Abort|Quit|AbortInitFailure') {
  throw 'orphaned previous installation must remain installable through forward recovery'
}

# A failed program-tree restore must never be treated as a runnable rollback.
if (-not $installer.Contains('Var ProgramRestoreFailed')) {
  throw 'installer must track program-tree restoration separately from aggregate rollback status'
}
$restoreStart = $installer.IndexOf('Function RestorePreviousProgramBackup')
$restoreEnd = $installer.IndexOf('FunctionEnd', $restoreStart)
if ($restoreStart -lt 0 -or $restoreEnd -le $restoreStart) {
  throw 'unable to isolate RestorePreviousProgramBackup'
}
$restoreProgram = $installer.Substring($restoreStart, $restoreEnd - $restoreStart)
if ($restoreProgram -notmatch 'restore_program_failed:[\s\S]*?StrCpy \$ProgramRestoreFailed "1"[\s\S]*?StrCpy \$RollbackFailed "1"') {
  throw 'program restore failures must set both the dedicated and aggregate rollback failure flags'
}
foreach ($critical in @('WeaselSetup.exe', 'WeaselServer.exe')) {
  if ($restoreProgram -notmatch ('restore_program_ok:[\s\S]*?IfFileExists\s+"\$PreviousInstallDir\\' + [regex]::Escape($critical) + '"\s+0\s+restore_program_failed')) {
    throw "restored program tree must verify $critical before becoming runnable"
  }
}

$rollbackStart = $installer.IndexOf('Function RollbackPreviousInstall')
$rollbackEnd = $installer.IndexOf('FunctionEnd', $rollbackStart)
if ($rollbackStart -lt 0 -or $rollbackEnd -le $rollbackStart) {
  throw 'unable to isolate RollbackPreviousInstall'
}
$rollback = $installer.Substring($rollbackStart, $rollbackEnd - $rollbackStart)
$restoreCall = $rollback.IndexOf('  Call RestorePreviousProgramBackup')
$programFailureGuard = $rollback.IndexOf('  StrCmp $ProgramRestoreFailed "1" rollback_restore_base_registry', $restoreCall)
$registerOld = $rollback.IndexOf('  IfFileExists "$PreviousInstallDir\WeaselSetup.exe" 0 rollback_registration_failed', $restoreCall)
if ($restoreCall -lt 0 -or $programFailureGuard -lt 0 -or $registerOld -lt 0 -or
    -not ($restoreCall -lt $programFailureGuard -and $programFailureGuard -lt $registerOld)) {
  throw 'rollback must gate old registration on successful program-tree restoration'
}
if ($rollback -match '(?m)^rollback_register_old:\s*$') {
  throw 'rollback registration must not retain an unused fallthrough label'
}
if ($rollback -notmatch 'rollback_registration_failed:[\s\S]*?StrCpy \$RollbackFailed "1"[\s\S]*?GoTo rollback_restore_base_registry') {
  throw 'failed old-version registration must skip runtime autorun/server restoration'
}
if ($rollback -match 'WeaselSetup\.exe" /i') {
  throw 'rollback must not use ambiguous /i registration that can route to modify-install'
}
foreach ($profileRestore in @(
  'StrCpy $R6 "/s"',
  'StrCpy $R6 "/t"',
  'StrCpy $R6 "/hk"',
  'StrCpy $R6 "/mc"',
  'StrCpy $R6 "/sg"',
  'ExecWait ''"$PreviousInstallDir\WeaselSetup.exe" $R6'' $R2'
)) {
  if (-not $rollback.Contains($profileRestore)) {
    throw "rollback must force the restored TSF payload with '$profileRestore'"
  }
}
if ($rollback -notmatch 'StrCmp \$ReplacementStarted "1" rollback_remove_partial_new rollback_restore_program') {
  throw 'rollback must not unregister the old install again before replacement files exist'
}
if ($rollback -notmatch 'rollback_profile_legacy:[\s\S]*?StrCpy \$R5 "0"[\s\S]*?ReadRegDWORD \$R5 HKCU') {
  throw 'legacy rollback profile fallback must initialize Hant before reading it'
}
$baseRegistryStart = $rollback.IndexOf('rollback_restore_base_registry:')
$runtimeRegistryStart = $rollback.IndexOf('rollback_restore_runtime_registry:', $baseRegistryStart)
$cleanupStart = $rollback.IndexOf('rollback_cleanup_temp:', $baseRegistryStart)
if ($baseRegistryStart -lt 0 -or $runtimeRegistryStart -lt 0 -or $cleanupStart -lt 0) {
  throw 'rollback must separate base registry recovery from runnable registration recovery'
}
$baseOnly = $rollback.Substring($baseRegistryStart, $runtimeRegistryStart - $baseRegistryStart)
if ($baseOnly -match 'CurrentVersion\\Run|Exec\s+''"\$PreviousInstallDir\\WeaselServer\.exe"''') {
  throw 'a failed program or registration restore must not publish autorun or start WeaselServer'
}
$runtimeRestore = $rollback.Substring($runtimeRegistryStart, $cleanupStart - $runtimeRegistryStart)
if ($runtimeRestore -notmatch 'CurrentVersion\\Run' -or
    $runtimeRestore -notmatch 'Exec\s+''"\$PreviousInstallDir\\WeaselServer\.exe"''') {
  throw 'a fully restored and registered rollback must still restore autorun and restart WeaselServer'
}

$werIndex = $uninstallFlow.IndexOf('const LSTATUS wer_deleted')
$userDeleteIndex = $uninstallFlow.IndexOf('const LSTATUS user_deleted')
$machineFailureGuard = $uninstallFlow.IndexOf('if (retval)', $werIndex)
if ($werIndex -lt 0 -or $machineFailureGuard -lt 0 -or $userDeleteIndex -lt 0 -or
    -not ($werIndex -lt $machineFailureGuard -and $machineFailureGuard -lt $userDeleteIndex)) {
  throw 'per-user PendingUninstall state must survive until all machine cleanup succeeds'
}

$freshInstallStart = $transaction.IndexOf('inline int InstallFreshTransactional')
if ($freshInstallStart -lt 0) { throw 'unable to isolate fresh install transaction' }
$freshInstall = $transaction.Substring($freshInstallStart)
if ($freshInstall -notmatch 'RegDeleteValueW\(user_handle, L"PendingUninstall"\)[\s\S]*?cleared\s*!=\s*ERROR_SUCCESS[\s\S]*?rollback\(\)[\s\S]*?return\s+1;') {
  throw 'fresh install must rollback when PendingUninstall cannot be cleared'
}
if ($imeSetup -notmatch 'static\s+bool\s+clear_pending_uninstall\(\)' -or
    $imeSetup -notmatch 'if\s*\(!clear_pending_uninstall\(\)\)\s*return\s+1;[\s\S]*?IDS_STR_INSTALL_SUCCESS_INFO') {
  throw 'existing install must not report success when PendingUninstall cannot be cleared'
}

Write-Host 'Setup transaction policy: PASS'
