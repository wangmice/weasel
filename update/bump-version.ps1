#!/usr/bin/env pwsh
param( [string]$tag, [string]$basetag, [switch]$updatelog, [switch]$h)

function faild_exit {
  Write-Host
  Write-Host "Orz :( bump version failed!"
  Write-Host "we will revert the modification by command git checkout . "
  Write-Host
  & git checkout .
  exit 1
}

function write_utf8_no_bom {
  param([string]$filePath, [string]$content)
  [System.IO.File]::WriteAllText(
    $filePath,
    $content,
    [System.Text.UTF8Encoding]::new($false))
}

function get_old_version {
  $version = (Get-Content -Path "VERSION" -Raw -Encoding UTF8).Trim()
  if ($version -notmatch '^\d+\.\d+\.\d+$') {
    Write-Host "VERSION does not contain a valid MAJOR.MINOR.PATCH version: $version"
    faild_exit
  }
  return $version
}

function update_changelog {
  param([string]$new_tag, [string]$old_tag, [string]$repo_url)

  $commits = git log "$old_tag..HEAD" --pretty=format:"%s ([%an]($repo_url/commit/%H))"
  $groupedCommits = @{
    build = @(); ci = @(); fix = @(); feat = @(); docs = @(); style = @();
    refactor = @(); test = @(); chore = @(); commit = @();
  }
  $groupedTitle = @{
    build = "Builds";
    ci = "Continuous Integration";
    fix = "Bug Fixes";
    feat = "Features";
    docs = "Documents";
    style = "Code Style";
    refactor = "Code Refactor";
    test = "Tests";
    chore = "Chores";
    commit = "Commits";
  }

  foreach ($commit in $commits) {
    if ($commit -match "^((?i)(build|ci|fix|feat|docs|style|refactor|test|chore)(\([^\)]+\))?):") {
      $prefix = $matches[2].ToLower()
      $groupedCommits[$prefix] += $commit
    } else {
      $groupedCommits["commit"] += $commit
    }
  }

  $changelog = ""
  foreach ($group in $groupedCommits.Keys) {
    if ($groupedCommits[$group].Count -gt 0) {
      $changelog += "`n#### " + $groupedTitle[$group] + "`n"
      foreach ($commit in $groupedCommits[$group]) {
        $msg = $commit -replace "^" + $group + ":", ''
        $changelog += "* $msg`n"
      }
    }
  }

  $currentDateTime = Get-Date -Format "yyyy-MM-dd"
  $fileContent = Get-Content -Path "CHANGELOG.md" -Raw -Encoding UTF8
  $contentAdd = "<a name=`"$new_tag`"></a>`n"
  $contentAdd += "## [$new_tag]($repo_url/compare/$old_tag...$new_tag)($currentDateTime)`n"

  $new_tag_file = "$new_tag.txt"
  if (Test-Path -Path $new_tag_file) {
    $new_tag_content = Get-Content -Path $new_tag_file -Raw -Encoding UTF8
    $contentAdd += "`n" + $new_tag_content + "`n"
  }

  $contentAdd += $changelog
  Write-Host "`n" + $contentAdd + "`n"
  write_utf8_no_bom -filePath "CHANGELOG.md" -content ($contentAdd + "`n" + $fileContent)
}

function replace_str {
  param([string]$filePath, [string]$pat_orig, [string]$pat_replace)
  $fileContent = Get-Content -Path $filePath -Raw -Encoding UTF8
  $fileContent = [regex]::Replace($fileContent, $pat_orig, $pat_replace)
  write_utf8_no_bom -filePath $filePath -content $fileContent
}

function update_appcast {
  param([string]$filePath, [string]$version, [string]$repo_url)

  $currentDateTime = (Get-Date).ToString(
    "ddd, dd MMM yyyy HH:mm:ss zz00",
    [System.Globalization.CultureInfo]::InvariantCulture)

  replace_str -filePath $filePath -pat_orig '(?<=<title>小狼毫 )\d+\.\d+\.\d+(?=</title>)' -pat_replace $version
  replace_str -filePath $filePath -pat_orig '(?<=<link>)[^<]*(?=</link>)' -pat_replace "$repo_url/releases"
  replace_str -filePath $filePath -pat_orig '(?<=<sparkle:releaseNotesLink>)[^<]*(?=</sparkle:releaseNotesLink>)' -pat_replace "$repo_url/releases/tag/$version"
  replace_str -filePath $filePath -pat_orig '(?<=<enclosure url=")[^"]*(?=")' -pat_replace "$repo_url/releases/download/$version/weasel-$version.0-installer.exe"
  replace_str -filePath $filePath -pat_orig '(?<=sparkle:version=")[^"]*(?=")' -pat_replace $version
  replace_str -filePath $filePath -pat_orig '(?<=<pubDate>).*?(?=</pubDate>)' -pat_replace $currentDateTime

  Write-Host "$filePath updated"
}

function is_weasel_root {
  return (Test-Path -Path "weasel.sln") -and
         (Test-Path -Path ".git") -and
         (Test-Path -Path "VERSION")
}

if ($h -or (-not $tag)) {
  $info = @(
    "-tag [new_tag]`n`tnew tag shall be in format of ^\d+\.\d+\.\d+$ and greater than the current VERSION",
    "-basetag [basetag name]`n`tbase tag for changelog generation; defaults to the current VERSION",
    "-updatelog`n`tupdate CHANGELOG.md if this switch is used",
    "-h`n`tshow help info"
  )
  Write-Host ($info -join "`n")
  exit 0
}

if (-not (is_weasel_root)) {
  Write-Host "Current directory is: $(Get-Location), it's not the weasel root directory"
  exit 1
}

if ($tag -notmatch '^\d+\.\d+\.\d+$') {
  Write-Host "tag name not match rule '^\d+\.\d+\.\d+$'"
  exit 1
}

$old_version = get_old_version
if (-not $basetag) {
  $basetag = $old_version
}
if ($basetag -notmatch '^\d+\.\d+\.\d+$') {
  Write-Host "basetag format is not correct, it shall be in format of '^\d+\.\d+\.\d+$'"
  exit 1
}

$new_semver = [version]$tag
$old_semver = [version]$old_version
if ($new_semver -le $old_semver) {
  Write-Host "$tag must be newer than $old_version"
  exit 1
}

$repository = if ($env:WEASEL_REPOSITORY) {
  $env:WEASEL_REPOSITORY
} elseif ($env:GITHUB_REPOSITORY) {
  $env:GITHUB_REPOSITORY
} else {
  "wangmice/weasel"
}
$repo_url = "https://github.com/$repository"

Write-Host "Bumping version from $old_version to $tag ..."
write_utf8_no_bom -filePath "VERSION" -content "$tag`n"

update_appcast -filePath "update/appcast.xml" -version $tag -repo_url $repo_url
if (-not $?) { faild_exit }
update_appcast -filePath "update/testing-appcast.xml" -version $tag -repo_url $repo_url
if (-not $?) { faild_exit }

if ($updatelog) {
  update_changelog -new_tag $tag -old_tag $basetag -repo_url $repo_url
  if (-not $?) { faild_exit }
  & git add CHANGELOG.md
  Write-Host "CHANGELOG.md updated"
}

$release_message = "chore(release): $tag :tada:"
& git add VERSION update/*.xml
& git commit --message $release_message
if ($LASTEXITCODE -ne 0) { faild_exit }
& git tag --annotate $tag --message $release_message
if ($LASTEXITCODE -ne 0) { faild_exit }
