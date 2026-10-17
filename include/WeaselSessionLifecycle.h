#pragma once

#include <WeaselSessionKey.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace weasel {

struct ClientProcessToken {
  std::uint64_t value = 0;

  friend bool operator==(const ClientProcessToken& lhs,
                         const ClientProcessToken& rhs) noexcept {
    return lhs.value == rhs.value;
  }
  friend bool operator!=(const ClientProcessToken& lhs,
                         const ClientProcessToken& rhs) noexcept {
    return !(lhs == rhs);
  }
};

struct SessionOwner {
  std::optional<ClientProcessToken> process;
};

enum class SessionKeyMetadataState {
  Absent,
  Valid,
  Invalid,
};

struct ParsedSessionKeyMetadata {
  SessionKeyMetadataState state = SessionKeyMetadataState::Absent;
  std::optional<SessionKey> key;
};

ParsedSessionKeyMetadata ParseSessionKeyMetadata(std::wstring_view body);

struct StartSessionRequest {
  std::optional<SessionKey> key;
  SessionOwner owner;
  wchar_t* body = nullptr;
};

struct StartSessionResult {
  std::uint32_t ipc_id = 0;
  bool created = false;
};

}  // namespace weasel
