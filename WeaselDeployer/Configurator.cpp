#include "stdafx.h"
#include "WeaselDeployer.h"
#include "Configurator.h"
#include "SwitcherSettingsDialog.h"
#include "UIStyleSettings.h"
#include "UIStyleSettingsDialog.h"
#include "DictManagementDialog.h"
#include "DeployerDiagnostics.h"
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
    if (active_ && client_.Connect()) {
      LOG(INFO) << "Resuming service.";
      client_.EndMaintenance();
    }
  }

 private:
  weasel::Client& client_;
  bool active_;
};

// Enter maintenance only when a server is already present. A failed
// acknowledgement is ambiguous because the request may have reached the server
// and only the response may have been lost, so send one best-effort resume
// before reporting failure. Local librime work must not start in that case.
static bool BeginMaintenanceIfConnected(weasel::Client& client,
                                        bool& maintenance_started) {
  maintenance_started = false;
  if (!client.Connect())
    return true;

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
  AppendDeployerDiagnostic(L"Configurator constructed");
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
  AppendDeployerDiagnostic(L"before rime_api->setup");
  rime_api->setup(&weasel_traits);
  AppendDeployerDiagnostic(L"after rime_api->setup");
  LOG(INFO) << "WeaselDeployer reporting.";
  AppendDeployerDiagnostic(L"before deployer_initialize");
  rime_api->deployer_initialize(NULL);
  AppendDeployerDiagnostic(L"after deployer_initialize");
}

enum class ConfigureResult { Cancelled, Saved, Failed };

static ConfigureResult configure_switcher(
    RimeLeversApi* api,
    RimeSwitcherSettings* switcher_settings) {
  AppendDeployerDiagnostic(L"configure_switcher entered");
  AppendDeployerDiagnosticValue(L"configure_switcher switcher_settings_present",
                                switcher_settings ? 1 : 0);
  RimeCustomSettings* settings = (RimeCustomSettings*)switcher_settings;
  AppendDeployerDiagnostic(L"configure_switcher before load_settings");
  const bool loaded = api->load_settings(settings);
  AppendDeployerDiagnosticValue(L"configure_switcher after load_settings result",
                                loaded ? 1 : 0);
  if (!loaded) {
    LOG(ERROR) << "Failed to load switcher settings.";
    return ConfigureResult::Failed;
  }
  AppendDeployerDiagnostic(L"configure_switcher before dialog construction");
  SwitcherSettingsDialog dialog(switcher_settings);
  AppendDeployerDiagnostic(L"configure_switcher after dialog construction");
  AppendDeployerDiagnostic(L"configure_switcher before DoModal");
  const INT_PTR dialog_result = dialog.DoModal();
  AppendDeployerDiagnosticValue(L"configure_switcher after DoModal result",
                                static_cast<int>(dialog_result));
  if (dialog_result != IDOK)
    return ConfigureResult::Cancelled;
  AppendDeployerDiagnostic(L"configure_switcher before save_settings");
  const bool saved = api->save_settings(settings);
  AppendDeployerDiagnosticValue(L"configure_switcher after save_settings result",
                                saved ? 1 : 0);
  if (!saved) {
    LOG(ERROR) << "Failed to save switcher settings.";
    return ConfigureResult::Failed;
  }
  return ConfigureResult::Saved;
}

static ConfigureResult configure_ui(RimeLeversApi* api,
                                    UIStyleSettings* ui_style_settings) {
  AppendDeployerDiagnostic(L"configure_ui entered");
  AppendDeployerDiagnosticValue(L"configure_ui ui_style_settings_present",
                                ui_style_settings ? 1 : 0);
  RimeCustomSettings* settings = ui_style_settings->settings();
  AppendDeployerDiagnosticValue(L"configure_ui custom_settings_present",
                                settings ? 1 : 0);
  AppendDeployerDiagnostic(L"configure_ui before load_settings");
  const bool loaded = api->load_settings(settings);
  AppendDeployerDiagnosticValue(L"configure_ui after load_settings result",
                                loaded ? 1 : 0);
  if (!loaded) {
    LOG(ERROR) << "Failed to load UI style settings.";
    return ConfigureResult::Failed;
  }
  AppendDeployerDiagnostic(L"configure_ui before dialog construction");
  UIStyleSettingsDialog dialog(ui_style_settings);
  AppendDeployerDiagnostic(L"configure_ui after dialog construction");
  AppendDeployerDiagnostic(L"configure_ui before DoModal");
  const INT_PTR dialog_result = dialog.DoModal();
  AppendDeployerDiagnosticValue(L"configure_ui after DoModal result",
                                static_cast<int>(dialog_result));
  if (dialog_result != IDOK)
    return ConfigureResult::Cancelled;
  AppendDeployerDiagnostic(L"configure_ui before save_settings");
  const bool saved = api->save_settings(settings);
  AppendDeployerDiagnosticValue(L"configure_ui after save_settings result",
                                saved ? 1 : 0);
  if (!saved) {
    LOG(ERROR) << "Failed to save UI style settings.";
    return ConfigureResult::Failed;
  }
  return ConfigureResult::Saved;
}

int Configurator::Run(bool installing) {
  AppendDeployerDiagnosticValue(L"Configurator::Run entered installing",
                                installing ? 1 : 0);
  RimeModule* levers = rime_get_api()->find_module("levers");
  if (!levers) {
    AppendDeployerDiagnostic(L"Configurator::Run failed: levers module not found");
    return 1;
  }
  RimeLeversApi* api = (RimeLeversApi*)levers->get_api();
  if (!api) {
    AppendDeployerDiagnostic(L"Configurator::Run failed: levers API unavailable");
    return 1;
  }

  bool reconfigured = false;
  bool config_failed = false;

  RimeSwitcherSettings* switcher_settings = api->switcher_settings_init();
  UIStyleSettings ui_style_settings;

  bool skip_switcher_settings =
      installing && !api->is_first_run((RimeCustomSettings*)switcher_settings);
  bool skip_ui_style_settings =
      installing && !api->is_first_run(ui_style_settings.settings());
  AppendDeployerDiagnosticValue(L"Configurator::Run skip_switcher_settings",
                                skip_switcher_settings ? 1 : 0);
  AppendDeployerDiagnosticValue(L"Configurator::Run skip_ui_style_settings",
                                skip_ui_style_settings ? 1 : 0);

  auto record_result = [&](ConfigureResult result) {
    if (result == ConfigureResult::Saved)
      reconfigured = true;
    else if (result == ConfigureResult::Failed)
      config_failed = true;
  };

  // 两步独立执行：取消或失败其中一步都不阻止另一步；成功保存的部分仍需部署。
  if (!skip_switcher_settings)
    record_result(configure_switcher(api, switcher_settings));
  if (!skip_ui_style_settings)
    record_result(configure_ui(api, &ui_style_settings));

  api->custom_settings_destroy((RimeCustomSettings*)switcher_settings);
  AppendDeployerDiagnosticValue(L"Configurator::Run reconfigured",
                                reconfigured ? 1 : 0);
  AppendDeployerDiagnosticValue(L"Configurator::Run config_failed",
                                config_failed ? 1 : 0);

  int deploy_result = 0;
  if (installing || reconfigured) {
    AppendDeployerDiagnostic(L"before UpdateWorkspace");
    deploy_result = UpdateWorkspace(reconfigured);
    AppendDeployerDiagnosticValue(L"after UpdateWorkspace result",
                                  deploy_result);
  }
  const int final_result = (config_failed || deploy_result != 0) ? 1 : 0;
  AppendDeployerDiagnosticValue(L"Configurator::Run final_result",
                                final_result);
  return final_result;
}

int Configurator::UpdateWorkspace(bool report_errors) {
  AppendDeployerDiagnosticValue(L"UpdateWorkspace entered report_errors",
                                report_errors ? 1 : 0);
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
  if (!hMutex) {
    LOG(ERROR) << "Error creating WeaselDeployerMutex.";
    AppendDeployerDiagnostic(L"UpdateWorkspace failed: create mutex");
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    LOG(WARNING) << "another deployer process is running; aborting operation.";
    AppendDeployerDiagnostic(L"UpdateWorkspace failed: mutex already exists");
    CloseHandle(hMutex);
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
  AppendDeployerDiagnostic(L"before BeginMaintenanceIfConnected");
  if (!BeginMaintenanceIfConnected(client, maintenance_started)) {
    AppendDeployerDiagnostic(L"BeginMaintenanceIfConnected failed");
    CloseHandle(hMutex);
    return 1;
  }
  AppendDeployerDiagnosticValue(L"after BeginMaintenanceIfConnected maintenance_started",
                                maintenance_started ? 1 : 0);

  int retval = 0;
  {
    MaintenanceReleaser releaser(client, maintenance_started);
    RimeApi* rime = rime_get_api();
    // initialize default config, preset schemas
    AppendDeployerDiagnostic(L"before rime->deploy");
    const bool workspace_deploy_ok = rime->deploy();
    AppendDeployerDiagnosticValue(L"after rime->deploy result",
                                  workspace_deploy_ok ? 1 : 0);
    if (!workspace_deploy_ok) {
      LOG(ERROR) << "Error deploying Rime workspace.";
      retval = 1;
    }
    // initialize weasel config
    AppendDeployerDiagnostic(L"before deploy_config_file");
    const bool weasel_config_deploy_ok =
        rime->deploy_config_file("weasel.yaml", "config_version");
    AppendDeployerDiagnosticValue(L"after deploy_config_file result",
                                  weasel_config_deploy_ok ? 1 : 0);
    if (!weasel_config_deploy_ok) {
      LOG(ERROR) << "Error deploying weasel.yaml.";
      retval = 1;
    }
    // Even on failure, wait for any maintenance work spawned by the successful
    // portions above before releasing the mutex and resuming WeaselServer.
    AppendDeployerDiagnostic(L"before join_maintenance_thread");
    rime->join_maintenance_thread();
    AppendDeployerDiagnostic(L"after join_maintenance_thread");
    CloseHandle(hMutex);  // should be closed before resuming service.
  }
  AppendDeployerDiagnosticValue(L"UpdateWorkspace return", retval);
  return retval;
}

int Configurator::DictManagement() {
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
  if (!hMutex) {
    LOG(ERROR) << "Error creating WeaselDeployerMutex.";
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    LOG(WARNING) << "another deployer process is running; aborting operation.";
    CloseHandle(hMutex);
    // MessageBox(NULL, L"正在執行另一項部署任務，請稍候再試。", L"【小狼毫】",
    // MB_OK | MB_ICONINFORMATION);
    MSG_BY_IDS(IDS_STR_DEPLOYING_WAIT, IDS_STR_WEASEL,
               MB_OK | MB_ICONINFORMATION);
    return 1;
  }

  weasel::Client client;
  bool maintenance_started = false;
  if (!BeginMaintenanceIfConnected(client, maintenance_started)) {
    CloseHandle(hMutex);
    return 1;
  }

  {
    MaintenanceReleaser releaser(client, maintenance_started);
    RimeApi* rime = rime_get_api();
    if (RIME_API_AVAILABLE(rime, run_task)) {
      rime->run_task("installation_update");  // setup user data sync dir
    }
    DictManagementDialog dlg;
    dlg.DoModal();
    rime->join_maintenance_thread();
    CloseHandle(hMutex);  // should be closed before resuming service.
  }
  return 0;
}

int Configurator::SyncUserData() {
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
  if (!hMutex) {
    LOG(ERROR) << "Error creating WeaselDeployerMutex.";
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    LOG(WARNING) << "another deployer process is running; aborting operation.";
    CloseHandle(hMutex);
    // MessageBox(NULL, L"正在執行另一項部署任務，請稍候再試。", L"【小狼毫】",
    // MB_OK | MB_ICONINFORMATION);
    MSG_BY_IDS(IDS_STR_DEPLOYING_WAIT, IDS_STR_WEASEL,
               MB_OK | MB_ICONINFORMATION);
    return 1;
  }

  weasel::Client client;
  bool maintenance_started = false;
  if (!BeginMaintenanceIfConnected(client, maintenance_started)) {
    CloseHandle(hMutex);
    return 1;
  }

  int retval = 0;
  {
    // 失败或异常退出本作用域时同样恢复服务端；
    // EndMaintenance 由析构执行，保持在 CloseHandle 之后（先释放部署互斥再唤醒服务）
    MaintenanceReleaser releaser(client, maintenance_started);
    RimeApi* rime = rime_get_api();
    if (!rime->sync_user_data()) {
      LOG(ERROR) << "Error synching user data.";
      retval = 1;
    }
    // 无论同步成败，等待可能仍在运行的维护任务结束后再唤醒服务
    rime->join_maintenance_thread();
    CloseHandle(hMutex);  // should be closed before resuming service.
  }
  return retval;
}
