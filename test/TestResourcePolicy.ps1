$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$resourceHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\resource.h') -Raw
$deployerRc = Get-Content -LiteralPath (Join-Path $repo 'WeaselDeployer\WeaselDeployer.rc') -Raw

if ($resourceHeader -notmatch '#define\s+IDC_STATIC1\s+-1') {
  throw 'WeaselDeployer resource.h must keep IDC_STATIC1 mapped to -1 for anonymous static labels'
}

$zeroIdLabels = [regex]::Matches(
  $deployerRc,
  '(?m)^\s*LTEXT\s+.*?,\s*0\s*,')
if ($zeroIdLabels.Count -ne 0) {
  throw "WeaselDeployer.rc still contains $($zeroIdLabels.Count) LTEXT control(s) with dialog ID 0"
}

Write-Host 'Resource policy: PASS'
