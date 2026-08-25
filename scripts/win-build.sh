#!/usr/bin/env bash
# Builds the core on the Windows host with MSVC.
#
# Sources are pushed with rsync --checksum: timestamps drift between the VM and
# the host, content comparison does not. build-core.bat must exist on the host
# and set up the MSVC environment through vcvarsall before calling CMake.
#
# Usage: scripts/win-build.sh [host] [remote-dir]

set -euo pipefail

host="${1:-${WIN_HOST:-win}}"
remote_dir="${2:-${WIN_DIR:-/c/dev/core-msvc}}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "==> ${host}:${remote_dir}"
ssh "${host}" "echo ok" >/dev/null

rsync -az --checksum --delete \
      --exclude 'build/' --exclude 'build-*/' --exclude '.git/' \
      --exclude 'claude/' --exclude 'artifacts/' \
      "${project_dir}/" "${host}:${remote_dir}/"

ssh "${host}" "build-core.bat"

mkdir -p "${project_dir}/artifacts"
scp "${host}:${remote_dir}/build/lib/core.lib" "${project_dir}/artifacts/" || \
  echo "core.lib не найдена — модулей пока нет, это ожидаемо"
