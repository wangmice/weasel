// TestFindIME.cpp : regression tests for the IME layout lookup (N1).
//
// FindIMEUnderKey takes the registry root as a parameter, so the tests build
// a controlled layout tree under HKEY_CURRENT_USER (no elevation needed) and
// check the scan logic, the HKL mapping, and that the scan neither leaks nor
// closes unopened keys.

#include <Windows.h>
#include <stdio.h>

#include "../../WeaselTSF/FindIME.h"

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    printf("[FAIL] %s\n", what);
  } else {
    printf("[ok] %s\n", what);
  }
}

static const LANGID kLang = 0x0804;  // hans
static const wchar_t* kTestRoot = L"Software\\Rime\\WeaselTestLayouts";

static HKEY open_test_root(bool create) {
  HKEY hKey = NULL;
  if (create) {
    RegCreateKeyExW(HKEY_CURRENT_USER, kTestRoot, 0, NULL, 0, KEY_ALL_ACCESS,
                    NULL, &hKey, NULL);
  } else {
    RegOpenKeyExW(HKEY_CURRENT_USER, kTestRoot, 0, KEY_READ, &hKey);
  }
  return hKey;
}

// a layout entry that mimics HKLM\...\Keyboard Layouts\E0xx0804
static void set_layout(LPCWSTR name, const wchar_t* imeFile) {
  WCHAR sub[MAX_PATH];
  swprintf_s(sub, L"%s\\%s", kTestRoot, name);
  HKEY hKey;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, sub, 0, NULL, 0, KEY_ALL_ACCESS, NULL,
                      &hKey, NULL) != ERROR_SUCCESS)
    return;
  if (imeFile)
    RegSetValueExW(hKey, L"Ime File", 0, REG_SZ, (const BYTE*)imeFile,
                   (DWORD)((wcslen(imeFile) + 1) * sizeof(WCHAR)));
  RegCloseKey(hKey);
}

static DWORD process_handle_count() {
  DWORD count = 0;
  HANDLE pseudo = GetCurrentProcess();
  GetProcessHandleCount(pseudo, &count);
  return count;
}

int main() {
  // isolate: start from a clean tree
  RegDeleteTreeW(HKEY_CURRENT_USER, kTestRoot);

  // 1) empty root: no layout found, scan completes
  HKEY root = open_test_root(/*create*/ true);
  check(root != NULL, "setup: create test root");
  check(FindIMEUnderKey(root, kLang, L"weasel.ime") == NULL,
        "empty root returns NULL");
  RegCloseKey(root);

  // 2) exact mapping: E0200804 holds weasel.ime
  set_layout(L"E0200804", L"weasel.ime");
  root = open_test_root(false);
  HKL hkl = FindIMEUnderKey(root, kLang, L"weasel.ime");
  check(hkl == (HKL)(DWORD_PTR)0xE0200804, "E0200804 with weasel.ime mapped");
  RegCloseKey(root);

  // 3) scan returns the first hit; match is case-insensitive; other files
  // are skipped
  set_layout(L"E0200804", L"notweasel.ime");
  set_layout(L"E0210804", L"Weasel.IME");
  set_layout(L"E0220804", L"other.ime");
  root = open_test_root(false);
  check(FindIMEUnderKey(root, kLang, L"weasel.ime") == (HKL)(DWORD_PTR)0xE0210804,
        "case-insensitive first hit, other.ime skipped");
  check(FindIMEUnderKey(root, kLang, L"other.ime") == (HKL)(DWORD_PTR)0xE0220804,
        "different ime file matches its own layout");
  RegCloseKey(root);

  // 4) a different LANGID scans its own suffix range
  check(FindIMEUnderKey(open_test_root(false), 0x0404, L"weasel.ime") == NULL,
        "hant (0x0404) range is independent");

  // 5) highest slot boundary is included in the scan
  set_layout(L"E0FF0804", L"boundary.ime");
  root = open_test_root(false);
  check(FindIMEUnderKey(root, kLang, L"boundary.ime") == (HKL)(DWORD_PTR)0xE0FF0804,
        "E0FF0804 (last slot) is scanned");
  RegCloseKey(root);

  // 6) no handle leak across a full scan over many missing keys
  DWORD before = process_handle_count();
  for (int i = 0; i < 100; ++i) {
    root = open_test_root(false);
    FindIMEUnderKey(root, kLang, L"weasel.ime");
    RegCloseKey(root);
  }
  DWORD after = process_handle_count();
  check(after == before, "100 scans leak no handles");

  // 7) NULL root fails gracefully instead of crashing
  check(FindIMEUnderKey(NULL, kLang, L"weasel.ime") == NULL,
        "NULL root returns NULL");

  // 8) production FindIME against the real HKLM tree (weasel.ime may or may
  // not be installed): must complete and answer deterministically
  HKL real1 = FindIME(kLang);
  HKL real2 = FindIME(kLang);
  check(real1 == real2, "FindIME(HKLM) is deterministic");

  RegDeleteTreeW(HKEY_CURRENT_USER, kTestRoot);
  printf(g_failures ? "FAILED: %d failure(s)\n" : "PASSED: %d failure(s)\n",
         g_failures);
  return g_failures ? 1 : 0;
}
