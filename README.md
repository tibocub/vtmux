# vtmux

The Vapor Terminal Multiplexer: a cross-platform dwm-style tiling multiplexer (tags, master/stack layout) built on
[vaporUI](../vaporUI)'s terminal widget.

    make                      # needs ../vaporUI, or make VAPORUI=/path/to/vaporUI
    ./vtmux

Alt is the Mod key: Alt+c new, Alt+Shift+c close, Alt+j/k focus, Alt+h/l resize, Alt+i/d master count,
Alt+Shift+j/k rotate all terminals in view (master included), Alt+p launcher (run a program in a new terminal), Alt+Shift+p launcher (run it in the background), Alt+m monocle on/off, Alt+Ctrl+<number row> toggle a tag in the view (+Shift: on the terminal), Alt+Enter zoom, Alt+Tab previous tag, Alt+<number row> view tag, Alt+Shift+<number row> move to tag,
Alt+F1..F9 / Alt+Shift+F1..F9 the same on any layout, Alt+Shift+r reload the config, Alt+Shift+q quit.

The bar is dwm's: dark with light text, a coloured background on the focused tag(s) and terminal (colours in
`config.h`). Tags and terminal tabs are clickable: left click views a tag, right click adds / removes it from the view,
Alt+left moves the focused terminal to it, Alt+right adds / removes the tag on that terminal; a click on a tab focuses it.


## Configuration

Defaults are compile-time, like dwm: `make` copies `config.def.h` to `config.h` (yours, never overwritten); edit it
and rebuild. It lists every key, the tags, the master size and the terminal command. On top of that, `~/.config/vtmux.conf` is read at
start and again by Alt+Shift+r, no rebuild: see `vtmux.conf.example`.

    set mfact 0.6          set nmaster 2        set topbar 0        set arrange monocle        set combo 80        set layout fr        set term /bin/zsh
    bind Alt+x spawn htop  bind Alt+9 view 9    bind Alt+Shift+9 tag 9    unbind Alt+Enter

`set mfact`, `nmaster` and `arrange` give every tag the same start value; Alt+m, Alt+h/l and Alt+i/d then change only the tag you are on.

Chords are `Mod+key` (Alt, Shift, Ctrl). A letter is the key that types it on your layout, a digit is the key in that
position of the number row, so `Alt+1` is the same key on AZERTY. Actions: `spawn [cmd...]` (no quoting, 8 words
max), `kill`, `focus +1|-1`, `nmaster +1|-1`, `mfact +0.05|-0.05`, `arrange tile|monocle|toggle`, `launch [bg]`, `rotatestack +1|-1`, `toggleview N`, `toggletag N`, `zoom`, `view N|prev|all`, `tag N|all`,
`reload`, `quit`. `tbind <chord> pageup|pagedown|lineup|linedown|top|bottom|copy|paste|none` sets keys of the focused terminal (scroll, copy, paste); unlisted keys keep the defaults: Shift+PageUp/Down, Shift+Home/End, Ctrl+Shift+C/V, mouse wheel, drag to select (Shift+drag when the program uses the mouse); `none` swallows a key. A bad line is skipped and reported in the bar. `$VTMUX_CONF` points at another file.
Note: `config.h` is copied from `config.def.h` once; delete it after updating to get new default keys (the reload key).


## Keyboard layouts

Tag keys are bound by *position* (`PK('1')` = the number row's first key), so they work on any layout. Terminals
that can't say which physical key was pressed (anything but kitty, foot, WezTerm, Ghostty, Alacritty) need to be
told your layout (vaporUI also asks the OS: GNOME/X settings, vconsole.conf, Windows): `layout = "fr"` in `config.h` or `VUI_LAYOUT=fr ./vtmux`. Layouts live in vaporUI
(`src/input.c`): add yours there. Letters are bound by what they type (`CH('q')`, like dwm's `XK_q`).


## Windows

    make                                            # On Windows with MSYS2
    make TARGET=WIN32 CC=x86_64-w64-mingw32-gcc     # cross-build vtmux.exe from Linux

Needs Windows 10 1809+ (vaporUI's terminal widget uses ConPTY). New terminals run `%COMSPEC%` (cmd.exe); set another
with `set term pwsh` (or `set term wsl`) in `%USERPROFILE%\.config\vtmux.conf`. The launcher lists the `.exe`, `.bat`,
`.cmd` and `.com` files of `%PATH%`; text with a space runs through `cmd /c`; the background run is detached with no window.
Terminal names are the command each terminal was started with (the live foreground name is Linux only). Keys come from
console records, so Ctrl/Alt/Shift combos, key releases (the exact combo) and the mouse work as on the Linux console.
Windows Terminal takes some keys before vtmux sees them: Alt+Enter (zoom), Alt+Ctrl+digit (toggleview), Ctrl+Shift+C/V
(use the terminal's own copy/paste, or `tbind`), Shift+PageUp/Down, F11. Rebind what you need, e.g.
`bind Alt+z zoom`, `bind Alt+Shift+t toggleview 1`; conhost itself only takes Alt+Enter. Windows has no clipboard read in
vaporUI: paste with the host terminal's own paste.

Status: Linux (desktop terminals and TTYs) works; Windows works; vaporOS needs smaller
terminals and one libvterm (see vaporUI docs/small-targets.md).

## TODO
- [x] alt-m monocle mode (full screen terminals, rotate with alt-j/alt-k)
- [x] combo: like dwm's patch, press a tag key while the previous tag key is still held (Alt+1, then 2 with 1 still down) to view both; any key release ends it (Linux and Windows consoles report releases). Terminal windows can't see releases, so there a second tag key within 80 ms (`set combo <ms>`, 0 = off) counts as held. `toggleview` (Alt+Ctrl+number) is the other way
- [x] pertag: each tag keeps its own arrangement (tile or monocle), master size and master count (a view of several tags shares one more set). Not done: a grid arrangement
- [x] rotatestack: Alt+Shift+j/k cycles every terminal in view, master and stack, one place; the focused terminal moves with its place
- [x] launcher (Alt+p / Alt+Shift+p): type to narrow the programs on $PATH (prefix matches first, then substring), Up/Down, Enter runs it in a new terminal or, with Alt+Shift+p, detached in the background; text with a space is run as typed (`sh -c`). Not done: fuzzy matching, scripts outside $PATH, history
- [x] config file `~/.config/vtmux.conf` with reload (Alt+Shift+r). Not done: `~/.vtmux.conf` as a second location, quoting in `spawn`, setting the focus colour or tag names from the file
- [x] terminals in view are tabs in the bar ("<number> - <name>", the name is the program in the terminal's foreground, read from /proc on Linux); click one to focus it. The same title is on the frame. Not done: names from the terminal's own title (OSC 0/2), which the widget doesn't expose yet
- [x] scroll / copy / paste keys of the terminal are configurable (`tbind`)
- [x] mouse selection with Ctrl+Shift+C / Ctrl+Shift+V (vaporUI's terminal widget). Not done: double/triple click, reading the system clipboard (paste uses vaporUI's in-process clipboard; the host terminal's own paste works)
