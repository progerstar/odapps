#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd -- "${script_dir}/.." && pwd)
source "${script_dir}/apps.sh"

build_root=${1:-"${root_dir}/build/linux-release"}
qmake_bin=${QMAKE:-qmake}
make_bin=${MAKE:-make}
jobs=${JOBS:-$(nproc)}

command -v "${qmake_bin}" >/dev/null
command -v "${make_bin}" >/dev/null
configure_linux_qt5_serialbus "${root_dir}" "${qmake_bin}"

for app in "${APP_NAMES[@]}"; do
    app_build_dir="${build_root}/${app}"
    mkdir -p "${app_build_dir}"
    printf '\n==> Building %s for Linux\n' "${app}"
    (
        cd "${app_build_dir}"
        qmake_args=(CONFIG+=release CONFIG-=debug)
        if [[ "${app}" == odhiddfu ]]; then
            qmake_args+=(DEFINES+=OD_NO_DEVELOPER)
        fi
        "${qmake_bin}" "${root_dir}/${APP_PROJECTS[${app}]}" "${qmake_args[@]}"
        if [[ "${app}" == odhiddfu ]]; then
            "${make_bin}" clean
        fi
        "${make_bin}" -j"${jobs}"
    )
done

printf '\nLinux binaries:\n'
for app in "${APP_NAMES[@]}"; do
    binary="${build_root}/${app}/${LINUX_BINARIES[${app}]}"
    [[ -x "${binary}" ]] || { printf 'Missing binary: %s\n' "${binary}" >&2; exit 1; }
    file "${binary}"
done
