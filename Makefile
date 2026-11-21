# vtmux: needs a vaporUI checkout (default: next to this repo) and gcc. POSIX only for now.
#   make [KEYMAP=fr] [VAPORUI=/path/to/vaporUI]
# KEYMAP picks keymaps/<KEYMAP>.h (default us = QWERTY). config.h is your copy of config.def.h, never overwritten.
VAPORUI ?= ../vaporUI
KEYMAP  ?= us
CC      ?= gcc
CFLAGS  ?= -Wall -O2
V       := $(VAPORUI)

SRCS := $(wildcard $(V)/src/*.c $(V)/src/widgets/*.c) $(V)/src/platform/posix.c $(V)/src/ext/terminal.c \
        $(wildcard $(V)/third_party/libvterm/src/*.c)

vtmux: vtmux.c config.h keymaps/$(KEYMAP).h .keymap $(SRCS)
	$(CC) $(CFLAGS) -DVUI_TERMINAL -DKEYMAP_H='"keymaps/$(KEYMAP).h"' -I. -I$(V)/include -I$(V)/third_party/libvterm/include vtmux.c $(SRCS) -o $@

config.h:
	cp config.def.h $@

# rebuild when KEYMAP changes
.keymap: FORCE
	@echo '$(KEYMAP)' | cmp -s - $@ || echo '$(KEYMAP)' > $@

clean:
	rm -f vtmux .keymap
.PHONY: clean FORCE
FORCE:
