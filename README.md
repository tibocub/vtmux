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

## TODO
- [ ] alt-m monocle mode (full screen terminals, rotate with alt-j/alt-k)
- [ ] combo (like the patch for dwm, allow to populate the view with multiple tags, for example tags 1 and 2 with the control "alt-1-2")
- [ ] pertag (like the dwm patch, keep some info such as display mode for each tag, so tag 1 can be a grid and tag 2 a monocle)
- [ ] rotatestack (like the dwm patch, rotate the whole stack with alt-J/alt-K)
- [ ] simple dmenu-like utility, simply opens a new terminal with the selected program with alt-p or runs scripts/processes in the background with alt-P
- [ ] make a simple config system to load user config from ~/.config/vtmux.conf or ~/.vtmux.conf to allow dynamic configuartion editing and reloading
