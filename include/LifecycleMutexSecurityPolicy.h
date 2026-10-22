#pragma once

#include <string>

namespace weasel::lifecycle_security {

// SYNCHRONIZE (0x00100000) | MUTEX_MODIFY_STATE (0x00000001).
// Do not reuse the named-pipe's GENERIC_READ/GENERIC_WRITE ACEs:
// those rights have object-specific mappings and need not grant SYNCHRONIZE.
// Retain the low-integrity compatibility and the same logon-session scope.
inline std::wstring BuildLifecycleMutexSecuritySddl(
    const std::wstring& logon_sid) {
  if (logon_sid.empty())
    return {};

  return L"S:(ML;;NW;;;LW)D:(A;;GA;;;SY)(A;;0x00100001;;;" +
         logon_sid + L")(A;;0x00100001;;;AC)";
}

}  // namespace weasel::lifecycle_security
