$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\BuildEnvPolicy.ps1')

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("weasel-boost-policy-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null
try {
  $patterns = @(Get-WeaselRequiredBoostPatterns)
  if ($patterns.Count -ne 12) { throw "expected 12 required Boost patterns, got $($patterns.Count)" }

  foreach ($pattern in $patterns | Where-Object { $_ -like '*-x64-*' }) {
    $name = $pattern.Replace('*', 'vc143')
    New-Item -ItemType File -Path (Join-Path $temp $name) | Out-Null
  }
  $missing = @(Get-WeaselMissingBoostPatterns -StageLibDir $temp)
  if (-not ($missing | Where-Object { $_ -like '*-x32-*' })) {
    throw 'x64-only Boost stage was incorrectly accepted as complete'
  }

  Remove-Item -LiteralPath $temp -Recurse -Force
  New-Item -ItemType Directory -Path $temp | Out-Null
  foreach ($pattern in $patterns) {
    $name = $pattern.Replace('*', 'vc143')
    New-Item -ItemType File -Path (Join-Path $temp $name) | Out-Null
  }
  $missing = @(Get-WeaselMissingBoostPatterns -StageLibDir $temp)
  if ($missing.Count -ne 0) { throw "complete Boost stage reported missing: $($missing -join ', ')" }

  Remove-Item -LiteralPath (Get-ChildItem -LiteralPath $temp -Filter 'libboost_serialization-*x64-*.lib' | Select-Object -First 1).FullName
  $missing = @(Get-WeaselMissingBoostPatterns -StageLibDir $temp)
  if (-not ($missing | Where-Object { $_ -like 'libboost_serialization-*x64-*' })) {
    throw 'missing x64 serialization library was not detected'
  }

  Write-Host 'Build environment Boost policy: PASS'
} finally {
  Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
