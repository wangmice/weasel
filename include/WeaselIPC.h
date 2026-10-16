#pragma once
#include <WeaselIPCData.h>
#include <WeaselUtility.h>
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <KeyEvent.h>

#define WEASEL_IPC_WINDOW L"WeaselIPCWindow_1.0"
#define WEASEL_IPC_PIPE_NAME L"WeaselNamedPipe"

#define WEASEL_IPC_METADATA_SIZE 1024
#define WEASEL_IPC_BUFFER_SIZE (4 * 1024)
#define WEASEL_IPC_BUFFER_LENGTH (WEASEL_IPC_BUFFER_SIZE / sizeof(WCHAR))
#define WEASEL_IPC_SHARED_MEMORY_SIZE \
  (sizeof(PipeMessage) + WEASEL_IPC_BUFFER_SIZE)

enum WEASEL_IPC_COMMAND {
  WEASEL_IPC_ECHO = (WM_APP + 1),
  WEASEL_IPC_START_SESSION,
  WEASEL_IPC_END_SESSION,
  WEASEL_IPC_PROCESS_KEY_EVENT,
  WEASEL_IPC_SHUTDOWN_SERVER,
  WEASEL_IPC_FOCUS_IN,
  WEASEL_IPC_FOCUS_OUT,
  WEASEL_IPC_UPDATE_INPUT_POS,
  WEASEL_IPC_START_MAINTENANCE,
  WEASEL_IPC_END_MAINTENANCE,
  WEASEL_IPC_COMMIT_COMPOSITION,
  WEASEL_IPC_CLEAR_COMPOSITION,
  WEASEL_IPC_TRAY_COMMAND,
  WEASEL_IPC_SELECT_CANDIDATE_ON_CURRENT_PAGE,
  WEASEL_IPC_HIGHLIGHT_CANDIDATE_ON_CURRENT_PAGE,
  WEASEL_IPC_CHANGE_PAGE,
  WEASEL_IPC_LAST_COMMAND
};

// Posted by WeaselTrayIcon to the server window so that Shell_NotifyIcon runs
// on the server message thread instead of a pipe worker thread.
#define WM_WEASEL_SERVICE_NOTIFY (WEASEL_IPC_LAST_COMMAND + 200)

namespace weasel {
// Typed key result for callers that need to distinguish a normal unhandled
// key from session invalidation and an ambiguous transport failure.
enum class KeyEventResult {
  Handled,
  Unhandled,
  InvalidSession,
  TransportError,
};

// Kept in the response body instead of the scalar DWORD result so an older
// client still sees FALSE and releases the key rather than swallowing it.
constexpr wchar_t kInvalidSessionResponseField[] = L"session.invalid=1";

struct PipeMessage {
  WEASEL_IPC_COMMAND Msg;
  DWORD wParam;
  DWORD lParam;
};

struct IPCMetadata {
  enum { WINDOW_CLASS_LENGTH = 64 };
  UINT32 server_hwnd;
  WCHAR server_window_class[WINDOW_CLASS_LENGTH];
};

// 處理請求之物件
struct RequestHandler {
  using EatLine = std::function<bool(std::wstring&)>;
  RequestHandler() {}
  virtual ~RequestHandler() {}
  virtual void Initialize() {}
  virtual void Finalize() {}
  virtual DWORD FindSession(DWORD session_id) { return 0; }
  virtual DWORD AddSession(LPWSTR buffer, EatLine eat = 0) { return 0; }
  virtual DWORD RemoveSession(DWORD session_id) { return 0; }
  virtual BOOL ProcessKeyEvent(KeyEvent keyEvent,
                               DWORD session_id,
                               EatLine eat) {
    return FALSE;
  }
  virtual void CommitComposition(DWORD session_id, EatLine eat) {}
  virtual void ClearComposition(DWORD session_id) {}
  virtual void SelectCandidateOnCurrentPage(size_t index,
                                            DWORD session_id,
                                            EatLine eat) {}
  virtual bool HighlightCandidateOnCurrentPage(size_t index,
                                               DWORD session_id,
                                               EatLine eat) {
    return false;
  }
  virtual bool ChangePage(bool backward, DWORD session_id, EatLine eat) {
    return false;
  }
  virtual void FocusIn(DWORD param, DWORD session_id) {}
  virtual void FocusOut(DWORD param, DWORD session_id) {}
  virtual void UpdateInputPosition(RECT const& rc, DWORD session_id) {}
  virtual void StartMaintenance() {}
  virtual void EndMaintenance() {}
  virtual void SetOption(DWORD session_id, const std::string& opt, bool val) {}
  virtual void UpdateColorTheme(BOOL darkMode) {}
};

// 處理server端回應之物件
typedef std::function<bool(LPWSTR buffer, DWORD length)> ResponseHandler;

// 事件處理函數
typedef std::function<bool()> CommandHandler;

// IPC實現類聲明

class ClientImpl;
class ServerImpl;
class ClientTestAccess;

// IPC接口類

class Client {
 public:
  Client();
  virtual ~Client();

  // 连接到服务进程的管道（服务进程由 TSF 注册的 autostart 保证在位）
  bool Connect();
  // 断开连接
  void Disconnect();
  // 终止服务
  void ShutdownServer();
  // 發起會話
  void StartSession();
  // 結束會話
  void EndSession();
  // 進入維護模式；仅当服务端已完成状态切换并返回确认时为 true
  bool StartMaintenance();
  // 退出維護模式
  void EndMaintenance();
  // 测试连接
  bool Echo();
  // 会话已建立在活连接上（失败后为 false，直到重新建立会话）
  bool IsSessionActive();
  // 保留未吃键、失效会话与传输失败之间的区别；TSF 用它决定是否可安全重试
  KeyEventResult ProcessKeyEventEx(KeyEvent const& keyEvent);
  // 请求服务处理按键消息
  bool ProcessKeyEvent(KeyEvent const& keyEvent);
  // 上屏正在編輯的文字
  bool CommitComposition();
  // 清除正在編輯的文字
  bool ClearComposition();
  // 选择当前页面编号为index的候选
  bool SelectCandidateOnCurrentPage(size_t index);
  // 高亮当前页面编号为index的候选
  bool HighlightCandidateOnCurrentPage(size_t index);
  // 翻页，backward = true 向前翻，false向后翻
  bool ChangePage(bool backward);
  // 更新输入位置
  void UpdateInputPosition(RECT const& rc);
  // 输入窗口获得焦点
  void FocusIn();
  // 输入窗口失去焦点
  void FocusOut();
  // 托盤菜單
  void TrayCommand(UINT menuId);
  // 读取server返回的数据
  bool GetResponseData(ResponseHandler handler);
  // 本线程最近一次收到的带体应答序号；前进意味着缓冲区中有一份未解析的应答
  UINT64 ResponseSerial();

 private:
  friend class ClientTestAccess;
  ClientImpl* m_pImpl;
};

class Server {
 public:
  Server();
  virtual ~Server();

  // 初始化服务
  HWND Start();
  // 结束服务
  int Stop();
  // 消息循环
  int Run();

  void SetRequestHandler(RequestHandler* pHandler);
  void AddMenuHandler(UINT uID, CommandHandler handler);
  HWND GetHWnd();

  // Callback invoked on the server message thread when a tray icon refresh is
  // requested from a pipe worker thread.
  void SetTrayRefreshCallback(std::function<void()> callback);

 private:
  ServerImpl* m_pImpl;
};

inline bool GetCurrentWindowsSessionId(DWORD& session_id) noexcept {
  session_id = 0;
  return ::ProcessIdToSessionId(::GetCurrentProcessId(), &session_id) != FALSE;
}

// Keep the pre-session-scoping endpoint available during upgrades: processes
// that already loaded an older TSF DLL cannot change their pipe name until the
// host reloads the input method.
inline std::wstring GetLegacyPipeName() {
  std::wstring pipe_name = L"\\\\.\\pipe\\";
  pipe_name += getUsername();
  pipe_name += L"\\";
  pipe_name += WEASEL_IPC_PIPE_NAME;
  return pipe_name;
}

inline std::wstring GetPipeName() {
  DWORD session_id = 0;
  if (!GetCurrentWindowsSessionId(session_id))
    return GetLegacyPipeName();

  std::wstring pipe_name = GetLegacyPipeName();
  pipe_name += L"-session-";
  pipe_name += std::to_wstring(session_id);
  return pipe_name;
}

inline std::wstring GetServerInstanceName() {
  std::wstring name = L"(WEASEL)Furandōru-Sukāretto-";
  name += getUsername();
  DWORD session_id = 0;
  if (GetCurrentWindowsSessionId(session_id)) {
    name += L"-session-";
    name += std::to_wstring(session_id);
  }
  return name;
}

inline std::wstring GetServerLaunchMutexName() {
  std::wstring name = L"WeaselServerLaunch-";
  name += getUsername();
  DWORD session_id = 0;
  if (GetCurrentWindowsSessionId(session_id)) {
    name += L"-session-";
    name += std::to_wstring(session_id);
  }
  return name;
}
}  // namespace weasel
