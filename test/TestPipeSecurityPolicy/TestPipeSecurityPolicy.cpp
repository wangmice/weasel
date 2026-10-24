#include <PipeSecurityPolicy.h>
#include <LifecycleMutexSecurityPolicy.h>

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

#if defined(_WIN32)
#include <windows.h>
#include <sddl.h>
#include <vector>

// Regression using the exact lifecycle-mutex SDDL created by WeaselServer,
// distinct from the named-pipe ACL. The second open must have synchronize
// rights even if the first creator is a different process.
static std::wstring current_logon_sid() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return {};
  DWORD size = 0;
  GetTokenInformation(token, TokenGroups, nullptr, 0, &size);
  if (!size) {
    CloseHandle(token);
    return {};
  }
  std::vector<BYTE> bytes(size);
  if (!GetTokenInformation(token, TokenGroups, bytes.data(), size, &size)) {
    CloseHandle(token);
    return {};
  }
  CloseHandle(token);
  const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(bytes.data());
  for (DWORD i = 0; i < groups->GroupCount; ++i) {
    if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID)
      continue;
    LPWSTR value = nullptr;
    if (!ConvertSidToStringSidW(groups->Groups[i].Sid, &value))
      return {};
    std::wstring result = value;
    LocalFree(value);
    return result;
  }
  return {};
}

static void test_lifecycle_mutex_access() {
  const std::wstring sid = current_logon_sid();
  check(!sid.empty(), "can resolve the current logon session SID");
  if (sid.empty())
    return;

  const std::wstring sddl =
      weasel::lifecycle_security::BuildLifecycleMutexSecuritySddl(sid);
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
    check(false, "can build the production-equivalent mutex DACL");
    return;
  }

  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.lpSecurityDescriptor = descriptor;
  const std::wstring name = L"Local\\WeaselLifecycleAccessTest-" +
                            std::to_wstring(GetCurrentProcessId()) + L"-" +
                            std::to_wstring(GetTickCount64());

  constexpr DWORD rights = SYNCHRONIZE | MUTEX_MODIFY_STATE;
  HANDLE creator = CreateMutexExW(&attributes, name.c_str(), 0, rights);
  check(creator != nullptr, "creator can create mutex with logon-session DACL");
  if (creator) {
    HANDLE other = CreateMutexExW(nullptr, name.c_str(), 0, rights);
    if (!other)
      std::cerr << "[debug] lifecycle mutex reopen GetLastError="
                << GetLastError() << std::endl;
    check(other != nullptr, "another handle can reopen with required rights");
    if (other) {
      check(WaitForSingleObject(other, 0) == WAIT_OBJECT_0,
            "reopened handle can acquire the lifecycle mutex");
      check(ReleaseMutex(other) != 0,
            "reopened handle can release the lifecycle mutex");
      CloseHandle(other);
    }
    CloseHandle(creator);
  }
  LocalFree(descriptor);
}
#endif

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

  const std::wstring mutex_sddl =
      weasel::lifecycle_security::BuildLifecycleMutexSecuritySddl(logon_sid);
  check(mutex_sddl.find(logon_sid) != std::wstring::npos,
        "lifecycle mutex DACL grants current logon session");
  check(mutex_sddl.find(L"0x00100001") != std::wstring::npos,
        "lifecycle mutex grants explicit sync and modify-state access");
  check(mutex_sddl.find(L";;;WD)") == std::wstring::npos,
        "lifecycle mutex never grants access to Everyone");
  check(
      weasel::lifecycle_security::BuildLifecycleMutexSecuritySddl(L"").empty(),
      "lifecycle mutex policy fails closed without a logon SID");

#if defined(_WIN32)
  test_lifecycle_mutex_access();
#endif

  std::cout << (g_failures ? "FAILED: " : "PASSED: ") << g_failures
            << " failure(s)" << std::endl;
  return g_failures ? 1 : 0;
}
