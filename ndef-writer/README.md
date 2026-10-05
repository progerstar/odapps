# ndef-writer

Command line tool that writes NDEF messages to Mifare Classic, Ultralight and NTAG tags
with an ODRFID reader (ODRFID-N, ODRFID-M) over its USB serial (CDC AT) interface.
It is released as **self-contained executables**: Python is not needed to run it.

Parameters and examples: <https://unitx.pro/docs/rfid-nfc/ndef/>

| Command | Writes |
| --- | --- |
| `text`, `uri`, `poster` | NDEF Text, URI, Smart Poster |
| `mime` | MIME record (for example `text/vcard`) or External Type |
| `raw` | a ready NDEF message, also with several records |
| `batch` | one tag per line of a file |
| `format` | returns a Mifare Classic card to the factory keys |

```text
ndef-writer -p /dev/ttyACM0 uri https://unitx.pro
ndef-writer -p COM3 text "Hello" --lang en
ndef-writer --help
```

Tags: NFC Forum Type 2 (Ultralight, Ultralight EV1, NTAG213/215/216) and Mifare Classic (MAD1).

## Download

Every release is built by GitHub Actions from a tag `ndef-writer-vX.Y.Z` and contains only the executables
and one checksum list:

| Platform | File |
| --- | --- |
| Linux x86-64 | `ndef-writer-X.Y.Z-linux-x86_64` |
| Windows x86-64 | `ndef-writer-X.Y.Z-windows-x86_64.exe` |
| macOS, Apple silicon | `ndef-writer-X.Y.Z-macos-arm64` |
| macOS, Intel | `ndef-writer-X.Y.Z-macos-x86_64` |
| all | `ndef-writer-SHA256SUMS.txt` |

The executables are **not code-signed**:

- Linux and macOS: `chmod +x ndef-writer-*`. On macOS also run `xattr -d com.apple.quarantine ndef-writer-*`.
- Windows: if SmartScreen stops the file, choose "More info", then "Run anyway".
- Linux executables are built on Ubuntu 22.04 and need glibc 2.35 or newer.
- On Linux the user must be allowed to open the port (group `dialout` or `uucp`).
- Close ODRFIDKit Web, terminals and other copies of the tool before you start: they hold the port.

Exit codes: `0` done, `1` the operation failed (one line `Error: …`), `2` internal error, `130` interrupted.
`-v` shows the AT exchange and a traceback. `ndef-writer --licenses` shows the licenses of the bundled components.

## Layout

```text
ndef-writer/
  src/ndef_writer/   api.py (reader, tags), mad.py (Mifare Application Directory), cli.py
  tests/             unit tests; the reader and the tags are modelled in the tests, no hardware needed
  scripts/           build.py, smoke_test.py, collect_licenses.py
  packaging/         PyInstaller entry point
  requirements.in    direct dependencies
  requirements.lock  all dependencies for all platforms, with hashes
```

The tool does not depend on the Qt applications in `../qt`, and the two release flows do not share anything:
own folder, own workflow (`.github/workflows/ndef-writer.yml`), own tags (`ndef-writer-v*` against `qt-apps-v*`),
own artifacts and GitHub Releases.

## Develop

```bash
python -m venv .venv && . .venv/bin/activate          # Windows: .venv\Scripts\activate
python -m pip install --require-hashes --no-deps -r requirements.lock
python -m unittest discover -s tests
PYTHONPATH=src python -m ndef_writer --help           # run from the sources
python scripts/build.py                                # dist/ndef-writer-<version>-<os>-<arch>
python scripts/smoke_test.py dist/ndef-writer-<version>-<os>-<arch> <version>
```

PyInstaller cannot cross-compile: build on the platform you build for (the workflow does this on four runners).

Change the dependencies in `requirements.in`, then:

```bash
uv pip compile requirements.in --universal --generate-hashes --python-version 3.14 -o requirements.lock
python scripts/collect_licenses.py        # refreshes src/ndef_writer/THIRD_PARTY_LICENSES.txt
```

## Release

1. Raise `__version__` in `src/ndef_writer/__init__.py` and merge it to `main`.
2. Tag and push: `git tag ndef-writer-v1.2.3 && git push origin ndef-writer-v1.2.3`.

The workflow refuses a tag that differs from `__version__`, runs the tests on Linux, Windows and macOS,
builds the four executables, smoke tests each of them and publishes the GitHub Release.

ndef-writer releases are created with `--latest=false`, and the workflow fails if one becomes "Latest" anyway.
The site links the Qt applications through `releases/latest/download/<app>-<os>-<arch>…`, which follows the
"Latest" release of the whole repository: if an ndef-writer release takes it, every one of those links returns 404.
Restore it with `gh release edit <newest qt-apps-v tag> --latest`. Link ndef-writer itself with versioned URLs
(`releases/download/ndef-writer-vX.Y.Z/…`).

## Hardware validation

Linux build, ODRFID3-N (firmware 3.13n), Mifare Classic 1K: Text, URI, Smart Poster, MIME, raw, batch, `format`,
refused rewrite, maximum message (667 bytes), refused oversized message, parallel access to the port.
Not run on hardware: Ultralight/NTAG (covered by the model in the tests), Classic Mini/2K/4K, Windows, macOS.
