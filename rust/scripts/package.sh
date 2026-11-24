#!/usr/bin/env bash
# Builds one Rust application in release mode and packages it for one
# platform, the way the Qt applications are shipped:
#
#   linux    <app>-<version>-linux-x86_64.AppImage
#   windows  <app>-<version>-windows-x86_64.zip    (the .exe, static CRT)
#   macos    <app>-<version>-macos-<arch>.dmg      (an ad-hoc signed .app)
#
# Each package gets a .sha256 file next to it. Usage:
#
#   rust/scripts/package.sh <app> <linux|windows|macos>
#
# Environment:
#   DIST_DIR               output directory (default rust/dist)
#   CARGO_TARGET_DIR       honoured as usual (default rust/<app>/target)
#   APPIMAGETOOL           appimagetool to use (linux; default: from PATH)
#   APPIMAGE_RUNTIME_FILE  AppImage runtime passed to appimagetool (linux)
#   MACOS_ARCH             arm64 or x86_64, checked against the binary (macos)
#   MACOS_CODESIGN_IDENTITY  signing identity (macos; default "-": ad hoc)
set -euo pipefail

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=apps.sh
source "${script_dir}/apps.sh"

[[ $# == 2 ]] || { printf 'Usage: %s <app> <linux|windows|macos>\n' "$0" >&2; exit 2; }
app=$1
platform=$2
require_app "${app}"

root=$(rust_root)
app_dir="${root}/${app}"
version=$(app_version "${app}")
dist_dir=${DIST_DIR:-${root}/dist}
mkdir -p -- "${dist_dir}"
dist_dir=$(CDPATH='' cd -- "${dist_dir}" && pwd)
icon="${app_dir}/${APP_ICONS[${app}]}"
[[ -f "${icon}" ]] || { printf 'Missing icon: %s\n' "${icon}" >&2; exit 1; }

staging=
cleanup()
{
    if [[ -n "${staging}" && -d "${staging}" ]]; then
        rm -rf -- "${staging}"
    fi
}
trap cleanup EXIT

if [[ "${platform}" == windows && "${RUSTFLAGS:-}" != *crt-static* ]]; then
    # No dependency on the Visual C++ redistributable.
    export RUSTFLAGS="${RUSTFLAGS:-} -C target-feature=+crt-static"
fi

printf 'Building %s %s for %s\n' "${app}" "${version}" "${platform}"
cargo build --manifest-path "${app_dir}/Cargo.toml" --release --locked

target_dir=${CARGO_TARGET_DIR:-${app_dir}/target}
exe=
[[ "${platform}" == windows ]] && exe=.exe
binary="${target_dir}/release/${app}${exe}"
[[ -f "${binary}" ]] || { printf 'Cargo did not build %s\n' "${binary}" >&2; exit 1; }

# `--version` is answered before any window opens: a cheap check that the
# packaged binary starts and is the expected version.
check_version()
{
    local output
    output=$("$@" --version 2>&1) || { printf '%s --version failed: %s\n' "$*" "${output}" >&2; return 1; }
    [[ "${output}" == *"${app} ${version}"* ]] || {
        printf 'Unexpected version output from %s: %s\n' "$*" "${output}" >&2
        return 1
    }
}

package_linux()
{
    local appimagetool artifact tmp
    appimagetool=${APPIMAGETOOL:-$(command -v appimagetool || true)}
    [[ -n "${appimagetool}" && -x "${appimagetool}" ]] || {
        printf 'AppImage packaging needs appimagetool; set APPIMAGETOOL=/path/to/appimagetool\n' >&2
        exit 1
    }
    staging=$(mktemp -d "${dist_dir}/.${app}.AppDir.XXXXXX")
    install -D -m 0755 "${binary}" "${staging}/usr/bin/${app}"
    {
        printf '%s\n' '#!/bin/sh'
        printf '%s\n' 'app_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)'
        printf 'exec "${app_dir}/usr/bin/%s" "$@"\n' "${app}"
    } > "${staging}/AppRun"
    chmod 0755 "${staging}/AppRun"
    install -m 0644 "${icon}" "${staging}/${app}.png"
    install -D -m 0644 "${icon}" "${staging}/usr/share/icons/hicolor/256x256/apps/${app}.png"
    ln -s "${app}.png" "${staging}/.DirIcon"
    {
        printf '%s\n' '[Desktop Entry]'
        printf '%s\n' 'Type=Application'
        printf 'Name=%s\n' "${app}"
        printf 'Comment=%s\n' "${APP_DESCRIPTIONS[${app}]}"
        printf 'Exec=%s\n' "${app}"
        printf 'Icon=%s\n' "${app}"
        # The window's Wayland app_id / X11 class is the application name.
        printf 'StartupWMClass=%s\n' "${app}"
        printf '%s\n' 'Categories=Utility;'
        printf '%s\n' 'Terminal=false'
    } > "${staging}/${app}.desktop"
    install -D -m 0644 "${staging}/${app}.desktop" \
        "${staging}/usr/share/applications/${app}.desktop"
    if command -v desktop-file-validate >/dev/null; then
        desktop-file-validate "${staging}/${app}.desktop"
    fi

    artifact="${dist_dir}/${app}-${version}-linux-x86_64.AppImage"
    tmp="${dist_dir}/.${app}-${version}-linux-x86_64.AppImage"
    rm -f -- "${tmp}"
    local args=()
    [[ -n "${APPIMAGE_RUNTIME_FILE:-}" ]] && args+=(--runtime-file "${APPIMAGE_RUNTIME_FILE}")
    (
        export ARCH=x86_64 VERSION="${version}"
        # CI runners have no FUSE.
        export APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-1}
        "${appimagetool}" "${args[@]}" "${staging}" "${tmp}"
    )
    chmod 0755 "${tmp}"
    export APPIMAGE_EXTRACT_AND_RUN=1
    check_version "${tmp}"
    mv -f -- "${tmp}" "${artifact}"
    write_sha256 "${artifact}"
    printf 'Created %s\n' "${artifact}"
}

package_windows()
{
    local artifact
    # A GUI-subsystem binary may print nothing to a pipe: the exit status is
    # what counts here.
    "${binary}" --version >/dev/null 2>&1 || { printf '%s --version failed\n' "${binary}" >&2; exit 1; }
    staging=$(mktemp -d "${dist_dir}/.${app}.zip.XXXXXX")
    cp -- "${binary}" "${staging}/${app}.exe"
    artifact="${dist_dir}/${app}-${version}-windows-x86_64.zip"
    rm -f -- "${artifact}"
    if command -v 7z >/dev/null; then
        (cd -- "${staging}" && 7z a -tzip -mx=9 "${artifact}" "${app}.exe" >/dev/null)
    elif command -v zip >/dev/null; then
        (cd -- "${staging}" && zip -9 -q "${artifact}" "${app}.exe")
    else
        printf 'Windows packaging needs 7z or zip\n' >&2
        exit 1
    fi
    write_sha256 "${artifact}"
    printf 'Created %s\n' "${artifact}"
}

# The deployment target the linker recorded in the binary.
macos_min_version()
{
    otool -l "$1" | awk '
        /LC_BUILD_VERSION/ { build = 1 }
        build && $1 == "minos" { print $2; exit }
        /LC_VERSION_MIN_MACOSX/ { legacy = 1 }
        legacy && $1 == "version" { print $2; exit }'
}

package_macos()
{
    local arch bundle iconset size artifact tmp minos dmg_dir
    arch=$(lipo -archs "${binary}")
    if [[ -n "${MACOS_ARCH:-}" && "${arch}" != "${MACOS_ARCH}" ]]; then
        printf 'Built %s for %s, expected %s\n' "${binary}" "${arch}" "${MACOS_ARCH}" >&2
        exit 1
    fi
    minos=$(macos_min_version "${binary}")
    [[ -n "${minos}" ]] || { printf 'No deployment target in %s\n' "${binary}" >&2; exit 1; }

    staging=$(mktemp -d "${dist_dir}/.${app}.dmg.XXXXXX")
    bundle="${staging}/${app}.app"
    mkdir -p "${bundle}/Contents/MacOS" "${bundle}/Contents/Resources"
    install -m 0755 "${binary}" "${bundle}/Contents/MacOS/${app}"

    # The icon in every size up to the artwork's own.
    iconset="${staging}/${app}.iconset"
    mkdir -p "${iconset}"
    local source_size
    source_size=$(sips -g pixelWidth "${icon}" | awk '/pixelWidth/ { print $2 }')
    for size in 16 32 128 256 512; do
        if ((size <= source_size)); then
            sips -z "${size}" "${size}" "${icon}" --out "${iconset}/icon_${size}x${size}.png" >/dev/null
        fi
        if ((size * 2 <= source_size)); then
            sips -z $((size * 2)) $((size * 2)) "${icon}" \
                --out "${iconset}/icon_${size}x${size}@2x.png" >/dev/null
        fi
    done
    iconutil --convert icns --output "${bundle}/Contents/Resources/${app}.icns" "${iconset}"
    rm -rf -- "${iconset}"

    cat > "${bundle}/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleDisplayName</key><string>${app}</string>
    <key>CFBundleExecutable</key><string>${app}</string>
    <key>CFBundleIconFile</key><string>${app}.icns</string>
    <key>CFBundleIdentifier</key><string>${APP_BUNDLE_IDS[${app}]}</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>${app}</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${version}</string>
    <key>CFBundleVersion</key><string>${version}</string>
    <key>LSMinimumSystemVersion</key><string>${minos}</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSHumanReadableCopyright</key><string>Open Development</string>
</dict>
</plist>
PLIST
    plutil -lint "${bundle}/Contents/Info.plist" >/dev/null

    local identity=${MACOS_CODESIGN_IDENTITY:--}
    local sign_args=(--force --deep --sign "${identity}")
    [[ "${identity}" != - ]] && sign_args+=(--options runtime --timestamp)
    codesign "${sign_args[@]}" "${bundle}"
    codesign --verify --deep --strict "${bundle}"
    check_version "${bundle}/Contents/MacOS/${app}"

    dmg_dir="${staging}/dmg"
    mkdir -p "${dmg_dir}"
    ditto "${bundle}" "${dmg_dir}/${app}.app"
    ln -s /Applications "${dmg_dir}/Applications"
    artifact="${dist_dir}/${app}-${version}-macos-${arch}.dmg"
    tmp="${dist_dir}/.${app}-${version}-macos-${arch}.dmg"
    rm -f -- "${tmp}"
    hdiutil create -quiet -ov -format UDZO -volname "${app}-${version}" \
        -srcfolder "${dmg_dir}" "${tmp}"
    hdiutil verify -quiet "${tmp}"
    mv -f -- "${tmp}" "${artifact}"
    write_sha256 "${artifact}"
    printf 'Created %s\n' "${artifact}"
}

case "${platform}" in
    linux) package_linux ;;
    windows) package_windows ;;
    macos) package_macos ;;
    *) printf 'Unknown platform: %s\n' "${platform}" >&2; exit 2 ;;
esac
