#!/usr/bin/env bash
# Checks the rule "one module — one unit test": every folder under subprojects/
# that declares a module must have a matching tests/unit/<module>/ folder.
#
# A module is a .h + .cpp pair with a CMakeLists.txt; the unit test is not
# optional and is run when the module is handed over. See
# manual/setup/56-static-analysis-and-tests.md.
#
# Exit: 0 every module covered, 1 gaps found, 2 wrong invocation.
#
# Usage: scripts/check_module_tests.sh [--quiet]

set -uo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
quiet=0
[ "${1:-}" = "--quiet" ] && quiet=1

modules_dir="$project_dir/subprojects"
units_dir="$project_dir/tests/unit"

missing=()
orphans=()
covered=0

shopt -s nullglob
for module_path in "$modules_dir"/*/; do
    module="$(basename "$module_path")"
    [ -f "$module_path/CMakeLists.txt" ] || continue
    if [ -f "$units_dir/$module/CMakeLists.txt" ]; then
        covered=$((covered + 1))
    else
        missing+=("$module")
    fi
done

# A unit test without a module is just as wrong: the module was renamed or
# removed and its test now covers nothing.
for unit_path in "$units_dir"/*/; do
    unit="$(basename "$unit_path")"
    [ -f "$unit_path/CMakeLists.txt" ] || continue
    [ -f "$modules_dir/$unit/CMakeLists.txt" ] || orphans+=("$unit")
done
shopt -u nullglob

if [ $quiet -eq 0 ]; then
    echo "Модулей с тестом : $covered"
    echo "Без теста        : ${#missing[@]}"
    echo "Тестов без модуля: ${#orphans[@]}"
fi

rc=0
if [ ${#missing[@]} -gt 0 ]; then
    echo ""
    echo "НЕТ unit-теста — модуль сдавать нельзя:"
    for m in "${missing[@]}"; do
        echo "  $m   -> завести tests/unit/$m/CMakeLists.txt с kolkhoz_add_unit_test($m ...)"
    done
    rc=1
fi
if [ ${#orphans[@]} -gt 0 ]; then
    echo ""
    echo "Тест есть, а модуля нет — переименован или удалён:"
    for o in "${orphans[@]}"; do
        echo "  tests/unit/$o"
    done
    rc=1
fi

[ $rc -eq 0 ] && [ $quiet -eq 0 ] && echo "" && echo "Правило соблюдено."
exit $rc
