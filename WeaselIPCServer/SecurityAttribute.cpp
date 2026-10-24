#include "stdafx.h"
#include "SecurityAttribute.h"
#include <PipeSecurityPolicy.h>
#include <LifecycleMutexSecurityPolicy.h>
#include <Sddl.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

bool GetCurrentLogonSid(std::wstring& sid) {
  HANDLE token = nullptr;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
    return false;

  DWORD size = 0;
  ::GetTokenInformation(token, TokenGroups, nullptr, 0, &size);
  if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
    ::CloseHandle(token);
    return false;
  }

  std::vector<BYTE> buffer(size);
  if (!::GetTokenInformation(token, TokenGroups, buffer.data(), size, &size)) {
    ::CloseHandle(token);
    return false;
  }
  ::CloseHandle(token);

  const auto groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
  for (DWORD i = 0; i < groups->GroupCount; ++i) {
    const auto& group = groups->Groups[i];
    if ((group.Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID)
      continue;

    LPWSTR sid_text = nullptr;
    if (!::ConvertSidToStringSidW(group.Sid, &sid_text))
      return false;
    sid.assign(sid_text);
    ::LocalFree(sid_text);
    return true;
  }
  return false;
}

}  // namespace

namespace weasel {

void SecurityAttribute::_Init() {
  std::wstring logon_sid;
  if (!GetCurrentLogonSid(logon_sid))
    throw std::runtime_error("failed to resolve the current logon SID");

  const std::wstring sddl = pipe_security::BuildPipeSecuritySddl(logon_sid);
  if (sddl.empty() || !::ConvertStringSecurityDescriptorToSecurityDescriptorW(
                          sddl.c_str(), SDDL_REVISION_1, &pd, nullptr)) {
    throw std::runtime_error(
        "failed to create the Weasel pipe security descriptor");
  }

  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = pd;
  sa.bInheritHandle = TRUE;

  // Pipe rights (GRGW) do not imply the synchronize rights required to
  // reopen a mutex. Use a separate type-specific descriptor, without
  // broadening access to the named pipe.
  const std::wstring mutex_sddl =
      lifecycle_security::BuildLifecycleMutexSecuritySddl(logon_sid);
  if (mutex_sddl.empty() ||
      !::ConvertStringSecurityDescriptorToSecurityDescriptorW(
          mutex_sddl.c_str(), SDDL_REVISION_1, &mutex_pd, nullptr)) {
    ::LocalFree(pd);
    pd = nullptr;
    throw std::runtime_error(
        "failed to create the Weasel lifecycle mutex security descriptor");
  }
  mutex_sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  mutex_sa.lpSecurityDescriptor = mutex_pd;
  mutex_sa.bInheritHandle = FALSE;
}

SecurityAttribute::~SecurityAttribute() {
  if (mutex_pd)
    ::LocalFree(mutex_pd);
  if (pd)
    ::LocalFree(pd);
}

SECURITY_ATTRIBUTES* SecurityAttribute::get_attr() {
  return &sa;
}

SECURITY_ATTRIBUTES* SecurityAttribute::get_mutex_attr() {
  return &mutex_sa;
}
};  // namespace weasel
