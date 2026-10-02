#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd -- "${script_dir}/.." && pwd)
source "${script_dir}/apps.sh"

build_root=${1:-"${root_dir}/build/macos-release"}

if [[ -n "${QMAKE:-}" ]]; then
    qmake_bin=${QMAKE}
else
    qmake_bin=$(find_first_command qmake qmake-qt5) || {
        printf '%s\n' 'Qt 5 qmake was not found in PATH.' >&2
        exit 2
    }
fi

make_bin=${MAKE:-make}
jobs=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || printf '%s' 4)}

command -v "${qmake_bin}" >/dev/null
command -v "${make_bin}" >/dev/null
command -v lipo >/dev/null

qt_version=$("${qmake_bin}" -query QT_VERSION)
[[ "${qt_version}" == 5.* ]] || {
    printf 'Qt 5 is required; %s reports Qt %s.\n' "${qmake_bin}" "${qt_version}" >&2
    exit 2
}

machine=$(uname -m)
case "${machine}" in
    arm64) expected_arch=arm64 ;;
    x86_64) expected_arch=x86_64 ;;
    *) printf 'Unsupported macOS build architecture: %s\n' "${machine}" >&2; exit 2 ;;
esac

for app in "${APP_NAMES[@]}"; do
    app_build_dir="${build_root}/${app}"
    mkdir -p "${app_build_dir}"
    printf '\n==> Building %s for macOS %s\n' "${app}" "${expected_arch}"
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

printf '\nmacOS application bundles:\n'
for app in "${APP_NAMES[@]}"; do
    bundle="${build_root}/${app}/${MACOS_BUNDLES[${app}]}"
    executable="${bundle}/Contents/MacOS/${MACOS_EXECUTABLES[${app}]}"
    [[ -d "${bundle}" ]] || { printf 'Missing application bundle: %s\n' "${bundle}" >&2; exit 1; }
    [[ -x "${executable}" ]] || { printf 'Missing bundle executable: %s\n' "${executable}" >&2; exit 1; }
    lipo -archs "${executable}" | tr ' ' '\n' | grep -Fx "${expected_arch}" >/dev/null || {
        printf 'Wrong architecture in %s; expected %s.\n' "${executable}" "${expected_arch}" >&2
        exit 1
    }
    file "${executable}"
done
