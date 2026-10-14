param(
  [string]$SourcePath = (Join-Path $PSScriptRoot '..\..\WeaselTSF\dllmain.cpp')
)

$source = Get-Content -LiteralPath $SourcePath -Raw
$forbidden = @(
  'SetUnhandledExceptionFilter',
  'MiniDumpWriteDump',
  'DbgHelp'
)

$hits = @($forbidden | Where-Object { $source.Contains($_) })
if ($hits.Count -ne 0) {
  throw "TSF host-safety regression: process-wide crash handling found: $($hits -join ', ')"
}

Write-Host 'TSF host-safety policy: PASS'
