$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\xmake.lua'
$source = Get-Content -LiteralPath $sourcePath -Raw
$xbuildPath = Join-Path $PSScriptRoot '..\xbuild.bat'
$xbuild = Get-Content -LiteralPath $xbuildPath -Raw
$buildPath = Join-Path $PSScriptRoot '..\build.bat'
$build = Get-Content -LiteralPath $buildPath -Raw
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

# ATL discovery must be anchored to both the selected Visual Studio instance
# and the selected VC platform-toolset family.
foreach ($required in @('VCToolsInstallDir', 'vswhere', 'get_config("vs")',
                         'get_config("vs_toolset")', 'requested_toolset_family',
                         'toolset_matches')) {
  if (-not $source.Contains($required)) {
    throw "xmake toolchain policy regression: missing selected-toolchain anchor '$required'"
  }
}

# --vs_toolset=14.3 means platform toolset v143; it must not be used as a
# literal VC\Tools\MSVC\14.3* directory prefix. VS2022 v143 also includes
# concrete 14.4x compiler directories such as 14.44.35207.
if ($source -match 'requested_(?:prefix|family)\s*\.\.\s*"\*"') {
  throw 'xmake toolchain policy regression: platform toolset is used as a concrete MSVC directory prefix'
}
if (-not $source.Contains('os.dirs(path.join(msvc_root, "*"))')) {
  throw 'xmake toolchain policy regression: selected VS MSVC directories are not enumerated before family filtering'
}
if (-not $source.Contains('minor_number >= 30') -or
    -not $source.Contains('minor_number < 50')) {
  throw 'xmake toolchain policy regression: v143 does not accept concrete MSVC 14.3x/14.4x versions'
}

# Native command execution is script-domain-only in xmake. Keep vswhere inside
# the ATL rule's on_load callback rather than at top-level description scope.
$ruleIndex = $source.IndexOf('rule("weasel_msvc_atl")')
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

# Xmake can reuse the calling shell's active MSVC target environment. A build
# started from an x64 Developer Prompt must therefore not rely on `-a x86`
# alone: otherwise HostX64\x64\cl.exe can emit x64 objects into the x86 tree.
# Resolve one VS2022 instance and enter a fresh vcvarsall environment per arch.
$xbuildNormalized = $xbuild -replace "`r`n", "`n"
foreach ($required in @(
  'vswhere.exe',
  'VC\Auxiliary\Build\vcvarsall.bat',
  'call :build_xmake_arch x64 x64',
  'call :build_xmake_arch x86 x64_x86',
  'call :build_xmake_arch arm x64_arm',
  'call :build_xmake_arch arm64 x64_arm64'
)) {
  if (-not $xbuild.Contains($required)) {
    throw "xbuild target-environment regression: missing '$required'"
  }
}
if ($xbuildNormalized -notmatch ':build_xmake_arch\n\s*setlocal') {
  throw 'xbuild target-environment regression: per-architecture helper is not environment-isolated'
}
if ($xbuildNormalized -notmatch 'call "%XMAKE_VCVARSALL%" %vcvars_arch%\n\s*if errorlevel 1 exit /b 1') {
  throw 'xbuild target-environment regression: vcvarsall failure is not propagated'
}
if ($xbuildNormalized -notmatch 'xmake f -c -a %xmake_arch%[^\n]*\n\s*if errorlevel 1 exit /b 1') {
  throw 'xbuild target-environment regression: helper does not cleanly configure the requested architecture'
}
if ($xbuildNormalized -match 'xmake f -a (?:arm64|arm|x64|x86)') {
  throw 'xbuild architecture-isolation regression: architecture configure without -c returned'
}

# Build scripts must fail at the first failed subcommand. Otherwise an old xmake
# configuration or a later successful platform build can overwrite ERRORLEVEL
# and hide the root cause.
if ($xbuildNormalized -notmatch 'xmake clean\n\s*if errorlevel 1 exit /b 1') {
  throw 'xbuild fail-fast regression: per-architecture clean failure is not propagated'
}
if ($xbuildNormalized -notmatch 'xmake project -k compile_commands -m %build_config%\n\s*if errorlevel 1 goto error') {
  throw 'xbuild fail-fast regression: compile_commands failure is not propagated'
}

$buildNormalized = $build -replace "`r`n", "`n"
foreach ($pattern in @(
  'call :build_librime_platform x64[^\n]*\n\s*if errorlevel 1 goto error',
  'call :build_librime_platform Win32[^\n]*\n\s*if errorlevel 1 goto error',
  'call :build_data\n\s*if errorlevel 1 goto error',
  'call :build_opencc_data\n\s*if errorlevel 1 goto error'
)) {
  if ($buildNormalized -notmatch $pattern) {
    throw "build.bat fail-fast regression: missing immediate failure propagation for pattern '$pattern'"
  }
}

Write-Host 'Xmake MSVC toolchain isolation policy: PASS'
