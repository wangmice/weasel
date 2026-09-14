// TestRegisterTipKeys.cpp : regression tests for the dual-location TIP key
// cleanup in UnregisterServer (K5).
//
// DeleteTipKeyUnderRoot takes the registry root as a parameter, so the tests
// plant scratch TIP subtrees under HKEY_CURRENT_USER with a fake CLSID and
// verify that the "registered" cleanup targets the correctly spelled
// Microsoft path while the "legacy" cleanup keeps the historical Microsft
// typo, each removing only its own subtree. The real HKLM/HKCR locations are
// never touched.

#include <Windows.h>
#include <stdio.h>

#include "../../WeaselTSF/Register.h"

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    printf("[FAIL] %s\n", what);
  } else {
    printf("[ok] %s\n", what);
  }
}

// scratch CLSID: never registered anywhere, inert for TSF
static const GUID kFakeClsid = {
    0x5a5e13aa,
    0x62b5,
    0x4c61,
    {0x8d, 0x9e, 0x07, 0xf0, 0x11, 0x22, 0x33, 0x44}};

// string form as produced by CLSIDToStringA (uppercase, GUID map order)
static const wchar_t kRealPath[] =
    L"Software\\Microsoft\\CTF\\TIP\\{5A5E13AA-62B5-4C61-8D9E-07F011223344}";
static const wchar_t kRealSub[] =
    L"Software\\Microsoft\\CTF\\TIP\\"
    L"{5A5E13AA-62B5-4C61-8D9E-07F011223344}\\LanguageProfile";
static const wchar_t kTypoPath[] =
    L"Software\\Microsft\\CTF\\TIP\\{5A5E13AA-62B5-4C61-8D9E-07F011223344}";
static const wchar_t kTypoSub[] =
    L"Software\\Microsft\\CTF\\TIP\\"
    L"{5A5E13AA-62B5-4C61-8D9E-07F011223344}\\LanguageProfile";

static bool key_exists(const wchar_t* path) {
  HKEY hKey;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &hKey) !=
      ERROR_SUCCESS)
    return false;
  RegCloseKey(hKey);
  return true;
}

static void plant(const wchar_t* path) {
  HKEY hKey;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_ALL_ACCESS,
                      NULL, &hKey, NULL) != ERROR_SUCCESS)
    return;
  RegSetValueExW(hKey, L"probe", 0, REG_SZ, (const BYTE*)L"x",
                 2 * sizeof(WCHAR));
  RegCloseKey(hKey);
}

int main() {
  // isolate: start without either scratch tree
  RegDeleteTreeW(HKEY_CURRENT_USER, kRealPath);
  RegDeleteTreeW(HKEY_CURRENT_USER, kTypoPath);

  // 1) missing keys are not an error
  check(DeleteTipKeyUnderRoot(HKEY_CURRENT_USER, TipKeyPath::Registered,
                              kFakeClsid) != FALSE,
        "absent registered key deletes as success");
  check(DeleteTipKeyUnderRoot(HKEY_CURRENT_USER, TipKeyPath::LegacyTypo,
                              kFakeClsid) != FALSE,
        "absent legacy key deletes as success");

  // 2) registered cleanup removes the correctly spelled subtree (nested keys
  // included) and leaves the typo tree alone
  plant(kRealPath);
  plant(kRealSub);
  plant(kTypoPath);
  check(DeleteTipKeyUnderRoot(HKEY_CURRENT_USER, TipKeyPath::Registered,
                              kFakeClsid) != FALSE &&
            !key_exists(kRealPath) && !key_exists(kRealSub),
        "registered cleanup removes the Microsoft subtree recursively");
  check(key_exists(kTypoPath), "registered cleanup leaves the typo tree");

  // 3) legacy cleanup removes the misspelled subtree
  plant(kTypoSub);
  check(DeleteTipKeyUnderRoot(HKEY_CURRENT_USER, TipKeyPath::LegacyTypo,
                              kFakeClsid) != FALSE &&
            !key_exists(kTypoPath) && !key_exists(kTypoSub),
        "legacy cleanup removes the Microsft subtree recursively");

  // cleanup: only the fake CLSID subtrees and the inert typo root we created
  RegDeleteTreeW(HKEY_CURRENT_USER, kRealPath);
  RegDeleteTreeW(HKEY_CURRENT_USER, kTypoPath);
  RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsft");

  printf(g_failures ? "FAILED: %d failure(s)\n" : "PASSED: %d failure(s)\n",
         g_failures);
  return g_failures ? 1 : 0;
}
