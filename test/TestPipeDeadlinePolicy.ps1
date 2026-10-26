$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$pipeCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\PipeChannel.cpp') -Raw
$pipeHeader = Get-Content -LiteralPath (Join-Path $repo 'include\PipeChannel.h') -Raw
$clientCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\WeaselClientImpl.cpp') -Raw
$clientImplHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\WeaselClientImpl.h') -Raw
$composition = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Composition.cpp') -Raw
$clientHeader = Get-Content -LiteralPath (Join-Path $repo 'include\WeaselIPC.h') -Raw
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$tsfHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.h') -Raw
$keySink = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\KeyEventSink.cpp') -Raw

# FILE_FLAG_OVERLAPPED handles must not use the initiating call's byte-count
# output. Microsoft documents lpNumberOfBytes{Read,Written}=NULL for async
# handles; obtain the actual transfer count from GetOverlappedResult instead.
if ($pipeCpp -notmatch 'WriteFile\(pipe,\s*buffer,\s*size,\s*(?:NULL|nullptr),\s*overlapped\)') {
  throw 'overlapped WriteFile must pass a null byte-count pointer'
}
if ($pipeCpp -notmatch 'ReadFile\(pipe,\s*buffer,\s*size,\s*(?:NULL|nullptr),\s*overlapped\)') {
  throw 'overlapped ReadFile must pass a null byte-count pointer'
}
if ($pipeCpp -notmatch 'if\s*\(ok\)\s*\{[\s\S]*?GetOverlappedResult\(pipe,\s*overlapped,\s*&result\.bytes,\s*FALSE\)[\s\S]*?return\s+result;') {
  throw 'immediately completed overlapped I/O must obtain its byte count from GetOverlappedResult'
}

# Normal key/Echo/focus traffic keeps the short input budget. Session creation
# and normal state changes get three seconds so a wedged server cannot freeze a
# host UI thread for tens of seconds. Maintenance remains a separate 30-second
# class because librime deployment/reinitialization is intentionally slower.
if ($clientCpp -notmatch 'kStateChangingTransactionTimeoutMs\s*=\s*3000') {
  throw 'ordinary state-changing IPC must use the three-second bounded timeout'
}
if ($clientCpp -notmatch 'kMaintenanceTransactionTimeoutMs\s*=\s*30000') {
  throw 'maintenance IPC must retain its separate 30-second bounded timeout'
}
if ($clientCpp -notmatch 'kHostCallbackTransactionTimeoutMs\s*=\s*100') {
  throw 'host callback IPC must stay bounded to 100 ms'
}
foreach ($command in @(
  'WEASEL_IPC_ECHO',
  'WEASEL_IPC_FOCUS_IN',
  'WEASEL_IPC_FOCUS_OUT',
  'WEASEL_IPC_UPDATE_INPUT_POS'
)) {
  $pattern = [regex]::Escape($command) + '[\s\S]*?kHostCallbackTransactionTimeoutMs'
  if ($clientCpp -notmatch $pattern) {
    throw "$command must use the 100 ms host-callback timeout"
  }
}
foreach ($command in @(
  'WEASEL_IPC_START_SESSION',
  'WEASEL_IPC_SHUTDOWN_SERVER',
  'WEASEL_IPC_COMMIT_COMPOSITION',
  'WEASEL_IPC_CLEAR_COMPOSITION',
  'WEASEL_IPC_SELECT_CANDIDATE_ON_CURRENT_PAGE',
  'WEASEL_IPC_HIGHLIGHT_CANDIDATE_ON_CURRENT_PAGE',
  'WEASEL_IPC_CHANGE_PAGE',
  'WEASEL_IPC_TRAY_COMMAND'
)) {
  $pattern = [regex]::Escape($command) + '[\s\S]*?kStateChangingTransactionTimeoutMs'
  if ($clientCpp -notmatch $pattern) {
    throw "$command must use the three-second state-changing timeout"
  }
}
foreach ($command in @(
  'WEASEL_IPC_START_MAINTENANCE',
  'WEASEL_IPC_END_MAINTENANCE'
)) {
  $pattern = [regex]::Escape($command) + '[\s\S]*?kMaintenanceTransactionTimeoutMs'
  if ($clientCpp -notmatch $pattern) {
    throw "$command must retain the 30-second maintenance timeout"
  }
}
if ($clientCpp -notmatch 'WEASEL_IPC_END_SESSION[\s\S]*?kTeardownTransactionTimeoutMs') {
  throw 'EndSession must retain its short teardown timeout'
}

# Pipe discovery on a physical key stays on one short end-to-end deadline.
# Rime session creation has a separate bounded budget because AddSession is
# application work (session creation, schema/style/status/UI), not pipe I/O.
if ($clientHeader -notmatch 'bool\s+Connect\(DWORD\s+timeout_ms\)') {
  throw 'Client must expose a bounded Connect overload for recovery'
}
if ($clientHeader -notmatch 'void\s+StartSession\(DWORD\s+timeout_ms\)') {
  throw 'Client must expose a bounded StartSession overload for recovery'
}
if ($pipeHeader -notmatch 'bool\s+Connect\(DWORD\s+timeout_ms\)') {
  throw 'PipeChannel must accept the caller recovery budget'
}
if ($clientHeader -notmatch 'ProcessKeyEventEx\(KeyEvent\s+const&\s+keyEvent,\s*DWORD\s+timeout_ms\)') {
  throw 'ClientImpl must expose a bounded key transaction overload'
}
if ($clientCpp -notmatch 'ProcessKeyEventEx\(KeyEvent\s+const&\s+keyEvent,\s*DWORD\s+timeout_ms\)[\s\S]*?channel\.Transact\(req,\s*timeout_ms\)') {
  throw 'bounded key transactions must pass the caller timeout to PipeChannel'
}
if ($tsfHeader -notmatch 'kKeyEventTransactionTimeoutMs\s*=\s*100') {
  throw 'healthy physical-key IPC must stay bounded to 100 ms'
}
if ($tsf -notmatch 'OnSetThreadFocus\(\)[\s\S]*?m_client\.Echo\(\)[\s\S]*?ProcessKeyEventEx\(weasel::KeyEvent\(0\),[\s\S]*?kKeyEventTransactionTimeoutMs\)') {
  throw 'thread-focus status synchronization must use the bounded key IPC overload'
}
if ($tsf -match 'OnSetThreadFocus\(\)[\s\S]*?m_client\.ProcessKeyEvent\(0\)') {
  throw 'thread-focus status synchronization must not use the one-second default key transaction'
}
if ($keySink -notmatch 'process_key_bounded[\s\S]*?ProcessKeyEventEx\(keyEvent,[\s\S]*?kKeyEventTransactionTimeoutMs\)') {
  throw 'healthy TSF keys must use the bounded key IPC overload'
}
if ($keySink -notmatch 'kFastTransportRecoveryBudgetMs') {
  throw 'inactive physical-key reconnects must retain the short transport budget'
}
if ($tsfHeader -notmatch 'kFastTransportRecoveryBudgetMs\s*=\s*20' -or
    $tsfHeader -notmatch 'kSessionEstablishmentTimeoutMs\s*=\s*1000') {
  throw 'TSF must separate fast transport recovery from bounded Rime session establishment'
}
if ($tsf -notmatch '_EnsureServerConnected\(\)[\s\S]*?GetTickCount64\(\)\s*\+[\s\S]*?kFastTransportRecoveryBudgetMs') {
  throw 'activation-time pipe discovery must stay on the short transport budget'
}
if ($tsf -notmatch '_EnsureServerConnectedUntil\(ULONGLONG\s+recovery_deadline\)[\s\S]*?m_client\.Connect\(remaining\)[\s\S]*?m_client\.StartSession\(kSessionEstablishmentTimeoutMs\)') {
  throw 'pipe discovery and Rime session establishment must use separate bounded budgets'
}
if ($tsf -match '_EnsureServerConnectedUntil\(ULONGLONG\s+recovery_deadline\)[\s\S]*?m_client\.StartSession\(remaining\)') {
  throw 'StartSession must not consume only the few milliseconds left by pipe discovery'
}
if ($keySink -notmatch '_ProcessKeyEventWithRecovery\([\s\S]*?ULONGLONG\s+recovery_deadline') {
  throw 'transport/session retry must reuse the physical key recovery deadline'
}
if ($keySink -notmatch 'TransportError[\s\S]*?KeyDispatchResult::AmbiguousConsumed' -or
    $keySink -notmatch 'dispatch\s*==\s*KeyDispatchResult::Unhandled\s*\?\s*FALSE\s*:\s*TRUE') {
  throw 'ambiguous timed-out keys must stay consumed locally instead of falling through to the host'
}
if ($keySink -notmatch 'TransportNotDelivered[\s\S]*?_EnsureServerConnectedUntil\(deadline\)[\s\S]*?process_key_bounded\(\)') {
  throw 'definitely-undelivered keys may replay only after bounded recovery'
}
if ($keySink -notmatch 'InvalidSession[\s\S]*?StartSession\(kSessionEstablishmentTimeoutMs\)[\s\S]*?process_key_bounded\(\)') {
  throw 'InvalidSession rebuild must use the dedicated session-establishment timeout before one bounded replay'
}
if ($keySink -match 'InvalidSession[\s\S]*?remaining_recovery_time\(') {
  throw 'InvalidSession rebuild must not be squeezed into the physical-key transport deadline'
}
if ($keySink -match 'process_key_bounded[\s\S]*?remaining_recovery_time\([^\)]*\)[\s\S]*?ProcessKeyEventEx\(keyEvent,\s*remaining') {
  throw 'successful recovery must not squeeze the real key IPC into leftover recovery milliseconds'
}

# StartSession(timeout_ms) itself is also an end-to-end budget. If an active
# session triggers an Echo preflight, that Echo must consume the caller's
# budget rather than taking an independent default one-second transaction
# before START receives a fresh timeout.
$startSessionStart = $clientCpp.IndexOf('void ClientImpl::StartSession(DWORD timeout_ms)')
$endSessionStart = $clientCpp.IndexOf('void ClientImpl::EndSession()', $startSessionStart)
if ($startSessionStart -lt 0 -or $endSessionStart -le $startSessionStart) {
  throw 'unable to isolate bounded ClientImpl::StartSession'
}
$startSessionBody = $clientCpp.Substring($startSessionStart, $endSessionStart - $startSessionStart)
if ($startSessionBody -notmatch 'deadline\s*=\s*GetTickCount64\(\)\s*\+\s*timeout_ms') {
  throw 'StartSession(timeout_ms) must create one absolute operation deadline'
}
if ($startSessionBody -notmatch 'RemainingTransactionTime\(deadline\)[\s\S]*?WEASEL_IPC_ECHO[\s\S]*?remaining') {
  throw 'StartSession Echo preflight must consume the remaining caller budget'
}
if ($startSessionBody -notmatch 'RemainingTransactionTime\(deadline\)[\s\S]*?WEASEL_IPC_START_SESSION[\s\S]*?remaining') {
  throw 'START_SESSION must consume only the budget left after Echo/client-info staging'
}


# Focus loss and caret exit are synchronous host callbacks. Their composition
# cleanup must not inherit the default 3000 ms IPC wait, while legacy callers
# retain their default timeout and edit-session scheduling.
if ($tsfHeader -notmatch 'kFocusLossClearTimeoutMs\s*=\s*100' -or
    $tsfHeader -notmatch '_AbortComposition\(\s*bool\s+clear\s*=\s*true,\s*DWORD\s+clear_timeout_ms\s*=\s*0,\s*DWORD\s+editSessionFlags\s*=\s*TF_ES_ASYNCDONTCARE\)') {
  throw 'TSF focus cleanup must expose a dedicated 100 ms timeout without changing the default'
}
if ($clientHeader -notmatch 'bool\s+ClearComposition\(DWORD\s+timeout_ms\)' -or
    $clientImplHeader -notmatch 'bool\s+ClearComposition\(DWORD\s+timeout_ms\)' -or
    $clientCpp -notmatch 'ClientImpl::ClearComposition\(\)[\s\S]*?return\s+ClearComposition\(0\)' -or
    $clientCpp -notmatch 'ClientImpl::ClearComposition\(DWORD\s+timeout_ms\)[\s\S]*?WEASEL_IPC_CLEAR_COMPOSITION,\s*0,\s*_SessionId\(\),\s*timeout_ms' -or
    $clientCpp -notmatch 'Client::ClearComposition\(DWORD\s+timeout_ms\)[\s\S]*?m_pImpl->ClearComposition\(timeout_ms\)') {
  throw 'bounded composition clear must propagate its budget without altering legacy calls'
}
if ($composition -notmatch 'void\s+WeaselTSF::_AbortComposition\(\s*bool\s+clear,\s*DWORD\s+clear_timeout_ms,\s*DWORD\s+editSessionFlags\)' -or
    $composition -notmatch 'm_client\.ClearComposition\(clear_timeout_ms\)') {
  throw 'composition cleanup must forward the selected clear timeout'
}
$lostThreadFocus = $tsf.IndexOf('STDMETHODIMP WeaselTSF::OnKillThreadFocus()')
$nextThreadFocus = $tsf.IndexOf('BOOL WeaselTSF::_InitThreadFocusSink()', $lostThreadFocus)
$lostKeyFocus = $keySink.IndexOf('STDMETHODIMP WeaselTSF::OnSetFocus(BOOL fForeground)')
$nextKeyFocus = $keySink.IndexOf('STDMETHODIMP WeaselTSF::OnTestKeyDown(', $lostKeyFocus)
if ($lostThreadFocus -lt 0 -or $nextThreadFocus -le $lostThreadFocus -or
    $lostKeyFocus -lt 0 -or $nextKeyFocus -le $lostKeyFocus) {
  throw 'unable to isolate TSF focus-loss callbacks'
}
$threadFocusBody = $tsf.Substring($lostThreadFocus, $nextThreadFocus - $lostThreadFocus)
$keyFocusBody = $keySink.Substring($lostKeyFocus, $nextKeyFocus - $lostKeyFocus)
if ($threadFocusBody -notmatch '_AbortComposition\(true,\s*kFocusLossClearTimeoutMs\)' -or
    $keyFocusBody -notmatch 'm_client\.FocusOut\(\);\s*_AbortComposition\(true,\s*kFocusLossClearTimeoutMs\)') {
  throw 'both TSF focus-loss callbacks must use the short composition-clear budget'
}

Write-Host 'Pipe deadline policy: PASS'
