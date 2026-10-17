#pragma once

#include "../../WeaselIPC/WeaselClientImpl.h"

#include <functional>
#include <mutex>
#include <optional>

namespace weasel {

class ClientTestAccess {
 public:
  static PipeChannel<PipeMessage>& Channel(Client& client) {
    return client.m_pImpl->channel;
  }

  static UINT SessionId(Client& client) {
    return client.m_pImpl->_SessionId();
  }

  static bool EnsureSessionKey(Client& client) {
    return client.m_pImpl->_EnsureSessionKey();
  }

  static void RetireSessionKey(Client& client) {
    client.m_pImpl->_RetireSessionKey();
  }

  static std::optional<SessionKey> SessionKeyValue(Client& client) {
    std::lock_guard<std::mutex> lock(client.m_pImpl->session_key_mutex_);
    return client.m_pImpl->session_key_;
  }

  static void SetSessionKeyGenerator(
      Client& client,
      std::function<std::optional<SessionKey>()> generator) {
    std::lock_guard<std::mutex> lock(client.m_pImpl->session_key_mutex_);
    client.m_pImpl->session_key_generator_ = std::move(generator);
  }

  static bool WriteClientInfo(Client& client) {
    return client.m_pImpl->_WriteClientInfo();
  }
};

}  // namespace weasel
