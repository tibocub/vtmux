# vtmux
Vapor terminal multiplexer: a dwm-style tiling multiplexer (tags, master/stack layout) built on
[vaporUI](../vaporUI)'s terminal widget. Meant for Linux, Windows and vaporOS.

    make                      # needs ../vaporUI, or make VAPORUI=/path/to/vaporUI
    ./vtmux

Keys are listed at the top of `vtmux.c` (Alt is the Mod key: Alt+c new, Alt+C close, Alt+1..9 view tag,
Alt+Shift+1..9 move to tag, Alt+j/k focus, Alt+h/l resize, Alt+Enter zoom, Alt+Q quit).

Status: Linux only. Windows needs a ConPTY backend in vaporUI's terminal widget; vaporOS needs smaller
terminals and one libvterm (see vaporUI docs/small-targets.md).
