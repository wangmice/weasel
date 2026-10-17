$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$ipcProject = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\WeaselIPC.vcxproj') -Raw
$ipcFilters = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\WeaselIPC.vcxproj.filters') -Raw
$ipcXmake = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\xmake.lua') -Raw
$sessionKeySource = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\WeaselSessionKey.cpp') -Raw
$pipeServer = Get-Content -LiteralPath (Join-Path $repo 'include\PipeServer.h') -Raw
$rimeHeader = Get-Content -LiteralPath (Join-Path $repo 'include\RimeWithWeasel.h') -Raw
$rimeUtility = Get-Content -LiteralPath (Join-Path $repo 'RimeWithWeasel\WeaselUtility.cpp') -Raw

# xmake picks up implementation sources through the directory glob, while the
# legacy MSBuild project uses an explicit source list. Both build paths must
# compile the SessionKey implementation or the MSBuild path will link with
# unresolved GenerateSessionKey/ParseSessionKeyMetadata symbols.
if ($ipcXmake -notmatch 'add_files\("\.\/\*\.cpp"\)') {
  throw 'WeaselIPC xmake target must continue compiling all implementation .cpp files'
}
if ($ipcProject -notmatch '<ClCompile Include="WeaselSessionKey\.cpp"\s*/>') {
  throw 'WeaselIPC MSBuild project must compile WeaselSessionKey.cpp'
}
if ($ipcProject -notmatch '<ClInclude Include="\.\.\\include\\WeaselSessionKey\.h"\s*/>') {
  throw 'WeaselIPC MSBuild project must expose WeaselSessionKey.h'
}
if ($ipcProject -notmatch '<ClInclude Include="\.\.\\include\\WeaselSessionLifecycle\.h"\s*/>') {
  throw 'WeaselIPC MSBuild project must expose WeaselSessionLifecycle.h'
}
if ($ipcFilters -notmatch '<ClCompile Include="WeaselSessionKey\.cpp">') {
  throw 'WeaselIPC filters must classify WeaselSessionKey.cpp'
}

# WeaselIPC MSBuild enables /Yu"stdafx.h" project-wide. MSVC discards tokens
# before the PCH include; wrapping stdafx.h in an outer #ifdef therefore makes
# the matching #endif visible without its opening directive (C1020). Keep the
# PCH include as the first preprocessing directive in this source.
if ($sessionKeySource -notmatch '^\s*#include\s+"stdafx\.h"') {
  throw 'WeaselSessionKey.cpp must include stdafx.h before any conditional preprocessing for MSBuild /Yu'
}

# Connection metadata may be discovered once for a worker, but healthy key
# traffic must not acquire the global worker-registry mutex or scan workers on
# every request. Cache the immutable context in worker-thread local storage.
$contextStart = $pipeServer.IndexOf('std::optional<ConnectionContext> CurrentConnectionContext()')
if ($contextStart -lt 0) {
  throw 'PipeServer CurrentConnectionContext implementation is missing'
}
$contextEnd = $pipeServer.IndexOf('  /* Unblock listeners blocked in ConnectNamedPipe', $contextStart)
if ($contextEnd -le $contextStart) {
  throw 'PipeServer CurrentConnectionContext implementation boundary is missing'
}
$contextBody = $pipeServer.Substring($contextStart, $contextEnd - $contextStart)
$cachePos = $contextBody.IndexOf('current_connection_context_.get()')
$publishPos = $contextBody.IndexOf('current_connection_context_.reset(')
$lockPos = $contextBody.IndexOf('m_workers_mutex')
if ($cachePos -lt 0 -or $publishPos -lt 0) {
  throw 'PipeServer must cache and publish worker connection context in thread-local storage'
}
if ($lockPos -ge 0 -and $cachePos -gt $lockPos) {
  throw 'PipeServer must consult the worker-thread context cache before touching m_workers_mutex'
}

# RimeWithWeasel.cpp owns file-local librime state. Public headers must not
# reference that state from inline lifecycle code or every including TU fails
# before link time. DestroySession stays declared in the header and is defined
# in an already-compiled implementation unit.
if ($rimeHeader -notmatch 'DWORD\s+DestroySession\(WeaselSessionId\s+ipc_id\)\s+override\s*;') {
  throw 'Rime backend must declare DestroySession without an inline implementation'
}
if ($rimeHeader -match 'DestroySession\([\s\S]*?rime_api') {
  throw 'Rime public header must not reference implementation-only rime_api state'
}
$destroyStart = $rimeUtility.IndexOf('DWORD RimeWithWeaselHandler::DestroySession(WeaselSessionId ipc_id)')
if ($destroyStart -lt 0) {
  throw 'Rime backend must define DestroySession in an implementation source'
}
$destroyEnd = $rimeUtility.IndexOf('fs::path WeaselUserDataPath()', $destroyStart)
if ($destroyEnd -le $destroyStart) {
  throw 'Rime DestroySession implementation boundary is missing'
}
$destroyBody = $rimeUtility.Substring($destroyStart, $destroyEnd - $destroyStart)
if ($destroyBody -notmatch 'if\s*\(m_active_session\s*==\s*ipc_id\)[\s\S]*?m_ui->Hide\(\)[\s\S]*?m_active_session\s*=\s*0') {
  throw 'background session destruction must not hide or clear unrelated active UI'
}
if ($destroyBody -notmatch 'destroy_session\(session_id\)') {
  throw 'lifecycle destruction must still release the underlying Rime session'
}

Write-Host 'Session lifecycle build/hot-path policy: PASS'