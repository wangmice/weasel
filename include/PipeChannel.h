#pragma once
#include <WeaselIPC.h>
#include <string>
#include <memory>
#include <windows.h>
#include <WeaselUtility.h>
#include <boost/interprocess/streams/bufferstream.hpp>
#include <boost/thread.hpp>
#include <boost/thread/tss.hpp>

namespace weasel {

/* A failed transaction is only safe to replay when the request write never
 * completed. Once the write succeeds, a later response failure is ambiguous:
 * the server may already have processed the command. */
class PipeTransactionError {
 public:
  PipeTransactionError(DWORD error_code,
                       bool request_may_have_been_delivered) noexcept
      : error_code(error_code),
        request_may_have_been_delivered(request_may_have_been_delivered) {}

  DWORD error_code;
  bool request_may_have_been_delivered;
};

/* Release a named pipe handle: disconnect the server end (no-op for a
 * client end) and close the kernel handle. Idempotent. */
inline void FinalizePipeHandle(HANDLE& p) {
  if (p != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(p);
    CloseHandle(p);
  }
  p = INVALID_HANDLE_VALUE;
}

/* Owns one thread's pipe handle. boost's default TSS cleanup is a plain
 * delete, so the handle used to live in a raw HANDLE that was never
 * closed; the owner destructor releases it when the owning thread exits
 * without an explicit Disconnect (server-side handles live in local
 * variables and are finalized by their own paths). */
struct PipeHandleOwner {
  PipeHandleOwner() = default;
  ~PipeHandleOwner() { Finalize(); }
  PipeHandleOwner(const PipeHandleOwner&) = delete;
  PipeHandleOwner& operator=(const PipeHandleOwner&) = delete;

  void Finalize() { FinalizePipeHandle(handle); }

  HANDLE handle = INVALID_HANDLE_VALUE;
};

class PipeChannelBase {
 public:
  using Stream = boost::interprocess::wbufferstream;
  static constexpr DWORD kDefaultTransactionTimeoutMs = 1000;

  struct ChannelContext {
    std::unique_ptr<char[]> buffer;
    std::unique_ptr<Stream> write_stream;
    bool has_body;
    // bumped each time a fresh response body is received into buffer;
    // lets callers tell an unparsed response from a stale one (request
    // staging or body-less replies reuse the same buffer)
    UINT64 resp_serial;

    ChannelContext(size_t bs)
        : buffer(std::make_unique<char[]>(bs)),
          has_body(false),
          resp_serial(0) {}
  };

  PipeChannelBase(std::wstring&& pn_cmd,
                  size_t bs,
                  SECURITY_ATTRIBUTES* s,
                  std::wstring fallback_pn = {});
  ~PipeChannelBase();

 protected:
  /* To ensure connection before operation */
  bool _Ensure(ULONGLONG deadline = 0);
  /* Connect pipe as client */
  HANDLE _Connect(const wchar_t* name, ULONGLONG deadline = 0);
  /* Try to connect for one time */
  HANDLE _TryConnect(const wchar_t* name);
  size_t _WritePipe(HANDLE p, size_t s, char* b, ULONGLONG deadline = 0);
  void _Receive(HANDLE pipe,
                LPVOID msg,
                size_t rec_len,
                ULONGLONG deadline = 0);
  /* Try to get a connection from client */
  HANDLE _ConnectServerPipe(std::wstring& pn);
  /* Create a new server-side pipe instance */
  HANDLE _CreateServerPipe(std::wstring& pn);
  /* Wait for a client to connect to an instance */
  HANDLE _AcceptServerPipe(HANDLE pipe);
  inline bool _Invalid(HANDLE p) const { return p == INVALID_HANDLE_VALUE; }

  PipeHandleOwner* _GetPipeHandle() const {
    if (!hpipe_ptr.get()) {
      hpipe_ptr.reset(new PipeHandleOwner());
    }
    return hpipe_ptr.get();
  }

  ChannelContext* _GetContext() const {
    if (!context.get()) {
      context.reset(new ChannelContext(buff_size));
    }
    return context.get();
  }

 protected:
  std::wstring pname;
  // Optional compatibility endpoint. Clients try it only when the scoped
  // primary endpoint cannot be reached; PipeServer may expose it as an alias.
  std::wstring fallback_pname;
  // Thread-local pipe handle; its owner closes it when the thread exits
  mutable boost::thread_specific_ptr<PipeHandleOwner> hpipe_ptr;
  const size_t buff_size;
  // Thread-local context for buffer and state
  mutable boost::thread_specific_ptr<ChannelContext> context;

 private:
  /* Security attributes */
  SECURITY_ATTRIBUTES* sa;
};

/* Pipe based IPC channel */
template <typename _TyMsg,
          typename _TyRes = DWORD,
          size_t _MsgSize = sizeof(_TyMsg),
          size_t _ResSize = sizeof(_TyRes)>
class PipeChannel : public PipeChannelBase {
 public:
  /* Type definitions */

  using Ptr = std::shared_ptr<PipeChannel>;
  using UPtr = std::unique_ptr<PipeChannel>;
  using Msg = _TyMsg;
  using Res = _TyRes;

  enum class ChannalCommand { NEW_MSG_PIPE, REFRESH };

 public:
  PipeChannel(std::wstring&& pn_cmd,
              SECURITY_ATTRIBUTES* s = NULL,
              size_t bs = 64 * 1024,
              std::wstring fallback_pn = {})
      : PipeChannelBase(std::move(pn_cmd), bs, s, std::move(fallback_pn)) {}

 public:
  /* Common pipe operations */

  bool Connect() { return _Ensure(); }
  bool Connect(DWORD timeout_ms) {
    return _Ensure(GetTickCount64() + timeout_ms);
  }
  bool Connected() const { return !_Invalid(_GetPipeHandle()->handle); }
  // serial of the latest response body received on this thread; changes
  // iff the buffer now holds an unread response
  UINT64 ResponseSerial() { return _GetContext()->resp_serial; }
  void Disconnect() { _GetPipeHandle()->Finalize(); }

  /* Write data to buffer */

  template <typename _TyWrite>
  void Write(_TyWrite cnt) {
    _GetContext()->has_body = true;
    _BufferWriteStream() << cnt;
  }

  /* Write data to buffer */
  template <typename _TyWrite>
  PipeChannel& operator<<(_TyWrite cnt) {
    Write(cnt);
    return *this;
  }

  _TyRes Transact(Msg& msg, DWORD timeout_ms = kDefaultTransactionTimeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    PipeHandleOwner* phandle = _GetPipeHandle();
    if (!_Ensure(deadline)) {
      // The request never reached WriteFile, so callers that can safely
      // reconstruct the operation may retry it after recovery.
      const DWORD error = GetLastError();
      ClearBufferStream();
      throw PipeTransactionError(error ? error : ERROR_FILE_NOT_FOUND, false);
    }

    bool request_sent = false;
    try {
      _Send(phandle->handle, msg, deadline);
      request_sent = true;
      return _ReceiveResponse(deadline);
    } catch (const PipeTransactionError&) {
      // A cancelled pending write may have raced delivery. Preserve the
      // transport's conservative classification instead of replaying it.
      ClearBufferStream();
      phandle->Finalize();
      throw;
    } catch (DWORD error) {
      // Preserve whether the request write completed before the failure.
      // A response-stage failure is ambiguous and must never be replayed
      // blindly; pre-submission failures are definitely not delivered.
      ClearBufferStream();
      phandle->Finalize();
      throw PipeTransactionError(error, request_sent);
    } catch (...) {
      // Non-Win32 failures remain conservative: after a successful write the
      // server may have processed the request, so preserve the original error
      // and let callers treat it as ambiguous.
      ClearBufferStream();
      phandle->Finalize();
      throw;
    }
  }

  void ClearBufferStream() {
    auto ctx = _GetContext();
    ctx->has_body = false;
    if (ctx->write_stream != nullptr) {
      ctx->write_stream.reset(nullptr);
    }
  }

  char* SendBuffer() const { return _GetContext()->buffer.get() + _MsgSize; }

  // Request bodies land at buffer[0] via _Receive's ERROR_MORE_DATA path:
  // the fixed-size header is consumed into the caller's variable first.
  char* ReceiveBuffer() const { return _GetContext()->buffer.get(); }

  template <typename _TyHandler>
  bool HandleResponseData(_TyHandler const& handler) {
    // Use whole buffer to receive data in client
    return handler((LPWSTR)_GetContext()->buffer.get(),
                   (UINT)(buff_size * sizeof(char) / sizeof(wchar_t)));
  }

 protected:
  void _Send(HANDLE pipe, Msg& msg, ULONGLONG deadline = 0) {
    auto ctx = _GetContext();
    char* pbuff = ctx->buffer.get();
    DWORD lwritten = 0;

    *reinterpret_cast<Msg*>(pbuff) = msg;
    size_t body_bytes = 0;
    if (ctx->has_body && ctx->write_stream) {
      std::streampos pos = ctx->write_stream->tellp();
      if (pos == std::streampos(-1)) {
        // The staged body overran the fixed send buffer (wbufferstream
        // failbit): sending now would deliver the header without the body,
        // losing it silently. Fail the transaction explicitly and drop the
        // poisoned staging state instead.
        ClearBufferStream();
        LOG(ERROR) << "IPC request body exceeds the send buffer ("
                   << (buff_size - _MsgSize) << " bytes), dropping it";
        throw (DWORD)ERROR_MORE_DATA;
      }
      body_bytes = static_cast<size_t>(pos) * sizeof(wchar_t);
    }
    // body_bytes is bounded by the buffer capacity, so data_sz never
    // exceeds buff_size here
    size_t data_sz = ctx->has_body ? (_MsgSize + body_bytes) : _MsgSize;

    // Pending overlapped write failures carry their own delivery ambiguity.
    _WritePipe(pipe, data_sz, pbuff, deadline);
    ClearBufferStream();
  }

  _TyRes _ReceiveResponse(ULONGLONG deadline) {
    _TyRes result{};
    _Receive(_GetPipeHandle()->handle, &result, sizeof(result), deadline);
    return result;
  }

  Stream& _BufferWriteStream() {
    auto ctx = _GetContext();
    if (ctx->write_stream == nullptr) {
      char* pbuff = (char*)ctx->buffer.get() + _MsgSize;
      memset(pbuff, 0, buff_size - _MsgSize);
      ctx->write_stream =
          std::make_unique<Stream>((wchar_t*)pbuff, _SendBufferSizeW());
    }
    return *ctx->write_stream;
  }

 private:
  inline size_t _SendBufferSizeW() const {
    return (buff_size - _MsgSize) * sizeof(char) / sizeof(wchar_t);
  }

  inline size_t _ReceiveBufferSizeW() const {
    return (buff_size - _ResSize) * sizeof(char) / sizeof(wchar_t);
  }
};
};  // namespace weasel