#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"

STDMETHODIMP WeaselTSF::DoEditSession(TfEditCookie ec) {
  // 应答已在 _UpdateComposition 中解析（_status/_context/_config 及语言栏），
  // 本会话只把结果应用到文档；commit 取走即清，重复排队的会话天然幂等
  std::wstring commit = std::move(_pendingCommit);
  _pendingCommit.clear();

  bool compositionEnded = false;
  UINT64 pendingUIGeneration = 0;
  if (_status.composing) {
    pendingUIGeneration = _QueuePendingUI(_context, _status);
  } else {
    // A non-composing response owns the visible state immediately. Any extent
    // session still queued for the previous composition must become inert.
    _InvalidatePendingUI();
  }
  if (!commit.empty()) {
    // For auto-selecting, commit and preedit can both exist.
    // Commit the old TSF composition. If Rime immediately has a new
    // preedit (top-word input), _EndComposition() drops the local pointer
    // synchronously, so the following state check starts a new TSF
    // composition instead of observing the old one.
    if (!_IsComposing()) {
      // This temporary composition only carries committed text. It does not
      // own the pending candidate snapshot, so its extent must never flush it.
      _StartComposition(_pEditSessionContext,
                        _fCUASWorkaroundEnabled && !_config.inline_preedit, 0);
    }
    _InsertText(_pEditSessionContext, commit);
    // Keep the candidate UI alive while the replacement composition is
    // being created; otherwise the key-down path destroys the old window
    // and the new one cannot be positioned until key-up.
    _EndComposition(_pEditSessionContext, false, !_status.composing);
    compositionEnded = true;
    _committed = TRUE;
  } else {
    _committed = FALSE;
  }
  if (_status.composing && (compositionEnded || !_IsComposing())) {
    _StartComposition(_pEditSessionContext,
                      _fCUASWorkaroundEnabled && !_config.inline_preedit,
                      pendingUIGeneration);
  } else if (!_status.composing && _IsComposing()) {
    _EndComposition(_pEditSessionContext, true);
  }
  if (_IsComposing() && _config.inline_preedit) {
    _ShowInlinePreedit(_pEditSessionContext, _context);
  }

  // For an existing composition, this response owns exactly one extent token.
  // A replacement composition schedules its extent from the start session once
  // the new range really exists.
  if (_status.composing && _IsComposing() && !compositionEnded)
    _UpdateCompositionWindow(_pEditSessionContext, pendingUIGeneration);

  // Composing responses are published only by their matching extent session.
  // Do not use _IsComposing() here: the asynchronous start session may still
  // be waiting for the host document lock even though this response composes.
  if (!_status.composing)
    _UpdateUI(*_context, _status);

  return S_OK;
}
