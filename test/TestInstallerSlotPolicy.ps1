$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$installer = Get-Content -LiteralPath (Join-Path $repo 'output\install.nsi') -Raw

if ($installer -match 'Rename\s+"\$PreviousInstallDir"') {
  throw 'in-place replacement must not rename the live Weasel installation directory'
}
if ($installer -match '\.upgrade-\$UpgradeSlotId') {
  throw 'in-place replacement must not create a side-by-side .upgrade-* slot'
}
if ($installer -match '/REBOOTOK') {
  throw 'installer must not defer install/version directory deletion until reboot'
}

$sectionStart = $installer.IndexOf('Section "Weasel"')
$sectionEnd = $installer.IndexOf('SectionEnd', $sectionStart)
if ($sectionStart -lt 0 -or $sectionEnd -le $sectionStart) {
  throw 'unable to isolate Weasel install section'
}
$section = $installer.Substring($sectionStart, $sectionEnd - $sectionStart)

$canonical = '  StrCpy $NewInstallDir "$INSTDIR\weasel-${WEASEL_VERSION}"'
$selectTarget = '  StrCpy $INSTDIR "$NewInstallDir"'
$prepareCall = '  Call PreparePreviousInstall'
$overwrite = '  SetOverwrite on'
$canonicalIndex = $section.IndexOf($canonical)
$targetIndex = $section.IndexOf($selectTarget)
$prepareIndex = $section.IndexOf($prepareCall)
$overwriteIndex = $section.IndexOf($overwrite)
if ($canonicalIndex -lt 0) {
  throw 'installer must select the canonical version directory'
}
if ($targetIndex -lt 0 -or $prepareIndex -lt 0 -or $overwriteIndex -lt 0) {
  throw 'installer is missing canonical target selection or transactional preparation'
}
if (-not ($canonicalIndex -lt $targetIndex -and
          $targetIndex -lt $prepareIndex -and
          $prepareIndex -lt $overwriteIndex)) {
  throw 'canonical target selection must precede transactional preparation and extraction'
}

$prepareStart = $installer.IndexOf('Function PreparePreviousInstall')
$prepareEnd = $installer.IndexOf('FunctionEnd', $prepareStart)
if ($prepareStart -lt 0 -or $prepareEnd -le $prepareStart) {
  throw 'unable to isolate PreparePreviousInstall'
}
$prepare = $installer.Substring($prepareStart, $prepareEnd - $prepareStart)

$backupIndex = $prepare.IndexOf('prepare_backup_program:')
$stopIndex = $prepare.IndexOf('  ExecWait ''"$PreviousInstallDir\WeaselServer.exe" /quit''')
if ($backupIndex -lt 0 -or $stopIndex -le $backupIndex) {
  throw 'program backup must precede old-service shutdown'
}
$backup = $prepare.Substring($backupIndex, $stopIndex - $backupIndex)
if ($backup -notmatch 'StrCpy\s+\$PreviousProgramBackupDir\s+"\$TEMP\\weasel-program-backup-\$UpgradeSlotId"') {
  throw 'installer must keep the rollback copy in an isolated temporary directory'
}
if ($backup -notmatch 'robocopy\.exe[\s\S]*?"\$PreviousInstallDir"[\s\S]*?"\$PreviousProgramBackupDir"') {
  throw 'installer must back up the complete previous program tree before replacement'
}

$samePathStart = $prepare.IndexOf('prepare_remove_same_path:')
$lockedStart = $prepare.IndexOf('prepare_locked:', $samePathStart)
if ($samePathStart -lt 0 -or $lockedStart -le $samePathStart) {
  throw 'unable to isolate same-version in-place replacement guard'
}
$samePath = $prepare.Substring($samePathStart, $lockedStart - $samePathStart)
foreach ($required in @(
  'StrCmp $PreviousInstallDir $INSTDIR 0 prepare_done',
  'RMDir /r "$INSTDIR"',
  'GoTo prepare_locked',
  'IfFileExists "$INSTDIR\*.*" prepare_locked 0'
)) {
  if (-not $samePath.Contains($required)) {
    throw "same-version in-place replacement is missing '$required'"
  }
}
$lockedEnd = $prepare.IndexOf('prepare_failed_before_mutation:', $lockedStart)
$locked = $prepare.Substring($lockedStart, $lockedEnd - $lockedStart)
if ($locked -notmatch 'Call RollbackPreviousInstall[\s\S]*?StrCpy \$R9 "2"') {
  throw 'locked canonical directory must rollback instead of falling back to another slot'
}

$rollbackStart = $installer.IndexOf('Function RollbackPreviousInstall')
$rollbackEnd = $installer.IndexOf('FunctionEnd', $rollbackStart)
if ($rollbackStart -lt 0 -or $rollbackEnd -le $rollbackStart) {
  throw 'unable to isolate RollbackPreviousInstall'
}
$rollback = $installer.Substring($rollbackStart, $rollbackEnd - $rollbackStart)
if ($rollback -notmatch 'Call RestorePreviousProgramBackup') {
  throw 'failed in-place replacement must restore the previous program backup'
}

$commitIndex = $section.IndexOf('install_commit:')
$setupIndex = $section.IndexOf('  ExecWait ''"$INSTDIR\WeaselSetup.exe" $R2''')
$cleanupGuard = $section.IndexOf(
  '  StrCmp $PreviousInstallDir $INSTDIR cleanup_upgrade_backup 0',
  $commitIndex)
$cleanupIndex = $section.IndexOf('  RMDir /r "$PreviousInstallDir"', $cleanupGuard)
if ($setupIndex -lt 0 -or $commitIndex -lt 0 -or $cleanupGuard -lt 0 -or
    $cleanupIndex -lt 0 -or
    -not ($setupIndex -lt $commitIndex -and
          $commitIndex -lt $cleanupGuard -and
          $cleanupGuard -lt $cleanupIndex)) {
  throw 'obsolete cross-version files must be cleaned only after replacement Setup succeeds'
}

Write-Host 'Installer in-place overwrite policy: PASS'
