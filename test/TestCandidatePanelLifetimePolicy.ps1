$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$candidate = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\CandidateList.cpp') -Raw
$candidateHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\CandidateList.h') -Raw
$tsf = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.cpp') -Raw
$panel = Get-Content -LiteralPath (Join-Path $repo 'WeaselUI\WeaselPanel.cpp') -Raw
$panelHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselUI\WeaselPanel.h') -Raw

if ($candidateHeader -notmatch 'DllLifetimeLease\s+_dllLifetime' -or
    $candidate -notmatch 'DllLifetimeLease::DllLifetimeLease\(\)[\s\S]*?DllAddRef\(\)' -or
    $candidate -notmatch 'DllLifetimeLease::~DllLifetimeLease\(\)[\s\S]*?DllRelease\(\)') {
  throw 'candidate COM object must pin DLL for its full lifetime'
}
if ($candidateHeader -notmatch 'WeaselTSF\*\s+_tsf' -or
    $candidateHeader -match 'com_ptr<WeaselTSF>\s+_tsf') {
  throw 'candidate must not introduce a strong COM cycle back to WeaselTSF'
}
$detachStart = $candidate.IndexOf('void CCandidateList::DetachOwner()')
$nextStart = $candidate.IndexOf('STDMETHODIMP CCandidateList::QueryInterface(', $detachStart)
if ($detachStart -lt 0 -or $nextStart -le $detachStart) {
  throw 'candidate must have explicit owner invalidation'
}
$detach = $candidate.Substring($detachStart, $nextStart - $detachStart)
if ($detach -notmatch 'DestroyAll\(\)' -or
    $detach -notmatch 'SetUICallBack\(\{\}\)' -or
    $detach -notmatch '_tsf\s*=\s*nullptr') {
  throw 'candidate must end UI and callback ownership before dropping TIP'
}
if ($tsf -notmatch 'WeaselTSF::~WeaselTSF\(\)[\s\S]*?_cand->DetachOwner\(\)' -or
    $tsf -notmatch 'WeaselTSF::Deactivate\(\)[\s\S]*?_cand->DetachOwner\(\)' -or
    $tsf -notmatch 'WeaselTSF::ActivateEx\([\s\S]*?_cand->HasOwner\(\)[\s\S]*?new CCandidateList\(this\)') {
  throw 'TIP must detach on deactivate/destruction and allocate fresh candidate upon reactivation'
}
if ($candidate -notmatch 'CCandidateList::Abort\([\s\S]*?if\s*\(!_tsf\)' -or
    $candidate -notmatch 'CCandidateList::FinalizeExactCompositionString\([\s\S]*?if\s*\(!_tsf\)' -or
    $candidate -notmatch 'CCandidateList::StartUI\([\s\S]*?if\s*\(!_tsf') {
  throw 'host-retained candidate interfaces must safely reject detached-owner operations'
}
if ($panel -notmatch 'GetModuleHandleExW\([\s\S]*?GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS' -or
    $panel -notmatch 'reinterpret_cast<LPCWSTR>\(&WeaselPanel::WindowProc\)' -or
    $panel -notmatch 'RegisterClassW\(&wc\)' -or
    $panel -notmatch 'CreateWindowExW\([\s\S]*?MAKEINTATOM\(m_windowClassAtom\)' -or
    $panel -notmatch 'UnregisterClassW\(MAKEINTATOM\(m_windowClassAtom\)' -or
    $panelHeader -notmatch 'HMODULE\s+m_windowClassModule') {
  throw 'window class must be module-owned and unregistered before unloading callback code'
}
if ($panel -notmatch 'SendMessageTimeoutW\(hwnd,\s*WM_WEASEL_DESTROY_OWNER' -or
    $panel -notmatch 'SMTO_ABORTIFHUNG\s*\|\s*SMTO_BLOCK' -or
    $panel -notmatch 'g_panelClassSerial\.fetch_add\(') {
  throw 'panel teardown must synchronize with HWND owner and use generation-unique classes'
}
$ui = Get-Content -LiteralPath (Join-Path $repo 'WeaselUI\WeaselUI.cpp') -Raw
$uiHeader = Get-Content -LiteralPath (Join-Path $repo 'include\WeaselUI.h') -Raw
if ($ui -notmatch 'if\s*\(!pimpl_->panel\.DestroyWindow\(full\)\)[\s\S]*?pimpl_\.release\(\)' -or
    $uiHeader -notmatch 'struct\s+UISharedState' -or
    $panelHeader -notmatch 'shared_ptr<UISharedState>\s+m_uiState') {
  throw 'failed window destruction must preserve panel and referenced state'
}
$langBar = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\LanguageBar.cpp') -Raw
$langBarHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\LanguageBar.h') -Raw
if ($langBarHeader -notmatch 'UIStyle\s+_style;' -or
    $langBarHeader -match 'UIStyle&\s+_style' -or
    $langBar -notmatch '_pLangBarButton->DetachOwner\(\)' -or
    $langBar -notmatch 'UpdateWeaselStatus\(stat,\s*_cand->style\(\)\)') {
  throw 'host-retained language bar must own its style and detach from TIP'
}
if ($panel -match 'GetModuleHandle\(nullptr\);\s*WNDCLASS' -or
    $panel -match 'ERROR_CLASS_ALREADY_EXISTS\)') {
  throw 'window class must not silently reuse an EXE-owned or stale callback'
}
# UI-less host selection must use the real Rime selection/commit path and
# preserve its original document. Unsupported integration must never eat keys.
$finalizeStart = $candidate.IndexOf('STDMETHODIMP CCandidateList::Finalize(void)')
$finalizeEnd = $candidate.IndexOf('STDMETHODIMP CCandidateList::Abort(void)', $finalizeStart)
if ($finalizeStart -lt 0 -or $finalizeEnd -le $finalizeStart) {
  throw 'unable to isolate candidate Finalize'
}
$finalize = $candidate.Substring($finalizeStart, $finalizeEnd - $finalizeStart)
if ($finalize -notmatch '_finalization_started\s*=\s*true' -or
    $finalize -notmatch '_SelectCandidateOnCurrentPage\(' -or
    $finalize -notmatch '_pCandidateUIContext\s*!=\s*_pContextDocument' -or
    $finalize -notmatch 'GetFocus\(' -or
    $finalize -notmatch 'if\s*\(!selected\)' -or
    $finalize -notmatch 'Destroy\(\);[\s\S]*?return S_OK') {
  throw 'UI-less candidate Finalize must commit once to the owning document before closing'
}
if ($candidate -notmatch 'CCandidateList::SetIntegrationStyle\([\s\S]*?E_NOTIMPL' -or
    $candidate -notmatch 'CCandidateList::OnKeyDown\([\s\S]*?\*pIsEaten\s*=\s*FALSE' -or
    $candidate -match 'FinalizeExactCompositionString\(\)[\s\S]*?_AbortComposition\(false\)') {
  throw 'unsupported integrated candidate handling must not claim keys or mutate composition'
}
# Old servers return zero even for completed candidate selections. Requiring a
# nonzero return would break rolling upgrades and existing mouse selection.
if ($candidate -notmatch 'previous_serial\s*=\s*m_client\.ResponseSerial\(\)' -or
    $candidate -notmatch 'm_client\.SelectCandidateOnCurrentPage\(index\)' -or
    $candidate -notmatch 'return _parsedSerial\s*==\s*m_client\.ResponseSerial\(\)') {
  throw 'candidate selection must use a fresh parsed response, not legacy zero IPC return values'
}

Write-Host 'Candidate COM and panel window-class lifetime policy: PASS'
