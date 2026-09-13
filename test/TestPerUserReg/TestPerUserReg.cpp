// TestPerUserReg.cpp : regression tests for the per-user hive redirection
// used by the elevated installer (N6).
//
// Runs against the real registry: reads compare HKEY_USERS\<sid> with HKCU,
// writes go to a scratch key under HKCU\Software\Rime (deleted afterwards).

#include <Windows.h>
#include <stdio.h>

#include "../../WeaselSetup/PerUserReg.h"

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    printf("[FAIL] %s\n", what);
  } else {
    printf("[ok] %s\n", what);
  }
}

static bool read_reg_sz(HKEY root, const wchar_t* sub, const wchar_t* value,
                        std::wstring& out) {
  HKEY hKey;
  if (RegOpenKeyW(root, sub, &hKey) != ERROR_SUCCESS)
    return false;
  WCHAR buf[MAX_PATH] = {0};
  DWORD len = sizeof(buf) - sizeof(WCHAR);
  DWORD type = 0;
  bool ok = RegQueryValueExW(hKey, value, NULL, &type, (LPBYTE)buf, &len) ==
                ERROR_SUCCESS &&
            type == REG_SZ;
  RegCloseKey(hKey);
  if (ok)
    out = buf;
  return ok;
}

int main() {
  // 1) the invoking user's SID is captured and arms the redirection
  std::wstring sid = current_user_sid();
  check(sid.rfind(L"S-1-", 0) == 0, "current_user_sid returns S-1-5-*");
  set_per_user_origin_sid(sid);
  check(per_user_redirected() && per_user_root() == HKEY_USERS,
        "sid arms redirection to HKEY_USERS");
  check(per_user_subkey(L"Software\\Rime\\Weasel") ==
            sid + L"\\Software\\Rime\\Weasel",
        "subkey is prefixed with the sid");

  // 2) HKEY_USERS\<sid> shows the same weasel config as HKCU (read-only)
  std::wstring via_hkcu, via_hku;
  bool has_hkcu =
      read_reg_sz(HKEY_CURRENT_USER, L"Software\\Rime\\Weasel",
                  L"RimeUserDir", via_hkcu);
  bool has_hku = read_reg_sz(per_user_root(),
                             per_user_subkey(L"Software\\Rime\\Weasel").c_str(),
                             L"RimeUserDir", via_hku);
  check(has_hkcu == has_hku && (!has_hkcu || via_hkcu == via_hku),
        "HKCU and HKU<sid> views agree on RimeUserDir");

  // 3) a write through the redirected path lands in the real user hive
  HKEY hKey;
  RegCreateKeyW(per_user_root(), per_user_subkey(L"Software\\Rime\\WeaselTest").c_str(), &hKey);
  RegSetValueExW(hKey, L"Probe", 0, REG_SZ, (const BYTE*)L"ok",
                 3 * sizeof(WCHAR));
  RegCloseKey(hKey);
  std::wstring probe;
  check(read_reg_sz(HKEY_CURRENT_USER, L"Software\\Rime\\WeaselTest",
                    L"Probe", probe) &&
            probe == L"ok",
        "redirected write is visible through HKCU");
  RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Rime\\WeaselTest");

  // 4) redirected default dir resolves the invoking user's %APPDATA%
  std::wstring appdata_hku;
  bool got_env = read_reg_sz(
      HKEY_USERS, per_user_subkey(L"Volatile Environment").c_str(),
      L"APPDATA", appdata_hku);
  WCHAR expand[MAX_PATH] = {0};
  ExpandEnvironmentStringsW(L"%APPDATA%", expand, MAX_PATH);
  check(got_env && per_user_default_dir() ==
                       std::wstring(expand) + L"\\Rime",
        "redirected default dir is the invoking user's %APPDATA%\\Rime");

  // 5) without redirection everything addresses HKCU directly
  set_per_user_origin_sid(L"");
  check(!per_user_redirected() && per_user_root() == HKEY_CURRENT_USER &&
            per_user_subkey(L"Software\\Rime\\Weasel") ==
                L"Software\\Rime\\Weasel" &&
            per_user_default_dir() == std::wstring(expand) + L"\\Rime",
        "no redirection -> HKCU and local %APPDATA%");

  // 6) command-line parameter: stripped and armed
  wchar_t cmd[128];
  swprintf_s(cmd, L"/i /origsid:%s", sid.c_str());
  apply_orig_sid_param(cmd);
  check(wcscmp(cmd, L"/i") == 0 && per_user_redirected(),
        "apply_orig_sid_param strips the parameter and arms redirection");

  // 7) injection-style garbage sids are rejected (fresh start, as in a
  // newly launched process)
  set_per_user_origin_sid(L"");
  wchar_t bad[64];
  swprintf_s(bad, L"/i /origsid:..\\Software");
  apply_orig_sid_param(bad);
  check(!per_user_redirected(),
        "malformed sid (path injection) rejected");

  // 8) relaunch helper appends the parameter
  std::wstring params = with_orig_sid_param(L"/i");
  check(params == std::wstring(L"/i /origsid:") + sid,
        "with_orig_sid_param appends /origsid:<sid>");

  printf(g_failures ? "FAILED: %d failure(s)\n" : "PASSED: %d failure(s)\n",
         g_failures);
  return g_failures ? 1 : 0;
}
