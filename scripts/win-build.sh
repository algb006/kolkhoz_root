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
# publish/<Config>/{include,lib,VERSION}, so Debug and Release can sit on the
# host at once and are told apart from outside. The graphics layer takes
# Release; the Debug one is for our own runs.
#
# Usage: scripts/win-build.sh [--sync-only] [--clean] [--release]
# Host and directory come from WIN_HOST and WIN_DIR.

set -euo pipefail

host="${WIN_HOST:-win}"
remote_dir="${WIN_DIR:-/c/MyGames/Kolkhoz/core-msvc}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

sync_only=0
build_type=Debug
clean_arg=""

while [ $# -gt 0 ]; do
  case "$1" in
    --sync-only) sync_only=1 ;;
    --clean)     clean_arg="clean" ;;
    --release)   build_type=Release ;;
    *) echo "Неизвестный ключ: $1" >&2; exit 2 ;;
  esac
  shift
done

echo "==> ${host}:${remote_dir}"
ssh "${host}" "echo ok" >/dev/null

# Deliberately not -a: it implies -p -o -g, and POSIX permissions cannot be
# set on NTFS through MSYS2 — every file arrives and then fails with
# "failed to set permissions ... Permission denied". -rlt carries what the
# host actually needs, and content comparison is what --checksum is for.
rsync -rltz --checksum --delete --omit-dir-times \
      --exclude 'build/' --exclude 'build-*/' --exclude '.git/' \
      --exclude 'claude/' --exclude 'artifacts/' --exclude 'publish/' \
      "${project_dir}/" "${host}:${remote_dir}/"

if [ "${sync_only}" -eq 1 ]; then
  echo "Исходники на хосте, сборка не запускалась."
  exit 0
fi

# The host shell is MSYS2 bash, so the batch file goes through cmd. Two habits
# of that shell have to be worked around at once: //c keeps MSYS from
# rewriting the switch into a path, and the single quotes keep bash from
# eating the backslash before cmd ever sees the name.
ssh "${host}" "cd '${remote_dir}' && cmd //c 'scripts\\build-core.bat' ${build_type} ${clean_arg}"

# Not scp: with MSYS2 bash as the sshd shell the SFTP subsystem is broken
# ("Connection closed") and legacy scp -O finds no scp.exe in PATH. A cat
# over the ssh pipe needs nothing on the host and moves binaries fine.
#
# Taken from publish/, not from the build directory: the workshop holds the
# wreckage of interrupted builds, and the published copy is the one this
# project promises to anybody. Artifacts land per configuration for the same
# reason the host does — one name for two libraries is how the wrong one gets
# linked.
publish_dir="${remote_dir}/publish/${build_type}"
local_artifacts="${project_dir}/artifacts/${build_type}"
mkdir -p "${local_artifacts}"
if ssh "${host}" "test -f '${publish_dir}/lib/core.lib'"; then
  ssh "${host}" "cat '${publish_dir}/lib/core.lib'" > "${local_artifacts}/core.lib"
  published=$(ssh "${host}" "cat '${publish_dir}/VERSION'" | tr -d '\r\n')
  local_version=$(tr -d '\r\n' < "${project_dir}/VERSION")
  echo "Забрана artifacts/${build_type}/core.lib — версия ${published}"
  echo "Опубликовано на хосте: ${publish_dir}"
  # Cannot normally differ: the host built the tree that was just synced from
  # here. If it does, something wrote into the host tree behind our back, and
  # that is exactly the silent staleness the published VERSION exists to catch.
  if [ "${published}" != "${local_version}" ]; then
    echo "ВНИМАНИЕ: на хосте ${published}, в дереве ${local_version} — кто-то писал в каталог хоста мимо этого скрипта" >&2
    exit 1
  fi
else
  echo "core.lib не найдена — модулей пока нет, это ожидаемо"
fi
