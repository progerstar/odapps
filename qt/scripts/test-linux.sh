#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd -- "${script_dir}/.." && pwd)

build_root=${1:-"${root_dir}/build/linux-tests"}
qmake_bin=${QMAKE:-qmake}
make_bin=${MAKE:-make}
jobs=${JOBS:-$(nproc)}

tests=(lfmemoryprotocol rfidfirmwareversion)

command -v "${qmake_bin}" >/dev/null
command -v "${make_bin}" >/dev/null

for test_name in "${tests[@]}"; do
    test_build_dir="${build_root}/${test_name}"
    mkdir -p "${test_build_dir}"
    printf '\n==> Building Qt test %s\n' "${test_name}"
    (
        cd "${test_build_dir}"
        "${qmake_bin}" "${root_dir}/tests/${test_name}/${test_name}.pro" \
            CONFIG+=release CONFIG-=debug
        "${make_bin}" -j"${jobs}"
        QT_QPA_PLATFORM=offscreen "./${test_name}" -maxwarnings 0
    )
done
