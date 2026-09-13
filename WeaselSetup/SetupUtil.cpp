#include "SetupUtil.h"

#include <algorithm>

std::wstring unquote_argument(const std::wstring& arg) {
  if (arg.size() >= 2 && arg.front() == L'"' && arg.back() == L'"')
    return arg.substr(1, arg.size() - 2);
  return arg;
}

bool read_reg_sz(HKEY hKey, const wchar_t* value_name, std::wstring& out) {
  WCHAR value[MAX_PATH] = {0};
  DWORD len = sizeof(value);
  DWORD type = 0;
  if (RegQueryValueExW(hKey, value_name, NULL, &type, (LPBYTE)value, &len) !=
          ERROR_SUCCESS ||
      type != REG_SZ)
    return false;
  const size_t chars = std::min<size_t>(len / sizeof(WCHAR), _countof(value));
  size_t end = 0;
  while (end < chars && value[end] != L'\0')
    ++end;
  out.assign(value, end);
  return true;
}
