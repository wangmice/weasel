#pragma once
#include <WeaselIPC.h>
#include <PipeChannel.h>

#include <atomic>

namespace weasel {

class ClientImpl {
 public:
  ClientImpl();
  ~ClientImpl();

  bool Connect();
  void Disconnect();
  void ShutdownServer();
  void StartSession();
  void EndSession();
  void StartMaintenance();
  void EndMaintenance();
  bool Echo();
  bool ProcessKeyEvent(KeyEvent const& keyEvent);
  bool CommitComposition();
  bool ClearComposition();
  bool SelectCandidateOnCurrentPage(size_t index);
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
  bool _WriteClientInfo();

  LRESULT _SendMessage(WEASEL_IPC_COMMAND Msg, DWORD wParam, DWORD lParam);

  bool _Connected() const { return channel.Connected(); }
  bool _Active() const {
    return channel.Connected() && _SessionId() != 0;
  }

 private:
  // Written by the connecting thread, read by every UI thread of the host
  // process (the pipe handle itself is thread-local); relaxed load/store
  // suffice: it is a plain value, no ordering is carried through it
  std::atomic<UINT> session_id;

  UINT _SessionId() const { return session_id.load(std::memory_order_relaxed); }
  void _SetSessionId(UINT id) {
    session_id.store(id, std::memory_order_relaxed);
  }
  std::wstring app_name;
  bool is_ime;

  PipeChannel<PipeMessage> channel;
};

}  // namespace weasel