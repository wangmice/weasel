$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$pipeCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\PipeChannel.cpp') -Raw
$pipeHeader = Get-Content -LiteralPath (Join-Path $repo 'include\PipeChannel.h') -Raw
$clientCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\WeaselClientImpl.cpp') -Raw
$clientHeader = Get-Content -LiteralPath (Join-Path $repo 'include\WeaselIPC.h') -Raw
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw

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

# The first-key recovery window is an end-to-end deadline. Connect and
# StartSession must consume the same remaining budget instead of each creating
# a fresh one-second transaction window.
if ($clientHeader -notmatch 'bool\s+Connect\(DWORD\s+timeout_ms\)') {
  throw 'Client must expose a bounded Connect overload for recovery'
}
if ($clientHeader -notmatch 'void\s+StartSession\(DWORD\s+timeout_ms\)') {
  throw 'Client must expose a bounded StartSession overload for recovery'
}
if ($pipeHeader -notmatch 'bool\s+Connect\(DWORD\s+timeout_ms\)') {
  throw 'PipeChannel must accept the caller recovery budget'
}
if ($tsf -notmatch 'deadline\s*=\s*GetTickCount64\(\)\s*\+\s*kFirstKeyRecoveryTimeoutMs[\s\S]*?reconnect_until\(deadline\)') {
  throw 'first-key deadline must be created before the initial recovery attempt'
}
if ($tsf -notmatch 'reconnect_until\s*=\s*\[&\]\(ULONGLONG\s+recovery_deadline\)[\s\S]*?m_client\.Connect\(remaining\)[\s\S]*?m_client\.StartSession\(remaining\)') {
  throw 'Connect and StartSession must consume one shared first-key deadline'
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

Write-Host 'Pipe deadline policy: PASS'
