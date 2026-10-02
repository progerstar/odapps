# OpenDev Qt applications

This directory consolidates seven legacy Qt 5 applications and their shared
source libraries:

- `odrfidkit`
- `odrfidcfg`
- `odrfidcfgM`
- `odhiddfu`
- `wdtmon3`
- `wdtmon3-mini` (formerly `wdtmon3_lite`)
- `iosenmon`

The original source directories are not required after this tree has been
created. All relative qmake includes resolve inside this directory.

## Linux

Install build and runtime dependencies on Ubuntu:

```bash
sudo apt install \
  build-essential curl qtbase5-dev qtbase5-private-dev \
  libqt5svg5-dev libqt5serialport5-dev \
  libudev-dev libusb-1.0-0-dev qtdeclarative5-dev \
  qtquickcontrols2-5-dev qttools5-dev-tools \
  qml-module-qtquick2 qml-module-qtquick-controls \
  qml-module-qtquick-controls2 qml-module-qtquick-layouts \
  qml-module-qtquick-window2 qml-module-qt-labs-settings \
  qml-module-qt-labs-platform qml-module-qt-labs-calendar \
  qml-module-qtquick-extras qml-module-qtgraphicaleffects patchelf file rsync zip unzip
```

`odrfidcfgM` uses Qt 5 Serial Bus. Ubuntu 26.04 no longer ships its Qt 5
development package, so prepare a local copy before the first build:

```bash
./scripts/bootstrap-qt5-serialbus-linux.sh
```

On distributions that still provide Qt 5 Serial Bus development files, the
bootstrap step is not required.

Build all applications:

```bash
./scripts/build-linux.sh
```

Creating AppImages additionally requires `appimagetool`. Put the executable in
`PATH`, or point the packaging script to it explicitly:

```bash
APPIMAGETOOL=/absolute/path/to/appimagetool ./scripts/package-linux.sh
```

The standalone `appimagetool` executable from AppImageKit is supported; the
script enables its extract-and-run mode, so FUSE is not required during
packaging. Set `APPIMAGE_RUNTIME_FILE` to a downloaded type-2 runtime to avoid
letting `appimagetool` fetch a mutable runtime during the build.

Build and create both portable tarballs and AppImages:

```bash
./scripts/package-linux.sh
```

Build or package only selected applications by setting `QT_APPS` to a
comma-separated list, for example:

```bash
QT_APPS=odrfidkit ./scripts/package-linux.sh
```

Artifacts and matching `.sha256` files are written to `dist/linux`. Package
names include the application version read from its `version_win.h` file.

Set `SKIP_APPIMAGE=1` to create tarballs without `appimagetool`, or
`SKIP_TARBALL=1` to create only AppImages. As with the Windows packager, set
`SKIP_BUILD=1` to reuse existing binaries.

## Native Windows x86-64

The GitHub workflow builds Windows packages natively with MSYS2 UCRT64. The
same build can be run in an MSYS2 UCRT64 shell after installing these packages:

```bash
pacman -S --needed \
  file make zip mingw-w64-ucrt-x86_64-angleproject \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-libusb \
  mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-qt5-base \
  mingw-w64-ucrt-x86_64-qt5-declarative \
  mingw-w64-ucrt-x86_64-qt5-graphicaleffects \
  mingw-w64-ucrt-x86_64-qt5-quickcontrols \
  mingw-w64-ucrt-x86_64-qt5-quickcontrols2 \
  mingw-w64-ucrt-x86_64-qt5-serialbus \
  mingw-w64-ucrt-x86_64-qt5-svg \
  mingw-w64-ucrt-x86_64-qt5-tools \
  mingw-w64-ucrt-x86_64-qt5-translations

QMAKE=qmake-qt5 MAKE=mingw32-make WDEPLOYQT=windeployqt-qt5 \
  ./scripts/package-windows-native.sh
```

The packager uses `windeployqt`, includes the MinGW runtime, Qt plugins and QML
modules, and writes versioned ZIP archives plus `.sha256` files to
`dist/windows`.

## Windows x86-64 from Linux

MXE is used because the Qt libraries installed by Ubuntu only target Linux.
Install the MXE host dependencies on Ubuntu 26.04:

```bash
sudo apt install \
  autoconf automake autopoint bash bison bzip2 cmake flex g++ g++-multilib \
  gettext git gperf intltool libc6-dev-i386 libclang-dev \
  libgdk-pixbuf-2.0-dev libltdl-dev libgl-dev libpcre2-dev libssl-dev \
  libtool-bin libxml-parser-perl lzip make ninja-build openssl 7zip patch \
  perl pkg-config \
  python3 python3-mako python3-packaging python3-pkg-resources \
  python3-setuptools python-is-python3 ruby sed sqlite3 unzip wget xz-utils
```

Prepare a shared Qt 5 MXE toolchain (the first source build can take a long
time):

```bash
./scripts/bootstrap-mxe.sh
```

Then build and package all applications:

```bash
./scripts/build-windows.sh
./scripts/package-windows.sh
```

The same filter is supported for Windows builds and packages:

```bash
QT_APPS=odrfidkit ./scripts/package-windows.sh
```

Artifacts and matching `.sha256` files are written to `dist/windows`. Set
`MXE_ROOT` or `MXE_QMAKE` to use an existing MXE installation. Set
`SKIP_BUILD=1` when a packaging script should reuse an existing build.

## macOS

Install Qt 5, Bash 4 or newer, libusb and pkg-config with Homebrew:

```bash
brew install bash libusb pkgconf qt@5
export PATH="$(brew --prefix bash)/bin:$(brew --prefix qt@5)/bin:$PATH"
export PKG_CONFIG_PATH="$(brew --prefix libusb)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LIBUSB_ROOT="$(brew --prefix libusb)"
```

Build and create one DMG per application for the current Mac architecture:

```bash
./scripts/build-macos.sh
./scripts/package-macos.sh
```

An Apple Silicon host creates `macos-arm64` packages and an Intel host creates
`macos-x86_64` packages. `macdeployqt` embeds Qt frameworks, plugins and QML
modules; the packaging script also embeds libusb, validates dynamic-library
paths, creates an Applications shortcut, verifies the DMG and writes a
`.sha256` file.

By default, bundles receive an ad-hoc signature. To use an installed Developer
ID certificate instead, set `MACOS_CODESIGN_IDENTITY` to its full identity.
Notarization still requires Apple credentials and is intentionally not done by
the public workflow.

## GitHub Actions and releases

`.github/workflows/qt-apps.yml` builds all seven applications on Linux x86-64,
Windows x86-64, macOS ARM64 and macOS x86-64. Normal pushes and manual runs
store packages as workflow artifacts. A semantic aggregate tag publishes all
packages in one GitHub Release:

```bash
git tag -a qt-apps-v1.0.0 -m "Qt applications 1.0.0"
git push origin qt-apps-v1.0.0
```

The number after `qt-apps-v` is the version of the release set; individual
application versions remain independent and are included in filenames and in
`qt-apps-versions.txt`. The release also contains per-file checksums and a
combined `qt-apps-SHA256SUMS.txt`. Firmware directories are outside this
workflow's path filters and are not built by it.
