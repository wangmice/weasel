$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\xmake.lua'
$source = Get-Content -LiteralPath $sourcePath -Raw

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

# ATL discovery must be anchored either to the active Developer Prompt's VC
# toolset root or, without a prompt, to the VS instance selected by xmake.
foreach ($required in @('VCToolsInstallDir', 'vswhere', 'get_config("vs")')) {
  if (-not $source.Contains($required)) {
    throw "xmake toolchain policy regression: missing selected-toolchain anchor '$required'"
  }
}

Write-Host 'Xmake MSVC toolchain isolation policy: PASS'
