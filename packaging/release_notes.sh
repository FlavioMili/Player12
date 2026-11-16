#!/usr/bin/env bash
# -----------------------------------------------------------------------------
#  Football Management Project
#  Prints the CHANGELOG.md section of a version, for the GitHub Release body.
#
#  Usage: release_notes.sh <version> [changelog]
#  The section starts at a "## v<version>" heading (any suffix, such as a date
#  or "(unreleased)") and ends before the next "## " heading. A pre-release
#  (1.0.0-rc1) falls back to the section of its final version. Exits with 1
#  when the changelog has no such section.
# -----------------------------------------------------------------------------
set -euo pipefail

version=${1:?usage: release_notes.sh <version> [changelog]}
version=${version#v}
changelog=${2:-$(cd "$(dirname "$0")/.." && pwd)/CHANGELOG.md}

section() {
  local section_version=$1
  awk -v version="$section_version" '
    BEGIN { heading = "^## v?" version "( |\t|$)"; gsub(/\./, "\\.", heading) }
    /^## / { if (found) exit; if ($0 ~ heading) { found = 1; next } }
    found { print }
    END { exit found ? 0 : 1 }
  ' "$changelog"
}

notes=$(section "$version" || section "${version%%-*}") || {
  echo "release_notes.sh: no '## v$version' section in $changelog" >&2
  exit 1
}
# Trim leading and trailing blank lines.
printf '%s\n' "$notes" | sed -e '/./,$!d' | sed -e ':a' -e '/^\n*$/{$d;N;ba' -e '}'
