param(
  [string]$DllMainPath = (Join-Path $PSScriptRoot '..\WeaselTSF\dllmain.cpp'),
  [string]$ClientPath = (Join-Path $PSScriptRoot '..\WeaselIPC\WeaselClientImpl.cpp'),
  [string]$CompositionPath = (Join-Path $PSScriptRoot '..\WeaselTSF\Composition.cpp'),
  [string]$KeyEventSinkPath = (Join-Path $PSScriptRoot '..\WeaselTSF\KeyEventSink.cpp')
)

$dllMainSource = Get-Content -LiteralPath $DllMainPath -Raw
$forbidden = @(
  'SetUnhandledExceptionFilter',
  'MiniDumpWriteDump',
  'DbgHelp'
)

$hits = @($forbidden | Where-Object { $dllMainSource.Contains($_) })
if ($hits.Count -ne 0) {
  throw "TSF host-safety regression: process-wide crash handling found: $($hits -join ', ')"
}

$clientSource = Get-Content -LiteralPath $ClientPath -Raw
if (-not $clientSource.Contains('GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS') -or
    -not $clientSource.Contains('GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT')) {
  throw 'TSF host-safety regression: current-module probing must not increment the loader reference count'
}

$compositionSource = Get-Content -LiteralPath $CompositionPath -Raw
$directCommitPattern = [regex]::Escape('return pInsertAtSelection->InsertTextAtSelection(') +
  '\s*ec,\s*0,\s*text\.c_str\(\),\s*static_cast<LONG>\(text\.length\(\)\),\s*&pRange\);'
if ($compositionSource -notmatch $directCommitPattern) {
  throw 'TSF host-safety regression: direct commits without an active composition must allow TSF default composition'
}

$keyEventSinkSource = Get-Content -LiteralPath $KeyEventSinkPath -Raw
if ($keyEventSinkSource -notmatch 'const\s+bool\s+suppress_simulated_caps\s*=[\s\S]*?_keyCountToSimulate\s*>\s*0[\s\S]*?ke\.keycode\s*==\s*ibus::Caps_Lock[\s\S]*?if\s*\(_keyCountToSimulate\s*&&\s*!suppress_simulated_caps\)[\s\S]*?_keyCountToSimulate\s*=\s*0') {
  throw 'TSF host-safety regression: stale CapsLock simulation must self-clear before a real non-CapsLock key'
}
if ($keyEventSinkSource -notmatch '_keyCountToSimulate\s*=\s*static_cast<int>\([\s\S]*?SendInput' -or
    $keyEventSinkSource -notmatch 'if\s*\(suppress_simulated_caps\s*&&\s*_keyCountToSimulate\)[\s\S]*?_keyCountToSimulate--') {
  throw 'TSF host-safety regression: only pre-existing synthetic CapsLock callbacks may consume the injected-event budget'
}
if ($keyEventSinkSource -notmatch 'OnSetFocus\(BOOL\s+fForeground\)[\s\S]*?_keyCountToSimulate\s*=\s*0') {
  throw 'TSF host-safety regression: focus changes must clear stale CapsLock simulation state'
}

Write-Host 'TSF host-safety policy: PASS'
