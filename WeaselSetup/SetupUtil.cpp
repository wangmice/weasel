#include "SetupUtil.h"

#include <algorithm>
#include <vector>

std::wstring unquote_argument(const std::wstring& arg) {
  if (arg.size() >= 2 && arg.front() == L'"' && arg.back() == L'"')
    return arg.substr(1, arg.size() - 2);
  return arg;
}

bool read_reg_sz(HKEY hKey, const wchar_t* value_name, std::wstring& out) {
  WCHAR value[MAX_PATH] = {0};
  std::vector<WCHAR> grown;
  WCHAR* buf = value;
  DWORD len = sizeof(value);
  DWORD type = 0;
  LSTATUS rc =
      RegQueryValueExW(hKey, value_name, NULL, &type, (LPBYTE)buf, &len);
  if (rc == ERROR_MORE_DATA && type == REG_SZ) {
    // Windows 11 24H2 起注册表可为 REG_SZ 追加 NUL 终止符，实际存储可大于
    // 写入长度；按需扩容重查一次，仍以首个 NUL 定界
    grown.resize(len / sizeof(WCHAR) + 1);
    buf = grown.data();
    len = (DWORD)(grown.size() * sizeof(WCHAR));
    rc = RegQueryValueExW(hKey, value_name, NULL, &type, (LPBYTE)buf, &len);
  }
  if (rc != ERROR_SUCCESS || type != REG_SZ)
    return false;
  const size_t cap = grown.empty() ? (size_t)_countof(value) : grown.size();
  const size_t chars = std::min<size_t>(len / sizeof(WCHAR), cap);
  size_t end = 0;
  while (end < chars && buf[end] != L'\0')
    ++end;
  out.assign(buf, end);
  return true;
}

LSTATUS delete_reg_tree(HKEY root, const wchar_t* subkey) {
  return RegDeleteTreeW(root, subkey);
}
