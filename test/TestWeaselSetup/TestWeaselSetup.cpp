// TestWeaselSetup.cpp : regression tests for WeaselSetup pure helpers
// (command-line unquoting for K15, bounded registry string reads for A14,
// recursive registry tree deletion for K13).
//
// Registry cases run against a private scratch key under
// HKCU\Software\Rime\WeaselSetupTest (deleted on exit); no HKLM access.

#include <Windows.h>
#include <stdio.h>

#include <string>
#include <vector>
#include <chrono>
#include <iomanip>

#include "../../WeaselSetup/ProfileSwitchTransaction.h"
#include "../../WeaselSetup/TsfUninstallTransaction.h"
#include "../../WeaselSetup/SetupUtil.h"
#include <WeaselUtility.h>

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
  check(unquote_argument(L"") == L"", "unquote of an empty argument is empty");
}

// 私有测试根键（结束即删，不触碰真实配置）
static const wchar_t* kTestKey = L"Software\\Rime\\WeaselSetupTest";

static HKEY open_test_key() {
  HKEY hKey = NULL;
  RegCreateKeyExW(HKEY_CURRENT_USER, kTestKey, 0, NULL, 0,
                  KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &hKey, NULL);
  return hKey;
}

static void set_test_value(HKEY hKey,
                           const wchar_t* name,
                           const void* data,
                           DWORD bytes,
                           DWORD type) {
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
  check(read_reg_sz(hKey, L"NoNul", out) && out.size() == _countof(full) &&
            out.find_first_not_of(L'A') == std::wstring::npos,
        "non-NUL-terminated full-buffer REG_SZ is truncated in bounds");

  // 超过 MAX_PATH 的未 NUL 终止 REG_SZ：必经 ERROR_MORE_DATA 扩容路径。
  // Windows 11 24H2 起注册表可为 REG_SZ 追加终止符，实际存储可大于写入
  // 长度，读取不得依赖写入尺寸
  const size_t kLong = MAX_PATH + 40;
  std::vector<WCHAR> longBuf(kLong, L'B');
  set_test_value(hKey, L"TooLong", longBuf.data(),
                 (DWORD)(kLong * sizeof(WCHAR)), REG_SZ);
  check(read_reg_sz(hKey, L"TooLong", out) && out.size() == kLong &&
            out.find_first_not_of(L'B') == std::wstring::npos,
        "over-long non-NUL REG_SZ is read via a grown buffer");

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
  check(!read_reg_sz(hKey, L"NotSz", out), "non-REG_SZ value is rejected");

  // 不存在的值
  check(!read_reg_sz(hKey, L"Missing", out), "missing value is rejected");

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
                        KEY_SET_VALUE, NULL, &hSub, NULL) == ERROR_SUCCESS,
        "nested subkey created");
  set_test_value(hSub, L"CheckForUpdates", &dword, sizeof(dword), REG_DWORD);
  RegCloseKey(hSub);
  HKEY hLeaf = NULL;
  check(RegCreateKeyExW(HKEY_CURRENT_USER, channel.c_str(), 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &hLeaf, NULL) == ERROR_SUCCESS,
        "second-level subkey created");
  set_test_value(hLeaf, L"Name", L"testing", sizeof(L"testing"), REG_SZ);
  RegCloseKey(hLeaf);

  check(delete_reg_tree(HKEY_CURRENT_USER, kTestKey) == ERROR_SUCCESS,
        "key tree with nested subkeys is deleted recursively");
  HKEY hCheck = NULL;
  check(
      RegOpenKeyW(HKEY_CURRENT_USER, kTestKey, &hCheck) == ERROR_FILE_NOT_FOUND,
      "no trace of the tree is left behind");

  // 键不存在：报告 ERROR_FILE_NOT_FOUND 而非误报成功
  check(delete_reg_tree(HKEY_CURRENT_USER, kTestKey) == ERROR_FILE_NOT_FOUND,
        "deleting a missing tree reports ERROR_FILE_NOT_FOUND");
}

static void test_reg_get_string_value() {
  const std::wstring key = L"Software\\Rime\\WeaselUtilityTest-" +
                           std::to_wstring(GetCurrentProcessId());
  HKEY hKey = NULL;
  check(RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, NULL,
                        REG_OPTION_VOLATILE, KEY_SET_VALUE | KEY_QUERY_VALUE,
                        NULL, &hKey, NULL) == ERROR_SUCCESS,
        "private utility test key created");
  if (!hKey)
    return;

  for (const std::wstring& path :
       {std::wstring(L"C:\\Rime"), std::wstring(200, L'路'),
        std::wstring(MAX_PATH - 1, L'界'), std::wstring()}) {
    check(RegSetValueExW(hKey, L"Path", 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(path.c_str()),
                         DWORD((path.size() + 1) * sizeof(WCHAR))) ==
              ERROR_SUCCESS,
          "private utility path value written");
    std::wstring out = L"unchanged";
    check(RegGetStringValue(HKEY_CURRENT_USER, key.c_str(), L"Path", out) ==
                  ERROR_SUCCESS &&
              out == path,
          "Unicode REG_SZ uses the full MAX_PATH character capacity");
  }

  const std::wstring unterminated(200, L'路');
  check(RegSetValueExW(hKey, L"NoNul", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(unterminated.data()),
                       DWORD(unterminated.size() * sizeof(WCHAR))) ==
            ERROR_SUCCESS,
        "private non-terminated value written");
  std::wstring out;
  check(RegGetStringValue(HKEY_CURRENT_USER, key.c_str(), L"NoNul", out) ==
                ERROR_SUCCESS &&
            out == unterminated,
        "RegGetValue supplies the missing REG_SZ terminator in bounds");

  out = L"unchanged";
  check(RegGetStringValue(HKEY_CURRENT_USER, key.c_str(), L"Missing", out) ==
                ERROR_FILE_NOT_FOUND &&
            out == L"unchanged",
        "missing utility value preserves the output and error code");
  DWORD number = 1;
  check(RegSetValueExW(hKey, L"Number", 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&number),
                       sizeof(number)) == ERROR_SUCCESS,
        "private non-string value written");
  check(RegGetStringValue(HKEY_CURRENT_USER, key.c_str(), L"Number", out) ==
                ERROR_UNSUPPORTED_TYPE &&
            out == L"unchanged",
        "utility rejects non-REG_SZ without changing the output");

  const std::wstring too_long(MAX_PATH + 40, L'路');
  check(RegSetValueExW(hKey, L"TooLong", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(too_long.c_str()),
                       DWORD((too_long.size() + 1) * sizeof(WCHAR))) ==
            ERROR_SUCCESS,
        "private over-capacity value written");
  check(RegGetStringValue(HKEY_CURRENT_USER, key.c_str(), L"TooLong", out) ==
                ERROR_MORE_DATA &&
            out == L"unchanged",
        "over-capacity utility value fails without returning truncated data");
  RegCloseKey(hKey);
  check(RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str()) == ERROR_SUCCESS,
        "private utility test key removed");
}

struct ProfileSwitchProbe {
  int fail_step = 0;
  HRESULT failure = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
  HRESULT rollback_failure = S_OK;
  bool initial_old_profile = true;
  bool initial_new_profile = false;
  bool initial_old_layout = true;
  bool initial_old_layout_default = true;
  bool initial_new_layout = false;
  bool initial_new_layout_default = false;
  bool old_profile = initial_old_profile;
  bool new_profile = initial_new_profile;
  bool old_layout = initial_old_layout;
  bool old_layout_default = initial_old_layout_default;
  bool new_layout = initial_new_layout;
  bool new_layout_default = initial_new_layout_default;
  std::vector<std::string> calls;

  ProfileSwitchProbe() = default;
  ProfileSwitchProbe(bool old_profile_enabled,
                     bool new_profile_enabled,
                     bool old_layout_enabled,
                     bool new_layout_enabled)
      : initial_old_profile(old_profile_enabled),
        initial_new_profile(new_profile_enabled),
        initial_old_layout(old_layout_enabled),
        initial_old_layout_default(old_layout_enabled),
        initial_new_layout(new_layout_enabled),
        initial_new_layout_default(false),
        old_profile(old_profile_enabled),
        new_profile(new_profile_enabled),
        old_layout(old_layout_enabled),
        old_layout_default(old_layout_enabled),
        new_layout(new_layout_enabled),
        new_layout_default(false) {}

  HRESULT forward(const char* name, int step, bool& state, bool desired) {
    calls.emplace_back(name);
    // Model a pessimistic API: the failed call may already have changed state.
    state = desired;
    return fail_step == step ? failure : S_OK;
  }
  HRESULT restore(const char* name, bool& state, bool initial) {
    calls.emplace_back(name);
    if (FAILED(rollback_failure))
      return rollback_failure;
    state = initial;
    return S_OK;
  }

  HRESULT enable_new() { return forward("enable_new", 1, new_profile, true); }
  HRESULT disable_old() {
    return forward("disable_old", 2, old_profile, false);
  }
  HRESULT remove_old_layout() {
    old_layout_default = false;
    return forward("remove_old_layout", 3, old_layout, false);
  }
  HRESULT install_new_layout() {
    return forward("install_new_layout", 4, new_layout, true);
  }
  HRESULT restore_new_profile() {
    return restore("restore_new_profile", new_profile, initial_new_profile);
  }
  HRESULT restore_old_profile() {
    return restore("restore_old_profile", old_profile, initial_old_profile);
  }
  HRESULT restore_old_layout() {
    const HRESULT hr =
        restore("restore_old_layout", old_layout, initial_old_layout);
    if (SUCCEEDED(hr))
      old_layout_default = initial_old_layout_default;
    return hr;
  }
  HRESULT restore_new_layout() {
    const HRESULT hr =
        restore("restore_new_layout", new_layout, initial_new_layout);
    if (SUCCEEDED(hr))
      new_layout_default = initial_new_layout_default;
    return hr;
  }
  bool failed(HRESULT hr) const { return FAILED(hr); }
  HRESULT success() const { return S_OK; }
  bool matches_initial_state() const {
    return old_profile == initial_old_profile &&
           new_profile == initial_new_profile &&
           old_layout == initial_old_layout &&
           old_layout_default == initial_old_layout_default &&
           new_layout == initial_new_layout &&
           new_layout_default == initial_new_layout_default;
  }
};

static void test_profile_switch_transaction() {
  check(weasel::setup::ResultFromBool(TRUE != FALSE, S_OK, E_FAIL) == S_OK &&
            FAILED(weasel::setup::ResultFromBool(FALSE != FALSE, S_OK, E_FAIL)),
        "BOOL API result maps FALSE to failed HRESULT and TRUE to S_OK");

  check(weasel::setup::GetLayoutRestoreAction({false, false}) ==
                weasel::setup::LayoutRestoreAction::Uninstall &&
            weasel::setup::GetLayoutRestoreAction({true, false}) ==
                weasel::setup::LayoutRestoreAction::Install &&
            weasel::setup::GetLayoutRestoreAction({true, true}) ==
                weasel::setup::LayoutRestoreAction::InstallDefault,
        "layout rollback preserves disabled, enabled, and default state");

  {
    ProfileSwitchProbe ops;
    check(SUCCEEDED(weasel::setup::RunProfileSwitchTransaction(ops)) &&
              !ops.old_profile && ops.new_profile && !ops.old_layout &&
              ops.new_layout &&
              ops.calls == std::vector<std::string>(
                               {"enable_new", "disable_old",
                                "remove_old_layout", "install_new_layout"}),
          "profile switch success reaches the requested final state");
  }

  const std::vector<std::vector<std::string>> expected_calls = {
      {},
      {"enable_new", "restore_new_profile"},
      {"enable_new", "disable_old", "restore_old_profile",
       "restore_new_profile"},
      {"enable_new", "disable_old", "remove_old_layout", "restore_old_layout",
       "restore_old_profile", "restore_new_profile"},
      {"enable_new", "disable_old", "remove_old_layout", "install_new_layout",
       "restore_new_layout", "restore_old_layout", "restore_old_profile",
       "restore_new_profile"},
  };
  for (int step = 1; step <= 4; ++step) {
    ProfileSwitchProbe ops;
    ops.fail_step = step;
    const HRESULT hr = weasel::setup::RunProfileSwitchTransaction(ops);
    check(hr == ops.failure && ops.matches_initial_state() &&
              ops.calls == expected_calls[step],
          "failed profile switch restores the exact pre-transaction state");
  }

  {
    // A user may already have enabled the destination profile/layout manually.
    // Rollback must preserve that pre-existing state instead of disabling it.
    ProfileSwitchProbe ops(true, true, true, true);
    ops.fail_step = 3;
    const HRESULT hr = weasel::setup::RunProfileSwitchTransaction(ops);
    check(hr == ops.failure && ops.matches_initial_state(),
          "rollback preserves pre-existing destination profile and layout");
  }
  {
    ProfileSwitchProbe ops;
    ops.fail_step = 4;
    ops.rollback_failure = E_OUTOFMEMORY;
    const HRESULT hr = weasel::setup::RunProfileSwitchTransaction(ops);
    check(hr == ops.failure,
          "rollback failures do not overwrite the original HRESULT");
  }
}


struct TsfUninstallProbe {
  int fail_step = 0;
  std::vector<std::string> calls;

  int remove_layouts() {
    calls.emplace_back("remove_layouts");
    return fail_step == 1 ? 1 : 0;
  }
  int unregister_services() {
    calls.emplace_back("unregister_services");
    return fail_step == 2 ? 1 : 0;
  }
  void restore_services() { calls.emplace_back("restore_services"); }
  void restore_default_profiles() {
    calls.emplace_back("restore_default_profiles");
  }
  void restore_profiles() { calls.emplace_back("restore_profiles"); }
  void restore_layouts() { calls.emplace_back("restore_layouts"); }
  bool failed(int result) const { return result != 0; }
  int success() const { return 0; }
};

static void test_tsf_uninstall_transaction() {
  {
    TsfUninstallProbe ops;
    check(weasel::setup::RunTsfUninstallTransaction(ops) == 0 &&
              ops.calls ==
                  std::vector<std::string>({"remove_layouts",
                                            "unregister_services"}),
          "TSF uninstall commits only after layout and registration cleanup");
  }
  {
    TsfUninstallProbe ops;
    ops.fail_step = 1;
    check(weasel::setup::RunTsfUninstallTransaction(ops) != 0 &&
              ops.calls ==
                  std::vector<std::string>({"remove_layouts",
                                            "restore_layouts"}),
          "layout failure restores user layout without touching registration");
  }
  {
    TsfUninstallProbe ops;
    ops.fail_step = 2;
    check(weasel::setup::RunTsfUninstallTransaction(ops) != 0 &&
              ops.calls ==
                  std::vector<std::string>(
                      {"remove_layouts", "unregister_services",
                       "restore_services", "restore_default_profiles",
                       "restore_profiles", "restore_layouts"}),
          "regsvr32 failure rebuilds registration before user-state rollback");
  }
}

int main() {
  test_unquote_argument();
  test_read_reg_sz();
  test_delete_reg_tree();
  test_reg_get_string_value();
  test_profile_switch_transaction();
  test_tsf_uninstall_transaction();

  if (g_failures) {
    printf("%d failure(s)\n", g_failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
