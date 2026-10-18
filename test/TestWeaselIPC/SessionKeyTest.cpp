#include <WeaselSessionKey.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] SessionKey: " << what << std::endl;
  std::abort();
}

struct SessionKeySelfTest {
  SessionKeySelfTest() {
    weasel::SessionKey expected{};
    for (std::size_t i = 0; i < expected.bytes.size(); ++i)
      expected.bytes[i] = static_cast<std::uint8_t>(i);

    const std::wstring text = weasel::FormatSessionKey(expected);
    require(text == L"000102030405060708090a0b0c0d0e0f",
            "format is fixed-width lowercase hex");

    const auto parsed = weasel::ParseSessionKey(text);
    require(parsed.has_value() && *parsed == expected,
            "valid 32-character key round-trips");

    require(!weasel::ParseSessionKey(L"").has_value(), "empty key is rejected");
    require(!weasel::ParseSessionKey(L"abc").has_value(),
            "short key is rejected");
    require(!weasel::ParseSessionKey(L"000102030405060708090a0b0c0d0e0")
                 .has_value(),
            "31-character key is rejected");
    require(!weasel::ParseSessionKey(L"000102030405060708090a0b0c0d0e0f0")
                 .has_value(),
            "33-character key is rejected");
    require(!weasel::ParseSessionKey(L"000102030405060708090a0b0c0d0e0g")
                 .has_value(),
            "non-hex key is rejected");
    require(!weasel::ParseSessionKey(L"000102030405060708090A0B0C0D0E0F")
                 .has_value(),
            "uppercase key is rejected to keep one canonical wire form");

#ifdef _WIN32
    const auto generated = weasel::GenerateSessionKey();
    require(generated.has_value(), "system CSPRNG generates a key");
    require(weasel::FormatSessionKey(*generated).size() == 32,
            "generated key serializes to 32 characters");
#endif
  }
};

SessionKeySelfTest g_session_key_self_test;

}  // namespace
