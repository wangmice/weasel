#include <windows.h>
#include <atlbase.h>
#include <wtl/atlapp.h>

#include <PipeChannel.h>
#include <WeaselIPC.h>

#include <iostream>
#include <string>

CAppModule _Module;

namespace {
int g_failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::cout << "[FAIL] " << what << std::endl;
  } else {
    std::cout << "[ok] " << what << std::endl;
  }
}

std::wstring unique_pipe_name() {
  return L"\\\\.\\pipe\\WeaselPipeConnectFailure_" +
         std::to_wstring(GetCurrentProcessId());
}

class ExposedClient : public weasel::PipeChannel<weasel::PipeMessage> {
 public:
  using PipeChannel::PipeChannel;
  HANDLE ExposedConnect(const wchar_t* name) { return _Connect(name); }
};
}  // namespace

int main() {
  const std::wstring name = unique_pipe_name();
  HANDLE server = ::CreateNamedPipeW(
      name.c_str(), PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1024, 1024, 0,
      nullptr);
  check(server != INVALID_HANDLE_VALUE, "create byte-mode server pipe");
  if (server == INVALID_HANDLE_VALUE)
    return 1;

  ExposedClient client{std::wstring(name)};
  DWORD before = 0;
  check(::GetProcessHandleCount(::GetCurrentProcess(), &before) != FALSE,
        "capture handle-count baseline");

  bool threw = false;
  try {
    // CreateFile succeeds, but changing a byte-type pipe to message read mode
    // fails. The transient client HANDLE must be closed before _Connect throws.
    client.ExposedConnect(name.c_str());
  } catch (...) {
    threw = true;
  }
  check(threw, "byte-mode pipe rejects message read mode");

  DWORD after = 0;
  check(::GetProcessHandleCount(::GetCurrentProcess(), &after) != FALSE,
        "capture handle count after failed connect");
  check(after <= before,
        "failed SetNamedPipeHandleState does not leak the client pipe handle");

  ::CloseHandle(server);

  std::cout << (g_failures ? "FAILED: " : "PASSED: ") << g_failures
            << " failure(s)" << std::endl;
  return g_failures ? 1 : 0;
}
