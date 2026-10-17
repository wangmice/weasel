#include <WeaselSessionLifecycle.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] SessionLifecycle: " << what << std::endl;
  std::abort();
}

struct SessionLifecycleSelfTest {
  SessionLifecycleSelfTest() {
    using weasel::SessionKeyMetadataState;

    const auto absent = weasel::ParseSessionKeyMetadata(
        L"action=session\nsession.client_app=test.exe\n.\n");
    require(absent.state == SessionKeyMetadataState::Absent && !absent.key,
            "missing key remains a legacy request");

    const auto valid = weasel::ParseSessionKeyMetadata(
        L"action=session\nsession.client_key=000102030405060708090a0b0c0d0e0f\n"
        L"session.client_type=tsf\n.\n");
    require(valid.state == SessionKeyMetadataState::Valid && valid.key &&
                weasel::FormatSessionKey(*valid.key) ==
                    L"000102030405060708090a0b0c0d0e0f",
            "valid key metadata parses canonically");

    const auto malformed = weasel::ParseSessionKeyMetadata(
        L"action=session\nsession.client_key=bad\n.\n");
    require(malformed.state == SessionKeyMetadataState::Invalid && !malformed.key,
            "present malformed key fails closed");

    const auto duplicate = weasel::ParseSessionKeyMetadata(
        L"session.client_key=000102030405060708090a0b0c0d0e0f\n"
        L"session.client_key=000102030405060708090a0b0c0d0e0f\n.\n");
    require(duplicate.state == SessionKeyMetadataState::Invalid && !duplicate.key,
            "duplicate key fields fail closed");
  }
};

SessionLifecycleSelfTest g_session_lifecycle_self_test;

}  // namespace
