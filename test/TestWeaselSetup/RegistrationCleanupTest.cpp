#include <Windows.h>
#include <stdio.h>

#include <string>

#include "../../WeaselSetup/RegistrationCleanup.h"

namespace {

[[noreturn]] void registration_cleanup_test_fail(const char* message) {
  fprintf(stderr, "[FAIL] registration cleanup: %s\n", message);
  ExitProcess(1);
}

struct RegistrationCleanupTestRunner {
  RegistrationCleanupTestRunner() {
    const std::wstring base =
        L"Software\\Rime\\WeaselRegistrationCleanupTest-" +
        std::to_wstring(GetCurrentProcessId());
    const std::wstring target = base + L"\\Target";
    const std::wstring nested = target + L"\\Nested";
    const std::wstring sibling = base + L"\\Sibling";

    RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, nested.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
      registration_cleanup_test_fail("failed to create nested scratch key");
    }
    DWORD value = 1;
    RegSetValueExW(key, L"Value", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);

    if (RegCreateKeyExW(HKEY_CURRENT_USER, sibling.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
      RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
      registration_cleanup_test_fail("failed to create sibling scratch key");
    }
    RegCloseKey(key);

    if (!weasel::setup::detail::DeleteRegistryTreeInView(
            HKEY_CURRENT_USER, target, 0)) {
      RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
      registration_cleanup_test_fail("helper reported deletion failure");
    }

    HKEY check = nullptr;
    const LSTATUS target_open =
        RegOpenKeyExW(HKEY_CURRENT_USER, target.c_str(), 0, KEY_READ, &check);
    if (target_open != ERROR_FILE_NOT_FOUND &&
        target_open != ERROR_PATH_NOT_FOUND) {
      if (check)
        RegCloseKey(check);
      RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
      registration_cleanup_test_fail("target tree survived recursive deletion");
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, sibling.c_str(), 0, KEY_READ, &check) !=
        ERROR_SUCCESS) {
      RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
      registration_cleanup_test_fail("sibling key was removed accidentally");
    }
    RegCloseKey(check);

    if (!weasel::setup::detail::DeleteRegistryTreeInView(
            HKEY_CURRENT_USER, target, 0)) {
      RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
      registration_cleanup_test_fail("missing target was not idempotent");
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
    printf("[ok] registration cleanup removes only the requested nested tree\n");
  }
};

RegistrationCleanupTestRunner g_registration_cleanup_test_runner;

}  // namespace
