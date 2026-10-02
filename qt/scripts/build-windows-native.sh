#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd -- "${script_dir}/.." && pwd)
source "${script_dir}/apps.sh"

build_root=${1:-"${root_dir}/build/windows-native-release"}

if [[ -n "${QMAKE:-}" ]]; then
    qmake_bin=${QMAKE}
else
    qmake_bin=$(find_first_command qmake-qt5 qmake) || {
        printf '%s\n' 'Qt 5 qmake was not found in PATH.' >&2
        exit 2
    }
fi

if [[ -n "${MAKE:-}" ]]; then
    make_bin=${MAKE}
else
    make_bin=$(find_first_command mingw32-make make) || {
        printf '%s\n' 'A MinGW make executable was not found in PATH.' >&2
        exit 2
    }
fi

jobs=${JOBS:-$(nproc 2>/dev/null || printf '%s' "${NUMBER_OF_PROCESSORS:-4}")}

command -v "${qmake_bin}" >/dev/null
command -v "${make_bin}" >/dev/null

qt_version=$("${qmake_bin}" -query QT_VERSION)
[[ "${qt_version}" == 5.* ]] || {
    printf 'Qt 5 is required; %s reports Qt %s.\n' "${qmake_bin}" "${qt_version}" >&2
    exit 2
}

case "${MSYSTEM:-}" in
    UCRT64|MINGW64|CLANG64) ;;
    *)
        printf '%s\n' 'This script must run in a 64-bit MSYS2 MinGW environment.' >&2
        exit 2
        ;;
esac

for app in "${APP_NAMES[@]}"; do
    app_build_dir="${build_root}/${app}"
    mkdir -p "${app_build_dir}"
    printf '\n==> Building %s for native Windows with %s\n' "${app}" "${qmake_bin}"
    (
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
