; weasel installation script
!include FileFunc.nsh
!include LogicLib.nsh
!include MUI2.nsh
!include x64.nsh
!include winVer.nsh

; ARM64 setup needs the native DLL, the ARM64X redirector, and the ARM32 DLL
; for Windows versions that still support ARM32 WOW. Partial payloads must
; never reach Setup or the upgrade's old-version removal path.
!if /FileExists "weaselARM.dll"
  !if /FileExists "weaselARM64.dll"
    !if /FileExists "weaselARM64X.dll"
      !define WEASEL_HAS_ARM64_PAYLOAD
    !endif
  !endif
!endif
!ifdef WEASEL_HAS_ARM64_PAYLOAD
  !echo "Installer targets: x86, x64, ARM64"
!else
  !echo "Installer targets: x86, x64 only (ARM64 payload incomplete)"
!endif

Unicode true

;--------------------------------
; General

!ifndef WEASEL_VERSION
!define WEASEL_VERSION 0.1.0
!endif

!ifndef WEASEL_BUILD
!define WEASEL_BUILD 0
!endif

!define WEASEL_ROOT $INSTDIR\weasel-${WEASEL_VERSION}
!define REG_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Weasel"
Var InstallBaseDir
Var NewInstallDir
Var PreviousBaseDir
Var PreviousInstallDir
Var PreviousProgramBackupDir
Var HadPreviousInstall
Var UpgradeSlotId
Var RollbackFailed
Var ProgramRestoreFailed

; The name of the installer
Name "小狼毫 ${WEASEL_VERSION}"

; The file to write
OutFile "archives\weasel-${PRODUCT_VERSION}-installer.exe"

VIProductVersion "${WEASEL_VERSION}.${WEASEL_BUILD}"
VIAddVersionKey /LANG=2052 "ProductName" "小狼毫"
VIAddVersionKey /LANG=2052 "Comments" "Powered by RIME | 中州韻輸入法引擎"
VIAddVersionKey /LANG=2052 "CompanyName" "式恕堂"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Copyleft RIME Developers"
VIAddVersionKey /LANG=2052 "FileDescription" "小狼毫輸入法"
VIAddVersionKey /LANG=2052 "FileVersion" "${WEASEL_VERSION}"

!define MUI_ICON ..\resource\weasel.ico
SetCompressor /SOLID lzma


; Request application privileges for Windows Vista
RequestExecutionLevel admin

;--------------------------------

; Pages

!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

;--------------------------------

; Languages

!insertmacro MUI_LANGUAGE "TradChinese"
LangString DISPLAYNAME ${LANG_TRADCHINESE} "小狼毫輸入法"
LangString LNKFORMANUAL ${LANG_TRADCHINESE} "【小狼毫】說明書"
LangString LNKFORSETTING ${LANG_TRADCHINESE} "【小狼毫】輸入法設定"
LangString LNKFORDICT ${LANG_TRADCHINESE} "【小狼毫】用戶詞典管理"
LangString LNKFORSYNC ${LANG_TRADCHINESE} "【小狼毫】用戶資料同步"
LangString LNKFORDEPLOY ${LANG_TRADCHINESE} "【小狼毫】重新部署"
LangString LNKFORSERVER ${LANG_TRADCHINESE} "小狼毫算法服務"
LangString LNKFORUSERFOLDER ${LANG_TRADCHINESE} "【小狼毫】用戶文件夾"
LangString LNKFORAPPFOLDER ${LANG_TRADCHINESE} "【小狼毫】程序文件夾"
LangString LNKFORUPDATER ${LANG_TRADCHINESE} "【小狼毫】檢查新版本"
LangString LNKFORSETUP ${LANG_TRADCHINESE} "【小狼毫】安裝選項"
LangString LNKFORUNINSTALL ${LANG_TRADCHINESE} "卸載小狼毫"
LangString CONFIRMATION ${LANG_TRADCHINESE} "安裝前，請先卸載舊版本的小狼毫。$\n$\n按下「確定」移除舊版本，按下「取消」放棄本次安裝。"
LangString SYSTEMVERSIONNOTOK ${LANG_TRADCHINESE} "您的系统不被支持，最低系統要求:Windows 8.1!"
LangString AUTOCHKUPDATE ${LANG_TRADCHINESE} "自動檢查版本更新？"
LangString SETUPCANCELLED ${LANG_TRADCHINESE} "已取消安裝，不會繼續部署或啟動服務。"
LangString SETUPFAILED ${LANG_TRADCHINESE} "輸入法安裝失敗，不會繼續部署或啟動服務。"
LangString FILESINUSE ${LANG_TRADCHINESE} "無法替換正在使用中的小狼毫程式檔案。請關閉所有使用輸入法的程式後重試；若仍失敗，請登出 Windows 後重新登入再安裝。舊版本已嘗試恢復。"
LangString ROLLBACKFAILED ${LANG_TRADCHINESE} "安裝回滾未能完整完成。請先登出或重新啟動 Windows，再重新執行安裝程式。暫存備份會保留以供恢復。"
LangString ARM64NOTSUPPORTED ${LANG_TRADCHINESE} "此安裝包僅支援 x86/x64 Windows。請使用包含完整 ARM64 元件的小狼毫安裝包；現有安裝尚未變更。"

!insertmacro MUI_LANGUAGE "SimpChinese"
LangString DISPLAYNAME ${LANG_SIMPCHINESE} "小狼毫输入法"
LangString LNKFORMANUAL ${LANG_SIMPCHINESE} "【小狼毫】说明书"
LangString LNKFORSETTING ${LANG_SIMPCHINESE} "【小狼毫】输入法设定"
LangString LNKFORDICT ${LANG_SIMPCHINESE} "【小狼毫】用户词典管理"
LangString LNKFORSYNC ${LANG_SIMPCHINESE} "【小狼毫】用户资料同步"
LangString LNKFORDEPLOY ${LANG_SIMPCHINESE} "【小狼毫】重新部署"
LangString LNKFORSERVER ${LANG_SIMPCHINESE} "小狼毫算法服务"
LangString LNKFORUSERFOLDER ${LANG_SIMPCHINESE} "【小狼毫】用户文件夹"
LangString LNKFORAPPFOLDER ${LANG_SIMPCHINESE} "【小狼毫】程序文件夹"
LangString LNKFORUPDATER ${LANG_SIMPCHINESE} "【小狼毫】检查新版本"
LangString LNKFORSETUP ${LANG_SIMPCHINESE} "【小狼毫】安装选项"
LangString LNKFORUNINSTALL ${LANG_SIMPCHINESE} "卸载小狼毫"
LangString CONFIRMATION ${LANG_SIMPCHINESE} '安装前，请先卸载旧版本的小狼毫。$\n$\n点击 "确定" 移除旧版本，或点击 "取消" 放弃本次安装。'
LangString SYSTEMVERSIONNOTOK ${LANG_SIMPCHINESE} "您的系統不被支持，最低系统要求:Windows 8.1!"
LangString AUTOCHKUPDATE ${LANG_SIMPCHINESE} "自动检查版本更新？"
LangString SETUPCANCELLED ${LANG_SIMPCHINESE} "已取消安装，不会继续部署或启动服务。"
LangString SETUPFAILED ${LANG_SIMPCHINESE} "输入法安装失败，不会继续部署或启动服务。"
LangString FILESINUSE ${LANG_SIMPCHINESE} "无法替换正在使用中的小狼毫程序文件。请关闭所有正在使用输入法的程序后重试；如果仍然失败，请注销 Windows 后重新登录再安装。旧版本已尝试恢复。"
LangString ROLLBACKFAILED ${LANG_SIMPCHINESE} "安装回滚未能完整完成。请先注销或重新启动 Windows，再重新运行安装程序。临时备份会保留以供恢复。"
LangString ARM64NOTSUPPORTED ${LANG_SIMPCHINESE} "此安装包仅支持 x86/x64 Windows。请使用包含完整 ARM64 组件的小狼毫安装包；现有安装尚未更改。"

!insertmacro MUI_LANGUAGE "English"
LangString DISPLAYNAME ${LANG_ENGLISH} "Weasel"
LangString LNKFORMANUAL ${LANG_ENGLISH} "Weasel Manual"
LangString LNKFORSETTING ${LANG_ENGLISH} "Weasel Settings"
LangString LNKFORDICT ${LANG_ENGLISH} "Weasel Dictionary Manager"
LangString LNKFORSYNC ${LANG_ENGLISH} "Weasel Sync User Profile"
LangString LNKFORDEPLOY ${LANG_ENGLISH} "Weasel Deploy"
LangString LNKFORSERVER ${LANG_ENGLISH} "Weasel Server"
LangString LNKFORUSERFOLDER ${LANG_ENGLISH} "Weasel User Folder"
LangString LNKFORAPPFOLDER ${LANG_ENGLISH} "Weasel App Folder"
LangString LNKFORUPDATER ${LANG_ENGLISH} "Weasel Check for Updates"
LangString LNKFORSETUP ${LANG_ENGLISH} "Weasel Installation Preference"
LangString LNKFORUNINSTALL ${LANG_ENGLISH} "Uninstall Weasel"
LangString CONFIRMATION ${LANG_ENGLISH} "Before installation, please uninstall the old version of Weasel.$\n$\nPress 'OK' to remove the old version, or 'Cancel' to abort installation."
LangString SYSTEMVERSIONNOTOK ${LANG_ENGLISH} "Your system not supported, minimium system required: Windows 8.1!"
LangString AUTOCHKUPDATE ${LANG_ENGLISH} "Automatically check for updates?"
LangString SETUPCANCELLED ${LANG_ENGLISH} "Installation cancelled. Deployment and service startup will not continue."
LangString SETUPFAILED ${LANG_ENGLISH} "Input method setup failed. Deployment and service startup will not continue."
LangString FILESINUSE ${LANG_ENGLISH} "Weasel program files are still in use and cannot be replaced. Close applications using the input method and retry. If the files remain locked, sign out of Windows and sign back in before installing again. The previous version has been restored where possible."
LangString ROLLBACKFAILED ${LANG_ENGLISH} "Installation rollback could not complete. Sign out or restart Windows, then run the installer again. The temporary backup has been retained for recovery."
LangString ARM64NOTSUPPORTED ${LANG_ENGLISH} "This package supports x86/x64 Windows only. Please use a Weasel installer with the complete ARM64 components. Your existing installation has not been changed."

; .onInit runs before the normal installer UI exists. Abort's optional string is
; only written to the status display, so interactive fatal preflight failures
; must show their own MessageBox before quitting.
!macro AbortInitFailure
  IfSilent +2
  MessageBox MB_OK|MB_ICONSTOP "$(SETUPFAILED)"
  Abort
!macroend

;--------------------------------
; Restore the complete previous program tree from the transaction backup.
; Robocopy skips identical files that survived a failed delete, which lets the
; rollback refill a partially removed same-version directory without trying to
; rewrite an unchanged DLL that is still mapped by another process.
Function RestorePreviousProgramBackup
  StrCpy $ProgramRestoreFailed "0"
  StrCmp $PreviousProgramBackupDir "" restore_program_failed
  IfFileExists "$PreviousProgramBackupDir\WeaselSetup.exe" 0 restore_program_failed
  CreateDirectory "$PreviousInstallDir"
  ClearErrors
  ExecWait '"$SYSDIR\robocopy.exe" "$PreviousProgramBackupDir" "$PreviousInstallDir" /E /COPY:DAT /DCOPY:T /R:0 /W:0 /XJ /NFL /NDL /NJH /NJS /NP' $R2
  ${If} ${Errors}
    GoTo restore_program_failed
  ${EndIf}
  IntCmp $R2 8 restore_program_failed restore_program_ok restore_program_failed
restore_program_ok:
  IfFileExists "$PreviousInstallDir\WeaselSetup.exe" 0 restore_program_failed
  IfFileExists "$PreviousInstallDir\WeaselServer.exe" 0 restore_program_failed
  Return
restore_program_failed:
  StrCpy $ProgramRestoreFailed "1"
  StrCpy $RollbackFailed "1"
FunctionEnd

;--------------------------------
; Restore a previously working installation after an in-place replacement
; fails. Rollback never schedules version directories for reboot deletion,
; because a successful retry before reboot could otherwise be deleted later.
Function RollbackPreviousInstall
  StrCpy $RollbackFailed "0"
  StrCpy $ProgramRestoreFailed "0"
  StrCmp $HadPreviousInstall "1" 0 rollback_fresh_only

  ; Best effort: remove any partial registration produced by the new Setup.
  IfFileExists "$INSTDIR\WeaselSetup.exe" 0 rollback_remove_new_files
  ExecWait '"$INSTDIR\WeaselSetup.exe" /u'

rollback_remove_new_files:
  SetOutPath $TEMP
  StrCmp $INSTDIR $PreviousInstallDir rollback_remove_same_path rollback_remove_other_path
rollback_remove_same_path:
  ; A locked file that survived the failed same-path replacement can still be
  ; the exact old file. Do not fail rollback merely because cleanup cannot
  ; remove it; robocopy below will skip identical survivors and will itself
  ; report failure if a differing locked file prevents restoration.
  ClearErrors
  RMDir /r "$INSTDIR"
  ClearErrors
  GoTo rollback_restore_program
rollback_remove_other_path:
  ClearErrors
  RMDir /r "$INSTDIR"
  ${If} ${Errors}
    StrCpy $RollbackFailed "1"
  ${EndIf}

rollback_restore_program:
  Call RestorePreviousProgramBackup

  ; HKCU preferences are independent of program-tree usability and can still be
  ; restored even when a locked/damaged file prevents a runnable rollback.
  IfFileExists "$TEMP\weasel-hkcu-backup.reg" 0 rollback_after_config
  ExecWait 'reg import "$TEMP\weasel-hkcu-backup.reg"' $R2
  ${If} $R2 != 0
    StrCpy $RollbackFailed "1"
  ${EndIf}

rollback_after_config:
  ; Never register or launch a partially restored program tree. Keep its backup
  ; for manual/retry recovery and restore only the non-runnable base metadata.
  StrCmp $ProgramRestoreFailed "1" rollback_restore_base_registry

  IfFileExists "$PreviousInstallDir\WeaselSetup.exe" 0 rollback_registration_failed
  ExecWait '"$PreviousInstallDir\WeaselSetup.exe" /i' $R2
  ${If} $R2 != 0
    StrCpy $RollbackFailed "1"
    GoTo rollback_restore_base_registry
  ${EndIf}
  GoTo rollback_restore_runtime_registry
rollback_registration_failed:
  StrCpy $RollbackFailed "1"
  GoTo rollback_restore_base_registry

rollback_restore_base_registry:
  StrCmp $PreviousBaseDir "" rollback_cleanup_temp 0
  WriteRegStr HKLM SOFTWARE\Rime\Weasel "InstallDir" "$PreviousBaseDir"
  GoTo rollback_cleanup_temp

rollback_restore_runtime_registry:
  StrCmp $PreviousBaseDir "" +2 0
  WriteRegStr HKLM SOFTWARE\Rime\Weasel "InstallDir" "$PreviousBaseDir"
  ${If} ${IsNativeARM64}
    SetRegView 64
  ${ElseIf} ${IsNativeAMD64}
    SetRegView 64
  ${Endif}
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "WeaselServer" "$PreviousInstallDir\WeaselServer.exe"
  SetRegView 32
  IfFileExists "$PreviousInstallDir\WeaselServer.exe" 0 rollback_cleanup_temp
  Exec '"$PreviousInstallDir\WeaselServer.exe"'

rollback_cleanup_temp:
  StrCmp $RollbackFailed "1" rollback_keep_temp
  Delete "$TEMP\weasel-hkcu-backup.reg"
  RMDir /r "$PreviousProgramBackupDir"
rollback_keep_temp:
  Return

rollback_fresh_only:
  ; A fresh Setup has its own registration transaction. Remove only staged
  ; program files here; do not run /u, which would erase pre-existing HKCU
  ; preferences on a machine that did not have Weasel installed.
  SetOutPath $TEMP
  ClearErrors
  RMDir /r "$INSTDIR"
  ${If} ${Errors}
    StrCpy $RollbackFailed "1"
  ${EndIf}
FunctionEnd

;--------------------------------
; Begin the destructive part of an upgrade only after the user has reached the
; install section. Back up the whole old program directory first, then stop and
; unregister the old version. Same-version upgrades remove the old canonical
; directory before extraction so a locked file fails closed instead of being
; silently skipped.
Function PreparePreviousInstall
  StrCpy $R9 "0"
  StrCmp $HadPreviousInstall "1" 0 prepare_done

  ; Back up per-user configuration before old /u clears it. A valid install can
  ; have no HKCU state yet, in which case there is simply nothing to export.
  Delete "$TEMP\weasel-hkcu-backup.reg"
  StrCpy $R4 ""
  EnumRegValue $R4 HKCU "Software\Rime\Weasel" 0
  StrCmp $R4 "" prepare_check_user_config_subkey prepare_export_user_config
prepare_check_user_config_subkey:
  StrCpy $R4 ""
  EnumRegKey $R4 HKCU "Software\Rime\Weasel" 0
  StrCmp $R4 "" prepare_backup_program prepare_export_user_config
prepare_export_user_config:
  ClearErrors
  ExecWait 'reg export HKCU\Software\Rime\Weasel "$TEMP\weasel-hkcu-backup.reg" /y' $R2
  ${If} ${Errors}
    GoTo prepare_failed_before_mutation
  ${EndIf}
  ${If} $R2 != 0
    GoTo prepare_failed_before_mutation
  ${EndIf}
  IfFileExists "$TEMP\weasel-hkcu-backup.reg" prepare_backup_program prepare_failed_before_mutation

prepare_backup_program:
  StrCpy $PreviousProgramBackupDir "$TEMP\weasel-program-backup-$UpgradeSlotId"
  RMDir /r "$PreviousProgramBackupDir"
  CreateDirectory "$PreviousProgramBackupDir"
  ClearErrors
  ExecWait '"$SYSDIR\robocopy.exe" "$PreviousInstallDir" "$PreviousProgramBackupDir" /E /COPY:DAT /DCOPY:T /R:0 /W:0 /XJ /NFL /NDL /NJH /NJS /NP' $R2
  ${If} ${Errors}
    GoTo prepare_failed_before_mutation
  ${EndIf}
  IntCmp $R2 8 prepare_failed_before_mutation prepare_backup_verified prepare_failed_before_mutation
prepare_backup_verified:
  IfFileExists "$PreviousProgramBackupDir\WeaselSetup.exe" 0 prepare_failed_before_mutation
  IfFileExists "$PreviousProgramBackupDir\WeaselServer.exe" 0 prepare_failed_before_mutation

  ; Stop our own service first. Other applications can still hold TSF DLLs; the
  ; same-path directory removal below detects that condition without killing
  ; unrelated user processes.
  ExecWait '"$PreviousInstallDir\WeaselServer.exe" /quit'
  ExecWait '"$PreviousInstallDir\WeaselSetup.exe" /u' $R2
  ${If} $R2 != 0
    DetailPrint "Previous WeaselSetup /u exited with $R2; continuing transactional replacement."
  ${EndIf}

  ; Restore preferences for the replacement Setup. Keep the export until commit
  ; so rollback can import the exact previous state again.
  IfFileExists "$TEMP\weasel-hkcu-backup.reg" 0 prepare_remove_same_path
  ExecWait 'reg import "$TEMP\weasel-hkcu-backup.reg"' $R2
  ${If} $R2 != 0
    Call RollbackPreviousInstall
    StrCpy $R9 "1"
    Return
  ${EndIf}

prepare_remove_same_path:
  StrCmp $PreviousInstallDir $INSTDIR 0 prepare_done
  SetOutPath $TEMP
  ClearErrors
  RMDir /r "$INSTDIR"
  ${If} ${Errors}
    GoTo prepare_locked
  ${EndIf}
  IfFileExists "$INSTDIR\*.*" prepare_locked 0
  GoTo prepare_done

prepare_locked:
  ; No replacement files have been written yet. Restore anything that the
  ; failed recursive delete managed to remove and re-register the old version.
  Call RollbackPreviousInstall
  StrCpy $R9 "2"
  Return

prepare_failed_before_mutation:
  Delete "$TEMP\weasel-hkcu-backup.reg"
  StrCmp $PreviousProgramBackupDir "" +2 0
  RMDir /r "$PreviousProgramBackupDir"
  StrCpy $R9 "1"
prepare_done:
FunctionEnd

;--------------------------------

Function .onInit
  StrCpy $HadPreviousInstall "0"
  StrCpy $PreviousBaseDir ""
  StrCpy $PreviousInstallDir ""
  StrCpy $PreviousProgramBackupDir ""
  StrCpy $UpgradeSlotId ""
  StrCpy $RollbackFailed "0"
  StrCpy $ProgramRestoreFailed "0"

!ifndef WEASEL_HAS_ARM64_PAYLOAD
  ; Reject before reading/removing an existing installation or copying files.
  ${If} ${IsNativeARM64}
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "$(ARM64NOTSUPPORTED)"
    SetErrorLevel 1633 ; ERROR_INSTALL_PLATFORM_UNSUPPORTED (also in /S mode)
    Quit
  ${EndIf}
!endif

  ; if not version >= 8.1, quit and MessageBox(if not silent)
  ${IfNot} ${AtLeastWin8.1}
    IfSilent toquit
    MessageBox MB_OK '$(SYSTEMVERSIONNOTOK)'
toquit:
    Quit
  ${EndIf}

  ReadRegStr $R0 HKLM "Software\Rime\Weasel" "InstallDir"
  StrCpy $PreviousBaseDir "$R0"
  StrCmp $R0 "" 0 skip
  ; The default installation directory
  ; install x64 build for NativeARM64_WINDOWS11 and NativeAMD64_WINDOWS11
  ${If} ${AtLeastWin11} ; Windows 11 and above
    ${If} ${IsNativeARM64}
      StrCpy $INSTDIR "$PROGRAMFILES64\Rime"
    ${ElseIf} ${IsNativeAMD64}
      StrCpy $INSTDIR "$PROGRAMFILES64\Rime"
    ${Else}
      StrCpy $INSTDIR "$PROGRAMFILES\Rime"
    ${Endif}
  ; install x64 build for NativeAMD64_BELLOW_WINDOWS11
  ${Else} ; Windows 10 or bellow
    ${If} ${IsNativeAMD64}
      StrCpy $INSTDIR "$PROGRAMFILES64\Rime"
    ${Else}
      StrCpy $INSTDIR "$PROGRAMFILES\Rime"
    ${Endif}
  ${Endif}
skip:
  ReadRegStr $R0 HKLM \
  "Software\Microsoft\Windows\CurrentVersion\Uninstall\Weasel" \
  "UninstallString"
  StrCmp $R0 "" done

  IfSilent uninst 0
  MessageBox MB_OKCANCEL|MB_ICONINFORMATION "$(CONFIRMATION)" IDOK uninst
  Abort

uninst:
  ; Validate the previous installation now, but leave it registered and running
  ; until the install section actually begins. This makes cancelling a later UI
  ; page non-destructive.
  ReadRegStr $R1 HKLM SOFTWARE\Rime\Weasel "WeaselRoot"
  StrCmp $R1 "" previous_invalid
  IfFileExists "$R1\WeaselSetup.exe" 0 previous_invalid
  StrCpy $HadPreviousInstall "1"
  StrCpy $PreviousInstallDir "$R1"
  System::Call 'kernel32::GetCurrentProcessId() i.R5'
  StrCpy $UpgradeSlotId "$R5"
  GoTo done

previous_invalid:
  ; The uninstall metadata says an old version exists but its Setup executable
  ; cannot be found. Aborting is safer than destroying unrecoverable state.
  SetErrorLevel 1
  !insertmacro AbortInitFailure

done:
FunctionEnd

; Registry key to check for directory (so if you install again, it will
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\Rime\Weasel" "InstallDir"

; The stuff to install
Section "Weasel"

  SectionIn RO

  ; Publish the base directory only after Setup succeeds. Do not replace
  ; an existing registration when setup is cancelled or fails.
  StrCpy $InstallBaseDir "$INSTDIR"

  ; Always install into the canonical version directory. Same-version upgrades
  ; use the same path and must remove the old tree successfully before any new
  ; payload is extracted; there is no .upgrade-* side-by-side fallback.
  StrCpy $NewInstallDir "$INSTDIR\weasel-${WEASEL_VERSION}"
  StrCpy $INSTDIR "$NewInstallDir"

  Call PreparePreviousInstall
  StrCmp $R9 "0" install_target_ready
  StrCmp $R9 "2" install_target_locked
  SetErrorLevel 1
  StrCmp $RollbackFailed "1" 0 +3
  IfSilent +2
  MessageBox MB_OK|MB_ICONSTOP "$(ROLLBACKFAILED)"
  Abort "$(SETUPFAILED)"

install_target_locked:
  SetErrorLevel 32
  StrCmp $RollbackFailed "1" 0 install_target_locked_restored
  IfSilent +2
  MessageBox MB_OK|MB_ICONSTOP "$(ROLLBACKFAILED)"
  Abort "$(SETUPFAILED)"
install_target_locked_restored:
  IfSilent +2
  MessageBox MB_OK|MB_ICONSTOP "$(FILESINUSE)"
  Abort "$(FILESINUSE)"

install_target_ready:
  SetOverwrite on
  ; Set output path to the installation directory.
  SetOutPath $INSTDIR

  ; Migrate the previous user data from the complete transaction backup. The
  ; source remains intact until commit so rollback never depends on the new tree.
  StrCmp $HadPreviousInstall "1" 0 program_files
  IfFileExists "$PreviousProgramBackupDir\data\*.*" 0 program_files
  CreateDirectory "$INSTDIR\data"
  ClearErrors
  ExecWait '"$SYSDIR\robocopy.exe" "$PreviousProgramBackupDir\data" "$INSTDIR\data" /E /COPY:DAT /DCOPY:T /R:0 /W:0 /XJ /NFL /NDL /NJH /NJS /NP' $R2
  ${If} ${Errors}
    GoTo install_files_failed
  ${EndIf}
  IntCmp $R2 8 install_files_failed program_files install_files_failed

program_files:
  ClearErrors
  File "LICENSE.txt"
  File "README.txt"
  File "7-zip-license.txt"
  File "7z.dll"
  File "7z.exe"
  File "COPYING-curl.txt"
  File "curl.exe"
  File "curl-ca-bundle.crt"
  File "rime-install.bat"
  File "rime-install-config.bat"
  File "start_service.bat"
  File "stop_service.bat"
  File "weasel.dll"
  ${If} ${RunningX64}
    File "weaselx64.dll"
  ${EndIf}
  ${If} ${IsNativeARM64}
!ifdef WEASEL_HAS_ARM64_PAYLOAD
    File "weaselARM.dll"
    File "weaselARM64.dll"
    File "weaselARM64X.dll"
!endif
  ${EndIf}
  ; install x64 build for NativeARM64_WINDOWS11 and NativeAMD64_WINDOWS11
  ${If} ${AtLeastWin11} ; Windows 11 and above
    ${If} ${IsNativeARM64}
      File "WeaselDeployer.exe"
      File "WeaselServer.exe"
      File "rime.dll"
      File "WinSparkle.dll"
    ${ElseIf} ${IsNativeAMD64}
      File "WeaselDeployer.exe"
      File "WeaselServer.exe"
      File "rime.dll"
      File "WinSparkle.dll"
    ${Else}
      File "Win32\WeaselDeployer.exe"
      File "Win32\WeaselServer.exe"
      File "Win32\rime.dll"
      File "Win32\WinSparkle.dll"
    ${Endif}
  ; install x64 build for NativeAMD64_BELLOW_WINDOWS11
  ${Else} ; Windows 10 or bellow
    ${If} ${IsNativeAMD64}
      File "WeaselDeployer.exe"
      File "WeaselServer.exe"
      File "rime.dll"
      File "WinSparkle.dll"
    ${Else}
      File "Win32\WeaselDeployer.exe"
      File "Win32\WeaselServer.exe"
      File "Win32\rime.dll"
      File "Win32\WinSparkle.dll"
    ${Endif}
  ${Endif}

  File "WeaselSetup.exe"
  ; shared data files
  SetOutPath $INSTDIR\data
  File "data\*.yaml"
  File /nonfatal "data\*.txt"
  File /nonfatal "data\*.gram"
  ; opencc data files
  SetOutPath $INSTDIR\data\opencc
  File "data\opencc\*.json"
  File "data\opencc\*.ocd*"

  ${If} ${Errors}
    GoTo install_files_failed
  ${EndIf}

  SetOutPath $INSTDIR

  ; test /T flag for zh_TW locale
  StrCpy $R2 "/i"
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/S" $R1
  IfErrors +2 0
  StrCpy $R2 "/s"
  ${GetOptions} $R0 "/T" $R1
  IfErrors +2 0
  StrCpy $R2 "/t"

  ClearErrors
  ExecWait '"$INSTDIR\WeaselSetup.exe" $R2' $R3
  ${If} ${Errors}
    Call RollbackPreviousInstall
    SetErrorLevel 1
    StrCmp $RollbackFailed "1" 0 +3
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "$(ROLLBACKFAILED)"
    Abort "$(SETUPFAILED)"
  ${EndIf}
  ${If} $R3 == 1223 ; ERROR_CANCELLED from WeaselSetup
    Call RollbackPreviousInstall
    SetErrorLevel 1223
    StrCmp $RollbackFailed "1" 0 +3
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "$(ROLLBACKFAILED)"
    Abort "$(SETUPCANCELLED)"
  ${ElseIf} $R3 != 0
    Call RollbackPreviousInstall
    SetErrorLevel $R3
    StrCmp $RollbackFailed "1" 0 +3
    IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "$(ROLLBACKFAILED)"
    Abort "$(SETUPFAILED)"
  ${EndIf}
  GoTo install_commit

install_files_failed:
  Call RollbackPreviousInstall
  SetErrorLevel 1
  StrCmp $RollbackFailed "1" 0 +3
  IfSilent +2
  MessageBox MB_OK|MB_ICONSTOP "$(ROLLBACKFAILED)"
  Abort "$(SETUPFAILED)"

install_commit:
  ; Commit only after the replacement Setup has completed successfully. For a
  ; cross-version upgrade the old version directory is now obsolete; for a
  ; same-version in-place replacement it is the live new directory and must not
  ; be removed.
  StrCmp $HadPreviousInstall "1" 0 upgrade_committed
  SetOutPath $TEMP
  StrCmp $PreviousInstallDir $INSTDIR cleanup_upgrade_backup 0
  ClearErrors
  RMDir /r "$PreviousInstallDir"
  ${If} ${Errors}
    DetailPrint "Previous version files remain locked and were left in place; they are no longer registered."
  ${EndIf}
cleanup_upgrade_backup:
  Delete "$TEMP\weasel-hkcu-backup.reg"
  RMDir /r "$PreviousProgramBackupDir"
  SetShellVarContext all
  Delete "$SMPROGRAMS\$(DISPLAYNAME)\*.*"
  RMDir "$SMPROGRAMS\$(DISPLAYNAME)"
upgrade_committed:

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM SOFTWARE\Rime\Weasel "InstallDir" "$InstallBaseDir"
  WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayName" "$(DISPLAYNAME)"
  WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayIcon" '"$INSTDIR\WeaselServer.exe"'
  WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayVersion" "${WEASEL_VERSION}.${WEASEL_BUILD}"
  WriteRegStr HKLM "${REG_UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${REG_UNINST_KEY}" "Publisher" "式恕堂"
  WriteRegStr HKLM "${REG_UNINST_KEY}" "URLInfoAbout" "https://rime.im/"
  WriteRegStr HKLM "${REG_UNINST_KEY}" "HelpLink" "https://rime.im/docs/"
  WriteRegDWORD HKLM "${REG_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${REG_UNINST_KEY}" "NoRepair" 1
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; run as user...
  IfSilent deploy_silently
  ExecWait "$INSTDIR\WeaselDeployer.exe /install"
  GoTo deploy_done

  deploy_silently:
  ExecWait "$INSTDIR\WeaselDeployer.exe /deploy"
  deploy_done:

  ; don't redirect on 64 bit system for auto run setting
  ${If} ${IsNativeARM64}
    SetRegView 64
  ${ElseIf} ${IsNativeAMD64}
    SetRegView 64
  ${Endif}
  ; Write autorun key
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "WeaselServer" "$INSTDIR\WeaselServer.exe"
  ; Start WeaselServer
  Exec "$INSTDIR\WeaselServer.exe"

  ; option CheckForUpdates: keep the stored preference on upgrades (restored
  ; before replacement Setup); ask only when unconfigured; silent installs
  ; default to off.
  ReadRegStr $R3 HKCU "Software\Rime\Weasel\Updates" "CheckForUpdates"
  StrCmp $R3 "" 0 end
  IfSilent DisableAutoCheckUpdate
  MessageBox MB_YESNO|MB_ICONINFORMATION "$(AUTOCHKUPDATE)" IDYES EnableAutoCheckUpdate
DisableAutoCheckUpdate:
  WriteRegStr HKCU "Software\Rime\Weasel\Updates" "CheckForUpdates" "0"
  GoTo end
  EnableAutoCheckUpdate:
  WriteRegStr HKCU "Software\Rime\Weasel\Updates" "CheckForUpdates" "1"
end:

SectionEnd

; Optional section (can be disabled by the user)
Section "Start Menu Shortcuts"
  SetShellVarContext all
  CreateDirectory "$SMPROGRAMS\$(DISPLAYNAME)"
  CreateShortCut "$SMPROGRAMS\$(DISPLAYNAME)\$(LNKFORMANUAL).lnk" "$INSTDIR\README.txt"
  CreateShortCut "$SMPROGRAMS\$(DISPLAYNAME)\$(LNKFORSETTING).lnk" "$INSTDIR\WeaselDeployer.exe" "" "$SYSDIR\shell32.dll" 21
  CreateShortCut "$SMPROGRAMS\$(DISPLAYNAME)\$(LNKFORDICT).lnk" "$INSTDIR\WeaselDeployer.exe" "/dict" "$SYSDIR\shell32.dll" 6
  CreateShortCut "$SMPROGRAMS\$(DISPLAYNAME)\$(LNKFORSYNC).lnk" "$INSTDIR\WeaselDeployer.exe" "/sync" "$SYSDIR\shell32.dll" 26
  CreateShortCut "$SMPROGRAMS\$(DISPLAYNAME)\$(LNKFORDEPLOY).lnk" "$INSTDIR\WeaselDeployer.exe" "/deploy" "$SYSDIR\shell32.dll" 144
  CreateShortCut "$SMPROGRAMS\$(DISPLAYNAME)\$(LNKFORSERVER).lnk" "$INSTDIR\WeaselServer.exe" "" "$INSTDIR\WeaselServer.exe" 0
  CreateShortCut "$SMPROGRAMS\$(DISPLAYNAME)\$(LNKFORUSERFOLDER).lnk" "$INSTDIR\WeaselServer.exe" "/userdir" "$SYSDIR\shell32.dll" 126
  CreateShortCut "$SMPROGRAMS\$(DISPLAYNAME)\$(LNKFORAPPFOLDER).lnk" "$INSTDIR\WeaselServer.exe" "/weaseldir" "$SYSDIR\shell32.dll" 19
  CreateShortCut "$SMPROGRAMS\$(DISPLAYNAME)\$(LNKFORUPDATER).lnk" "$INSTDIR\WeaselServer.exe" "/update" "$SYSDIR\shell32.dll" 13
  CreateShortCut "$SMPROGRAMS\$(DISPLAYNAME)\$(LNKFORSETUP).lnk" "$INSTDIR\WeaselSetup.exe" "" "$SYSDIR\shell32.dll" 162
  CreateShortCut "$SMPROGRAMS\$(DISPLAYNAME)\$(LNKFORUNINSTALL).lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0

SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"

  ExecWait '"$INSTDIR\WeaselServer.exe" /quit'

  ExecWait '"$INSTDIR\WeaselSetup.exe" /u'

  ; Remove registry keys
  DeleteRegKey HKLM SOFTWARE\Rime
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Weasel"
  ; don't redirect on 64 bit system for auto run setting
  ${If} ${IsNativeARM64}
    SetRegView 64
  ${ElseIf} ${IsNativeAMD64}
    SetRegView 64
  ${Endif}
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "WeaselServer"

  ; Remove files and uninstaller
  SetOutPath $TEMP
  Delete  "$INSTDIR\data\opencc\*.*"
  Delete  "$INSTDIR\data\*.*"
  Delete  "$INSTDIR\*.*"
  RMDir  "$INSTDIR\data\opencc"
  RMDir  "$INSTDIR\data"
  RMDir  "$INSTDIR"
  SetShellVarContext all
  Delete  "$SMPROGRAMS\$(DISPLAYNAME)\*.*"
  RMDir  "$SMPROGRAMS\$(DISPLAYNAME)"

SectionEnd