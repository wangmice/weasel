#!/usr/bin/env bash

show_help() {
  echo "-tag [new_tag]"
  echo "    new tag shall be in format of ^\\d+\\.\\d+\\.\\d+$ and greater than the current VERSION"
  echo "-basetag [basetag name]"
  echo "    base tag for changelog generation; defaults to the current VERSION"
  echo "-updatelog"
  echo "    update CHANGELOG.md if this switch is used"
  echo "-h"
  echo "    show help info"
}

faild_exit() {
  echo
  echo "Orz :( bump version failed!"
  echo "we will revert the modification by command git checkout . "
  echo
  git checkout .
  exit 1
}

get_old_version() {
  local version
  version=$(tr -d '\r\n' < VERSION)
  if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "VERSION does not contain a valid MAJOR.MINOR.PATCH version: $version" >&2
    return 1
  fi
  printf '%s\n' "$version"
}

update_changelog() {
  local new_tag=$1
  local old_tag=$2
  local repo_url=$3
  local commits
  commits=$(git log "$old_tag..HEAD" --pretty=format:"%s ([%an]($repo_url/commit/%H))") || return 1

  declare -A groupedCommits
  declare -A groupedTitle
  groupedTitle=(
    [build]="Builds"
    [ci]="Continuous Integration"
    [fix]="Bug Fixes"
    [feat]="Features"
    [docs]="Documents"
    [style]="Code Style"
    [refactor]="Code Refactor"
    [test]="Tests"
    [chore]="Chores"
    [commit]="Commits"
  )

  shopt -s nocasematch
  while IFS= read -r commit; do
    if [[ $commit =~ ^(build|ci|fix|feat|docs|style|refactor|test|chore)(\([^\)]+\))?: ]]; then
      prefix=${BASH_REMATCH[1],,}
      groupedCommits[$prefix]+="$commit"$'\n'
    else
      groupedCommits[commit]+="$commit"$'\n'
    fi
  done <<< "$commits"
  shopt -u nocasematch

  local changelog=""
  local group
  for group in "${!groupedCommits[@]}"; do
    if [[ -n ${groupedCommits[$group]} ]]; then
      changelog+=$'\n'"#### ${groupedTitle[$group]}"$'\n'
      changelog+="${groupedCommits[$group]}"
    fi
  done

  local currentDateTime
  currentDateTime=$(date "+%Y-%m-%d")
  local fileContent
  fileContent=$(<CHANGELOG.md)
  local contentAdd
  contentAdd="<a name=\"$new_tag\"></a>"$'\n'
  contentAdd+="## [$new_tag]($repo_url/compare/$old_tag...$new_tag)($currentDateTime)"$'\n'

  if [[ -f "$new_tag.txt" ]]; then
    contentAdd+=$'\n'
    contentAdd+=$(<"$new_tag.txt")
    contentAdd+=$'\n'
  fi

  contentAdd+="$changelog"
  contentAdd+=$'\n'
  printf '%s%s' "$contentAdd" "$fileContent" > CHANGELOG.md || return 1
  echo "CHANGELOG.md updated"
}

update_appcast() {
  local file=$1
  local version=$2
  local repo_url=$3
  local currentDateTime=$4

  sed -E -i "s|(<title>小狼毫 )[0-9]+\.[0-9]+\.[0-9]+(</title>)|\\1$version\\2|" "$file" || return 1
  sed -E -i "s|(<link>)[^<]*(</link>)|\\1$repo_url/releases\\2|" "$file" || return 1
  sed -E -i "s|(<sparkle:releaseNotesLink>)[^<]*(</sparkle:releaseNotesLink>)|\\1$repo_url/releases/tag/$version\\2|" "$file" || return 1
  sed -E -i "s|(enclosure url=\")[^\"]*(\")|\\1$repo_url/releases/download/$version/weasel-$version.0-installer.exe\\2|" "$file" || return 1
  sed -E -i "s|(sparkle:version=\")[^\"]*(\")|\\1$version\\2|" "$file" || return 1
  sed -E -i "s|(<pubDate>)[^<]*(</pubDate>)|\\1$currentDateTime\\2|" "$file" || return 1
  echo "$file updated"
}

if [[ $# -eq 0 ]]; then
  show_help
  exit 0
fi

while [[ $# -gt 0 ]]; do
  case $1 in
    -tag)
      tag="$2"
      shift
      ;;
    -basetag)
      basetag="$2"
      shift
      ;;
    -updatelog)
      updatelog=true
      ;;
    -h)
      show_help
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
  shift
done

if ! [[ -f weasel.sln && -d .git && -f VERSION ]]; then
  echo "Current directory is: $(pwd), it's not the weasel root directory"
  exit 1
fi

if [[ ! $tag =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Tag name does not match rule '^\\d+\\.\\d+\\.\\d+$'"
  exit 1
fi

old_version=$(get_old_version) || exit 1
if [[ -z $basetag ]]; then
  basetag=$old_version
fi
if [[ ! $basetag =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Basetag format is not correct, it shall be in format of '^\\d+\\.\\d+\\.\\d+$'"
  exit 1
fi

if [[ $tag == "$old_version" ]]; then
  echo "The new tag: $tag is the same as the old one: $old_version"
  exit 1
fi
if [[ $(printf '%s\n%s\n' "$old_version" "$tag" | sort -V | head -n 1) != "$old_version" ]]; then
  echo "$tag is older than $old_version, please recheck your target version"
  exit 1
fi

repository=${WEASEL_REPOSITORY:-${GITHUB_REPOSITORY:-wangmice/weasel}}
repo_url="https://github.com/$repository"
currentDateTime=$(date --rfc-2822)

echo "Bumping version from $old_version to $tag ..."
printf '%s\n' "$tag" > VERSION || faild_exit

update_appcast "./update/appcast.xml" "$tag" "$repo_url" "$currentDateTime" || faild_exit
update_appcast "./update/testing-appcast.xml" "$tag" "$repo_url" "$currentDateTime" || faild_exit

if [[ $updatelog ]]; then
  update_changelog "$tag" "$basetag" "$repo_url" || faild_exit
  git add CHANGELOG.md
fi

release_message="chore(release): $tag :tada:"
git add VERSION update/*.xml
git commit -m "$release_message" || faild_exit
git tag -a "$tag" -m "$release_message" || faild_exit
