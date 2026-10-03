#include "stdafx.h"
#include "WeaselServerApp.h"
#include <filesystem>
#include <vector>

namespace {

std::wstring GetDialogItemText(HWND dialog, int control_id) {
  HWND control = ::GetDlgItem(dialog, control_id);
  if (!control)
    return {};
  int length = ::GetWindowTextLengthW(control);
  if (length <= 0)
    return {};
  std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1);
  ::GetWindowTextW(control, buffer.data(),
                   static_cast<int>(buffer.size()));
  return std::wstring(buffer.data());
}

void ShowQuickWordMessage(HWND dialog, UINT string_id) {
  wchar_t message[256] = {};
  wchar_t caption[128] = {};
  ::LoadStringW(::GetModuleHandleW(nullptr), string_id, message,
                _countof(message));
  ::GetWindowTextW(dialog, caption, _countof(caption));
  ::MessageBoxW(dialog, message, caption, MB_OK | MB_ICONINFORMATION);
}

class QuickWordDialog : public CDialogImpl<QuickWordDialog> {
 public:
  enum { IDD = IDD_QUICK_WORD };

  QuickWordDialog(weasel::Server& server, DWORD session_id)
      : server_(server), session_id_(session_id) {}

  BEGIN_MSG_MAP(QuickWordDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  COMMAND_ID_HANDLER(IDOK, OnOk)
  COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
  END_MSG_MAP()

 private:
  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
    CenterWindow();
    ::SetFocus(GetDlgItem(IDC_QUICK_WORD_TEXT));
    return FALSE;
  }

  LRESULT OnOk(WORD, WORD, HWND, BOOL&) {
    const auto text = GetDialogItemText(m_hWnd, IDC_QUICK_WORD_TEXT);
    if (text.empty()) {
      ShowQuickWordMessage(m_hWnd, IDS_STR_QUICK_WORD_EMPTY_TEXT);
      ::SetFocus(GetDlgItem(IDC_QUICK_WORD_TEXT));
      return 0;
    }
    const auto code = GetDialogItemText(m_hWnd, IDC_QUICK_WORD_CODE);
    if (code.empty()) {
      ShowQuickWordMessage(m_hWnd, IDS_STR_QUICK_WORD_EMPTY_CODE);
      ::SetFocus(GetDlgItem(IDC_QUICK_WORD_CODE));
      return 0;
    }
    if (!server_.AddQuickWord(session_id_, text, code)) {
      ShowQuickWordMessage(m_hWnd, IDS_STR_QUICK_WORD_SAVE_FAILED);
      return 0;
    }
    EndDialog(IDOK);
    return 0;
  }

  LRESULT OnCancel(WORD, WORD, HWND, BOOL&) {
    EndDialog(IDCANCEL);
    return 0;
  }

  weasel::Server& server_;
  DWORD session_id_;
};

}  // namespace

WeaselServerApp::WeaselServerApp()
    : m_handler(std::make_unique<RimeWithWeaselHandler>(&m_ui)),
      tray_icon(m_ui) {
  // m_handler.reset(new RimeWithWeaselHandler(&m_ui));
  m_server.SetRequestHandler(m_handler.get());
  SetupMenuHandlers();
}

WeaselServerApp::~WeaselServerApp() {}

int WeaselServerApp::Run() {
  if (!m_server.Start())
    return -1;

  // win_sparkle_set_appcast_url("http://localhost:8000/weasel/update/appcast.xml");
  win_sparkle_set_registry_path("Software\\Rime\\Weasel\\Updates");
  if (GetThreadUILanguage() ==
      MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL))
    win_sparkle_set_lang("zh-TW");
  else if (GetThreadUILanguage() ==
           MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED))
    win_sparkle_set_lang("zh-CN");
  else
    win_sparkle_set_lang("en");
  win_sparkle_init();
  m_ui.Create(m_server.GetHWnd());

  m_handler->Initialize();
  m_handler->OnUpdateUI([this]() { tray_icon.RequestRefresh(); });

  tray_icon.Create(m_server.GetHWnd());
  m_server.SetTrayRefreshCallback([this]() { tray_icon.ApplyRefresh(); });
  m_server.SetQuickWordCallback(
      [this](DWORD session_id) { OpenQuickWordDialog(session_id); });
  tray_icon.RequestRefresh();

  int ret = m_server.Run();

  tray_icon.DisableRefresh();
  m_handler->Finalize();
  m_ui.Destroy();
  tray_icon.RemoveIcon();
  win_sparkle_cleanup();

  return ret;
}

void WeaselServerApp::OpenQuickWordDialog(DWORD session_id) {
  // A modal dialog runs its own message loop, so a repeated preserved-key
  // request can arrive while this function is still active.  Ignore it to
  // prevent recursively nested dialogs.
  if (m_quickWordDialogOpen)
    return;
  m_quickWordDialogOpen = true;
  QuickWordDialog dialog(m_server, session_id);
  dialog.DoModal(m_server.GetHWnd());
  m_quickWordDialogOpen = false;
}

void WeaselServerApp::SetupMenuHandlers() {
  std::filesystem::path dir = install_dir();
  m_server.AddMenuHandler(ID_WEASELTRAY_QUIT,
                          [this] { return m_server.Stop() == 0; });
  m_server.AddMenuHandler(ID_WEASELTRAY_DEPLOY,
                          std::bind(execute, dir / L"WeaselDeployer.exe",
                                    std::wstring(L"/deploy")));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_SETTINGS,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring()));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_DICT_MANAGEMENT,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring(L"/dict")));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_SYNC,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring(L"/sync")));
  m_server.AddMenuHandler(ID_WEASELTRAY_WIKI,
                          std::bind(open, L"https://rime.im/docs/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_HOMEPAGE,
                          std::bind(open, L"https://rime.im/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_FORUM,
                          std::bind(open, L"https://rime.im/discuss/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_CHECKUPDATE, check_update);
  m_server.AddMenuHandler(ID_WEASELTRAY_INSTALLDIR, std::bind(explore, dir));
  m_server.AddMenuHandler(ID_WEASELTRAY_USERCONFIG,
                          std::bind(explore, WeaselUserDataPath()));
  m_server.AddMenuHandler(ID_WEASELTRAY_LOGDIR,
                          std::bind(explore, WeaselLogPath()));
}
