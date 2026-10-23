$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$compartment = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Compartment.cpp') -Raw
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$header = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.h') -Raw

function Assert-Policy([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw $Message }
}

$disabledStart = $compartment.IndexOf('BOOL WeaselTSF::_IsKeyboardDisabled()')
$disabledEnd = $compartment.IndexOf('void WeaselTSF::_RetargetKeyboardDisabledSinks(', $disabledStart)
Assert-Policy ($disabledStart -ge 0 -and $disabledEnd -gt $disabledStart) 'cannot isolate keyboard-disabled query'
$disabled = $compartment.Substring($disabledStart, $disabledEnd - $disabledStart)
Assert-Policy ($disabled -match 'GUID_COMPARTMENT_KEYBOARD_DISABLED[\s\S]*?fDisabled\s*=\s*\(var\.lVal\s*!=\s*0\)' ) 'KEYBOARD_DISABLED must be independently read'
Assert-Policy ($disabled -match 'GUID_COMPARTMENT_EMPTYCONTEXT[\s\S]*?fDisabled\s*=\s*fDisabled\s*\|\|\s*\(var\.lVal\s*!=\s*0\)' ) 'EMPTYCONTEXT must OR with KEYBOARD_DISABLED, never overwrite it'

$requestStart = $compartment.IndexOf('HRESULT WeaselTSF::_RequestKeyboardOpenDeferred()')
$applyStart = $compartment.IndexOf('HRESULT WeaselTSF::_ApplyDeferredKeyboardOpen(', $requestStart)
$getStart = $compartment.IndexOf('HRESULT WeaselTSF::_GetCompartmentDWORD(', $applyStart)
Assert-Policy ($requestStart -ge 0 -and $applyStart -gt $requestStart -and $getStart -gt $applyStart) 'cannot isolate deferred keyboard request/apply'
$request = $compartment.Substring($requestStart, $applyStart - $requestStart)
$apply = $compartment.Substring($applyStart, $getStart - $applyStart)
Assert-Policy ($request -match 'RequestEditSession\([\s\S]*?TF_ES_ASYNC\s*\|\s*TF_ES_READ' -and $request -notmatch 'TF_ES_ASYNCDONTCARE') 'keyboard recovery must be genuinely asynchronous'
Assert-Policy ($request -match 'const\s+HRESULT\s+result\s*=\s*FAILED\(hr\)\s*\?\s*hr\s*:\s*hrSession' -and
  $request -match 'if\s*\(FAILED\(result\)' ) 'both edit-session HRESULTs must be checked before rearming'
Assert-Policy ($compartment -match 'class CForceKeyboardOpenEditSession[\s\S]*?generation_\(generation\)[\s\S]*?requestSerial_\(requestSerial\)[\s\S]*?_ApplyDeferredKeyboardOpen\([\s\S]*?requestSerial_' ) 'deferred callback must carry activation and request identities'
Assert-Policy ($apply -match '_SetKeyboardOpen\(TRUE\)' -and
  $apply -match 'if\s*\(FAILED\(hr\)\)' -and
  $apply -match '_ScheduleKeyboardOpenRetry\(\)' -and
  $apply -notmatch 'm_client\.TrayCommand|_HandleLangBarMenuSelect|m_client\.ClearComposition' ) 'edit session must only reopen OPENCLOSE and schedule completion/retry'
Assert-Policy ($apply -match '_fSuppressOpenCloseSelfWrite\s*=\s*priorSuppression') 'self-write suppression must restore its prior state'
Assert-Policy ($header -match '_keyboardOpenGeneration\s*=\s*0' -and
  ([regex]::Matches($tsf, '\+\+_keyboardOpenGeneration')).Count -ge 2 -and
  $apply -match 'generation\s*!=\s*_keyboardOpenGeneration') 'stale activation callbacks must be rejected'
Assert-Policy ($header -match '_keyboardOpenRequestSerial\s*=\s*0' -and
  $request -match '_keyboardOpenPending\s*=\s*true' -and
  $apply -match 'requestSerial\s*!=\s*_keyboardOpenRequestSerial' -and
  $apply -match 'focused\s*!=\s*requestContext' -and
  $apply -match 'if\s*\(_IsKeyboardOpen\(\)\)') 'deduplicate callbacks and reject stale focus/request generations'
Assert-Policy ($compartment -match 'kKeyboardOpenMaxRetries\s*=\s*3' -and
  $compartment -match 'kKeyboardOpenPendingRetryMs\s*=\s*2000' -and
  $compartment -match 'void WeaselTSF::_OnKeyboardOpenTimer\(\)' -and
  $compartment -match 'if\s*\(_keyboardOpenPending\)\s*\{[\s\S]*?elapsed\s*<\s*kKeyboardOpenPendingRetryMs[\s\S]*?_ScheduleKeyboardOpenRetry\(\);\s*return;' -and
  $compartment -match '_ScheduleKeyboardOpenRetry\(\)' -and
  $compartment -match 'void WeaselTSF::_CancelKeyboardOpenRequest\(\)' ) 'keyboard recovery retries must be bounded and cancelable'
Assert-Policy ($compartment -match 'void WeaselTSF::_CommitDeferredKeyboardOpen\(\)' -and
  $compartment -match 'PostMessageW\(_keyboardOpenWindow,\s*kKeyboardOpenCommitMessage' -and
  $compartment -match '_HandleLangBarMenuSelect\(_status\.ascii_mode' ) 'mode synchronization must occur in a posted callback after the edit session'
Assert-Policy ($tsf -match 'WeaselTSF::~WeaselTSF\(\)\s*\{\s*_DestroyKeyboardOpenWindow\(\)' -and
  $tsf -match 'STDMETHODIMP WeaselTSF::Deactivate\(\)[\s\S]*?_DestroyKeyboardOpenWindow\(\)' ) 'keyboard completion window must be destroyed at teardown'
$handlerStart = $compartment.IndexOf('HRESULT WeaselTSF::_HandleCompartment(')
$handler = $compartment.Substring($handlerStart)
Assert-Policy ($handler -match '_RequestKeyboardOpenDeferred\(\)' -and
  $handler -notmatch '_status\.ascii_mode\s*=\s*!_status\.ascii_mode') 'OnChange must not apply the mode before recovery completes'
Write-Host 'Keyboard compartment recovery policy: PASS'
