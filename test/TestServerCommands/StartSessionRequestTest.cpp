#include <WeaselIPC.h>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] SessionBackend: " << what << std::endl;
  std::abort();
}

class SessionBackendHandler : public weasel::RequestHandler {
 public:
  DWORD AddSession(LPWSTR, EatLine) override {
    ++creates;
    return 77;
  }

  bool RespondSession(DWORD session_id, EatLine eat) override {
    ++responses;
    if (session_id != 77)
      return false;
    if (eat) {
      std::wstring response = L"action=status\n.\n";
      eat(response);
    }
    return true;
  }

  int creates = 0;
  int responses = 0;
};

struct SessionBackendSelfTest {
  SessionBackendSelfTest() {
    SessionBackendHandler handler;
    wchar_t body[] = L"action=session\n.\n";
    std::wstring response;
    weasel::RequestHandler::EatLine eat =
        [&](std::wstring& line) -> bool {
      response += line;
      return true;
    };

    require(handler.AddSession(body, eat) == 77 && handler.creates == 1,
            "legacy creation remains available to the server lifecycle layer");
    require(handler.RespondSession(77, eat) && handler.responses == 1 &&
                response.find(L"action=status") != std::wstring::npos,
            "existing logical session can emit a fresh retry response");
  }
};

SessionBackendSelfTest g_session_backend_self_test;

}  // namespace
