#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd -- "${script_dir}/.." && pwd)
qmake_bin=${QMAKE:-qmake}
make_bin=${MAKE:-make}
jobs=${JOBS:-$(nproc)}

command -v "${qmake_bin}" >/dev/null
command -v "${make_bin}" >/dev/null
command -v sha256sum >/dev/null
command -v tar >/dev/null

qt_version=$("${qmake_bin}" -query QT_VERSION)
if [[ "${qt_version}" != 5.* ]]; then
    printf 'Qt 5 qmake is required, found Qt %s.\n' "${qt_version}" >&2
    exit 2
fi

serialbus_version=${QT5_SERIALBUS_VERSION:-${qt_version}}
case "${serialbus_version}" in
    5.15.18)
        archive_sha256=41630fd283814bdf94d1c53887e46667a374e97e5aebbb4975b26a701cf5fde8
        ;;
    *)
        if [[ -z "${QT5_SERIALBUS_SHA256:-}" ]]; then
            printf 'No bundled checksum for Qt Serial Bus %s.\n' "${serialbus_version}" >&2
            printf '%s\n' 'Set QT5_SERIALBUS_SHA256 to the official archive checksum.' >&2
            exit 2
        fi
        archive_sha256=${QT5_SERIALBUS_SHA256}
        ;;
esac

machine=$(uname -m)
install_root=${QT5_SERIALBUS_ROOT:-"${root_dir}/toolchains/qt5-serialbus-linux/${qt_version}-${machine}"}
if [[ -f "${install_root}/include/QtSerialBus/QModbusDevice" &&
      -e "${install_root}/lib/libQt5SerialBus.so.5" ]]; then
    printf 'Qt 5 Serial Bus is already available at %s\n' "${install_root}"
    exit 0
fi

qt_headers=$("${qmake_bin}" -query QT_INSTALL_HEADERS)
private_headers=${QT_PRIVATE_HEADERS_ROOT:-"${qt_headers}/QtCore/${qt_version}"}
if [[ ! -f "${private_headers}/QtCore/private/qobject_p.h" ]]; then
    printf 'Qt private headers were not found at %s.\n' "${private_headers}" >&2
    printf '%s\n' 'Install qtbase5-private-dev or set QT_PRIVATE_HEADERS_ROOT.' >&2
    exit 2
fi

install_parent=$(dirname -- "${install_root}")
mkdir -p "${install_parent}"
work_dir=$(mktemp -d "${install_parent}/.qt5-serialbus.XXXXXX")
cleanup_work_dir()
{
    if [[ -n "${work_dir:-}" && "${work_dir}" == "${install_parent}"/.qt5-serialbus.* && -d "${work_dir}" ]]; then
        rm -rf -- "${work_dir}"
    fi
}
trap cleanup_work_dir EXIT

archive=${QT5_SERIALBUS_ARCHIVE:-"${work_dir}/qtserialbus.tar.xz"}
if [[ -z "${QT5_SERIALBUS_ARCHIVE:-}" ]]; then
    archive_url=${QT5_SERIALBUS_URL:-"https://download.qt.io/archive/qt/5.15/${serialbus_version}/submodules/qtserialbus-everywhere-opensource-src-${serialbus_version}.tar.xz"}
    if command -v curl >/dev/null; then
        curl --fail --location --retry 3 --output "${archive}" "${archive_url}"
    elif command -v wget >/dev/null; then
        wget -O "${archive}" "${archive_url}"
    else
        printf '%s\n' 'curl or wget is required to download Qt Serial Bus.' >&2
        exit 2
    fi
fi

printf '%s  %s\n' "${archive_sha256}" "${archive}" | sha256sum --check --status

source_dir="${work_dir}/source"
build_dir="${work_dir}/build"
stage_dir="${work_dir}/stage"
mkdir -p "${source_dir}" "${build_dir}" "${stage_dir}"
tar -xf "${archive}" -C "${source_dir}" --strip-components=1

(
    cd "${build_dir}"
    "${qmake_bin}" "${source_dir}/qtserialbus.pro" \
        CONFIG+=release CONFIG-=debug \
        "INCLUDEPATH+=${private_headers} ${private_headers}/QtCore"
    "${make_bin}" -j"${jobs}" sub-src-qmake_all
    "${make_bin}" -C src -j"${jobs}" sub-serialbus
    "${make_bin}" -C src/serialbus install INSTALL_ROOT="${stage_dir}"
)

staged_headers="${stage_dir}$("${qmake_bin}" -query QT_INSTALL_HEADERS)/QtSerialBus"
staged_libs="${stage_dir}$("${qmake_bin}" -query QT_INSTALL_LIBS)"
mkdir -p "${install_root}/include" "${install_root}/lib"
cp -a "${staged_headers}" "${install_root}/include/"
cp -a "${staged_libs}"/libQt5SerialBus.so* "${install_root}/lib/"

printf 'Qt 5 Serial Bus installed at %s\n' "${install_root}"
