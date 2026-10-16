$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$compartment = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Compartment.cpp') -Raw
$pipeCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\PipeChannel.cpp') -Raw
$pipeHeader = Get-Content -LiteralPath (Join-Path $repo 'include\PipeChannel.h') -Raw
$d2d = Get-Content -LiteralPath (Join-Path $repo 'WeaselUI\d2d.cpp') -Raw
$configurator = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\Configurator.cpp') -Raw
$serverCommandsTest = Get-Content -LiteralPath (Join-Path $repo 'test\TestServerCommands\TestServerCommands.cpp') -Raw
$serverCommandsXmake = Get-Content -LiteralPath (Join-Path $repo 'test\TestServerCommands\xmake.lua') -Raw
$weaselTsfHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.h') -Raw
$serverImplHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPCServer\WeaselServerImpl.h') -Raw

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

if ($pipeCpp -notmatch 'success\s*&&\s*lread\s*!=\s*rec_len') {
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
if ($resizeBody -notmatch 'RecoverAfterDeviceLoss\(\)') {
  throw 'D2D::OnResize must delegate device-loss rollover to RecoverAfterDeviceLoss'
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
if ($updateBody -notmatch 'rime->join_maintenance_thread\(\)') {
  throw 'UpdateWorkspace must still join librime maintenance work before resuming the server'
}
if ($updateBody -notmatch 'return\s+retval\s*;') {
  throw 'UpdateWorkspace must return its accumulated deployment result'
}

if ($configurator -notmatch 'enum\s+class\s+ConfigureResult\s*\{\s*Cancelled\s*,\s*Saved\s*,\s*Failed\s*\}') {
  throw 'deployer settings steps must distinguish cancelled, saved, and failed outcomes'
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
  if ($body -notmatch '!api->save_settings\(settings\)[\s\S]*?ConfigureResult::Failed') {
    throw "$functionName must propagate save_settings failure"
  }
  if ($body -notmatch 'ConfigureResult::Cancelled' -or $body -notmatch 'ConfigureResult::Saved') {
    throw "$functionName must preserve cancel and saved outcomes separately"
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

Write-Host 'Runtime hardening policy: PASS'
