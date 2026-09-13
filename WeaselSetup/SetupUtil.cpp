#include "SetupUtil.h"

std::wstring unquote_argument(const std::wstring& arg) {
  if (arg.size() >= 2 && arg.front() == L'"' && arg.back() == L'"')
    return arg.substr(1, arg.size() - 2);
  return arg;
}
