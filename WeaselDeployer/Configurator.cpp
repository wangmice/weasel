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
#include <iostream>
#include "WeaselDeployer.h"

namespace {

constexpr int kDictCliUsageError = 2;
constexpr DWORD kInstallLifecycleWaitMs = 30 * 1000;

bool IsDictCommand(const std::wstring& value, const wchar_t* command) {
  return _wcsicmp(value.c_str(), command) == 0;
}

RimeLeversApi* GetLeversApi() {
  RimeModule* levers = rime_get_api()->find_module("levers");
  if (!levers)
    return nullptr;
  return (RimeLeversApi*)levers->get_api();
}

void PrintDictCliUsage() {
  std::wcerr << L"Usage: WeaselDeployer.exe /dict <command> [arguments]\n"
             << L"Commands:\n"
             << L"  list\n"
             << L"  backup <dict-name>\n"
             << L"  restore <snapshot-file>\n"
             << L"  export <dict-name> <text-file>\n"
             << L"  import <dict-name> <text-file>\n";
}

std::filesystem::path UserDataSyncDir(RimeApi* rime) {
  char dir[MAX_PATH] = {0};
  rime->get_user_data_sync_dir(dir, _countof(dir));
  return std::filesystem::path(u8tow(dir));
}

}  // namespace

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
    deploy_result = UpdateWorkspace(reconfigured, installing);
  return (config_failed || deploy_result != 0) ? 1 : 0;
}

int Configurator::UpdateWorkspace(bool report_errors,
                                  bool wait_for_install_startup) {
  RimeOperationWatchdog deploy_watchdog;
  if (!deploy_watchdog.Arm()) {
    LOG(ERROR) << "Could not arm Rime workspace deployment watchdog.";
    return 1;
  }

  weasel::ServerLifecycleLock lifecycle_lock;
  const DWORD lifecycle_wait_ms =
      wait_for_install_startup ? kInstallLifecycleWaitMs : 0;
  if (!lifecycle_lock.Acquire(lifecycle_wait_ms)) {
    LOG(WARNING) << "another server lifecycle operation is running; aborting "
                    "deployment operation after "
                 << lifecycle_wait_ms << " ms.";
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
    // deploy() and deploy_config_file() are synchronous librime RunTask()
    // calls; there is no asynchronous maintenance future to join here.
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

  int result = 0;
  MaintenanceReleaser releaser(client, maintenance_started);
  {
    RimeApi* rime = rime_get_api();
    if (RIME_API_AVAILABLE(rime, run_task) &&
        !rime->run_task("installation_update")) {
      // installation_update rewrites installation.yaml directly. A
      // process-killing watchdog could truncate that file, so this mutating
      // operation must complete or fail naturally until it is isolated behind
      // a recoverable transaction boundary.
      LOG(ERROR) << "Dictionary installation_update task failed.";
      result = 1;
    }
  }

  // Never time the user's interaction with the modal dialog. Only read-only
  // dictionary enumeration inside the dialog uses a hard watchdog.
  if (!result) {
    DictManagementDialog dlg;
    dlg.DoModal();
  }

  if (!releaser.Finish())
    result = 1;
  return result;
}

int Configurator::DictManagement(const std::vector<std::wstring>& args) {
  if (args.empty()) {
    return DictManagement();
  }

  const std::wstring& command = args.front();
  if (IsDictCommand(command, L"help")) {
    PrintDictCliUsage();
    return args.size() == 1 ? 0 : kDictCliUsageError;
  }

  const bool list = IsDictCommand(command, L"list");
  const bool backup = IsDictCommand(command, L"backup");
  const bool restore = IsDictCommand(command, L"restore");
  const bool export_dict = IsDictCommand(command, L"export");
  const bool import_dict = IsDictCommand(command, L"import");

  const size_t expected_args =
      list ? 1 : (backup || restore ? 2 : (export_dict || import_dict ? 3 : 0));
  if (!expected_args || args.size() != expected_args) {
    PrintDictCliUsage();
    return kDictCliUsageError;
  }

  weasel::ServerLifecycleLock lifecycle_lock;
  if (!lifecycle_lock.Acquire(0)) {
    LOG(WARNING) << "another server lifecycle operation is running; aborting "
                    "dictionary operation.";
    return 1;
  }

  weasel::Client client;
  bool maintenance_started = false;
  if (!BeginMaintenanceIfConnected(client, maintenance_started)) {
    return 1;
  }

  MaintenanceReleaser releaser(client, maintenance_started);
  RimeApi* rime = rime_get_api();
  RimeLeversApi* api = GetLeversApi();
  int result = 0;

  if (!api) {
    LOG(ERROR) << "Rime levers module is unavailable.";
    result = 1;
  } else if (RIME_API_AVAILABLE(rime, run_task)) {
    // installation_update writes installation.yaml through a direct
    // std::ofstream in librime. Do not make a timeout corrupt that file.
    rime->run_task("installation_update");
  }

  if (!result && list) {
    std::vector<std::wstring> dictionaries;
    RimeOperationWatchdog list_watchdog;
    if (!list_watchdog.Arm()) {
      LOG(ERROR) << "Could not arm dictionary enumeration watchdog.";
      result = 1;
    } else {
      RimeUserDictIterator iter = {0};
      if (!api->user_dict_iterator_init(&iter)) {
        LOG(ERROR) << "Failed to enumerate user dictionaries.";
        result = 1;
      } else {
        while (const char* dict = api->next_user_dict(&iter)) {
          dictionaries.push_back(u8tow(dict));
        }
        api->user_dict_iterator_destroy(&iter);
      }
      list_watchdog.Disarm();
    }
    if (!result) {
      for (const auto& dict : dictionaries)
        std::wcout << dict << std::endl;
    }
  } else if (!result && backup) {
    // backup_user_dict may recreate userdb metadata. Do not attach the
    // process-killing watchdog to mutating userdb operations: librime writes
    // them incrementally and forced termination can leave a partial mutation.
    const std::string dict_name = wtou8(args[1]);
    if (!api->backup_user_dict(dict_name.c_str())) {
      LOG(ERROR) << "Failed to backup user dictionary: " << dict_name;
      result = 1;
    } else {
      const std::filesystem::path snapshot =
          UserDataSyncDir(rime) / (args[1] + L".userdb.txt");
      std::wcout << snapshot.wstring() << std::endl;
    }
  } else if (!result && restore) {
    // Restore merges records directly into the destination userdb. It must
    // finish or report failure naturally; hard-killing this process mid-merge
    // would turn a timeout into a partially applied restore.
    const std::string snapshot = wtou8(args[1]);
    if (!api->restore_user_dict(snapshot.c_str())) {
      LOG(ERROR) << "Failed to restore user dictionary: " << snapshot;
      result = 1;
    }
  } else if (!result && export_dict) {
    const std::string dict_name = wtou8(args[1]);
    const std::string text_file = wtou8(args[2]);
    // Export writes directly to the caller-selected file. Hard-killing the
    // process can truncate an existing target, so leave this write unbounded
    // until export uses a temporary-file + atomic-replace boundary.
    const int count =
        api->export_user_dict(dict_name.c_str(), text_file.c_str());
    if (count < 0) {
      LOG(ERROR) << "Failed to export user dictionary: " << dict_name;
      result = 1;
    } else {
      std::wcout << count << std::endl;
    }
  } else if (!result && import_dict) {
    // Import performs incremental db_->Update() calls. Keep it outside the
    // process-killing watchdog so timeout handling cannot leave half an import.
    const std::string dict_name = wtou8(args[1]);
    const std::string text_file = wtou8(args[2]);
    const int count =
        api->import_user_dict(dict_name.c_str(), text_file.c_str());
    if (count < 0) {
      LOG(ERROR) << "Failed to import user dictionary: " << dict_name;
      result = 1;
    } else {
      std::wcout << count << std::endl;
    }
  }

  // The synchronous calls above do not require an unrelated maintenance join.
  if (!releaser.Finish())
    result = 1;
  return result;
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
    // sync_user_data() is the one deployer operation here that intentionally
    // starts librime's asynchronous maintenance worker. This method is invoked
    // only by the isolated internal worker, whose process-level watchdog is
    // the hard failure boundary for an unresponsive librime future.
    rime->join_maintenance_thread();
  }
  if (!releaser.Finish())
    retval = 1;
  return retval;
}
