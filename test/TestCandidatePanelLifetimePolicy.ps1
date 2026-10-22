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
if ($panel -notmatch 'DestroyWindow\(\)[\s\S]*?if\s*\(!::DestroyWindow\(hwnd\)\s*&&\s*::IsWindow\(hwnd\)\)[\s\S]*?SetWindowLongPtrW\(hwnd,\s*GWLP_USERDATA,\s*0\)') {
  throw 'failed window destruction must remove stale panel user data'
}
if ($panel -match 'GetModuleHandle\(nullptr\);\s*WNDCLASS' -or
    $panel -match 'ERROR_CLASS_ALREADY_EXISTS\)') {
  throw 'window class must not silently reuse an EXE-owned or stale callback'
}
Write-Host 'Candidate COM and panel window-class lifetime policy: PASS'
