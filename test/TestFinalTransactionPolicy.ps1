$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$rimePath = Join-Path $repoRoot 'RimeWithWeasel\RimeWithWeasel.cpp'
$setupPath = Join-Path $repoRoot 'WeaselSetup\WeaselSetup.cpp'
$imeSetupPath = Join-Path $repoRoot 'WeaselSetup\imesetup.cpp'
$cleanupPath = Join-Path $repoRoot 'WeaselSetup\RegistrationCleanup.h'
$installerPath = Join-Path $repoRoot 'output\install.nsi'

$rime = Get-Content -LiteralPath $rimePath -Raw
$setup = Get-Content -LiteralPath $setupPath -Raw
$imeSetup = Get-Content -LiteralPath $imeSetupPath -Raw
$cleanup = if (Test-Path -LiteralPath $cleanupPath) {
  Get-Content -LiteralPath $cleanupPath -Raw
} else {
  ''
}
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

# Deployment is not rollback-safe: librime can mutate the user workspace before
# returning failure. Commit the installed program first, then report deployment
# failure without reverting to an older binary or running fresh-install /u.
Assert-Policy (-not $installer.Contains('Var SetupCommitted')) `
  'installer must not model deployment as part of the reversible setup transaction'
Assert-Policy ($installer.Contains('Var DeployExitCode')) `
  'installer must retain deployment status after the program commit'
$rollbackFreshStart = $installer.IndexOf('rollback_fresh_only:')
$rollbackFreshEnd = $installer.IndexOf('FunctionEnd', $rollbackFreshStart)
$rollbackFresh = $installer.Substring(
  $rollbackFreshStart,
  $rollbackFreshEnd - $rollbackFreshStart
)
Assert-Policy (-not $rollbackFresh.Contains('WeaselSetup.exe" /u')) `
  'fresh rollback must not erase pre-existing HKCU state through WeaselSetup /u'

$commit = $installer.IndexOf('install_commit:')
$deploy = $installer.IndexOf('install_deploy:', $commit)
Assert-Policy ($commit -ge 0 -and $deploy -gt $commit) `
  'deployment must run only after the new installation has been committed'
$deployEnd = $installer.IndexOf('; option CheckForUpdates:', $deploy)
$deployBlock = $installer.Substring($deploy, $deployEnd - $deploy)
Assert-Policy ($deployBlock.Contains('ExecWait "$INSTDIR\WeaselDeployer.exe /install" $R3')) `
  'interactive deployment must capture WeaselDeployer exit status'
Assert-Policy ($deployBlock.Contains('ExecWait "$INSTDIR\WeaselDeployer.exe /deploy" $R3')) `
  'silent deployment must capture WeaselDeployer exit status'
Assert-Policy (-not $deployBlock.Contains('Call RollbackPreviousInstall')) `
  'deployment failure must not roll an already-mutated workspace back to older binaries'
Assert-Policy (-not $deployBlock.Contains('Abort "$(SETUPFAILED)"')) `
  'post-commit deployment failure must keep the committed installation recoverable'
Assert-Policy ($installer.Contains('SetErrorLevel $DeployExitCode')) `
  'deployment failure must still propagate a non-zero installer exit code'
Assert-Policy ($installer.Contains('$(DEPLOYFAILED)')) `
  'interactive deployment failure must have a distinct warning instead of setup rollback'

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

# Missing TSF DLLs must not make uninstall permanently non-idempotent. TSF
# unregister removes profiles/categories, while the executable must also delete
# stale COM/TIP registry trees that DllUnregisterServer normally owns.
Assert-Policy ($imeSetup.Contains('unregister_text_service_without_dll')) `
  'uninstall must support TSF cleanup when the primary DLL is already missing'
Assert-Policy ($imeSetup.Contains('GetFileAttributesW(path.c_str())')) `
  'uninstall must distinguish a missing DLL from a real unregister failure'
Assert-Policy ($imeSetup.Contains('pProfiles->Unregister(c_clsidTextService)')) `
  'missing-DLL cleanup must unregister the text service through TSF directly'
Assert-Policy ($setup.Contains('#include "RegistrationCleanup.h"')) `
  'WeaselSetup must include post-uninstall registration cleanup'
Assert-Policy ($setup.Contains('CleanupTextServiceRegistrationAfterUninstall')) `
  'successful uninstall must remove stale COM/TIP registration without loading the DLL'
Assert-Policy ($cleanup.Contains('Software\\Classes\\CLSID\\')) `
  'cleanup must remove the COM CLSID tree normally owned by DllUnregisterServer'
Assert-Policy ($cleanup.Contains('Software\\Microsoft\\CTF\\TIP\\')) `
  'cleanup must remove the registered TSF TIP tree'
Assert-Policy ($cleanup.Contains('Software\\Classes\\Software\\Microsft\\CTF\\TIP\\')) `
  'cleanup must remove the historical misspelled TIP tree'
Assert-Policy ($cleanup.Contains('KEY_WOW64_64KEY') -and $cleanup.Contains('KEY_WOW64_32KEY')) `
  'cleanup must cover both registry views on 64-bit Windows'
Assert-Policy ($cleanup.Contains('per_user_root()') -and $cleanup.Contains('per_user_subkey(L"Software\\Classes")')) `
  'cleanup must keep per-user class removal bound to the original invoking user across UAC elevation'

Write-Host 'Final transaction policy checks passed.'
