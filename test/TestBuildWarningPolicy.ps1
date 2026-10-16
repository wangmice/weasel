$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$targetver = Get-Content -LiteralPath (Join-Path $repo 'WeaselTSF\targetver.h') -Raw
$xmake = Get-Content -LiteralPath (Join-Path $repo 'xmake.lua') -Raw
$ipcTest = Get-Content -LiteralPath (Join-Path $repo 'test\TestWeaselIPC\TestWeaselIPC.cpp') -Raw

if ($targetver -notmatch '(?s)#ifdef\s+_WIN32_WINNT\s*#undef\s+_WIN32_WINNT\s*#endif\s*#define\s+_WIN32_WINNT\s+_WIN32_WINNT_WINBLUE') {
  throw 'WeaselTSF targetver.h must replace an existing _WIN32_WINNT before pinning Windows 8.1'
}

if ($xmake -match 'add_defines\(\s*"VERSION_MAJOR=') {
  throw 'version macros must not use global add_defines because they leak into resource compilation'
}
foreach ($required in @(
  '/DVERSION_MAJOR=',
  '/DVERSION_MINOR=',
  '/DVERSION_PATCH=',
  '/DPRODUCT_VERSION='
)) {
  if (-not $xmake.Contains($required)) {
    throw "C/C++ version macro injection is missing '$required'"
  }
}

if ($ipcTest -match '\(int\)\s*ShellExecute') {
  throw 'TestWeaselIPC must not truncate ShellExecute HINSTANCE to int'
}
if ($ipcTest -notmatch 'INT_PTR\s+ret\s*=\s*reinterpret_cast<INT_PTR>\(\s*ShellExecuteW\(') {
  throw 'TestWeaselIPC must compare ShellExecuteW through an INT_PTR result'
}

Write-Host 'Build warning hygiene policy: PASS'
