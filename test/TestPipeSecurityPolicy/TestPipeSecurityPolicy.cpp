#include <PipeSecurityPolicy.h>

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
  using weasel::pipe_security::BuildPipeSecuritySddl;

  const std::wstring logon_sid = L"S-1-5-5-123-456";
  const std::wstring sddl = BuildPipeSecuritySddl(logon_sid);
  check(!sddl.empty(), "valid logon SID produces a security descriptor");
  check(sddl.find(logon_sid) != std::wstring::npos,
        "security descriptor grants the current logon session");
  check(sddl.find(L";;;AC)") != std::wstring::npos,
        "security descriptor preserves AppContainer access");
  check(sddl.find(L";;;SY)") != std::wstring::npos,
        "security descriptor preserves LocalSystem access");
  check(sddl.find(L";;;WD)") == std::wstring::npos,
        "security descriptor does not grant Everyone");
  check(BuildPipeSecuritySddl(L"").empty(),
        "missing logon SID fails closed at policy construction");

  std::cout << (g_failures ? "FAILED: " : "PASSED: ") << g_failures
            << " failure(s)" << std::endl;
  return g_failures ? 1 : 0;
}
