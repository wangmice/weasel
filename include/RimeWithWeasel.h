#pragma once
#include <WeaselIPC.h>
#include <WeaselUI.h>
#include <map>
#include <string>
#include <mutex>

#include <rime_api.h>

struct CaseInsensitiveCompare {
  bool operator()(const std::string& str1, const std::string& str2) const {
    std::string str1Lower, str2Lower;
    std::transform(str1.begin(), str1.end(), std::back_inserter(str1Lower),
                   [](char c) { return std::tolower(c); });
    std::transform(str2.begin(), str2.end(), std::back_inserter(str2Lower),
                   [](char c) { return std::tolower(c); });
    return str1Lower < str2Lower;
  }
};

typedef std::map<std::string, bool> AppOptions;
typedef std::map<std::string, AppOptions, CaseInsensitiveCompare>
    AppOptionsByAppName;

struct SessionStatus {
  SessionStatus() : style(weasel::UIStyle()), __synced(false), session_id(0) {
    RIME_STRUCT(RimeStatus, status);
  }
  weasel::UIStyle style;
  RimeStatus status;
  bool __synced;
  RimeSessionId session_id;
  // 客户端应用名（小写，_ReadClientInfo 于建会话时解析）：会话期内不变，
  // 供应用选项匹配与托盘刷新使用，避免每键 get_property 交叉查询
  std::string client_app;
};
typedef std::map<DWORD, SessionStatus> SessionStatusMap;
typedef DWORD WeaselSessionId;
class RimeWithWeaselHandler : public weasel::RequestHandler {
 public:
  RimeWithWeaselHandler(weasel::UI* ui);
  virtual ~RimeWithWeaselHandler();
  virtual void Initialize();
  virtual void Finalize();
  virtual DWORD FindSession(WeaselSessionId ipc_id);
  virtual DWORD AddSession(LPWSTR buffer, EatLine eat = 0);
  virtual DWORD RemoveSession(WeaselSessionId ipc_id);
  virtual BOOL ProcessKeyEvent(weasel::KeyEvent keyEvent,
                               WeaselSessionId ipc_id,
                               EatLine eat);
  virtual void CommitComposition(WeaselSessionId ipc_id, EatLine eat);
  virtual void ClearComposition(WeaselSessionId ipc_id);
  virtual void SelectCandidateOnCurrentPage(size_t index,
                                            WeaselSessionId ipc_id,
                                            EatLine eat);
  virtual bool HighlightCandidateOnCurrentPage(size_t index,
                                               WeaselSessionId ipc_id,
                                               EatLine eat);
  virtual bool ChangePage(bool backward, WeaselSessionId ipc_id, EatLine eat);
  virtual void FocusIn(DWORD param, WeaselSessionId ipc_id);
  virtual void FocusOut(DWORD param, WeaselSessionId ipc_id);
  virtual void UpdateInputPosition(RECT const& rc, WeaselSessionId ipc_id);
  virtual void StartMaintenance();
  virtual void EndMaintenance();
  virtual void SetOption(WeaselSessionId ipc_id,
                         const std::string& opt,
                         bool val);
  virtual void UpdateColorTheme(BOOL darkMode);
  virtual bool AddQuickWord(WeaselSessionId ipc_id,
                            const std::wstring& text,
                            const std::wstring& code);

  void OnUpdateUI(std::function<void()> const& cb);

 private:
  void _Setup();
  bool _IsDeployerRunning();

  // 单次请求内 _Respond → _UpdateUI 的流水数据（B11）：
  // - passthrough：直通键标记（未吃键、无上屏、未组词、无状态联动、
  //   无待展示通知），由 ProcessKeyEvent 置位、_Respond 期间按实际情况
  //   清除，用于跳过 get_context 与服务端 UI 全刷新；
  // - status/have_status：_Respond 已取得的会话状态快照，
  //   供 _UpdateUI/_GetStatus 复用，免去第二次 get_status 全量拷贝
  struct RespondContext {
    bool passthrough = true;
    bool have_status = false;
    weasel::Status status;
  };

  void _UpdateUI(WeaselSessionId ipc_id, const RespondContext* rc = nullptr);
  void _LoadSchemaSpecificSettings(WeaselSessionId ipc_id,
                                   const std::string& schema_id);
  void _LoadAppInlinePreeditSet(WeaselSessionId ipc_id,
                                bool ignore_app_name = false);
  bool _ShowMessage(weasel::Context& ctx, weasel::Status& status);
  bool _Respond(WeaselSessionId ipc_id,
                EatLine eat,
                RespondContext* rc = nullptr);
  void _ReadClientInfo(WeaselSessionId ipc_id, LPWSTR buffer);
  void _GetCandidateInfo(weasel::CandidateInfo& cinfo, RimeContext& ctx);
  void _GetStatus(weasel::Status& stat,
                  WeaselSessionId ipc_id,
                  weasel::Context& ctx,
                  const RespondContext* rc = nullptr);
  void _GetContext(weasel::Context& ctx, RimeSessionId session_id);
  void _UpdateShowNotifications(RimeConfig* config, bool initialize = false);

  void _UpdateInlinePreeditStatus(WeaselSessionId ipc_id);

  // 会话表查询：未知/已失效会话一律返回 0（librime 的无效会话号，
  // 相应 API 调用安全落空），绝不向表内插入条目
  RimeSessionId to_session_id(WeaselSessionId ipc_id) const {
    auto it = m_session_status_map.find(ipc_id);
    return it != m_session_status_map.end() ? it->second.session_id : 0;
  }
  // 查找会话状态；不存在返回 nullptr，调用方必须判空。
  // new_session_status（AddSession）是会话表唯一的插入点
  SessionStatus* find_session_status(WeaselSessionId ipc_id) {
    auto it = m_session_status_map.find(ipc_id);
    return it != m_session_status_map.end() ? &it->second : nullptr;
  }
  SessionStatus& new_session_status(WeaselSessionId ipc_id) {
    return m_session_status_map[ipc_id] = SessionStatus();
  }

  AppOptionsByAppName m_app_options;
  weasel::UI* m_ui;  // reference
  DWORD m_active_session;
  bool m_disabled;
  std::string m_last_schema_id;
  std::string m_last_app_name;
  weasel::UIStyle m_base_style;
  std::map<std::string, bool> m_show_notifications;
  std::map<std::string, bool> m_show_notifications_base;
  std::function<void()> _UpdateUICallback;

  static void OnNotify(void* context_object,
                       uintptr_t session_id,
                       const char* message_type,
                       const char* message_value);
  // 消息暂存与互斥为 static：OnNotify 运行于 rime 线程（含部署线程），
  // 不解引用 this，handler 析构后被 librime 回调亦安全；单实例服务进程
  // 内跨实例共享无实际影响
  static std::string m_message_type;
  static std::string m_message_value;
  static std::string m_message_label;
  static std::string m_option_name;
  // 非递归锁，OnNotify 与 _ShowMessage/_UpdateUI/_Respond 共用：
  // 持锁段内不得调用会同步触发 OnNotify 的 rime API。当前持锁段内唯一的
  // rime 调用 get_state_label 为纯配置查询（librime：
  // RimeGetStateLabel → Service::GetSession + Switches::GetStateLabel），
  // 不派发通知，无自锁路径
  static std::mutex m_notifier_mutex;
  SessionStatusMap m_session_status_map;
  bool m_current_dark_mode;
  bool m_global_ascii_mode;
  int m_show_notifications_time;
  DWORD m_pid;
};
