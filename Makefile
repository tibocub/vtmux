# vtmux: needs a vaporUI checkout (default: next to this repo) and gcc. POSIX only for now.
#   make [VAPORUI=/path/to/vaporUI]
# config.h is your copy of config.def.h, never overwritten.
#   make TARGET=WIN32 CC=x86_64-w64-mingw32-gcc      cross-build vtmux.exe from Linux (or plain `make` under MSYS2)
VAPORUI ?= ../vaporUI
CC      ?= gcc
CFLAGS  ?= -Wall -O2
V       := $(VAPORUI)
ifeq ($(OS),Windows_NT)
TARGET ?= WIN32
endif
TARGET ?= POSIX

ifeq ($(TARGET),WIN32)
EXE      := vtmux.exe
PLATFORM := $(V)/src/platform/win32.c winproc.c
else
EXE      := vtmux
PLATFORM := $(V)/src/platform/posix.c
endif

SRCS := $(wildcard $(V)/src/*.c $(V)/src/widgets/*.c) $(PLATFORM) $(V)/src/ext/terminal.c $(V)/src/ext/termsel.c \
        $(wildcard $(V)/third_party/libvterm/src/*.c)

HDRS := $(wildcard $(V)/include/*.h $(V)/src/*.h $(V)/src/platform/*.h $(V)/src/widgets/*.h $(V)/src/ext/*.h)

$(EXE): vtmux.c config.h $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -DVUI_TERMINAL -I. -I$(V)/include -I$(V)/third_party/libvterm/include vtmux.c $(SRCS) -o $@

config.h:
	cp config.def.h $@

clean:
	rm -f vtmux vtmux.exe
.PHONY: clean
