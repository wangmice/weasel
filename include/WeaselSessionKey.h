#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace weasel {

struct SessionKey {
  std::array<std::uint8_t, 16> bytes{};

  friend bool operator==(const SessionKey& lhs, const SessionKey& rhs) noexcept {
    return lhs.bytes == rhs.bytes;
  }
  friend bool operator!=(const SessionKey& lhs, const SessionKey& rhs) noexcept {
    return !(lhs == rhs);
  }
  friend bool operator<(const SessionKey& lhs, const SessionKey& rhs) noexcept {
    return lhs.bytes < rhs.bytes;
  }
};

struct SessionKeyHash {
  std::size_t operator()(const SessionKey& key) const noexcept;
};

std::wstring FormatSessionKey(const SessionKey& key);
std::optional<SessionKey> ParseSessionKey(std::wstring_view text);
std::optional<SessionKey> GenerateSessionKey();

}  // namespace weasel
