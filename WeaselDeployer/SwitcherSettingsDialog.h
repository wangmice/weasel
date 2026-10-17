#pragma once

#include "resource.h"
#include <CDialogDpiAware.h>
#include <rime_levers_api.h>

class SwitcherSettingsDialog : public CDialogDpiAware<SwitcherSettingsDialog> {
 public:
  enum { IDD = IDD_SWITCHER_SETTING };

  SwitcherSettingsDialog(RimeSwitcherSettings* settings);
  ~SwitcherSettingsDialog();

 protected:
  BEGIN_MSG_MAP(SwitcherSettingsDialog)
  CHAIN_MSG_MAP(CDialogDpiAware<SwitcherSettingsDialog>)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_TIMER, OnTimer)
  COMMAND_HANDLER(IDC_GET_SCHEMATA, BN_CLICKED, OnGetSchemata)
  COMMAND_ID_HANDLER(IDOK, OnOK)
  NOTIFY_HANDLER(IDC_SCHEMA_LIST, LVN_ITEMCHANGED, OnSchemaListItemChanged)
  END_MSG_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnGetSchemata(WORD, WORD, HWND, BOOL&);
  LRESULT OnOK(WORD, WORD, HWND, BOOL&);
  LRESULT OnSchemaListItemChanged(int, LPNMHDR, BOOL&);

  void Populate();
  void ShowDetails(RimeSchemaInfo* info);
  void StopSchemataInstallMonitor();

  RimeLeversApi* api_;
  RimeSwitcherSettings* settings_;
  bool loaded_;
  bool modified_;

  HANDLE schemata_process_ = nullptr;
  UINT_PTR schemata_timer_id_ = 0;
  ULONGLONG schemata_started_at_ = 0;

  CCheckListViewCtrl schema_list_;
  CStatic description_;
  CEdit hotkeys_;
  CButton get_schemata_;
};