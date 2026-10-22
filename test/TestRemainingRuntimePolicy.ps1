$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$serverImpl = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPCServer\WeaselServerImpl.cpp') -Raw
$rimeHandler = Get-Content -LiteralPath (Join-Path $repo 'RimeWithWeasel\RimeWithWeasel.cpp') -Raw
$ipc = Get-Content -LiteralPath (Join-Path $repo 'include\WeaselIPC.h') -Raw
$recoveryCoordinator = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\ServerRecoveryCoordinator.h') -Raw
$serverCommands = Get-Content -LiteralPath (Join-Path $repo 'test\TestServerCommands\TestServerCommands.cpp') -Raw
$imeSetup = Get-Content -LiteralPath (Join-Path $repo 'WeaselSetup\imesetup.cpp') -Raw
$installer = Get-Content -LiteralPath (Join-Path $repo 'output\install.nsi') -Raw

# Recovery launch arbitration belongs entirely to a DLL-safe background task.
if ($tsf -notmatch 'void\s+RunServerRecoveryOnce\(\)\s+noexcept') {
  throw 'TSF recovery must have one background worker implementation'
}
if ($tsf -notmatch 'void\s+RunServerRecoveryWorker\(\)\s+noexcept' -or
    $tsf -notmatch 'QueueDllBackgroundTask\(\[\]\(\)\s*\{\s*RunServerRecoveryWorker\(\);\s*\}\)') {
  throw 'TSF recovery must queue the bounded retry worker without carrying stale health epochs'
}
if ($ipc -notmatch 'class\s+ServerLifecycleLock' -or
    $tsf -notmatch 'ServerLifecycleLock\s+lifecycle_lock[\s\S]*?lifecycle_lock\.Acquire\(0\)') {
  throw 'background recovery must acquire the shared lifecycle lock nonblocking'
}
if ($tsf -notmatch 'OpenMutexW\(SYNCHRONIZE,\s*FALSE,\s*mutex_name\.c_str\(\)\)' -or
    $tsf -notmatch 'GetLegacyServerInstanceName\(\)') {
  throw 'server presence must cover current and legacy singleton mutexes'
}
if ($tsf -match 'CreateToolhelp32Snapshot|Process32First|Process32Next') {
  throw 'recovery must not poll the process table'
}
if ($tsf -notmatch 'kServerLaunchObservationTimeoutMs\s*=\s*2000') {
  throw 'background server launch observation must stay bounded'
}
if ($serverImpl -notmatch 'kServerStartupLifecycleWaitMs\s*=\s*2000' -or
    $serverImpl -match 'm_startupLifecycleLock\.Acquire\(INFINITE') {
  throw 'server startup lifecycle wait must remain bounded'
}
if ($recoveryCoordinator -notmatch 'worker_inflight_' -or
    $recoveryCoordinator -notmatch 'FinishBackoff\(' -or
    $recoveryCoordinator -notmatch 'FinishFailure\(' -or
    $recoveryCoordinator -notmatch 'FinishHealthy\(' -or
    $recoveryCoordinator -match 'health_epoch_') {
  throw 'recovery coordinator must remain a small process-local throttle without recovery epochs'
}
if ($recoveryCoordinator -notmatch 'health_revision_' -or
    $recoveryCoordinator -notmatch 'RunIfHealthUnchanged\(') {
  throw 'recovery coordinator must retain a monotonic foreground-health revision for destructive cancellation'
}
if ($serverCommands -notmatch 'test_server_recovery_coordinator_state_machine' -or
    $serverCommands -notmatch 'winners\s*==\s*1' -or
    $serverCommands -notmatch 'Backoff rejects early workers' -or
    $serverCommands -notmatch 'new recovery cannot bypass a published Backoff' -or
    $serverCommands -notmatch 'check\(!coordinator\.RunIfHealthUnchanged\(revision,' -or
    $serverCommands -notmatch '!destructive_action_ran') {
  throw 'TestServerCommands must dynamically exercise coordinator ownership and backoff semantics'
}
if ($rimeHandler -notmatch 'RimeWithWeaselHandler::AddSession[\s\S]*?if\s*\(m_disabled\)\s*return\s+0;' -or
    $rimeHandler -match 'RimeWithWeaselHandler::AddSession[\s\S]{0,300}?EndMaintenance\(\)') {
  throw 'START_SESSION must never end maintenance or reinitialize disabled librime'
}
if ($serverImpl -notmatch 'int\s+ServerImpl::Stop\(\)[\s\S]*?lock_guard\s+guard\(g_api_mutex\)[\s\S]*?_RequestStopUnderApiLock\(\)' -or
    $serverImpl -notmatch 'OnShutdownServer[\s\S]*?_RequestStopUnderApiLock\(\)') {
  throw 'server stop and maintenance transitions must share the API serialization boundary'
}
$listenerStart = $serverImpl.IndexOf('auto listener = [this]')
$listenerEnd = $serverImpl.IndexOf('pipeThread = std::make_unique<boost::thread>', $listenerStart)
if ($listenerStart -lt 0 -or $listenerEnd -le $listenerStart) {
  throw 'unable to isolate server pipe listener'
}
$listenerBody = $serverImpl.Substring($listenerStart, $listenerEnd - $listenerStart)
if ($listenerBody -notmatch 'ServerLifecycleLock\s+session_lifecycle' -or
    $listenerBody -notmatch 'session_lifecycle\.Acquire\(0\)' -or
    $listenerBody.IndexOf('session_lifecycle.Acquire(0)') -gt
        $listenerBody.IndexOf('std::lock_guard guard(g_api_mutex)') -or
    $serverImpl -notmatch 'OnStartSession[\s\S]*?_RuntimeLifecycle\(\)\s*!=\s*ServerLifecycle::Ready' -or
    $serverImpl -notmatch 'OnStartMaintenance[\s\S]*?_TryTransitionRuntimeLifecycle\(ServerLifecycle::Ready,[\s\S]*?ServerLifecycle::Maintenance\)' -or
    $serverImpl -notmatch 'OnEndMaintenance[\s\S]*?_TryTransitionRuntimeLifecycle\(ServerLifecycle::Maintenance,[\s\S]*?ServerLifecycle::Ready\)') {
  throw 'server command admission must enforce lifecycle-before-api lock order and monotonic lifecycle transitions'
}

if ($ipc -notmatch 'GetServerLifecycleMutexName\(\)' -or
    $ipc -notmatch 'GetServerRuntimeStateName\(\)' -or
    $ipc -notmatch 'enum\s+class\s+ServerLifecycle' -or
    $serverImpl -notmatch 'm_startupLifecycleLock\.Acquire\(kServerStartupLifecycleWaitMs' -or
    $serverImpl -notmatch '_PublishRuntimeState\(\)' -or
    $tsf -notmatch 'ReadServerRuntimeState\(') {
  throw 'server lifecycle correctness must use one mutex and one runtime state mapping'
}
if ($ipc -match 'GetServerReadyEventName|ServerRuntimeIdentity|GetServerRuntimeIdentityName') {
  throw 'recovery must not retain duplicated ready-event or seqlock identity state'
}
if ($tsf -notmatch 'kServerUnavailableRestartThresholdMs\s*=\s*1500' -or
    $tsf -notmatch 'kServerForceTerminateThresholdMs\s*=\s*5000' -or
    $tsf -notmatch 'StopServerResult\s+StopPersistentlyUnavailableServer\(') {
  throw 'persistent live-server failure must escalate through bounded restart thresholds'
}
if ($tsf -notmatch 'ProbeServerResponsive\(\)[\s\S]*?kServerLivenessProbeTimeoutMs[\s\S]*?ProbeServer\(remaining\)' -or
    $tsf -notmatch 'if\s*\(ProbeServerResponsive\(\)\)[\s\S]*?FinishServerRecoveryHealthy\(\)' -or
    $tsf -notmatch 'StopPersistentlyUnavailableServer[\s\S]*?if\s*\(ProbeServerResponsive\(\)\)[\s\S]*?RunIfHealthUnchanged\([\s\S]*?PostMessageW\(target\.window,\s*WM_CLOSE') {
  throw 'restart escalation must re-probe before linearizing the WM_CLOSE decision'
}
if (([regex]::Matches($tsf, 'IsServerInstancePresent\(instance_name,\s*legacy_instance_name\)')).Count -lt 2) {
  throw 'recovery launch must preserve singleton observation around lifecycle ownership'
}
if ($recoveryCoordinator -notmatch 'retry_after_' -or
    $recoveryCoordinator -notmatch 'RetryAfter\(\)' -or
    $tsf -notmatch 'kServerRecoveryWorkerRetryBudgetMs\s*=\s*2500' -or
    $tsf -notmatch 'RunServerRecoveryWorker\(\)[\s\S]*?for\s*\(;;\)[\s\S]*?RetryAfter\(\)[\s\S]*?Sleep\([\s\S]*?TryBeginServerRecovery\(\)[\s\S]*?WorkerInflight\(\)') {
  throw 'recovery backoff must drive one bounded background self-retry lease'
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
