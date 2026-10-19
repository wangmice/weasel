$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$tsfHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.h') -Raw
$keySink = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\KeyEventSink.cpp') -Raw
$client = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\WeaselClientImpl.cpp') -Raw
$pipe = Get-Content -LiteralPath (Join-Path $repo 'include\PipeChannel.h') -Raw
$ipc = Get-Content -LiteralPath (Join-Path $repo 'include\WeaselIPC.h') -Raw
$serverImpl = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPCServer\WeaselServerImpl.cpp') -Raw
$recoveryCoordinator = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\ServerRecoveryCoordinator.h') -Raw

$scheduleStart = $tsf.IndexOf('void WeaselTSF::_ScheduleServerRecovery()')
$untilStart = $tsf.IndexOf('bool WeaselTSF::_EnsureServerConnectedUntil(', $scheduleStart)
$ensureStart = $tsf.IndexOf('bool WeaselTSF::_EnsureServerConnected()', $untilStart)
if ($scheduleStart -lt 0 -or $untilStart -le $scheduleStart -or $ensureStart -le $untilStart) {
  throw 'unable to isolate recovery scheduler and bounded reconnect paths'
}
$schedule = $tsf.Substring($scheduleStart, $untilStart - $scheduleStart)
$until = $tsf.Substring($untilStart, $ensureStart - $untilStart)
$ensure = $tsf.Substring($ensureStart)

if ($tsfHeader -notmatch 'kKeyEventTransactionTimeoutMs\s*=\s*100') {
  throw 'healthy physical-key transaction timeout must remain 100 ms'
}
if ($tsfHeader -notmatch 'kFastSessionRecoveryBudgetMs\s*=\s*20') {
  throw 'synchronous recovery budget must remain 20 ms'
}
if ($ensure -notmatch 'if\s*\(m_client\.IsSessionActive\(\)\)\s*return true;') {
  throw 'healthy connection check must return before recovery work'
}
$fastReturn = $ensure.IndexOf('return true;')
$clockRead = $ensure.IndexOf('GetTickCount64()')
if ($fastReturn -lt 0 -or $clockRead -le $fastReturn) {
  throw 'healthy connection check must precede the first recovery clock read'
}
$fastPath = $ensure.Substring(0, $fastReturn + 'return true;'.Length)
if ($fastPath -match 'GetTickCount64|Sleep|Interlocked|QueueDllBackgroundTask|RegGetStringValue|OpenMutex') {
  throw 'healthy connection path must not touch recovery machinery'
}

if ($schedule -notmatch 'TryBeginServerRecovery\(\)[\s\S]*?QueueDllBackgroundTask\s*\(\[\]\(\)\s*\{\s*RunServerRecoveryWorker\(\);\s*\}\)') {
  throw 'failed recovery must be deduplicated before queueing one DLL-safe recovery worker'
}
if ($schedule -match 'RegGetStringValue|GetServerInstanceName|GetServerLifecycleMutexName|OpenMutex|CreateMutex|ShellExecute|Sleep') {
  throw 'TSF callback-side scheduler must not perform server discovery or launch work'
}

if ($tsf -match 'GetWeaselRegName\(') {
  throw 'background recovery must not reference the undefined GetWeaselRegName helper'
}
if ($tsf -notmatch 'RegGetStringValue\(HKEY_LOCAL_MACHINE,\s*WEASEL_REG_KEY,\s*L"WeaselRoot"') {
  throw 'background recovery must read WeaselRoot from the canonical HKLM Weasel registry key'
}
if ($recoveryCoordinator -notmatch 'class\s+ServerRecoveryCoordinator' -or
    $recoveryCoordinator -notmatch 'SRWLOCK\s+gate_' -or
    $recoveryCoordinator -notmatch 'TryBegin\(ULONGLONG\s+now\)[\s\S]*?ExclusiveGuard\s+guard\(gate_\)[\s\S]*?worker_inflight_[\s\S]*?retry_after_' -or
    $recoveryCoordinator -notmatch 'NoteHealthy\(\)[\s\S]*?ExclusiveGuard\s+guard\(gate_\)[\s\S]*?health_revision_' -or
    $recoveryCoordinator -notmatch 'FinishBackoff\(' -or
    $recoveryCoordinator -notmatch 'FinishFailure\(' -or
    $recoveryCoordinator -notmatch 'FinishHealthy\(') {
  throw 'server recovery coordinator must remain a small worker/backoff throttle'
}
if ($recoveryCoordinator -match 'health_epoch_|enum\s+class\s+State|InterlockedCompareExchange\(&worker_inflight_') {
  throw 'recovery throttling must not grow a second lifecycle or hand-rolled atomic state machine'
}
if ($recoveryCoordinator -notmatch 'HealthRevision\(\)' -or
    $recoveryCoordinator -notmatch 'RunIfHealthUnchanged\(' -or
    $tsf -notmatch 'ProbeServerResponsive\(\)[\s\S]*?RunIfHealthUnchanged\([\s\S]*?PostMessageW\(target\.window,\s*WM_CLOSE') {
  throw 'foreground health must revoke stale recovery before the WM_CLOSE commit point'
}
if ($tsf -notmatch 'ServerRecoveryCoordinator\s+g_serverRecovery' -or
    $tsf -notmatch 'g_serverRecovery\.TryBegin\(GetTickCount64\(\)\)' -or
    $tsf -notmatch 'g_serverRecovery\.FinishBackoff\(delay_ms,\s*GetTickCount64\(\)\)') {
  throw 'production recovery must use the testable coordinator implementation'
}
if ($tsf -notmatch 'kServerRecoveryWorkerRetryBudgetMs\s*=\s*2500' -or
    $tsf -notmatch 'RunServerRecoveryWorker\(\)[\s\S]*?RetryAfter\(\)[\s\S]*?Sleep\([\s\S]*?TryBeginServerRecovery\(\)') {
  throw 'background recovery must consume coordinator backoff within one bounded self-retry budget'
}
if ($tsf -notmatch 'OpenMutexW\(SYNCHRONIZE,\s*FALSE,\s*mutex_name\.c_str\(\)\)') {
  throw 'background recovery must observe named server singleton mutexes'
}
if ($tsf -notmatch 'GetLegacyServerInstanceName\(\)') {
  throw 'background recovery must preserve the pre-session-scoping server singleton during upgrades'
}
if ($tsf -match 'CreateToolhelp32Snapshot') {
  throw 'background recovery must not poll the full process table'
}
if ($tsf -notmatch 'kServerLaunchObservationTimeoutMs\s*=\s*2000') {
  throw 'server launch observation must stay bounded'
}
if ($ipc -notmatch 'class\s+ServerLifecycleLock' -or
    $ipc -notmatch 'GetServerLifecycleMutexName\(\)' -or
    $ipc -notmatch 'struct\s+ServerRuntimeState[\s\S]*?process_id[\s\S]*?lifecycle[\s\S]*?process_creation_time' -or
    $ipc -notmatch 'enum\s+class\s+ServerLifecycle[\s\S]*?Starting[\s\S]*?Ready[\s\S]*?Maintenance[\s\S]*?Stopping') {
  throw 'cross-process recovery must use one lifecycle lock and one runtime state'
}
if ($ipc -match 'GetServerReadyEventName|ServerRuntimeIdentity|GetServerRuntimeIdentityName') {
  throw 'ready events and seqlock identity must not duplicate runtime lifecycle state'
}
if ($serverImpl -notmatch 'int\s+ServerImpl::Stop\(\)[\s\S]*?lock_guard\s+guard\(g_api_mutex\)[\s\S]*?_RequestStopUnderApiLock\(\)' -or
    $serverImpl -notmatch 'OnShutdownServer[\s\S]*?_RequestStopUnderApiLock\(\)' -or
    $serverImpl -notmatch '_Module\.RemoveMessageLoop\(\)[\s\S]*?lock_guard\s+guard\(g_api_mutex\)[\s\S]*?ServerLifecycle::Stopping') {
  throw 'server stop publication must serialize with maintenance handlers through g_api_mutex'
}
$listenerStart = $serverImpl.IndexOf('auto listener = [this]')
$listenerEnd = $serverImpl.IndexOf('pipeThread = std::make_unique<boost::thread>', $listenerStart)
if ($listenerStart -lt 0 -or $listenerEnd -le $listenerStart) {
  throw 'unable to isolate server pipe listener'
}
$listenerBody = $serverImpl.Substring($listenerStart, $listenerEnd - $listenerStart)
$lifecycleAcquire = $listenerBody.IndexOf('session_lifecycle.Acquire(0)')
$apiAcquire = $listenerBody.IndexOf('std::lock_guard guard(g_api_mutex)')
if ($lifecycleAcquire -lt 0 -or $apiAcquire -lt 0 -or $lifecycleAcquire -gt $apiAcquire) {
  throw 'START_SESSION must acquire lifecycle ownership before g_api_mutex to preserve global lock order'
}
if ($serverImpl -notmatch 'm_startupLifecycleLock\.Acquire\(INFINITE,\s*sa\.get_attr\(\)\)' -or
    $serverImpl -notmatch '_PublishRuntimeState\(\)' -or
    $serverImpl -notmatch 'ServerLifecycle::Starting' -or
    $serverImpl -notmatch 'WaitForPrimaryPipeReady\(\)[\s\S]*?ServerLifecycle::Ready[\s\S]*?_UnlockStartupLifecycleLock\(\)' -or
    $serverImpl -notmatch 'OnStartSession[\s\S]*?_RuntimeLifecycle\(\)\s*!=\s*ServerLifecycle::Ready' -or
    $serverImpl -notmatch 'OnStartMaintenance[\s\S]*?_TryTransitionRuntimeLifecycle\(ServerLifecycle::Ready,[\s\S]*?ServerLifecycle::Maintenance\)[\s\S]*?StartMaintenance\(\)' -or
    $serverImpl -notmatch 'OnEndMaintenance[\s\S]*?_RuntimeLifecycle\(\)\s*!=\s*ServerLifecycle::Maintenance[\s\S]*?EndMaintenance\(\)[\s\S]*?_TryTransitionRuntimeLifecycle\(ServerLifecycle::Maintenance,[\s\S]*?ServerLifecycle::Ready\)') {
  throw 'server must own lifecycle from singleton publication through Ready and publish maintenance transitions'
}
$workerStart = $tsf.IndexOf('void RunServerRecoveryOnce()')
$workerEnd = $tsf.IndexOf('#endif', $workerStart)
if ($workerStart -lt 0 -or $workerEnd -le $workerStart) {
  throw 'unable to isolate server recovery worker'
}
$workerBody = $tsf.Substring($workerStart, $workerEnd - $workerStart)
if ($workerBody -notmatch 'ServerLifecycleLock\s+lifecycle_lock' -or
    $workerBody -notmatch 'lifecycle_lock\.Acquire\(0\)' -or
    $workerBody -notmatch 'ReadServerRuntimeState\(' -or
    $workerBody -notmatch 'ServerLifecycle::Ready' -or
    $workerBody -notmatch 'ProbeServerResponsive\(\)' -or
    $workerBody -notmatch 'StopPersistentlyUnavailableServer' -or
    $workerBody -notmatch 'ShellExecuteW') {
  throw 'recovery probe stop and launch decisions must stay under lifecycle ownership'
}
if ($tsf -notmatch 'kServerUnavailableRestartThresholdMs\s*=\s*1500' -or
    $tsf -notmatch 'ObserveServerUnavailableLongEnough\(\)') {
  throw 'live-server restart must require sustained unavailability'
}
$stopStart = $tsf.IndexOf('StopServerResult StopPersistentlyUnavailableServer(')
$stopEnd = $tsf.IndexOf('void RunServerRecoveryOnce()', $stopStart)
if ($stopStart -lt 0 -or $stopEnd -le $stopStart) {
  throw 'unable to isolate StopPersistentlyUnavailableServer'
}
$stopBody = $tsf.Substring($stopStart, $stopEnd - $stopStart)
if ($stopBody -notmatch 'ProbeServerResponsive\(\)[\s\S]*?PostMessageW\(target\.window,\s*WM_CLOSE' -or
    $stopBody -notmatch 'kServerForceTerminateThresholdMs' -or
    $stopBody -notmatch 'TerminateProcess\(target\.process') {
  throw 'persistent live-server recovery must try graceful shutdown before bounded forced termination'
}
$stateReadStart = $tsf.IndexOf('bool ReadServerRuntimeState(')
$stateReadEnd = $tsf.IndexOf('bool SameServerGeneration(', $stateReadStart)
if ($stateReadStart -lt 0 -or $stateReadEnd -le $stateReadStart) {
  throw 'unable to isolate ReadServerRuntimeState'
}
$stateReadBody = $tsf.Substring($stateReadStart, $stateReadEnd - $stateReadStart)
if ($stateReadBody -notmatch 'OpenFileMappingW' -or
    $stateReadBody -notmatch 'GetServerRuntimeStateName' -or
    $stateReadBody -notmatch 'process_id' -or
    $stateReadBody -notmatch 'lifecycle' -or
    $stateReadBody -notmatch 'process_creation_time') {
  throw 'runtime state must expose identity and lifecycle through one mapping'
}
$targetStart = $tsf.IndexOf('bool OpenCurrentSessionServerTarget(')
$targetEnd = $tsf.IndexOf('bool ProbeServerResponsive()', $targetStart)
if ($targetStart -lt 0 -or $targetEnd -le $targetStart) {
  throw 'unable to isolate OpenCurrentSessionServerTarget'
}
$targetBody = $tsf.Substring($targetStart, $targetEnd - $targetStart)
if ($targetBody -notmatch 'OpenProcess[\s\S]*?state\.process_id' -or
    $targetBody -notmatch 'ProcessIdToSessionId\(state\.process_id' -or
    $targetBody -notmatch 'GetProcessTimes\(target\.process' -or
    $targetBody -notmatch 'creation\.QuadPart\s*!=\s*state\.process_creation_time' -or
    $targetBody -notmatch 'QueryFullProcessImageNameW\(target\.process' -or
    $targetBody -notmatch 'WeaselServer\.exe' -or
    $targetBody -notmatch 'FindServerWindowForProcess\(state\.process_id\)') {
  throw 'destructive recovery must target the exact published server generation'
}
if ($tsf -notmatch 'SameServerGeneration\(current_state,\s*expected_state\)' -or
    $tsf -notmatch 'SameServerGeneration\(latest_state,\s*state\)') {
  throw 'server generation must be revalidated before restart commitment while the process handle pins the termination target'
}
if ($tsf -notmatch 'kServerLivenessProbeTimeoutMs\s*=\s*100' -or
    $tsf -notmatch 'ProbeServerResponsive\(\)[\s\S]*?Connect\(kServerLivenessProbeTimeoutMs\)[\s\S]*?RemainingRecoveryTime\(deadline\)[\s\S]*?ProbeServer\(remaining\)') {
  throw 'persistent restart must perform a fresh bounded side-effect-free IPC liveness probe'
}
if ($ipc -notmatch 'ProbeServer\(DWORD\s+timeout_ms\)') {
  throw 'liveness probe API must expose a bounded timeout'
}
$probeStart = $client.IndexOf('bool ClientImpl::ProbeServer(DWORD timeout_ms)')
$probeEnd = $client.IndexOf('bool ClientImpl::GetResponseData', $probeStart)
if ($probeStart -lt 0 -or $probeEnd -le $probeStart) {
  throw 'unable to isolate ClientImpl::ProbeServer'
}
$probeBody = $client.Substring($probeStart, $probeEnd - $probeStart)
if ($probeBody -notmatch 'PipeMessage\s+req\{WEASEL_IPC_ECHO,\s*0,\s*0\}' -or
    $probeBody -notmatch 'channel\.Transact\(req,\s*timeout_ms\)') {
  throw 'liveness probe must use ECHO session 0 without creating a logical session'
}
if ($probeBody -match 'StartSession|EndSession|_SetSessionId|_InvalidateLogicalSession') {
  throw 'liveness probe must not create, destroy, or activate a Rime session'
}
if ($tsf -notmatch 'if\s*\(ProbeServerResponsive\(\)\)[\s\S]*?FinishServerRecoveryHealthy\(\)') {
  throw 'a healthy fresh probe must finish recovery and clear stale unavailable history'
}
if (([regex]::Matches($tsf, 'IsServerInstancePresent\(instance_name,\s*legacy_instance_name\)')).Count -lt 2) {
  throw 'launcher must double-check the server singleton around launch arbitration'
}

if ($until -notmatch 'm_client\.Connect\(remaining\)[\s\S]*?m_client\.StartSession\(remaining\)') {
  throw 'bounded reconnect must share one absolute deadline across Connect and StartSession'
}
if ($until -notmatch '_ScheduleServerRecovery\(\)') {
  throw 'failed bounded reconnect must hand server startup to the background coordinator'
}
if (([regex]::Matches($until, 'NoteServerRecoveryHealthy\(\)')).Count -lt 2) {
  throw 'successful cold reconnects must report health to the recovery throttle'
}
if ($tsf -notmatch 'NoteServerRecoveryHealthy\(\)[\s\S]*?g_serverRecovery\.NoteHealthy\(\)' -or
    $recoveryCoordinator -notmatch 'NoteHealthy\(\)[\s\S]*?ExclusiveGuard\s+guard\(gate_\)[\s\S]*?\+\+health_revision_[\s\S]*?if\s*\(worker_inflight_\)[\s\S]*?return;[\s\S]*?failure_streak_\s*=\s*0[\s\S]*?unavailable_since_\s*=\s*0[\s\S]*?retry_after_\s*=\s*0') {
  throw 'foreground health must advance revision even while a recovery worker owns admission'
}
if ($keySink -notmatch '_ProcessKeyEventWithRecovery\([\s\S]*?ULONGLONG\s+recovery_deadline') {
  throw 'physical-key recovery must carry a shared deadline'
}
if ($keySink -notmatch 'if\s*\(!m_client\.IsSessionActive\(\)\)[\s\S]*?recovery_deadline\s*=[\s\S]*?kFastSessionRecoveryBudgetMs[\s\S]*?_EnsureServerConnectedUntil\(recovery_deadline\)') {
  throw 'inactive-session first-key recovery must allocate its deadline only on the failure path'
}
if ($keySink -notmatch '_ProcessKeyEventWithRecovery\(ke,\s*recovery_deadline\)') {
  throw 'the original first-key deadline must flow into transport/session retry logic'
}
if ($keySink -notmatch 'InvalidSession[\s\S]*?remaining_recovery_time\(deadline\)[\s\S]*?StartSession\(remaining\)') {
  throw 'invalid-session rebuild must consume only the remaining physical-key budget'
}

$disconnectStart = $client.IndexOf('void ClientImpl::Disconnect()')
$shutdownStart = $client.IndexOf('void ClientImpl::ShutdownServer()', $disconnectStart)
if ($disconnectStart -lt 0 -or $shutdownStart -le $disconnectStart) {
  throw 'unable to isolate ClientImpl::Disconnect'
}
$disconnectBody = $client.Substring($disconnectStart, $shutdownStart - $disconnectStart)
if ($disconnectBody -match 'channel\.Disconnect\(\);\s*_RetireSessionKey\(\)') {
  throw 'inactive transport disconnect must preserve SessionKey for START retry'
}
if ($disconnectBody -notmatch 'if\s*\(_Active\(\)\)[\s\S]*?EndSession\(\)[\s\S]*?channel\.Disconnect\(\)') {
  throw 'active Disconnect must still end the established logical session'
}
if ($ipc -notmatch 'TransportNotDelivered') {
  throw 'key result must distinguish a definitely-unsent request'
}
if ($pipe -notmatch 'class\s+PipeTransactionError[\s\S]*?request_may_have_been_delivered') {
  throw 'pipe transaction failure must carry delivery ambiguity'
}
if ($client -notmatch 'catch\s*\(const\s+PipeTransactionError&[\s\S]*?request_may_have_been_delivered[\s\S]*?TransportNotDelivered') {
  throw 'client must map definitely-unsent transport failures to TransportNotDelivered'
}
if ($keySink -notmatch 'process_key_bounded[\s\S]*?ProcessKeyEventEx\(keyEvent,[\s\S]*?kKeyEventTransactionTimeoutMs\)') {
  throw 'healthy physical keys must use the bounded key transaction overload'
}
if ($keySink -notmatch 'TransportNotDelivered[\s\S]*?_EnsureServerConnectedUntil\(deadline\)[\s\S]*?process_key_bounded\(\)') {
  throw 'only definitely-unsent keys may recover and replay once with a bounded key IPC'
}
$recoveryStart = $keySink.IndexOf('WeaselTSF::KeyDispatchResult WeaselTSF::_ProcessKeyEventWithRecovery')
$recoveryEnd = $keySink.IndexOf('void WeaselTSF::_ProcessKeyEvent(', $recoveryStart)
if ($recoveryStart -lt 0 -or $recoveryEnd -le $recoveryStart) {
  throw 'unable to isolate WeaselTSF::_ProcessKeyEventWithRecovery'
}
$recoveryBody = $keySink.Substring($recoveryStart, $recoveryEnd - $recoveryStart)
$transportErrorPattern = 'result\s*==\s*weasel::KeyEventResult::TransportError'
$transportErrorMatches = [regex]::Matches($recoveryBody, $transportErrorPattern)
if ($transportErrorMatches.Count -ne 1) {
  throw 'ambiguous TransportError must appear only in the final recovery-scheduling branch'
}
$transportErrorTail = $recoveryBody.Substring($transportErrorMatches[0].Index)
if ($transportErrorTail -notmatch '_ScheduleServerRecovery\(\)[\s\S]*?return\s+KeyDispatchResult::AmbiguousConsumed' -or
    $transportErrorTail -match 'process_key_bounded\(\)|StartSession\(|_EnsureServerConnectedUntil\(') {
  throw 'ambiguous TransportError must be consumed locally after scheduling recovery without replay'
}
if ($tsfHeader -notmatch 'enum\s+class\s+KeyDispatchResult[\s\S]*?Handled[\s\S]*?Unhandled[\s\S]*?AmbiguousConsumed') {
  throw 'TSF key dispatch must preserve ambiguous-consumed state separately from normal unhandled keys'
}
if ($keySink -notmatch 'dispatch\s*==\s*KeyDispatchResult::Unhandled\s*\?\s*FALSE\s*:\s*TRUE') {
  throw 'only definitely unhandled keys may be released back to the host'
}

Write-Host 'First-key wakeup policy: PASS'
