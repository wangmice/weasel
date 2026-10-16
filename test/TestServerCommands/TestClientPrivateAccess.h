#pragma once

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
