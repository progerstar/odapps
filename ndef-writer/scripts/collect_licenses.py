#!/usr/bin/env python3
"""
Regenerates src/ndef_writer/THIRD_PARTY_LICENSES.txt.

The texts come from the source distributions of the exact versions pinned in requirements.lock
(the installed wheels do not carry them) and from the Python that runs this script.
The result is committed, compiled into every executable and printed by `ndef-writer --licenses`.
Run it after changing the pinned versions of ndeflib or pyserial.
"""
import re
import subprocess
import sys
import sysconfig
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "src" / "ndef_writer" / "THIRD_PARTY_LICENSES.txt"

COMPONENTS = [
    # distribution, display name, license, license file name inside the sdist, homepage
    ("ndeflib", "ndeflib", "ISC", "LICENSE", "https://github.com/nfcpy/ndeflib"),
    ("pyserial", "pyserial", "BSD-3-Clause", "LICENSE.txt", "https://github.com/pyserial/pyserial"),
]


def pinned_version(name):
    match = re.search(r"^{}==([^\s;\\]+)".format(re.escape(name)), (ROOT / "requirements.lock").read_text(), re.M | re.I)
    if not match:
        sys.exit("{} is not pinned in requirements.lock".format(name))
    return match.group(1)


def sdist_license(name, version, filename):
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([sys.executable, "-m", "pip", "download", "--quiet", "--no-deps", "--no-binary", ":all:",
                        "--dest", tmp, "{}=={}".format(name, version)], check=True)
        archive = next(Path(tmp).glob("*.tar.gz"))
        with tarfile.open(archive) as tar:
            member = next(m for m in tar.getmembers() if m.name.count("/") == 1 and m.name.endswith("/" + filename))
            return tar.extractfile(member).read().decode("utf-8").strip()


def python_license():
    path = Path(sysconfig.get_paths()["stdlib"]) / "LICENSE.txt"
    if not path.exists():
        sys.exit("The Python license file was not found: {}".format(path))
    return path.read_text(encoding="utf-8").strip()


def main():
    rule = "=" * 78
    parts = [
        "ndef-writer executables contain the following third-party components.\n"
        "The ndef-writer source code itself is under the MIT license of the repository.\n"
    ]
    for dist, title, spdx, filename, url in COMPONENTS:
        version = pinned_version(dist)
        parts.append("{}\n{} {} - {}\n{}\n{}\n".format(rule, title, version, spdx, url, sdist_license(dist, version, filename)))
    parts.append("{}\nPython {} - Python Software Foundation License\nhttps://docs.python.org/3/license.html\n{}\n".format(
        rule, sys.version.split()[0], python_license()))
    parts.append("{}\nPyInstaller - GPL-2.0-or-later with the bootloader exception\n"
                 "https://github.com/pyinstaller/pyinstaller/blob/develop/COPYING.txt\n"
                 "The executable is made with the PyInstaller bootloader. The special exception of PyInstaller allows distributing\n"
                 "the result under any license; the bootloader text is not included here.\n".format(rule))
    OUTPUT.write_text("\n".join(parts), encoding="utf-8", newline="\n")
    print("Wrote {} ({} bytes)".format(OUTPUT.relative_to(ROOT), OUTPUT.stat().st_size))


if __name__ == "__main__":
    main()
