$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$targets = @(
  @{ Path = 'WeaselSetup\xmake.lua'; Kind = 'ld'; Fragment = 'add_ldflags("/DEBUG", "/OPT:REF", "/OPT:ICF", "/LARGEADDRESSAWARE", "/ERRORREPORT:QUEUE", {force = true})' },
  @{ Path = 'WeaselServer\xmake.lua'; Kind = 'ld'; Fragment = 'add_ldflags("/DEBUG", "/OPT:REF", "/OPT:ICF", "/LARGEADDRESSAWARE", "/ERRORREPORT:QUEUE", {force = true})' },
  @{ Path = 'WeaselDeployer\xmake.lua'; Kind = 'ld'; Fragment = 'add_ldflags("/DEBUG", "/OPT:ICF", "/LARGEADDRESSAWARE", "/ERRORREPORT:QUEUE", {force = true})' },
  @{ Path = 'WeaselTSF\xmake.lua'; Kind = 'sh'; Fragment = 'add_shflags("/DEBUG", "/OPT:REF", "/OPT:ICF", {force = true})' }
)

foreach ($target in $targets) {
  $path = Join-Path $repoRoot $target.Path
  $content = Get-Content -LiteralPath $path -Raw

  if ($content -match 'add_(?:ld|sh)flags\(\s*["''][^"'']*\s+[^"'']*["'']') {
    throw "xmake linker flag policy regression: multiple linker options are packed into one string in $($target.Path)"
  }
  if (-not $content.Contains($target.Fragment)) {
    throw "xmake linker flag policy regression: missing split /DEBUG flags in $($target.Path)"
  }
  if (-not $content.Contains('.pdb"), "$(projectdir)/output')) {
    throw "xmake linker flag policy regression: expected PDB publishing path missing in $($target.Path)"
  }
}

$rootXmakePath = Join-Path $repoRoot 'xmake.lua'
$rootXmake = Get-Content -LiteralPath $rootXmakePath -Raw
if ($rootXmake -match 'add_ldflags\(\s*["''][^"'']*\s+[^"'']*["'']') {
  throw 'xmake linker flag policy regression: multiple global linker options are packed into one string'
}

$requiredGlobalFlags = 'add_ldflags("/TLBID:1", "/DYNAMICBASE", "/NXCOMPAT", {force = true})'
if (-not $rootXmake.Contains($requiredGlobalFlags)) {
  throw 'xmake linker flag policy regression: global MSVC security flags must stay split and forced'
}

$requiredReleaseFlags = 'add_ldflags("/LTCG", "/INCREMENTAL:NO", {force = true})'
if (-not $rootXmake.Contains($requiredReleaseFlags)) {
  throw 'xmake linker flag policy regression: release LTCG flags must stay split and forced'
}

Write-Host 'Xmake target linker flag policy: PASS'
