#pragma once

#include "../../WeaselTSF/WeaselTSF.h"

class WeaselTSFTestAccess {
 public:
  static weasel::Client& Client(WeaselTSF& tsf) { return tsf.m_client; }

  static bool HasPendingUI(const WeaselTSF& tsf) { return tsf._hasPendingUI; }

  static auto& ResponseQueue(WeaselTSF& tsf) { return tsf._responseApplyQueue; }

  static void PauseAbandonedHandoff(WeaselTSF& tsf) {
    tsf._handoffWaitPauseToken = tsf._responseApplyQueue.Pause();
  }

  static void EnqueueInputResponse(WeaselTSF& tsf, const std::wstring& commit) {
    // Exercise the real enqueue/recovery path without requesting a host edit
    // session, registering a TIP, or contacting the live algorithm server.
    tsf._tfClientId = TF_CLIENTID_NULL;
    weasel::tsf::ResponseApplySnapshot snapshot;
    snapshot.commit = commit;
    tsf._EnqueueResponseApply(nullptr, std::move(snapshot), 0);
  }

  static bool ProcessKeyEventWithRecovery(WeaselTSF& tsf,
                                          const weasel::KeyEvent& key_event) {
    return tsf._ProcessKeyEventWithRecovery(key_event) !=
           WeaselTSF::KeyDispatchResult::Unhandled;
  }
};

