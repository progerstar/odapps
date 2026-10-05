#!/usr/bin/env python3
"""
Checks a built executable without a reader: usage: smoke_test.py <executable> <expected version>

It proves that the executable starts, carries the licenses, has ndeflib and pyserial inside and reports errors
as one line with a non-zero exit code.
"""
import subprocess
import sys

BAD_PORT = "ndef-writer-smoke-test-no-such-port"


def run(exe, *args):
    return subprocess.run([exe, *args], capture_output=True, encoding="utf-8", errors="replace", timeout=120)


def main():
    exe, version = sys.argv[1], sys.argv[2]
    failures = []

    def check(name, ok, detail=""):
        print("{:<4} {}".format("ok" if ok else "FAIL", name))
        if not ok:
            failures.append(name)
            print("     " + detail.replace("\n", "\n     "))

    res = run(exe, "--version")
    check("--version", res.returncode == 0 and res.stdout.strip() == "ndef-writer " + version, res.stdout + res.stderr)

    res = run(exe, "--licenses")
    check("--licenses", res.returncode == 0 and all(word in res.stdout for word in ("ndeflib", "pyserial", "Python")), res.stdout[:300] + res.stderr)

    res = run(exe, "--help")
    check("--help lists every command", res.returncode == 0 and all(c in res.stdout for c in ("text", "uri", "poster", "raw", "mime", "format", "batch")), res.stdout)

    # The NDEF message is built (ndeflib inside) before the port is opened (pyserial inside, fails here)
    for name, args, expect in (
        ("text", ["text", "Привет"], "NDEF message:"),
        ("uri", ["uri", "https://unitx.pro"], "NDEF message:"),
        ("poster", ["poster", "https://unitx.pro", "--title", "ru:Заголовок", "--action", "exec"], "NDEF message:"),
        ("mime", ["mime", "text/vcard", "--text", "BEGIN:VCARD"], "NDEF message:"),
        ("raw", ["raw", "--hex", "D00000"], "NDEF message:"),
    ):
        res = run(exe, "-v", "-p", BAD_PORT, *args)
        check("{} reaches the port".format(name), res.returncode == 1 and expect in res.stdout and "Error: Cannot open" in res.stderr,
              "exit {}\n{}\n{}".format(res.returncode, res.stdout[-300:], res.stderr[-300:]))

    res = run(exe, "-p", BAD_PORT, "uri", "https://unitx.pro")
    lines = [line for line in res.stderr.splitlines() if line.strip()]
    check("an error is one line, no traceback", res.returncode == 1 and len(lines) == 1 and lines[0].startswith("Error: Cannot open"), res.stderr)

    res = run(exe, "raw", "--hex", "D10101547800")
    check("raw rejects extra bytes before the port", res.returncode == 1 and "extra byte" in res.stderr, res.stderr)

    res = run(exe, "mime", "plain", "--text", "x")
    check("mime rejects a bad type", res.returncode == 1 and "neither a MIME type" in res.stderr, res.stderr)

    res = run(exe)
    check("a command is required", res.returncode == 2, res.stdout + res.stderr)

    if failures:
        sys.exit("{} check(s) failed: {}".format(len(failures), ", ".join(failures)))
    print("All checks passed")


if __name__ == "__main__":
    main()
