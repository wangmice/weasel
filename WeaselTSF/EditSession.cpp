#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "AsyncUiPolicy.h"

HRESULT WeaselTSF::_ApplyResponseSnapshot(
    TfEditCookie ec,
    com_ptr<ITfContext> pContext,
    weasel::tsf::ResponseApplySnapshot snapshot) {
  auto context = snapshot.context;
  auto status = snapshot.status;
  const auto config = snapshot.config;
  const std::wstring& commit = snapshot.commit;

  if (!pContext || !context)
    return E_INVALIDARG;

  // During a cross-context handoff the edit sink deliberately remains on the
  // old composition context so TF_E_LOCKED can be retried from its OnEndEdit.
  // Once this response has actually obtained the new context's write lock, the
  // barrier is over and the sink can safely follow the response target.
  if (pContext != _pTextEditSinkContext) {
    com_ptr<ITfDocumentMgr> targetDocumentMgr;
    if (SUCCEEDED(pContext->GetDocumentMgr(&targetDocumentMgr)) &&
        targetDocumentMgr) {
      _InitTextEditSink(targetDocumentMgr);
    }
  }

  const bool trackedResponse =
      snapshot.identity.serial != weasel::tsf::kInvalidResponseSerial;
  const auto epochDisposition =
      trackedResponse ? weasel::tsf::ClassifyEpochDisposition(
                            snapshot.identity.engine_epoch,
                            _responseApplyQueue.CurrentEpoch(), !commit.empty())
                      : weasel::tsf::EpochDisposition::ApplyState;
  if (epochDisposition == weasel::tsf::EpochDisposition::Drop)
    return S_OK;

  const bool restoredComposition = _RestoreRetainedComposition(pContext);

  if (_IsComposing() && !_IsComposingInContext(pContext))
    return E_UNEXPECTED;

  // Engine resets invalidate every stateful part of an old response. A commit,
  // however, is a durable event already emitted by Rime. Apply only that text
  // to its original document context and never replay stale preedit, candidate,
  // status, or configuration state into the new engine epoch.
  if (epochDisposition == weasel::tsf::EpochDisposition::ApplyCommitOnly) {
    com_ptr<ITfComposition> composition =
        _IsComposingInContext(pContext) ? _pComposition : nullptr;
    const HRESULT insertHr =
        _InsertTextLocked(ec, pContext, composition, commit);
    if (FAILED(insertHr))
      return insertHr;

    _committed = TRUE;
    if (composition) {
      const HRESULT endHr =
          _EndCompositionLocked(ec, pContext, composition, FALSE, TRUE);
      if (FAILED(endHr)) {
        // The text has already reached the document. Do not report a retryable
        // failure that could replay it. Also drop local ownership before an
        // older deferred abort-clear session runs; that session may still end
        // the host composition, but it must no longer erase the durable text.
        _InvalidatePendingUI();
        _EndUI();
        if (_IsCurrentComposition(composition))
          _FinalizeComposition();
        _responseApplyQueue.AdvanceEpoch();
        m_client.ClearComposition();
      }
    } else {
      _EndUI();
    }
    return S_OK;
  }

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

  if (restoredComposition && status.composing && !plan.start_composition)
    _StartUI(pContext);

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
      const HRESULT endHr =
          _EndCompositionLocked(ec, pContext, current, FALSE, TRUE);
      if (FAILED(endHr) && _IsCurrentComposition(current))
        _FinalizeComposition();
    }
    _responseApplyQueue.AdvanceEpoch();
    m_client.ClearComposition();
    return S_OK;
  };

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
      const HRESULT hr = _ShowInlinePreeditLocked(ec, pContext, composition,
                                                  context, pendingUIGeneration);
      if (FAILED(hr)) {
        if (commitApplied)
          return recoverAfterCommit(hr);
        _InvalidatePendingUI();
        return hr;
      }
    }

    // Host Finalize closes one logical candidate generation. A successful
    // selection may leave Rime composing (including commit + new preedit).
    // Do not retire the old element until the response actually reaches the
    // correct document, and publish the new candidates before BeginUIElement
    // so a synchronous host query never observes the previous page.
    if (weasel::tsf::ShouldRenewCandidateUIAfterFinalization(
            _cand->HasAcceptedFinalization(snapshot.identity.serial),
            _pCandidateUIContext == pContext, status.composing)) {
      _RenewCandidateUI(pContext, pendingUIGeneration, *context, status);
    }

    if (weasel::tsf::ShouldPublishCandidateImmediately(_cand->IsUIless()))
      _FlushPendingUI(pendingUIGeneration);

    _UpdateCompositionWindow(pContext, pendingUIGeneration);
  }

  if (!status.composing) {
    _EndUI();
    _UpdateUI(*context, status);
  }

  return S_OK;
}

STDMETHODIMP WeaselTSF::DoEditSession(TfEditCookie ec) {
  auto snapshot = weasel::tsf::MakeResponseApplySnapshot(
      std::move(_pendingCommit), _context, _status, _config);
  _pendingCommit.clear();
  return _ApplyResponseSnapshot(ec, _pEditSessionContext, std::move(snapshot));
}
