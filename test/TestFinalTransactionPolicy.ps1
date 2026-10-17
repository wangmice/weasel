$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$rimePath = Join-Path $repoRoot 'RimeWithWeasel\RimeWithWeasel.cpp'
$setupPath = Join-Path $repoRoot 'WeaselSetup\WeaselSetup.cpp'
$imeSetupPath = Join-Path $repoRoot 'WeaselSetup\imesetup.cpp'
$installerPath = Join-Path $repoRoot 'output\install.nsi'

$rime = Get-Content -LiteralPath $rimePath -Raw
$setup = Get-Content -LiteralPath $setupPath -Raw
$imeSetup = Get-Content -LiteralPath $imeSetupPath -Raw
$installer = Get-Content -LiteralPath $installerPath -Raw

function Assert-Policy {
  param(
    [Parameter(Mandatory = $true)][bool]$Condition,
    [Parameter(Mandatory = $true)][string]$Message
  )
  if (-not $Condition) {
    throw $Message
  }
}

# RimeStatus string fields are nullable. Direct std::string construction from
# schema_id is undefined behavior and previously caused crash-only paths.
Assert-Policy (-not $rime.Contains('std::string schema_id = status.schema_id;')) `
  'Rime status handling must not construct std::string directly from nullable schema_id'
Assert-Policy (-not $rime.Contains('std::string(status.schema_id)')) `
  'Rime status handling must not pass nullable schema_id directly to std::string'
$guardedSchemaUses = [regex]::Matches(
  $rime,
  'status\.schema_id\s*\?\s*status\.schema_id\s*:\s*""'
).Count
Assert-Policy ($guardedSchemaUses -ge 3) `
  'all schema_id reads outside response/status conversion must explicitly accept NULL'

# The installer transaction must remain recoverable until deployment succeeds.
Assert-Policy ($installer.Contains('Var SetupCommitted')) `
  'installer must track whether replacement WeaselSetup completed before rollback'
Assert-Policy ($installer.Contains('StrCpy $SetupCommitted "1"')) `
  'installer must mark successful replacement setup before deployment'
Assert-Policy ($installer.Contains('ExecWait "$INSTDIR\WeaselDeployer.exe /install" $R3')) `
  'interactive deployment must capture WeaselDeployer exit status'
Assert-Policy ($installer.Contains('ExecWait "$INSTDIR\WeaselDeployer.exe /deploy" $R3')) `
  'silent deployment must capture WeaselDeployer exit status'
Assert-Policy ($installer.Contains('install_deploy_failed:')) `
  'installer must have an explicit deployment failure rollback path'
$deployFailure = $installer.IndexOf('install_deploy_failed:')
$rollbackAfterDeployFailure = $installer.IndexOf('Call RollbackPreviousInstall', $deployFailure)
$backupCleanup = $installer.IndexOf('cleanup_upgrade_backup:')
Assert-Policy ($deployFailure -ge 0 -and $rollbackAfterDeployFailure -gt $deployFailure) `
  'deployment failure must call the installer rollback transaction'
Assert-Policy ($backupCleanup -gt $deployFailure) `
  'upgrade backup must survive until deployment has succeeded'
Assert-Policy ($installer.Contains('StrCmp $SetupCommitted "1" 0 rollback_fresh_remove_files')) `
  'fresh-install rollback after deployment failure must unregister successful replacement setup'

# Modify-install must roll persistent/profile state back when runtime restart or
# deployment fails, and a deployment failure must reload the restored state.
Assert-Policy ($setup.Contains('auto rollback_modify_state = [&]()')) `
  'modify install must define one rollback path for registry/profile state'
Assert-Policy ($setup.Contains('if (!RestartServer(dir))')) `
  'modify install must distinguish server restart failure from deployment failure'
Assert-Policy ($setup.Contains('if (!RunDeployer(dir))')) `
  'modify install must treat deployer failure as a transactional failure'
Assert-Policy ([regex]::Matches($setup, 'rollback_modify_state\(\);').Count -ge 2) `
  'both restart and deployment failure paths must roll back modify-install state'
Assert-Policy ($setup.Contains('(void)RestartServer(dir);')) `
  'deployment failure rollback must best-effort reload the restored registry state'

# Missing TSF DLLs must not make uninstall permanently non-idempotent. The
# service can be removed from TSF without loading the deleted COM server.
Assert-Policy ($imeSetup.Contains('unregister_text_service_without_dll')) `
  'uninstall must support TSF cleanup when the primary DLL is already missing'
Assert-Policy ($imeSetup.Contains('GetFileAttributesW(path.c_str())')) `
  'uninstall must distinguish a missing DLL from a real unregister failure'
Assert-Policy ($imeSetup.Contains('pProfiles->Unregister(c_clsidTextService)')) `
  'missing-DLL cleanup must unregister the text service through TSF directly'

Write-Host 'Final transaction policy checks passed.'
