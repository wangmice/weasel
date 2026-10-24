// TestBstrUtil.cpp : regression tests for WeaselTSF's BSTR allocation (K4).
//
// Pins the contract of AllocBstr (used by CCandidateList::GetString):
// SysStringLen must equal the source length; the +1 that used to be passed
// to SysAllocStringLen embedded a trailing NUL in the BSTR and made
// SysStringLen report one code unit too many.

#include <Windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string>

#include "../../WeaselTSF/BstrUtil.h"

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    printf("[FAIL] %s\n", what);
  } else {
    printf("[ok] %s\n", what);
  }
}

static void roundtrip(const char* label, const std::wstring& s) {
  BSTR b = AllocBstr(s);
  check(b != nullptr, label);
  if (!b)
    return;
  check(SysStringLen(b) == s.size(), label);  // K4: length must match exactly
  check(wcscmp(b, s.c_str()) == 0, label);
  SysFreeString(b);
}

int main() {
  roundtrip("empty string", std::wstring());
  roundtrip("ascii", std::wstring(L"weasel"));
  roundtrip("chinese", std::wstring(L"候選乙"));
  roundtrip("surrogate pair", std::wstring(L"\U0001F600"));  // 2 code units

  // null termination is still guaranteed by SysAllocStringLen semantics
  std::wstring s(L"abc");
  BSTR b = AllocBstr(s);
  check(b[3] == L'\0', "explicit NUL terminator present");
  SysFreeString(b);

  printf(g_failures ? "FAILED: %d failure(s)\n" : "PASSED: %d failure(s)\n",
         g_failures);
  return g_failures ? 1 : 0;
}
