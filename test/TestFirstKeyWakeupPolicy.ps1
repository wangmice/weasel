$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$tsfHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.h') -Raw
$keySink = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\KeyEventSink.cpp') -Raw
$client = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\WeaselClientImpl.cpp') -Raw
$pipe = Get-Content -LiteralPath (Join-Path $repo 'include\PipeChannel.h') -Raw
$ipc = Get-Content -LiteralPath (Join-Path $repo 'include\WeaselIPC.h') -Raw

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

if ($schedule -notmatch 'TryBeginServerRecovery\(\)[\s\S]*?QueueDllBackgroundTask\s*\(') {
  throw 'failed recovery must be deduplicated before queueing DLL-safe background work'
}
if ($schedule -match 'RegGetStringValue|GetServerInstanceName|GetServerLaunchMutexName|OpenMutex|CreateMutex|ShellExecute|Sleep') {
  throw 'TSF callback-side scheduler must not perform server discovery or launch work'
}
if ($tsf -notmatch 'enum\s+class\s+ServerRecoveryState\s*:\s*LONG[\s\S]*?Idle[\s\S]*?Busy[\s\S]*?Backoff') {
  throw 'server recovery coordinator must expose Idle/Busy/Backoff states'
}
if ($tsf -notmatch 'g_serverRecoveryRetryAfter') {
  throw 'backoff must be represented by a retry deadline'
}
if ($tsf -match 'kServerRecoveryRetryCooldownMs') {
  throw 'backoff must not be implemented by sleeping a worker for a cooldown'
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
if ($tsf -notmatch 'WaitForServerInstanceState\(instance_name,\s*legacy_instance_name,\s*false,[\s\S]*?kServerExitObservationTimeoutMs') {
  throw 'old-server exit observation must be short and background-only'
}
if ($tsf -notmatch 'kServerLaunchQuickObservationTimeoutMs\s*=\s*500' -or
    $tsf -notmatch 'kServerLaunchLeaseTimeoutMs\s*=\s*2000') {
  throw 'server launch must use a 500 ms fast observation inside a 2 s cross-process lease'
}
if ($tsf -notmatch 'WaitForServerInstanceState\([\s\S]*?kServerLaunchQuickObservationTimeoutMs[\s\S]*?remaining_launch_lease\s*=\s*kServerLaunchLeaseTimeoutMs\s*-\s*kServerLaunchQuickObservationTimeoutMs[\s\S]*?WaitForServerInstanceState\([\s\S]*?remaining_launch_lease') {
  throw 'launch mutex must remain held through the slow-start remainder of the launch lease'
}
if ($tsf -notmatch 'WaitForSingleObject\(launch_mutex\.handle,\s*0\)') {
  throw 'cross-process launch arbitration must stay nonblocking'
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
  throw 'successful cold reconnects must reset the previous launch failure streak'
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
if ($keySink -match 'TransportError\s*\|\||\|\|\s*[^\r\n]*TransportError') {
  throw 'ambiguous TransportError must never be grouped into the replay path'
}
if ($keySink -notmatch 'TransportError[\s\S]*?_ScheduleServerRecovery\(\)') {
  throw 'ambiguous transport failure must schedule recovery without replay'
}

Write-Host 'First-key wakeup policy: PASS'
