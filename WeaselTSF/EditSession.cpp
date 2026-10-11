#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"

HRESULT WeaselTSF::_ApplyResponseSnapshot(
    com_ptr<ITfContext> pContext,
    weasel::tsf::ResponseApplySnapshot snapshot) {
  auto context = std::move(snapshot.context);
  auto status = snapshot.status;
  auto config = snapshot.config;
  std::wstring commit = std::move(snapshot.commit);

  // A delayed response may finally obtain a write lock after focus moved to a
  // different ITfContext. Never let a new context overwrite the single tracked
  // composition while the old context still owns it. Queue the old end first;
  // _EndComposition drops local ownership as soon as TSF accepts that request.
  if (_IsComposing() && !_IsComposingInContext(pContext)) {
    com_ptr<ITfContext> previousContext = _pCompositionContext;
    if (previousContext)
      _EndComposition(previousContext, true, !status.composing);
  }

  const bool blockedByForeignComposition =
      _IsComposing() && !_IsComposingInContext(pContext);
  const bool hasComposition = _IsComposingInContext(pContext);
  const auto plan = weasel::tsf::BuildResponseApplyPlan(
      hasComposition, !commit.empty(), status.composing);

  UINT64 pendingUIGeneration = 0;
  if (status.composing) {
    pendingUIGeneration = _QueuePendingUI(context, status);
  } else {
    // A non-composing response owns the visible state immediately. Any extent
    // session still queued for the previous composition must become inert.
    _InvalidatePendingUI();
  }

  if (!commit.empty()) {
    // With no composition in this context, CInsertTextEditSession already has
    // a direct InsertTextAtSelection fallback. Do not create a temporary
    // asynchronous composition merely to carry committed text: its matching
    // EndComposition session could capture nullptr and leave an orphan behind.
    _InsertText(pContext, commit);
    if (plan.end_existing_composition) {
      // Keep the candidate UI alive while a replacement composition is being
      // created; otherwise the key-down path destroys the old window and the
      // new one cannot be positioned until key-up.
      _EndComposition(pContext, false, !status.composing);
    }
    _committed = TRUE;
  } else {
    _committed = FALSE;
  }

  if (plan.start_composition && !blockedByForeignComposition) {
    _StartComposition(pContext,
                      _fCUASWorkaroundEnabled && !config.inline_preedit,
                      pendingUIGeneration);
  } else if (!status.composing && hasComposition &&
             !plan.end_existing_composition) {
    _EndComposition(pContext, true);
  }

  if (_IsComposingInContext(pContext) && config.inline_preedit) {
    _ShowInlinePreedit(pContext, context);
  }

  // For an existing composition, this response owns exactly one extent token.
  // A replacement composition schedules its extent from the start session once
  // the new range really exists.
  if (status.composing && _IsComposingInContext(pContext) &&
      !plan.end_existing_composition)
    _UpdateCompositionWindow(pContext, pendingUIGeneration);

  // Composing responses are published only by their matching extent session.
  // Do not use _IsComposing() here: the asynchronous start session may still
  // be waiting for the host document lock even though this response composes.
  if (!status.composing)
    _UpdateUI(*context, status);

  return S_OK;
}

STDMETHODIMP WeaselTSF::DoEditSession(TfEditCookie ec) {
  // Compatibility fallback for callers that still obtain ITfEditSession from
  // WeaselTSF directly. Internal response application now uses a dedicated
  // edit-session object that carries its own context and immutable snapshot.
  auto snapshot = weasel::tsf::MakeResponseApplySnapshot(
      std::move(_pendingCommit), _context, _status, _config);
  _pendingCommit.clear();
  return _ApplyResponseSnapshot(_pEditSessionContext, std::move(snapshot));
}
