# Test only the real Find-Makensis function, without running packaging,
# downloading NSIS, or invoking the host's Scoop installation.
param([string]$Repo = (Join-Path $PSScriptRoot '..\..'))
$ErrorActionPreference = 'Stop'
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
  (Join-Path $Repo 'make-installer.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw $parseErrors[0] }
$function = $ast.Find({ param($node)
  $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
  $node.Name -eq 'Find-Makensis'
}, $true)
if (-not $function) { throw 'Find-Makensis not found' }
Invoke-Expression $function.Extent.Text
$script:failures = 0

function Test-Case([string]$Name, [scriptblock]$Body) {
  try {
    if (-not (& $Body)) { throw 'Assertion failed' }
    Write-Host "[ok] $Name"
  } catch {
    ++$script:failures
    Write-Host "[FAIL] ${Name}: $_"
  }
}

Test-Case 'No Scoop returns not-found instead of aborting the fallback' {
  function Get-Command { param($Name, $ErrorAction) return $null }
  function Test-Path { param($Path) return $false }
  function scoop { throw 'Scoop must not be invoked when it is unavailable' }
  $null -eq (Find-Makensis)
}

Test-Case 'PATH makensis wins without consulting Scoop or probing files' {
  $expected = 'C:\private-test\makensis.exe'
  function Get-Command {
    param($Name, $ErrorAction)
    if ($Name -eq 'makensis') { return [pscustomobject]@{ Source = $expected } }
    throw 'Unexpected command lookup'
  }
  function Test-Path { throw 'Unexpected filesystem lookup' }
  function scoop { throw 'Unexpected Scoop call' }
  (Find-Makensis) -eq $expected
}

Test-Case 'Conventional NSIS install wins before Scoop' {
  $expected = Join-Path ${env:ProgramFiles(x86)} 'NSIS\Bin\makensis.exe'
  function Get-Command { param($Name, $ErrorAction) return $null }
  function Test-Path { param($Path) $Path -eq $expected }
  function scoop { throw 'Unexpected Scoop call' }
  (Find-Makensis) -eq $expected
}

Test-Case 'Scoop prefix is the package root, not a parent of current' {
  $prefix = Join-Path $env:USERPROFILE 'private-scoop-test\apps\nsis\current'
  $expected = Join-Path $prefix 'Bin\makensis.exe'
  function Get-Command {
    param($Name, $ErrorAction)
    if ($Name -eq 'scoop') { return [pscustomobject]@{ Source = 'scoop' } }
    return $null
  }
  function Test-Path { param($Path) $Path -eq $expected }
  function scoop { param($Action, $Package) $prefix }
  (Find-Makensis) -eq $expected
}

Test-Case 'Scoop with no NSIS package does not abort the installer fallback' {
  function Get-Command {
    param($Name, $ErrorAction)
    if ($Name -eq 'scoop') { return [pscustomobject]@{ Source = 'scoop' } }
    return $null
  }
  function Test-Path { param($Path) return $false }
  function scoop { throw 'NSIS package is not installed' }
  $null -eq (Find-Makensis)
}

if ($script:failures) { exit 1 }
