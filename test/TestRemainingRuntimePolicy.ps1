$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$imeSetup = Get-Content -LiteralPath (Join-Path $repo 'WeaselSetup\imesetup.cpp') -Raw
$installer = Get-Content -LiteralPath (Join-Path $repo 'output\install.nsi') -Raw

# Server recovery serialization must be owned by the detached launcher thread,
# so the mutex stays held until the launched server becomes observable.
if ($tsf -notmatch 'launch_server_serialized\s*\(') {
  throw 'TSF recovery must use a serialized launcher helper'
}
if ($tsf -notmatch 'CreateMutex\(NULL,\s*FALSE,\s*mutex_name\.c_str\(\)\)') {
  throw 'launcher mutex must be created unowned and acquired by the launcher thread'
}
if ($tsf -notmatch 'WaitForSingleObject\(launch_mutex,\s*0\)') {
  throw 'launcher thread must acquire the launch mutex without blocking TSF input'
}
if ($tsf -notmatch 'ReleaseMutex\(launch_mutex\)') {
  throw 'launcher thread must release the launch mutex after launch arbitration'
}
if ($tsf -notmatch 'count_server_process_in_current_session\(\)\s*>\s*0') {
  throw 'launcher mutex must be held until a server process becomes observable'
}
if ($tsf -notmatch 'GetTickCount64\(\)[\s\S]*?kServerLaunchObservationTimeoutMs') {
  throw 'launcher observation wait must be bounded'
}
if ($tsf -match 'CreateMutex\(NULL,\s*TRUE,\s*launch_mutex_name\.c_str\(\)\)') {
  throw 'TSF input thread must not briefly own and immediately release the launch mutex'
}

# Uninstall must surface profile/layout/file/registry cleanup failures.
if ($imeSetup -notmatch 'HRESULT\s+enable_profile\s*\(') {
  throw 'profile registration helper must return HRESULT instead of swallowing failures'
}
if ($imeSetup -notmatch 'if\s*\(!delete_file\([^\)]*\)\)\s*\+\+retval') {
  throw 'system DLL deletion failures must contribute to uninstall failure'
}
if ($imeSetup -notmatch 'FAILED\(install_layout_or_tip\(profile,\s*LayoutOrTipAction::Uninstall\)\)') {
  throw 'layout removal failure must contribute to uninstall failure'
}
if ($imeSetup -notmatch 'RegDeleteKeyEx\(HKEY_LOCAL_MACHINE,\s*WEASEL_WER_KEY[\s\S]*?ERROR_FILE_NOT_FOUND') {
  throw 'WER registry cleanup failure must be checked while missing keys remain harmless'
}
if ($imeSetup -notmatch 'if\s*\(retval\)\s*return\s+1;[\s\S]*?delete_reg_tree') {
  throw 'critical TSF/layout/file failures must stop before install metadata is destroyed'
}
if ($installer -notmatch 'ExecWait\s+''"\$INSTDIR\\WeaselSetup\.exe" /u''\s+\$R0[\s\S]*?\$R0\s*!=\s*0[\s\S]*?Abort') {
  throw 'NSIS uninstaller must stop when WeaselSetup /u reports failure'
}

Write-Host 'Remaining runtime hardening policy: PASS'
