#pragma once

#include <msctf.h>
#include <strsafe.h>
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include <WeaselConstants.h>
#include <WeaselUtility.h>
#include "PerUserReg.h"
#include "ProfileSwitchTransaction.h"
#include "resource.h"

namespace weasel::setup {

using TransactionalImeRegisterFunc = int (*)(const std::wstring& ime_path,
                                             bool register_ime,
                                             bool is_wow64,
                                             bool is_wowarm,
                                             const std::wstring& profile,
                                             bool silent);

}  // namespace weasel::setup

// imesetup.cpp keeps these legacy entry points in the global namespace.
int install_ime_file(std::wstring& srcPath,
                     const std::wstring& ext,
                     const std::wstring& profile,
                     bool silent,
                     weasel::setup::TransactionalImeRegisterFunc func);
int uninstall_ime_file(const std::wstring& ext,
                       const std::wstring& profile,
                       bool silent,
                       weasel::setup::TransactionalImeRegisterFunc func);
int register_text_service(const std::wstring& tsf_path,
                          bool register_ime,
                          bool is_wow64,
                          bool is_wowarm32,
                          const std::wstring& profile,
                          bool silent);
bool has_installed();

namespace weasel::setup {
namespace detail {

static const GUID kTextServiceClsid = {
    0xa3f4cded,
    0xb1e9,
    0x41ee,
    {0x9c, 0xa6, 0x7b, 0x4d, 0xd, 0xe6, 0xcb, 0xa}};
static const GUID kProfileGuid = {
    0x3d02cab6,
    0x2b8e,
    0x4781,
    {0xba, 0x20, 0x1c, 0x92, 0x67, 0x52, 0x94, 0x67}};

constexpr DWORD kIlotUninstall = 0x00000001;
constexpr DWORD kIlotDefaultProfile = 0x00000002;
constexpr DWORD kLayoutOrTipInputProcessor = 1;
constexpr DWORD kLayoutOrTipDefault = 0x0001;
constexpr DWORD kLayoutOrTipDisabled = 0x0002;

using InstallLayoutOrTipFn = BOOL(WINAPI*)(LPCWSTR, DWORD);

struct LayoutOrTipProfile {
  DWORD dwProfileType;
  LANGID langid;
  CLSID clsid;
  GUID guidProfile;
  GUID catid;
  DWORD dwSubstituteLayout;
  DWORD dwFlags;
  WCHAR szId[MAX_PATH];
};
using EnumEnabledLayoutOrTipFn =
    UINT(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LayoutOrTipProfile*, UINT);

inline LANGID ProfileLangId(const std::wstring& profile) {
  if (profile == L"hant")
    return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);
  if (profile == L"hongkong")
    return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG);
  if (profile == L"macau")
    return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_MACAU);
  if (profile == L"singapore")
    return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SINGAPORE);
  return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
}

inline std::wstring ProfileTitle(const std::wstring& profile) {
  WCHAR clsid[64] = {};
  WCHAR profile_guid[64] = {};
  WCHAR langid[5] = {};
  if (StringFromGUID2(kTextServiceClsid, clsid, _countof(clsid)) <= 0 ||
      StringFromGUID2(kProfileGuid, profile_guid, _countof(profile_guid)) <=
          0 ||
      FAILED(StringCchPrintfW(langid, _countof(langid), L"%04X",
                              ProfileLangId(profile)))) {
    return {};
  }
  return std::wstring(langid) + L":" + clsid + profile_guid;
}

inline HRESULT ChangeLayout(const std::wstring& profile,
                            LayoutRestoreAction action) {
  HMODULE input = LoadLibraryW(L"input.dll");
  if (!input) {
    const DWORD error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : ERROR_MOD_NOT_FOUND);
  }
  auto fn = reinterpret_cast<InstallLayoutOrTipFn>(
      GetProcAddress(input, "InstallLayoutOrTip"));
  if (!fn) {
    const DWORD error = GetLastError();
    FreeLibrary(input);
    return HRESULT_FROM_WIN32(error ? error : ERROR_PROC_NOT_FOUND);
  }
  const std::wstring title = ProfileTitle(profile);
  if (title.empty()) {
    FreeLibrary(input);
    return E_INVALIDARG;
  }
  DWORD flags = 0;
  if (action == LayoutRestoreAction::Uninstall)
    flags = kIlotUninstall;
  else if (action == LayoutRestoreAction::InstallDefault)
    flags = kIlotDefaultProfile;
  const BOOL ok = fn(title.c_str(), flags);
  FreeLibrary(input);
  return ResultFromBool(ok != FALSE, S_OK, E_FAIL);
}

inline HRESULT QueryLayoutState(const std::wstring& profile,
                                LayoutState& state) {
  state = {};
  HMODULE input = LoadLibraryW(L"input.dll");
  if (!input) {
    const DWORD error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : ERROR_MOD_NOT_FOUND);
  }
  auto fn = reinterpret_cast<EnumEnabledLayoutOrTipFn>(
      GetProcAddress(input, "EnumEnabledLayoutOrTip"));
  if (!fn) {
    const DWORD error = GetLastError();
    FreeLibrary(input);
    return HRESULT_FROM_WIN32(error ? error : ERROR_PROC_NOT_FOUND);
  }
  const UINT count = fn(nullptr, nullptr, nullptr, nullptr, 0);
  if (count == 0) {
    FreeLibrary(input);
    return S_OK;
  }
  std::vector<LayoutOrTipProfile> items(count);
  const UINT copied = fn(nullptr, nullptr, nullptr, items.data(), count);
  FreeLibrary(input);
  if (copied == 0)
    return E_FAIL;

  const LANGID langid = ProfileLangId(profile);
  const UINT limit = (std::min)(count, copied);
  for (UINT i = 0; i < limit; ++i) {
    const auto& item = items[i];
    if ((item.dwProfileType & kLayoutOrTipInputProcessor) ==
            kLayoutOrTipInputProcessor &&
        item.langid == langid && IsEqualGUID(item.clsid, kTextServiceClsid) &&
        IsEqualGUID(item.guidProfile, kProfileGuid)) {
      state.enabled = (item.dwFlags & kLayoutOrTipDisabled) == 0;
      state.is_default = (item.dwFlags & kLayoutOrTipDefault) != 0;
      break;
    }
  }
  return S_OK;
}

struct RegistryValueSnapshot {
  HKEY root = nullptr;
  std::wstring key;
  std::wstring name;
  bool existed = false;
  DWORD type = REG_NONE;
  std::vector<BYTE> data;

  bool Capture() {
    DWORD bytes = 0;
    LSTATUS status = RegGetValueW(root, key.c_str(), name.c_str(), RRF_RT_ANY,
                                  &type, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
      existed = false;
      type = REG_NONE;
      data.clear();
      return true;
    }
    if (status != ERROR_SUCCESS)
      return false;
    existed = true;
    data.resize(bytes);
    if (bytes == 0)
      return true;
    status = RegGetValueW(root, key.c_str(), name.c_str(), RRF_RT_ANY, &type,
                          data.data(), &bytes);
    if (status != ERROR_SUCCESS)
      return false;
    data.resize(bytes);
    return true;
  }

  void Restore() const {
    HKEY handle = nullptr;
    if (!existed) {
      if (RegOpenKeyExW(root, key.c_str(), 0, KEY_SET_VALUE, &handle) ==
          ERROR_SUCCESS) {
        RegDeleteValueW(handle, name.c_str());
        RegCloseKey(handle);
      }
      return;
    }
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &handle,
                        nullptr) != ERROR_SUCCESS) {
      return;
    }
    RegSetValueExW(handle, name.c_str(), 0, type,
                   data.empty() ? nullptr : data.data(),
                   static_cast<DWORD>(data.size()));
    RegCloseKey(handle);
  }
};

inline void RestoreLayout(const std::wstring& profile,
                          const LayoutState& state) {
  ChangeLayout(profile, GetLayoutRestoreAction(state));
}

}  // namespace detail

inline int InstallFreshTransactional(const std::wstring& profile,
                                     const std::wstring& user_dir,
                                     bool silent) {
  // This wrapper is for a genuinely fresh system registration. Existing
  // installs keep using the profile-switch transaction instead of replacing
  // their registered service in place.
  if (has_installed())
    return 1;

  const WCHAR profile_key[] = L"Software\\Rime\\Weasel";
  const std::wstring user_key = per_user_subkey(profile_key);
  std::vector<detail::RegistryValueSnapshot> registry = {
      {HKEY_LOCAL_MACHINE, WEASEL_REG_KEY, L"WeaselRoot"},
      {HKEY_LOCAL_MACHINE, WEASEL_REG_KEY, L"ServerExecutable"},
      {per_user_root(), user_key, L"Profile"},
      {per_user_root(), user_key, L"Hant"},
      {per_user_root(), user_key, L"RimeUserDir"},
  };
  for (auto& snapshot : registry) {
    if (!snapshot.Capture())
      return 1;
  }

  LayoutState old_layout;
  if (FAILED(detail::QueryLayoutState(profile, old_layout)))
    return 1;

  bool service_touched = false;
  auto rollback = [&]() {
    // install_ime_file can fail after registering one architecture; always run
    // the matching uninstaller once it has been attempted.
    if (service_touched) {
      uninstall_ime_file(L".dll", profile, true, &register_text_service);
    }
    for (auto it = registry.rbegin(); it != registry.rend(); ++it)
      it->Restore();
    detail::RestoreLayout(profile, old_layout);
  };

  std::wstring ime_src_path;
  service_touched = true;
  if (install_ime_file(ime_src_path, L".dll", profile, silent,
                       &register_text_service) != 0) {
    rollback();
    return 1;
  }

  WCHAR drive[_MAX_DRIVE] = {};
  WCHAR dir[_MAX_DIR] = {};
  _wsplitpath_s(ime_src_path.c_str(), drive, _countof(drive), dir,
                _countof(dir), nullptr, 0, nullptr, 0);
  std::wstring root_dir = std::wstring(drive) + dir;
  if (root_dir.empty()) {
    rollback();
    return 1;
  }
  if (root_dir.back() == L'\\' || root_dir.back() == L'/')
    root_dir.pop_back();

  auto write_failed = [&](LSTATUS status) {
    return FAILED(HRESULT_FROM_WIN32(status));
  };
  bool registry_failed =
      write_failed(SetRegKeyValue(HKEY_LOCAL_MACHINE, WEASEL_REG_KEY,
                                  L"WeaselRoot", root_dir.c_str(), REG_SZ)) ||
      write_failed(SetRegKeyValue(HKEY_LOCAL_MACHINE, WEASEL_REG_KEY,
                                  L"ServerExecutable", L"WeaselServer.exe",
                                  REG_SZ)) ||
      write_failed(SetRegKeyValue(per_user_root(), user_key.c_str(), L"Profile",
                                  profile.c_str(), REG_SZ)) ||
      write_failed(SetRegKeyValue(per_user_root(), user_key.c_str(), L"Hant",
                                  profile == L"hant" ? 1 : 0, REG_DWORD));
  if (!registry_failed && !user_dir.empty()) {
    registry_failed = write_failed(
        SetRegKeyValue(per_user_root(), user_key.c_str(), L"RimeUserDir",
                       user_dir.c_str(), REG_SZ, false));
  }
  if (registry_failed) {
    rollback();
    return 1;
  }

  if (FAILED(detail::ChangeLayout(profile, LayoutRestoreAction::Install))) {
    rollback();
    return 1;
  }

  // WER diagnostics are optional and intentionally outside the transaction.
  static constexpr wchar_t kWerKey[] =
      L"SOFTWARE\\Microsoft\\Windows\\Windows Error "
      L"Reporting\\LocalDumps\\WeaselServer.exe";
  const std::wstring dump_path = WeaselLogPath().wstring();
  SetRegKeyValue(HKEY_LOCAL_MACHINE, kWerKey, L"DumpFolder", dump_path.c_str(),
                 REG_SZ, true);
  SetRegKeyValue(HKEY_LOCAL_MACHINE, kWerKey, L"DumpType", 1, REG_DWORD, true);
  SetRegKeyValue(HKEY_LOCAL_MACHINE, kWerKey, L"DumpCount", 10, REG_DWORD,
                 true);

  MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_INSTALL_SUCCESS_INFO,
                        IDS_STR_INSTALL_SUCCESS_CAP,
                        MB_ICONINFORMATION | MB_OK);
  return 0;
}

}  // namespace weasel::setup
