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

if ($ipc -notmatch 'kServerLifecycleTickMarker\s*=\s*0x80000000u' -or
    $ipc -notmatch 'ServerLifecycleElapsedAtLeast\s*\(' -or
    $serverImpl -notmatch '_SetRuntimeLifecycle[\s\S]*?EncodeServerLifecycleTick\(GetTickCount\(\)\)[\s\S]*?InterlockedExchange' -or
    $serverImpl -notmatch '_TryTransitionRuntimeLifecycle[\s\S]*?EncodeServerLifecycleTick\(GetTickCount\(\)\)[\s\S]*?InterlockedCompareExchange') {
  throw 'server runtime state must publish a backward-compatible monotonic lifecycle-start tick before lifecycle changes'
}
if ($tsf -notmatch 'kServerStartingHardTimeoutMs\s*=\s*30\s*\*\s*1000' -or
    $tsf -notmatch 'kServerMaintenanceHardTimeoutMs\s*=\s*15\s*\*\s*60\s*\*\s*1000' -or
    $tsf -notmatch 'kServerStoppingHardTimeoutMs\s*=\s*30\s*\*\s*1000' -or
    $tsf -notmatch 'NonReadyLifecycleHardTimeoutExpired\s*\(') {
  throw 'non-Ready recovery must bound Starting/Stopping while preserving the deploy maintenance deadline'
}
$recoveryOnceStart = $tsf.IndexOf('void RunServerRecoveryOnce() noexcept')
$recoveryWorkerStart = $tsf.IndexOf('void RunServerRecoveryWorker() noexcept', $recoveryOnceStart)
if ($recoveryOnceStart -lt 0 -or $recoveryWorkerStart -le $recoveryOnceStart) {
  throw 'unable to isolate RunServerRecoveryOnce for non-Ready recovery policy'
}
$recoveryOnce = $tsf.Substring($recoveryOnceStart, $recoveryWorkerStart - $recoveryOnceStart)
$busyStart = $recoveryOnce.IndexOf('if (!lifecycle_lock.Acquire(0))')
$busyEnd = $recoveryOnce.IndexOf('const bool server_present', $busyStart)
if ($busyStart -lt 0 -or $busyEnd -le $busyStart) {
  throw 'unable to isolate lifecycle-busy recovery branch'
}
$busyRecovery = $recoveryOnce.Substring($busyStart, $busyEnd - $busyStart)
if ($busyRecovery -notmatch 'ServerLifecycle::Starting' -or
    $busyRecovery -notmatch 'NonReadyLifecycleHardTimeoutExpired\(state\)' -or
    $busyRecovery -notmatch 'StopPersistentlyUnavailableServer\([\s\S]*?ServerLifecycle::Starting,[\s\S]*?false\)' -or
    $busyRecovery -match 'ServerLifecycle::Maintenance') {
  throw 'lifecycle-busy recovery may bypass ownership only for a hard-timed-out Starting server'
}
$ownedNonReadyStart = $recoveryOnce.IndexOf('if (state.lifecycle !=', $busyEnd)
$ownedNonReadyEnd = $recoveryOnce.IndexOf('} else {', $ownedNonReadyStart)
if ($ownedNonReadyStart -lt 0 -or $ownedNonReadyEnd -le $ownedNonReadyStart) {
  throw 'unable to isolate lifecycle-owned non-Ready recovery branch'
}
$ownedNonReady = $recoveryOnce.Substring(
  $ownedNonReadyStart,
  $ownedNonReadyEnd - $ownedNonReadyStart
)
$deadlineCheck = $ownedNonReady.IndexOf('NonReadyLifecycleHardTimeoutExpired(state)')
$expectedLifecycle = $ownedNonReady.IndexOf('const auto expected_lifecycle')
$authenticatedStop = $ownedNonReady.IndexOf('StopPersistentlyUnavailableServer(')
if ($ownedNonReady -notmatch 'ServerLifecycle::Ready' -or
    $deadlineCheck -lt 0 -or $expectedLifecycle -lt 0 -or $authenticatedStop -lt 0 -or
    -not ($deadlineCheck -lt $expectedLifecycle -and $expectedLifecycle -lt $authenticatedStop) -or
    $ownedNonReady -notmatch 'state,\s*health_revision,\s*expected_lifecycle,\s*false') {
  throw 'once lifecycle ownership is acquired timed-out non-Ready generations must use the authenticated stop path'
}
if ($tsf -notmatch 'latest_state\.lifecycle\s*!=\s*static_cast<LONG>\(expected_lifecycle\)[\s\S]*?!probe_liveness[\s\S]*?!NonReadyLifecycleHardTimeoutExpired\(latest_state\)') {
  throw 'destructive non-Ready recovery must revalidate generation lifecycle and hard deadline at the commit point'
}
if ($serverCommands -notmatch 'test_server_lifecycle_tick_deadline\(\)' -or
    $serverCommands -notmatch 'legacy runtime states without the tick marker never hard-timeout' -or
    $serverCommands -notmatch 'lifecycle deadline arithmetic survives the 31-bit tick wrap') {
  throw 'TestServerCommands must dynamically cover lifecycle deadline threshold wrap and legacy compatibility'
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
$stopStart = $serverImpl.IndexOf('int ServerImpl::Stop()')
$runStart = $serverImpl.IndexOf('int ServerImpl::Run()', $stopStart)
if ($stopStart -lt 0 -or $runStart -le $stopStart) {
  throw 'unable to isolate ServerImpl::Stop'
}
$stopBody = $serverImpl.Substring($stopStart, $runStart - $stopStart)
if ($stopBody -match 'g_api_mutex' -or
    $stopBody -notmatch '_RequestStop\(\)' -or
    $serverImpl -notmatch 'OnShutdownServer[\s\S]*?_RequestStop\(\)') {
  throw 'server stop must remain independent of the Rime API serialization lock'
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

$lifecycleUnlock = $listenerBody.IndexOf('session_lifecycle.Unlock()')
$responseWrite = $listenerBody.IndexOf('resp(result)')
if ($lifecycleUnlock -lt 0 -or $responseWrite -lt 0 -or
    $lifecycleUnlock -gt $responseWrite -or
    $listenerBody -notmatch 'starting\s*&&\s*lifecycle_admitted[\s\S]*?session_lifecycle\.Unlock\(\)') {
  throw 'START_SESSION lifecycle ownership must end after admission commit and before the synchronous response write'
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
    $tsf -notmatch 'StopPersistentlyUnavailableServer[\s\S]*?if\s*\(probe_liveness\)[\s\S]*?ProbeServerResponsive\(\)[\s\S]*?RunIfHealthUnchanged\([\s\S]*?PostMessageW\(current_window,\s*WM_CLOSE') {
  throw 'Ready-state restart escalation must re-probe before linearizing the WM_CLOSE decision'
}
if ($tsf -notmatch 'target\.window\s*=\s*FindServerWindowForProcess\(state\.process_id\);[\s\S]*?return\s+true;' -or
    $tsf -notmatch 'forced_without_window[\s\S]*?RunIfHealthUnchanged\([\s\S]*?SameServerGeneration\(latest_state,\s*expected_state\)[\s\S]*?NonReadyLifecycleHardTimeoutExpired\(latest_state\)[\s\S]*?FindServerWindowForProcess\(expected_state\.process_id\)[\s\S]*?PostMessageW\(current_window,\s*WM_CLOSE[\s\S]*?SameServerGeneration\(kill_state,\s*expected_state\)[\s\S]*?NonReadyLifecycleHardTimeoutExpired\(kill_state\)[\s\S]*?TerminateProcess\(target\.process,\s*ERROR_TIMEOUT\)') {
  throw 'timed-out non-Ready recovery must recheck the window generation and deadline immediately before direct termination'
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
