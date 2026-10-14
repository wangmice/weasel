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

  check(CanUseEnhancedForegroundBounds(true, true, true),
        "enhanced positioning uses validated foreground bounds");
  check(!CanUseEnhancedForegroundBounds(true, false, false),
        "enhanced positioning skips a missing foreground window");
  check(!CanUseEnhancedForegroundBounds(true, true, false),
        "enhanced positioning skips a failed foreground rect query");
  check(!CanUseEnhancedForegroundBounds(false, true, true),
        "normal positioning does not consult foreground bounds");

  check(ShouldFinalizeEndSession(true),
        "confirmed end-session finalizes the server handler");
  check(!ShouldFinalizeEndSession(false),
        "cancelled end-session keeps the server handler alive");

  PendingAutoHideState pending;
  check(!pending.Pending(), "auto-hide state starts idle");
  pending.OnPost();
  check(pending.Pending(), "posted timeout is visible before UI apply");
  pending.OnPost();
  pending.OnApplied();
  check(pending.Pending(), "one of two queued timeouts remains pending");
  pending.OnApplied();
  check(!pending.Pending(), "all applied timeouts clear pending state");
  pending.OnApplied();
  check(!pending.Pending(), "extra consume cannot underflow pending state");
  pending.OnPost();
  pending.OnPostFailed();
  check(!pending.Pending(), "failed PostMessage rolls back pending state");
  pending.OnPost();
  pending.Reset();
  check(!pending.Pending(), "destroy/reset clears queued timeout state");

  std::cout << (g_failures ? "FAILED: " : "PASSED: ") << g_failures
            << " failure(s)" << std::endl;
  return g_failures ? 1 : 0;
}
