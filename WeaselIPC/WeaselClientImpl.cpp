#include "stdafx.h"
#include "WeaselClientImpl.h"
#include <StringAlgorithm.hpp>
#include <WeaselInteractionPolicy.h>

using namespace weasel;

namespace {

constexpr DWORD kStateChangingTransactionTimeoutMs = 3000;
constexpr DWORD kMaintenanceTransactionTimeoutMs = 30000;
constexpr DWORD kTeardownTransactionTimeoutMs = 100;

DWORD TransactionTimeoutForCommand(WEASEL_IPC_COMMAND command) {
  switch (command) {
    case WEASEL_IPC_START_MAINTENANCE:
    case WEASEL_IPC_END_MAINTENANCE:
      return kMaintenanceTransactionTimeoutMs;
    case WEASEL_IPC_START_SESSION:
    case WEASEL_IPC_SHUTDOWN_SERVER:
    case WEASEL_IPC_COMMIT_COMPOSITION:
    case WEASEL_IPC_CLEAR_COMPOSITION:
    case WEASEL_IPC_TRAY_COMMAND:
    case WEASEL_IPC_SELECT_CANDIDATE_ON_CURRENT_PAGE:
    case WEASEL_IPC_HIGHLIGHT_CANDIDATE_ON_CURRENT_PAGE:
    case WEASEL_IPC_CHANGE_PAGE:
      return kStateChangingTransactionTimeoutMs;
    case WEASEL_IPC_END_SESSION:
      return kTeardownTransactionTimeoutMs;
    default:
      return PipeChannelBase::kDefaultTransactionTimeoutMs;
  }
}

DWORD RemainingTransactionTime(ULONGLONG deadline) {
  const ULONGLONG now = GetTickCount64();
  if (now >= deadline)
    return 0;
  return static_cast<DWORD>(deadline - now);
}

bool HasInvalidSessionResponse(PipeChannel<PipeMessage>& channel,
                               UINT64 previous_serial) {
  if (channel.ResponseSerial() == previous_serial)
    return false;

  // The server emits this small exceptional response before librime sees a
  // stale key. Compare only the fixed prefix instead of line-scanning the
  // entire normal response (which TSF parses immediately afterwards anyway).
  static constexpr wchar_t kInvalidSessionPrefix[] =
      L"action=noop\nsession.invalid=1\n";
  static constexpr size_t kPrefixLength =
      sizeof(kInvalidSessionPrefix) / sizeof(kInvalidSessionPrefix[0]) - 1;

  bool found = false;
  channel.HandleResponseData([&](LPWSTR buffer, DWORD length) {
    if (!buffer || length < kPrefixLength)
      return false;
    found = std::char_traits<wchar_t>::compare(
                buffer, kInvalidSessionPrefix, kPrefixLength) == 0;
    return true;
  });
  return found;
}

}  // namespace

ClientImpl::ClientImpl()
    : session_id(0),
      session_key_generator_(GenerateSessionKey),
      is_ime(false),
      channel(GetPipeName(), NULL, 64 * 1024, GetLegacyPipeName()) {
  _InitializeClientInfo();
}

ClientImpl::~ClientImpl() {
  if (channel.Connected())
    Disconnect();
  // Destruction is always a final logical-session boundary, including a
  // failed START whose transport was already torn down.
  _RetireSessionKey();
}

// http://stackoverflow.com/questions/557081/how-do-i-get-the-hmodule-for-the-currently-executed-code
HMODULE GetCurrentModule() {  // NB: XP+ solution!
  HMODULE hModule = NULL;
  GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    (LPCTSTR)GetCurrentModule, &hModule);

  return hModule;
}

void ClientImpl::_InitializeClientInfo() {
  // get app name
  WCHAR exe_path[MAX_PATH] = {0};
  GetModuleFileName(NULL, exe_path, MAX_PATH);
  std::wstring path = exe_path;
  size_t separator_pos = path.find_last_of(L"\\/");
  if (separator_pos < path.size())
    app_name = path.substr(separator_pos + 1);
  else
    app_name = path;
  to_lower(app_name);
  // determine client type
  GetModuleFileName(GetCurrentModule(), exe_path, MAX_PATH);
  path = exe_path;
  to_lower(path);
  is_ime = ends_with(path, L".ime");
}

bool ClientImpl::Connect() {
  return channel.Connect();
}

bool ClientImpl::Connect(DWORD timeout_ms) {
  return channel.Connect(timeout_ms);
}

void ClientImpl::Disconnect() {
  // If a logical session is established, Disconnect remains a full teardown.
  // If transport/session routing is already inactive (the recovery path),
  // close only this thread's pipe and preserve SessionKey so START_SESSION can
  // retry the same logical identity after a lost response.
  if (_Active())
    EndSession();
  channel.Disconnect();
}

void ClientImpl::ShutdownServer() {
  _SendMessage(WEASEL_IPC_SHUTDOWN_SERVER, 0, 0);
}

KeyEventResult ClientImpl::ProcessKeyEventEx(KeyEvent const& keyEvent) {
  const UINT request_session = _SessionId();
  if (!_Connected() || request_session == 0)
    return KeyEventResult::TransportNotDelivered;

  const UINT64 response_serial = channel.ResponseSerial();
  LRESULT ret = 0;
  try {
    PipeMessage req{WEASEL_IPC_PROCESS_KEY_EVENT, keyEvent, request_session};
    ret = channel.Transact(req);
  } catch (const PipeTransactionError& error) {
    _ClearSessionIdIf(request_session);
    return error.request_may_have_been_delivered
               ? KeyEventResult::TransportError
               : KeyEventResult::TransportNotDelivered;
  } catch (...) {
    // Unknown transport failures stay conservative: without delivery-stage
    // evidence, replaying could process one physical key twice.
    _ClearSessionIdIf(request_session);
    return KeyEventResult::TransportError;
  }

  // Another thread may have replaced/ended the logical session after this
  // request was sent. Never apply or replay an old-session result as though it
  // belonged to the newer routing id.
  if (!_Connected() || _SessionId() != request_session)
    return KeyEventResult::TransportError;

  // InvalidSession is emitted only before librime receives the key, making a
  // single caller-controlled retry safe. Retire the key only if this response
  // still names the current session; a newer concurrent session must survive.
  if (HasInvalidSessionResponse(channel, response_serial)) {
    {
      std::lock_guard<std::mutex> lock(session_key_mutex_);
      if (_SessionId() == request_session) {
        _SetSessionId(0);
        _RetireSessionKeyLocked();
      }
    }
    return KeyEventResult::InvalidSession;
  }

  return ret != 0 ? KeyEventResult::Handled : KeyEventResult::Unhandled;
}

bool ClientImpl::ProcessKeyEvent(KeyEvent const& keyEvent) {
  return ProcessKeyEventEx(keyEvent) == KeyEventResult::Handled;
}

bool ClientImpl::CommitComposition() {
  if (!_Active())
    return false;

  LRESULT ret = _SendMessage(WEASEL_IPC_COMMIT_COMPOSITION, 0, _SessionId());
  return ret != 0;
}

bool ClientImpl::ClearComposition() {
  if (!_Active())
    return false;

  LRESULT ret = _SendMessage(WEASEL_IPC_CLEAR_COMPOSITION, 0, _SessionId());
  return ret != 0;
}

bool ClientImpl::SelectCandidateOnCurrentPage(size_t index) {
  if (!_Active())
    return false;
  LRESULT ret = _SendMessage(WEASEL_IPC_SELECT_CANDIDATE_ON_CURRENT_PAGE, index,
                             _SessionId());
  return ret != 0;
}

bool ClientImpl::HighlightCandidateOnCurrentPage(size_t index) {
  if (!_Active())
    return false;
  LRESULT ret = _SendMessage(WEASEL_IPC_HIGHLIGHT_CANDIDATE_ON_CURRENT_PAGE,
                             index, _SessionId());
  return ret != 0;
}

bool ClientImpl::ChangePage(bool backward) {
  if (!_Active())
    return false;
  LRESULT ret = _SendMessage(WEASEL_IPC_CHANGE_PAGE, backward, _SessionId());
  return ret != 0;
}

void ClientImpl::UpdateInputPosition(RECT const& rc) {
  if (!_Active())
    return;
  /*
  移位标志 = 1bit == 0
  height:0~127 = 7bit
  top:-2048~2047 = 12bit（有符号）
  left:-2048~2047 = 12bit（有符号）

  高解析度下：
  移位标志 = 1bit == 1
  height:0~254 = 7bit（舍弃低1位）
  top:-4096~4094 = 12bit（有符号，舍弃低1位）
  left:-4096~4094 = 12bit（有符号，舍弃低1位）
  */
  int hi_res =
      static_cast<int>(rc.bottom - rc.top >= 128 || rc.left < -2048 ||
                       rc.left >= 2048 || rc.top < -2048 || rc.top >= 2048);
  int left = max(-2048, min(2047, rc.left >> hi_res));
  int top = max(-2048, min(2047, rc.top >> hi_res));
  int height = max(0, min(127, (rc.bottom - rc.top) >> hi_res));
  DWORD compressed_rect = ((hi_res & 0x01) << 31) | ((height & 0x7f) << 24) |
                          ((top & 0xfff) << 12) | (left & 0xfff);
  _SendMessage(WEASEL_IPC_UPDATE_INPUT_POS, compressed_rect, _SessionId());
}

void ClientImpl::FocusIn() {
  DWORD client_caps = 0; /* TODO */
  _SendMessage(WEASEL_IPC_FOCUS_IN, client_caps, _SessionId());
}

void ClientImpl::FocusOut() {
  _SendMessage(WEASEL_IPC_FOCUS_OUT, 0, _SessionId());
}

void ClientImpl::TrayCommand(UINT menuId) {
  _SendMessage(WEASEL_IPC_TRAY_COMMAND, menuId, _SessionId());
}

void ClientImpl::StartSession() {
  StartSession(TransactionTimeoutForCommand(WEASEL_IPC_START_SESSION));
}

void ClientImpl::StartSession(DWORD timeout_ms) {
  if (!timeout_ms)
    timeout_ms = TransactionTimeoutForCommand(WEASEL_IPC_START_SESSION);
  const ULONGLONG deadline = GetTickCount64() + timeout_ms;

  if (_Active()) {
    const UINT expected_session = _SessionId();
    const DWORD remaining = RemainingTransactionTime(deadline);
    if (!remaining)
      return;

    const DWORD echo_timeout =
        remaining < PipeChannelBase::kDefaultTransactionTimeoutMs
            ? remaining
            : PipeChannelBase::kDefaultTransactionTimeoutMs;
    const UINT server_echo = static_cast<UINT>(
        _SendMessage(WEASEL_IPC_ECHO, 0, expected_session, echo_timeout));
    if (weasel::interaction_policy::EchoConfirmsSession(
            expected_session, _SessionId(), server_echo)) {
      return;
    }
  }

  std::uint64_t generation = 0;
  if (!_WriteClientInfo(&generation)) {
    const UINT current = _SessionId();
    _ClearSessionIdIf(current);
    return;
  }

  const DWORD remaining = RemainingTransactionTime(deadline);
  if (!remaining) {
    channel.ClearBufferStream();
    return;
  }

  const UINT ret = static_cast<UINT>(
      _SendMessage(WEASEL_IPC_START_SESSION, 0, 0, remaining));
  if (!ret)
    return;

  bool published = false;
  {
    std::lock_guard<std::mutex> lock(session_key_mutex_);
    if (session_key_ && session_generation_ == generation) {
      _SetSessionId(ret);
      published = true;
    }
  }

  if (!published) {
    // EndSession/maintenance retired this logical generation while START was
    // in flight. Do not revive it with a late response; clean up the concrete
    // server session best-effort without touching a newer shared routing id.
    _EndSessionId(ret);
  }
}

void ClientImpl::EndSession() {
  UINT ending = 0;
  {
    std::lock_guard<std::mutex> lock(session_key_mutex_);
    ending = _SessionId();
    _RetireSessionKeyLocked();
    _ClearSessionIdIf(ending);
  }
  _EndSessionId(ending);
}

bool ClientImpl::StartMaintenance() {
  const LRESULT ret = _SendMessage(WEASEL_IPC_START_MAINTENANCE, 0, 0);
  _InvalidateLogicalSession();
  return ret != 0;
}

void ClientImpl::EndMaintenance() {
  _SendMessage(WEASEL_IPC_END_MAINTENANCE, 0, 0);
  _InvalidateLogicalSession();
}

bool ClientImpl::Echo() {
  const UINT expected_session = _SessionId();
  if (!_Connected() || expected_session == 0)
    return false;

  const UINT server_echo =
      _SendMessage(WEASEL_IPC_ECHO, 0, expected_session);
  return weasel::interaction_policy::EchoConfirmsSession(
      expected_session, _SessionId(), server_echo);
}

bool ClientImpl::GetResponseData(ResponseHandler const& handler) {
  if (!handler) {
    return false;
  }

  return channel.HandleResponseData(handler);
}

bool ClientImpl::_EnsureSessionKey() {
  std::lock_guard<std::mutex> lock(session_key_mutex_);
  if (session_key_)
    return true;
  if (!session_key_generator_)
    return false;

  const std::optional<SessionKey> generated = session_key_generator_();
  if (!generated)
    return false;
  session_key_ = *generated;
  return true;
}

void ClientImpl::_RetireSessionKeyLocked() {
  session_key_.reset();
  ++session_generation_;
  if (session_generation_ == 0)
    ++session_generation_;
}

void ClientImpl::_RetireSessionKey() {
  std::lock_guard<std::mutex> lock(session_key_mutex_);
  _RetireSessionKeyLocked();
}

void ClientImpl::_InvalidateLogicalSession() {
  std::lock_guard<std::mutex> lock(session_key_mutex_);
  _SetSessionId(0);
  _RetireSessionKeyLocked();
}

bool ClientImpl::_WriteClientInfo() {
  return _WriteClientInfo(nullptr);
}

bool ClientImpl::_WriteClientInfo(std::uint64_t* generation) {
  std::lock_guard<std::mutex> lock(session_key_mutex_);
  if (!session_key_) {
    if (!session_key_generator_) {
      channel.ClearBufferStream();
      return false;
    }
    const std::optional<SessionKey> generated = session_key_generator_();
    if (!generated) {
      channel.ClearBufferStream();
      return false;
    }
    session_key_ = *generated;
  }

  if (generation)
    *generation = session_generation_;
  const SessionKey key = *session_key_;

  channel << L"action=session\n";
  channel << L"session.client_key=" << FormatSessionKey(key) << L"\n";
  channel << L"session.client_app=" << app_name.c_str() << L"\n";
  channel << L"session.client_type=" << (is_ime ? L"ime" : L"tsf") << L"\n";
  channel << L".\n";
  return true;
}

void ClientImpl::_EndSessionId(UINT id) {
  if (!id)
    return;
  try {
    PipeMessage req{WEASEL_IPC_END_SESSION, 0, id};
    channel.Transact(req, kTeardownTransactionTimeoutMs);
  } catch (...) {
    // END_SESSION is best-effort at this boundary. The logical key is already
    // retired locally and process ownership supplies the final cleanup bound.
  }
}

LRESULT ClientImpl::_SendMessage(WEASEL_IPC_COMMAND Msg,
                                 DWORD wParam,
                                 DWORD lParam,
                                 DWORD timeout_ms) {
  const UINT observed_session = _SessionId();
  try {
    PipeMessage req{Msg, wParam, lParam};
    if (!timeout_ms)
      timeout_ms = TransactionTimeoutForCommand(Msg);
    return channel.Transact(req, timeout_ms);
  } catch (...) {
    // Transport failure invalidates only the routing id that was current when
    // this request began. A concurrent newer START_SESSION must not be erased.
    _ClearSessionIdIf(observed_session);
    return 0;
  }
}

Client::Client() : m_pImpl(new ClientImpl()) {}

Client::~Client() {
  if (m_pImpl)
    delete m_pImpl;
}

bool Client::Connect() {
  return m_pImpl->Connect();
}

bool Client::Connect(DWORD timeout_ms) {
  return m_pImpl->Connect(timeout_ms);
}

void Client::Disconnect() {
  m_pImpl->Disconnect();
}

void Client::ShutdownServer() {
  m_pImpl->ShutdownServer();
}

bool Client::ProcessKeyEvent(KeyEvent const& keyEvent) {
  return m_pImpl->ProcessKeyEvent(keyEvent);
}

KeyEventResult Client::ProcessKeyEventEx(KeyEvent const& keyEvent) {
  return m_pImpl->ProcessKeyEventEx(keyEvent);
}

bool Client::CommitComposition() {
  return m_pImpl->CommitComposition();
}

bool Client::ClearComposition() {
  return m_pImpl->ClearComposition();
}

bool Client::SelectCandidateOnCurrentPage(size_t index) {
  return m_pImpl->SelectCandidateOnCurrentPage(index);
}

bool Client::HighlightCandidateOnCurrentPage(size_t index) {
  return m_pImpl->HighlightCandidateOnCurrentPage(index);
}

bool Client::ChangePage(bool backward) {
  return m_pImpl->ChangePage(backward);
}

void Client::UpdateInputPosition(RECT const& rc) {
  m_pImpl->UpdateInputPosition(rc);
}

void Client::FocusIn() {
  m_pImpl->FocusIn();
}

void Client::FocusOut() {
  m_pImpl->FocusOut();
}

void Client::StartSession() {
  m_pImpl->StartSession();
}

void Client::StartSession(DWORD timeout_ms) {
  m_pImpl->StartSession(timeout_ms);
}

void Client::EndSession() {
  m_pImpl->EndSession();
}

bool Client::StartMaintenance() {
  return m_pImpl->StartMaintenance();
}

void Client::EndMaintenance() {
  m_pImpl->EndMaintenance();
}

void Client::TrayCommand(UINT menuId) {
  m_pImpl->TrayCommand(menuId);
}

bool Client::Echo() {
  return m_pImpl->Echo();
}

bool Client::IsSessionActive() {
  return m_pImpl->IsSessionActive();
}

bool Client::GetResponseData(ResponseHandler handler) {
  return m_pImpl->GetResponseData(handler);
}

UINT64 Client::ResponseSerial() {
  return m_pImpl->ResponseSerial();
}
