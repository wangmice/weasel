#pragma once

#include <Windows.h>

#include <string>

#include "PerUserReg.h"

namespace weasel::setup {
namespace detail {

inline bool Is64BitWindowsForRegistrationCleanup() noexcept {
#if defined(_WIN64)
  return true;
#else
  BOOL wow64 = FALSE;
  return IsWow64Process(GetCurrentProcess(), &wow64) && wow64 != FALSE;
#endif
}

inline bool DeleteRegistryTreeInView(HKEY root,
                                     const std::wstring& path,
                                     REGSAM view) noexcept {
  const size_t split = path.find_last_of(L'\\');
  if (split == std::wstring::npos || split + 1 >= path.size())
    return false;

  const std::wstring parent_path = path.substr(0, split);
  const std::wstring leaf = path.substr(split + 1);
  HKEY parent = nullptr;
  const LSTATUS opened = RegOpenKeyExW(
      root, parent_path.c_str(), 0,
      DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE | view,
      &parent);
  if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND)
    return true;
  if (opened != ERROR_SUCCESS)
    return false;

  const LSTATUS deleted = RegDeleteTreeW(parent, leaf.c_str());
  RegCloseKey(parent);
  return deleted == ERROR_SUCCESS || deleted == ERROR_FILE_NOT_FOUND ||
         deleted == ERROR_PATH_NOT_FOUND;
}

inline bool DeleteTextServiceRegistrationInView(
    const std::wstring& clsid,
    HKEY user_root,
    const std::wstring& user_classes_root,
    REGSAM view) noexcept {
  const std::wstring machine_com = L"Software\\Classes\\CLSID\\" + clsid;
  const std::wstring machine_tip =
      L"Software\\Microsoft\\CTF\\TIP\\" + clsid;
  const std::wstring machine_legacy_tip =
      L"Software\\Classes\\Software\\Microsft\\CTF\\TIP\\" + clsid;
  const std::wstring user_com = user_classes_root + L"\\CLSID\\" + clsid;
  const std::wstring user_legacy_tip =
      user_classes_root + L"\\Software\\Microsft\\CTF\\TIP\\" + clsid;

  bool ok = true;
  // DllUnregisterServer historically used HKCR. HKCR is a merged view, and
  // writes can reside in either machine or per-user Classes. Remove only this
  // product's exact CLSID/legacy TIP trees from both stores.
  ok = DeleteRegistryTreeInView(HKEY_LOCAL_MACHINE, machine_com, view) && ok;
  ok = DeleteRegistryTreeInView(HKEY_LOCAL_MACHINE, machine_tip, view) && ok;
  ok = DeleteRegistryTreeInView(HKEY_LOCAL_MACHINE, machine_legacy_tip, view) &&
       ok;
  ok = DeleteRegistryTreeInView(user_root, user_com, view) && ok;
  ok = DeleteRegistryTreeInView(user_root, user_legacy_tip, view) && ok;
  return ok;
}

}  // namespace detail

inline bool CleanupTextServiceRegistrationAfterUninstall() noexcept {
  static const GUID kTextServiceClsid = {
      0xa3f4cded,
      0xb1e9,
      0x41ee,
      {0x9c, 0xa6, 0x7b, 0x4d, 0x0d, 0xe6, 0xcb, 0x0a}};

  wchar_t clsid[64] = {};
  if (StringFromGUID2(kTextServiceClsid, clsid, _countof(clsid)) <= 0)
    return false;

  // PerUserReg keeps this pointed at the invoking user's hive even when UAC
  // elevation used credentials from a different administrator account.
  HKEY user_root = per_user_root();
  const std::wstring user_classes_root =
      per_user_subkey(L"Software\\Classes");

  if (!detail::Is64BitWindowsForRegistrationCleanup()) {
    return detail::DeleteTextServiceRegistrationInView(
        clsid, user_root, user_classes_root, 0);
  }

  bool ok = detail::DeleteTextServiceRegistrationInView(
      clsid, user_root, user_classes_root, KEY_WOW64_64KEY);
  ok = detail::DeleteTextServiceRegistrationInView(
           clsid, user_root, user_classes_root, KEY_WOW64_32KEY) &&
       ok;
  return ok;
}

}  // namespace weasel::setup
