// TestWeaselSetup.cpp : regression tests for WeaselSetup pure helpers
// (command-line unquoting for K15, bounded registry string reads for A14,
// recursive registry tree deletion for K13).
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
  check(unquote_argument(L"\"dir\"\"") == L"dir\"",
        "unquote strips only the outermost pair");
  check(unquote_argument(L"") == L"",
        "unquote of an empty argument is empty");
}

// 私有测试根键（结束即删，不触碰真实配置）
static const wchar_t* kTestKey = L"Software\\Rime\\WeaselSetupTest";

static HKEY open_test_key() {
  HKEY hKey = NULL;
  RegCreateKeyExW(HKEY_CURRENT_USER, kTestKey, 0, NULL, 0, KEY_SET_VALUE |
                    KEY_QUERY_VALUE, NULL, &hKey, NULL);
  return hKey;
}

static void set_test_value(HKEY hKey, const wchar_t* name, const void* data,
                           DWORD bytes, DWORD type) {
  RegSetValueExW(hKey, name, 0, type, (const BYTE*)data, bytes);
}

static void test_read_reg_sz() {
  HKEY hKey = open_test_key();
  check(hKey != NULL, "scratch key created");

  std::wstring out;

  // 常规带 NUL 的 REG_SZ
  const wchar_t* normal = L"D:\\rime user";
  set_test_value(hKey, L"Normal", normal,
                 (DWORD)((wcslen(normal) + 1) * sizeof(WCHAR)), REG_SZ);
  check(read_reg_sz(hKey, L"Normal", out) && out == normal,
        "regular NUL-terminated REG_SZ reads exactly");

  // 数据恰好填满缓冲且未 NUL 终止（A14 的越读场景）
  WCHAR full[MAX_PATH];
  wmemset(full, L'A', _countof(full));
  set_test_value(hKey, L"NoNul", full, sizeof(full), REG_SZ);
  check(read_reg_sz(hKey, L"NoNul", out) &&
            out.size() == _countof(full) &&
            out.find_first_not_of(L'A') == std::wstring::npos,
        "non-NUL-terminated full-buffer REG_SZ is truncated in bounds");

  // 内嵌 NUL 后跟残留字节：取首个 NUL 前
  const wchar_t embedded[] = L"ab\0cd\0";
  set_test_value(hKey, L"Embedded", embedded, sizeof(embedded), REG_SZ);
  check(read_reg_sz(hKey, L"Embedded", out) && out == L"ab",
        "string stops at the first embedded NUL");

  // 仅一个 NUL 的空串
  const wchar_t empty[] = L"";
  set_test_value(hKey, L"Empty", empty, sizeof(empty), REG_SZ);
  check(read_reg_sz(hKey, L"Empty", out) && out.empty(),
        "empty REG_SZ succeeds with an empty string");

  // 类型不匹配
  DWORD dword = 1;
  set_test_value(hKey, L"NotSz", &dword, sizeof(dword), REG_DWORD);
  check(!read_reg_sz(hKey, L"NotSz", out),
        "non-REG_SZ value is rejected");

  // 不存在的值
  check(!read_reg_sz(hKey, L"Missing", out),
        "missing value is rejected");

  RegCloseKey(hKey);
  RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
}

// K13：卸载清理需整树删除（RegDeleteKey 遇子键即失败），
// 验证 delete_reg_tree 对嵌套键树语义正确
static void test_delete_reg_tree() {
  // 构造 值 + 两层嵌套子键（含子键自身的值）
  HKEY hKey = open_test_key();
  check(hKey != NULL, "scratch key created");
  DWORD dword = 1;
  set_test_value(hKey, L"Top", &dword, sizeof(dword), REG_DWORD);
  RegCloseKey(hKey);

  const std::wstring updates = std::wstring(kTestKey) + L"\\Updates";
  const std::wstring channel = updates + L"\\Channel";
  HKEY hSub = NULL;
  check(RegCreateKeyExW(HKEY_CURRENT_USER, updates.c_str(), 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &hSub,
                        NULL) == ERROR_SUCCESS,
        "nested subkey created");
  set_test_value(hSub, L"CheckForUpdates", &dword, sizeof(dword), REG_DWORD);
  RegCloseKey(hSub);
  HKEY hLeaf = NULL;
  check(RegCreateKeyExW(HKEY_CURRENT_USER, channel.c_str(), 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &hLeaf,
                        NULL) == ERROR_SUCCESS,
        "second-level subkey created");
  set_test_value(hLeaf, L"Name", L"testing", sizeof(L"testing"), REG_SZ);
  RegCloseKey(hLeaf);

  check(delete_reg_tree(HKEY_CURRENT_USER, kTestKey) == ERROR_SUCCESS,
        "key tree with nested subkeys is deleted recursively");
  HKEY hCheck = NULL;
  check(RegOpenKeyW(HKEY_CURRENT_USER, kTestKey, &hCheck) ==
            ERROR_FILE_NOT_FOUND,
        "no trace of the tree is left behind");

  // 键不存在：报告 ERROR_FILE_NOT_FOUND 而非误报成功
  check(delete_reg_tree(HKEY_CURRENT_USER, kTestKey) == ERROR_FILE_NOT_FOUND,
        "deleting a missing tree reports ERROR_FILE_NOT_FOUND");
}

int main() {
  test_unquote_argument();
  test_read_reg_sz();
  test_delete_reg_tree();

  if (g_failures) {
    printf("%d failure(s)\n", g_failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
