#include "TestClientPrivateAccess.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] ClientSessionKey: " << what << std::endl;
  std::abort();
}

weasel::SessionKey Key(std::uint8_t tail) {
  weasel::SessionKey key{};
  key.bytes.back() = tail;
  return key;
}

struct ClientSessionKeySelfTest {
  ClientSessionKeySelfTest() {
    weasel::Client client;
    int generated = 0;
    weasel::ClientTestAccess::SetSessionKeyGenerator(
        client, [&]() -> std::optional<weasel::SessionKey> {
          ++generated;
          return Key(static_cast<std::uint8_t>(generated));
        });

    require(weasel::ClientTestAccess::EnsureSessionKey(client),
            "first ensure generates a key");
    const auto first = weasel::ClientTestAccess::SessionKeyValue(client);
    require(first.has_value() && generated == 1,
            "first logical session owns exactly one generated key");
    require(
        weasel::ClientTestAccess::EnsureSessionKey(client) && generated == 1,
        "repeated ensure preserves the key across retries");

    require(weasel::ClientTestAccess::WriteClientInfo(client),
            "START_SESSION metadata stages with a valid key");
    const auto* body = reinterpret_cast<const wchar_t*>(
        weasel::ClientTestAccess::Channel(client).SendBuffer());
    const std::wstring staged = body ? std::wstring(body) : std::wstring();
    require(
        staged.find(L"session.client_key=00000000000000000000000000000001\n") !=
            std::wstring::npos,
        "staged metadata carries the canonical SessionKey");

    weasel::ClientTestAccess::Channel(client).ClearBufferStream();
    require(weasel::ClientTestAccess::WriteClientInfo(client),
            "retry metadata can be restaged");
    const auto* retry_body = reinterpret_cast<const wchar_t*>(
        weasel::ClientTestAccess::Channel(client).SendBuffer());
    const std::wstring retry =
        retry_body ? std::wstring(retry_body) : std::wstring();
    require(
        retry.find(L"session.client_key=00000000000000000000000000000001\n") !=
                std::wstring::npos &&
            generated == 1,
        "retry sends the exact same SessionKey");

    // Recovery calls Disconnect only after IsSessionActive() is false. That is
    // a transport reset, not a logical-session boundary: the key must survive
    // so a lost START_SESSION response can be retried idempotently.
    client.Disconnect();
    require(weasel::ClientTestAccess::SessionKeyValue(client) == first,
            "inactive transport disconnect preserves the logical SessionKey");

    weasel::ClientTestAccess::RetireSessionKey(client);
    require(!weasel::ClientTestAccess::SessionKeyValue(client).has_value(),
            "logical session retirement clears the old key");
    require(weasel::ClientTestAccess::EnsureSessionKey(client),
            "next logical session can generate a replacement key");
    const auto second = weasel::ClientTestAccess::SessionKeyValue(client);
    require(second.has_value() && *second == Key(2) && generated == 2,
            "retired key is never reused for a later logical session");

    weasel::Client failed;
    int failed_generations = 0;
    weasel::ClientTestAccess::SetSessionKeyGenerator(
        failed, [&]() -> std::optional<weasel::SessionKey> {
          ++failed_generations;
          return std::nullopt;
        });
    require(!weasel::ClientTestAccess::WriteClientInfo(failed) &&
                failed_generations == 1 &&
                !weasel::ClientTestAccess::SessionKeyValue(failed).has_value(),
            "RNG failure fails closed without staging an unkeyed session");
  }
};

ClientSessionKeySelfTest g_client_session_key_self_test;

}  // namespace
