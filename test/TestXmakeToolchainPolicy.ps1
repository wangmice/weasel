$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\xmake.lua'
$source = Get-Content -LiteralPath $sourcePath -Raw
$xbuildPath = Join-Path $PSScriptRoot '..\xbuild.bat'
$xbuild = Get-Content -LiteralPath $xbuildPath -Raw
$env2022Path = Join-Path $PSScriptRoot '..\env.vs2022.bat'
$env2022 = Get-Content -LiteralPath $env2022Path -Raw

# The project must not re-inject a Developer Prompt's complete INCLUDE list.
# Xmake already wires the selected MSVC/SDK sysincludes; importing a stale
# shell INCLUDE after `--vs` selection can mix STL/ATL headers from another
# Visual Studio installation with xmake's compiler and libraries.
if ($source -match 'os\.getenv\("include"\)') {
  throw 'xmake toolchain policy regression: shell INCLUDE is imported directly'
}
if ($source -match 'add_includedirs\(include\)') {
  throw 'xmake toolchain policy regression: shell include entries are appended globally'
}
if ($source -match 'os\.dirs\([^\r\n]*Microsoft Visual Studio') {
  throw 'xmake toolchain policy regression: unconstrained Visual Studio directory scan returned'
}

# ATL discovery must be anchored either to the active Developer Prompt's VC
# toolset root or, without a prompt, to the VS instance selected by xmake.
foreach ($required in @('VCToolsInstallDir', 'vswhere', 'get_config("vs")'.Replace('\"', '"'))) {
  if (-not $source.Contains($required)) {
    throw "xmake toolchain policy regression: missing selected-toolchain anchor '$required'"
  }
}

# Native command execution is script-domain-only in xmake. Keep vswhere inside
# the ATL rule's on_load callback rather than at top-level description scope.
$ruleIndex = $source.IndexOf('rule("weasel_msvc_atl")'.Replace('\"', '"'))
$onLoadIndex = if ($ruleIndex -ge 0) {
  $source.IndexOf('on_load(function(target)', $ruleIndex)
} else { -1 }
$ioRunIndex = if ($ruleIndex -ge 0) {
  $source.IndexOf('os.iorunv', $ruleIndex)
} else { -1 }
if ($ruleIndex -lt 0 -or $onLoadIndex -lt 0 -or $ioRunIndex -lt $onLoadIndex) {
  throw 'xmake toolchain policy regression: vswhere execution is outside the ATL on_load script domain'
}
if ($source -match 'os\.cp\(') {
  throw 'xmake toolchain policy regression: filesystem mutation returned to description loading'
}

# xbuild must pass the same Visual Studio/toolset policy to every xmake
# configure. Otherwise xmake defaults to the newest installed VS, which can
# diverge from build.bat/v143 on hosts that also have newer Build Tools.
foreach ($required in @('--vs=%XMAKE_VS%', '--vs_toolset=%XMAKE_VS_TOOLSET%')) {
  if (-not $xbuild.Contains($required)) {
    throw "xbuild toolchain policy regression: missing '$required'"
  }
}
if (-not $env2022.Contains('set XMAKE_VS=2022') -or
    -not $env2022.Contains('set XMAKE_VS_TOOLSET=14.3')) {
  throw 'VS2022 environment must pin xmake to VS2022/v143'
}

Write-Host 'Xmake MSVC toolchain isolation policy: PASS'
