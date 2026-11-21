# vtmux
Vapor terminal multiplexer: a dwm-style tiling multiplexer (tags, master/stack layout) built on
[vaporUI](../vaporUI)'s terminal widget. Linux today; Windows and vaporOS are the goal.

    make                      # needs ../vaporUI, or make VAPORUI=/path/to/vaporUI
    ./vtmux

Alt is the Mod key: Alt+c new, Alt+Shift+c close, Alt+j/k focus, Alt+h/l resize, Alt+i/d master count,
Alt+Shift+j/k rotate the stack, Alt+m monocle on/off, Alt+Ctrl+<number row> toggle a tag in the view (+Shift: on the terminal), Alt+Enter zoom, Alt+Tab previous tag, Alt+<number row> view tag, Alt+Shift+<number row> move to tag,
Alt+F1..F9 / Alt+Shift+F1..F9 the same on any layout, Alt+Shift+r reload the config, Alt+Shift+q quit.

## Configuration
Defaults are compile-time, like dwm: `make` copies `config.def.h` to `config.h` (yours, never overwritten); edit it
and rebuild. It lists every key, the tags, the master size and the terminal command. On top of that, `~/.config/vtmux.conf` is read at
start and again by Alt+Shift+r, no rebuild: see `vtmux.conf.example`.

    set mfact 0.6          set nmaster 2        set topbar 0        set arrange monocle        set combo 300        set layout fr        set term /bin/zsh
    bind Alt+x spawn htop  bind Alt+9 view 9    bind Alt+Shift+9 tag 9    unbind Alt+Enter

`set mfact`, `nmaster` and `arrange` give every tag the same start value; Alt+m, Alt+h/l and Alt+i/d then change only the tag you are on.

Chords are `Mod+key` (Alt, Shift, Ctrl). A letter is the key that types it on your layout, a digit is the key in that
position of the number row, so `Alt+1` is the same key on AZERTY. Actions: `spawn [cmd...]` (no quoting, 8 words
max), `kill`, `focus +1|-1`, `nmaster +1|-1`, `mfact +0.05|-0.05`, `arrange tile|monocle|toggle`, `rotatestack +1|-1`, `toggleview N`, `toggletag N`, `zoom`, `view N|prev|all`, `tag N|all`,
`reload`, `quit`. `tbind <chord> pageup|pagedown|lineup|linedown|top|bottom|copy|paste|none` sets keys of the focused terminal (scroll, copy, paste); unlisted keys keep the defaults: Shift+PageUp/Down, Shift+Home/End, Ctrl+Shift+C/V, mouse wheel, drag to select (Shift+drag when the program uses the mouse); `none` swallows a key. A bad line is skipped and reported in the bar. `$VTMUX_CONF` points at another file.
Note: `config.h` is copied from `config.def.h` once; delete it after updating to get new default keys (the reload key).

## Keyboard layouts
Tag keys are bound by *position* (`PK('1')` = the number row's first key), so they work on any layout. Terminals
that can't say which physical key was pressed (anything but kitty, foot, WezTerm, Ghostty, Alacritty) need to be
told your layout (vaporUI also asks the OS: GNOME/X settings, vconsole.conf, Windows): `layout = "fr"` in `config.h` or `VUI_LAYOUT=fr ./vtmux`. Layouts live in vaporUI
(`src/input.c`): add yours there. Letters are bound by what they type (`CH('q')`, like dwm's `XK_q`).

Status: Linux (desktop terminals and the console). vaporUI's Windows backend now handles keys, mouse and output
natively (see its docs/input.md), but the terminal widget that runs the shells is POSIX-only (forkpty), so vtmux
does not build on Windows yet: that needs a ConPTY back end in the widget (`CreatePseudoConsole`). vaporOS needs
smaller terminals and one libvterm (see vaporUI docs/small-targets.md).

## TODO
- [x] alt-m monocle mode (full screen terminals, rotate with alt-j/alt-k)
- [x] combo: Alt+1 then Alt+2 within 300 ms (`set combo <ms>`, 0 = off) views tags 1 and 2 together. On the Linux console and Windows console (which report key releases) it is exact: tags keep adding while Alt stays held. In a terminal window, which can't see Alt released, it is the time window; `toggleview` (Alt+Ctrl+number) is the exact way
- [x] pertag: each tag keeps its own arrangement (tile or monocle), master size and master count (a view of several tags shares one more set). Not done: a grid arrangement
- [x] rotatestack: Alt+Shift+j/k (the master area stays, the focused terminal moves with the stack)
- [ ] simple dmenu-like utility, simply opens a new terminal with the selected program with alt-p or runs scripts/processes in the background with alt-P
- [x] config file `~/.config/vtmux.conf` with reload (Alt+Shift+r). Not done: `~/.vtmux.conf` as a second location, quoting in `spawn`, setting the focus colour or tag names from the file
- [ ] display names of the terminals in the current focused tag(s) in the status bar as tabs
- [x] scroll / copy / paste keys of the terminal are configurable (`tbind`)
- [x] mouse selection with Ctrl+Shift+C / Ctrl+Shift+V (vaporUI's terminal widget). Not done: double/triple click, reading the system clipboard (paste uses vaporUI's in-process clipboard; the host terminal's own paste works)
