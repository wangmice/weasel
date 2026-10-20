$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $repo 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
  throw "VERSION must contain exactly MAJOR.MINOR.PATCH, got: $version"
}

foreach ($name in @('build.bat', 'xbuild.bat')) {
  $content = Get-Content -LiteralPath (Join-Path $repo $name) -Raw
  if ($content -notmatch 'update\\load-version\.bat') {
    throw "$name must load version metadata through update\load-version.bat"
  }
  if ($content -match '(?im)^\s*(?:if\s+not\s+defined\s+)?VERSION_(?:MAJOR|MINOR|PATCH)\s+set\s+') {
    throw "$name must not define version components independently"
  }
}

$xmake = Get-Content -LiteralPath (Join-Path $repo 'xmake.lua') -Raw
if ($xmake -notmatch 'path\.join\(os\.projectdir\(\),\s*"VERSION"\)') {
  throw 'xmake.lua must load the repository VERSION file for direct xmake builds'
}
if ($xmake -match 'PRODUCT_VERSION[^\r\n]*0\.0\.0\.0') {
  throw 'xmake.lua must not fall back to a synthetic 0.0.0.0 product version'
}

$repoUrl = 'https://github.com/wangmice/weasel'
foreach ($relative in @('update\appcast.xml', 'update\testing-appcast.xml')) {
  $appcast = Get-Content -LiteralPath (Join-Path $repo $relative) -Raw
  $expectedAsset = "$repoUrl/releases/download/$version/weasel-$version.0-installer.exe"

  if ($appcast -notmatch [regex]::Escape("<title>小狼毫 $version</title>")) {
    throw "$relative does not advertise VERSION $version"
  }
  if ($appcast -notmatch [regex]::Escape("sparkle:version=`"$version`"")) {
    throw "$relative does not expose sparkle:version=$version"
  }
  if ($appcast -notmatch [regex]::Escape($expectedAsset)) {
    throw "$relative does not point at $expectedAsset"
  }
  if ($appcast -match 'github\.com/rime/weasel|rime\.github\.io/(?:release|testing)/weasel') {
    throw "$relative still contains an upstream release/update URL"
  }
}

foreach ($relative in @('update\bump-version.ps1', 'update\bump-version.sh')) {
  $script = Get-Content -LiteralPath (Join-Path $repo $relative) -Raw
  if ($script -notmatch 'wangmice/weasel') {
    throw "$relative must default to wangmice/weasel"
  }
  if ($script -match 'github\.com/rime/weasel') {
    throw "$relative still contains an upstream repository URL"
  }
}

$serverHeader = Get-Content -LiteralPath (Join-Path $repo 'WeaselServer\WeaselServerApp.h') -Raw

function Get-CppStringConstantValue {
  param(
    [string]$Content,
    [string]$Name
  )

  $pattern = '(?s)\b' + [regex]::Escape($Name) + '\s*\[\]\s*=\s*(?<value>(?:"(?:[^"\\]|\\.)*"\s*)+);'
  $match = [regex]::Match($Content, $pattern)
  if (-not $match.Success) {
    throw "unable to locate C++ string constant $Name"
  }

  $parts = [regex]::Matches($match.Groups['value'].Value, '"(?<part>(?:[^"\\]|\\.)*)"')
  return (($parts | ForEach-Object { $_.Groups['part'].Value }) -join '')
}

$expectedFeeds = @{
  kStableUpdateFeedUrl = 'https://raw.githubusercontent.com/wangmice/weasel/master/update/appcast.xml'
  kTestingUpdateFeedUrl = 'https://raw.githubusercontent.com/wangmice/weasel/master/update/testing-appcast.xml'
}
foreach ($entry in $expectedFeeds.GetEnumerator()) {
  $actual = Get-CppStringConstantValue -Content $serverHeader -Name $entry.Key
  if ($actual -ne $entry.Value) {
    throw "WeaselServerApp.h $($entry.Key) is '$actual', expected '$($entry.Value)'"
  }
}
if ($serverHeader -match 'ManualUpdateFeedURL|TestingManualUpdateFeedURL') {
  throw 'manual update checks must not fall back to legacy upstream APPCAST resources'
}

$serverCpp = Get-Content -LiteralPath (Join-Path $repo 'WeaselServer\WeaselServerApp.cpp') -Raw
if ($serverCpp -notmatch 'win_sparkle_set_appcast_url\(kStableUpdateFeedUrl\)') {
  throw 'WinSparkle must be initialized with the fork stable feed'
}

$ci = Get-Content -LiteralPath (Join-Path $repo '.github\workflows\ci.yml') -Raw
if ($ci -match "github\.repository\s*==\s*'rime/weasel'") {
  throw 'CI release jobs must not be restricted to rime/weasel'
}
if ($ci -notmatch 'Validate release version') {
  throw 'CI must verify release tag, VERSION and appcast consistency'
}

Write-Host 'Release metadata policy: PASS'
