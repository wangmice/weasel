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
