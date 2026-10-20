#include "stdafx.h"
#include "WeaselDeployer.h"
#include "Configurator.h"
#include "SwitcherSettingsDialog.h"
#include "UIStyleSettings.h"
#include "UIStyleSettingsDialog.h"
#include "DictManagementDialog.h"
#include <WeaselConstants.h>
#include <WeaselIPC.h>
#include <WeaselIPCData.h>
#include <WeaselUtility.h>
#pragma warning(disable : 4005)
#include <rime_api.h>
#include <rime_levers_api.h>
#pragma warning(default : 4005)
#include <filesystem>
#include <fstream>
#include "WeaselDeployer.h"

static void CreateFileIfNotExist(std::string filename) {
  std::filesystem::path file_path = WeaselUserDataPath() / u8tow(filename);
  DWORD dwAttrib = GetFileAttributes(file_path.c_str());
  if (!(INVALID_FILE_ATTRIBUTES != dwAttrib &&
        0 == (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))) {
    std::wofstream o(file_path.c_str(), std::ios::app);
    o.close();
  }
}

// Only a caller that received START_MAINTENANCE's acknowledgement owns the
// matching resume. This avoids sending EndMaintenance to a server that was not
// running when deployment began but happened to start while local work ran.
class MaintenanceReleaser {
 public:
  MaintenanceReleaser(weasel::Client& client, bool active)
      : client_(client), active_(active) {}
  MaintenanceReleaser(const MaintenanceReleaser&) = delete;
  MaintenanceReleaser& operator=(const MaintenanceReleaser&) = delete;
  ~MaintenanceReleaser() {
    if (active_)
      Finish();
  }

  bool Finish() {
    if (!active_)
      return true;

    // Consume the local obligation before the IPC attempt. If the request is
    // delivered but its reply is lost, blindly retrying from the destructor
    // would turn a successful resume into a second stale END_MAINTENANCE.
    active_ = false;
    if (!client_.Connect()) {
      LOG(ERROR) << "Could not reconnect to WeaselServer to resume service.";
      return false;
    }

    LOG(INFO) << "Resuming service.";
    if (!client_.EndMaintenance()) {
      LOG(ERROR) << "WeaselServer did not confirm maintenance completion.";
      return false;
    }
    return true;
  }

 private:
  weasel::Client& client_;
  bool active_;
};

static bool NamedMutexPresent(const std::wstring& name) {
  HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
  if (mutex) {
    CloseHandle(mutex);
    return true;
  }
  return GetLastError() == ERROR_ACCESS_DENIED;
}

static bool ServerSingletonPresent() {
  const std::wstring current = weasel::GetServerInstanceName();
  const std::wstring legacy = weasel::GetLegacyServerInstanceName();
  return NamedMutexPresent(current) ||
         (legacy != current && NamedMutexPresent(legacy));
}

// Enter maintenance only when a server is already present. A failed
// acknowledgement is ambiguous because the request may have reached the server
// and only the response may have been lost, so send one best-effort resume
// before reporting failure. Local librime work must not start in that case.
static bool BeginMaintenanceIfConnected(weasel::Client& client,
                                        bool& maintenance_started) {
  maintenance_started = false;
  if (!client.Connect()) {
    // A starting/stopping or wedged server still owns its singleton even when
    // the pipe is temporarily unavailable. Never run local librime maintenance
    // beside that process; only a truly absent server permits local-only work.
    if (ServerSingletonPresent()) {
      LOG(WARNING) << "WeaselServer exists but IPC is unavailable; aborting "
                      "maintenance operation.";
      return false;
    }
    return true;
  }

  LOG(INFO) << "Turning WeaselServer into maintenance mode.";
  if (client.StartMaintenance()) {
    maintenance_started = true;
    return true;
  }

  LOG(ERROR) << "WeaselServer did not acknowledge maintenance mode; aborting "
                "local maintenance work.";
  if (client.Connect()) {
    // The request could have been applied before the reply was lost.
    client.EndMaintenance();
  }
  return false;
}

Configurator::Configurator() {
  CreateFileIfNotExist("default.custom.yaml");
  CreateFileIfNotExist("weasel.custom.yaml");
}

void Configurator::Initialize() {
  RIME_STRUCT(RimeTraits, weasel_traits);
  std::string shared_dir = wtou8(WeaselSharedDataPath().wstring());
  std::string user_dir = wtou8(WeaselUserDataPath().wstring());
  weasel_traits.shared_data_dir = shared_dir.c_str();
  weasel_traits.user_data_dir = user_dir.c_str();
  weasel_traits.prebuilt_data_dir = weasel_traits.shared_data_dir;
  std::string distribution_name = wtou8(get_weasel_ime_name());
  weasel_traits.distribution_name = distribution_name.c_str();
  weasel_traits.distribution_code_name = WEASEL_CODE_NAME;
  weasel_traits.distribution_version = WEASEL_PRODUCT_VERSION;
  weasel_traits.app_name = "rime.weasel";
  std::string log_dir = WeaselLogPath().u8string();
  weasel_traits.log_dir = log_dir.c_str();
  RimeApi* rime_api = rime_get_api();
  assert(rime_api);
  rime_api->setup(&weasel_traits);
  LOG(INFO) << "WeaselDeployer reporting.";
  rime_api->deployer_initialize(NULL);
}

enum class ConfigureResult { Cancelled, Unchanged, Saved, Failed };

static ConfigureResult configure_switcher(
    RimeLeversApi* api,
    RimeSwitcherSettings* switcher_settings) {
  RimeCustomSettings* settings = (RimeCustomSettings*)switcher_settings;
  if (!api->load_settings(settings)) {
    LOG(ERROR) << "Failed to load switcher settings.";
    return ConfigureResult::Failed;
  }
  SwitcherSettingsDialog dialog(switcher_settings);
  if (dialog.DoModal() != IDOK)
    return ConfigureResult::Cancelled;
  // librime's CustomSettings::Save() returns false when nothing was modified;
  // the boolean does not report file-I/O status.
  if (!api->save_settings(settings))
    return ConfigureResult::Unchanged;
  return ConfigureResult::Saved;
}

static ConfigureResult configure_ui(RimeLeversApi* api,
                                    UIStyleSettings* ui_style_settings) {
  RimeCustomSettings* settings = ui_style_settings->settings();
  if (!api->load_settings(settings)) {
    LOG(ERROR) << "Failed to load UI style settings.";
    return ConfigureResult::Failed;
  }
  UIStyleSettingsDialog dialog(ui_style_settings);
  if (dialog.DoModal() != IDOK)
    return ConfigureResult::Cancelled;
  if (!api->save_settings(settings))
    return ConfigureResult::Unchanged;
  return ConfigureResult::Saved;
}

int Configurator::Run(bool installing) {
  RimeModule* levers = rime_get_api()->find_module("levers");
  if (!levers)
    return 1;
  RimeLeversApi* api = (RimeLeversApi*)levers->get_api();
  if (!api)
    return 1;

  bool reconfigured = false;
  bool config_failed = false;

  RimeSwitcherSettings* switcher_settings = api->switcher_settings_init();
  UIStyleSettings ui_style_settings;

  bool skip_switcher_settings =
      installing && !api->is_first_run((RimeCustomSettings*)switcher_settings);
  bool skip_ui_style_settings =
      installing && !api->is_first_run(ui_style_settings.settings());

  auto record_result = [&](ConfigureResult result) {
    if (result == ConfigureResult::Saved)
      reconfigured = true;
    else if (result == ConfigureResult::Failed)
      config_failed = true;
  };

  // 两步独立执行：取消、未修改或失败其中一步都不阻止另一步；成功保存的部分仍需部署。
  if (!skip_switcher_settings)
    record_result(configure_switcher(api, switcher_settings));
  if (!skip_ui_style_settings)
    record_result(configure_ui(api, &ui_style_settings));

  api->custom_settings_destroy((RimeCustomSettings*)switcher_settings);

  int deploy_result = 0;
  if (installing || reconfigured)
    deploy_result = UpdateWorkspace(reconfigured);
  return (config_failed || deploy_result != 0) ? 1 : 0;
}

int Configurator::UpdateWorkspace(bool report_errors) {
  weasel::ServerLifecycleLock lifecycle_lock;
  if (!lifecycle_lock.Acquire(0)) {
    LOG(WARNING) << "another server lifecycle operation is running; aborting "
                    "deployment operation.";
    if (report_errors) {
      // MessageBox(NULL,
      // L"正在執行另一項部署任務，方纔所做的修改將在輸入法再次啓動後生效。",
      // L"【小狼毫】", MB_OK | MB_ICONINFORMATION);
      MSG_BY_IDS(IDS_STR_DEPLOYING_RESTARTREQ, IDS_STR_WEASEL,
                 MB_OK | MB_ICONINFORMATION);
    }
    return 1;
  }

  weasel::Client client;
  bool maintenance_started = false;
  if (!BeginMaintenanceIfConnected(client, maintenance_started)) {
    return 1;
  }

  int retval = 0;
  MaintenanceReleaser releaser(client, maintenance_started);
  {
    RimeApi* rime = rime_get_api();
    // initialize default config, preset schemas
    if (!rime->deploy()) {
      LOG(ERROR) << "Error deploying Rime workspace.";
      retval = 1;
    }
    // initialize weasel config
    if (!rime->deploy_config_file("weasel.yaml", "config_version")) {
      LOG(ERROR) << "Error deploying weasel.yaml.";
      retval = 1;
    }
    // Even on failure, wait for maintenance work before the scoped releaser
    // resumes WeaselServer while lifecycle ownership is still held.
    rime->join_maintenance_thread();
  }
  if (!releaser.Finish())
    retval = 1;
  return retval;
}

int Configurator::DictManagement() {
  weasel::ServerLifecycleLock lifecycle_lock;
  if (!lifecycle_lock.Acquire(0)) {
    LOG(WARNING) << "another server lifecycle operation is running; aborting "
                    "deployment operation.";
    // MessageBox(NULL, L"正在執行另一項部署任務，請稍候再試。", L"【小狼毫】",
    // MB_OK | MB_ICONINFORMATION);
    MSG_BY_IDS(IDS_STR_DEPLOYING_WAIT, IDS_STR_WEASEL,
               MB_OK | MB_ICONINFORMATION);
    return 1;
  }

  weasel::Client client;
  bool maintenance_started = false;
  if (!BeginMaintenanceIfConnected(client, maintenance_started)) {
    return 1;
  }

  MaintenanceReleaser releaser(client, maintenance_started);
  {
    RimeApi* rime = rime_get_api();
    if (RIME_API_AVAILABLE(rime, run_task)) {
      rime->run_task("installation_update");  // setup user data sync dir
    }
    DictManagementDialog dlg;
    dlg.DoModal();
    rime->join_maintenance_thread();
  }
  return releaser.Finish() ? 0 : 1;
}

int Configurator::SyncUserData() {
  weasel::ServerLifecycleLock lifecycle_lock;
  if (!lifecycle_lock.Acquire(0)) {
    LOG(WARNING) << "another server lifecycle operation is running; aborting "
                    "deployment operation.";
    // MessageBox(NULL, L"正在執行另一項部署任務，請稍候再試。", L"【小狼毫】",
    // MB_OK | MB_ICONINFORMATION);
    MSG_BY_IDS(IDS_STR_DEPLOYING_WAIT, IDS_STR_WEASEL,
               MB_OK | MB_ICONINFORMATION);
    return 1;
  }

  weasel::Client client;
  bool maintenance_started = false;
  if (!BeginMaintenanceIfConnected(client, maintenance_started)) {
    return 1;
  }

  int retval = 0;
  MaintenanceReleaser releaser(client, maintenance_started);
  {
    // 异常退出本作用域时析构仍负责兜底；正常路径显式 Finish 并检查 ACK。
    // lifecycle_lock stays owned until that resume completes.
    RimeApi* rime = rime_get_api();
    if (!rime->sync_user_data()) {
      LOG(ERROR) << "Error synching user data.";
      retval = 1;
    }
    // 无论同步成败，等待可能仍在运行的维护任务结束后再唤醒服务；
    // lifecycle ownership remains held through EndMaintenance.
    rime->join_maintenance_thread();
  }
  if (!releaser.Finish())
    retval = 1;
  return retval;
}
