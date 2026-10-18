#pragma once

#include "../../WeaselTSF/WeaselTSF.h"

class WeaselTSFTestAccess {
 public:
  static weasel::Client& Client(WeaselTSF& tsf) { return tsf.m_client; }

  static bool HasPendingUI(const WeaselTSF& tsf) { return tsf._hasPendingUI; }

  static bool ProcessKeyEventWithRecovery(WeaselTSF& tsf,
                                          const weasel::KeyEvent& key_event) {
    return tsf._ProcessKeyEventWithRecovery(key_event);
  }
};
