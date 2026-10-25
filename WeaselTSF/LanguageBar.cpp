#include "stdafx.h"
#include <resource.h>
#include <shellapi.h>
#include "WeaselTSF.h"
#include "LanguageBar.h"
#include "CandidateList.h"
#include "Globals.h"
#include <WeaselUtility.h>

static const DWORD LANGBARITEMSINK_COOKIE = 0x42424242;

static void HMENU2ITfMenu(HMENU hMenu, ITfMenu* pTfMenu) {
  /* NOTE: Only limited functions are supported */
  int N = GetMenuItemCount(hMenu);
  for (int i = 0; i < N; i++) {
    MENUITEMINFO mii;
    mii.cbSize = sizeof(MENUITEMINFO);
    mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING;
    mii.dwTypeData = NULL;
    if (GetMenuItemInfo(hMenu, i, TRUE, &mii)) {
      UINT id = mii.wID;
      if (mii.fType == MFT_SEPARATOR)
        pTfMenu->AddMenuItem(id, TF_LBMENUF_SEPARATOR, NULL, NULL, NULL, 0,
                             NULL);
      else if (mii.fType == MFT_STRING) {
        mii.dwTypeData = (LPWSTR)malloc(sizeof(WCHAR) * (mii.cch + 1));
        mii.cch++;
        if (GetMenuItemInfo(hMenu, i, TRUE, &mii))
          pTfMenu->AddMenuItem(id, 0, NULL, NULL, mii.dwTypeData, mii.cch,
                               NULL);
        free(mii.dwTypeData);
      }
    }
  }
}

static LPCWSTR GetWeaselRegName() {
  LPCWSTR WEASEL_REG_NAME_;
  if (is_wow64())
    WEASEL_REG_NAME_ = L"Software\\WOW6432Node\\Rime\\Weasel";
  else
    WEASEL_REG_NAME_ = L"Software\\Rime\\Weasel";

  return WEASEL_REG_NAME_;
}

static bool open(const std::wstring& path) {
  return (uintptr_t)ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL,
                                  SW_SHOWNORMAL) > 32;
}

CLangBarItemButton::CLangBarItemButton(WeaselTSF* pTextService,
                                       REFGUID guid,
                                       const weasel::UIStyle& style)
    : _status(0),
      _style(style),
      _current_schema_zhung_icon(),
      _current_schema_ascii_icon() {
  DllAddRef();

  _pLangBarItemSink = NULL;
  _cRef = 1;
  _pTextService = pTextService;
  _guid = guid;
  ascii_mode = false;
}

CLangBarItemButton::~CLangBarItemButton() {
  DetachOwner();
  _DestroyCachedIcons();
  DllRelease();
}

void CLangBarItemButton::DetachOwner() {
  _pTextService = nullptr;
  _pLangBarItemSink = nullptr;
}

STDMETHODIMP CLangBarItemButton::QueryInterface(REFIID riid, void** ppvObject) {
  if (ppvObject == NULL)
    return E_INVALIDARG;

  *ppvObject = NULL;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
      IsEqualIID(riid, IID_ITfLangBarItemButton))
    *ppvObject = (ITfLangBarItemButton*)this;
  else if (IsEqualIID(riid, IID_ITfSource))
    *ppvObject = (ITfSource*)this;

  if (*ppvObject) {
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CLangBarItemButton::AddRef() {
  return ++_cRef;
}

STDMETHODIMP_(ULONG) CLangBarItemButton::Release() {
  LONG cr = --_cRef;
  assert(_cRef >= 0);
  if (_cRef == 0)
    delete this;
  return cr;
}

STDMETHODIMP CLangBarItemButton::GetInfo(TF_LANGBARITEMINFO* pInfo) {
  pInfo->clsidService = c_clsidTextService;
  pInfo->guidItem = _guid;
  pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU |
                   TF_LBI_STYLE_SHOWNINTRAY;
  pInfo->ulSort = 1;
  lstrcpyW(pInfo->szDescription, L"WeaselTSF Button");
  return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetStatus(DWORD* pdwStatus) {
  *pdwStatus = _status;
  return S_OK;
}

STDMETHODIMP CLangBarItemButton::Show(BOOL fShow) {
  SetLangbarStatus(TF_LBI_STATUS_HIDDEN, fShow ? FALSE : TRUE);
  return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetTooltipString(BSTR* pbstrToolTip) {
  LANGID langid = get_language_id();
  if (langid == TEXTSERVICE_LANGID_HANS) {
    *pbstrToolTip = SysAllocString(L"左键切换模式，右键打开菜单");
  } else if (langid == TEXTSERVICE_LANGID_HANT) {
    *pbstrToolTip = SysAllocString(L"左鍵切換模式，右鍵打開菜單");
  } else {
    *pbstrToolTip = SysAllocString(
        L"Left-click to switch modes\n\nRight-click for more options");
  }

  return (*pbstrToolTip == NULL) ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP CLangBarItemButton::OnClick(TfLBIClick click,
                                         POINT pt,
                                         const RECT* prcArea) {
  if (!_pTextService)
    return E_UNEXPECTED;
  if (click == TF_LBI_CLK_LEFT) {
    _pTextService->_HandleLangBarMenuSelect(
        ascii_mode ? ID_WEASELTRAY_DISABLE_ASCII : ID_WEASELTRAY_ENABLE_ASCII);
    ascii_mode = !ascii_mode;
    // Keep the sink alive across callbacks that may deactivate the TIP.
    com_ptr<ITfLangBarItemSink> sink = _pLangBarItemSink;
    if (sink)
      sink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
  } else if (click == TF_LBI_CLK_RIGHT) {
    /* Open menu */
    HWND hwnd = _pTextService->_GetFocusedContextWindow();
    if (hwnd != NULL) {
      LANGID langid = get_language_id();
      HMENU menu;
      if (langid == TEXTSERVICE_LANGID_HANS) {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP_HANS));
      } else if (langid == TEXTSERVICE_LANGID_HANT) {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP_HANT));
      } else {
        menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP));
      }
      HMENU popupMenu = GetSubMenu(menu, 0);
      UINT wID = TrackPopupMenuEx(
          popupMenu, TPM_NONOTIFY | TPM_RETURNCMD | TPM_HORPOSANIMATION, pt.x,
          pt.y, hwnd, NULL);
      DestroyMenu(menu);
      // TrackPopupMenuEx runs a nested message pump; the TIP may deactivate.
      if (_pTextService)
        _pTextService->_HandleLangBarMenuSelect(wID);
    }
  }
  return S_OK;
}

STDMETHODIMP CLangBarItemButton::InitMenu(ITfMenu* pMenu) {
  HMENU menu = LoadMenuW(g_hInst, MAKEINTRESOURCE(IDR_MENU_POPUP));
  HMENU popupMenu = GetSubMenu(menu, 0);
  HMENU2ITfMenu(popupMenu, pMenu);
  DestroyMenu(menu);
  return S_OK;
}

STDMETHODIMP CLangBarItemButton::OnMenuSelect(UINT wID) {
  if (!_pTextService)
    return E_UNEXPECTED;
  _pTextService->_HandleLangBarMenuSelect(wID);
  return S_OK;
}

// 命中缓存返回 master 的副本；路径变化或首次调用时 LR_LOADFROMFILE
// 重载 master。调用方（语言栏）按 MSDN 约定 DestroyIcon 销毁返回值，
// 因此每次都返回 CopyIcon 副本，绝不能把 master 本身交出去
HICON CLangBarItemButton::_LoadCachedFileIcon(const std::wstring& path,
                                              std::wstring& cachedPath,
                                              HICON& cachedIcon) {
  if (cachedIcon == NULL || cachedPath != path) {
    if (cachedIcon != NULL) {
      DestroyIcon(cachedIcon);
      cachedIcon = NULL;
      cachedPath.clear();
    }
    cachedIcon = (HICON)LoadImageW(
        NULL, path.c_str(), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
    if (cachedIcon == NULL)
      return NULL;
    cachedPath = path;
  }
  return CopyIcon(cachedIcon);
}

void CLangBarItemButton::_DestroyCachedIcons() {
  if (_zhung_icon != NULL) {
    DestroyIcon(_zhung_icon);
    _zhung_icon = NULL;
    _zhung_icon_path.clear();
  }
  if (_ascii_icon != NULL) {
    DestroyIcon(_ascii_icon);
    _ascii_icon = NULL;
    _ascii_icon_path.clear();
  }
}

STDMETHODIMP CLangBarItemButton::GetIcon(HICON* phIcon) {
  if (ascii_mode) {
    if (_style.current_ascii_icon.empty())
      *phIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_EN), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    else
      *phIcon = _LoadCachedFileIcon(_style.current_ascii_icon, _ascii_icon_path,
                                    _ascii_icon);
  } else {
    if (_style.current_zhung_icon.empty())
      *phIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ZH), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    else
      *phIcon = _LoadCachedFileIcon(_style.current_zhung_icon, _zhung_icon_path,
                                    _zhung_icon);
  }
  return (*phIcon == NULL) ? E_FAIL : S_OK;
}

STDMETHODIMP CLangBarItemButton::GetText(BSTR* pbstrText) {
  *pbstrText = SysAllocString(L"WeaselTSF Button");
  return (*pbstrText == NULL) ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP CLangBarItemButton::AdviseSink(REFIID riid,
                                            IUnknown* punk,
                                            DWORD* pdwCookie) {
  if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
    return CONNECT_E_CANNOTCONNECT;
  if (_pLangBarItemSink != NULL)
    return CONNECT_E_ADVISELIMIT;

  if (punk->QueryInterface(IID_ITfLangBarItemSink,
                           (LPVOID*)&_pLangBarItemSink) != S_OK) {
    _pLangBarItemSink = NULL;
    return E_NOINTERFACE;
  }
  *pdwCookie = LANGBARITEMSINK_COOKIE;
  return S_OK;
}

STDMETHODIMP CLangBarItemButton::UnadviseSink(DWORD dwCookie) {
  if (dwCookie != LANGBARITEMSINK_COOKIE || _pLangBarItemSink == NULL)
    return CONNECT_E_NOCONNECTION;
  _pLangBarItemSink = NULL;
  return S_OK;
}

void CLangBarItemButton::UpdateWeaselStatus(weasel::Status stat,
                                            const weasel::UIStyle& style) {
  if (!_pTextService)
    return;
  // 仅状态真变化才通知语言栏：图标内容由 ascii_mode 与 schema 图标路径
  // 惟一决定，状态位(_status)只经 SetLangbarStatus 变化（自带变化检测），
  // 无变化时 OnUpdate 只会让系统空跑 GetStatus/GetIcon
  bool changed = false;
  if (_style.current_zhung_icon != style.current_zhung_icon ||
      _style.current_ascii_icon != style.current_ascii_icon) {
    // LanguageBar only consumes the two icon paths. Avoid copying the entire
    // UIStyle on every status/keystroke update.
    _style.current_zhung_icon = style.current_zhung_icon;
    _style.current_ascii_icon = style.current_ascii_icon;
    changed = true;
  }
  if (stat.ascii_mode != ascii_mode) {
    ascii_mode = stat.ascii_mode;
    changed = true;
  }
  if (_current_schema_zhung_icon != _style.current_zhung_icon) {
    _current_schema_zhung_icon = _style.current_zhung_icon;
    changed = true;
  }
  if (_current_schema_ascii_icon != _style.current_ascii_icon) {
    _current_schema_ascii_icon = _style.current_ascii_icon;
    changed = true;
  }
  com_ptr<ITfLangBarItemSink> sink = _pLangBarItemSink;
  if (changed && sink)
    sink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
}

void CLangBarItemButton::SetLangbarStatus(DWORD dwStatus, BOOL fSet) {
  BOOL isChange = FALSE;

  if (fSet) {
    if (!(_status & dwStatus)) {
      _status |= dwStatus;
      isChange = TRUE;
    }
  } else {
    if (_status & dwStatus) {
      _status &= ~dwStatus;
      isChange = TRUE;
    }
  }

  com_ptr<ITfLangBarItemSink> sink = _pLangBarItemSink;
  if (isChange && sink)
    sink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);

  return;
}

std::wstring WeaselTSF::_GetRootDir() {
  std::wstring dir{};
  RegGetStringValue(HKEY_LOCAL_MACHINE, GetWeaselRegName(), L"WeaselRoot", dir);
  return dir;
}

void WeaselTSF::_HandleLangBarMenuSelect(UINT wID) {
#if defined(WEASEL_TEST_DISABLE_SERVER_LAUNCH)
  if (wID == ID_WEASELTRAY_RERUN_SERVICE)
    return;
#endif
  std::wstring dir{};
  switch (wID) {
    case ID_WEASELTRAY_RERUN_SERVICE:
    case ID_WEASELTRAY_INSTALLDIR:
      if (RegGetStringValue(HKEY_LOCAL_MACHINE, GetWeaselRegName(),
                            L"WeaselRoot", dir) == ERROR_SUCCESS) {
        if (wID == ID_WEASELTRAY_RERUN_SERVICE) {
          const bool queued = QueueDllBackgroundTask([dir]() {
            const auto server = dir + L"\\WeaselServer.exe";
            const HINSTANCE launched = ShellExecuteW(
                NULL, L"open", server.c_str(), NULL, dir.c_str(), SW_HIDE);
            if (reinterpret_cast<INT_PTR>(launched) <= 32) {
              LOG(ERROR) << "Could not launch WeaselServer from language bar, "
                            "ShellExecute error: "
                         << reinterpret_cast<INT_PTR>(launched);
            }
          });
          if (!queued)
            LOG(ERROR) << "Could not queue WeaselServer restart from language "
                          "bar.";
        } else
          open(dir);
      }
      break;
    case ID_WEASELTRAY_USERCONFIG:
      if (FAILED(RegGetStringValue(HKEY_CURRENT_USER, L"Software\\Rime\\Weasel",
                                   L"RimeUserDir", dir)) ||
          dir.empty()) {
        WCHAR _path[MAX_PATH] = {0};
        ExpandEnvironmentStringsW(L"%AppData%\\Rime", _path, _countof(_path));
        dir = std::wstring(_path);
      }
      if (!dir.empty() && fs::exists(dir))
        open(dir);
      else
        MessageBoxW(NULL, (L"Not found: " + dir).c_str(), L"RimeUserDir",
                    MB_ICONERROR | MB_OK);
      break;
    case ID_WEASELTRAY_LOGDIR:
      open(WeaselLogPath().wstring());
      break;
    case ID_WEASELTRAY_WIKI:
      open(L"https://rime.im/docs/");
      break;
    case ID_WEASELTRAY_FORUM:
      open(L"https://rime.im/discuss/");
      break;
    default:
      m_client.TrayCommand(wID);
      break;
  }
}

HWND WeaselTSF::_GetFocusedContextWindow() {
  HWND hwnd = NULL;
  ITfDocumentMgr* pDocMgr;
  if (_pThreadMgr->GetFocus(&pDocMgr) == S_OK && pDocMgr != NULL) {
    ITfContext* pContext;
    if (pDocMgr->GetTop(&pContext) == S_OK && pContext != NULL) {
      ITfContextView* pContextView;
      if (pContext->GetActiveView(&pContextView) == S_OK &&
          pContextView != NULL) {
        pContextView->GetWnd(&hwnd);
        pContextView->Release();
      }
      pContext->Release();
    }
    pDocMgr->Release();
  }

  if (hwnd == NULL) {
    HWND hwndForeground = GetForegroundWindow();
    if (GetWindowThreadProcessId(hwndForeground, NULL) == GetCurrentThreadId())
      hwnd = hwndForeground;
  }

  return hwnd;
}

BOOL WeaselTSF::_InitLanguageBar() {
  com_ptr<ITfLangBarItemMgr> pLangBarItemMgr;
  BOOL fRet = FALSE;

  if (_pThreadMgr->QueryInterface(&pLangBarItemMgr) != S_OK)
    return FALSE;

  // Attach (no AddRef): the releasing CComPtr assignment in
  // _UninitLanguageBar then drops the last reference instead of leaking one.
  _pLangBarButton.Attach(
      new CLangBarItemButton(this, GUID_LBI_INPUTMODE, _cand->style()));

  if (pLangBarItemMgr->AddItem(_pLangBarButton) != S_OK) {
    _pLangBarButton->DetachOwner();
    _pLangBarButton = NULL;
    return FALSE;
  }

  _pLangBarButton->Show(TRUE);
  fRet = TRUE;

  return fRet;
}

void WeaselTSF::_UninitLanguageBar() {
  com_ptr<ITfLangBarItemMgr> pLangBarItemMgr;

  if (_pLangBarButton == NULL)
    return;

  // Detach before RemoveItem's potential synchronous host reentrancy.
  _pLangBarButton->DetachOwner();
  if (_pThreadMgr && _pThreadMgr->QueryInterface(&pLangBarItemMgr) == S_OK) {
    pLangBarItemMgr->RemoveItem(_pLangBarButton);
  }

  _pLangBarButton = NULL;
}

void WeaselTSF::_UpdateLanguageBar(weasel::Status stat) {
  if (!_pLangBarButton)
    return;
  DWORD flags = 0;
  HRESULT hr = _GetCompartmentDWORD(
      flags, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
  if (FAILED(hr)) {
    // 读取失败：compartment 当前值未知，不回写。原实现按 flags=0 重建回写，
    // 会清掉宿主设置的 TF_CONVERSIONMODE_ROMAN/KATAKANA 等无关转换位
  } else {
    // hr == S_OK：读到 VT_I4；hr == S_FALSE：VT_EMPTY（从未设置），以 0 为基
    // 首次初始化，与原实现一致
    const DWORD actual_flags = flags;
    if (stat.ascii_mode)
      flags &= (~TF_CONVERSIONMODE_NATIVE);
    else
      flags |= TF_CONVERSIONMODE_NATIVE;
    if (stat.full_shape)
      flags |= TF_CONVERSIONMODE_FULLSHAPE;
    else
      flags &= (~TF_CONVERSIONMODE_FULLSHAPE);
    // Compare against the actual compartment value, not a previous write.
    // Initialize an unset compartment even when the desired value is zero.
    // Preserve unrelated host-controlled bits and avoid redundant OnChange.
    if (hr == S_FALSE || flags != actual_flags)
      _SetCompartmentDWORD(flags, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
  }

  _pLangBarButton->UpdateWeaselStatus(stat, _cand->style());
}

void WeaselTSF::_ShowLanguageBar(BOOL show) {
  if (!_pLangBarButton)
    return;
  _pLangBarButton->Show(show);
}

void WeaselTSF::_EnableLanguageBar(BOOL enable) {
  if (!_pLangBarButton)
    return;
  _pLangBarButton->SetLangbarStatus(TF_LBI_STATUS_DISABLED, !enable);
}