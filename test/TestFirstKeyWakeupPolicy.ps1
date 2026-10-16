$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw

$ensureStart = $tsf.IndexOf('bool WeaselTSF::_EnsureServerConnected()')
if ($ensureStart -lt 0) {
  throw 'unable to isolate _EnsureServerConnected'
}
$ensure = $tsf.Substring($ensureStart)

if ($ensure -match '_reconnectRetry\s*>?=\s*6') {
  throw 'server recovery must not wait for six failed keystrokes before launching'
}
if ($ensure -notmatch 'count_server_process_in_current_session\(\)\s*==\s*0[\s\S]*?std::thread') {
  throw 'first failed reconnect must trigger serialized server recovery immediately when the server is absent'
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
if ($ensure -notmatch 'return\s+false;') {
  throw 'timed-out recovery must still release the key to the host'
}

Write-Host 'First-key wakeup policy: PASS'
