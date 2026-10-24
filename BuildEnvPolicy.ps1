function Get-WeaselDefaultBoostVersion {
  '1.84.0'
}

function Get-WeaselBoostSourceProblems {
  param([Parameter(Mandatory = $true)][string]$BoostRoot)

  $required = @(
    'boost\version.hpp',
    'bootstrap.bat',
    'boost-build.jam',
    'tools\build\src\build-system.jam'
  )
  @($required | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $BoostRoot $_) -PathType Leaf)
  })
}

function Get-WeaselRequiredBoostPatterns {
  $libraries = @('filesystem', 'json', 'locale', 'regex', 'serialization', 'thread')
  foreach ($arch in @('x64', 'x32')) {
    foreach ($library in $libraries) {
      "libboost_$library-*mt-s-$arch-*.lib"
    }
  }
}

function Get-WeaselMissingBoostPatterns {
  param([Parameter(Mandatory = $true)][string]$StageLibDir)

  $required = @(Get-WeaselRequiredBoostPatterns)
  if (-not (Test-Path -LiteralPath $StageLibDir)) {
    return $required
  }

  @($required | Where-Object {
    -not (Get-ChildItem -LiteralPath $StageLibDir -Filter $_ -File -ErrorAction SilentlyContinue |
          Select-Object -First 1)
  })
}
