$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$imeSetup = Get-Content -LiteralPath (Join-Path $repo 'WeaselSetup\imesetup.cpp') -Raw
$installer = Get-Content -LiteralPath (Join-Path $repo 'output\install.nsi') -Raw

# Recovery launch arbitration belongs entirely to a DLL-safe background task.
if ($tsf -notmatch 'void\s+RunServerRecoveryOnce\(\)\s+noexcept') {
  throw 'TSF recovery must have a single background recovery worker'
}
if ($tsf -notmatch 'CreateMutexW\(nullptr,\s*FALSE,\s*launch_mutex_name\.c_str\(\)\)') {
  throw 'background recovery must create the launch mutex unowned'
}
if ($tsf -notmatch 'WaitForSingleObject\(launch_mutex\.handle,\s*0\)') {
  throw 'cross-process launch arbitration must be nonblocking'
}
if ($tsf -notmatch 'ReleaseMutex\(handle\)') {
  throw 'launch mutex guard must release ownership after the launch window'
}
if ($tsf -notmatch 'OpenMutexW\(SYNCHRONIZE,\s*FALSE,\s*mutex_name\.c_str\(\)\)' -or
    $tsf -notmatch 'GetLegacyServerInstanceName\(\)') {
  throw 'server presence must cover current and legacy singleton mutexes'
}
if ($tsf -match 'CreateToolhelp32Snapshot|Process32First|Process32Next') {
  throw 'recovery must not poll the process table'
}
if ($tsf -notmatch 'kServerExitObservationTimeoutMs\s*=\s*250' -or
    $tsf -notmatch 'kServerLaunchQuickObservationTimeoutMs\s*=\s*500' -or
    $tsf -notmatch 'kServerLaunchLeaseTimeoutMs\s*=\s*2000') {
  throw 'background transition observation and cross-process launch lease must stay bounded'
}
if ($tsf -notmatch 'remaining_launch_lease\s*=\s*kServerLaunchLeaseTimeoutMs\s*-\s*kServerLaunchQuickObservationTimeoutMs') {
  throw 'slow startup must retain the original launch mutex for the remainder of the lease'
}
if ($tsf -notmatch 'PublishServerRecoveryBackoff\([\s\S]*?InterlockedCompareExchange\(&g_serverRecoveryState,\s*backoff,\s*busy\)') {
  throw 'recovery Backoff must be published only by the Busy owner'
}
if ($tsf -notmatch 'NoteServerRecoveryHealthy\(\)[\s\S]*?InterlockedIncrement\(&g_serverRecoveryHealthEpoch\)[\s\S]*?ResetServerUnavailableObservation\(\)') {
  throw 'successful cold recovery must invalidate stale failure and unavailability epochs'
}
if ($tsf -notmatch 'kServerUnavailableRestartThresholdMs\s*=\s*1500' -or
    $tsf -notmatch 'kServerForceTerminateThresholdMs\s*=\s*5000' -or
    $tsf -notmatch 'StopPersistentlyUnavailableServer\(\)') {
  throw 'persistent live-server failure must escalate through bounded restart thresholds'
}
if ($tsf -notmatch 'ProbeServerResponsive\(\)[\s\S]*?kServerLivenessProbeTimeoutMs[\s\S]*?ProbeServer\(remaining\)' -or
    $tsf -notmatch 'if\s*\(ProbeServerResponsive\(\)\)[\s\S]*?ResetServerUnavailableObservation\(\)') {
  throw 'restart escalation must re-probe server IPC health after acquiring launch ownership'
}
if (([regex]::Matches($tsf, 'IsServerInstancePresent\(instance_name,\s*legacy_instance_name\)')).Count -lt 2) {
  throw 'launcher must double-check server presence across launch mutex acquisition'
}
if ($tsf -notmatch 'g_serverRecoveryRetryAfter' -or
    $tsf -match 'kServerRecoveryRetryCooldownMs') {
  throw 'recovery backoff must use a deadline instead of sleeping a cooldown worker'
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
