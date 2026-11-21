# vtmux
Vapor terminal multiplexer: a dwm-style tiling multiplexer (tags, master/stack layout) built on
[vaporUI](../vaporUI)'s terminal widget. Meant for Linux, Windows and vaporOS.

    make                      # needs ../vaporUI, or make VAPORUI=/path/to/vaporUI
    make KEYMAP=fr            # French AZERTY number row (default: us = QWERTY)
    ./vtmux

Configuration is compile-time, like dwm: `make` copies `config.def.h` to `config.h` (yours, never overwritten);
edit it and rebuild. It lists every key, the tags, the master size and the terminal command.
Alt is the Mod key: Alt+c new, Alt+Shift+c close, Alt+j/k focus, Alt+h/l resize, Alt+i/d master count,
Alt+Enter zoom, Alt+Tab previous tag, Alt+<number row> view tag, Alt+Shift+<number row> move to tag,
Alt+F1..F9 / Alt+Shift+F1..F9 the same on any layout, Alt+Shift+q quit.

## Keyboard layouts
A terminal sends the *typed* character (Alt+& on AZERTY, not "Alt+1"), so each layout lists what its number row
types: `keymaps/us.h`, `keymaps/fr.h`. To add yours, copy one to `keymaps/<code>.h`, edit it, build with
`make KEYMAP=<code>` and send a pull request.

Status: Linux only. Windows needs a ConPTY backend in vaporUI's terminal widget; vaporOS needs smaller
terminals and one libvterm (see vaporUI docs/small-targets.md).
