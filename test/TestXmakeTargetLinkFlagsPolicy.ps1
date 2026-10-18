$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$targets = @(
  @{ Path = 'WeaselSetup\xmake.lua'; Kind = 'ld'; Fragment = 'add_ldflags("/DEBUG", "/OPT:REF", "/OPT:ICF", "/LARGEADDRESSAWARE", "/ERRORREPORT:QUEUE", {force = true})' },
  @{ Path = 'WeaselServer\xmake.lua'; Kind = 'ld'; Fragment = 'add_ldflags("/DEBUG", "/OPT:REF", "/OPT:ICF", "/LARGEADDRESSAWARE", "/ERRORREPORT:QUEUE", {force = true})' },
  @{ Path = 'WeaselDeployer\xmake.lua'; Kind = 'ld'; Fragment = 'add_ldflags("/DEBUG", "/OPT:ICF", "/LARGEADDRESSAWARE", "/ERRORREPORT:QUEUE", {force = true})' },
  @{ Path = 'WeaselTSF\xmake.lua'; Kind = 'sh'; Fragment = 'add_shflags("/DEBUG", "/OPT:REF", "/OPT:ICF", "/LTCG", "/INCREMENTAL:NO", {force = true})' }
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
$tsfXmake = Get-Content -LiteralPath (Join-Path $repoRoot 'WeaselTSF\xmake.lua') -Raw

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

if (-not $rootXmake.Contains('rule("weasel_release_gl")')) {
  throw 'xmake LTCG policy regression: release /GL target rule is missing'
}
if (-not $rootXmake.Contains('if target:name() ~= "WeaselTSF" then')) {
  throw 'xmake LTCG policy regression: WeaselTSF is no longer excluded from release /GL'
}
if (-not $rootXmake.Contains('target:add("cxflags", "/GL", {force = true})')) {
  throw 'xmake LTCG policy regression: release /GL is not forced through the target rule'
}
if ($tsfXmake.Contains('/GL-')) {
  throw 'xmake LTCG policy regression: WeaselTSF should be excluded from /GL instead of overriding it with /GL-'
}
if (-not $tsfXmake.Contains('add_shflags("/DEBUG", "/OPT:REF", "/OPT:ICF", "/LTCG", "/INCREMENTAL:NO", {force = true})')) {
  throw 'xmake LTCG policy regression: WeaselTSF shared link must carry explicit /LTCG'
}

if (-not $rootXmake.Contains('target:add("ldflags", "/SUBSYSTEM:CONSOLE", {force = true})')) {
  throw 'xmake subsystem policy regression: console subsystem flag is not forced'
}
if (-not $rootXmake.Contains('target:add("ldflags", "/SUBSYSTEM:WINDOWS", {force = true})')) {
  throw 'xmake subsystem policy regression: windows subsystem flag is not forced'
}

& (Join-Path $PSScriptRoot 'TestBuildWarningPolicy.ps1')

Write-Host 'Xmake target linker flag policy: PASS'
