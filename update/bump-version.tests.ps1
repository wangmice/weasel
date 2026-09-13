#!/usr/bin/env pwsh
# Regression tests for replace_str in bump-version.ps1 (N7).
#
# Extracts the REAL function from bump-version.ps1 via the PowerShell AST
# (dot-sourcing would run the script's main flow), then checks against
# copies of the live files that a bump round-trip keeps files as UTF-8
# without BOM, XML parseable, non-ASCII intact, and batch files pure ASCII.
#
# Run from anywhere:  powershell -File update/bump-version.tests.ps1
#                     pwsh        -File update/bump-version.tests.ps1

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$bumpScript = Join-Path $scriptDir 'bump-version.ps1'
$fail = 0

# --- extract the production replace_str from bump-version.ps1 ---
$ast = [System.Management.Automation.Language.Parser]::ParseFile($bumpScript, [ref]$null, [ref]$null)
$fnAst = $ast.Find({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'replace_str' }, $true)
if (-not $fnAst) {
  Write-Output '[FAIL] replace_str not found in bump-version.ps1'
  exit 1
}
$replace_str = [scriptblock]::Create($fnAst.Extent.Text)
. $replace_str  # define the extracted function in this scope

function Assert([bool]$cond, [string]$what) {
  if ($cond) { Write-Output "[ok] $what" }
  else { Write-Output "[FAIL] $what"; $script:fail = 1 }
}

# --- run against copies of the live files ---
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("weasel_bump_tests_" + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
  foreach ($f in 'update/appcast.xml', 'update/testing-appcast.xml', 'build.bat', 'xbuild.bat') {
    $dst = Join-Path $tmp ($f -replace '/', '_')
    Copy-Item (Join-Path $scriptDir "..\$f") $dst
    replace_str $dst '\d+\.\d+\.\d+' '9.9.9'

    $bytes = [System.IO.File]::ReadAllBytes($dst)
    $bom = ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $utf16 = ($bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE)
    Assert (-not $bom) "$f : no BOM"
    Assert (-not $utf16) "$f : not UTF-16LE"

    if ($f -like '*.xml') {
      $xml_ok = $true
      try {
        $x = New-Object System.Xml.XmlDocument
        $x.Load($dst)
        if ($x.rss.channel.item.title -notmatch '9\.9\.9') { $xml_ok = $false }
      } catch { $xml_ok = $false }
      Assert $xml_ok "$f : valid XML with version replaced"
      # non-ASCII bytes survive the round-trip
      $orig = [System.IO.File]::ReadAllBytes((Join-Path $scriptDir "..\$f"))
      $src_text = [System.Text.Encoding]::UTF8.GetString($orig)
      $new_text = [System.Text.Encoding]::UTF8.GetString($bytes)
      $na = { param($t) (($t.ToCharArray() | Where-Object { [int]$_ -gt 127 }) -join '') }
      Assert ((& $na $src_text) -eq (& $na $new_text)) "$f : non-ASCII content preserved"
    } else {
      $nonAscii = ($bytes | Where-Object { $_ -gt 127 }).Count
      Assert ($nonAscii -eq 0) "$f : still pure ASCII (cmd-safe)"
    }
  }
} finally {
  Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}

if ($fail) { Write-Output '=> FAIL'; exit 1 } else { Write-Output '=> PASS'; exit 0 }
