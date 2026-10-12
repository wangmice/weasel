#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"

HRESULT WeaselTSF::_ApplyResponseSnapshot(
    TfEditCookie ec,
    com_ptr<ITfContext> pContext,
    weasel::tsf::ResponseApplySnapshot snapshot) {
  auto context = snapshot.context;
  const auto status = snapshot.status;
  const auto config = snapshot.config;
  const std::wstring& commit = snapshot.commit;

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

  // Once commit text reaches the document, replaying this response would type
  // it twice. Any later composition/UI failure is therefore recovered in
  // place rather than propagated back to the FIFO as a retryable failure.
  bool commitApplied =
      snapshot.progress == weasel::tsf::ApplyProgress::CommitApplied;
  auto recoverAfterCommit = [&](HRESULT failure) -> HRESULT {
    LOG(WARNING) << "TSF response state transition failed after commit; "
                 << "aborting composition without replaying commit, hr="
                 << static_cast<long>(failure);
    _InvalidatePendingUI();
    _EndUI();
    if (_IsComposingInContext(pContext)) {
      com_ptr<ITfComposition> current = _pComposition;
      // The committed text is already in the document. Never clear this range
      // during recovery or the supposedly durable commit would be deleted.
      // _EndCompositionLocked restores ownership on failure, so do not force
      // local finalization if the host still considers the composition alive.
      _EndCompositionLocked(ec, pContext, current, FALSE, TRUE);
    }
    m_client.ClearComposition();
    return S_OK;
  };

  // All document mutations owned by this server response run under this one
  // write cookie, in protocol order. There are no nested ASYNCDONTCARE write
  // sessions here, so commit/end/start cannot overtake one another.
  if (!commit.empty() && !commitApplied) {
    HRESULT hr = _InsertTextLocked(ec, pContext, composition, commit);
    if (FAILED(hr)) {
      if (status.composing)
        _InvalidatePendingUI();
      return hr;
    }
    commitApplied = true;
    snapshot.progress = weasel::tsf::ApplyProgress::CommitApplied;
    _committed = TRUE;

    if (plan.end_existing_composition) {
      hr = _EndCompositionLocked(ec, pContext, composition, FALSE,
                                 !status.composing);
      if (FAILED(hr))
        return recoverAfterCommit(hr);
      composition = nullptr;
    }
  } else if (commit.empty()) {
    _committed = FALSE;
  }

  if (plan.start_composition) {
    HRESULT hr = _StartCompositionLocked(
        ec, pContext, _fCUASWorkaroundEnabled && !config.inline_preedit,
        pendingUIGeneration);
    if (FAILED(hr) || !_IsComposingInContext(pContext)) {
      const HRESULT failure = FAILED(hr) ? hr : E_FAIL;
      if (commitApplied)
        return recoverAfterCommit(failure);
      _InvalidatePendingUI();
      _EndUI();
      return failure;
    }
    composition = _pComposition;
  } else if (!status.composing && hasComposition &&
             !plan.end_existing_composition) {
    const HRESULT hr =
        _EndCompositionLocked(ec, pContext, composition, TRUE, TRUE);
    if (FAILED(hr)) {
      if (commitApplied)
        return recoverAfterCommit(hr);
      return hr;
    }
    composition = nullptr;
  }

  if (status.composing && _IsComposingInContext(pContext)) {
    composition = _pComposition;
    if (config.inline_preedit) {
      const HRESULT hr = _ShowInlinePreeditLocked(
          ec, pContext, composition, context, pendingUIGeneration);
      if (FAILED(hr)) {
        if (commitApplied)
          return recoverAfterCommit(hr);
        _InvalidatePendingUI();
        return hr;
      }
    }

    // UI-less applications begin reading candidate contents only after
    // UpdateUIElement. Publish their data immediately; owned windows may still
    // wait for the caret extent so the first visible frame is correctly placed.
    if (weasel::tsf::ShouldPublishCandidateImmediately(_cand->IsUIless()))
      _FlushPendingUI(pendingUIGeneration);

    // Extent/layout work still needs a separate read session, but every
    // document mutation for this response is complete before it is queued.
    // The extent path flushes any still-pending candidate snapshot even when
    // positioning is unavailable.
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
