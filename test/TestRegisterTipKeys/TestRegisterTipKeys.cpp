// TestRegisterTipKeys.cpp : regression tests for TSF registration helpers.
//
// TIP-key cleanup uses scratch HKCU subtrees only; profile environment tests
// change only the current process environment and restore it before exit.

#include <Windows.h>
#include <stdio.h>

#include <string>
#include <vector>

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

class ScopedProfileEnvironment {
 public:
  ScopedProfileEnvironment() {
    SetLastError(ERROR_SUCCESS);
    DWORD required =
        GetEnvironmentVariableW(L"TEXTSERVICE_PROFILE", nullptr, 0);
    if (required == 0) {
      existed_ = GetLastError() != ERROR_ENVVAR_NOT_FOUND;
      return;
    }

    while (required != 0) {
      std::vector<wchar_t> buffer(required);
      SetLastError(ERROR_SUCCESS);
      const DWORD copied = GetEnvironmentVariableW(
          L"TEXTSERVICE_PROFILE", buffer.data(), required);
      if (copied == 0) {
        existed_ = GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        value_.clear();
        return;
      }
      if (copied < required) {
        existed_ = true;
        value_.assign(buffer.data(), copied);
        return;
      }
      required = copied;
    }
  }

  ~ScopedProfileEnvironment() {
    SetEnvironmentVariableW(L"TEXTSERVICE_PROFILE",
                            existed_ ? value_.c_str() : nullptr);
  }

 private:
  bool existed_ = false;
  std::wstring value_;
};

static void test_profile_registration_boundaries() {
  check(TextServiceDescriptionCch(L"小狼毫") == 3,
        "RegisterProfile description length is measured in WCHARs");
  check(TextServiceDescriptionCch(L"Weasel") == 6,
        "ASCII description length remains a character count");

  ScopedProfileEnvironment restore;
  const std::wstring long_profile(300, L'x');
  check(SetEnvironmentVariableW(L"TEXTSERVICE_PROFILE", long_profile.c_str()) !=
            FALSE,
        "install long TEXTSERVICE_PROFILE test value");
  check(ReadTextServiceProfileEnvironment() == long_profile,
        "TEXTSERVICE_PROFILE longer than 100 WCHARs is read without truncation");

  check(SetEnvironmentVariableW(L"TEXTSERVICE_PROFILE", L"hant") != FALSE,
        "install supported profile test value");
  check(ReadTextServiceProfileEnvironment() == L"hant",
        "supported profile value is preserved exactly");

  check(SetEnvironmentVariableW(L"TEXTSERVICE_PROFILE", nullptr) != FALSE,
        "remove profile environment test value");
  check(ReadTextServiceProfileEnvironment().empty(),
        "missing TEXTSERVICE_PROFILE produces an empty profile");
}

int main() {
  test_profile_registration_boundaries();

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
