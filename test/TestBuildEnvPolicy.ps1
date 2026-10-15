$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\BuildEnvPolicy.ps1')

if ((Get-WeaselDefaultBoostVersion) -ne '1.84.0') {
  throw "default Boost version must stay aligned with CI baseline 1.84.0"
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("weasel-boost-policy-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null
try {
  $sourceProblems = @(Get-WeaselBoostSourceProblems -BoostRoot $temp)
  foreach ($required in @(
    'boost\version.hpp',
    'bootstrap.bat',
    'boost-build.jam',
    'tools\build\src\build-system.jam')) {
    if ($sourceProblems -notcontains $required) {
      throw "incomplete Boost source did not report missing $required"
    }
  }

  foreach ($relative in @(
    'boost\version.hpp',
    'bootstrap.bat',
    'boost-build.jam',
    'tools\build\src\build-system.jam')) {
    $path = Join-Path $temp $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
    New-Item -ItemType File -Path $path -Force | Out-Null
  }
  $sourceProblems = @(Get-WeaselBoostSourceProblems -BoostRoot $temp)
  if ($sourceProblems.Count -ne 0) {
    throw "complete Boost source reported missing files: $($sourceProblems -join ', ')"
  }

  $stage = Join-Path $temp 'stage\lib'
  New-Item -ItemType Directory -Path $stage -Force | Out-Null
  $patterns = @(Get-WeaselRequiredBoostPatterns)
  if ($patterns.Count -ne 12) { throw "expected 12 required Boost patterns, got $($patterns.Count)" }

  foreach ($pattern in $patterns | Where-Object { $_ -like '*-x64-*' }) {
    $name = $pattern.Replace('*', 'vc143')
    New-Item -ItemType File -Path (Join-Path $stage $name) | Out-Null
  }
  $missing = @(Get-WeaselMissingBoostPatterns -StageLibDir $stage)
  if (-not ($missing | Where-Object { $_ -like '*-x32-*' })) {
    throw 'x64-only Boost stage was incorrectly accepted as complete'
  }

  Remove-Item -LiteralPath $stage -Recurse -Force
  New-Item -ItemType Directory -Path $stage | Out-Null
  foreach ($pattern in $patterns) {
    $name = $pattern.Replace('*', 'vc143')
    New-Item -ItemType File -Path (Join-Path $stage $name) | Out-Null
  }
  $missing = @(Get-WeaselMissingBoostPatterns -StageLibDir $stage)
  if ($missing.Count -ne 0) { throw "complete Boost stage reported missing: $($missing -join ', ')" }

  Remove-Item -LiteralPath (Get-ChildItem -LiteralPath $stage -Filter 'libboost_serialization-*x64-*.lib' | Select-Object -First 1).FullName
  $missing = @(Get-WeaselMissingBoostPatterns -StageLibDir $stage)
  if (-not ($missing | Where-Object { $_ -like 'libboost_serialization-*x64-*' })) {
    throw 'missing x64 serialization library was not detected'
  }

  Write-Host 'Build environment Boost policy: PASS'
} finally {
  Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}