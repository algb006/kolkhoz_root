#!/usr/bin/env bash
# Bumps the core library version in VERSION.
#
# Called as the closing step of a delivery cycle, in the same commit as the work
# being delivered — never as a separate "bump version" commit, which would
# detach the number from what it names. Rules: manual/setup/57-versioning.md.
#
#   scripts/bump_version.sh patch     module delivered
#   scripts/bump_version.sh minor     stage delivered, or the boundary to UE broke
#   scripts/bump_version.sh patch --dry-run
#
# There is no major bump. The major is frozen at zero until the game ships (the
# human's rule, 2026-09-01): a breaking change to the boundary is a minor, the
# way 0.y.z is read everywhere. The check lives here and not only in the manual
# because a rule kept on paper alone is broken silently.
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
    patch|minor) ;;
    major)
        echo "ОТКАЗ: major заморожен на нуле, пока идёт разработка игры." >&2
        echo "       Слом границы с UE — это minor: manual/setup/57-versioning.md §2." >&2
        exit 1
        ;;
    *) echo "usage: bump_version.sh {patch|minor} [--dry-run]" >&2; exit 2 ;;
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

if [ "$major" -ne 0 ]; then
    echo "VERSION: major = $major, а он должен быть нулём до выхода игры" >&2
    echo "         (manual/setup/57-versioning.md §2). Откати номер, потом бампай." >&2
    exit 1
fi

case "$level" in
    # Lower components reset: 0.1.3 -> 0.2.0, not 0.2.3. A stage hand-over does
    # not inherit the patch count of the modules inside it.
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

# A minor may mean the core/presentation boundary broke — the number no longer
# separates that from a delivered stage. Say it out loud either way: the UE side
# pins the full string and has to move its pin and rebuild.
if [ "$level" = minor ]; then
    echo "ВНИМАНИЕ: слой графики пришпилен к полной строке — подвинь ue/CORE_VERSION и пересобери."
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
