$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$globalsHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Globals.h') -Raw
$globalsCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Globals.cpp') -Raw
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$languageBar = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\LanguageBar.cpp') -Raw
$switcherHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\SwitcherSettingsDialog.h') -Raw
$switcher = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\SwitcherSettingsDialog.cpp') -Raw
$diag = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\DeployerDiagnostics.h') -Raw
$deployer = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\WeaselDeployer.cpp') -Raw
$configurator = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\Configurator.cpp') -Raw
$installer = Get-Content -LiteralPath (Join-Path $repo 'output\install.nsi') -Raw

if ($globalsHeader -notmatch 'bool\s+QueueDllBackgroundTask\s*\(') {
  throw 'TSF must expose one DLL-lifetime-aware background task helper'
}
if ($globalsCpp -notmatch 'GetModuleHandleExW\s*\([\s\S]*?GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS') {
  throw 'background tasks must acquire a loader reference before submission'
}
if ($globalsCpp -notmatch 'DllAddRef\s*\(\)[\s\S]*?TrySubmitThreadpoolCallback') {
  throw 'background task submission must make DllCanUnloadNow return S_FALSE first'
}
if ($globalsCpp -notmatch 'FreeLibraryWhenCallbackReturns\s*\(') {
  throw 'background callbacks must hold their loader reference until the callback returns'
}
if ($globalsCpp -notmatch 'DllRelease\s*\(\)') {
  throw 'background callbacks must release their logical DLL reference'
}
if ($globalsCpp -notmatch 'TrySubmitThreadpoolCallback[\s\S]*?FreeLibrary\s*\(') {
  throw 'failed background submission must release the loader reference'
}

$recoveryStart = $tsf.IndexOf('const auto try_start_recovery')
$recoveryEnd = $tsf.IndexOf('try_start_recovery();', $recoveryStart)
if ($recoveryStart -lt 0 -or $recoveryEnd -le $recoveryStart) {
  throw 'unable to isolate first-key background launcher'
}
$recoveryLaunch = $tsf.Substring($recoveryStart, $recoveryEnd - $recoveryStart)
if ($recoveryLaunch -match '\.detach\s*\(' -or
    $recoveryLaunch -notmatch 'QueueDllBackgroundTask\s*\(') {
  throw 'first-key server launch must use the DLL-lifetime-aware background helper'
}

$langStart = $languageBar.IndexOf('case ID_WEASELTRAY_RERUN_SERVICE:')
$langEnd = $languageBar.IndexOf('case ID_WEASELTRAY_USERCONFIG:', $langStart)
if ($langStart -lt 0 -or $langEnd -le $langStart) {
  throw 'unable to isolate language-bar restart launcher'
}
$langLaunch = $languageBar.Substring($langStart, $langEnd - $langStart)
if ($langLaunch -match '\.detach\s*\(' -or
    $langLaunch -notmatch 'QueueDllBackgroundTask\s*\(') {
  throw 'language-bar service restart must use the DLL-lifetime-aware background helper'
}

$getStart = $switcher.IndexOf('LRESULT SwitcherSettingsDialog::OnGetSchemata')
$getEnd = $switcher.IndexOf('LRESULT SwitcherSettingsDialog::OnOK', $getStart)
if ($getStart -lt 0 -or $getEnd -le $getStart) {
  throw 'unable to isolate SwitcherSettingsDialog::OnGetSchemata'
}
$getBody = $switcher.Substring($getStart, $getEnd - $getStart)
if ($getBody -match 'L"/k\s') {
  throw 'schema retrieval must not keep cmd.exe alive with /k'
}
if ($getBody -notmatch 'L"/c\s') {
  throw 'schema retrieval must run rime-install.bat with cmd /c'
}
if ($getBody -match 'WaitForSingleObject\s*\(') {
  throw 'OnGetSchemata must not synchronously wait on the UI thread'
}
if ($getBody -notmatch 'SetTimer\s*\(') {
  throw 'schema retrieval must observe completion asynchronously through the dialog timer'
}
if ($getBody -notmatch 'get_schemata_\.EnableWindow\(FALSE\)') {
  throw 'schema retrieval must suppress duplicate launch requests while one is pending'
}
if ($switcherHeader -notmatch 'MESSAGE_HANDLER\(WM_TIMER,\s*OnTimer\)') {
  throw 'schema retrieval completion must be routed through WM_TIMER'
}
if ($switcher -notmatch 'WaitForSingleObject\(schemata_process_,\s*0\)') {
  throw 'timer polling must inspect the process without blocking the UI thread'
}
if ($switcher -notmatch 'KillTimer\s*\(' -or
    $switcher -notmatch 'CloseHandle\(schemata_process_\)' -or
    $switcher -notmatch 'get_schemata_\.EnableWindow\(TRUE\)') {
  throw 'schema monitor teardown must stop the timer, close the handle, and restore the button'
}
if ($switcher -match 'TerminateProcess\s*\(') {
  throw 'schema retrieval timeout must not terminate an installer that may be writing files'
}

if ($diag -notmatch 'weasel-deployer-bootstrap\.log') {
  throw 'deployer bootstrap diagnostics must use a dedicated temp log'
}
if ($diag -notmatch 'FILE_APPEND_DATA' -or $diag -notmatch 'OPEN_ALWAYS') {
  throw 'deployer bootstrap diagnostics must append without truncating prior evidence'
}
if ($diag -notmatch 'GetLastError\s*\(' -or $diag -notmatch 'SetLastError\s*\(') {
  throw 'diagnostics must preserve the caller LastError value'
}
if ($diag -match 'Sleep\s*\(' -or $diag -match 'CreateMutex\s*\(' -or
    $diag -match 'WaitForSingleObject\s*\(' -or $diag -match 'TerminateProcess\s*\(') {
  throw 'diagnostics must not add waits, synchronization, or process termination'
}

$deployerMarkers = @(
  'process started; command_line',
  'before Run',
  'after Run result',
  'process exiting'
)
foreach ($marker in $deployerMarkers) {
  if ($deployer -notmatch [regex]::Escape($marker)) {
    throw "missing deployer diagnostic marker: $marker"
  }
}

$configuratorMarkers = @(
  'Configurator constructed',
  'before rime_api->setup',
  'after rime_api->setup',
  'before deployer_initialize',
  'after deployer_initialize',
  'Configurator::Run entered installing',
  'before UpdateWorkspace',
  'after UpdateWorkspace result',
  'before rime->deploy',
  'after rime->deploy result',
  'before deploy_config_file',
  'after deploy_config_file result',
  'before join_maintenance_thread',
  'after join_maintenance_thread',
  'UpdateWorkspace return'
)
foreach ($marker in $configuratorMarkers) {
  if ($configurator -notmatch [regex]::Escape($marker)) {
    throw "missing configurator diagnostic marker: $marker"
  }
}

if ($configurator -notmatch 'enum class ConfigureResult \{ Cancelled, Unchanged, Saved, Failed \};') {
  throw 'deployer settings results must distinguish unchanged from failure'
}
if (([regex]::Matches($configurator, 'return ConfigureResult::Unchanged;')).Count -lt 2) {
  throw 'both deployer settings dialogs must treat save_settings(false) as unchanged'
}
if ($configurator -match 'Failed to save switcher settings' -or
    $configurator -match 'Failed to save UI style settings') {
  throw 'save_settings(false) means unchanged in librime and must not be logged as failure'
}

if ($configurator -notmatch 'CreateMutex\(NULL, TRUE, L"WeaselDeployerMutex"\)') {
  throw 'diagnostics must preserve the existing WeaselDeployerMutex policy'
}
if ($configurator -match 'WeaselServerLaunch' -or
    $configurator -match 'ServerLaunchMutexGuard') {
  throw 'diagnostics must not reintroduce the reverted server-launch mutex experiment'
}
if ($configurator -match 'Sleep\s*\(') {
  throw 'diagnostics must not add timing changes to the deployer path'
}

$installExec = 'ExecWait "$INSTDIR\WeaselDeployer.exe /install" $R3'
$deployExec = 'ExecWait "$INSTDIR\WeaselDeployer.exe /deploy" $R3'
if (-not $installer.Contains($installExec) -or -not $installer.Contains($deployExec)) {
  throw 'diagnostics must preserve the existing installer ExecWait commands exactly'
}
if ($installer -notmatch '\[diagnostic\] before ExecWait /install' -or
    $installer -notmatch '\[diagnostic\] before ExecWait /deploy' -or
    $installer -notmatch '\[diagnostic\] ExecWait errors=1' -or
    $installer -notmatch '\[diagnostic\] ExecWait errors=0 exit_code=') {
  throw 'installer must expose the deployer launch/exit boundary through DetailPrint'
}

Write-Host 'Background lifetime and deployer policy: PASS'
