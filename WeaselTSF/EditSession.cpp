#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"

HRESULT WeaselTSF::_ApplyResponseSnapshot(
    TfEditCookie ec,
    com_ptr<ITfContext> pContext,
    weasel::tsf::ResponseApplySnapshot snapshot) {
  auto context = std::move(snapshot.context);
  const auto status = snapshot.status;
  const auto config = snapshot.config;
  std::wstring commit = std::move(snapshot.commit);

  if (!pContext || !context)
    return E_INVALIDARG;

  // The serial scheduler inserts a write-lock barrier before a response whose
  // context differs from the currently tracked composition. Reaching this
  // branch would mean the scheduler invariant was broken; never mutate a
  // foreign context with this edit cookie.
  if (_IsComposing() && !_IsComposingInContext(pContext))
    return E_UNEXPECTED;

  // A host can terminate the old composition before the handoff barrier is
  // needed while the candidate UI remains alive. Force a new UIElement/window
  // lifetime when its tracked owner differs from this response context.
  if (_pCandidateUIContext && _pCandidateUIContext != pContext)
    _EndUI();

  const bool hasComposition = _IsComposingInContext(pContext);
  const auto plan = weasel::tsf::BuildResponseApplyPlan(
      hasComposition, !commit.empty(), status.composing);

  UINT64 pendingUIGeneration = 0;
  if (status.composing) {
    pendingUIGeneration = _QueuePendingUI(context, status);
  } else {
    _InvalidatePendingUI();
  }

  com_ptr<ITfComposition> composition =
      hasComposition ? _pComposition : nullptr;

  // All document mutations owned by this server response run under this one
  // write cookie, in protocol order. There are no nested ASYNCDONTCARE write
  // sessions here, so commit/end/start cannot overtake one another.
  if (!commit.empty()) {
    HRESULT hr = _InsertTextLocked(ec, pContext, composition, commit);
    if (FAILED(hr))
      return hr;

    if (plan.end_existing_composition) {
      hr = _EndCompositionLocked(ec, pContext, composition, FALSE,
                                 !status.composing);
      if (FAILED(hr)) {
        if (status.composing) {
          _InvalidatePendingUI();
          _EndUI();
        }
        return hr;
      }
      composition = nullptr;
    }
    _committed = TRUE;
  } else {
    _committed = FALSE;
  }

  if (plan.start_composition) {
    HRESULT hr = _StartCompositionLocked(
        ec, pContext, _fCUASWorkaroundEnabled && !config.inline_preedit,
        pendingUIGeneration);
    if (FAILED(hr) || !_IsComposingInContext(pContext)) {
      _InvalidatePendingUI();
      _EndUI();
      return FAILED(hr) ? hr : E_FAIL;
    }
    composition = _pComposition;
  } else if (!status.composing && hasComposition &&
             !plan.end_existing_composition) {
    const HRESULT hr =
        _EndCompositionLocked(ec, pContext, composition, TRUE, TRUE);
    if (FAILED(hr))
      return hr;
    composition = nullptr;
  }

  if (status.composing && _IsComposingInContext(pContext)) {
    composition = _pComposition;
    if (config.inline_preedit) {
      const HRESULT hr = _ShowInlinePreeditLocked(
          ec, pContext, composition, context, pendingUIGeneration);
      if (FAILED(hr))
        return hr;
    }

    // Extent/layout work still needs a separate read session, but every
    // document mutation for this response is complete before it is queued.
    _UpdateCompositionWindow(pContext, pendingUIGeneration);
  }

  if (!status.composing) {
    // Normal composition completion is also a UIElement lifetime boundary,
    // including the case where the host already terminated the TSF
    // composition before this response was applied.
    _EndUI();
    _UpdateUI(*context, status);
  }

  return S_OK;
}

STDMETHODIMP WeaselTSF::DoEditSession(TfEditCookie ec) {
  // Compatibility fallback for external callers that still obtain
  // ITfEditSession from WeaselTSF directly. The key/UI response path uses the
  // ordered CApplyResponseEditSession queue instead.
  auto snapshot = weasel::tsf::MakeResponseApplySnapshot(
      std::move(_pendingCommit), _context, _status, _config);
  _pendingCommit.clear();
  return _ApplyResponseSnapshot(ec, _pEditSessionContext,
                                std::move(snapshot));
}
