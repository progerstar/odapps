# gpio_extender (Nuklear / SDL2)

A C rewrite of the original Qt **gpio_extender** controller, using the
[Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) immediate-mode GUI
library with the SDL2 renderer backend.

It controls a GPIO extender board over a serial port:

- **OUT 1..5** — stateful toggle buttons (green when on). Their combined state
  is pushed to the board periodically.
- **IN 6..10** — colored indicators (green = high, gray = low), refreshed from
  the board.

## Protocol

The board speaks a tiny `~`-prefixed protocol (8N1, fixed at 115200 baud).

One command is sent every 10 ms, alternating each tick (so each of the two
repeats every 20 ms):

| Command            | Meaning                                   |
| ------------------ | ----------------------------------------- |
| `~A`               | request the state of all inputs           |
| `~P` + 5 digits    | set outputs 1..5 (`'1'`/`'0'` per channel)|

Received and parsed:

| Response           | Meaning                                   |
| ------------------ | ----------------------------------------- |
| `~A` + 5 digits    | state of inputs 6..10                      |
| `~G` `<pin>` `<st>`| state of a single pin (`pin` = hex 6..A)  |
| `~I` `<ver...>`    | firmware version string                   |

## Build

Requires SDL2.

```sh
# macOS
brew install sdl2

# Debian / Ubuntu
sudo apt install libsdl2-dev

make
./gpio_extender      # or: make run
```

### Windows (cross-compiled from Linux)

Needs a MinGW-w64 toolchain and an SDL2 MinGW dev package. On Arch/Manjaro:
`yay -S mingw-w64-gcc mingw-w64-sdl2`. Then:

```sh
make windows                 # -> gpio_extender.exe
cp /usr/x86_64-w64-mingw32/bin/SDL2.dll .   # ship SDL2.dll next to the .exe
```

If SDL2 lives elsewhere (e.g. an unpacked `SDL2-devel-*-mingw` tarball), point
the build at it: `make windows SDL2_PREFIX=/path/to/x86_64-w64-mingw32`.

The Windows serial backend lives in `src/serial_win32.c` (Win32 `CreateFile` /
`DCB` / registry port enumeration); the POSIX `src/serial.c` is used elsewhere.

## Usage

The UI is in Russian:

1. Click **Скан** to enumerate serial ports (`/dev/cu.*` on macOS,
   `/dev/ttyUSB*` / `/dev/ttyACM*` on Linux).
2. Pick a **Порт**, then click **Подключить** (115200 8N1).
3. Toggle **Выход** buttons to drive outputs; watch **Вход** indicators for inputs.

## Layout

```
src/
  main.c     UI, protocol logic, main loop
  serial.c   POSIX serial helper (replaces Qt QSerialPort / SerialThread)
  serial.h
third_party/
  nuklear.h                  vendored single-header GUI library
  nuklear_sdl_renderer.h     vendored SDL2 renderer backend
```

## Notes

- The original threaded `transaction()` model is replaced by a single-threaded
  immediate-mode loop: the port stays open and is polled non-blocking each frame.
- The UI uses a Cyrillic-capable system TTF (Arial / Verdana / DejaVuSans),
  loaded with `nk_font_cyrillic_glyph_ranges()`, since the built-in Nuklear font
  has no Cyrillic glyphs. If none of the candidate fonts is found, it falls back
  to the Latin-only default font.
