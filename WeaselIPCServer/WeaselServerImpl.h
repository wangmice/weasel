#pragma once
#include <WeaselIPC.h>
#include <WeaselInteractionPolicy.h>
#include <map>
#include <optional>
#include <Winnt.h>   // for security attributes constants
#include <aclapi.h>  // for ACL
#include <boost/thread.hpp>
#include <PipeChannel.h>

#include "ClientProcessTracker.h"
#include "SecurityAttribute.h"
#include "SessionRoutingHandler.h"

namespace weasel {
class PipeServer;
class ServerImplTestAccess;

typedef CWinTraits<WS_DISABLED, WS_EX_TRANSPARENT> ServerWinTraits;

class ServerImpl : public CWindowImpl<ServerImpl, CWindow, ServerWinTraits>
// class ServerImpl
{
 public:
  DECLARE_WND_CLASS(WEASEL_IPC_WINDOW)

  BEGIN_MSG_MAP(WEASEL_IPC_WINDOW)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_QUERYENDSESSION, OnQueryEndSystemSession)
  MESSAGE_HANDLER(WM_ENDSESSION, OnEndSystemSessionChecked)
  MESSAGE_HANDLER(WM_DWMCOLORIZATIONCOLORCHANGED, OnColorChange)
  MESSAGE_HANDLER(WM_SETTINGCHANGE, OnColorChange)
  MESSAGE_HANDLER(WM_COMMAND, OnCommand)
  MESSAGE_HANDLER(WM_WEASEL_SERVICE_NOTIFY, OnServiceNotifyMessage)
  END_MSG_MAP()

  LRESULT OnColorChange(UINT uMsg,
                        WPARAM wParam,
                        LPARAM lParam,
                        BOOL& bHandled);
  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnQueryEndSystemSession(UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled);
  LRESULT OnEndSystemSession(UINT uMsg,
                             WPARAM wParam,
                             LPARAM lParam,
                             BOOL& bHandled);
  LRESULT OnEndSystemSessionChecked(UINT uMsg,
                                    WPARAM wParam,
                                    LPARAM lParam,
                                    BOOL& bHandled) {
    if (!interaction_policy::ShouldFinalizeEndSession(wParam != FALSE))
      return 0;
    return OnEndSystemSession(uMsg, wParam, lParam, bHandled);
  }
  LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnServiceNotifyMessage(UINT uMsg,
                                 WPARAM wParam,
                                 LPARAM lParam,
                                 BOOL& bHandled);
  DWORD OnCommand(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);
  DWORD OnEcho(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);
  DWORD OnStartSession(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);
  DWORD OnEndSession(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);
  DWORD OnKeyEvent(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);
  DWORD OnShutdownServer(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);
  DWORD OnFocusIn(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);
  DWORD OnFocusOut(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);
  DWORD OnUpdateInputPosition(WEASEL_IPC_COMMAND uMsg,
                              DWORD wParam,
                              DWORD lParam);
  DWORD OnStartMaintenance(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);
  DWORD OnEndMaintenance(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);
  DWORD OnCommitComposition(WEASEL_IPC_COMMAND uMsg,
                            DWORD wParam,
                            DWORD lParam);
  DWORD OnClearComposition(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);
  DWORD OnSelectCandidateOnCurrentPage(WEASEL_IPC_COMMAND uMsg,
                                       DWORD wParam,
                                       DWORD lParam);
  DWORD OnHighlightCandidateOnCurrentPage(WEASEL_IPC_COMMAND uMsg,
                                          DWORD wParam,
                                          DWORD lParam);
  DWORD OnChangePage(WEASEL_IPC_COMMAND uMsg, DWORD wParam, DWORD lParam);

 public:
  ServerImpl();
  ~ServerImpl();

  HWND Start();
  int Stop();
  int Run();

  void SetRequestHandler(RequestHandler* pHandler) {
    session_router.SetBackend(pHandler);
    m_pRequestHandler =
        pHandler ? static_cast<RequestHandler*>(&session_router) : nullptr;
  }
  void AddMenuHandler(UINT uID, CommandHandler& handler) {
    m_MenuHandlers[uID] = handler;
  }
  void SetTrayRefreshCallback(std::function<void()> callback) {
    m_trayRefreshCallback = std::move(callback);
  }

 private:
  friend class ServerImplTestAccess;

  using PPhysicalToLogicalPointForPerMonitorDPI = BOOL(WINAPI*)(HWND, LPPOINT);

  void _Finailize();
  void _OnClientProcessExit(ClientProcessToken token);
  // Caller must hold g_api_mutex for commands that change handler options.
  LRESULT _HandleCommand(UINT uID, LPARAM lParam, BOOL& bHandled);
  /* Interrupt and join the pipe listener and its workers; idempotent */
  void _StopPipeServer();
  int _RequestStopUnderApiLock() noexcept;
  void _SetRuntimeLifecycle(ServerLifecycle lifecycle) noexcept;
  ServerLifecycle _RuntimeLifecycle() const noexcept;
  bool _TryTransitionRuntimeLifecycle(ServerLifecycle expected,
                                      ServerLifecycle desired) noexcept;
  bool _PublishRuntimeState() noexcept;
  void _UnlockStartupLifecycleLock() noexcept;
  template <typename _Resp>
  void HandlePipeMessage(PipeMessage pipe_msg, _Resp resp);

  // The channel stores a pointer to these attributes, so the security object
  // must be constructed before the channel and destroyed after it.
  SecurityAttribute sa;
  std::unique_ptr<PipeServer> channel;
  std::unique_ptr<boost::thread> pipeThread;
  SessionRoutingHandler session_router;
  // Declared after the router so it is destroyed first; _Finailize also stops
  // it explicitly after pipe workers drain and before the handler/UI may die.
  ClientProcessTracker process_tracker;
  // Resolved from the actual named-pipe peer. The tracker token identifies a
  // process generation rather than a numeric PID, so PID reuse cannot inherit
  // maintenance ownership.
  std::optional<ClientProcessToken> m_dispatchProcessOwner;
  std::optional<ClientProcessToken> m_maintenanceOwner;
  RequestHandler* m_pRequestHandler;  // points at session_router while active
  ServerLifecycleLock m_startupLifecycleLock;
  HANDLE m_runtimeStateMapping;
  ServerRuntimeState* m_runtimeState;
  std::map<UINT, CommandHandler> m_MenuHandlers;
  std::function<void()> m_trayRefreshCallback;
  HMODULE m_hUser32Module;
  /* Resolved once in the ctor; stays null if user32 does not export it
   * (below Win8.1), and OnUpdateInputPosition then keeps physical coords */
  PPhysicalToLogicalPointForPerMonitorDPI
      m_pPhysicalToLogicalPointForPerMonitorDPI;
  BOOL m_darkMode;
};

}  // namespace weasel
