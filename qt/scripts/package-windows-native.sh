#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd -- "${script_dir}/.." && pwd)
source "${script_dir}/apps.sh"

build_root=${1:-"${root_dir}/build/windows-native-release"}
dist_root=${2:-"${root_dir}/dist/windows"}

if [[ -n "${WDEPLOYQT:-}" ]]; then
    windeployqt_bin=${WDEPLOYQT}
else
    windeployqt_bin=$(find_first_command windeployqt-qt5 windeployqt) || {
        printf '%s\n' 'Qt 5 windeployqt was not found in PATH.' >&2
        exit 2
    }
fi

command -v "${windeployqt_bin}" >/dev/null
command -v zip >/dev/null

objdump_bin=${OBJDUMP:-objdump}
command -v "${objdump_bin}" >/dev/null
runtime_bin_dir=${MINGW_PREFIX:-$(dirname -- "$(command -v "${windeployqt_bin}")")}
[[ "${runtime_bin_dir}" == */bin ]] || runtime_bin_dir="${runtime_bin_dir}/bin"

is_windows_system_dll()
{
    case "${1,,}" in
        api-ms-win-*.dll|ext-ms-win-*.dll|advapi32.dll|bcrypt.dll|cabinet.dll|cfgmgr32.dll|comctl32.dll|comdlg32.dll|crypt32.dll|cryptsp.dll|d2d1.dll|d3d9.dll|d3d11.dll|dbghelp.dll|dnsapi.dll|dwmapi.dll|dwrite.dll|dxgi.dll|gdi32.dll|hid.dll|imm32.dll|iphlpapi.dll|kernel32.dll|kernelbase.dll|mpr.dll|msimg32.dll|msvcrt.dll|netapi32.dll|normaliz.dll|ntdll.dll|odbc32.dll|ole32.dll|oleacc.dll|oleaut32.dll|opengl32.dll|powrprof.dll|propsys.dll|psapi.dll|rpcrt4.dll|secur32.dll|setupapi.dll|shell32.dll|shlwapi.dll|ucrtbase.dll|urlmon.dll|user32.dll|userenv.dll|usp10.dll|uxtheme.dll|version.dll|winhttp.dll|wininet.dll|winmm.dll|winspool.drv|wintrust.dll|ws2_32.dll|wsock32.dll|wtsapi32.dll) return 0 ;;
        *) return 1 ;;
    esac
}

find_dll()
{
    local dll=$1
    local dir source
    for dir in "${runtime_bin_dir}"; do
        [[ -d "${dir}" ]] || continue
        source=$(find "${dir}" -maxdepth 1 -type f -iname "${dll}" -print -quit)
        if [[ -n "${source}" ]]; then
            printf '%s\n' "${source}"
            return 0
        fi
    done
    return 0
}

copy_pe_dependencies()
{
    local target_dir=$1
    local pass pe dll source copied
    local -A missing_dlls=()

    for pass in 1 2 3 4 5 6 7 8; do
        copied=0
        while IFS= read -r -d '' pe; do
            while IFS= read -r dll; do
                [[ -n "${dll}" ]] || continue
                is_windows_system_dll "${dll}" && continue
                if ! find "${target_dir}" -type f -iname "${dll}" -print -quit | grep -q .; then
                    source=$(find_dll "${dll}")
                    if [[ -z "${source}" ]]; then
                        missing_dlls["${dll,,}"]="${dll}"
                        continue
                    fi
                    cp -L "${source}" "${target_dir}/${dll}"
                    unset 'missing_dlls['"${dll,,}"']'
                    copied=1
                fi
            done < <("${objdump_bin}" -p "${pe}" 2>/dev/null | awk '/DLL Name:/ {print $3}')
        done < <(find "${target_dir}" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0)
        [[ "${copied}" == 0 ]] && break
    done

    if (( ${#missing_dlls[@]} != 0 )); then
        printf 'Missing redistributable DLLs:\n' >&2
        printf '  %s\n' "${missing_dlls[@]}" >&2
        return 1
    fi
}

if [[ "${SKIP_BUILD:-0}" != 1 ]]; then
    "${script_dir}/build-windows-native.sh" "${build_root}"
fi

mkdir -p "${dist_root}"

package_dir=
archive_tmp=
cleanup_package_dir()
{
    if [[ -n "${package_dir:-}" && "${package_dir}" == "${dist_root}"/.* && -d "${package_dir}" ]]; then
        rm -rf -- "${package_dir}"
    fi
    if [[ -n "${archive_tmp:-}" && "${archive_tmp}" == "${dist_root}"/.* && -f "${archive_tmp}" ]]; then
        rm -f -- "${archive_tmp}"
    fi
}
trap cleanup_package_dir EXIT

for app in "${APP_NAMES[@]}"; do
    version=$(app_version "${root_dir}" "${app}")
    source_binary="${build_root}/${app}/${WINDOWS_BINARIES[${app}]}"
    [[ -f "${source_binary}" ]] || { printf 'Missing binary: %s\n' "${source_binary}" >&2; exit 1; }

    package_dir=$(mktemp -d "${dist_root}/.${app}.XXXXXX")
    binary_name=$(basename -- "${source_binary}")
    cp "${source_binary}" "${package_dir}/${binary_name}"

    deploy_args=(--release --compiler-runtime --dir "${package_dir}")
    if app_uses_qml "${app}"; then
        deploy_args+=(--qmldir "${root_dir}/${app}")
    fi

    "${windeployqt_bin}" "${deploy_args[@]}" "${package_dir}/${binary_name}"

    # The applications only use SQLite. Other SQL plugins pull optional
    # database client runtimes into otherwise portable packages.
    if [[ -d "${package_dir}/sqldrivers" ]]; then
        find "${package_dir}/sqldrivers" -type f ! -iname 'qsqlite.dll' -delete
    fi

    copy_pe_dependencies "${package_dir}"

    [[ -f "${package_dir}/platforms/qwindows.dll" ]] || {
        printf 'windeployqt did not deploy the Windows platform plugin for %s.\n' "${app}" >&2
        exit 1
    }

    archive="${dist_root}/${app}-${version}-windows-x86_64.zip"
    archive_tmp="${dist_root}/.${app}-${version}-windows-x86_64.zip"
    rm -f -- "${archive_tmp}"
    (
        cd "${package_dir}"
        zip -9qr "${archive_tmp}" .
    )
    mv -f -- "${archive_tmp}" "${archive}"
    archive_tmp=
    printf 'Created %s\n' "${archive}"
    write_sha256 "${archive}"

    rm -rf -- "${package_dir}"
    package_dir=
done

trap - EXIT
