#!/usr/bin/env bash

if (( BASH_VERSINFO[0] < 4 )); then
    printf '%s\n' 'OpenDev Qt packaging scripts require Bash 4 or newer.' >&2
    if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
        return 2
    fi
    exit 2
fi

ALL_APP_NAMES=(odrfidkit odrfidcfg odrfidcfgM odhiddfu wdtmon3 wdtmon3-mini iosenmon)
APP_NAMES=("${ALL_APP_NAMES[@]}")

declare -A APP_PROJECTS=(
    [odrfidkit]="odrfidkit/odrfidkit.pro"
    [odrfidcfg]="odrfidcfg/odrfidcfg.pro"
    [odrfidcfgM]="odrfidcfgM/odrfidcfgM.pro"
    [odhiddfu]="odhiddfu/odhiddfu.pro"
    [wdtmon3]="wdtmon3/wdtmon3.pro"
    [wdtmon3-mini]="wdtmon3-mini/wdtmon3_lite.pro"
    [iosenmon]="iosenmon/iosenmon.pro"
)

QML_APP_NAMES=(wdtmon3 iosenmon)

app_uses_qml()
{
    local candidate
    for candidate in "${QML_APP_NAMES[@]}"; do
        [[ "$1" == "${candidate}" ]] && return 0
    done
    return 1
}

declare -A LINUX_BINARIES=(
    [odrfidkit]="odrfidkit"
    [odrfidcfg]="odrfidconfig"
    [odrfidcfgM]="odrfidconfigM"
    [odhiddfu]="odhiddfu"
    [wdtmon3]="bin/wdtmon3"
    [wdtmon3-mini]="bin/wdtmon3-mini"
    [iosenmon]="bin/iosenmon"
)

declare -A WINDOWS_BINARIES=(
    [odrfidkit]="release/odrfidkit.exe"
    [odrfidcfg]="release/odrfidconfig.exe"
    [odrfidcfgM]="release/odrfidconfigM.exe"
    [odhiddfu]="odhiddfu.exe"
    [wdtmon3]="release/wdtmon3.exe"
    [wdtmon3-mini]="release/wdtmon3mini.exe"
    [iosenmon]="release/iosenmon.exe"
)

declare -A MACOS_BUNDLES=(
    [odrfidkit]="OdRFIDKit.app"
    [odrfidcfg]="ODRFIDConfig.app"
    [odrfidcfgM]="ODRFIDConfigM.app"
    [odhiddfu]="ODHidDFU.app"
    [wdtmon3]="wdtmon3.app"
    [wdtmon3-mini]="wdtmon3-mini.app"
    [iosenmon]="IOSenMon.app"
)

declare -A MACOS_EXECUTABLES=(
    [odrfidkit]="OdRFIDKit"
    [odrfidcfg]="ODRFIDConfig"
    [odrfidcfgM]="ODRFIDConfigM"
    [odhiddfu]="ODHidDFU"
    [wdtmon3]="wdtmon3"
    [wdtmon3-mini]="wdtmon3-mini"
    [iosenmon]="IOSenMon"
)

declare -A APP_VERSION_HEADERS=(
    [odrfidkit]="odrfidkit/version_win.h"
    [odrfidcfg]="odrfidcfg/version_win.h"
    [odrfidcfgM]="odrfidcfgM/version_win.h"
    [odhiddfu]="odhiddfu/version_win.h"
    [wdtmon3]="wdtmon3/version_win.h"
    [wdtmon3-mini]="wdtmon3-mini/version_win.h"
    [iosenmon]="iosenmon/version_win.h"
)

declare -A APP_DISPLAY_NAMES=(
    [odrfidkit]="ODRFIDKit"
    [odrfidcfg]="ODRFID Configurator"
    [odrfidcfgM]="ODRFID Configurator M"
    [odhiddfu]="ODHIDDFU"
    [wdtmon3]="Watchdog Monitor 3"
    [wdtmon3-mini]="Watchdog Monitor 3 Mini"
    [iosenmon]="IOT Sensor Monitor"
)

declare -A APP_DESCRIPTIONS=(
    [odrfidkit]="Read and edit RFID tags"
    [odrfidcfg]="Configure OpenDev RFID readers"
    [odrfidcfgM]="Configure OpenDev Modbus RFID readers"
    [odhiddfu]="Update firmware on OpenDev DFU devices"
    [wdtmon3]="Monitor and configure OpenDev USB watchdogs"
    [wdtmon3-mini]="Monitor OpenDev USB watchdogs"
    [iosenmon]="Monitor OpenDev IoT sensors"
)

declare -A APP_ICONS=(
    [odrfidkit]="odrfidkit/images/odrfidkit_src.svg"
    [odrfidcfg]="odrfidcfg/odrfidcfg.svg"
    [odrfidcfgM]="odrfidcfgM/odrfidcfgM.svg"
    [odhiddfu]="libhiddfu/images-src/dfu_icon.svg"
    [wdtmon3]="wdtmon3/wdtmon3.svg"
    [wdtmon3-mini]="wdtmon3-mini/wdtmon3_lite.svg"
    [iosenmon]="iosenmon/iosenmon.svg"
)

if [[ -n "${QT_APPS:-}" ]]; then
    IFS=', ' read -r -a APP_NAMES <<< "${QT_APPS}"
    [[ ${#APP_NAMES[@]} -gt 0 ]] || {
        printf '%s\n' 'QT_APPS does not contain an application name.' >&2
        return 2
    }

    for app in "${APP_NAMES[@]}"; do
        [[ -n "${APP_PROJECTS[${app}]+defined}" ]] || {
            printf 'Unknown application in QT_APPS: %s\n' "${app}" >&2
            return 2
        }
    done
fi

app_version()
{
    local root_dir=$1
    local app=$2
    local header version

    header="${root_dir}/${APP_VERSION_HEADERS[${app}]}"
    [[ -f "${header}" ]] || {
        printf 'Missing version header for %s: %s\n' "${app}" "${header}" >&2
        return 1
    }

    version=$(sed -n \
        's/^[[:space:]]*#define[[:space:]]*VER_PRODUCTVERSION_STR[[:space:]]*"\([0-9][0-9.]*\)\\0".*/\1/p' \
        "${header}")
    if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        printf 'Invalid application version in %s: %s\n' "${header}" "${version:-<empty>}" >&2
        return 1
    fi

    printf '%s\n' "${version}"
}

write_sha256()
{
    local artifact=$1
    local artifact_dir artifact_name

    [[ -f "${artifact}" ]] || {
        printf 'Cannot checksum missing artifact: %s\n' "${artifact}" >&2
        return 1
    }

    artifact_dir=$(cd -- "$(dirname -- "${artifact}")" && pwd)
    artifact_name=$(basename -- "${artifact}")
    (
        cd "${artifact_dir}" || exit 1
        if command -v sha256sum >/dev/null 2>&1; then
            sha256sum "${artifact_name}" > "${artifact_name}.sha256"
        elif command -v shasum >/dev/null 2>&1; then
            shasum -a 256 "${artifact_name}" > "${artifact_name}.sha256"
        else
            printf '%s\n' 'Neither sha256sum nor shasum is available.' >&2
            return 1
        fi
    )
    printf 'Created %s.sha256\n' "${artifact}"
}

find_first_command()
{
    local candidate

    for candidate in "$@"; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            command -v "${candidate}"
            return 0
        fi
    done

    return 1
}

configure_linux_qt5_serialbus()
{
    local root_dir=$1
    local qmake_bin=$2
    local qt_archdata qt_version machine serialbus_root

    if [[ -n "${QT5_SERIALBUS_ROOT:-}" ]]; then
        serialbus_root=${QT5_SERIALBUS_ROOT}
    else
        qt_archdata=$("${qmake_bin}" -query QT_INSTALL_ARCHDATA)
        if [[ -f "${qt_archdata}/mkspecs/modules/qt_lib_serialbus.pri" ]]; then
            return 0
        fi

        qt_version=$("${qmake_bin}" -query QT_VERSION)
        machine=$(uname -m)
        serialbus_root="${root_dir}/toolchains/qt5-serialbus-linux/${qt_version}-${machine}"
    fi

    if [[ ! -f "${serialbus_root}/include/QtSerialBus/QModbusDevice" ||
          ! -e "${serialbus_root}/lib/libQt5SerialBus.so.5" ]]; then
        printf 'Qt 5 Serial Bus was not found at %s.\n' "${serialbus_root}" >&2
        printf '%s\n' 'Run scripts/bootstrap-qt5-serialbus-linux.sh first.' >&2
        return 2
    fi

    export QT5_SERIALBUS_ROOT=${serialbus_root}
    export LD_LIBRARY_PATH="${serialbus_root}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
}

find_mxe_qmake()
{
    local root_dir=$1
    local candidate

    if [[ -n "${MXE_QMAKE:-}" && -x "${MXE_QMAKE}" ]]; then
        printf '%s\n' "${MXE_QMAKE}"
        return 0
    fi

    for candidate in \
        "${MXE_ROOT:-${root_dir}/toolchains/mxe}/usr/x86_64-w64-mingw32.shared/qt5/bin/qmake" \
        "${MXE_ROOT:-${root_dir}/toolchains/mxe}/usr/bin/x86_64-w64-mingw32.shared-qmake-qt5"
    do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    return 1
}
