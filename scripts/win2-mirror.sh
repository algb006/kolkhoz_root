#!/usr/bin/env bash
# The core's publish mirrored from the main host to the second box (#1987; the two-host review,
# accepted 22 Nov 2026; boss, core-boss #1987). `make publish` ends with it; `make mirror
# VERSION=x` runs it alone for a version already published on the main host.
#
# Usage: scripts/win2-mirror.sh <version>
# Hosts and directory: WIN_HOST (the main, default win), WIN2_HOST (default win2), WIN_DIR (the
# same path on both boxes, default /c/MyGames/Kolkhoz/core-msvc).
#
# THE STEPS, each said aloud:
# 1. A KNOWN OUTAGE of the second box (tools/host.sh outage win2 status) postpones the mirror
#    and exits 0: «ЗЕРКАЛО ОТЛОЖЕНО: авария win2». A known outage must not turn every publish
#    red; the mirror is redone with `make mirror` once the box is back.
# 2. Only a WHOLE publish is mirrored: both configurations' core.lib on the main host.
# 3. The copy goes through this machine (no key between the boxes): a tar stream from the main
#    host into publish/.incoming/<version> on the second box.
# 4. CHECKED BEFORE IT COUNTS: a sha256 manifest of every file on both sides, compared as a
#    set of names and sums. A mismatch refuses and leaves the second box as it was.
# 5. Moved into place by one mv; a version already there is replaced only after the new copy
#    has verified.
# 6. current on the second box: the main host's rule - never backwards, newest counted over
#    whole publishes only - written through a temporary file and read back.
# 7. The weeding on the second box: keep-7 whole publishes and that box's own pins - SKIPPED
#    while the second box's lock is held, said so (a running build there may read a version).
# 8. Both boxes' version lists and currents printed side by side.
# Any failure after step 1 prints «ЗЕРКАЛО НЕ СДЕЛАНО» and exits non-zero; the main host's
# publish stands either way.

set -euo pipefail

version="${1:-}"
if ! printf '%s' "${version}" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
  echo "ЗЕРКАЛО НЕ СДЕЛАНО: версия не задана или не похожа на номер («${version}»)" >&2
  exit 2
fi
main_host="${WIN_HOST:-win}"
mirror_host="${WIN2_HOST:-win2}"
remote_dir="${WIN_DIR:-/c/MyGames/Kolkhoz/core-msvc}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
host_tool="${KOLKHOZ_ROOT:-${project_dir}/..}/tools/host.sh"
kept_versions=7

fail() {
  echo "ЗЕРКАЛО НЕ СДЕЛАНО: $*" >&2
  exit 1
}

# 1. The known outage. `outage <box> status` exits 0 while an outage stands (1: none).
if [ -f "${host_tool}" ] && bash "${host_tool}" outage "${mirror_host}" status > /dev/null 2>&1; then
  echo "ЗЕРКАЛО ОТЛОЖЕНО: авария ${mirror_host} ($(bash "${host_tool}" outage "${mirror_host}" status 2>&1 | head -n 1)); повторить: make mirror VERSION=${version}"
  exit 0
fi
ssh "${mirror_host}" "echo ok" > /dev/null 2>&1 || fail "${mirror_host} не отвечает по ssh, а аварии на нём не объявлено"

# The whole-publish listing, the main host's (win-build.sh): the loop ends in `true`, since its
# last test fails on whatever is not a whole publish and pipefail would end the script.
whole_on() {
  ssh "$1" "cd '${remote_dir}/publish' 2>/dev/null && for d in *; do \
    test -f \"\$d/Debug/lib/core.lib\" && test -f \"\$d/Release/lib/core.lib\" && echo \"\$d\"; \
    done; true" | tr -d '\r' | { grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' || true; } | sort -V
}

manifest_of() {
  ssh "$1" "cd '$2' && find . -type f -print0 | sort -z | xargs -0 sha256sum" | tr -d '\r'
}

# 2. A whole publish only.
if ! whole_on "${main_host}" | grep -qxF "${version}"; then
  fail "на ${main_host} нет целой публикации ${version} (нужны Debug и Release core.lib)"
fi

# 3. The copy, into the second box's incoming folder.
incoming="${remote_dir}/publish/.incoming"
ssh "${mirror_host}" "mkdir -p '${incoming}' && rm -rf '${incoming}/${version}'" \
  || fail "не готова папка ${incoming} на ${mirror_host}"
if ! ssh "${main_host}" "tar -C '${remote_dir}/publish' -cf - '${version}'" \
  | ssh "${mirror_host}" "tar -C '${incoming}' -xf -"; then
  fail "поток tar ${main_host} → ${mirror_host} оборвался; ${incoming}/${version} не в счёт"
fi

# 4. The manifests, as sets of names and sums.
source_manifest=$(manifest_of "${main_host}" "${remote_dir}/publish/${version}")
copy_manifest=$(manifest_of "${mirror_host}" "${incoming}/${version}")
source_files=$(printf '%s\n' "${source_manifest}" | grep -c . || true)
if [ "${source_files}" -eq 0 ]; then
  fail "манифест ${version} на ${main_host} пуст — нечего сверять"
fi
if [ "${source_manifest}" != "${copy_manifest}" ]; then
  echo "Расхождения манифестов (< ${main_host}, > ${mirror_host}):" >&2
  diff <(printf '%s\n' "${source_manifest}") <(printf '%s\n' "${copy_manifest}") | head -n 20 >&2 || true
  fail "копия ${version} на ${mirror_host} не совпала с ${main_host} по sha256; на месте осталось прежнее"
fi
bytes=$(ssh "${main_host}" "cd '${remote_dir}/publish/${version}' && find . -type f -printf '%s\n'" \
  | tr -d '\r' | awk '{s += $1} END {print s + 0}')
echo "Сверено: ${source_files} файлов, ${bytes} байт, sha256 совпали на ${main_host} и ${mirror_host}"

# 5. Into place: the old copy moves aside only now, and goes once the new one stands.
ssh "${mirror_host}" "cd '${remote_dir}/publish' && \
  if [ -e '${version}' ]; then rm -rf '.incoming/old-${version}' && mv '${version}' '.incoming/old-${version}'; fi && \
  mv '.incoming/${version}' '${version}' && rm -rf '.incoming/old-${version}'" \
  || fail "не встала на место publish/${version} на ${mirror_host}"

# 6. current on the second box, never backwards over whole publishes.
newest=$(whole_on "${mirror_host}" | tail -n 1)
current_before=$(ssh "${mirror_host}" "cat '${remote_dir}/publish/current' 2>/dev/null; true" | tr -d '\r\n')
target_current="${version}"
if [ -n "${newest}" ] && [ "${newest}" != "${version}" ] \
  && [ "$(printf '%s\n%s\n' "${newest}" "${version}" | sort -V | tail -n 1)" = "${newest}" ]; then
  target_current="${current_before}"
  echo "current на ${mirror_host} НЕ сдвинут: ${version} старше ${newest}, остаётся ${target_current}"
else
  ssh "${mirror_host}" "printf '%s\n' '${version}' > '${remote_dir}/publish/.current.tmp' \
    && mv '${remote_dir}/publish/.current.tmp' '${remote_dir}/publish/current'" \
    || fail "current на ${mirror_host} не записан"
fi
current_now=$(ssh "${mirror_host}" "cat '${remote_dir}/publish/current'" | tr -d '\r\n')
if [ -n "${target_current}" ] && [ "${current_now}" != "${target_current}" ]; then
  fail "current на ${mirror_host} читается как «${current_now}», а не ${target_current}"
fi

# 7. The weeding, unless the second box's lock is held.
lock_line=$(WIN_HOST="${mirror_host}" bash "${host_tool}" status 2>&1 | head -n 1 || true)
if ! printf '%s' "${lock_line}" | grep -q "свободен"; then
  echo "Чистка на ${mirror_host} ПРОПУЩЕНА: замок занят («${lock_line}») — идущая там сборка может читать версию"
else
  kept_whole=$(whole_on "${mirror_host}" | tail -n "${kept_versions}")
  oldest_kept=$(printf '%s\n' "${kept_whole}" | head -n 1)
  pinned=$(ssh "${mirror_host}" "ls -1 '${remote_dir}/publish/.pins' 2>/dev/null; true" | tr -d '\r' \
    | sed 's/\.[^.]*$//' | sort -u)
  all_versions=$(ssh "${mirror_host}" "ls -1 '${remote_dir}/publish' 2>/dev/null; true" | tr -d '\r' \
    | { grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' || true; } | sort -V)
  for candidate in ${all_versions}; do
    if [ -z "${oldest_kept}" ] || [ "${candidate}" = "${oldest_kept}" ] \
      || [ "$(printf '%s\n%s\n' "${candidate}" "${oldest_kept}" | sort -V | head -n 1)" != "${candidate}" ]; then
      continue
    fi
    if printf '%s\n' "${pinned}" | grep -qxF "${candidate}"; then
      echo "ОСТАВЛЕНА под пином на ${mirror_host}: ${candidate}"
      continue
    fi
    if [ "${candidate}" = "${version}" ] || [ "${candidate}" = "${current_now}" ]; then
      echo "ОСТАВЛЕНА на ${mirror_host}: ${candidate} — её только что зеркалили или на неё указывает current"
      continue
    fi
    ssh "${mirror_host}" "rm -rf '${remote_dir}/publish/${candidate}'"
    echo "Убрана старая версия на ${mirror_host}: ${candidate}"
  done
fi

# 8. Side by side.
list_of() {
  ssh "$1" "ls -1 '${remote_dir}/publish' 2>/dev/null; true" | tr -d '\r' \
    | { grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' || true; } | sort -V | tr '\n' ' '
}
main_current=$(ssh "${main_host}" "cat '${remote_dir}/publish/current' 2>/dev/null; true" | tr -d '\r\n')
echo "На ${main_host}:  $(list_of "${main_host}")— current ${main_current}"
echo "На ${mirror_host}: $(list_of "${mirror_host}")— current ${current_now}"
echo "ЗЕРКАЛО СДЕЛАНО: ${version} на ${mirror_host}"
