#!/usr/bin/env bash
# Builds the core on the Windows host with MSVC.
#
# Sources are pushed with rsync --checksum: timestamps drift between the VM and
# the host, content comparison does not. The host needs MSYS2 (for rsync and a
# POSIX shell behind sshd) and Visual Studio Build Tools; scripts/win-setup.ps1
# puts both in place, manual/setup/60-windows-host.md explains why.
#
# What comes back is the PUBLISHED library, not the build directory's: each
# configuration builds into build-msvc-<Config> and publishes into
# publish/<version>/<Config>/{include,lib,VERSION,LAYOUT.txt}. The graphics
# layer takes Release; the Debug one is for our own runs.
#
# THE VERSION IS STORED, NOT REPLACED (manual/technical/50-architecture.md,
# "Контур поставки"). A slot per configuration, rewritten by every build, is
# a moving target: a consumer pinned to a number had nothing to pin to, and
# "what is published" meant "whatever ran last". A directory per version
# keeps the previous one standing while the next appears.
#
#     publish/0.17.0/{Debug,Release}/{include,lib,VERSION,LAYOUT.txt}
#     publish/0.17.0/tables/           <- beside the configurations, not in them
#     publish/0.16.0/...
#     publish/current      <- a FILE holding "0.17.0"
#
# current IS A FILE AND NOT A LINK, and that is the point of it. A link would
# be the same overwriting one level up: whoever linked against publish/current
# would be building against a moving target again — exactly what the contour
# exists to prevent. As a file it still answers the human's question (cat it),
# and a build PHYSICALLY CANNOT go through it, because there are no headers
# and no library there. A prohibition that cannot be broken is cheaper than
# one that has to be remembered (boss, 2026-09-04).
#
# --both is the publishing command, and the only one. It builds both
# configurations of one version and writes current only when BOTH are in
# place; a single-configuration run is a compile check that publishes nothing
# the pointer names. Half a version is therefore not something to guard
# against — it is something nothing can point at.
#
# Usage: scripts/win-build.sh [--sync-only] [--clean] [--release] [--both] [--dirty]
# Host and directory come from WIN_HOST and WIN_DIR.

set -euo pipefail

host="${WIN_HOST:-win}"
remote_dir="${WIN_DIR:-/c/MyGames/Kolkhoz/core-msvc}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

sync_only=0
build_type=Debug
clean_arg=""
allow_dirty=0
both=0

# How many versions stay on the host. Two: the one in use and the one to fall
# back to. A third is not a safety net, it is a museum — and the host's disk
# is shared with the editor's cooked content.
kept_versions=2

while [ $# -gt 0 ]; do
  case "$1" in
    --sync-only) sync_only=1 ;;
    --clean)     clean_arg="clean" ;;
    --dirty)     allow_dirty=1 ;;
    --release)   build_type=Release ;;
    --both)      both=1 ;;
    *) echo "Неизвестный ключ: $1" >&2; exit 2 ;;
  esac
  shift
done

version=$(tr -d '\r\n' < "${project_dir}/VERSION")
if [ -z "${version}" ]; then
  echo "ОТКАЗ: файл VERSION пуст — публиковать не под каким номером." >&2
  exit 1
fi

echo "==> ${host}:${remote_dir}"
ssh "${host}" "echo ok" >/dev/null

# ONLY A DELIVERED COMMIT IS PUBLISHED (boss's rule of 2026-09-03, and the
# reason is a day of somebody else's time). A publish carries a VERSION, and
# that number has to mean the same thing everywhere it is read; published
# from a working tree it means "whatever core happened to have open". It
# happened: 0.13.0 went out with an unfinished task A5 in it, the host
# crashed under MSVC before its first step, and nobody could tell from the
# outside that the number was lying.
#
# A dirty tree is the machine-checkable half of "delivered", so that is what
# is refused. Publishing an older commit is done by extracting it and running
# this script from there (git archive <sha> | tar -x -C <dir>), which is a
# clean tree by construction — and outside a repository the check has nothing
# to say and stands aside.
if git -C "${project_dir}" rev-parse --git-dir >/dev/null 2>&1; then
  if [ -n "$(git -C "${project_dir}" status --porcelain)" ] && [ "${allow_dirty}" -eq 0 ]; then
    echo "ОТКАЗ: дерево грязное — в publish уходит только сданный коммит." >&2
    echo "       Выложить незакоммиченное: --dirty. Выложить прошлую сдачу:" >&2
    echo "       git archive <sha> | tar -x -C ~/claudetmp/<dir> и запустить оттуда." >&2
    exit 1
  fi
fi

# Deliberately not -a: it implies -p -o -g, and POSIX permissions cannot be
# set on NTFS through MSYS2 — every file arrives and then fails with
# "failed to set permissions ... Permission denied". Content comparison is
# what --checksum is for.
#
# AND DELIBERATELY NOT -t EITHER, which is the harder-won half. Preserving
# the VM's modification times let a file arrive OLDER than the object Ninja
# had already built from its previous content — and Ninja, which compares
# times, then skipped the compile. The publish still copied include/ fresh,
# so the headers moved and the archive did not: task A3's FieldRow was 56
# bytes by the published headers and 40 in the published library, every
# version string on both sides said 0.13.0, and the graphics layer read the
# field table as confident nonsense (boss, 2026-09-03). Without -t every
# transferred file lands with the host's current time, which is always newer
# than anything built from its predecessor, and Ninja rebuilds what changed.
# The mtimes on the host mean "when this arrived", which is the only thing
# they can honestly mean across two machines.
#
# .cache/ is excluded for the same reason build/ is, and it bit us: clangd
# keeps its index there, and rsync creating those directories on NTFS hit
# exactly the permission error the flags above are chosen to avoid — a
# whole publish failing over an editor's cache that the host never needed.
rsync -rlz --checksum --delete --omit-dir-times \
      --exclude 'build/' --exclude 'build-*/' --exclude '.git/' \
      --exclude 'claude/' --exclude 'artifacts/' --exclude 'publish/' \
      --exclude '.cache/' \
      "${project_dir}/" "${host}:${remote_dir}/"

if [ "${sync_only}" -eq 1 ]; then
  echo "Исходники на хосте, сборка не запускалась."
  exit 0
fi

# The host shell is MSYS2 bash, so the batch file goes through cmd. Two habits
# of that shell have to be worked around at once: //c keeps MSYS from
# rewriting the switch into a path, and the single quotes keep bash from
# eating the backslash before cmd ever sees the name.
run_build() {
  ssh "${host}" "cd '${remote_dir}' && cmd //c 'scripts\\build-core.bat' $1 ${clean_arg}"
}

# Not scp: with MSYS2 bash as the sshd shell the SFTP subsystem is broken
# ("Connection closed") and legacy scp -O finds no scp.exe in PATH. A cat
# over the ssh pipe needs nothing on the host and moves binaries fine.
#
# Taken from publish/, not from the build directory: the workshop holds the
# wreckage of interrupted builds, and the published copy is the one this
# project promises to anybody. Artifacts land per configuration for the same
# reason the host does — one name for two libraries is how the wrong one gets
# linked.
fetch_artifacts() {
  local config="$1"
  local publish_dir="${remote_dir}/publish/${version}/${config}"
  local local_artifacts="${project_dir}/artifacts/${config}"
  mkdir -p "${local_artifacts}"
  if ! ssh "${host}" "test -f '${publish_dir}/lib/core.lib'"; then
    echo "core.lib не найдена — модулей пока нет, это ожидаемо"
    return 0
  fi
  ssh "${host}" "cat '${publish_dir}/lib/core.lib'" > "${local_artifacts}/core.lib"
  local published
  published=$(ssh "${host}" "cat '${publish_dir}/VERSION'" | tr -d '\r\n')
  # The layout report the host just produced, brought back beside the library
  # so that a mismatched pair is visible from this side too.
  if ssh "${host}" "test -f '${publish_dir}/LAYOUT.txt'"; then
    ssh "${host}" "cat '${publish_dir}/LAYOUT.txt'" | tr -d '\r' \
      > "${local_artifacts}/LAYOUT.txt"
  fi
  echo "Забрана artifacts/${config}/core.lib — версия ${published}"
  echo "Опубликовано на хосте: ${publish_dir}"
  # Cannot normally differ: the host built the tree that was just synced from
  # here, and the directory is NAMED by this tree's version. If it does, the
  # host wrote into that path behind our back.
  if [ "${published}" != "${version}" ]; then
    echo "ВНИМАНИЕ: в каталоге ${version} лежит VERSION ${published} — кто-то писал в каталог хоста мимо этого скрипта" >&2
    return 1
  fi
}

if [ "${both}" -eq 0 ]; then
  # A single configuration is a COMPILE CHECK. It fills its half of
  # publish/<version>/ so the artifact comes from the same place as always,
  # but it does not touch current — so nothing that anybody reads has moved.
  run_build "${build_type}"
  fetch_artifacts "${build_type}"
  echo "Одна конфигурация: publish/current не тронут — выкладывает только --both."
  exit 0
fi

# --- the publish, and it is all or nothing ---------------------------------
#
# Both configurations, then the pointer. If the second one fails the first is
# already on disk under publish/<version>/, and that is deliberate: it costs
# a rebuild to finish, not a rebuild to redo. What it is NOT is published,
# because current still names the previous version and nothing reads a
# directory it does not name.
for config in Debug Release; do
  if ! run_build "${config}"; then
    echo "ОТКАЗ: ${config} не собралась — publish/current не тронут, выложена ноль конфигураций." >&2
    exit 1
  fi
done
for config in Debug Release; do
  if ! ssh "${host}" "test -f '${remote_dir}/publish/${version}/${config}/lib/core.lib'"; then
    echo "ОТКАЗ: ${config} собралась, но библиотека не выложена — половину не публикуем." >&2
    exit 1
  fi
done
# A library without its tables computes nothing, so the tables are part of the
# delivery and not a separate errand. They sit BESIDE the configurations —
# publish/<version>/tables — because they do not depend on Debug or Release,
# and one copy cannot disagree with another. Checked here for the same reason
# the libraries are: what current names has to be whole.
if ! ssh "${host}" "test -d '${remote_dir}/publish/${version}/tables'"; then
  echo "ОТКАЗ: библиотеки на месте, а таблиц нет — без них ядро не считает ничего." >&2
  exit 1
fi
for config in Debug Release; do
  fetch_artifacts "${config}"
done

# The pointer moves LAST, and only now: everything it can be asked about is
# on disk. Written through a temporary file so that a dropped connection
# leaves the old pointer intact rather than an empty one.
ssh "${host}" "printf '%s\n' '${version}' > '${remote_dir}/publish/.current.tmp' \
  && mv '${remote_dir}/publish/.current.tmp' '${remote_dir}/publish/current'"
echo "publish/current → ${version}"

# And the weeding. Sorted by version, not by date: a republished older number
# would otherwise outlive the newer one it cannot replace. `sort -V` gives
# semantic order, and only directories that look like versions are touched —
# current is a file, and anything a human left there by hand is not a number.
old_versions=$(ssh "${host}" "ls -1 '${remote_dir}/publish' 2>/dev/null" | tr -d '\r' \
  | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | head -n -"${kept_versions}")
if [ -n "${old_versions}" ]; then
  for old in ${old_versions}; do
    ssh "${host}" "rm -rf '${remote_dir}/publish/${old}'"
    echo "Убрана старая версия: ${old}"
  done
fi

kept=$(ssh "${host}" "ls -1 '${remote_dir}/publish' 2>/dev/null" | tr -d '\r' \
  | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | tr '\n' ' ')
echo "На хосте: ${kept}— current ${version}"
