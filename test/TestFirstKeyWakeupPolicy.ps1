$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$keySink = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\KeyEventSink.cpp') -Raw
$client = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\WeaselClientImpl.cpp') -Raw
$pipe = Get-Content -LiteralPath (Join-Path $repo 'include\PipeChannel.h') -Raw
$ipc = Get-Content -LiteralPath (Join-Path $repo 'include\WeaselIPC.h') -Raw

$ensureStart = $tsf.IndexOf('bool WeaselTSF::_EnsureServerConnected()')
if ($ensureStart -lt 0) {
  throw 'unable to isolate _EnsureServerConnected'
}
$ensure = $tsf.Substring($ensureStart)

if ($ensure -match '_reconnectRetry\s*>?=\s*6') {
  throw 'server recovery must not wait for six failed keystrokes before launching'
}
if ($ensure -notmatch 'bool\s+launch_started\s*=\s*false') {
  throw 'first-key recovery must track whether a launcher has already been started'
}
if ($ensure -notmatch 'try_start_recovery\s*=\s*\[&\]\(\)[\s\S]*?count_server_process_in_current_session\(\)\s*!=\s*0[\s\S]*?std::thread') {
  throw 'first failed reconnect must launch once the server is actually absent'
}
if ($tsf -notmatch 'constexpr\s+ULONGLONG\s+kFirstKeyRecoveryTimeoutMs\s*=\s*1000') {
  throw 'first-key recovery must allow up to one second for server startup'
}
if ($tsf -notmatch 'constexpr\s+DWORD\s+kFirstKeyRecoveryPollMs\s*=\s*[1-9][0-9]*') {
  throw 'first-key recovery must use a bounded reconnect polling interval'
}
if ($ensure -notmatch 'deadline\s*=\s*GetTickCount64\(\)\s*\+\s*kFirstKeyRecoveryTimeoutMs') {
  throw 'first failed key must establish a bounded recovery deadline'
}

$loopStart = $ensure.IndexOf('while (GetTickCount64() < deadline)')
if ($loopStart -lt 0) {
  throw 'first failed key must retry only while the shared deadline remains live'
}
$loopEnd = $ensure.IndexOf('return false;', $loopStart)
if ($loopEnd -lt 0) {
  throw 'unable to isolate the bounded first-key recovery loop'
}
$recoveryLoop = $ensure.Substring($loopStart, $loopEnd - $loopStart)
if ($recoveryLoop -notmatch 'RemainingRecoveryTime\(deadline\)') {
  throw 'first-key retry loop must consume the remaining shared deadline budget'
}
if ($recoveryLoop -notmatch 'Sleep\([\s\S]*?kFirstKeyRecoveryPollMs') {
  throw 'first-key retry loop must retain bounded polling sleeps'
}
if ($recoveryLoop -notmatch 'if\s*\(reconnect_until\(deadline\)\)\s*\{[\s\S]*?return\s+true;') {
  throw 'first failed key must reconnect until a session is active or the shared deadline expires'
}
if ($recoveryLoop -notmatch 'try_start_recovery\(\)') {
  throw 'first-key recovery must re-check for an exiting server becoming absent during the same key'
}
if ($ensure -notmatch 'return\s+false;') {
  throw 'timed-out recovery must still release the key to the host'
}

# Recovery enters Disconnect only after IsSessionActive() is false. In that
# state Disconnect is a transport reset and must not retire SessionKey, or a
# lost START_SESSION response cannot be retried idempotently.
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
  throw 'active Disconnect must still end the established logical session before closing transport'
}

# A stale local pipe handle after WeaselServer exits is not proof that the
# first key reached the server. Distinguish a send-stage failure (safe replay)
# from a response-stage failure (ambiguous; replay could duplicate input).
if ($ipc -notmatch 'TransportNotDelivered') {
  throw 'key result must distinguish transport failure before request delivery'
}
if ($pipe -notmatch 'class\s+PipeTransactionError[\s\S]*?request_may_have_been_delivered') {
  throw 'pipe transaction failures must carry request-delivery ambiguity'
}
if ($pipe -notmatch 'bool\s+request_sent\s*=\s*false[\s\S]*?_Send\([^;]+;[\s\S]*?request_sent\s*=\s*true[\s\S]*?_ReceiveResponse\(deadline\)') {
  throw 'pipe transaction must mark delivery only after the request write succeeds'
}
if ($client -notmatch 'catch\s*\(const\s+PipeTransactionError&[\s\S]*?request_may_have_been_delivered[\s\S]*?TransportNotDelivered') {
  throw 'key processing must map definitely-unsent pipe failures to TransportNotDelivered'
}
if ($keySink -notmatch 'TransportNotDelivered[\s\S]*?_EnsureServerConnected\(\)[\s\S]*?ProcessKeyEventEx\(keyEvent\)') {
  throw 'TSF must recover and replay a key only when transport proves it was not delivered'
}
if ($keySink -match 'TransportError\s*\|\||\|\|\s*[^\r\n]*TransportError') {
  throw 'ambiguous TransportError must never be grouped into the safe replay path'
}
if ($keySink -notmatch 'kInvalidSessionRecoveryTimeoutMs\s*=\s*1000') {
  throw 'invalid-session recovery must use the one-second first-key budget'
}
if ($keySink -notmatch 'InvalidSession[\s\S]*?StartSession\(kInvalidSessionRecoveryTimeoutMs\)') {
  throw 'invalid-session recovery must not fall back to the three-second default StartSession budget'
}

Write-Host 'First-key wakeup policy: PASS'
