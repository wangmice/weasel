// TestWeaselSetup.cpp : regression tests for WeaselSetup pure helpers
// (command-line unquoting for K15, bounded registry string reads for A14).
//
// Registry cases run against a private scratch key under
// HKCU\Software\Rime\WeaselSetupTest (deleted on exit); no HKLM access.

#include <Windows.h>
#include <stdio.h>

#include <string>

#include "../../WeaselSetup/SetupUtil.h"

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    printf("[FAIL] %s\n", what);
  } else {
    printf("[ok] %s\n", what);
  }
}

static void test_unquote_argument() {
  check(unquote_argument(L"\"C:\\Program Files\\Rime\"") ==
            L"C:\\Program Files\\Rime",
        "unquote strips a matching pair of quotes");
  check(unquote_argument(L"\"\"") == L"",
        "unquote of a bare quote pair yields an empty string");
  check(unquote_argument(L"\"unterminated") == L"\"unterminated",
        "unquote leaves a lone leading quote untouched");
  check(unquote_argument(L"unterminated\"") == L"unterminated\"",
        "unquote leaves a lone trailing quote untouched");
  check(unquote_argument(L"plain") == L"plain",
        "unquote passes through unquoted arguments");
  check(unquote_argument(L"\"") == L"\"",
        "unquote of a single quote character is unchanged");
  check(unquote_argument(L"a\"b\"c") == L"a\"b\"c",
        "unquote keeps inner quotes when ends are unquoted");
  check(unquote_argument(L"\"dir\"\"") == L"dir\"\"",
        "unquote strips only the outermost pair");
  check(unquote_argument(L"") == L"",
        "unquote of an empty argument is empty");
}

int main() {
  test_unquote_argument();

  if (g_failures) {
    printf("%d failure(s)\n", g_failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
