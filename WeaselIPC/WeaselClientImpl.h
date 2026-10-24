#pragma once
#include <WeaselIPC.h>
#include <WeaselSessionKey.h>
#include <PipeChannel.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

namespace weasel {

class ClientImpl {
 public:
  ClientImpl();
  ~ClientImpl();

  bool Connect();
  bool Connect(DWORD timeout_ms);
  void Disconnect();
  void ShutdownServer();
  void StartSession();
  void StartSession(DWORD timeout_ms);
  void EndSession();
  bool StartMaintenance();
  bool EndMaintenance();
  bool Echo();
  bool ProbeServer(DWORD timeout_ms);
  KeyEventResult ProcessKeyEventEx(KeyEvent const& keyEvent);
  KeyEventResult ProcessKeyEventEx(KeyEvent const& keyEvent, DWORD timeout_ms);
  bool ProcessKeyEvent(KeyEvent const& keyEvent);
  bool CommitComposition();
  bool ClearComposition();
  bool ClearComposition(DWORD timeout_ms);
  bool SelectCandidateOnCurrentPage(size_t index);
  CandidateSelectResult SelectCandidateOnCurrentPageEx(size_t index);
  bool HighlightCandidateOnCurrentPage(size_t index);
  bool ChangePage(bool backward);
  void UpdateInputPosition(RECT const& rc);
  void FocusIn();
  void FocusOut();
  void TrayCommand(UINT menuId);
  bool GetResponseData(ResponseHandler const& handler);
  UINT64 ResponseSerial() { return channel.ResponseSerial(); }

  /* True while a session is established on a live pipe connection */
  bool IsSessionActive() const { return _Active(); }

 protected:
  void _InitializeClientInfo();
  bool _EnsureSessionKey();
  void _RetireSessionKey();
  bool _WriteClientInfo();

  LRESULT _SendMessage(WEASEL_IPC_COMMAND Msg,
                       DWORD wParam,
                       DWORD lParam,
                       DWORD timeout_ms = 0);

  bool _Connected() const { return channel.Connected(); }
  bool _Active() const { return channel.Connected() && _SessionId() != 0; }

 private:
  friend class ClientTestAccess;
  // Written by the connecting thread, read by every UI thread of the host
  // process (the pipe handle itself is thread-local); relaxed load/store
  // suffice: it is a plain value, no ordering is carried through it.
  std::atomic<UINT> session_id;

  UINT _SessionId() const { return session_id.load(std::memory_order_relaxed); }
  void _SetSessionId(UINT id) {
    session_id.store(id, std::memory_order_relaxed);
  }
  bool _ClearSessionIdIf(UINT expected) {
    return session_id.compare_exchange_strong(
        expected, 0, std::memory_order_relaxed, std::memory_order_relaxed);
  }

  bool _WriteClientInfo(std::uint64_t* generation);
  void _RetireSessionKeyLocked();
  void _InvalidateLogicalSession();
  void _EndSessionId(UINT session_id);

  // Logical identity is independent from the thread-local transport. Lifecycle
  // operations can race across host UI threads, so protect only this cold-path
  // state; the healthy key path never acquires this mutex.
  mutable std::mutex session_key_mutex_;
  std::optional<SessionKey> session_key_;
  std::uint64_t session_generation_ = 0;
  std::function<std::optional<SessionKey>()> session_key_generator_;

  std::wstring app_name;
  bool is_ime;

  PipeChannel<PipeMessage> channel;
};

}  // namespace weasel
