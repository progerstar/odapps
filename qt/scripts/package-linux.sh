#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd -- "${script_dir}/.." && pwd)
source "${script_dir}/apps.sh"

build_root=${1:-"${root_dir}/build/linux-release"}
dist_root=${2:-"${root_dir}/dist/linux"}
qmake_bin=${QMAKE:-qmake}

command -v "${qmake_bin}" >/dev/null
configure_linux_qt5_serialbus "${root_dir}" "${qmake_bin}"

machine=$(uname -m)
case "${machine}" in
    x86_64|amd64) appimage_arch=x86_64 ;;
    aarch64|arm64) appimage_arch=aarch64 ;;
    armv7l|armhf) appimage_arch=armhf ;;
    *) appimage_arch=${machine} ;;
esac
appimage_arch=${APPIMAGE_ARCH:-${appimage_arch}}

appimagetool_bin=
if [[ "${SKIP_APPIMAGE:-0}" != 1 ]]; then
    appimagetool_candidate=${APPIMAGETOOL:-appimagetool}
    if [[ "${appimagetool_candidate}" == */* ]]; then
        if [[ -x "${appimagetool_candidate}" ]]; then
            appimagetool_bin=${appimagetool_candidate}
        fi
    else
        appimagetool_bin=$(command -v "${appimagetool_candidate}" || true)
    fi

    if [[ -z "${appimagetool_bin}" ]]; then
        printf '%s\n' 'AppImage packaging requires appimagetool.' >&2
        printf '%s\n' 'Install it or set APPIMAGETOOL=/absolute/path/to/appimagetool.' >&2
        printf '%s\n' 'Set SKIP_APPIMAGE=1 to create portable tarballs only.' >&2
        exit 2
    fi

    appimagetool_args=()
    if [[ -n "${APPIMAGE_RUNTIME_FILE:-}" ]]; then
        [[ -f "${APPIMAGE_RUNTIME_FILE}" ]] || {
            printf 'AppImage runtime does not exist: %s\n' "${APPIMAGE_RUNTIME_FILE}" >&2
            exit 2
        }
        appimagetool_args+=(--runtime-file "${APPIMAGE_RUNTIME_FILE}")
    fi
fi

if [[ "${SKIP_BUILD:-0}" != 1 ]]; then
    "${script_dir}/build-linux.sh" "${build_root}"
fi

qt_plugins=$("${qmake_bin}" -query QT_INSTALL_PLUGINS)
qt_qml=$("${qmake_bin}" -query QT_INSTALL_QML)
qt_translations=$("${qmake_bin}" -query QT_INSTALL_TRANSLATIONS)
mkdir -p "${dist_root}"

package_dir=
image_tmp=
cleanup_package_dir()
{
    if [[ -n "${package_dir:-}" && "${package_dir}" == "${dist_root}"/.* && -d "${package_dir}" ]]; then
        rm -rf -- "${package_dir}"
    fi
    if [[ -n "${image_tmp:-}" && "${image_tmp}" == "${dist_root}"/.* && -f "${image_tmp}" ]]; then
        rm -f -- "${image_tmp}"
    fi
}
trap cleanup_package_dir EXIT

for app in "${APP_NAMES[@]}"; do
    if app_uses_qml "${app}"; then
        for required_qml_module in QtQuick/Extras/qmldir Qt/labs/calendar/qmldir QtGraphicalEffects/qmldir; do
            if [[ ! -f "${qt_qml}/${required_qml_module}" ]]; then
                printf 'Missing QML runtime module: %s\n' "${qt_qml}/${required_qml_module}" >&2
                printf '%s\n' 'Install qml-module-qt-labs-calendar, qml-module-qtquick-extras and qml-module-qtgraphicaleffects.' >&2
                exit 1
            fi
        done
        break
    fi
done

is_base_system_library()
{
    case "$1" in
        ld-linux*.so*|libc.so*|libm.so*|libpthread.so*|libdl.so*|librt.so*|libresolv.so*|libutil.so*) return 0 ;;
        *) return 1 ;;
    esac
}

# Libraries that must come from the host, never from the build machine. The GPU
# driver (libGLX_mesa, ...) is loaded into the process and links against the
# host's libstdc++, libdrm, libX11, etc.; bundled copies shadow those through
# LD_LIBRARY_PATH. A GCC 12 libstdc++ lacks the GLIBCXX_3.4.32 that Mesa 26
# needs, so the driver fails to load and Qt aborts with "Could not initialize
# GLX". The build links against glibc 2.35 anyway, so any host that can run the
# package already has a new enough libstdc++.
is_host_library()
{
    case "$1" in
        libstdc++.so*|libgcc_s.so*) return 0 ;;
        libGL.so*|libGLX.so*|libGLdispatch.so*|libOpenGL.so*|libEGL.so*|libGLESv2.so*) return 0 ;;
        libglapi.so*|libgbm.so*|libdrm.so*|libdrm_*.so*|libvulkan.so*|libwayland-*.so*) return 0 ;;
        libX11.so*|libX11-xcb.so*|libxcb.so*|libXau.so*|libXdmcp.so*) return 0 ;;
        libfontconfig.so*|libfreetype.so*|libharfbuzz.so*|libexpat.so*|libudev.so*) return 0 ;;
        *) return 1 ;;
    esac
}

copy_elf_dependencies()
{
    local package_dir=$1
    local pass elf dependency name copied

    for pass in 1 2 3 4 5 6 7 8; do
        copied=0
        while IFS= read -r -d '' elf; do
            file "${elf}" | grep -q 'ELF' || continue
            while IFS= read -r dependency; do
                [[ -f "${dependency}" ]] || continue
                name=$(basename -- "${dependency}")
                is_base_system_library "${name}" && continue
                is_host_library "${name}" && continue
                if [[ ! -e "${package_dir}/usr/lib/${name}" ]]; then
                    cp -L "${dependency}" "${package_dir}/usr/lib/${name}"
                    copied=1
                fi
            done < <(ldd "${elf}" 2>/dev/null | awk '/=> \// {print $3} /^[[:space:]]*\// {print $1}')
        done < <(find "${package_dir}/usr/bin" "${package_dir}/usr/lib" "${package_dir}/usr/plugins" "${package_dir}/usr/qml" -type f -print0 2>/dev/null)
        [[ "${copied}" == 0 ]] && break
    done
}

for app in "${APP_NAMES[@]}"; do
    version=$(app_version "${root_dir}" "${app}")
    source_binary="${build_root}/${app}/${LINUX_BINARIES[${app}]}"
    [[ -x "${source_binary}" ]] || { printf 'Missing binary: %s\n' "${source_binary}" >&2; exit 1; }

    package_dir=$(mktemp -d "${dist_root}/.${app}.XXXXXX")
    mkdir -p "${package_dir}/usr/bin" "${package_dir}/usr/lib" \
        "${package_dir}/usr/plugins" "${package_dir}/usr/qml"
    binary_name=$(basename -- "${source_binary}")
    install -m 0755 "${source_binary}" "${package_dir}/usr/bin/${binary_name}"
    cp -a "${qt_plugins}/." "${package_dir}/usr/plugins/"
    if app_uses_qml "${app}"; then
        cp -a "${qt_qml}/." "${package_dir}/usr/qml/"
    fi
    if [[ -d "${qt_translations}" ]]; then
        mkdir -p "${package_dir}/usr/translations"
        cp -a "${qt_translations}/." "${package_dir}/usr/translations/"
    fi

    copy_elf_dependencies "${package_dir}"

    {
        printf '%s\n' '#!/bin/sh'
        printf '%s\n' 'set -e'
        printf '%s\n' 'app_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)'
        printf '%s\n' 'export PATH="${app_dir}/usr/bin:${PATH}"'
        printf '%s\n' 'export LD_LIBRARY_PATH="${app_dir}/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"'
        printf '%s\n' 'export QT_PLUGIN_PATH="${app_dir}/usr/plugins"'
        printf '%s\n' 'export QT_QPA_PLATFORM_PLUGIN_PATH="${app_dir}/usr/plugins/platforms"'
        printf '%s\n' 'export QML2_IMPORT_PATH="${app_dir}/usr/qml"'
        printf '%s\n' 'export XDG_DATA_DIRS="${app_dir}/usr/share${XDG_DATA_DIRS:+:${XDG_DATA_DIRS}}"'
        printf 'exec "${app_dir}/usr/bin/%s" "$@"\n' "${binary_name}"
    } > "${package_dir}/${app}"
    chmod 0755 "${package_dir}/${app}"
    cp -a "${package_dir}/${app}" "${package_dir}/AppRun"

    printf '[Paths]\nPlugins=../plugins\nQml2Imports=../qml\nTranslations=../translations\n' > "${package_dir}/usr/bin/qt.conf"
    chmod 0644 "${package_dir}/usr/bin/qt.conf"

    icon_source="${root_dir}/${APP_ICONS[${app}]}"
    [[ -f "${icon_source}" ]] || { printf 'Missing application icon: %s\n' "${icon_source}" >&2; exit 1; }
    icon_ext=${icon_source##*.}
    install -m 0644 "${icon_source}" "${package_dir}/${app}.${icon_ext}"
    ln -s "${app}.${icon_ext}" "${package_dir}/.DirIcon"

    desktop_file="${package_dir}/${app}.desktop"
    {
        printf '%s\n' '[Desktop Entry]'
        printf '%s\n' 'Type=Application'
        printf 'Name=%s\n' "${APP_DISPLAY_NAMES[${app}]}"
        printf 'Comment=%s\n' "${APP_DESCRIPTIONS[${app}]}"
        printf 'Exec=%s\n' "${app}"
        printf 'Icon=%s\n' "${app}"
        printf '%s\n' 'Categories=Utility;'
        printf '%s\n' 'Terminal=false'
        printf '%s\n' 'StartupNotify=true'
    } > "${desktop_file}"
    chmod 0644 "${desktop_file}"

    mkdir -p "${package_dir}/usr/share/applications" \
        "${package_dir}/usr/share/icons/hicolor/scalable/apps"
    cp -a "${desktop_file}" "${package_dir}/usr/share/applications/"
    cp -a "${package_dir}/${app}.${icon_ext}" \
        "${package_dir}/usr/share/icons/hicolor/scalable/apps/"
    if command -v desktop-file-validate >/dev/null; then
        desktop-file-validate "${desktop_file}"
    fi

    if [[ "${SKIP_TARBALL:-0}" != 1 ]]; then
        archive="${dist_root}/${app}-${version}-linux-${appimage_arch}.tar.gz"
        tar -C "${package_dir}" -czf "${archive}" .
        printf 'Created %s\n' "${archive}"
        write_sha256 "${archive}"
    fi

    if [[ "${SKIP_APPIMAGE:-0}" != 1 ]]; then
        image="${dist_root}/${app}-${version}-linux-${appimage_arch}.AppImage"
        image_tmp="${dist_root}/.${app}-${version}-linux-${appimage_arch}.AppImage"
        rm -f -- "${image_tmp}"
        (
            export ARCH=${appimage_arch}
            export APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-1}
            export VERSION=${version}
            "${appimagetool_bin}" "${appimagetool_args[@]}" \
                "${package_dir}" "${image_tmp}"
        )
        [[ -f "${image_tmp}" ]] || { printf 'appimagetool did not create %s\n' "${image_tmp}" >&2; exit 1; }
        chmod 0755 "${image_tmp}"
        mv -f -- "${image_tmp}" "${image}"
        image_tmp=
        printf 'Created %s\n' "${image}"
        write_sha256 "${image}"
    fi

    rm -rf -- "${package_dir}"
    package_dir=
done

trap - EXIT
