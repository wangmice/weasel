$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$globalsHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Globals.h') -Raw
$globalsCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Globals.cpp') -Raw
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$languageBar = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\LanguageBar.cpp') -Raw
$switcherHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\SwitcherSettingsDialog.h') -Raw
$switcher = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\SwitcherSettingsDialog.cpp') -Raw
$configurator = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\Configurator.cpp') -Raw
$deployer = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\WeaselDeployer.cpp') -Raw
$deployerProject = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\WeaselDeployer.vcxproj') -Raw

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

$recoveryStart = $tsf.IndexOf('void WeaselTSF::_ScheduleServerRecovery()')
$recoveryEnd = $tsf.IndexOf('bool WeaselTSF::_EnsureServerConnectedUntil(', $recoveryStart)
if ($recoveryStart -lt 0 -or $recoveryEnd -le $recoveryStart) {
  throw 'unable to isolate first-key background scheduler'
}
$recoveryLaunch = $tsf.Substring($recoveryStart, $recoveryEnd - $recoveryStart)
if ($recoveryLaunch -match '\.detach\s*\(' -or
    $recoveryLaunch -notmatch 'QueueDllBackgroundTask\s*\(') {
  throw 'first-key server recovery must use the DLL-lifetime-aware background helper'
}
if ($recoveryLaunch -match 'RegGetStringValue|GetServerInstanceName|GetServerLifecycleMutexName|OpenMutex|CreateMutex|ShellExecute|Sleep') {
  throw 'callback-side recovery scheduling must not perform blocking discovery or launch work'
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

if ($configurator -match 'WeaselDeployerMutex') {
  throw 'deployer correctness must use the shared server lifecycle lock'
}
if (([regex]::Matches($configurator, 'ServerLifecycleLock\s+lifecycle_lock')).Count -ne 4 -or
    ([regex]::Matches($configurator, 'lifecycle_lock\.Acquire\(0\)')).Count -ne 3) {
  throw 'workspace deploy GUI dictionary CLI dictionary and sync worker must each own the shared lifecycle lock'
}
if ($configurator -notmatch 'kInstallLifecycleWaitMs\s*=\s*30\s*\*\s*1000' -or
    $configurator -notmatch 'UpdateWorkspace\(bool\s+report_errors,[\s\S]*?bool\s+wait_for_install_startup\)[\s\S]*?lifecycle_wait_ms\s*=\s*wait_for_install_startup\s*\?\s*kInstallLifecycleWaitMs\s*:\s*0[\s\S]*?lifecycle_lock\.Acquire\(lifecycle_wait_ms\)') {
  throw 'installer deployment must wait boundedly for an early WeaselServer startup while normal deploy stays nonblocking'
}
if ($configurator -match 'CloseHandle\(hMutex\)') {
  throw 'deployer lifecycle ownership must be RAII and extend through EndMaintenance'
}


if (([regex]::Matches($configurator, 'join_maintenance_thread\s*\(')).Count -ne 1) {
  throw 'only isolated sync_user_data may retain librime''s unbounded maintenance join'
}
if ($deployer -notmatch 'kInternalSyncWorkerOption\[\]\s*=\s*L"/internal-sync-worker"' -or
    $deployer -notmatch 'kRimeOperationHardTimeoutMs\s*=\s*15\s*\*\s*60\s*\*\s*1000') {
  throw 'sync isolation must use one hidden worker mode with a finite hard deadline'
}
if ($deployer -notmatch 'WaitForSingleObject\(cancel_event,\s*kRimeOperationHardTimeoutMs\)' -or
    $deployer -notmatch 'TerminateProcess\(GetCurrentProcess\(\),\s*ERROR_TIMEOUT\)') {
  throw 'the isolated sync worker must self-terminate when librime exceeds the hard deadline'
}
if ($deployer -notmatch 'CreateProcessW\([\s\S]*?CREATE_NO_WINDOW' -or
    $deployer -notmatch 'WaitForSingleObject\([\s\S]*?process\.hProcess,[\s\S]*?kRimeOperationHardTimeoutMs\s*\+\s*kSyncSupervisorGraceMs' -or
    $deployer -notmatch 'TerminateProcess\(process\.hProcess,\s*ERROR_TIMEOUT\)') {
  throw 'the /sync supervisor must launch a hidden child and bound the child process lifetime'
}
$syncSupervisor = $deployer.IndexOf('return RunIsolatedSyncWorker();')
$deployerInit = $deployer.IndexOf('configurator.Initialize();')
if ($syncSupervisor -lt 0 -or $deployerInit -lt 0 -or $syncSupervisor -gt $deployerInit) {
  throw '/sync must launch the isolated worker before the supervising process initializes librime'
}
if ($deployer -notmatch 'if\s*\(internal_sync_worker\)[\s\S]*?return\s+Run\(argc,\s*argv\);[\s\S]*?CreateMutex\(NULL,\s*TRUE,\s*weasel::kDeployerExclusiveMutexName\)') {
  throw 'only the exact internal sync worker may bypass the deployer process singleton'
}
if ($deployer -notmatch 'kInstallerDeployOption\[\]\s*=\s*L"/install-deploy"' -or
    $deployer -notmatch 'IsOption\(args\.front\(\),\s*kInstallerDeployOption\)[\s\S]*?UpdateWorkspace\(false,\s*true\)') {
  throw 'installer-only deployment mode must wait for startup arbitration without changing manual /deploy behavior'
}

if ($deployer -match 'IsBoundedWorkspaceDeploy' -or
    $deployer -notmatch 'internal_sync_worker\s*&&\s*!cleanup\.watchdog\.Arm\(\)') {
  throw 'only the isolated sync worker should carry an entry-point watchdog'
}
if ($deployer -notmatch '~RuntimeCleanup\(\)[\s\S]*?watchdog\.Disarm\(\)[\s\S]*?_Module\.Term\(\)[\s\S]*?CoUninitialize\(\)') {
  throw 'entry-point watchdog must be disarmed before ATL and COM teardown'
}
if ($deployer -notmatch 'RimeOperationWatchdog\s+initialization_watchdog[\s\S]*?initialization_watchdog\.Arm\(\)[\s\S]*?configurator\.Initialize\(\)' -or
    $configurator -notmatch 'int\s+Configurator::UpdateWorkspace[\s\S]*?RimeOperationWatchdog\s+deploy_watchdog[\s\S]*?deploy_watchdog\.Arm\(\)[\s\S]*?ServerLifecycleLock\s+lifecycle_lock') {
  throw 'Rime initialization and workspace deployment must each have an operation-scoped hard watchdog'
}

if ($configurator -notmatch 'ServerSingletonPresent\(\)[\s\S]*?GetServerInstanceName\(\)[\s\S]*?GetLegacyServerInstanceName\(\)' -or
    $configurator -notmatch 'if\s*\(!client\.Connect\(\)\)[\s\S]*?ServerSingletonPresent\(\)[\s\S]*?return false;[\s\S]*?return true;') {
  throw 'deployer must distinguish an absent server from a singleton that is starting stopping or IPC-unavailable'
}

if ($configurator -notmatch 'enum class ConfigureResult \{ Cancelled, Unchanged, Saved, Failed \};') {
  throw 'deployer settings results must distinguish unchanged from failure'
}
if (([regex]::Matches($configurator, 'return ConfigureResult::Unchanged;')).Count -ne 2) {
  throw 'both deployer settings dialogs must treat save_settings(false) as unchanged'
}
if ($configurator -match 'Failed to save switcher settings' -or
    $configurator -match 'Failed to save UI style settings') {
  throw 'save_settings(false) means unchanged in librime and must not be logged as failure'
}

if (([regex]::Matches($deployerProject, '<OptimizeReferences>false</OptimizeReferences>')).Count -ne 2) {
  throw 'Release Win32 and x64 WeaselDeployer links must disable /OPT:REF to preserve ATL selectany dynamic initialization'
}
if ($deployerProject -match '<OptimizeReferences>true</OptimizeReferences>') {
  throw 'WeaselDeployer must not re-enable /OPT:REF while ATL::_AtlWinModule depends on selectany dynamic initialization'
}

if (([regex]::Matches($deployerProject, '<LinkTimeCodeGeneration>UseLinkTimeCodeGeneration</LinkTimeCodeGeneration>')).Count -ne 2) {
  throw 'Release Win32 and x64 WeaselDeployer links must use full /LTCG because /OPT:NOREF is incompatible with /LTCG:incremental'
}

Write-Host 'Background lifetime and deployer policy: PASS'
