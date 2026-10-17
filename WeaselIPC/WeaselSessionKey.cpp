#include "stdafx.h"

#ifdef _WIN32
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

#include <WeaselSessionKey.h>
#include <WeaselSessionLifecycle.h>

namespace weasel {

std::size_t SessionKeyHash::operator()(const SessionKey& key) const noexcept {
  std::size_t hash = static_cast<std::size_t>(2166136261u);
  for (std::uint8_t byte : key.bytes) {
    hash ^= byte;
    hash *= static_cast<std::size_t>(16777619u);
  }
  return hash;
}

std::wstring FormatSessionKey(const SessionKey& key) {
  static constexpr wchar_t kHex[] = L"0123456789abcdef";
  std::wstring result;
  result.resize(key.bytes.size() * 2);
  for (std::size_t i = 0; i < key.bytes.size(); ++i) {
    const std::uint8_t byte = key.bytes[i];
    result[i * 2] = kHex[(byte >> 4) & 0x0f];
    result[i * 2 + 1] = kHex[byte & 0x0f];
  }
  return result;
}

std::optional<SessionKey> ParseSessionKey(std::wstring_view text) {
  if (text.size() != 32)
    return std::nullopt;

  auto nibble = [](wchar_t ch) -> int {
    if (ch >= L'0' && ch <= L'9')
      return ch - L'0';
    if (ch >= L'a' && ch <= L'f')
      return ch - L'a' + 10;
    return -1;
  };

  SessionKey key{};
  for (std::size_t i = 0; i < key.bytes.size(); ++i) {
    const int high = nibble(text[i * 2]);
    const int low = nibble(text[i * 2 + 1]);
    if (high < 0 || low < 0)
      return std::nullopt;
    key.bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return key;
}

std::optional<SessionKey> GenerateSessionKey() {
#ifdef _WIN32
  SessionKey key{};
  const NTSTATUS status = BCryptGenRandom(
      nullptr, key.bytes.data(), static_cast<ULONG>(key.bytes.size()),
      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  if (!BCRYPT_SUCCESS(status))
    return std::nullopt;
  return key;
#else
  return std::nullopt;
#endif
}

ParsedSessionKeyMetadata ParseSessionKeyMetadata(std::wstring_view body) {
  static constexpr std::wstring_view kPrefix = L"session.client_key=";
  ParsedSessionKeyMetadata result;
  bool seen = false;

  std::size_t offset = 0;
  while (offset <= body.size()) {
    const std::size_t newline = body.find(L'\n', offset);
    const std::size_t end =
        newline == std::wstring_view::npos ? body.size() : newline;
    std::wstring_view line = body.substr(offset, end - offset);
    if (!line.empty() && line.back() == L'\r')
      line.remove_suffix(1);
    if (line == L".")
      break;

    if (line.size() >= kPrefix.size() &&
        line.substr(0, kPrefix.size()) == kPrefix) {
      if (seen)
        return {SessionKeyMetadataState::Invalid, std::nullopt};
      seen = true;
      const std::optional<SessionKey> key =
          ParseSessionKey(line.substr(kPrefix.size()));
      if (!key)
        return {SessionKeyMetadataState::Invalid, std::nullopt};
      result.state = SessionKeyMetadataState::Valid;
      result.key = key;
    }

    if (newline == std::wstring_view::npos)
      break;
    offset = newline + 1;
  }

  if (!seen)
    result.state = SessionKeyMetadataState::Absent;
  return result;
}

}  // namespace weasel
