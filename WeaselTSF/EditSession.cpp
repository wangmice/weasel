#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"

STDMETHODIMP WeaselTSF::DoEditSession(TfEditCookie ec) {
  // 应答已在 _UpdateComposition 中解析（_status/_context/_config 及语言栏），
  // 本会话只把结果应用到文档；commit 取走即清，重复排队的会话天然幂等
  std::wstring commit = std::move(_pendingCommit);
  _pendingCommit.clear();

  bool compositionEnded = false;
  if (!commit.empty()) {
    // For auto-selecting, commit and preedit can both exist.
    // Commit the old TSF composition. If Rime immediately has a new
    // preedit (top-word input), _EndComposition() drops the local pointer
    // synchronously, so the following state check starts a new TSF
    // composition instead of observing the old one.
    if (!_IsComposing()) {
      _StartComposition(_pEditSessionContext,
                        _fCUASWorkaroundEnabled && !_config.inline_preedit);
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
                      _fCUASWorkaroundEnabled && !_config.inline_preedit);
  } else if (!_status.composing && _IsComposing()) {
    _EndComposition(_pEditSessionContext, true);
  }
  if (_IsComposing() && _config.inline_preedit) {
    _ShowInlinePreedit(_pEditSessionContext, _context);
  }

  if (!compositionEnded)
    _UpdateCompositionWindow(_pEditSessionContext);
  // Keep the existing candidate window alive during top-word input, but
  // publish the new candidates in this key-down edit session. Positioning is
  // still updated by the queued read session after the new composition is
  // created.
  _UpdateUI(*_context, _status);

  return TRUE;
}
