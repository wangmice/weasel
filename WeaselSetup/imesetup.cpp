#include "stdafx.h"
#include <string>
#include <vector>
#include <msctf.h>
#include <strsafe.h>
#include <StringAlgorithm.hpp>
#include <WeaselConstants.h>
#include <WeaselUtility.h>
#include "InstallOptionsDlg.h"
#include "PerUserReg.h"
#include "SetupUtil.h"

// {A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}
static const GUID c_clsidTextService = {
    0xa3f4cded,
    0xb1e9,
    0x41ee,
    {0x9c, 0xa6, 0x7b, 0x4d, 0xd, 0xe6, 0xcb, 0xa}};

// {3D02CAB6-2B8E-4781-BA20-1C9267529467}
static const GUID c_guidProfile = {
    0x3d02cab6,
    0x2b8e,
    0x4781,
    {0xba, 0x20, 0x1c, 0x92, 0x67, 0x52, 0x94, 0x67}};

#define ILOT_UNINSTALL 0x00000001
typedef HRESULT(WINAPI* PTF_INSTALLLAYOUTORTIP)(LPCWSTR psz, DWORD dwFlags);

#define WEASEL_WER_KEY                            \
  L"SOFTWARE\\Microsoft\\Windows\\Windows Error " \
  L"Reporting\\LocalDumps\\WeaselServer.exe"

BOOL copy_file(const std::wstring& src, const std::wstring& dest) {
  BOOL ret = CopyFile(src.c_str(), dest.c_str(), FALSE);
  if (!ret) {
    for (int i = 0; i < 10; ++i) {
      std::wstring old = dest + L".old." + std::to_wstring(i);
      if (MoveFileEx(dest.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileEx(old.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        break;
      }
    }
    ret = CopyFile(src.c_str(), dest.c_str(), FALSE);
  }
  return ret;
}

BOOL delete_file(const std::wstring& file) {
  BOOL ret = DeleteFile(file.c_str());
  if (!ret) {
    for (int i = 0; i < 10; ++i) {
      std::wstring old = file + L".old." + std::to_wstring(i);
      if (MoveFileEx(file.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileEx(old.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        return TRUE;
      }
    }
  }
  return ret;
}

typedef BOOL(WINAPI* PISWOW64P2)(HANDLE, USHORT*, USHORT*);
BOOL is_arm64_machine() {
  PISWOW64P2 fnIsWow64Process2 = (PISWOW64P2)GetProcAddress(
      GetModuleHandle(_T("kernel32.dll")), "IsWow64Process2");

  if (fnIsWow64Process2 == NULL) {
    return FALSE;
  }

  USHORT processMachine;
  USHORT nativeMachine;

  if (!fnIsWow64Process2(GetCurrentProcess(), &processMachine,
                         &nativeMachine)) {
    return FALSE;
  }
  return nativeMachine == IMAGE_FILE_MACHINE_ARM64;
}

typedef HRESULT(WINAPI* PISWOWGMS)(USHORT, BOOL*);
typedef UINT(WINAPI* PGSW64DIR2)(LPWSTR, UINT, WORD);
INT get_wow_arm32_system_dir(LPWSTR lpBuffer, UINT uSize) {
  PISWOWGMS fnIsWow64GuestMachineSupported = (PISWOWGMS)GetProcAddress(
      GetModuleHandle(_T("kernel32.dll")), "IsWow64GuestMachineSupported");
  PGSW64DIR2 fnGetSystemWow64Directory2W = (PGSW64DIR2)GetProcAddress(
      GetModuleHandle(_T("kernelbase.dll")), "GetSystemWow64Directory2W");

  if (fnIsWow64GuestMachineSupported == NULL ||
      fnGetSystemWow64Directory2W == NULL) {
    return 0;
  }

  BOOL supported;
  if (fnIsWow64GuestMachineSupported(IMAGE_FILE_MACHINE_ARMNT, &supported) !=
      S_OK) {
    return 0;
  }

  if (!supported) {
    return 0;
  }

  return fnGetSystemWow64Directory2W(lpBuffer, uSize, IMAGE_FILE_MACHINE_ARMNT);
}

typedef int (*ime_register_func)(const std::wstring& ime_path,
                                 bool register_ime,
                                 bool is_wow64,
                                 bool is_wowarm,
                                 const std::wstring& profile,
                                 bool silent);

static LANGID profile_to_lang_id(const std::wstring& profile) {
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

static std::wstring profile_to_title(const std::wstring& profile) {
  WCHAR clsidTextService[64] = {0};
  WCHAR profileGuid[64] = {0};
  WCHAR langidText[5] = {0};

  if (StringFromGUID2(c_clsidTextService, clsidTextService,
                      _countof(clsidTextService)) <= 0 ||
      StringFromGUID2(c_guidProfile, profileGuid, _countof(profileGuid)) <= 0 ||
      FAILED(StringCchPrintfW(langidText, _countof(langidText), L"%04X",
                              profile_to_lang_id(profile)))) {
    return L"";
  }

  return std::wstring(langidText) + L":" + clsidTextService + profileGuid;
}

// WOW64 文件系统重定向守卫：Disable 成功后，无论以何种路径离开作用域
// （含提前 return）都恢复重定向
class Wow64FsRedirectionGuard {
 public:
  bool Disable() {
    disabled_ = Wow64DisableWow64FsRedirection(&old_value_) != FALSE;
    return disabled_;
  }

  // 显式恢复；从未禁用或已恢复时为空操作，可重复调用
  bool Restore() {
    if (!disabled_)
      return true;
    disabled_ = false;
    return Wow64RevertWow64FsRedirection(old_value_) != FALSE;
  }

  ~Wow64FsRedirectionGuard() { Restore(); }

 private:
  PVOID old_value_ = NULL;
  bool disabled_ = false;
};

int install_ime_file(std::wstring& srcPath,
                     const std::wstring& ext,
                     const std::wstring& profile,
                     bool silent,
                     ime_register_func func) {
  WCHAR path[MAX_PATH];
  GetModuleFileNameW(GetModuleHandle(NULL), path, _countof(path));

  std::wstring srcFileName = L"weasel";

  srcFileName += ext;
  WCHAR drive[_MAX_DRIVE];
  WCHAR dir[_MAX_DIR];
  _wsplitpath_s(path, drive, _countof(drive), dir, _countof(dir), NULL, 0, NULL,
                0);
  srcPath = std::wstring(drive) + dir + srcFileName;

  GetSystemDirectoryW(path, _countof(path));
  std::wstring destPath = std::wstring(path) + L"\\weasel" + ext;

  int retval = 0;
  // 复制 .dll/.ime 到系统目录
  if (!copy_file(srcPath, destPath)) {
    MSG_NOT_SILENT_ID_CAP(silent, destPath.c_str(), IDS_STR_INSTALL_FAILED,
                          MB_ICONERROR | MB_OK);
    return 1;
  }
  retval += func(destPath, true, false, false, profile, silent);
  if (is_wow64()) {
    Wow64FsRedirectionGuard redirect;
    if (!redirect.Disable()) {
      MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERRCANCELFSREDIRECT,
                            IDS_STR_INSTALL_FAILED, MB_ICONERROR | MB_OK);
      return 1;
    }

    if (is_arm64_machine()) {
      WCHAR sysarm32[MAX_PATH];
      if (get_wow_arm32_system_dir(sysarm32, _countof(sysarm32)) > 0) {
        // Install the ARM32 version if ARM32 WOW is supported （lower than
        // Windows 11 24H2).
        std::wstring srcPathARM32 = srcPath;
        ireplace_last(srcPathARM32, ext, L"ARM" + ext);

        std::wstring destPathARM32 = std::wstring(sysarm32) + L"\\weasel" + ext;
        if (!copy_file(srcPathARM32, destPathARM32)) {
          MSG_NOT_SILENT_ID_CAP(silent, destPathARM32.c_str(),
                                IDS_STR_INSTALL_FAILED, MB_ICONERROR | MB_OK);
          return 1;
        }
        retval += func(destPathARM32, true, true, true, profile, silent);
      }

      // Then install the ARM64 (and x64) version.
      // On ARM64 weasel.dll(ime) is an ARM64X redirection DLL (weaselARM64X).
      // When loaded, it will be redirected to weaselARM64.dll(ime) on ARM64
      // processes, and weaselx64.dll(ime) on x64 processes. So we need a total
      // of three files.

      std::wstring srcPathX64 = srcPath;
      std::wstring destPathX64 = destPath;
      ireplace_last(srcPathX64, ext, L"x64" + ext);
      ireplace_last(destPathX64, ext, L"x64" + ext);
      if (!copy_file(srcPathX64, destPathX64)) {
        MSG_NOT_SILENT_ID_CAP(silent, destPathX64.c_str(),
                              IDS_STR_INSTALL_FAILED, MB_ICONERROR | MB_OK);
        return 1;
      }

      std::wstring srcPathARM64 = srcPath;
      std::wstring destPathARM64 = destPath;
      ireplace_last(srcPathARM64, ext, L"ARM64" + ext);
      ireplace_last(destPathARM64, ext, L"ARM64" + ext);
      if (!copy_file(srcPathARM64, destPathARM64)) {
        MSG_NOT_SILENT_ID_CAP(silent, destPathARM64.c_str(),
                              IDS_STR_INSTALL_FAILED, MB_ICONERROR | MB_OK);
        return 1;
      }

      // Since weaselARM64X is just a redirector we don't have separate
      // profile variants.
      srcPath = std::wstring(drive) + dir + L"weaselARM64X" + ext;
    } else {
      ireplace_last(srcPath, ext, L"x64" + ext);
    }

    if (!copy_file(srcPath, destPath)) {
      MSG_NOT_SILENT_ID_CAP(silent, destPath.c_str(), IDS_STR_INSTALL_FAILED,
                            MB_ICONERROR | MB_OK);
      return 1;
    }
    retval += func(destPath, true, true, false, profile, silent);
    if (!redirect.Restore()) {
      MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERRRECOVERFSREDIRECT,
                            IDS_STR_INSTALL_FAILED, MB_ICONERROR | MB_OK);
      return 1;
    }
  }
  return retval;
}

int uninstall_ime_file(const std::wstring& ext,
                       const std::wstring& profile,
                       bool silent,
                       ime_register_func func) {
  int retval = 0;
  WCHAR path[MAX_PATH];
  GetSystemDirectoryW(path, _countof(path));
  std::wstring imePath(path);
  imePath += L"\\weasel" + ext;
  retval += func(imePath, false, false, false, profile, silent);
  delete_file(imePath);
  if (is_wow64()) {
    retval += func(imePath, false, true, false, profile, silent);
    Wow64FsRedirectionGuard redirect;
    if (!redirect.Disable()) {
      MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERRCANCELFSREDIRECT,
                            IDS_STR_UNINSTALL_FAILED, MB_ICONERROR | MB_OK);
      return 1;
    }

    if (is_arm64_machine()) {
      WCHAR sysarm32[MAX_PATH];
      if (get_wow_arm32_system_dir(sysarm32, _countof(sysarm32)) > 0) {
        std::wstring imePathARM32 = std::wstring(sysarm32) + L"\\weasel" + ext;
        retval += func(imePathARM32, false, true, true, profile, silent);
        delete_file(imePathARM32);
      }

      std::wstring imePathX64 = imePath;
      ireplace_last(imePathX64, ext, L"x64" + ext);
      delete_file(imePathX64);

      std::wstring imePathARM64 = imePath;
      ireplace_last(imePathARM64, ext, L"ARM64" + ext);
      delete_file(imePathARM64);
    }

    delete_file(imePath);
    if (!redirect.Restore()) {
      MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERRRECOVERFSREDIRECT,
                            IDS_STR_UNINSTALL_FAILED, MB_ICONERROR | MB_OK);
      return 1;
    }
  }
  return retval;
}

// 注册IME输入法
// `register_ime` (IMM/.ime) support removed — TSF-only build

// 启用/停用一个 profile 的 TSF 启用状态（注册条目保留，用于已安装系统
// 切换 profile；停用≠移除，区别于卸载路径的 RemoveLanguageProfile）
static BOOL set_profile_enabled(const std::wstring& profile, BOOL fEnable) {
  ITfInputProcessorProfiles* pProfiles = NULL;
  HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL,
                                CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                                (LPVOID*)&pProfiles);
  if (FAILED(hr))
    return FALSE;

  LANGID lang_id = profile_to_lang_id(profile);
  hr = pProfiles->EnableLanguageProfile(c_clsidTextService, lang_id,
                                        c_guidProfile, fEnable);
  if (fEnable) {
    pProfiles->EnableLanguageProfileByDefault(c_clsidTextService, lang_id,
                                              c_guidProfile, fEnable);
  }
  pProfiles->Release();
  return SUCCEEDED(hr) ? TRUE : FALSE;
}

// 注册/注销路径的 profile 处理：TRUE=启用（同 set_profile_enabled）；
// FALSE=彻底移除注册条目
void enable_profile(BOOL fEnable, const std::wstring& profile) {
  if (fEnable) {
    set_profile_enabled(profile, TRUE);
    return;
  }

  ITfInputProcessorProfiles* pProfiles = NULL;
  HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL,
                                CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                                (LPVOID*)&pProfiles);
  if (SUCCEEDED(hr)) {
    pProfiles->RemoveLanguageProfile(c_clsidTextService,
                                     profile_to_lang_id(profile),
                                     c_guidProfile);
    pProfiles->Release();
  }
}

// InstallLayoutOrTip
// https://learn.microsoft.com/zh-cn/windows/win32/tsf/installlayoutortip
// example in ref page not right with "*PTF_ INSTALLLAYOUTORTIP"
// space inside should be removed
enum class LayoutOrTipAction { Install, Uninstall };

// 把 profile 对应的输入布局加入/移出当前用户的输入法列表
static void install_layout_or_tip(const std::wstring& profile,
                                  LayoutOrTipAction action) {
  HMODULE hInputDLL = LoadLibrary(TEXT("input.dll"));
  if (!hInputDLL)
    return;
  PTF_INSTALLLAYOUTORTIP pfnInstallLayoutOrTip =
      (PTF_INSTALLLAYOUTORTIP)GetProcAddress(hInputDLL, "InstallLayoutOrTip");
  if (pfnInstallLayoutOrTip) {
    std::wstring title = profile_to_title(profile);
    if (!title.empty())
      (*pfnInstallLayoutOrTip)(
          title.c_str(),
          action == LayoutOrTipAction::Uninstall ? ILOT_UNINSTALL : 0);
  }
  FreeLibrary(hInputDLL);
}

// 已安装状态下切换 profile（K14）。regsvr32 的 RegisterProfiles 在安装时
// 已注册全部五个 langid 的 profile 条目（仅选定的一个启用），因此切换只需
// 翻转启用状态并同步用户的输入法列表，无需重跑 regsvr32。
// 先启用新 profile，失败时旧 profile 原样保留，系统保持一致可用。
int switch_registered_profile(const std::wstring& old_profile,
                              const std::wstring& new_profile,
                              bool silent) {
  const BOOL switched =
      set_profile_enabled(new_profile, TRUE) &&
      set_profile_enabled(old_profile, FALSE);
  if (!switched) {
    MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERR_SWITCH_PROFILE,
                          IDS_STR_INSTALL_FAILED, MB_ICONERROR | MB_OK);
    return 1;
  }
  install_layout_or_tip(old_profile, LayoutOrTipAction::Uninstall);
  install_layout_or_tip(new_profile, LayoutOrTipAction::Install);
  return 0;
}

// 报告 regsvr32 失败（启动失败或退出码非 0）并返回错误码
static int report_regsvr32_failure(const std::wstring& params, bool silent) {
  WCHAR msg[100];
  CString str;
  str.LoadStringW(IDS_STR_ERRREGTSF);
  StringCchPrintfW(msg, _countof(msg), str, params.c_str());
  MSG_NOT_SILENT_ID_CAP(silent, msg, IDS_STR_INORUN_FAILED,
                        MB_ICONERROR | MB_OK);
  return 1;
}

// 注册TSF输入法
int register_text_service(const std::wstring& tsf_path,
                          bool register_ime,
                          bool is_wow64,
                          bool is_wowarm32,
                          const std::wstring& profile,
                          bool silent) {
  using RegisterServerFunction = HRESULT(STDAPICALLTYPE*)();

  if (!register_ime)
    enable_profile(FALSE, profile);

  std::wstring params = L" \"" + tsf_path + L"\"";
  if (!register_ime) {
    params = L" /u " + params;  // unregister
  }
  // if (silent)  // always silent
  { params = L" /s " + params; }

  // 失败则 regsvr32 拿不到 profile，中止注册；按本函数惯例报告并返回
  // 错误码，不抛异常（调用链无人捕获，抛出即 std::terminate）
  if (!SetEnvironmentVariable(L"TEXTSERVICE_PROFILE", profile.c_str())) {
    MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERR_SETENV_PROFILE,
                          IDS_STR_INORUN_FAILED, MB_ICONERROR | MB_OK);
    return 1;
  }

  std::wstring app = L"regsvr32.exe";
  if (is_wowarm32) {
    WCHAR sysarm32[MAX_PATH];
    get_wow_arm32_system_dir(sysarm32, _countof(sysarm32));

    app = std::wstring(sysarm32) + L"\\" + app;
  }

  SHELLEXECUTEINFOW shExInfo = {0};
  shExInfo.cbSize = sizeof(shExInfo);
  shExInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
  shExInfo.hwnd = 0;
  shExInfo.lpVerb = L"open";               // Operation to perform
  shExInfo.lpFile = app.c_str();           // Application to start
  shExInfo.lpParameters = params.c_str();  // Additional parameters
  shExInfo.lpDirectory = 0;
  shExInfo.nShow = SW_SHOW;
  shExInfo.hInstApp = 0;
  if (!ShellExecuteExW(&shExInfo)) {
    return report_regsvr32_failure(params, silent);
  }
  WaitForSingleObject(shExInfo.hProcess, INFINITE);
  DWORD exit_code = 0;
  BOOL got_exit_code = GetExitCodeProcess(shExInfo.hProcess, &exit_code);
  CloseHandle(shExInfo.hProcess);
  if (!got_exit_code || exit_code != 0) {
    // regsvr32 已运行但注册失败（退出码非 0）
    return report_regsvr32_failure(params, silent);
  }

  if (register_ime)
    enable_profile(TRUE, profile);

  return 0;
}

int install(const std::wstring& profile, bool silent) {
  std::wstring ime_src_path;

  // 文件未就位即终止：后续注册表与 TSF 注册全部不做
  if (0 != install_ime_file(ime_src_path, L".dll", profile, silent,
                            &register_text_service))
    return 1;

  // 写注册表
  WCHAR drive[_MAX_DRIVE];
  WCHAR dir[_MAX_DIR];
  _wsplitpath_s(ime_src_path.c_str(), drive, _countof(drive), dir,
                _countof(dir), NULL, 0, NULL, 0);
  std::wstring rootDir = std::wstring(drive) + dir;
  rootDir.pop_back();
  auto ret = SetRegKeyValue(HKEY_LOCAL_MACHINE, WEASEL_REG_KEY, L"WeaselRoot",
                            rootDir.c_str(), REG_SZ);
  if (FAILED(HRESULT_FROM_WIN32(ret))) {
    MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERRWRITEWEASELROOT,
                          IDS_STR_INSTALL_FAILED, MB_ICONERROR | MB_OK);
    return 1;
  }

  const std::wstring executable = L"WeaselServer.exe";
  ret = SetRegKeyValue(HKEY_LOCAL_MACHINE, WEASEL_REG_KEY, L"ServerExecutable",
                       executable.c_str(), REG_SZ);
  if (FAILED(HRESULT_FROM_WIN32(ret))) {
    MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERRREGIMEWRITESVREXE,
                          IDS_STR_INSTALL_FAILED, MB_ICONERROR | MB_OK);
    return 1;
  }

  // persist the installing profile so that uninstall removes the right one
  const WCHAR PROFILE_KEY[] = L"Software\\Rime\\Weasel";
  ret = SetRegKeyValue(per_user_root(), per_user_subkey(PROFILE_KEY).c_str(),
                       L"Profile", profile.c_str(), REG_SZ);
  if (FAILED(HRESULT_FROM_WIN32(ret))) {
    MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERR_WRITE_PROFILE,
                          IDS_STR_INSTALL_FAILED, MB_ICONERROR | MB_OK);
    return 1;
  }
  ret = SetRegKeyValue(per_user_root(), per_user_subkey(PROFILE_KEY).c_str(),
                       L"Hant", (profile == L"hant" ? 1 : 0), REG_DWORD);
  if (FAILED(HRESULT_FROM_WIN32(ret))) {
    MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERR_WRITE_HANT,
                          IDS_STR_INSTALL_FAILED, MB_ICONERROR | MB_OK);
    return 1;
  }

  // 把当前 profile 的输入布局加入用户的输入法列表
  install_layout_or_tip(profile, LayoutOrTipAction::Install);

  // https://learn.microsoft.com/zh-cn/windows/win32/wer/collecting-user-mode-dumps
  const std::wstring dmpPathW = WeaselLogPath().wstring();
  // DumpFolder
  SetRegKeyValue(HKEY_LOCAL_MACHINE, WEASEL_WER_KEY, L"DumpFolder",
                 dmpPathW.c_str(), REG_SZ, true);
  // mini dump：含线程栈与模块信息，足以定位崩溃栈。
  // 原配置 DumpType=0(custom)+CustomDumpFlags=0 为上游遗留，仅产出
  // MiniDumpNormal 最简内容；CustomDumpFlags 只在 DumpType=0 时生效，
  // 改用 DumpType=1 后不再写入
  SetRegKeyValue(HKEY_LOCAL_MACHINE, WEASEL_WER_KEY, L"DumpType", 1, REG_DWORD,
                 true);
  // maximium dump count 10
  SetRegKeyValue(HKEY_LOCAL_MACHINE, WEASEL_WER_KEY, L"DumpCount", 10,
                 REG_DWORD, true);

  MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_INSTALL_SUCCESS_INFO,
                        IDS_STR_INSTALL_SUCCESS_CAP,
                        MB_ICONINFORMATION | MB_OK);
  return 0;
}

int uninstall(bool silent) {
  // 注销输入法
  int retval = 0;

  const WCHAR KEY[] = L"Software\\Rime\\Weasel";
  HKEY hKey;
  std::wstring profile = L"hans";
  LSTATUS ret = RegOpenKeyW(per_user_root(), per_user_subkey(KEY).c_str(), &hKey);
  if (ret == ERROR_SUCCESS) {
    DWORD type = 0;
    DWORD data = 0;
    WCHAR value[MAX_PATH] = {0};
    DWORD len = sizeof(value);
    ret = RegQueryValueEx(hKey, L"Profile", NULL, &type, (LPBYTE)value, &len);
    if (ret == ERROR_SUCCESS && type == REG_SZ && value[0] != L'\0') {
      profile = value;
    } else {
      len = sizeof(data);
      ret = RegQueryValueEx(hKey, L"Hant", NULL, &type, (LPBYTE)&data, &len);
      if (ret == ERROR_SUCCESS && type == REG_DWORD) {
        profile = (data != 0) ? L"hant" : L"hans";
      }
    }

    install_layout_or_tip(profile, LayoutOrTipAction::Uninstall);
    RegCloseKey(hKey);
  }

  // IMM/.ime support removed; only uninstall TSF/.dll
  retval +=
      uninstall_ime_file(L".dll", profile, silent, &register_text_service);

  // 清除注册信息（RegDeleteKey 遇有子键的键即失败，必须递归整树删除；
  // 键本就不存在视为已清除，其余失败计入 retval 报卸载失败）
  for (const wchar_t* key : {WEASEL_REG_KEY, RIME_REG_KEY}) {
    const LSTATUS deleted = delete_reg_tree(HKEY_LOCAL_MACHINE, key);
    if (deleted != ERROR_SUCCESS && deleted != ERROR_FILE_NOT_FOUND)
      ++retval;
  }

  // NSIS 卸载脚本只清 HKLM 不碰 HKCU（output/install.nsi Uninstall 段），
  // 用户配置键（Profile/Hant/RimeUserDir/Language 等）在此一并清除；
  // 用户数据目录的文件留给用户自行处理
  const LSTATUS user_deleted =
      delete_reg_tree(per_user_root(), per_user_subkey(KEY).c_str());
  if (user_deleted != ERROR_SUCCESS && user_deleted != ERROR_FILE_NOT_FOUND)
    ++retval;

  // delete WER register,
  // "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\Windows Error
  // Reporting\\LocalDumps\\WeaselServer.exe" no WOW64 redirect

  auto flag_wow64 = is_wow64() ? KEY_WOW64_64KEY : 0;
  RegDeleteKeyEx(HKEY_LOCAL_MACHINE, WEASEL_WER_KEY, flag_wow64, 0);
  if (retval)
    return 1;

  MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_UNINSTALL_SUCCESS_INFO,
                        IDS_STR_UNINSTALL_SUCCESS_CAP,
                        MB_ICONINFORMATION | MB_OK);
  return 0;
}

bool has_installed() {
  WCHAR path[MAX_PATH];
  GetSystemDirectory(path, _countof(path));
  std::wstring sysPath(path);
  DWORD attr = GetFileAttributesW((sysPath + L"\\weasel.dll").c_str());
  return (attr != INVALID_FILE_ATTRIBUTES &&
          !(attr & FILE_ATTRIBUTE_DIRECTORY));
}
