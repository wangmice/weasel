#pragma once
#include <winnt.h>   // for security attributes constants
#include <aclapi.h>  // for ACL

namespace weasel {
class SecurityAttribute {
 private:
  PSECURITY_DESCRIPTOR pd = nullptr;
  SECURITY_ATTRIBUTES sa{};
  void _Init();

 public:
  SecurityAttribute() { _Init(); }
  ~SecurityAttribute();
  SecurityAttribute(const SecurityAttribute&) = delete;
  SecurityAttribute& operator=(const SecurityAttribute&) = delete;

  SECURITY_ATTRIBUTES* get_attr();
};
};  // namespace weasel
