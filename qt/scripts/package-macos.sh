#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd -- "${script_dir}/.." && pwd)
source "${script_dir}/apps.sh"

build_root=${1:-"${root_dir}/build/macos-release"}
dist_root=${2:-"${root_dir}/dist/macos"}

if [[ -n "${MACDEPLOYQT:-}" ]]; then
    macdeployqt_bin=${MACDEPLOYQT}
else
    macdeployqt_bin=$(find_first_command macdeployqt) || {
        printf '%s\n' 'Qt 5 macdeployqt was not found in PATH.' >&2
        exit 2
    }
fi

for required_tool in "${macdeployqt_bin}" ditto hdiutil install_name_tool lipo otool codesign; do
    command -v "${required_tool}" >/dev/null
done

machine=$(uname -m)
case "${machine}" in
    arm64) package_arch=arm64 ;;
    x86_64) package_arch=x86_64 ;;
    *) printf 'Unsupported macOS packaging architecture: %s\n' "${machine}" >&2; exit 2 ;;
esac
package_arch=${MACOS_ARCH:-${package_arch}}

case "${package_arch}" in
    arm64|x86_64) ;;
    *) printf 'Unsupported MACOS_ARCH: %s\n' "${package_arch}" >&2; exit 2 ;;
esac

if [[ "${SKIP_BUILD:-0}" != 1 ]]; then
    "${script_dir}/build-macos.sh" "${build_root}"
fi

if [[ -n "${LIBUSB_ROOT:-}" ]]; then
    libusb_root=${LIBUSB_ROOT}
elif command -v brew >/dev/null 2>&1; then
    libusb_root=$(brew --prefix libusb)
else
    libusb_root=
fi

bundle_libusb()
{
    local bundle=$1
    local executable=$2
    local dependency library_name source_library destination

    dependency=$(otool -L "${executable}" | awk 'NR > 1 && /libusb-1\.0/ {print $1; exit}')
    [[ -n "${dependency}" ]] || return 0

    library_name=${dependency##*/}
    destination="${bundle}/Contents/Frameworks/${library_name}"
    if [[ ! -f "${destination}" ]]; then
        [[ -n "${libusb_root}" ]] || {
            printf 'Cannot locate libusb needed by %s. Set LIBUSB_ROOT.\n' "${executable}" >&2
            return 1
        }
        source_library="${libusb_root}/lib/${library_name}"
        if [[ ! -f "${source_library}" ]]; then
            source_library=$(find "${libusb_root}/lib" -maxdepth 1 -type f \
                -name 'libusb-1.0*.dylib' -print -quit)
        fi
        [[ -f "${source_library}" ]] || {
            printf 'Cannot locate the libusb dynamic library below %s.\n' "${libusb_root}" >&2
            return 1
        }
        library_name=$(basename -- "${source_library}")
        destination="${bundle}/Contents/Frameworks/${library_name}"
        mkdir -p "$(dirname -- "${destination}")"
        cp -fL "${source_library}" "${destination}"
        chmod u+w "${destination}"
    fi

    install_name_tool -id "@rpath/${library_name}" "${destination}"
    install_name_tool -change "${dependency}" \
        "@executable_path/../Frameworks/${library_name}" "${executable}"
}

verify_bundle_dependencies()
{
    local bundle=$1
    local candidate dependency install_name
    local failed=0

    while IFS= read -r -d '' candidate; do
        file "${candidate}" | grep -q 'Mach-O' || continue
        # otool -L reports LC_ID_DYLIB as its first entry for libraries and
        # plugins. That is the file's identity, not a library it loads.
        install_name=$(otool -D "${candidate}" 2>/dev/null | awk 'NR == 2 {print $1}')
        while IFS= read -r dependency; do
            [[ -n "${install_name}" && "${dependency}" == "${install_name}" ]] && continue
            case "${dependency}" in
                @*|/System/Library/*|/usr/lib/*) ;;
                *)
                    printf 'Unbundled macOS dependency in %s: %s\n' \
                        "${candidate}" "${dependency}" >&2
                    failed=1
                    ;;
            esac
        done < <(otool -L "${candidate}" | awk 'NR > 1 {print $1}')
    done < <(find "${bundle}" -type f -print0)

    [[ "${failed}" == 0 ]]
}

mkdir -p "${dist_root}"

package_dir=
dmg_staging=
image_tmp=
cleanup_package_dir()
{
    if [[ -n "${package_dir:-}" && "${package_dir}" == "${dist_root}"/.* && -d "${package_dir}" ]]; then
        rm -rf -- "${package_dir}"
    fi
    if [[ -n "${dmg_staging:-}" && "${dmg_staging}" == "${dist_root}"/.* && -d "${dmg_staging}" ]]; then
        rm -rf -- "${dmg_staging}"
    fi
    if [[ -n "${image_tmp:-}" && "${image_tmp}" == "${dist_root}"/.* && -f "${image_tmp}" ]]; then
        rm -f -- "${image_tmp}"
    fi
}
trap cleanup_package_dir EXIT

for app in "${APP_NAMES[@]}"; do
    version=$(app_version "${root_dir}" "${app}")
    source_bundle="${build_root}/${app}/${MACOS_BUNDLES[${app}]}"
    [[ -d "${source_bundle}" ]] || { printf 'Missing application bundle: %s\n' "${source_bundle}" >&2; exit 1; }

    package_dir=$(mktemp -d "${dist_root}/.${app}.bundle.XXXXXX")
    bundle="${package_dir}/${MACOS_BUNDLES[${app}]}"
    ditto "${source_bundle}" "${bundle}"

    deploy_args=("${bundle}" -always-overwrite -verbose=2)
    if app_uses_qml "${app}"; then
        deploy_args+=("-qmldir=${root_dir}/${app}")
    fi
    "${macdeployqt_bin}" "${deploy_args[@]}"

    executable="${bundle}/Contents/MacOS/${MACOS_EXECUTABLES[${app}]}"
    [[ -x "${executable}" ]] || { printf 'Missing bundle executable: %s\n' "${executable}" >&2; exit 1; }
    bundle_libusb "${bundle}" "${executable}"
    verify_bundle_dependencies "${bundle}"

    lipo -archs "${executable}" | tr ' ' '\n' | grep -Fx "${package_arch}" >/dev/null || {
        printf 'Wrong architecture in %s; expected %s.\n' "${executable}" "${package_arch}" >&2
        exit 1
    }

    codesign_identity=${MACOS_CODESIGN_IDENTITY:--}
    codesign_args=(--force --deep --sign "${codesign_identity}")
    if [[ "${codesign_identity}" != - ]]; then
        codesign_args+=(--options runtime --timestamp)
    fi
    codesign "${codesign_args[@]}" "${bundle}"
    codesign --verify --deep --strict "${bundle}"

    dmg_staging=$(mktemp -d "${dist_root}/.${app}.dmg.XXXXXX")
    ditto "${bundle}" "${dmg_staging}/${MACOS_BUNDLES[${app}]}"
    ln -s /Applications "${dmg_staging}/Applications"

    image="${dist_root}/${app}-${version}-macos-${package_arch}.dmg"
    image_tmp="${dist_root}/.${app}-${version}-macos-${package_arch}.dmg"
    rm -f -- "${image_tmp}"
    hdiutil create -quiet -ov -format UDZO \
        -volname "${app}-${version}" -srcfolder "${dmg_staging}" "${image_tmp}"
    hdiutil verify -quiet "${image_tmp}"
    mv -f -- "${image_tmp}" "${image}"
    image_tmp=
    printf 'Created %s\n' "${image}"
    write_sha256 "${image}"

    rm -rf -- "${package_dir}" "${dmg_staging}"
    package_dir=
    dmg_staging=
done

trap - EXIT
