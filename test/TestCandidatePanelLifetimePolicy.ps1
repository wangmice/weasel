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
    $candidate -notmatch 'CCandidateList::FinalizeExactCompositionString\([\s\S]*?(?:if\s*\(!_tsf\)|return\s+_tsf\s*\?\s*E_NOTIMPL\s*:\s*E_UNEXPECTED)' -or
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
    $langBar -notmatch '_pLangBarButton->DetachOwner\(\)') {
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
    $finalize -notmatch 'CandidateSelectResult::Selected' -or
    $finalize -notmatch 'CandidateSelectResult::NotDelivered' -or
    $finalize -notmatch 'CandidateSelectResult::Rejected' -or
    $finalize -notmatch '_finalization_started\s*=\s*false' -or
    $finalize -notmatch 'Destroy\(\);[\s\S]*?return E_FAIL') {
  throw 'candidate Finalize must retry only definite failures and retire ambiguous confirmations'
}
# BeginUIElement is an external COM boundary and can call SetSelection or
# Finalize before registering the element. Selection updates remain valid in
# that phase, but an early Finalize must fail without changing Rime state.
$registrationGuard = $finalize.IndexOf('if (_registration_in_progress)')
$normalFinalize = $finalize.IndexOf('if (!_uiStarted', $registrationGuard)
if ($registrationGuard -lt 0 -or $normalFinalize -le $registrationGuard) {
  throw 'Finalize must reject selection before UI registration completes'
}
$guardBody = $finalize.Substring($registrationGuard, $normalFinalize - $registrationGuard)
$setSelectionStart = $candidate.IndexOf('STDMETHODIMP CCandidateList::SetSelection(UINT nIndex)')
$setSelectionEnd = $candidate.IndexOf('STDMETHODIMP CCandidateList::Finalize(void)', $setSelectionStart)
if ($guardBody -notmatch 'return E_FAIL;' -or
    $guardBody -match '(return S_OK|_SelectCandidateOnCurrentPage|m_client\.)' -or
    $setSelectionStart -lt 0 -or $setSelectionEnd -le $setSelectionStart) {
  throw 'early Finalize must not commit, report success or issue IPC'
}
$setSelection = $candidate.Substring($setSelectionStart, $setSelectionEnd - $setSelectionStart)
if ($setSelection -notmatch '_registration_in_progress' -or
    $setSelection -notmatch 'nIndex\s*>=\s*cinfo\.candies\.size\(\)' -or
    $candidate -notmatch '_registration_in_progress\s*=\s*true' -or
    $candidate -notmatch 'BeginUIElement\(this,' -or
    $candidate -notmatch '_registration_in_progress\s*=\s*false' -or
    $candidate -match '_deferred_finalize' -or
    $candidateHeader -match '_deferred_finalize') {
  throw 'registration may update selection but cannot retain a pending confirm'
}
# A confirmed Finalize must not destroy a new candidate generation created by
# a synchronously applied response. The edit-session owner retires/reopens the
# UI only after verifying the response still belongs to the original document.
$editSession = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\EditSession.cpp') -Raw
$composition = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\Composition.cpp') -Raw
$selectionStart = $candidate.IndexOf('CandidateSelectResult WeaselTSF::_SelectCandidateOnCurrentPage(')
$selectionEnd = $candidate.IndexOf('void WeaselTSF::_HandleMousePageEvent(', $selectionStart)
if ($selectionStart -lt 0 -or $selectionEnd -le $selectionStart) {
  throw 'unable to isolate acknowledged selection response ordering'
}
$selection = $candidate.Substring($selectionStart, $selectionEnd - $selectionStart)
$selectedBranch = $finalize.Substring(
  $finalize.IndexOf('if (result == CandidateSelectResult::Selected)'),
  $finalize.IndexOf('if (result == CandidateSelectResult::NotDelivered') -
    $finalize.IndexOf('if (result == CandidateSelectResult::Selected)')
)
if ($selectedBranch -match 'Destroy\(' -or
    $selectedBranch -notmatch 'return S_OK;' -or
    $selection.IndexOf('_ConsumeResponseIfFresh()') -lt 0 -or
    $selection.IndexOf('_cand->MarkFinalizationAccepted(received_serial)') -lt 0 -or
    $selection.IndexOf('_UpdateComposition(context, previous_parsed_serial)') -lt 0 -or
    -not ($selection.IndexOf('_ConsumeResponseIfFresh()') -lt
          $selection.IndexOf('_cand->MarkFinalizationAccepted(received_serial)') -and
          $selection.IndexOf('_cand->MarkFinalizationAccepted(received_serial)') -lt
          $selection.IndexOf('_UpdateComposition(context, previous_parsed_serial)')) -or
    $candidateHeader -notmatch 'HasAcceptedFinalization\(UINT64\s+responseSerial\)' -or
    $candidateHeader -notmatch '_finalization_response_serial\s*==\s*responseSerial' -or
    $editSession -notmatch 'HasAcceptedFinalization\(snapshot\.identity\.serial\)' -or
    $composition -notmatch '_RetireUnappliedCandidateFinalization\(failedContext,\s*failureIdentity\.serial\)' -or
    $selection -notmatch 'previous_parsed_serial\s*=\s*_parsedSerial;[\s\S]*?_ConsumeResponseIfFresh\(\)' -or
    $composition -notmatch '_UpdateComposition\(com_ptr<ITfContext> pContext,\s*UINT64 previousSerial\)' -or
    $editSession -notmatch 'ShouldRenewCandidateUIAfterFinalization\(' -or
    $editSession -notmatch '_RenewCandidateUI\(pContext,\s*pendingUIGeneration,\s*\*context,\s*status\)' -or
    $candidate -notmatch 'void WeaselTSF::_RenewCandidateUI\(' -or
    $candidate -notmatch 'oldCandidate->DetachOwner\(\)' -or
    $candidate -notmatch 'newCandidate\.Attach\(new CCandidateList\(this\)\)' -or
    $candidate -notmatch '_cand\s*=\s*newCandidate' -or
    $candidate -notmatch '_FlushPendingUI\(pendingUIGeneration\);\s*_StartUI\(pContext\)' -or
    $candidate -notmatch 'WeaselTSF::_RetireUnappliedCandidateFinalization\(' -or
    $composition -notmatch '_RetireUnappliedCandidateFinalization\(') {
  throw 'acknowledged multi-stage selection must renew UI only after TSF response apply'
}
# Host implementations may discard an accepted TF_S_ASYNC edit session without
# entering DoEditSession. On final COM Release, only the current flight may
# retire the matching candidate confirmation; restored retries and stale
# sessions must never close a newer candidate generation.
$applyState = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\ResponseApplyState.h') -Raw
$disposeStart = $composition.IndexOf('~CApplyResponseEditSession() override')
$disposeEnd = $composition.IndexOf('STDMETHODIMP DoEditSession(', $disposeStart)
if ($disposeStart -lt 0 -or $disposeEnd -le $disposeStart) {
  throw 'unable to isolate abandoned response edit session disposal'
}
$dispose = $composition.Substring($disposeStart, $disposeEnd - $disposeStart)
if ($dispose -notmatch '_queue->InFlight\(\)' -or
    $dispose -notmatch '_queue->FlightToken\(\)\s*==\s*_flightToken' -or
    $dispose -notmatch 'ShouldRetireCandidateOnApplyDispose\(\s*_executed,\s*_resetOnDispose,\s*ownsFlight\)' -or
    $dispose -notmatch '_RetireUnappliedCandidateFinalization\(' -or
    $dispose -notmatch '_request->snapshot\.identity\.serial' -or
    $dispose -notmatch '_CompleteFlight\(\)' -or
    $applyState -notmatch 'ownsFlight\s*&&\s*\(terminalFailure\s*\|\|\s*!executed\)') {
  throw 'abandoned async candidate confirmation must retire only its own active flight'
}
# A dropped cross-context handoff may only pause the old flight; retry is
# triggered by the matching old-context OnEndEdit, not the COM destructor.
$handoffStart = $composition.IndexOf('class CContextHandoffEditSession')
$handoffEnd = $composition.IndexOf('void WeaselTSF::_EnqueueResponseApply(', $handoffStart)
if ($handoffStart -lt 0 -or $handoffEnd -le $handoffStart) {
  throw 'unable to locate handoff edit session and wait helper'
}
$handoff = $composition.Substring($handoffStart, $handoffEnd - $handoffStart)
$handoffDisposeStart = $handoff.IndexOf('~CContextHandoffEditSession() override')
$handoffExecuteStart = $handoff.IndexOf('STDMETHODIMP DoEditSession(', $handoffDisposeStart)
if ($handoffDisposeStart -lt 0 -or $handoffExecuteStart -le $handoffDisposeStart) {
  throw 'unable to isolate abandoned handoff destructor'
}
$handoffDispose = $handoff.Substring($handoffDisposeStart, $handoffExecuteStart - $handoffDisposeStart)
$textEditSink = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\TextEditSink.cpp') -Raw
$tsfHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\WeaselTSF.h') -Raw
if ($handoffDispose -notmatch '_queue->InFlight\(\)' -or
    $handoffDispose -notmatch '_queue->FlightToken\(\)\s*==\s*_flightToken' -or
    $handoffDispose -notmatch 'ShouldWaitForAbandonedHandoff\(' -or
    $handoffDispose -notmatch '_WaitForHandoffEdit\(_pContext,\s*_pComposition\)' -or
    $handoffDispose -match '_FinalizeComposition\(' -or
    $handoffDispose -notmatch '_CompleteFlight\(\)' -or
    $handoff -notmatch 'bool\s+_executed\s*=\s*false' -or
    $handoff -notmatch '_handoffWaitPauseToken\s*=\s*_responseApplyQueue\.Pause\(\)' -or
    $handoff -notmatch '_responseApplyQueue\.Resume\(token\)' -or
    $textEditSink -notmatch '_handoffWaitContext\s*==\s*pContext' -or
    $textEditSink -notmatch '_ResumeHandoffWait\(\)' -or
    $tsfHeader -notmatch '_handoffWaitPauseToken\s*=\s*0') {
  throw 'abandoned handoff must wait for a matching old-context edit event'
}
$handoffScheduleStart = $composition.IndexOf('if (_IsComposing() && !_IsComposingInContext(next->context))')
$handoffScheduleEnd = $composition.IndexOf('auto request = _responseApplyQueue.TakeNext();', $handoffScheduleStart)
if ($handoffScheduleStart -lt 0 -or $handoffScheduleEnd -le $handoffScheduleStart) {
  throw 'unable to locate handoff request scheduling path'
}
$handoffSchedule = $composition.Substring($handoffScheduleStart, $handoffScheduleEnd - $handoffScheduleStart)
if ($handoffSchedule -notmatch 'if \(requestHr == TF_E_DISCONNECTED\)' -or
    $handoffSchedule -notmatch 'requestHr == TF_E_LOCKED' -or
    $handoffSchedule -notmatch '_responseApplyQueue\.FlightToken\(\)\s*==\s*flightToken' -or
    $handoffSchedule -match '_ScheduleAbandonedHandoffCleanup\(' -or
    $handoff -match 'CAbandonedHandoffCleanupEditSession') {
  throw 'handoff must preserve the original bounded failure policy'
}
if ($candidate -notmatch 'void CCandidateList::MarkFinalizationAccepted\(UINT64 responseSerial\)' -or
    $candidate -notmatch '_finalization_accepted\s*=\s*false;' -or
    $selection -notmatch 'CandidateSelectResult::LegacyResponse[\s\S]*?_pendingCommit\.size\(\)\s*>\s*previous_commit_length') {
  throw 'ambiguous legacy replies must not acknowledge and renew the candidate UI'
}

if ($candidate -notmatch 'CCandidateList::SetIntegrationStyle\([\s\S]*?E_NOTIMPL' -or
    $candidate -notmatch 'CCandidateList::OnKeyDown\([\s\S]*?\*pIsEaten\s*=\s*FALSE' -or
    $candidate -match 'FinalizeExactCompositionString\(\)[\s\S]*?_AbortComposition\(false\)') {
  throw 'unsupported integrated candidate handling must not claim keys or mutate composition'
}
# A new server confirms Rime selection; old zero-valued replies are accepted
# only when an independently parsed response contains newly committed text.
$clientImpl = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPC\WeaselClientImpl.cpp') -Raw
$rime = Get-Content -LiteralPath (Join-Path $repo 'RimeWithWeasel\RimeWithWeasel.cpp') -Raw
$server = Get-Content -LiteralPath (Join-Path $repo 'WeaselIPCServer\WeaselServerImpl.cpp') -Raw
if ($candidate -notmatch 'SelectCandidateOnCurrentPageEx\(index\)' -or
    $candidate -notmatch 'CandidateSelectResult::LegacyResponse\s*&&' -or
    $clientImpl -notmatch 'error\.request_may_have_been_delivered' -or
    $clientImpl -notmatch 'CandidateSelectResult::NotDelivered' -or
    $clientImpl -notmatch 'CandidateSelectResult::Ambiguous' -or
    $clientImpl -notmatch 'CandidateSelectResult::LegacyResponse' -or
    $rime -notmatch 'if\s*\(!rime_api->select_candidate_on_current_page\(' -or
    $server -notmatch 'return\s+m_pRequestHandler->SelectCandidateOnCurrentPage\(') {
  throw 'candidate selection must distinguish Rime rejection, uncertain transport and legacy response'
}

Write-Host 'Candidate COM and panel window-class lifetime policy: PASS'
