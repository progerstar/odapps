#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd -- "${script_dir}/.." && pwd)
source "${script_dir}/apps.sh"

build_root=${1:-"${root_dir}/build/windows-release"}
make_bin=${MAKE:-make}
jobs=${JOBS:-$(nproc)}

if ! qmake_bin=$(find_mxe_qmake "${root_dir}"); then
    printf 'MXE Qt 5 qmake was not found. Run scripts/bootstrap-mxe.sh first,\n' >&2
    printf 'or set MXE_QMAKE=/absolute/path/to/qt5/bin/qmake.\n' >&2
    exit 2
fi

mxe_usr=$(cd -- "$(dirname -- "${qmake_bin}")/../../.." && pwd)
mxe_bin="${mxe_usr}/bin"
[[ -x "${mxe_bin}/x86_64-w64-mingw32.shared-g++" ]] || {
    printf 'MXE compiler wrapper was not found: %s\n' \
        "${mxe_bin}/x86_64-w64-mingw32.shared-g++" >&2
    exit 2
}

for app in "${APP_NAMES[@]}"; do
    app_build_dir="${build_root}/${app}"
    mkdir -p "${app_build_dir}"
    printf '\n==> Building %s for Windows with %s\n' "${app}" "${qmake_bin}"
    (
        export PATH="${mxe_bin}:${PATH}"
        cd "${app_build_dir}"
        qmake_args=(BUILD_WIN64=1 CONFIG+=release CONFIG-=debug)
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

printf '\nWindows binaries:\n'
for app in "${APP_NAMES[@]}"; do
    binary="${build_root}/${app}/${WINDOWS_BINARIES[${app}]}"
    [[ -f "${binary}" ]] || { printf 'Missing binary: %s\n' "${binary}" >&2; exit 1; }
    file "${binary}"
done
