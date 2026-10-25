$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$server = Get-Content -LiteralPath (Join-Path $repo 'WeaselServer\WeaselServer.cpp') -Raw
$serverImpl = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPCServer\WeaselServerImpl.cpp') -Raw

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

$runStart = $serverImpl.IndexOf('int ServerImpl::Run()')
$removeLoop = $serverImpl.IndexOf('_Module.RemoveMessageLoop();', $runStart)
$runtimeStart = $serverImpl.IndexOf('void ServerImpl::_StopRuntime()', $removeLoop)
if ($runStart -lt 0 -or $removeLoop -lt 0 -or $runtimeStart -le $removeLoop) {
  throw 'unable to isolate the message-loop shutdown sequence'
}
$afterLoop = $serverImpl.Substring($removeLoop, $runtimeStart - $removeLoop)
if ($afterLoop -match 'lock_guard\s+guard\(g_api_mutex\)' -or
    $afterLoop -notmatch '_SetRuntimeLifecycle\(ServerLifecycle::Stopping\)[\s\S]*?_StopRuntime\(\)') {
  throw 'runtime teardown must arm its watchdog without waiting on g_api_mutex'
}
$logoffStart = $serverImpl.IndexOf('LRESULT ServerImpl::OnEndSystemSession(')
$logoffEnd = $serverImpl.IndexOf('LRESULT ServerImpl::OnCommand(', $logoffStart)
if ($logoffStart -lt 0 -or $logoffEnd -le $logoffStart -or
    $serverImpl.Substring($logoffStart, $logoffEnd - $logoffStart) -notmatch 'if\s*\(wParam\)\s*\{[\s\S]*?_RequestStop\(\)') {
  throw 'WM_ENDSESSION must ignore cancelled logoff and use the nonblocking shutdown path'
}

$runtimeStart = $serverImpl.IndexOf('void ServerImpl::_StopRuntime()')
$pipeStopStart = $serverImpl.IndexOf('void ServerImpl::_StopPipeServer()', $runtimeStart)
if ($runtimeStart -lt 0 -or $pipeStopStart -le $runtimeStart) {
  throw 'server shutdown must route through the complete runtime teardown helper'
}
$runtimeStop = $serverImpl.Substring($runtimeStart, $pipeStopStart - $runtimeStart)

$arm = $runtimeStop.IndexOf('shutdown_watchdog.Arm()')
$stopPipe = $runtimeStop.IndexOf('_StopPipeServer();')
$stopTracker = $runtimeStop.IndexOf('process_tracker.Stop();')
if ($arm -lt 0 -or $stopPipe -lt 0 -or $stopTracker -lt 0 -or
    -not ($arm -lt $stopPipe -and $stopPipe -lt $stopTracker)) {
  throw 'one hard watchdog must cover pipe teardown and process-tracker drain in order'
}
if ($runtimeStop -notmatch 'if\s*\(!shutdown_watchdog\.Arm\(\)\)[\s\S]*?TerminateProcess\(GetCurrentProcess\(\),\s*ERROR_GEN_FAILURE\)[\s\S]*?std::abort\(\)') {
  throw 'watchdog setup failure must fail closed instead of entering unbounded joins'
}

$unguardedTrackerStops = ([regex]::Matches($serverImpl, 'process_tracker\.Stop\(\)')).Count
$unguardedPipeStops = ([regex]::Matches($serverImpl, '_StopPipeServer\(\);')).Count
if ($unguardedTrackerStops -ne 1 -or $unguardedPipeStops -ne 1) {
  throw 'pipe and process-tracker teardown must only be invoked inside _StopRuntime'
}
if ($serverImpl -notmatch 'if\s*\(m_runtimeStopped\)\s*return;' -or
    $serverImpl -notmatch 'process_tracker\.Stop\(\);\s*m_runtimeStopped\s*=\s*true;') {
  throw 'complete runtime teardown must remain idempotent after a successful drain'
}


# Window messages may enqueue Rime work but must never execute or wait for it.
$header = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPCServer\WeaselServerImpl.h') -Raw
$colorStart = $serverImpl.IndexOf('LRESULT ServerImpl::OnColorChange(')
$colorEnd = $serverImpl.IndexOf('LRESULT ServerImpl::OnCreate(', $colorStart)
$commandStart = $serverImpl.IndexOf('LRESULT ServerImpl::OnCommand(')
$commandEnd = $serverImpl.IndexOf('LRESULT ServerImpl::_HandleCommand(', $commandStart)
if ($colorStart -lt 0 -or $colorEnd -le $colorStart -or
    $commandStart -lt 0 -or $commandEnd -le $commandStart) {
  throw 'unable to isolate window-level Rime entry points'
}
$colorPath = $serverImpl.Substring($colorStart, $colorEnd - $colorStart)
$commandPath = $serverImpl.Substring($commandStart, $commandEnd - $commandStart)
if ($colorPath -match 'g_api_mutex|UpdateColorTheme\(' -or
    $commandPath -match 'g_api_mutex|SetOption\(') {
  throw 'window message handlers must never wait for or execute Rime'
}
if ($colorPath -notmatch '_QueueControlTask\(' -or
    $commandPath -notmatch '_QueueControlTask\(') {
  throw 'window Rime requests must be queued to the owned control worker'
}
if ($header -notmatch 'kMaxControlTasks\s*=\s*64' -or
    $serverImpl -notmatch 'm_controlTasks\.size\(\)\s*>=\s*kMaxControlTasks' -or
    $serverImpl -notmatch 'm_controlTasks\.pop_front\(\)') {
  throw 'control queue must be bounded, with pending tasks coalesced'
}
if ($header -match 'm_controlSessions|ControlSession|session_generation|m_nextControlSessionGeneration' -or
    $serverImpl -match 'm_controlSessions|ControlSession|session_generation|m_nextControlSessionGeneration|m_sessionEpoch') {
  throw 'window control must not keep a duplicate session-lifetime registry'
}
$workerStart = $serverImpl.IndexOf('void ServerImpl::_RunControlWorker()')
$workerEnd = $serverImpl.IndexOf('DWORD ServerImpl::OnCommand(WEASEL_IPC_COMMAND', $workerStart)
if ($workerStart -lt 0 -or $workerEnd -le $workerStart -or
    $serverImpl.Substring($workerStart, $workerEnd - $workerStart) -notmatch
      'm_pRequestHandler->FindSession\(task\.session_id\)\s*==\s*task\.session_id') {
  throw 'targeted ASCII tasks must validate the live session under the API lock'
}
if ($serverImpl -notmatch 'pending\.kind\s*==\s*ControlTaskKind::Ascii' -or
    $serverImpl -notmatch 'm_controlTasks\.erase\(victim\)') {
  throw 'queue saturation must evict only ASCII commands, never a pending theme'
}
if ($header -notmatch 'std::optional<ControlTask>\s+m_deferredControlTask' -or
    $serverImpl -notmatch '_RequeueDeferredControlTask\(ControlTask task\) noexcept' -or
    $serverImpl -notmatch 'm_deferredControlTask\s*=\s*task' -or
    $serverImpl -match 'm_controlTasks\.push_front\(task\)') {
  throw 'maintenance deferral must use an allocation-free, no-throw task slot'
}
$controlStop = $runtimeStop.IndexOf('_StopControlWorker();')
if ($controlStop -lt 0 -or $arm -ge $controlStop -or $controlStop -ge $stopPipe) {
  throw 'control worker must drain within the existing watchdog before pipe teardown'
}
if ($serverImpl -notmatch '_StopControlWorker\(\)[\s\S]*?m_controlWorker\.join\(\)') {
  throw 'owned control worker must join before RequestHandler/UI teardown'
}

Write-Host 'Server shutdown policy: PASS'
