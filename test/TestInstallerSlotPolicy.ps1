$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$installer = Get-Content -LiteralPath (Join-Path $repo 'output\install.nsi') -Raw

if ($installer -match 'Rename\s+"\$PreviousInstallDir"\s+"\$PreviousBackupDir"') {
  throw 'same-version replacement must not rename the live Weasel installation directory'
}

$canonical = '  StrCpy $NewInstallDir "$INSTDIR\weasel-${WEASEL_VERSION}"'
$sameSlotGuard = '  StrCmp $PreviousInstallDir $NewInstallDir 0 install_slot_ready'
$upgradeSlot = '  StrCpy $NewInstallDir "$INSTDIR\weasel-${WEASEL_VERSION}.upgrade-$UpgradeSlotId"'
$canonicalIndex = $installer.IndexOf($canonical)
$guardIndex = $installer.IndexOf($sameSlotGuard)
$upgradeIndex = $installer.IndexOf($upgradeSlot)
if ($canonicalIndex -lt 0) {
  throw 'installer must select the canonical version directory by default'
}
if ($guardIndex -lt 0 -or $upgradeIndex -lt 0) {
  throw 'installer must use a side-by-side slot only when the canonical path is the live old slot'
}
if (-not ($canonicalIndex -lt $guardIndex -and $guardIndex -lt $upgradeIndex)) {
  throw 'canonical slot selection must precede the same-slot guard and upgrade-slot fallback'
}
if ($installer -notmatch 'StrCpy\s+\$INSTDIR\s+"\$NewInstallDir"') {
  throw 'installer payload must be written to the selected replacement slot'
}
if ($installer -notmatch 'RMDir\s+/r\s+/REBOOTOK\s+"\$PreviousInstallDir"') {
  throw 'committed replacements must clean the old slot with reboot-safe deletion'
}
if ($installer -notmatch 'rollback_remove_new_files:[\s\S]*?RMDir\s+/r\s+/REBOOTOK\s+"\$INSTDIR"') {
  throw 'failed replacements must remove only the new slot and preserve the old program directory'
}
if ($installer -match 'PreviousMoved') {
  throw 'installer must not retain the whole-directory move rollback state'
}

Write-Host 'Installer slot policy: PASS'
