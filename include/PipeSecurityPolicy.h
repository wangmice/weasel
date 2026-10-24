#pragma once

#include <string>

namespace weasel::pipe_security {

inline std::wstring BuildPipeSecuritySddl(const std::wstring& logon_sid) {
  if (logon_sid.empty())
    return {};

  // Normal desktop clients must belong to the server's logon session.
  // AppContainer clients additionally need the ALL APPLICATION PACKAGES ACE;
  // Windows intersects the normal-token and AppContainer SID grants.
  return L"S:(ML;;NW;;;LW)D:(A;;GA;;;SY)(A;;GRGW;;;" + logon_sid +
         L")(A;;GRGW;;;AC)";
}

}  // namespace weasel::pipe_security
