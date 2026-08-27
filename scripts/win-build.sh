#!/usr/bin/env bash
# Builds the core on the Windows host with MSVC.
#
# Sources are pushed with rsync --checksum: timestamps drift between the VM and
# the host, content comparison does not. The host needs MSYS2 (for rsync and a
# POSIX shell behind sshd) and Visual Studio Build Tools; scripts/win-setup.ps1
# puts both in place, manual/setup/60-windows-host.md explains why.
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
      --exclude 'claude/' --exclude 'artifacts/' \
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

mkdir -p "${project_dir}/artifacts"
if scp -q "${host}:${remote_dir}/build-msvc/lib/core.lib" "${project_dir}/artifacts/" 2>/dev/null; then
  echo "Забрана artifacts/core.lib"
else
  echo "core.lib не найдена — модулей пока нет, это ожидаемо"
fi
