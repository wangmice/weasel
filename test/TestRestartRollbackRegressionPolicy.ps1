$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$serverMain = Get-Content -LiteralPath (Join-Path $repo 'WeaselServer\WeaselServer.cpp') -Raw
$installer = Get-Content -LiteralPath (Join-Path $repo 'output\install.nsi') -Raw

# If the old server window already exists while its pipe listener is not ready,
# restart/quit must still request shutdown before waiting on the process handle.
$restartStart = $serverMain.IndexOf('// Retain the existing process for both /quit and a normal restart.')
$restartEnd = $serverMain.IndexOf('  const bool check_updates =', $restartStart)
if ($restartStart -lt 0 -or $restartEnd -le $restartStart) {
  throw 'unable to isolate WeaselServer restart flow'
}
$restart = $serverMain.Substring($restartStart, $restartEnd - $restartStart)
if ($serverMain -notmatch 'OpenProcess\(SYNCHRONIZE\s*\|\s*PROCESS_QUERY_LIMITED_INFORMATION') {
  throw 'restart must retain enough process rights to verify fallback-window ownership'
}
if ($restart -notmatch 'else\s+if\s*\(running_server\)') {
  throw 'restart must handle an old server whose window exists before its pipe is ready'
}
if ($restart -notmatch 'WaitForSingleObject\(running_server,\s*0\)' -or
    $restart -notmatch 'process_state\s*==\s*WAIT_OBJECT_0') {
  throw 'startup-window fallback must first recognize an already-exited old process'
}
if ($restart -notmatch 'GetProcessId\(running_server\)' -or
    $restart -notmatch 'GetWindowThreadProcessId\(running_window,\s*&window_process_id\)' -or
    $restart -notmatch 'window_process_id\s*==\s*server_process_id') {
  throw 'startup-window fallback must verify that the HWND still belongs to the retained server process'
}
if ($restart -notmatch 'PostMessageW\(running_window,\s*WM_CLOSE,\s*0,\s*0\)') {
  throw 'startup-window fallback must request shutdown through the server window before waiting'
}
if ($restart -notmatch 'PostMessageW[\s\S]*?WaitForSingleObject\(running_server,\s*0\)\s*==\s*WAIT_OBJECT_0') {
  throw 'failed window shutdown must re-check the retained process for an exit race'
}
$waitIndex = $restart.IndexOf('WaitForRunningServerExit(running_server)')
$fallbackShutdownIndex = $restart.IndexOf('PostMessageW(running_window, WM_CLOSE, 0, 0)')
if ($waitIndex -lt 0 -or $fallbackShutdownIndex -lt 0 -or $fallbackShutdownIndex -ge $waitIndex) {
  throw 'process-exit wait must come only after the no-pipe fallback has requested shutdown'
}

# A runnable rollback requires both the setup executable and the server binary.
$restoreStart = $installer.IndexOf('Function RestorePreviousProgramBackup')
$restoreEnd = $installer.IndexOf('FunctionEnd', $restoreStart)
if ($restoreStart -lt 0 -or $restoreEnd -le $restoreStart) {
  throw 'unable to isolate RestorePreviousProgramBackup'
}
$restore = $installer.Substring($restoreStart, $restoreEnd - $restoreStart)
foreach ($critical in @('WeaselSetup.exe', 'WeaselServer.exe')) {
  if ($restore -notmatch ('restore_program_ok:[\s\S]*?IfFileExists\s+"\$PreviousInstallDir\\' + [regex]::Escape($critical) + '"\s+0\s+restore_program_failed')) {
    throw "rollback restore must verify $critical before treating the old program tree as runnable"
  }
}

# Registration is now straight-line fallthrough after the program-restore guard;
# keep NSIS warning 6012 from returning through an unreferenced label.
if ($installer -match '(?m)^rollback_register_old:\s*$') {
  throw 'rollback_register_old is an unused fallthrough label and must be removed'
}

Write-Host 'Restart/rollback regression policy: PASS'
