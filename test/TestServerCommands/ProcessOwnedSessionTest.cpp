#include "../../WeaselIPCServer/SessionRoutingHandler.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace {

void require(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] ProcessOwnedSession: " << what << std::endl;
  std::abort();
}

class OwnedBackend : public weasel::RequestHandler {
 public:
  DWORD AddSession(LPWSTR, EatLine) override {
    const DWORD id = next++;
    sessions.insert(id);
    return id;
  }
  DWORD FindSession(DWORD id) override { return sessions.count(id) ? id : 0; }
  bool RespondSession(DWORD id, EatLine) override {
    return sessions.count(id) != 0;
  }
  DWORD RemoveSession(DWORD id) override {
    if (sessions.erase(id))
      removed.insert(id);
    return 0;
  }
  void StartMaintenance() override { sessions.clear(); }

  DWORD next = 100;
  std::set<DWORD> sessions;
  std::set<DWORD> removed;
};

std::wstring KeyBody(int suffix) {
  wchar_t tail[3]{};
  swprintf_s(tail, L"%02x", suffix & 0xff);
  return L"session.client_key=000000000000000000000000000000" +
         std::wstring(tail) + L"\n.\n";
}

struct ProcessOwnedSessionSelfTest {
  ProcessOwnedSessionSelfTest() {
    OwnedBackend backend;
    weasel::SessionRoutingHandler router;
    router.SetBackend(&backend);
    const weasel::ClientProcessToken p{1};
    const weasel::ClientProcessToken q{2};

    auto p1_body = KeyBody(1);
    auto p2_body = KeyBody(2);
    auto q1_body = KeyBody(3);

    router.SetCurrentOwner({p});
    const DWORD p1 = router.AddSession(p1_body.data());
    const DWORD p2 = router.AddSession(p2_body.data());
    router.SetCurrentOwner({q});
    const DWORD q1 = router.AddSession(q1_body.data());
    require(p1 && p2 && q1 && backend.sessions.size() == 3,
            "multiple process-owned sessions are established");

    router.ClearCurrentOwner();
    require(backend.sessions.size() == 3,
            "dropping pipe dispatch context alone removes no logical session");

    router.RemoveClient(p);
    require(!backend.sessions.count(p1) && !backend.sessions.count(p2) &&
                backend.sessions.count(q1),
            "client exit removes exactly sessions owned by that process token");

    auto shared_key = KeyBody(9);
    router.SetCurrentOwner({p});
    const DWORD owned = router.AddSession(shared_key.data());
    router.SetCurrentOwner({q});
    require(router.AddSession(shared_key.data()) == 0 &&
                backend.sessions.count(owned),
            "a live key cannot be adopted by a different process generation");

    router.StartMaintenance();
    require(
        backend.sessions.empty(),
        "maintenance clears backend sessions and lifecycle indexes together");
    router.EndMaintenance();
    router.SetCurrentOwner({q});
    const DWORD after_maintenance = router.AddSession(shared_key.data());
    require(after_maintenance && after_maintenance != owned,
            "cleared key can create a new monotonic session after maintenance");
  }
};

ProcessOwnedSessionSelfTest g_process_owned_session_self_test;

}  // namespace
