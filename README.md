# vtmux
Vapor terminal multiplexer: a dwm-style tiling multiplexer (tags, master/stack layout) built on
[vaporUI](../vaporUI)'s terminal widget. Meant for Linux, Windows and vaporOS.

    make                      # needs ../vaporUI, or make VAPORUI=/path/to/vaporUI
    ./vtmux

Configuration is compile-time, like dwm: `make` copies `config.def.h` to `config.h` (yours, never overwritten);
edit it and rebuild. It lists every key, the tags, the master size and the terminal command.
Alt is the Mod key: Alt+c new, Alt+Shift+c close, Alt+j/k focus, Alt+h/l resize, Alt+i/d master count,
Alt+Enter zoom, Alt+Tab previous tag, Alt+<number row> view tag, Alt+Shift+<number row> move to tag,
Alt+F1..F9 / Alt+Shift+F1..F9 the same on any layout, Alt+Shift+q quit.

## Keyboard layouts
Tag keys are bound by *position* (`PK('1')` = the number row's first key), so they work on any layout. Terminals
that can't say which physical key was pressed (anything but kitty, foot, WezTerm, Ghostty, Alacritty) need to be
told your layout: `layout = "fr"` in `config.h` or `VUI_LAYOUT=fr ./vtmux`. Layouts live in vaporUI
(`src/input.c`): add yours there. Letters are bound by what they type (`CH('q')`, like dwm's `XK_q`).

Status: Linux only. Windows needs a ConPTY backend in vaporUI's terminal widget; vaporOS needs smaller
terminals and one libvterm (see vaporUI docs/small-targets.md).
