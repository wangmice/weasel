$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$server = Get-Content -LiteralPath (Join-Path $repo 'WeaselServer\WeaselServer.cpp') -Raw

if ($server -match 'client\.ShutdownServer\(\);\s*if\s*\(quit\)\s*return\s+0;') {
  throw 'WeaselServer /quit must not report success immediately after sending shutdown'
}
if ($server -notmatch 'constexpr\s+DWORD\s+kServerShutdownTimeoutMs\s*=\s*30000') {
  throw 'WeaselServer /quit must use the bounded 30s shutdown timeout'
}
if ($server -notmatch 'OpenRunningServerProcess\s*\(') {
  throw 'WeaselServer /quit must retain a handle to the running server process'
}
if ($server -notmatch 'WaitForSingleObject\(process,\s*kServerShutdownTimeoutMs\)') {
  throw 'WeaselServer /quit must wait for the actual running server process to exit'
}
if ($server -notmatch 'return\s+wait_result\s*==\s*WAIT_OBJECT_0') {
  throw 'WeaselServer /quit must fail when the bounded shutdown wait does not complete'
}

Write-Host 'Server shutdown policy: PASS'
