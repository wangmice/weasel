$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$compartment = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Compartment.cpp') -Raw
$pipeCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\PipeChannel.cpp') -Raw
$pipeHeader = Get-Content -LiteralPath (Join-Path $repo 'include\PipeChannel.h') -Raw

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

Write-Host 'Runtime hardening policy: PASS'
