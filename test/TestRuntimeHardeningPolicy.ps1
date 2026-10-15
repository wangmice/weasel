$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$compartment = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Compartment.cpp') -Raw
$pipeCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\PipeChannel.cpp') -Raw
$pipeHeader = Get-Content -LiteralPath (Join-Path $repo 'include\PipeChannel.h') -Raw
$rimeWithWeasel = Get-Content -LiteralPath (Join-Path $repo 'RimeWithWeasel\RimeWithWeasel.cpp') -Raw
$d2d = Get-Content -LiteralPath (Join-Path $repo 'WeaselUI\d2d.cpp') -Raw

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

if ($rimeWithWeasel -match '\(1\s*<<\s*i\)') {
  throw 'session-id seed still performs signed 1 << 31'
}
if ($rimeWithWeasel -notmatch '(1u|DWORD\s*\{\s*1\s*\})\s*<<\s*i') {
  throw 'session-id seed does not use an unsigned bit mask'
}

$resize = [regex]::Match(
  $d2d,
  'void\s+D2D::OnResize\(UINT\s+width,\s*UINT\s+height\)\s*\{(?<body>.*?)\n\}\n\nstd::vector<std::wstring>',
  [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $resize.Success) { throw 'unable to locate D2D::OnResize' }
if ($resize.Groups['body'].Value -notmatch 'RecoverAfterDeviceLoss\(\)') {
  throw 'D2D::OnResize must reuse RecoverAfterDeviceLoss for generation rollover'
}
if ($resize.Groups['body'].Value -match 'RecreateAfterDeviceLoss') {
  throw 'D2D::OnResize still duplicates the generation rollover state machine'
}

Write-Host 'Runtime hardening policy: PASS'
