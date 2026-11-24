# shellcheck shell=bash
# Packaging metadata and helpers for the Rust applications in rust/<app>/.
# Sourced by package.sh and by .github/workflows/rust-app.yml.
#
# Each application is a Cargo package whose binary has the package's name;
# the version comes from its Cargo.toml.

# shellcheck disable=SC2034  # the arrays are read by the scripts that source this file
declare -gA APP_DESCRIPTIONS=(
    [odrfid-tool-modbus]="ODRFID-RS485 reader configuration over Modbus RTU"
    [wdtmon-rs]="USB WatchDog monitor over USB HID"
)

# shellcheck disable=SC2034
declare -gA APP_BUNDLE_IDS=(
    [odrfid-tool-modbus]="ru.open-dev.odrfid-tool-modbus"
    [wdtmon-rs]="ru.open-dev.wdtmon-rs"
)

# PNG artwork, relative to rust/<app>/. Square, 256 px or larger.
# shellcheck disable=SC2034
declare -gA APP_ICONS=(
    [odrfid-tool-modbus]="assets/icon-256.png"
    [wdtmon-rs]="assets/icon-512.png"
)

# Where the release notes send the reader.
# shellcheck disable=SC2034
declare -gA APP_DOCS=(
    [odrfid-tool-modbus]="https://unitx.pro/docs/rfid-nfc/odrfid-tool-modbus/"
    [wdtmon-rs]="https://unitx.pro/docs/watchdog/manual/"
)

ALL_APP_NAMES=(odrfid-tool-modbus wdtmon-rs)

rust_root()
{
    local here
    here=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
    dirname -- "${here}"
}

require_app()
{
    local app=$1
    [[ -n "${APP_DESCRIPTIONS[${app}]:-}" ]] || {
        printf 'Unknown application: %s (known: %s)\n' "${app}" "${ALL_APP_NAMES[*]}" >&2
        return 1
    }
}

# The version from the [package] section of rust/<app>/Cargo.toml.
app_version()
{
    local app=$1 manifest version
    manifest="$(rust_root)/${app}/Cargo.toml"
    version=$(awk '
        /^\[/ { in_package = ($0 == "[package]") }
        in_package && /^version[[:space:]]*=/ {
            sub(/^version[[:space:]]*=[[:space:]]*"/, ""); sub(/".*$/, ""); print; exit
        }' "${manifest}")
    [[ "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
        printf 'No x.y.z version in %s (got "%s")\n' "${manifest}" "${version}" >&2
        return 1
    }
    printf '%s\n' "${version}"
}

# Writes <artifact>.sha256 next to the artifact, in `sha256sum` format.
write_sha256()
{
    local artifact=$1 dir name
    [[ -f "${artifact}" ]] || {
        printf 'Cannot checksum missing artifact: %s\n' "${artifact}" >&2
        return 1
    }
    dir=$(dirname -- "${artifact}")
    name=$(basename -- "${artifact}")
    (
        cd -- "${dir}"
        if command -v sha256sum >/dev/null; then
            sha256sum -- "${name}" > "${name}.sha256"
        else
            shasum -a 256 -- "${name}" > "${name}.sha256"
        fi
    )
}
