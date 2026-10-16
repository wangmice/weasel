#pragma once

// TestServerCommands needs WeaselTSF's private test surface to have identical
// access in every translation unit. Load the TSF precompiled-header
// dependencies normally first so the C++ standard library never sees a
// macroized language keyword; then expose only the WeaselTSF class itself.
#include "../../WeaselTSF/stdafx.h"
#include "../../WeaselTSF/Globals.h"
#include "../../WeaselTSF/KeyEventPairState.h"
#include "../../WeaselTSF/ResponseApply.h"
#include "../../WeaselTSF/ResponseApplyQueue.h"
#include <WeaselIPC.h>
#include <WeaselIPCData.h>
#include <ResponseParser.h>

#define private public
#include "../../WeaselTSF/WeaselTSF.h"
#undef private

#include "../../WeaselIPC/WeaselClientImpl.h"

namespace weasel {

class ClientTestAccess {
 public:
  static PipeChannel<PipeMessage>& Channel(Client& client) {
    return client.m_pImpl->channel;
  }

  static UINT SessionId(Client& client) {
    return client.m_pImpl->_SessionId();
  }
};

}  // namespace weasel
