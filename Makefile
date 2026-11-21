# vtmux: needs a vaporUI checkout (default: next to this repo) and gcc. POSIX only for now.
#   make [VAPORUI=/path/to/vaporUI]
# config.h is your copy of config.def.h, never overwritten.
VAPORUI ?= ../vaporUI
CC      ?= gcc
CFLAGS  ?= -Wall -O2
V       := $(VAPORUI)

SRCS := $(wildcard $(V)/src/*.c $(V)/src/widgets/*.c) $(V)/src/platform/posix.c $(V)/src/ext/terminal.c $(V)/src/ext/termsel.c \
        $(wildcard $(V)/third_party/libvterm/src/*.c)

vtmux: vtmux.c config.h $(SRCS)
	$(CC) $(CFLAGS) -DVUI_TERMINAL -I. -I$(V)/include -I$(V)/third_party/libvterm/include vtmux.c $(SRCS) -o $@

config.h:
	cp config.def.h $@

clean:
	rm -f vtmux
.PHONY: clean
