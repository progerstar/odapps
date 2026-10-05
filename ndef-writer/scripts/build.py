#!/usr/bin/env python3
"""
Builds the self-contained executable of the current platform with PyInstaller.

Result: dist/ndef-writer-<version>-<os>-<arch>[.exe] and a .sha256 file next to it.
PyInstaller cannot cross-compile, so every platform is built on its own runner (see the workflow).
"""
import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from ndef_writer import __version__  # noqa: E402


def platform_tag():
    system = {"linux": "linux", "windows": "windows", "darwin": "macos"}.get(platform.system().lower())
    arch = {"x86_64": "x86_64", "amd64": "x86_64", "arm64": "arm64", "aarch64": "arm64"}.get(platform.machine().lower())
    if not system or not arch:
        sys.exit("Unsupported platform: {} {}".format(platform.system(), platform.machine()))
    return system, arch


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--expect-version", help="fail unless the package version is exactly this (the release tag)")
    args = parser.parse_args()

    if args.expect_version and args.expect_version != __version__:
        sys.exit("Version mismatch: the tag says {}, ndef_writer.__version__ is {}".format(args.expect_version, __version__))

    system, arch = platform_tag()
    exe = ".exe" if system == "windows" else ""
    work, dist = ROOT / "build", ROOT / "dist"
    shutil.rmtree(work, ignore_errors=True)
    dist.mkdir(exist_ok=True)

    licenses = ROOT / "src" / "ndef_writer" / "THIRD_PARTY_LICENSES.txt"
    subprocess.run([
        sys.executable, "-m", "PyInstaller", "--onefile", "--console", "--noconfirm", "--clean",
        "--name", "ndef-writer",
        "--paths", str(ROOT / "src"),
        "--add-data", "{}{}ndef_writer".format(licenses, os.pathsep),
        "--distpath", str(work / "out"), "--workpath", str(work / "pyi"), "--specpath", str(work),
        str(ROOT / "packaging" / "entry.py"),
    ], check=True, cwd=ROOT)

    final = dist / "ndef-writer-{}-{}-{}{}".format(__version__, system, arch, exe)
    shutil.copy2(work / "out" / ("ndef-writer" + exe), final)
    if system != "windows":
        final.chmod(0o755)
    digest = hashlib.sha256(final.read_bytes()).hexdigest()
    (dist / (final.name + ".sha256")).write_text("{}  {}\n".format(digest, final.name), encoding="utf-8", newline="\n")
    print("Built {} ({} bytes, sha256 {})".format(final, final.stat().st_size, digest))


if __name__ == "__main__":
    main()
