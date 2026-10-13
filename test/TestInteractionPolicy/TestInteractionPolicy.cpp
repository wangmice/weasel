#include <WeaselInteractionPolicy.h>

#include <iostream>

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::cerr << "[FAIL] " << what << std::endl;
  } else {
    std::cerr << "[ok] " << what << std::endl;
  }
}

int main() {
  using namespace weasel::interaction_policy;

  check(EchoConfirmsSession(7, 7, 7),
        "echo accepts the unchanged expected session");
  check(!EchoConfirmsSession(7, 0, 0),
        "echo rejects transport failure after session clear");
  check(!EchoConfirmsSession(7, 8, 7),
        "echo rejects a concurrently replaced session");
  check(!EchoConfirmsSession(0, 0, 0),
        "echo never accepts the inactive zero session");

  check(NeedsRimeSessionValidation(1, 2),
        "cleanup epoch invalidates prior Rime validation");
  check(!NeedsRimeSessionValidation(2, 2),
        "matching cleanup epoch keeps the key fast path");

  check(KeepNotificationVisible(true, false),
        "a queued notification keeps the panel visible before UI apply");
  check(KeepNotificationVisible(false, true),
        "an active UI timer keeps the panel visible");
  check(!KeepNotificationVisible(false, false),
        "idle panel can still be hidden");

  std::cout << (g_failures ? "FAILED: " : "PASSED: ") << g_failures
            << " failure(s)" << std::endl;
  return g_failures ? 1 : 0;
}
