# gpio_extender (Nuklear / SDL2)
#
# Native build (Linux / macOS):   make            -> ./gpio_extender
#   Uses the POSIX serial backend (src/serial.c). Requires SDL2:
#     macOS:          brew install sdl2
#     Debian/Ubuntu:  sudo apt install libsdl2-dev
#
# Windows cross build:            make windows    -> ./gpio_extender.exe
#   Uses the Win32 serial backend (src/serial_win32.c). Requires a MinGW-w64
#   toolchain plus an SDL2 MinGW development package. If SDL2 is not on the
#   compiler's default search path, point SDL2_PREFIX at the dir that holds
#   include/ and lib/, e.g.:
#     make windows SDL2_PREFIX=/usr/x86_64-w64-mingw32
#     make windows SDL2_PREFIX=$HOME/SDL2-2.30.0/x86_64-w64-mingw32
#   Ship SDL2.dll next to the .exe (unless you link SDL2 statically).

CC      ?= cc
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra
BIN      = gpio_extender
MAIN     = src/main.c
HDRS     = src/serial.h third_party/nuklear.h third_party/nuklear_sdl_renderer.h

# ---- Native (POSIX) build --------------------------------------------------
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)

# Fallback if pkg-config is unavailable: guess a Homebrew prefix
# (/opt/homebrew on Apple Silicon, /usr/local on Intel macOS).
ifeq ($(strip $(SDL_LIBS)),)
BREW_PREFIX := $(shell test -d /opt/homebrew && echo /opt/homebrew || echo /usr/local)
SDL_CFLAGS := -I$(BREW_PREFIX)/include/SDL2 -D_THREAD_SAFE
SDL_LIBS   := -L$(BREW_PREFIX)/lib -lSDL2
endif

LDLIBS := $(SDL_LIBS) -lm

all: $(BIN)

$(BIN): $(MAIN) src/serial.c $(HDRS)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(MAIN) src/serial.c $(LDLIBS)

# ---- Windows cross-compile (MinGW-w64) -------------------------------------
WIN_CC  ?= x86_64-w64-mingw32-gcc
WIN_BIN  = gpio_extender.exe

# SDL2 flags are discovered with pkg-config pointed at the MinGW pkgconfig dir
# (sdl2.pc supplies the correct include subdir -I.../include/SDL2, which the
# vendored nuklear backend needs since it includes <SDL.h>, not <SDL2/SDL.h>).
# SDL2_PREFIX overrides where to look, e.g. an unpacked SDL2 mingw tarball:
#   make windows SDL2_PREFIX=$HOME/SDL2-2.30.0/x86_64-w64-mingw32
SDL2_PREFIX ?=
ifeq ($(strip $(SDL2_PREFIX)),)
WIN_PKG_LIBDIR ?= /usr/x86_64-w64-mingw32/lib/pkgconfig
else
WIN_PKG_LIBDIR := $(SDL2_PREFIX)/lib/pkgconfig
endif

WIN_SDL_CFLAGS := $(shell PKG_CONFIG_LIBDIR=$(WIN_PKG_LIBDIR) pkg-config --cflags sdl2 2>/dev/null)
WIN_SDL_LIBS   := $(shell PKG_CONFIG_LIBDIR=$(WIN_PKG_LIBDIR) pkg-config --libs sdl2 2>/dev/null)

# Fallback for an SDL2 tarball without a .pc: build flags from SDL2_PREFIX.
# SDL2 on Windows must link in this order: -lmingw32 -lSDL2main -lSDL2.
ifeq ($(strip $(WIN_SDL_LIBS)),)
ifneq ($(strip $(SDL2_PREFIX)),)
WIN_SDL_CFLAGS := -I$(SDL2_PREFIX)/include -I$(SDL2_PREFIX)/include/SDL2
WIN_SDL_LIBS   := -L$(SDL2_PREFIX)/lib -lmingw32 -lSDL2main -lSDL2 -mwindows
endif
endif

# -mwindows (GUI subsystem, no console) comes from sdl2.pc's libs; advapi32 is
# needed by the Win32 serial backend (registry port enumeration).
WIN_LDLIBS := $(WIN_SDL_LIBS) -ladvapi32 -lm
# If you link SDL2 *statically* (-lSDL2-static), add its transitive deps too:
#   -lwinmm -limm32 -lversion -lsetupapi -lole32 -loleaut32 -lgdi32 \
#   -luuid -lcfgmgr32 -static-libgcc

windows: $(WIN_BIN)

$(WIN_BIN): $(MAIN) src/serial_win32.c $(HDRS)
	$(WIN_CC) $(CFLAGS) $(WIN_SDL_CFLAGS) -o $@ $(MAIN) src/serial_win32.c $(WIN_LDLIBS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) $(WIN_BIN)

.PHONY: all run clean windows
