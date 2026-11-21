# vtmux: needs a vaporUI checkout (default: next to this repo) and gcc. POSIX only for now.
VAPORUI ?= ../vaporUI
CC      ?= gcc
CFLAGS  ?= -Wall -O2
V       := $(VAPORUI)

SRCS := $(wildcard $(V)/src/*.c $(V)/src/widgets/*.c) $(V)/src/platform/posix.c $(V)/src/ext/terminal.c \
        $(wildcard $(V)/third_party/libvterm/src/*.c)

vtmux: vtmux.c $(SRCS)
	$(CC) $(CFLAGS) -DVUI_TERMINAL -I$(V)/include -I$(V)/third_party/libvterm/include $^ -o $@

clean:
	rm -f vtmux
.PHONY: clean
