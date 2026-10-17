#include "../../WeaselIPCServer/SessionRoutingHandler.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace {

void require(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] SessionRoutingHandler: " << what << std::endl;
  std::abort();
}

class Backend : public weasel::RequestHandler {
 public:
  DWORD AddSession(LPWSTR, EatLine eat) override {
    const DWORD id = next++;
    sessions.insert(id);
    ++creates;
    if (eat) {
      std::wstring response = L"action=status\n.\n";
      eat(response);
    }
    return id;
  }

  DWORD FindSession(DWORD id) override {
    return sessions.count(id) ? id : 0;
  }

  bool RespondSession(DWORD id, EatLine eat) override {
    if (!sessions.count(id))
      return false;
    ++responses;
    if (eat) {
      std::wstring response = L"action=status\n.\n";
      eat(response);
    }
    return true;
  }

  DWORD RemoveSession(DWORD id) override {
    if (sessions.erase(id))
      ++removes;
    return 0;
  }

  DWORD DestroySession(DWORD id) override {
    if (sessions.erase(id))
      ++destroys;
    return 0;
  }

  DWORD next = 10;
  int creates = 0;
  int responses = 0;
  int removes = 0;
  int destroys = 0;
  std::set<DWORD> sessions;
};

struct SessionRoutingHandlerSelfTest {
  SessionRoutingHandlerSelfTest() {
    Backend backend;
    weasel::SessionRoutingHandler router;
    router.SetBackend(&backend);
    std::wstring response;
    weasel::RequestHandler::EatLine eat = [&](std::wstring& line) {
      response += line;
      return true;
    };

    wchar_t keyed[] =
        L"action=session\n"
        L"session.client_key=00000000000000000000000000000001\n.\n";
    const DWORD first = router.AddSession(keyed, eat);
    require(first == 10 && backend.creates == 1,
            "first keyed START_SESSION creates one backend session");

    response.clear();
    const DWORD retry = router.AddSession(keyed, eat);
    require(retry == first && backend.creates == 1 && backend.responses == 1 &&
                response.find(L"action=status") != std::wstring::npos,
            "same keyed retry reuses id and emits a fresh response");

    wchar_t malformed[] = L"session.client_key=bad\n.\n";
    require(router.AddSession(malformed, eat) == 0 && backend.creates == 1,
            "malformed keyed request fails closed without backend creation");

    wchar_t legacy[] = L"action=session\nsession.client_app=old.exe\n.\n";
    require(router.AddSession(legacy, eat) == 11 && backend.creates == 2,
            "missing key preserves legacy creation");

    backend.sessions.erase(first);
    const DWORD rebuilt = router.AddSession(keyed, eat);
    require(rebuilt == 12 && backend.creates == 3 && rebuilt != first,
            "stale keyed mapping is rebuilt once under the same key");

    router.RemoveSession(rebuilt);
    require(!backend.sessions.count(rebuilt) && backend.destroys == 1 &&
                backend.removes == 0,
            "END_SESSION routes through lifecycle destruction exactly once");
  }
};

SessionRoutingHandlerSelfTest g_session_routing_handler_self_test;

}  // namespace
