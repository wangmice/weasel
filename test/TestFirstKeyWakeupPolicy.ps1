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
if ($ensure -notmatch 'while\s*\(GetTickCount64\(\)\s*<\s*deadline\)[\s\S]*?Sleep\(kFirstKeyRecoveryPollMs\)[\s\S]*?_Reconnect\(\)[\s\S]*?IsSessionActive\(\)[\s\S]*?return\s+true;') {
  throw 'first failed key must reconnect until a session is active or the deadline expires'
}
if ($ensure -notmatch 'while\s*\(GetTickCount64\(\)\s*<\s*deadline\)[\s\S]*?try_start_recovery\(\)') {
  throw 'first-key recovery must re-check for an exiting server becoming absent during the same key'
}
if ($ensure -notmatch 'return\s+false;') {
  throw 'timed-out recovery must still release the key to the host'
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
if ($pipe -notmatch 'bool\s+request_sent\s*=\s*false[\s\S]*?_Send\([^;]+;[\s\S]*?request_sent\s*=\s*true[\s\S]*?_ReceiveResponse\(\)') {
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

Write-Host 'First-key wakeup policy: PASS'
