#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd -- "${script_dir}/.." && pwd)
mxe_root=${MXE_ROOT:-"${root_dir}/toolchains/mxe"}
target=${MXE_TARGET:-x86_64-w64-mingw32.shared}
jobs=${JOBS:-$(nproc)}

missing_tools=()
for tool in 7z autoconf automake autopoint bison bzip2 cmake flex g++ gettext \
    git gperf intltoolize libtoolize lzip make ninja patch perl python python3 \
    ruby sqlite3 unzip wget xz; do
    command -v "${tool}" >/dev/null || missing_tools+=("${tool}")
done

if ((${#missing_tools[@]})); then
    printf 'Missing MXE host tools: %s\n' "${missing_tools[*]}" >&2
    printf '%s\n' 'Install the MXE host dependencies listed in README.md.' >&2
    exit 2
fi

if [[ ! -d "${mxe_root}/.git" ]]; then
    mkdir -p "$(dirname -- "${mxe_root}")"
    git clone --depth 1 https://github.com/mxe/mxe.git "${mxe_root}"
fi

make -C "${mxe_root}" \
    -j"${jobs}" \
    MXE_TARGETS="${target}" \
    qtbase qtsvg qtserialport qtserialbus qtdeclarative qtquickcontrols qtquickcontrols2 qttools

printf '\nMXE Qt qmake: %s\n' "${mxe_root}/usr/${target}/qt5/bin/qmake"
