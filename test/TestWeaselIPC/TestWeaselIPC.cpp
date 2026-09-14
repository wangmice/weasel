// TestWeaselIPC.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <WeaselIPC.h>
#include <RimeWithWeasel.h>

#include <boost/interprocess/streams/bufferstream.hpp>
using namespace boost::interprocess;

#include <iostream>
#include <memory>

CAppModule _Module;

int console_main();
int client_main();
int server_main();
int selftest_main();

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::cerr << "[FAIL] " << what << std::endl;
  } else {
    std::cerr << "[ok] " << what << std::endl;
  }
}

// usage: TestWeaselIPC.exe [/start | /stop | /console | /client]

int _tmain(int argc, _TCHAR* argv[]) {
  if (argc == 1)  // no args: run the self test
  {
    return selftest_main();
  } else if (argc > 1 && !wcscmp(L"/start", argv[1])) {
    return server_main();
  } else if (argc > 1 && !wcscmp(L"/stop", argv[1])) {
    weasel::Client client;
    if (!client.Connect()) {
      std::cerr << "server not running." << std::endl;
      return 0;
    }
    client.ShutdownServer();
    return 0;
  } else if (argc > 1 && !wcscmp(L"/console", argv[1])) {
    return console_main();
  } else if (argc > 1 && !wcscmp(L"/client", argv[1])) {
    return client_main();
  }

  return -1;
}

bool launch_server() {
  int ret = (int)ShellExecute(NULL, L"open", L"TestWeaselIPC.exe", L"/start",
                              NULL, SW_NORMAL);
  if (ret <= 32) {
    std::cerr << "failed to launch server." << std::endl;
    return false;
  }
  return true;
}

bool read_buffer(LPWSTR buffer, UINT length, LPWSTR dest) {
  wbufferstream bs(buffer, length);
  bs.read(dest, WEASEL_IPC_BUFFER_LENGTH);
  return bs.good();
}

const char* wcstomb(const wchar_t* wcs) {
  const int buffer_len = 8192;
  static char buffer[buffer_len];
  WideCharToMultiByte(CP_OEMCP, NULL, wcs, -1, buffer, buffer_len, NULL, FALSE);
  return buffer;
}

int console_main() {
  weasel::Client client;
  if (!client.Connect()) {
    std::cerr << "failed to connect to server." << std::endl;
    return -2;
  }
  client.StartSession();
  if (!client.Echo()) {
    std::cerr << "failed to start session." << std::endl;
    return -3;
  }

  while (std::cin.good()) {
    int ch = std::cin.get();
    if (!std::cin.good())
      break;
    bool eaten = client.ProcessKeyEvent(weasel::KeyEvent(ch, 0));
    std::cout << "server replies: " << eaten << std::endl;
    if (eaten) {
      WCHAR response[WEASEL_IPC_BUFFER_LENGTH];
      bool ret = client.GetResponseData(
          std::bind<bool>(read_buffer, std::placeholders::_1,
                          std::placeholders::_2, std::ref(response)));
      std::cout << "get response data: " << ret << std::endl;
      std::cout << "buffer reads: " << std::endl
                << wcstomb(response) << std::endl;
    }
  }

  client.EndSession();

  return 0;
}

int client_main() {
  // launch_server();
  Sleep(1000);
  weasel::Client client;
  if (!client.Connect()) {
    std::cerr << "failed to connect to server." << std::endl;
    return -2;
  }
  client.StartSession();
  if (!client.Echo()) {
    std::cerr << "failed to login." << std::endl;
    return -3;
  }
  bool eaten = client.ProcessKeyEvent(weasel::KeyEvent(L'a', 0));
  std::cout << "server replies: " << eaten << std::endl;
  if (eaten) {
    WCHAR response[WEASEL_IPC_BUFFER_LENGTH];
    bool ret = client.GetResponseData(
        std::bind<bool>(read_buffer, std::placeholders::_1,
                        std::placeholders::_2, std::ref(response)));
    std::cout << "get response data: " << ret << std::endl;
    std::cout << "buffer reads: " << std::endl
              << wcstomb(response) << std::endl;
  }
  client.EndSession();

  system("pause");
  return 0;
}

class TestRequestHandler : public weasel::RequestHandler {
 public:
  TestRequestHandler() : m_counter(0) {
    std::cerr << "handler ctor." << std::endl;
  }
  virtual ~TestRequestHandler() {
    std::cerr << "handler dtor: " << m_counter << std::endl;
  }
  // 签名须与基类逐字一致（DWORD / EatLine 形参）：K20 之前写作
  // UINT AddSession(LPWSTR)，隐藏而非重写，ServerImpl::OnStartSession
  // 带 eat 调用命中基类空实现，会话计数 m_counter 永不增长
  DWORD FindSession(DWORD session_id) override {
    std::cerr << "FindSession: " << session_id << std::endl;
    return (session_id <= m_counter ? session_id : 0);
  }
  DWORD AddSession(LPWSTR buffer, EatLine eat) override {
    std::cerr << "AddSession: " << m_counter + 1 << std::endl;
    ++m_counter;
    if (eat)
      eat(std::wstring(L"status.composing=0\n"));
    return m_counter;
  }
  DWORD RemoveSession(DWORD session_id) override {
    std::cerr << "RemoveClient: " << session_id << std::endl;
    return 0;
  }
  BOOL ProcessKeyEvent(weasel::KeyEvent keyEvent,
                       DWORD session_id,
                       EatLine eat) override {
    std::cerr << "ProcessKeyEvent: " << session_id
              << " keycode: " << keyEvent.keycode << " mask: " << keyEvent.mask
              << std::endl;
    eat(std::wstring(L"Greeting=Hello, 小狼毫.\n"));
    return TRUE;
  }

 private:
  unsigned int m_counter;
};

// K20: dispatch through the RequestHandler base pointer, exactly as
// ServerImpl::OnStartSession/OnKeyEvent call it. The old handler hid
// AddSession behind a mismatched signature, so the base no-op ran and the
// session counter never grew; these assertions keep that regression out.
int selftest_main() {
  std::unique_ptr<weasel::RequestHandler> handler(new TestRequestHandler);
  WCHAR buffer[WEASEL_IPC_BUFFER_LENGTH] =
      L"action=session\nsession.client_app=selftest.exe\n.\n";

  std::wstring eaten;
  weasel::RequestHandler::EatLine eat = [&eaten](std::wstring& line) -> bool {
    eaten += line;
    return true;
  };

  DWORD id1 = handler->AddSession(buffer, eat);
  check(id1 == 1, "K20: first AddSession returns session 1");
  DWORD id2 = handler->AddSession(buffer, eat);
  check(id2 == 2, "K20: second AddSession returns session 2");
  check(eaten.find(L"status.composing=0\n") != std::wstring::npos,
        "K20: AddSession drives the eat callback");

  check(handler->FindSession(1) == 1, "K20: FindSession(1)");
  check(handler->FindSession(3) == 0, "K20: FindSession(unknown) is 0");

  BOOL handled =
      handler->ProcessKeyEvent(weasel::KeyEvent(L'a', 0), 1, eat);
  check(handled == TRUE, "K20: ProcessKeyEvent handled");
  check(eaten.find(L"Greeting=Hello") != std::wstring::npos,
        "K20: ProcessKeyEvent drives the eat callback");

  return g_failures ? 1 : 0;
}

int server_main() {
  HRESULT hRes = _Module.Init(NULL, GetModuleHandle(NULL));
  ATLASSERT(SUCCEEDED(hRes));

  weasel::Server server;
  // weasel::UI ui;
  // const std::unique_ptr<weasel::RequestHandler> handler(new
  // RimeWithWeaselHandler(&ui));
  const std::unique_ptr<weasel::RequestHandler> handler(new TestRequestHandler);

  server.SetRequestHandler(handler.get());
  if (!server.Start())
    return -4;
  std::cerr << "server running." << std::endl;
  int ret = server.Run();
  std::cerr << "server quitting." << std::endl;
  return ret;
}
