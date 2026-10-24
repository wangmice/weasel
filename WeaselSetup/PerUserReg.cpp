#include <Windows.h>
#include <sddl.h>
#include <vector>

#include "PerUserReg.h"

std::wstring current_user_sid() {
  std::wstring sid;
  HANDLE token = NULL;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return sid;
  DWORD len = 0;
  GetTokenInformation(token, TokenUser, NULL, 0, &len);
  if (len) {
    std::vector<BYTE> buf(len);
    if (GetTokenInformation(token, TokenUser, buf.data(), len, &len)) {
      LPWSTR str = NULL;
      if (ConvertSidToStringSidW(((TOKEN_USER*)buf.data())->User.Sid, &str)) {
        sid = str;
        LocalFree(str);
      }
    }
  }
  CloseHandle(token);
  return sid;
}

static std::wstring g_per_user_origin_sid;

void set_per_user_origin_sid(const std::wstring& sid) {
  if (sid.empty()) {  // explicit reset
    g_per_user_origin_sid.clear();
    return;
  }
  if (sid.rfind(L"S-1-", 0) == 0)  // accept only well-formed SIDs
    g_per_user_origin_sid = sid;
}

bool per_user_redirected() {
  return !g_per_user_origin_sid.empty();
}

HKEY per_user_root() {
  return per_user_redirected() ? HKEY_USERS : HKEY_CURRENT_USER;
}

std::wstring per_user_subkey(const wchar_t* sub) {
  return per_user_redirected() ? g_per_user_origin_sid + L"\\" + sub
                               : std::wstring(sub);
}

std::wstring per_user_default_dir() {
  if (per_user_redirected()) {
    // %APPDATA% of the elevated process belongs to the admin; read the
    // invoking user's from their loaded profile environment
    HKEY hKey;
    if (RegOpenKeyW(HKEY_USERS,
                    per_user_subkey(L"Volatile Environment").c_str(),
                    &hKey) == ERROR_SUCCESS) {
      WCHAR path[MAX_PATH] = {0};
      DWORD len = sizeof(path) - sizeof(WCHAR);  // keep room for the NUL
      DWORD type = 0;
      bool ok = RegQueryValueExW(hKey, L"APPDATA", NULL, &type, (LPBYTE)path,
                                 &len) == ERROR_SUCCESS &&
                type == REG_SZ && path[0] != L'\0';
      RegCloseKey(hKey);
      if (ok)
        return std::wstring(path) + L"\\Rime";
    }
  }
  WCHAR _path[MAX_PATH] = {0};
  ExpandEnvironmentStringsW(L"%APPDATA%\\Rime", _path, _countof(_path));
  return std::wstring(_path);
}

void apply_orig_sid_param(LPTSTR lpCmdLine) {
  if (wchar_t* pos = wcsstr(lpCmdLine, L" /origsid:")) {
    *pos = L'\0';
    set_per_user_origin_sid(pos + wcslen(L" /origsid:"));
  }
}

std::wstring with_orig_sid_param(const std::wstring& lpCmdLine) {
  std::wstring sid = current_user_sid();
  if (sid.empty())
    return lpCmdLine;
  return lpCmdLine + L" /origsid:" + sid;
}
