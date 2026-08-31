#!/usr/bin/env bash
# Bumps the core library version in VERSION.
#
# Called as the closing step of a delivery cycle, in the same commit as the work
# being delivered — never as a separate "bump version" commit, which would
# detach the number from what it names. Rules: manual/setup/57-versioning.md.
#
#   scripts/bump_version.sh patch     module delivered
#   scripts/bump_version.sh minor     stage of the phase plan delivered
#   scripts/bump_version.sh major     the boundary to UE broke
#   scripts/bump_version.sh patch --dry-run
#
# VERSION_SAVE is deliberately NOT touched here. It is not a delivery number: a
# bump means existing saves stopped opening, which is an event to announce, not
# a side effect of handing over a module.
#
# Exit: 0 bumped (or previewed), 1 refused, 2 wrong invocation.

set -uo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version_file="$project_dir/VERSION"

level="${1:-}"
dry_run=0
[ "${2:-}" = "--dry-run" ] && dry_run=1

case "$level" in
    patch|minor|major) ;;
    *) echo "usage: bump_version.sh {patch|minor|major} [--dry-run]" >&2; exit 2 ;;
esac

[ -f "$version_file" ] || { echo "НЕТ ФАЙЛА: $version_file" >&2; exit 1; }

current="$(tr -d '[:space:]' < "$version_file")"
if ! [[ "$current" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    echo "VERSION испорчен: '$current' — ожидается major.minor.patch" >&2
    exit 1
fi
major="${BASH_REMATCH[1]}"
minor="${BASH_REMATCH[2]}"
patch="${BASH_REMATCH[3]}"

case "$level" in
    # Lower components reset: 0.1.3 -> 0.2.0, not 0.2.3. A stage hand-over does
    # not inherit the patch count of the modules inside it.
    major) major=$((major + 1)); minor=0; patch=0 ;;
    minor) minor=$((minor + 1)); patch=0 ;;
    patch) patch=$((patch + 1)) ;;
esac
next="$major.$minor.$patch"

if [ "$dry_run" -eq 1 ]; then
    echo "БЫЛО $current  ->  СТАЛО $next   ($level, ничего не записано)"
    exit 0
fi

printf '%s\n' "$next" > "$version_file"
echo "VERSION: $current -> $next   ($level)"

# Bumping major means the core/presentation boundary broke. Say it out loud:
# the UE side has to be rebuilt against the new contract.
if [ "$level" = major ]; then
    echo "ВНИМАНИЕ: major означает сломанную границу с UE — слой графики надо пересобрать."
fi
echo "Заголовок core_common/version.h перегенерируется при следующей настройке."

# The published library on the Windows host now carries a number this tree no
# longer has, and the consumer's version pin is what discovers it — an hour
# later, in somebody else's build. That happened once (1.0.0, 2026-08-31): the
# delivery cycle checks the MSVC build in DEBUG, while the graphics layer takes
# the RELEASE publication, and the two are different commands. The divergence
# is created here, in one second, so it is named here.
if [ -f "$project_dir/artifacts/Release/core.lib" ]; then
    echo "НАПОМИНАНИЕ: на хосте опубликована $current — выложи 'make win-release',"
    echo "             иначе слой графики упрётся в несовпадение версий."
fi
