#include "stdafx.h"

#include <PipeChannel.h>
#include <algorithm>

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

namespace {

DWORD RemainingTime(ULONGLONG deadline) {
  const ULONGLONG now = GetTickCount64();
  if (now >= deadline)
    return 0;
  return static_cast<DWORD>((std::min)(deadline - now,
                                      static_cast<ULONGLONG>(MAXDWORD - 1)));
}

struct PipeIoResult {
  DWORD bytes = 0;
  DWORD error = ERROR_SUCCESS;
};

struct PipeIoOperation {
  OVERLAPPED overlapped{};
  PipeIoOperation() {
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!overlapped.hEvent)
      _ThrowLastError;
  }
  ~PipeIoOperation() { CloseHandle(overlapped.hEvent); }
};

PipeIoResult TransferPipe(HANDLE pipe, void* buffer, DWORD size,
                          bool writing, ULONGLONG deadline) {
  PipeIoResult result;
  if (!deadline) {
    // Server workers keep synchronous I/O; their owner cancels it at shutdown.
    const BOOL ok = writing ? WriteFile(pipe, buffer, size, &result.bytes, NULL)
                            : ReadFile(pipe, buffer, size, &result.bytes, NULL);
    result.error = ok ? ERROR_SUCCESS : GetLastError();
    return result;
  }
  if (!RemainingTime(deadline))
    _ThrowCode(static_cast<DWORD>(ERROR_TIMEOUT));

  PipeIoOperation operation;
  auto* overlapped = &operation.overlapped;
  const BOOL ok = writing ? WriteFile(pipe, buffer, size, nullptr, overlapped)
                          : ReadFile(pipe, buffer, size, nullptr, overlapped);
  if (ok) {
    if (!GetOverlappedResult(pipe, overlapped, &result.bytes, FALSE)) {
      result.error = GetLastError();
      if (writing)
        throw PipeTransactionError(result.error, true);
    }
    return result;
  }

  result.error = GetLastError();
  if (result.error != ERROR_IO_PENDING)
    return result;

  const DWORD waited =
      WaitForSingleObject(overlapped->hEvent, RemainingTime(deadline));
  if (waited != WAIT_OBJECT_0) {
    const DWORD error = waited == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    CancelIoEx(pipe, overlapped);
    // Reap cancellation before the stack OVERLAPPED or caller's buffer goes
    // away, even when completion wins the race against CancelIoEx.
    GetOverlappedResult(pipe, overlapped, &result.bytes, TRUE);
    if (writing)
      throw PipeTransactionError(error, true);
    throw error;
  }
  if (!GetOverlappedResult(pipe, overlapped, &result.bytes, FALSE)) {
    result.error = GetLastError();
    if (writing)
      throw PipeTransactionError(result.error, true);
  } else {
    result.error = ERROR_SUCCESS;
  }
  return result;
}

}  // namespace

PipeChannelBase::PipeChannelBase(std::wstring&& pn_cmd,
                                 size_t bs,
                                 SECURITY_ATTRIBUTES* s,
                                 std::wstring fallback_pn)
    : pname(std::move(pn_cmd)),
      fallback_pname(std::move(fallback_pn)),
      buff_size(bs),
      sa(s) {}

PipeChannelBase::~PipeChannelBase() {
  // Thread-specific pointers are cleaned up automatically
}

bool PipeChannelBase::_Ensure(ULONGLONG deadline) {
  if (!deadline)
    deadline = GetTickCount64() + kDefaultTransactionTimeoutMs;
  PipeHandleOwner* owner = _GetPipeHandle();
  if (!_Invalid(owner->handle))
    return true;

  try {
    owner->handle = _Connect(pname.c_str(), deadline);
    return !_Invalid(owner->handle);
  } catch (DWORD error) {
    SetLastError(error);
    if (fallback_pname.empty() || fallback_pname == pname)
      return false;
  }

  try {
    owner->handle = _Connect(fallback_pname.c_str(), deadline);
    return !_Invalid(owner->handle);
  } catch (DWORD error) {
    SetLastError(error);
    return false;
  }
}

HANDLE PipeChannelBase::_Connect(const wchar_t* name, ULONGLONG deadline) {
  if (!deadline)
    deadline = GetTickCount64() + kDefaultTransactionTimeoutMs;
  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (;;) {
    const DWORD remaining = RemainingTime(deadline);
    if (!remaining)
      _ThrowCode(static_cast<DWORD>(ERROR_TIMEOUT));
    pipe = _TryConnect(name);
    if (!_Invalid(pipe))
      break;
    if (!::WaitNamedPipe(name, remaining)) {
      if (GetLastError() == ERROR_SEM_TIMEOUT)
        _ThrowCode(static_cast<DWORD>(ERROR_TIMEOUT));
      _ThrowLastError;
    }
  }
  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
    const DWORD error = ::GetLastError();
    FinalizePipeHandle(pipe);
    throw error;
  }
  return pipe;
}

HANDLE PipeChannelBase::_TryConnect(const wchar_t* name) {
  auto pipe = ::CreateFile(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
  if (!_Invalid(pipe)) {
    // connected to the pipe
    return pipe;
  }
  // being busy is not really an error since we just need to wait.
  _ThrowIfNot(ERROR_PIPE_BUSY);
  // All pipe instances are busy
  return INVALID_HANDLE_VALUE;
}

size_t PipeChannelBase::_WritePipe(HANDLE pipe, size_t s, char* b,
                                   ULONGLONG deadline) {
  // No FlushFileBuffers here: message-mode WriteFile delivers atomically,
  // while flushing blocks until the peer has read, so a single hung client
  // would freeze every other client while the server holds its api mutex.
  const auto result =
      TransferPipe(pipe, b, static_cast<DWORD>(s), true, deadline);
  if (result.error)
    throw result.error;
  if (result.bytes != s)
    throw PipeTransactionError(ERROR_WRITE_FAULT, result.bytes != 0);
  return result.bytes;
}

void PipeChannelBase::_Receive(HANDLE pipe, LPVOID msg, size_t rec_len,
                               ULONGLONG deadline) {
  auto result = TransferPipe(pipe, msg, static_cast<DWORD>(rec_len), false,
                             deadline);
  if (!result.error && result.bytes != rec_len) {
    // Message-mode pipes can successfully return a complete message that is
    // shorter than the caller's fixed header. Treat it as a malformed frame
    // instead of letting uninitialized header fields reach command dispatch.
    _ThrowCode(static_cast<DWORD>(ERROR_INVALID_DATA));
  }
  if (result.error) {
    if (result.error != ERROR_MORE_DATA)
      throw result.error;

    auto ctx = _GetContext();
    memset(ctx->buffer.get(), 0, buff_size);
    result = TransferPipe(pipe, ctx->buffer.get(), static_cast<DWORD>(buff_size),
                          false, deadline);
    if (result.error)
      throw result.error;
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