#include "stdafx.h"
#include "SecurityAttribute.h"
#include <PipeSecurityPolicy.h>
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
}

SecurityAttribute::~SecurityAttribute() {
  if (pd)
    ::LocalFree(pd);
}

SECURITY_ATTRIBUTES* SecurityAttribute::get_attr() {
  return &sa;
}
};  // namespace weasel
