$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$compartment = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Compartment.cpp') -Raw
$pipeCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\PipeChannel.cpp') -Raw
$pipeHeader = Get-Content -LiteralPath (Join-Path $repo 'include\PipeChannel.h') -Raw
$pipeServerHeader = Get-Content -LiteralPath (Join-Path $repo 'include\PipeServer.h') -Raw
$ipcHeader = Get-Content -LiteralPath (Join-Path $repo 'include\WeaselIPC.h') -Raw
$clientImpl = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\WeaselClientImpl.cpp') -Raw
$serverMain = Get-Content -LiteralPath (Join-Path $repo 'WeaselServer\WeaselServer.cpp') -Raw
$serverApp = Get-Content -LiteralPath (Join-Path $repo 'WeaselServer\WeaselServerApp.cpp') -Raw
$systemTray = Get-Content -LiteralPath (Join-Path $repo 'WeaselServer\SystemTraySDK.cpp') -Raw
$serverImpl = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPCServer\WeaselServerImpl.cpp') -Raw
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$languageBar = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\LanguageBar.cpp') -Raw
$textEditSink = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\TextEditSink.cpp') -Raw
$registerCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Register.cpp') -Raw
$d2d = Get-Content -LiteralPath (Join-Path $repo 'WeaselUI\d2d.cpp') -Raw
$configurator = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\Configurator.cpp') -Raw
$deployerMain = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\WeaselDeployer.cpp') -Raw
$fontSettingsDialog = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\FontSettingDialog.cpp') -Raw
$dictManagementDialog = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\DictManagementDialog.cpp') -Raw
$switcherDialog = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\SwitcherSettingsDialog.cpp') -Raw
$serverCommandsTest = Get-Content -LiteralPath (Join-Path $repo 'test\TestServerCommands\TestServerCommands.cpp') -Raw
$serverCommandsXmake = Get-Content -LiteralPath (Join-Path $repo 'test\TestServerCommands\xmake.lua') -Raw
$weaselTsfHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.h') -Raw
$serverImplHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPCServer\WeaselServerImpl.h') -Raw
$rimeWithWeasel = Get-Content -LiteralPath (Join-Path $repo 'RimeWithWeasel\RimeWithWeasel.cpp') -Raw

foreach ($sink in @(
  '_pKeyboardCompartmentSink',
  '_pConvertionCompartmentSink',
  '_pKeyboardDisabledSink',
  '_pEmptyContextSink')) {
  $assignPattern = [regex]::Escape($sink) + '\s*=\s*new\s+CCompartmentEventSink'
  if ($compartment -match $assignPattern) {
    throw "$sink still assigns a freshly-created COM object through CComPtr::operator= (extra AddRef leak)"
  }
  $attachPattern = [regex]::Escape($sink) + '\.Attach\(new\s+CCompartmentEventSink'
  if ($compartment -notmatch $attachPattern) {
    throw "$sink is not owned with CComPtr::Attach"
  }
}

$setOpen = [regex]::Match(
  $compartment,
  'HRESULT\s+WeaselTSF::_SetKeyboardOpen\(BOOL\s+fOpen\)\s*\{(?<body>.*?)\n\}',
  [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $setOpen.Success) { throw 'unable to locate WeaselTSF::_SetKeyboardOpen' }
if ($setOpen.Groups['body'].Value -notmatch 'com_ptr<ITfCompartment>\s+pCompartment') {
  throw '_SetKeyboardOpen must keep the GetCompartment reference in com_ptr'
}
if ($setOpen.Groups['body'].Value -match 'ITfCompartment\s*\*\s*pCompartment') {
  throw '_SetKeyboardOpen still owns the GetCompartment reference as a raw pointer'
}

if ($pipeCpp -notmatch '!result\.error\s*&&\s*result\.bytes\s*!=\s*rec_len') {
  throw 'PipeChannelBase::_Receive must reject successful short fixed-header reads'
}
if ($pipeHeader -notmatch '_TyRes\s+result\s*\{\s*\}\s*;') {
  throw 'PipeChannel::_ReceiveResponse must zero-initialize its scalar response'
}

if ($serverCommandsTest -match '(?m)^\s*#define\s+private\s+public\s*$') {
  throw 'TestServerCommands must use scoped friend accessors instead of macroizing private'
}
if ($serverCommandsXmake -match 'add_forceincludes\s*\(') {
  throw 'TestServerCommands must not force-include a private-access shim into every translation unit'
}
if ($serverCommandsXmake -notmatch 'add_defines\("WEASEL_TEST_DISABLE_SERVER_LAUNCH"\)') {
  throw 'TestServerCommands must compile out the real server launcher'
}
if ($tsf -notmatch 'void\s+WeaselTSF::_ScheduleServerRecovery\(\)\s+noexcept\s*\{\s*#if !defined\(WEASEL_TEST_DISABLE_SERVER_LAUNCH\)') {
  throw 'the test build must disable recovery scheduling before background launch work'
}
if ($weaselTsfHeader -notmatch 'friend\s+class\s+WeaselTSFTestAccess\s*;') {
  throw 'WeaselTSF must grant access only to the dedicated test accessor'
}
if ($serverImplHeader -notmatch 'friend\s+class\s+ServerImplTestAccess\s*;') {
  throw 'ServerImpl must grant access only to the dedicated test accessor'
}
if ($serverCommandsTest -notmatch 'WeaselTSFTestAccess::ProcessKeyEventWithRecovery') {
  throw 'TestServerCommands must route private key recovery checks through WeaselTSFTestAccess'
}
if ($serverCommandsTest -notmatch 'ServerImplTestAccess::HandlePipeMessage') {
  throw 'TestServerCommands must route private server dispatch checks through ServerImplTestAccess'
}

$resizeStart = $d2d.IndexOf('void D2D::OnResize')
$resizeEnd = $d2d.IndexOf('std::vector<std::wstring> ws_split', $resizeStart)
if ($resizeStart -lt 0 -or $resizeEnd -le $resizeStart) {
  throw 'unable to isolate D2D::OnResize for recovery policy checks'
}
$resizeBody = $d2d.Substring($resizeStart, $resizeEnd - $resizeStart)
if ($resizeBody -notmatch 'swapChain->ResizeBuffers\([\s\S]*?\);\s*(?://[^\r\n]*\s*)*HR\(hr\);' -or
    $resizeBody -match 'RecoverAfterDeviceLoss\(\)') {
  throw 'D2D::OnResize must propagate resize failures to the panel for complete font and layout recovery'
}
if ($resizeBody -match 'RecreateAfterDeviceLoss') {
  throw 'D2D::OnResize still duplicates shared generation rollover logic'
}

$updateStart = $configurator.IndexOf('int Configurator::UpdateWorkspace')
$updateEnd = $configurator.IndexOf('int Configurator::DictManagement', $updateStart)
if ($updateStart -lt 0 -or $updateEnd -le $updateStart) {
  throw 'unable to isolate Configurator::UpdateWorkspace'
}
$updateBody = $configurator.Substring($updateStart, $updateEnd - $updateStart)
if ($updateBody -notmatch 'if\s*\(!rime->deploy\(\)\)') {
  throw 'UpdateWorkspace must treat rime->deploy() failure as an operation failure'
}
if ($updateBody -notmatch 'if\s*\(!rime->deploy_config_file\("weasel\.yaml",\s*"config_version"\)\)') {
  throw 'UpdateWorkspace must treat weasel.yaml deployment failure as an operation failure'
}
if ($updateBody -match 'rime->join_maintenance_thread\(\)') {
  throw 'UpdateWorkspace uses synchronous librime deployment tasks and must not retain an unbounded maintenance join'
}
if ($updateBody -notmatch 'return\s+retval\s*;') {
  throw 'UpdateWorkspace must return its accumulated deployment result'
}

if ($configurator -notmatch 'enum\s+class\s+ConfigureResult\s*\{\s*Cancelled\s*,\s*Unchanged\s*,\s*Saved\s*,\s*Failed\s*\}') {
  throw 'deployer settings steps must distinguish cancelled, unchanged, saved, and failed outcomes'
}
if ($configurator -notmatch 'bool\s+Finish\(\)' -or
    $configurator -notmatch 'client_\.EndMaintenance\(\)' -or
    $configurator -notmatch '!releaser\.Finish\(\)' -or
    $clientImpl -notmatch 'bool\s+ClientImpl::EndMaintenance\(\)' -or
    $clientImpl -notmatch 'ServerRuntimeIsReady\(\)' -or
    $serverImpl -notmatch 'OnEndMaintenance[\s\S]*?_TryTransitionRuntimeLifecycle[\s\S]*?\?\s*1\s*:\s*0') {
  throw 'deployer maintenance completion must be acknowledged, compatibility-checked, and propagated'
}

if ($rimeWithWeasel -notmatch 'void\s+RimeWithWeaselHandler::_Initialize\(bool\s+detect_deployer\)' -or
    $rimeWithWeasel -notmatch 'm_disabled\s*=\s*true[\s\S]*?detect_deployer\s*&&\s*_IsDeployerRunning\(\)') {
  throw 'Rime initialization must separate startup deployer detection from maintenance resume'
}
$rimeInitStart = $rimeWithWeasel.IndexOf('void RimeWithWeaselHandler::_Initialize(bool detect_deployer)')
$rimeFinalizeStart = $rimeWithWeasel.IndexOf('void RimeWithWeaselHandler::Finalize()', $rimeInitStart)
if ($rimeInitStart -lt 0 -or $rimeFinalizeStart -le $rimeInitStart) {
  throw 'unable to isolate RimeWithWeaselHandler::_Initialize'
}
$rimeInitBody = $rimeWithWeasel.Substring(
  $rimeInitStart,
  $rimeFinalizeStart - $rimeInitStart
)
if ($rimeInitBody -match 'start_maintenance\s*\(' -or
    $rimeInitBody -match 'join_maintenance_thread\s*\(') {
  throw 'WeaselServer startup must not run synchronous librime maintenance before IPC Ready'
}
if ($rimeInitBody -match 'rime_api->create_session\s*\(') {
  throw 'pre-IPC server startup must not create a real Rime session'
}
if ($rimeInitBody -notmatch 'm_disabled\s*=\s*false') {
  throw 'Rime initialization must enable START_SESSION after non-session setup completes'
}
if ($ipcHeader -notmatch 'kDeployerExclusiveMutexName\[\][\s\S]*?WeaselDeployerExclusiveMutex' -or
    $rimeWithWeasel -notmatch 'OpenMutexW\(SYNCHRONIZE,\s*FALSE,\s*weasel::kDeployerExclusiveMutexName\)' -or
    $deployerMain -notmatch 'CreateMutex\(NULL,\s*TRUE,\s*weasel::kDeployerExclusiveMutexName\)') {
  throw 'WeaselServer and WeaselDeployer must share one deployer singleton name'
}
if ($rimeWithWeasel -match 'WeaselDeployerMutex') {
  throw 'server startup must not probe the obsolete WeaselDeployerMutex name'
}
$endMaintenance = [regex]::Match(
  $rimeWithWeasel,
  'void\s+RimeWithWeaselHandler::EndMaintenance\(\)\s*\{(?<body>[\s\S]*?)\n\}',
  [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $endMaintenance.Success -or
    $endMaintenance.Groups['body'].Value -notmatch '_Initialize\(false\)') {
  throw 'external END_MAINTENANCE must bypass the startup deployer probe'
}
if ($serverImpl -notmatch 'IsReadyForSessions\(\)' -or
    $serverImplHeader -notmatch 'm_maintenanceOwner' -or
    $serverImpl -notmatch 'maintenance owner exited; recovering WeaselServer' -or
    $serverImpl -notmatch '\*m_dispatchProcessOwner\s*!=\s*\*m_maintenanceOwner') {
  throw 'server Ready and Maintenance states must be backend-ready and process-generation owned'
}

# Tray quit from a pipe worker must use the active stop-control API name.
# This prevents an old helper name surviving a shutdown refactor and breaking
# the entire Windows build with C3861.
if ($serverImpl -match '_RequestStopUnderApiLock' -or
    $serverImplHeader -notmatch 'int\s+_RequestStop\(\)\s+noexcept\s*;' -or
    $serverImpl -notmatch 'ServerImpl::OnCommand\(WEASEL_IPC_COMMAND[\s\S]*?ID_WEASELTRAY_QUIT\)[\s\S]*?_RequestStop\(\);') {
  throw 'IPC tray quit must call the current non-blocking _RequestStop helper'
}

$switcherStart = $configurator.IndexOf('static ConfigureResult configure_switcher')
$uiStart = $configurator.IndexOf('static ConfigureResult configure_ui', $switcherStart)
$runStart = $configurator.IndexOf('int Configurator::Run', $uiStart)
if ($switcherStart -lt 0 -or $uiStart -le $switcherStart -or $runStart -le $uiStart) {
  throw 'unable to isolate Configurator settings helper functions'
}
$helperBodies = @{
  configure_switcher = $configurator.Substring($switcherStart, $uiStart - $switcherStart)
  configure_ui = $configurator.Substring($uiStart, $runStart - $uiStart)
}
foreach ($functionName in $helperBodies.Keys) {
  $body = $helperBodies[$functionName]
  if ($body -notmatch '!api->load_settings\(settings\)[\s\S]*?ConfigureResult::Failed') {
    throw "$functionName must propagate load_settings failure"
  }
  if ($body -notmatch '!api->save_settings\(settings\)[\s\S]*?ConfigureResult::Unchanged') {
    throw "$functionName must treat save_settings(false) as unchanged"
  }
  if ($body -notmatch 'ConfigureResult::Cancelled' -or
      $body -notmatch 'ConfigureResult::Unchanged' -or
      $body -notmatch 'ConfigureResult::Saved') {
    throw "$functionName must preserve cancel, unchanged, and saved outcomes separately"
  }
}

$runEnd = $configurator.IndexOf('int Configurator::UpdateWorkspace', $runStart)
if ($runEnd -le $runStart) {
  throw 'unable to isolate Configurator::Run'
}
$runBody = $configurator.Substring($runStart, $runEnd - $runStart)
if ($runBody -notmatch 'bool\s+config_failed\s*=\s*false') {
  throw 'Configurator::Run must remember settings load/save failures'
}
if ($runBody -notmatch 'ConfigureResult::Saved[\s\S]*?reconfigured\s*=\s*true') {
  throw 'Configurator::Run must deploy successfully saved settings'
}
if ($runBody -notmatch 'ConfigureResult::Failed[\s\S]*?config_failed\s*=\s*true') {
  throw 'Configurator::Run must preserve a failed settings result'
}
if ($runBody -notmatch 'return\s+\(config_failed\s*\|\|\s*deploy_result\s*!=\s*0\)\s*\?\s*1\s*:\s*0\s*;') {
  throw 'Configurator::Run must return failure when either configuration or deployment failed'
}

if ($systemTray -notmatch 'm_hInstance\s*=\s*NULL' -or
    $systemTray -notmatch 'm_hWnd\s*=\s*NULL' -or
    $systemTray -notmatch 'm_hTargetWnd\s*=\s*NULL' -or
    $systemTray -notmatch 'm_nCurrentIcon\s*=\s*0' -or
    $systemTray -notmatch 'm_StartTime\s*=\s*0' -or
    $systemTray -notmatch 'm_nAnimationPeriod\s*=\s*0') {
  throw 'CSystemTray must initialize every handle and animation field before Create() can fail'
}

$comboRead = [regex]::Match(
  $fontSettingsDialog,
  'wstring\s+FontSettingDialog::GetComboBoxSelectStr\(HWND\s+target\)\s*\{(?<body>[\s\S]*?)\n\}',
  [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $comboRead.Success -or
    $comboRead.Groups['body'].Value -match 'wchar_t\s+buffer\s*\[4096\]' -or
    $comboRead.Groups['body'].Value -notmatch 'CB_GETLBTEXTLEN' -or
    $comboRead.Groups['body'].Value -notmatch 'std::vector<wchar_t>\s+buffer' -or
    $comboRead.Groups['body'].Value -notmatch 'buffer\.data\(\)') {
  throw 'FontSettingDialog combo-box reads must size storage from CB_GETLBTEXTLEN'
}

$threadFocusUninit = [regex]::Match(
  $tsf,
  'void\s+WeaselTSF::_UninitThreadFocusSink\(\)\s*\{(?<body>[\s\S]*?)\n\}',
  [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $threadFocusUninit.Success -or
    $threadFocusUninit.Groups['body'].Value -notmatch '_dwThreadFocusSinkCookie\s*==\s*TF_INVALID_COOKIE' -or
    $threadFocusUninit.Groups['body'].Value -notmatch 'const\s+DWORD\s+cookie\s*=\s*_dwThreadFocusSinkCookie' -or
    $threadFocusUninit.Groups['body'].Value -notmatch '_dwThreadFocusSinkCookie\s*=\s*TF_INVALID_COOKIE' -or
    $threadFocusUninit.Groups['body'].Value -notmatch 'UnadviseSink\(cookie\)') {
  throw 'thread-focus sink cleanup must invalidate the stored cookie before touching a new source'
}

$textSinkInit = [regex]::Match(
  $textEditSink,
  'BOOL\s+WeaselTSF::_InitTextEditSink\(com_ptr<ITfDocumentMgr>\s+pDocMgr\)\s*\{(?<body>[\s\S]*?)\n\}',
  [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $textSinkInit.Success -or
    $textSinkInit.Groups['body'].Value -notmatch 'AdviseSink\(IID_ITfTextEditSink' -or
    $textSinkInit.Groups['body'].Value -notmatch '_dwTextEditSinkCookie\s*=\s*TF_INVALID_COOKIE[\s\S]*?_pTextEditSinkContext\s*=\s*nullptr[\s\S]*?return\s+FALSE' -or
    $textSinkInit.Groups['body'].Value -notmatch 'AdviseSink\(IID_ITfTextLayoutSink' -or
    $textSinkInit.Groups['body'].Value -notmatch '_dwTextLayoutSinkCookie\s*=\s*TF_INVALID_COOKIE[\s\S]*?return\s+TRUE') {
  throw 'text-edit sink must be mandatory while layout notifications remain optional'
}
if ($tsf -notmatch 'GetFocus\(&pDocMgrFocus\)[\s\S]*?!_InitTextEditSink\(pDocMgrFocus\)[\s\S]*?goto\s+ExitError') {
  throw 'activation must fail when the focused document cannot establish its text-edit sink'
}

if ($dictManagementDialog -match 'WideCharToMultiByte\(CP_UTF8[\s\S]{0,160}selected_path' -or
    $dictManagementDialog -match 'char\s+path\s*\[MAX_PATH\]' -or
    $dictManagementDialog -notmatch 'const\s+std::string\s+path\s*=\s*wtou8\(selected_path\)') {
  throw 'dictionary file-dialog paths must remain dynamically sized UTF-8 strings'
}
if ($switcherDialog -match 'WCHAR\s+parameters\s*\[MAX_PATH' -or
    $switcherDialog -notmatch 'const\s+std::wstring\s+parameters' -or
    $switcherDialog -notmatch 'parameters\.c_str\(\)') {
  throw 'schema-installer command lines must not fall back to MAX_PATH storage'
}

if ($deployerMain -notmatch 'struct\s+RuntimeCleanup' -or
    $deployerMain -notmatch 'if\s*\(FAILED\(hRes\)\)[\s\S]*?CoInitialize failed' -or
    $deployerMain -notmatch 'cleanup\.com_initialized\s*=\s*true' -or
    $deployerMain -notmatch 'ATL module initialization failed' -or
    $deployerMain -notmatch 'cleanup\.module_initialized\s*=\s*true' -or
    $deployerMain -match 'ATLASSERT\(SUCCEEDED\(hRes\)\)') {
  throw 'WeaselDeployer must handle COM/ATL initialization failures at runtime with scoped cleanup'
}

if ($registerCpp -notmatch 'std::wstring\s+GetModulePath\(HMODULE\s+module\)' -or
    $registerCpp -notmatch 'std::vector<wchar_t>\s+buffer\(capacity\)' -or
    $registerCpp -match 'WCHAR\s+achIconFile\s*\[MAX_PATH\]' -or
    $registerCpp -match 'char\s+achFileName\s*\[MAX_PATH\]' -or
    $registerCpp -match 'GetModuleFileNameA\(' -or
    $registerCpp -notmatch 'module_path_data\s*=\s*[\s\S]*?module_path\.c_str\(\)' -or
    $registerCpp -notmatch 'RegSetValueExW\([\s\S]*?module_path_data[\s\S]*?module_path_bytes') {
  throw 'TSF profile/COM registration must keep module paths dynamically sized and Unicode'
}

# WinSparkle setup, manual update checks, and cleanup must share one lifetime.
if ($serverMain -match 'WeaselServerApp::check_update\(\)' -or
    $serverMain -notmatch 'app\.Run\(check_updates\)') {
  throw '/update must be dispatched through the initialized WeaselServerApp lifetime'
}
$serverRun = [regex]::Match(
  $serverApp,
  'int\s+WeaselServerApp::Run\(bool\s+check_updates\)\s*\{(?<body>[\s\S]*?)\n\}',
  [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $serverRun.Success) {
  throw 'unable to locate WeaselServerApp::Run(bool)'
}
$serverRunBody = $serverRun.Groups['body'].Value
$feedSetup = $serverRunBody.IndexOf('win_sparkle_set_appcast_url(update_feed_url())')
$initSparkle = $serverRunBody.IndexOf('win_sparkle_init()')
$manualCheck = $serverRunBody.IndexOf('check_update()', $initSparkle)
if ($feedSetup -lt 0 -or $initSparkle -le $feedSetup -or
    $manualCheck -le $initSparkle -or
    $serverApp -notmatch 'class\s+WinSparkleScope' -or
    $serverApp -notmatch '~WinSparkleScope\(\)[\s\S]*?win_sparkle_cleanup\(\)') {
  throw 'WinSparkle feed/setup must precede init, manual checks must follow init, and cleanup must be scoped'
}

# Once singleton/Starting is published, make the control pipe reachable before
# every potentially blocking Rime/UI/shell startup operation.
if ($serverImplHeader -notmatch 'SetBeforeReadyCallback\(std::function<void\(\)> callback\)' -or
    $ipcHeader -notmatch 'SetBeforeReadyCallback\(std::function<void\(\)> callback\)') {
  throw 'server API must expose a before-Ready callback for post-listener startup work'
}
$runImplStart = $serverImpl.IndexOf('int ServerImpl::Run()')
$stopRuntimeStart = $serverImpl.IndexOf('void ServerImpl::_StopRuntime()', $runImplStart)
if ($runImplStart -lt 0 -or $stopRuntimeStart -le $runImplStart) {
  throw 'unable to isolate ServerImpl::Run startup ordering'
}
$runImpl = $serverImpl.Substring($runImplStart, $stopRuntimeStart - $runImplStart)
$listenerStart = $runImpl.IndexOf('pipeThread = std::make_unique<boost::thread>')
$pipeReady = $runImpl.IndexOf('WaitForPrimaryPipeReady()')
$beforeReady = $runImpl.IndexOf('m_beforeReadyCallback()')
$backendReady = $runImpl.IndexOf('m_pRequestHandler->IsReadyForSessions()', $beforeReady)
$publishReady = $runImpl.IndexOf('_TryTransitionRuntimeLifecycle(ServerLifecycle::Starting,')
if ($listenerStart -lt 0 -or $pipeReady -lt 0 -or $beforeReady -lt 0 -or
    $backendReady -lt 0 -or $publishReady -lt 0 -or
    -not ($listenerStart -lt $pipeReady -and $pipeReady -lt $beforeReady -and
          $beforeReady -lt $backendReady -and $backendReady -lt $publishReady)) {
  throw 'pipe must precede application startup and backend readiness must precede Ready publication'
}
if ($runImpl -notmatch '_RuntimeLifecycle\(\)\s*==\s*ServerLifecycle::Starting[\s\S]*?msg\.Msg\s*!=\s*WEASEL_IPC_SHUTDOWN_SERVER[\s\S]*?dispatch\s*=\s*false') {
  throw 'Starting listener must reject every data-plane command while retaining shutdown control'
}
if (([regex]::Matches($serverApp, 'm_handler->Initialize\(\)')).Count -ne 1 -or
    ([regex]::Matches($serverApp, 'm_ui\.Create\(')).Count -ne 1 -or
    ([regex]::Matches($serverApp, 'tray_icon\.Create\(')).Count -ne 1 -or
    $serverApp -notmatch 'SetBeforeReadyCallback\([\s\S]*?win_sparkle_init\(\)[\s\S]*?m_ui\.Create\(m_server\.GetHWnd\(\)\)[\s\S]*?m_handler->Initialize\(\)[\s\S]*?tray_icon\.Create\(m_server\.GetHWnd\(\)\)[\s\S]*?m_server\.Run\(\)') {
  throw 'WinSparkle UI Rime and tray startup must all execute only after the primary pipe is listening'
}
if ($languageBar -match 'start_service\.bat' -or
    $languageBar -notmatch 'ID_WEASELTRAY_RERUN_SERVICE[\s\S]*?QueueDllBackgroundTask[\s\S]*?WeaselServer\.exe') {
  throw 'language-bar restart must launch WeaselServer directly instead of relying on a batch shell layer'
}

# A restart must wait for the old process, not merely for its pipe to vanish:
# ServerImpl keeps the singleton mutex until process teardown completes.
$restartStart = $serverMain.IndexOf('// Retain the existing process for both /quit and a normal restart.')
$restartEnd = $serverMain.IndexOf('  const bool check_updates =', $restartStart)
if ($restartStart -lt 0 -or $restartEnd -le $restartStart) {
  throw 'unable to isolate WeaselServer restart flow'
}
$restartFlow = $serverMain.Substring($restartStart, $restartEnd - $restartStart)
$openProcess = $restartFlow.IndexOf('OpenRunningServerProcess(running_server, running_window,')
$shutdown = $restartFlow.IndexOf('client.ShutdownServer()')
$disconnect = $restartFlow.IndexOf('client.Disconnect()', $shutdown)
$waitExit = $restartFlow.IndexOf('WaitForRunningServerExit(running_server)', $disconnect)
if ($openProcess -lt 0 -or $shutdown -lt 0 -or $disconnect -lt 0 -or $waitExit -lt 0 -or
    -not ($openProcess -lt $shutdown -and $shutdown -lt $disconnect -and $disconnect -lt $waitExit)) {
  throw 'normal server restart must retain the old process handle, disconnect the pipe, and wait for process exit'
}
if ($restartFlow -match 'if\s*\(quit\s*&&\s*!OpenRunningServerProcess') {
  throw 'old-process tracking must apply to normal restart as well as /quit'
}
if ($serverMain -match 'FindWindowW\(nullptr,\s*WEASEL_IPC_WINDOW\)' -or
    $serverMain -notmatch 'ReadServerRuntimeState\(' -or
    $serverMain -notmatch 'state\.process_creation_time' -or
    $serverMain -notmatch 'ProcessIdToSessionId\(' -or
    $serverMain -notmatch 'QueryFullProcessImageNameW\(' -or
    $serverMain -notmatch 'FindServerWindowForProcess\(') {
  throw 'server restart target must be authenticated by runtime generation, session, and image before using its window'
}
if ($restartFlow -match 'while\s*\(client\.Connect\(\)') {
  throw 'restart must not treat pipe disappearance as proof that the old server process exited'
}
if ($restartFlow -notmatch 'ServerLifecycleLock\s+restart_lifecycle' -or
    $restartFlow -notmatch '\(connected\s*\|\|\s*running_server\)[\s\S]*?restart_lifecycle\.Acquire\(0\)' -or
    $restartFlow -notmatch 'client\.ShutdownServer\(\)[\s\S]*?WaitForRunningServerExit\(running_server\)') {
  throw 'normal restart must own the shared lifecycle lock while stopping an existing server'
}

if ($serverMain -notmatch 'ForceStopStartingServer\(' -or
    $serverMain -notmatch 'TerminateProcess\(process,\s*ERROR_TIMEOUT\)' -or
    $restartFlow -notmatch 'recovered_starting[\s\S]*?ForceStopStartingServer\(running_server,\s*running_window,[\s\S]*?running_state\)') {
  throw 'explicit restart and quit must recover only an authenticated generation wedged in Starting'
}
if ($tsf -notmatch 'kServerStartingHardTimeoutMs\s*=\s*30\s*\*\s*1000' -or
    $tsf -notmatch 'kServerMaintenanceHardTimeoutMs\s*=\s*15\s*\*\s*60\s*\*\s*1000' -or
    $tsf -notmatch 'kServerStoppingHardTimeoutMs\s*=\s*30\s*\*\s*1000') {
  throw 'Starting and Stopping recovery deadlines must stay short without shortening deploy maintenance'
}

# IPC must be scoped by Windows session while retaining the legacy endpoint for
# already-loaded pre-upgrade TSF clients in the current session.
foreach ($required in @(
  'GetCurrentWindowsSessionId',
  'GetLegacyPipeName',
  'GetLegacyServerInstanceName',
  'GetServerInstanceName',
  'GetServerLifecycleMutexName'
)) {
  if ($ipcHeader -notmatch ([regex]::Escape($required) + '\s*\(')) {
    throw "IPC session scoping is missing $required"
  }
}
if ($ipcHeader -notmatch 'GetPipeName\([\s\S]*?session-[\s\S]*?session_id') {
  throw 'primary named pipe must include the current Windows session id'
}
if ($clientImpl -notmatch 'channel\(GetPipeName\(\),\s*NULL,\s*64\s*\*\s*1024,\s*GetLegacyPipeName\(\)\)') {
  throw 'new clients must fall back to the legacy pipe for upgrade compatibility'
}
if ($serverImpl -notmatch 'PipeServer>\(GetPipeName\(\),\s*sa\.get_attr\(\),\s*GetLegacyPipeName\(\)\)') {
  throw 'server must listen on both the scoped pipe and the legacy compatibility alias'
}
if ($pipeServerHeader -notmatch 'compatibility_alias|m_alias_listener') {
  throw 'PipeServer must retain the compatibility alias while the scoped listener is active'
}

$drainStart = $serverImpl.IndexOf('void PipeServer::DrainWorkers()')
$drainEnd = $serverImpl.IndexOf('void PipeServer::_LaunchWorker', $drainStart)
if ($drainStart -lt 0 -or $drainEnd -le $drainStart) {
  throw 'unable to isolate PipeServer::DrainWorkers'
}
$drainWorkers = $serverImpl.Substring($drainStart, $drainEnd - $drainStart)
$cancelPipe = $drainWorkers.IndexOf('::CancelIoEx(entry.pipe, NULL)')
$disconnect = $drainWorkers.IndexOf('::DisconnectNamedPipe(entry.pipe)')
$join = $drainWorkers.IndexOf('worker->join()')
if ($cancelPipe -lt 0 -or $disconnect -lt 0 -or $join -lt 0 -or
    -not ($cancelPipe -lt $disconnect -and $disconnect -lt $join)) {
  throw 'worker drain must cancel only pipe-handle I/O, disconnect, then join'
}
if ($drainWorkers -match '::CancelSynchronousIo\s*\(' -or
    $serverImpl -match 'SynchronousPipeIoScope|pipe_io_pending' -or
    $pipeServerHeader -match 'pipe_io_pending') {
  throw 'worker shutdown must not use thread-wide I/O cancellation or sampled I/O flags'
}
if ($serverImpl -notmatch 'ERROR_ACCESS_DENIED[\s\S]*?return') {
  throw 'legacy alias listener must stop after a permanent cross-session access denial'
}
if ($serverImpl -notmatch 'const\s+std::wstring\s+instanceName\s*=\s*GetServerInstanceName\(\)' -or
    $serverImpl -notmatch 'CreateMutex\(NULL,\s*FALSE,\s*instanceName\.c_str\(\)\)') {
  throw 'server singleton mutex must use the same Windows-session scope as IPC'
}
if ($tsf -match 'CreateToolhelp32Snapshot|Process32First|Process32Next') {
  throw 'TSF recovery must use the server singleton object instead of polling the process table'
}
if ($tsf -notmatch 'GetServerInstanceName\(\)' -or
    $tsf -notmatch 'GetLegacyServerInstanceName\(\)' -or
    $tsf -notmatch 'OpenMutexW\(SYNCHRONIZE,\s*FALSE,\s*mutex_name\.c_str\(\)\)') {
  throw 'TSF recovery must observe both current and legacy server singleton mutexes'
}
if ($ipcHeader -notmatch 'GetServerLifecycleMutexName\(\)[\s\S]*?session-' -or
    $tsf -notmatch 'ServerLifecycleLock\s+lifecycle_lock') {
  throw 'TSF recovery lifecycle serialization must remain scoped to the current Windows session'
}

Write-Host 'Runtime hardening policy: PASS'
