#include "stdafx.h"

#include <PipeChannel.h>

using namespace weasel;
using namespace std;
using namespace boost;

#define _ThrowLastError throw ::GetLastError()
#define _ThrowCode(__c) throw __c
#define _ThrowIfNot(__c)                 \
  {                                      \
    DWORD err;                           \
    if ((err = ::GetLastError()) != __c) \
      throw err;                         \
  }

PipeChannelBase::PipeChannelBase(std::wstring&& pn_cmd,
                                 size_t bs = 4 * 1024,
                                 SECURITY_ATTRIBUTES* s = NULL)
    : pname(pn_cmd), buff_size(bs), sa(s) {};

PipeChannelBase::~PipeChannelBase() {
  // Thread-specific pointers are cleaned up automatically
}

bool PipeChannelBase::_Ensure() {
  try {
    PipeHandleOwner* owner = _GetPipeHandle();
    if (_Invalid(owner->handle)) {
      owner->handle = _Connect(pname.c_str());
      return !_Invalid(owner->handle);
    }
  } catch (...) {
    return false;
  }

  return true;
}

HANDLE PipeChannelBase::_Connect(const wchar_t* name) {
  // Bound the wait: a wedged server whose instances stay busy forever must
  // not pin the caller's (input) thread indefinitely.
  static constexpr int kMaxBusyWaits = 6;  // ~3s
  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (int waits = 0; _Invalid(pipe = _TryConnect()); ++waits) {
    // FALSE also covers "pipe does not exist at all": fail fast, the
    // server is gone rather than busy.
    if (!::WaitNamedPipe(name, 500))
      _ThrowLastError;
    if (waits >= kMaxBusyWaits)
      _ThrowCode(ERROR_SEM_TIMEOUT);
  }
  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
    _ThrowLastError;
  }
  return pipe;
}

void PipeChannelBase::_Reconnect() {
  PipeHandleOwner* owner = _GetPipeHandle();
  owner->Finalize();
  _Ensure();
}

HANDLE PipeChannelBase::_TryConnect() {
  auto pipe = ::CreateFile(pname.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, 0, NULL);
  if (!_Invalid(pipe)) {
    // connected to the pipe
    return pipe;
  }
  // being busy is not really an error since we just need to wait.
  _ThrowIfNot(ERROR_PIPE_BUSY);
  // All pipe instances are busy
  return INVALID_HANDLE_VALUE;
}

size_t PipeChannelBase::_WritePipe(HANDLE pipe, size_t s, char* b) {
  DWORD lwritten;
  // No FlushFileBuffers here: message-mode WriteFile delivers atomically,
  // while flushing blocks until the peer has read, so a single hung client
  // would freeze every other client while the server holds its api mutex.
  if (!::WriteFile(pipe, b, s, &lwritten, NULL) || lwritten <= 0) {
    _ThrowLastError;
  }
  return lwritten;
}

void PipeChannelBase::_Receive(HANDLE pipe, LPVOID msg, size_t rec_len) {
  DWORD lread;
  BOOL success = ::ReadFile(pipe, msg, rec_len, &lread, NULL);
  if (!success) {
    _ThrowIfNot(ERROR_MORE_DATA);

    auto ctx = _GetContext();
    memset(ctx->buffer.get(), 0, buff_size);
    success = ::ReadFile(pipe, ctx->buffer.get(), buff_size, &lread, NULL);
    if (!success) {
      _ThrowLastError;
    }
    ctx->resp_serial++;  // a fresh response body now occupies the buffer
  }
  _GetContext()->has_body = false;
}

HANDLE PipeChannelBase::_CreateServerPipe(std::wstring& pn) {
  return ::CreateNamedPipe(
      pn.c_str(), PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
      PIPE_UNLIMITED_INSTANCES, buff_size, buff_size, 0, sa);
}

HANDLE PipeChannelBase::_AcceptServerPipe(HANDLE pipe) {
  // A client that finished CreateFile between instance creation and this
  // call makes ConnectNamedPipe fail with ERROR_PIPE_CONNECTED, but the
  // pipe is then already connected and usable.
  if (!::ConnectNamedPipe(pipe, NULL) &&
      ::GetLastError() != ERROR_PIPE_CONNECTED) {
    _ThrowLastError;
  }
  return pipe;
}

HANDLE PipeChannelBase::_ConnectServerPipe(std::wstring& pn) {
  HANDLE pipe = _CreateServerPipe(pn);
  if (_Invalid(pipe)) {
    _ThrowLastError;
  }
  try {
    return _AcceptServerPipe(pipe);
  } catch (...) {
    FinalizePipeHandle(pipe);
    throw;
  }
}
