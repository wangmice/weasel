$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$getRime = Get-Content -LiteralPath (Join-Path $repo 'get-rime.ps1') -Raw

$start = $getRime.IndexOf('function KillWeaselServer')
$end = $getRime.IndexOf('      if ( $use -eq "dev")', $start)
if ($start -lt 0 -or $end -le $start) {
  throw 'unable to isolate KillWeaselServer in get-rime.ps1'
}
$body = $getRime.Substring($start, $end - $start)

if ($body -notmatch '\$targets\s*=\s*@\(' -or
    $body -notmatch 'Get-Process\s+(?:-Name\s+)?\$processName') {
  throw 'KillWeaselServer must capture one fixed initial WeaselServer process snapshot'
}
if ($body -match 'while\s*\(') {
  throw 'KillWeaselServer must not use an unbounded process-name polling loop'
}
if ($body -match 'Stop-Process\s+-Name\s+\$processName') {
  throw 'KillWeaselServer must stop the captured process instances, not every process matching the name'
}
if ($body -notmatch '\[System\.Diagnostics\.Stopwatch\]::StartNew\(\)') {
  throw 'KillWeaselServer must enforce one shared elapsed-time budget'
}
if ($body -notmatch 'Stop-Process[^\r\n]*-ErrorAction\s+Stop') {
  throw 'KillWeaselServer must preserve Stop-Process failures instead of silently swallowing them'
}
if ($body -notmatch '\.WaitForExit\s*\(' -or
    $body -notmatch '\$remaining') {
  throw 'KillWeaselServer must wait only within the remaining shared deadline'
}
if ($body -notmatch 'Get-Process\s+(?:-Name\s+)?\$processName[\s\S]*?throw') {
  throw 'KillWeaselServer must reject any WeaselServer instance still present after the bounded stop attempt'
}
if ($body -match 'Stop-Process[^\r\n]*-ErrorAction\s+SilentlyContinue') {
  throw 'KillWeaselServer must not discard stop failures'
}

Write-Host 'get-rime bounded server-stop policy: PASS'
