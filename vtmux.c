// vtmux: vapor terminal multiplexer, dwm-style. Tags, master/stack tiling, Alt as the Mod key.
// Configuration is config.h (see config.def.h for the key list), like dwm.
//
// Verify:
//   - the bar (top by default) shows the tags: the viewed one highlighted, tags holding terminals marked with '.'
//   - the new-terminal key opens shells that tile (master left, stack right); the mfact / nmaster keys reshape live
//   - the focused terminal has the bold frame and the cursor; the focus keys move it
//   - tagging sends the focused terminal to another tag (it disappears from view), viewing that tag shows it, still running
//   - `exit` in a shell removes its terminal; the close key kills one
//   - resizing the window re-tiles; the quit key quits

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#define strtok_r strtok_s
#define VTMUX_PATH_SEP ";"
int vtmux_spawn_detached(const char *line); // winproc.c
unsigned long long vtmux_tick_ms(void);
#else
#include <sys/wait.h>
#include <unistd.h>
#define VTMUX_PATH_SEP ":"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vaporui.h>

#ifndef VUI_TERMINAL
int main(void) { puts("vtmux needs the vaporUI terminal widget (POSIX build)"); return 1; }
#else

typedef union {
    int i;
    unsigned int ui;
    float f;
    const void *v;
} Arg;

typedef struct {
    VuiModifiers mods;
    VuiKey key;
    VuiCodepoint ch; // for VUI_KEY_CHAR
    int physical;    // ch is the key's US-layout position (base), not the typed character
    void (*func)(const Arg *);
    Arg arg;
} Key;

static void spawn(const Arg *a);
static void killclient(const Arg *a);
static void focusstack(const Arg *a);
static void incnmaster(const Arg *a);
static void setmfact(const Arg *a);
static void zoom(const Arg *a);
static void view(const Arg *a);
static void tag(const Arg *a);
static void quit(const Arg *a);
static void arrange(const Arg *a);
static void launch(const Arg *a);
static void rotatestack(const Arg *a);
static void toggleview(const Arg *a);
static void toggletag(const Arg *a);
static void reload(const Arg *a);

#include "config.h"

#define NTAGS ((int)(sizeof(tags) / sizeof(tags[0])))
#define MAXC  16
_Static_assert(NTAGS <= 9, "vtmux supports at most 9 tags");

typedef struct {
    VuiTerminal *term;
    VuiBorder *frame;
    char title[56];    // "<number> - <name>": the frame's title and the bar tab
    char fallback[24]; // the name while the foreground process can't be read
    unsigned int num;
    unsigned int tags; // bit n = on tag n+1
    int dead;          // its program exited: removed at the next loop turn
} Client;

static Client cl[MAXC]; // tile order, master first
static int ncl, sel = -1;
static unsigned int curview = 1, prevview = 1;

// Per-tag settings (dwm's pertag): each tag keeps its own arrangement, master size and master count. Index 0 is
// for views of several tags at once.
enum { ARRANGE_TILE, ARRANGE_MONOCLE };
typedef struct { int arrange; float mfact; int nmaster; } Pertag;
static Pertag pertag[9 + 1];
static unsigned int serial;
static VuiNode *root, *screen, *bar;

// Key bindings and settings as they are now: config.h's defaults, then ~/.config/vtmux.conf on top
#define MAXBIND 128
typedef struct {
    Key k;
    const char *argv[9]; // spawn's command, NULL-terminated
    char text[256];      // storage for argv
} Bind;

static Bind binds[MAXBIND];
static int nbinds;
// Keys for the focused terminal itself (scrolling, copy, paste), set with `tbind`. Anything not listed keeps the
// widget's defaults (Shift+PageUp/Down, Shift+Home/End, Ctrl+Shift+C/V); `tbind <chord> none` swallows a key.
enum { T_NONE, T_PAGEUP, T_PAGEDOWN, T_LINEUP, T_LINEDOWN, T_TOP, T_BOTTOM, T_COPY, T_PASTE };
typedef struct { Key k; int action; } TermBind;
static TermBind tbinds[32];
static int ntbinds;
static char msg[96]; // shown in the bar until the next key
static const char *const *term_argv = termcmd;
static int top = topbar;

static int ptidx(void)
{
    for (int i = 0; i < NTAGS; i++) {
        if (curview == (1u << i)) return i + 1;
    }
    return 0;
}

#define PT (pertag[ptidx()])

static int key_matches(const Key *b, const VuiKeyEvent *k)
{
    return b->key == k->key && b->mods == k->modifiers && (k->key != VUI_KEY_CHAR || b->ch == (b->physical ? k->base : k->character));
}

// The terminal widget asks before it handles a key: 1 = ours
static int term_key(VuiTerminal *t, const VuiKeyEvent *k)
{
    for (int i = 0; i < ntbinds; i++) {
        if (!key_matches(&tbinds[i].k, k)) continue;

        switch (tbinds[i].action) {
        case T_PAGEUP:   terminal_scroll(t, (int)t->rows - 1); break;
        case T_PAGEDOWN: terminal_scroll(t, -((int)t->rows - 1)); break;
        case T_LINEUP:   terminal_scroll(t, 1); break;
        case T_LINEDOWN: terminal_scroll(t, -1); break;
        case T_TOP:      terminal_scroll(t, 1 << 30); break;
        case T_BOTTOM:   terminal_scroll_reset(t); break;
        case T_COPY:     terminal_copy(t); break;
        case T_PASTE:    terminal_paste(t); break;
        default:         break;
        }
        return 1;
    }
    return 0;
}

static int visible(int i) { return cl[i].tags & curview; }

// The nth visible client, or -1
static int nth(int n)
{
    for (int i = 0; i < ncl; i++) {
        if (visible(i) && n-- == 0) return i;
    }
    return -1;
}

static int count_visible(void)
{
    int n = 0;

    for (int i = 0; i < ncl; i++) n += visible(i) ? 1 : 0;
    return n;
}

// "<number> - <name>": the name is the program in the terminal's foreground right now (vim, htop, bash), read from
// /proc on Linux, else the command the terminal was started with.
// shortcut: Linux only for the live name; a name change shows at the next redraw, not by itself.
static void refresh_names(void)
{
    for (int i = 0; i < ncl; i++) {
        char name[24] = "";

#ifdef __linux__
        pid_t pg = tcgetpgrp(cl[i].term->fd);

        if (pg > 0) {
            char path[40];
            FILE *f;

            snprintf(path, sizeof(path), "/proc/%d/comm", (int)pg);
            f = fopen(path, "r");
            if (f != NULL) {
                if (fgets(name, sizeof(name), f) != NULL) name[strcspn(name, "\n")] = '\0';
                fclose(f);
            }
        }
#endif
        snprintf(cl[i].title, sizeof(cl[i].title), "%u - %s", cl[i].num, name[0] != '\0' ? name : cl[i].fallback);
    }
}

// Rebuild the visible tree from the clients: master column | stack column. Hidden terminals keep running.
static void retile(void)
{
    VuiTheme *th = vui_theme();
    int n = count_visible(), m = PT.nmaster < n ? PT.nmaster : n, mono = PT.arrange == ARRANGE_MONOCLE;
    VuiDiv *col[2] = { NULL, NULL };

    for (int i = 0; i < ncl; i++) {
        cl[i].frame->title = cl[i].title; // the array is reordered (zoom, rotate, close): the title lives in the slot
        if (cl[i].frame->node.parent != NULL) node_remove(&cl[i].frame->node);
    }
    while (screen->child_count > 0) { // the old, now empty columns
        VuiNode *old = screen->children[screen->child_count - 1];

        node_remove(old);
        node_destroy(old);
    }

    if (sel < 0 || !visible(sel)) sel = nth(0);

    for (int k = 0, seen = 0; k < ncl; k++) {
        int c;

        if (!visible(k) || (mono && k != sel)) continue; // monocle: only the focused terminal is shown

        c = mono ? 0 : seen++ >= m;
        if (col[c] == NULL) {
            col[c] = div_create(VUI_DIR_COLUMN);
            col[c]->node.basis = 0;
            col[c]->node.grow = mono ? 1.0f : c ? 1.0f - PT.mfact : (n > m ? PT.mfact : 1.0f);
            node_add(screen, &col[c]->node);
        }

        cl[k].frame->node.basis = 0;
        cl[k].frame->node.grow = 1;
        cl[k].frame->style = th->border;
        if (k == sel) {
            cl[k].frame->style.attr |= VUI_ATTR_BOLD;
            cl[k].frame->style.fg = focuscolor;
        }
        node_add(&col[c]->node, &cl[k].frame->node);
    }

    if (sel >= 0) vui_set_focus(vui_ctx(), &cl[sel].term->node);
}

static void on_exit_cb(VuiNode *t, int status)
{
    (void)status;
    for (int i = 0; i < ncl; i++) {
        if (&cl[i].term->node == t) cl[i].dead = 1; // destroyed later: we are inside the widget's own callback
    }
}

static void close_client(int i)
{
    if (cl[i].frame->node.parent != NULL) node_remove(&cl[i].frame->node);
    node_destroy(&cl[i].frame->node); // the terminal is its child
    for (int j = i; j < ncl - 1; j++) cl[j] = cl[j + 1];
    ncl--;
    if (sel == i) sel = -1;
    else if (sel > i) sel--;
}

static void reap(void)
{
    int changed = 0;

    for (int i = ncl - 1; i >= 0; i--) {
        if (cl[i].dead) close_client(i), changed = 1;
    }
    if (changed) retile();
}

static void spawn(const Arg *a)
{
    VuiTerminal *t;
    VuiBorder *b;
    const char *const *argv = a->v == termcmd ? term_argv : a->v;

    if (ncl == MAXC) return;

    t = terminal_create(argv != NULL && argv[0] != NULL ? argv : NULL); // NULL: $SHELL
    if (t == NULL) return;

    b = border_create(NULL);
    t->on_exit = on_exit_cb;
    t->on_key = term_key;
    b->node.direction = VUI_DIR_COLUMN;
    node_add(&b->node, &t->node);

    cl[ncl] = (Client){ .term = t, .frame = b, .tags = curview, .num = ++serial };
    {
#ifdef _WIN32
        const char *cmd = argv != NULL && argv[0] != NULL ? argv[0] : getenv("COMSPEC");
        const char *slash = cmd != NULL ? strrchr(cmd, '\\') : NULL;
#else
        const char *cmd = argv != NULL && argv[0] != NULL ? argv[0] : getenv("SHELL");
        const char *slash = cmd != NULL ? strrchr(cmd, '/') : NULL;
#endif

        snprintf(cl[ncl].fallback, sizeof(cl[ncl].fallback), "%s", slash != NULL ? slash + 1 : cmd != NULL ? cmd : "sh");
    }
    snprintf(cl[ncl].title, sizeof(cl[ncl].title), "%u - %s", cl[ncl].num, cl[ncl].fallback);
    b->title = cl[ncl].title;
    sel = ncl++;
    retile();
}

static void killclient(const Arg *a)
{
    (void)a;
    if (sel >= 0) cl[sel].dead = 1;
    reap();
}

static void focusstack(const Arg *a)
{
    int n = count_visible(), at = 0;

    if (n == 0) return;
    for (int k = 0; k < n; k++) {
        if (nth(k) == sel) at = k;
    }
    sel = nth((at + a->i + n) % n);
    retile();
}

// Like dwm's zoom: the focused client goes first in the tile order (the old master goes second)
static void zoom(const Arg *a)
{
    int m = nth(0);
    Client c;

    (void)a;
    if (sel < 0 || m < 0 || sel == m) return;

    c = cl[sel];
    for (int j = sel; j > 0; j--) cl[j] = cl[j - 1];
    cl[0] = c;
    sel = 0;
    retile();
}

static unsigned long long now_ms(void)
{
#ifdef _WIN32
    return vtmux_tick_ms();
#else
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000 + (unsigned long long)t.tv_nsec / 1000000;
#endif
}

static unsigned long long last_view_ms;
static int combo_ms = comboms;
// dwm's combo patch: pressing a tag key while the previous tag key is still held adds the tag (any key release ends
// the combo). That needs key releases (Linux console, Windows console). Terminal windows never report them, so there
// a second tag key within combo_ms counts as held.
static int exact_keys;   // the platform reports releases (we have seen one, or a modifier event)
static int combo_armed;  // a tag key was pressed and nothing has been released since

static void setview(unsigned int v)
{
    if (v == 0 || v == curview) return;
    prevview = curview;
    curview = v;
    retile();
}

// Alt+1 then Alt+2 within combo_ms views tags 1 and 2 together (dwm's combo patch, which uses "Mod still held": a
// terminal can't see Mod released, so it is a short time window instead; `set combo 0` turns it off)
static void view(const Arg *a)
{
    unsigned int v = a->ui & ((1u << NTAGS) - 1); // 0: back to the previous tag
    unsigned long long now = now_ms();
    int single = v != 0 && (v & (v - 1)) == 0;
    int armed = last_view_ms != 0 && (exact_keys ? combo_armed : now - last_view_ms <= (unsigned long long)combo_ms);

    if (v == 0) v = prevview;
    else if (single && combo_ms > 0 && armed && (curview & v) == 0) v |= curview;
    last_view_ms = single ? now : 0;
    combo_armed = single;
    setview(v);
}

// dwm's toggleview / toggletag: add or remove one tag from the view / from the focused terminal
static void toggleview(const Arg *a)
{
    setview(curview ^ (a->ui & ((1u << NTAGS) - 1)));
}

static void toggletag(const Arg *a)
{
    unsigned int t;

    if (sel < 0) return;
    t = cl[sel].tags ^ (a->ui & ((1u << NTAGS) - 1));
    if (t == 0) return;
    cl[sel].tags = t;
    retile();
}

static void tag(const Arg *a)
{
    unsigned int mask = a->ui & ((1u << NTAGS) - 1);

    if (sel < 0 || mask == 0) return;
    cl[sel].tags = mask;
    retile();
}

static void setmfact(const Arg *a)
{
    PT.mfact += a->f;
    if (PT.mfact < 0.1f) PT.mfact = 0.1f;
    if (PT.mfact > 0.9f) PT.mfact = 0.9f;
    retile();
}

static void incnmaster(const Arg *a)
{
    PT.nmaster += a->i;
    if (PT.nmaster < 1) PT.nmaster = 1;
    retile();
}

// dwm's rotatestack: every terminal in view, master area included, moves one place (+1: the last becomes the first,
// -1: the first becomes the last); the focused terminal moves with its place.
static void rotatestack(const Arg *a)
{
    int idx[MAXC], n = 0, first = 0;
    Client keep;
    VuiTerminal *focus = sel >= 0 ? cl[sel].term : NULL;

    for (int i = 0; i < ncl; i++) {
        if (visible(i)) idx[n++] = i;
    }
    if (n - first < 2) return;

    if (a->i > 0) {
        keep = cl[idx[n - 1]];
        for (int k = n - 1; k > first; k--) cl[idx[k]] = cl[idx[k - 1]];
        cl[idx[first]] = keep;
    } else {
        keep = cl[idx[first]];
        for (int k = first; k < n - 1; k++) cl[idx[k]] = cl[idx[k + 1]];
        cl[idx[n - 1]] = keep;
    }

    for (int i = 0; i < ncl; i++) {
        if (cl[i].term == focus) sel = i;
    }
    retile();
}

// .i: ARRANGE_TILE / ARRANGE_MONOCLE, or -1 to flip between monocle and tile
static void arrange(const Arg *a)
{
    PT.arrange = a->i < 0 ? (PT.arrange == ARRANGE_MONOCLE ? ARRANGE_TILE : ARRANGE_MONOCLE) : a->i;
    retile();
}

static void quit(const Arg *a)
{
    (void)a;
    vui_quit(vui_ctx());
}

// Returns 1 if the key was a vtmux command
static int command(const VuiKeyEvent *k)
{
    int repeatable;

    for (int i = 0; i < nbinds; i++) {
        const Key *b = &binds[i].k;

        repeatable = b->func == focusstack || b->func == rotatestack || b->func == setmfact || b->func == incnmaster;
        if (key_matches(b, k)) {
            if (k->repeat && !repeatable) return 1; // a held key must not spawn sixteen terminals
            b->func(&b->arg);
            return 1;
        }
    }
    return 0;
}

// ---- ~/.config/vtmux.conf ----------------------------------------------------------------------------------------
//   # comment
//   set mfact 0.6            set nmaster 2        set topbar 0        set layout fr        set term /bin/zsh
//   tbind Shift+PageUp pageup   tbind Ctrl+Shift+c copy   tbind Shift+Insert paste   (keys of the focused terminal)
//   bind Alt+Shift+c kill    bind Alt+x spawn htop    bind Alt+9 view 9    unbind Alt+Enter
// Chords are Mod+key, Mod = Alt, Shift, Ctrl in any order, key = a character, Enter Tab Escape Space Backspace Up Down
// Left Right Home End PageUp PageDown Insert Delete F1..F12. A letter is the key that types it on your layout; a digit
// is the key in that position of the number row (so Alt+1 is the same key on AZERTY). Actions: spawn [cmd...], kill,
// focus +1|-1, nmaster +1|-1, mfact +0.05|-0.05, zoom, view N|prev|all, tag N|all, reload, quit.
// shortcut: spawn's command is split on spaces (no quoting); at most 8 words.

static const struct { const char *name; void (*func)(const Arg *); } actions[] = {
    { "spawn", spawn }, { "kill", killclient }, { "focus", focusstack }, { "nmaster", incnmaster },
    { "mfact", setmfact }, { "zoom", zoom }, { "view", view }, { "tag", tag }, { "reload", reload }, { "quit", quit }, { "arrange", arrange }, { "launch", launch }, { "rotatestack", rotatestack }, { "toggleview", toggleview }, { "toggletag", toggletag },
};

static const struct { const char *name; VuiKey key; } named_keys[] = {
    { "enter", VUI_KEY_ENTER }, { "return", VUI_KEY_ENTER }, { "tab", VUI_KEY_TAB }, { "escape", VUI_KEY_ESCAPE },
    { "esc", VUI_KEY_ESCAPE }, { "backspace", VUI_KEY_BACKSPACE }, { "up", VUI_KEY_UP }, { "down", VUI_KEY_DOWN },
    { "left", VUI_KEY_LEFT }, { "right", VUI_KEY_RIGHT }, { "home", VUI_KEY_HOME }, { "end", VUI_KEY_END },
    { "pageup", VUI_KEY_PAGE_UP }, { "pagedown", VUI_KEY_PAGE_DOWN }, { "insert", VUI_KEY_INSERT }, { "delete", VUI_KEY_DELETE },
};

static int same_word(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return *a == *b;
}

// One UTF-8 character that is the whole string, or 0
static VuiCodepoint one_char(const char *s)
{
    const unsigned char *u = (const unsigned char *)s;
    int n = u[0] < 0x80 ? 1 : u[0] >= 0xF0 ? 4 : u[0] >= 0xE0 ? 3 : u[0] >= 0xC0 ? 2 : 0;
    VuiCodepoint c = n == 1 ? u[0] : n == 2 ? (u[0] & 0x1F) : n == 3 ? (u[0] & 0x0F) : (u[0] & 0x07);

    if (n == 0) return 0;
    for (int i = 1; i < n; i++) {
        if ((u[i] & 0xC0) != 0x80) return 0;
        c = (c << 6) | (u[i] & 0x3F);
    }
    return s[n] == '\0' ? c : 0;
}

static int parse_chord(char *s, Key *k)
{
    char *tok;

    *k = (Key){ 0 };
    while ((tok = strchr(s, '+')) != NULL && tok != s && tok[1] != '\0') {
        *tok = '\0';
        if (same_word(s, "alt") || same_word(s, "mod")) k->mods |= VUI_MOD_ALT;
        else if (same_word(s, "shift")) k->mods |= VUI_MOD_SHIFT;
        else if (same_word(s, "ctrl") || same_word(s, "control")) k->mods |= VUI_MOD_CTRL;
        else return -1;
        s = tok + 1;
    }

    for (unsigned int i = 0; i < sizeof(named_keys) / sizeof(named_keys[0]); i++) {
        if (same_word(s, named_keys[i].name)) { k->key = named_keys[i].key; return 0; }
    }
    if ((s[0] == 'F' || s[0] == 'f') && atoi(s + 1) >= 1 && atoi(s + 1) <= 12 && strspn(s + 1, "0123456789") == strlen(s + 1)) {
        k->key = (VuiKey)(VUI_KEY_F1 + atoi(s + 1) - 1);
        return 0;
    }
    if (same_word(s, "space")) { k->key = VUI_KEY_CHAR; k->ch = ' '; return 0; }

    k->ch = one_char(s);
    if (k->ch == 0) return -1;
    k->key = VUI_KEY_CHAR;
    k->physical = k->ch >= '0' && k->ch <= '9';
    if (k->ch < 0x80) k->ch = (VuiCodepoint)tolower((int)k->ch);
    return 0;
}

static int tag_arg(const char *s, unsigned int *mask)
{
    if (same_word(s, "all")) { *mask = ~0u; return 0; }
    if (same_word(s, "prev")) { *mask = 0; return 0; }
    if (s[0] >= '1' && s[0] <= '9' && s[1] == '\0' && s[0] - '1' < NTAGS) { *mask = 1u << (s[0] - '1'); return 0; }
    return -1;
}

// Copy a binding; its command words point into its own text
static void store(Bind *dst, const Bind *src)
{
    *dst = *src;
    for (int j = 0; j < 9; j++) {
        if (src->argv[j] != NULL) dst->argv[j] = dst->text + (src->argv[j] - src->text);
    }
    if (src->k.arg.v == (const void *)src->argv) dst->k.arg.v = dst->argv;
}

static void add_bind(const Bind *b)
{
    int at = nbinds;

    for (int i = 0; i < nbinds; i++) {
        const Key *o = &binds[i].k;

        if (o->mods == b->k.mods && o->key == b->k.key && o->ch == b->k.ch && o->physical == b->k.physical) at = i;
    }
    if (at < MAXBIND) {
        store(&binds[at], b);
        if (at == nbinds) nbinds++;
    }
}

static void remove_bind(const Key *k)
{
    for (int i = 0; i < nbinds; i++) {
        const Key *o = &binds[i].k;

        if (o->mods == k->mods && o->key == k->key && o->ch == k->ch && o->physical == k->physical) {
            binds[i] = binds[--nbinds];
            return;
        }
    }
}

// One line; returns an error text or NULL
static const char *conf_line(char *line)
{
    char *word[12], *save = NULL, *t;
    int n = 0;

    if ((t = strchr(line, '#')) != NULL) *t = '\0';
    for (t = strtok_r(line, " \t\r\n", &save); t != NULL && n < 12; t = strtok_r(NULL, " \t\r\n", &save)) word[n++] = t;
    if (n == 0) return NULL;

    if (same_word(word[0], "set")) {
        if (n < 3) return "set needs a name and a value";
        if (same_word(word[1], "mfact")) { for (int i = 0; i <= NTAGS; i++) pertag[i].mfact = (float)atof(word[2]); }
        else if (same_word(word[1], "nmaster")) { for (int i = 0; i <= NTAGS; i++) pertag[i].nmaster = atoi(word[2]); }
        else if (same_word(word[1], "arrange")) {
            if (!same_word(word[2], "tile") && !same_word(word[2], "monocle")) return "arrange is tile or monocle";
            for (int i = 0; i <= NTAGS; i++) pertag[i].arrange = same_word(word[2], "monocle");
        }
        else if (same_word(word[1], "topbar")) top = atoi(word[2]) != 0;
        else if (same_word(word[1], "combo")) combo_ms = atoi(word[2]);
        else if (same_word(word[1], "layout")) { if (vui_set_layout(word[2]) != 0) return "unknown layout"; }
        else if (same_word(word[1], "term")) {
            static char text[256];
            static const char *words[9];
            size_t at = 0;
            int w = 0;

            for (int i = 2; i < n && w < 8; i++) {
                size_t len = strlen(word[i]);

                if (at + len + 1 > sizeof(text)) return "term is too long";
                memcpy(text + at, word[i], len + 1);
                words[w++] = text + at;
                at += len + 1;
            }
            words[w] = NULL;
            term_argv = words;
        } else return "unknown setting";
        return NULL;
    }

    if (same_word(word[0], "tbind")) {
        static const char *const names[] = { "none", "pageup", "pagedown", "lineup", "linedown", "top", "bottom", "copy", "paste" };
        TermBind b = { { 0 }, -1 };

        if (n < 3) return "tbind needs a key and an action";
        if (parse_chord(word[1], &b.k) != 0) return "unknown key";
        for (int i = 0; i < 9; i++) {
            if (same_word(word[2], names[i])) b.action = i;
        }
        if (b.action < 0) return "tbind action: none pageup pagedown lineup linedown top bottom copy paste";

        for (int i = 0; i < ntbinds; i++) {
            if (tbinds[i].k.mods == b.k.mods && tbinds[i].k.key == b.k.key && tbinds[i].k.ch == b.k.ch) { tbinds[i] = b; return NULL; }
        }
        if (ntbinds == 32) return "too many tbind lines";
        tbinds[ntbinds++] = b;
        return NULL;
    }

    if (same_word(word[0], "unbind") || same_word(word[0], "bind")) {
        Bind b = { 0 };
        int bind = same_word(word[0], "bind");
        unsigned int mask;

        if (n < 2 || (bind && n < 3)) return bind ? "bind needs a key and an action" : "unbind needs a key";
        if (parse_chord(word[1], &b.k) != 0) return "unknown key";
        if (!bind) { remove_bind(&b.k); return NULL; }

        for (unsigned int i = 0; i < sizeof(actions) / sizeof(actions[0]); i++) {
            if (same_word(word[2], actions[i].name)) b.k.func = actions[i].func;
        }
        if (b.k.func == NULL) return "unknown action";

        if (b.k.func == spawn) {
            size_t at = 0;
            int w = 0;

            for (int i = 3; i < n && w < 8; i++) {
                size_t len = strlen(word[i]);

                if (at + len + 1 > sizeof(b.text)) return "command is too long";
                memcpy(b.text + at, word[i], len + 1);
                b.argv[w++] = b.text + at; // repointed to the stored copy by add_bind
                at += len + 1;
            }
            b.k.arg.v = w > 0 ? (const void *)b.argv : (const void *)termcmd;
        } else if (b.k.func == focusstack || b.k.func == incnmaster || b.k.func == rotatestack) {
            if (n < 4) return "needs +1 or -1";
            b.k.arg.i = atoi(word[3]);
        } else if (b.k.func == setmfact) {
            if (n < 4) return "needs +0.05 or -0.05";
            b.k.arg.f = (float)atof(word[3]);
        } else if (b.k.func == launch) {
            b.k.arg.i = n >= 4 && same_word(word[3], "bg");
        } else if (b.k.func == arrange) {
            if (n < 4) return "needs tile, monocle or toggle";
            b.k.arg.i = same_word(word[3], "tile") ? ARRANGE_TILE : same_word(word[3], "monocle") ? ARRANGE_MONOCLE
                      : same_word(word[3], "toggle") ? -1 : -2;
            if (b.k.arg.i == -2) return "needs tile, monocle or toggle";
        } else if (b.k.func == view || b.k.func == tag || b.k.func == toggleview || b.k.func == toggletag) {
            if (n < 4 || tag_arg(word[3], &mask) != 0) return "needs a tag 1-9, all or prev";
            if (mask == 0 && b.k.func != view) return "needs a tag 1-9 or all";
            b.k.arg.ui = mask;
        }

        add_bind(&b);
        return NULL;
    }

    return "unknown command";
}

static void place_bar(void)
{
    node_remove(bar);
    node_remove(screen);
    node_add(root, top ? bar : screen);
    node_add(root, top ? screen : bar);
}

// Defaults from config.h, then the file. A bad line is reported (the first one, in the bar) and skipped.
static void load_config(int announce)
{
#ifdef _WIN32
    const char *conf = getenv("VTMUX_CONF"), *h = getenv("USERPROFILE");
#else
    const char *conf = getenv("VTMUX_CONF"), *h = getenv("HOME");
#endif
    char path[512], line[512], first[96] = "";
    FILE *f;
    int lineno = 0, bad = 0;

    nbinds = 0;
    ntbinds = 0;
    for (unsigned int i = 0; i < sizeof(keys) / sizeof(keys[0]) && nbinds < MAXBIND; i++) binds[nbinds++].k = keys[i];
    for (int i = 0; i <= NTAGS; i++) pertag[i] = (Pertag){ ARRANGE_TILE, mfact, nmaster };
    top = topbar;
    combo_ms = comboms;
    term_argv = termcmd;
    vui_set_layout(layout);

    if (conf != NULL) snprintf(path, sizeof(path), "%s", conf);
    else if (h != NULL) snprintf(path, sizeof(path), "%s/.config/vtmux.conf", h);
    else path[0] = '\0';

    f = path[0] != '\0' ? fopen(path, "r") : NULL;
    if (f == NULL) {
        if (announce) snprintf(msg, sizeof(msg), "no %s: defaults", path);
    } else {
        while (fgets(line, sizeof(line), f) != NULL) {
            const char *err = conf_line(line);

            lineno++;

            if (err != NULL && !bad++) snprintf(first, sizeof(first), "vtmux.conf:%d: %s", lineno, err);
        }
        fclose(f);
        if (bad) snprintf(msg, sizeof(msg), "%s%s", first, bad > 1 ? " (+more)" : "");
        else if (announce) snprintf(msg, sizeof(msg), "reloaded");
    }

    if (bar != NULL) place_bar();
}

static void reload(const Arg *a)
{
    (void)a;
    load_config(1);
    retile();
}

// A click focuses a terminal in the widget tree: make it vtmux's selected one too (frame highlight, key targets)
static void follow_focus(void)
{
    VuiNode *f = vui_focus(vui_ctx());

    for (int i = 0; i < ncl; i++) {
        if (&cl[i].term->node == f && i != sel && visible(i)) {
            sel = i;
            retile();
            return;
        }
    }
}

// Tabs: one per terminal in view, clickable
typedef struct { unsigned int x0, x1; int client; } Tab;
static Tab tabs[MAXC];
static int ntabs;

typedef struct { unsigned int x0, x1; } TagCell;
static TagCell tagcells[9];

static void bar_draw(VuiNode *node, VuiScreen *scr, void *userdata)
{
    // dwm's look: dark bar, light text; the focused tag(s) and terminal get a coloured background
    VuiStyle norm = { .fg = barfg, .bg = barbg }, hi = { .fg = selfg, .bg = selbg };
    unsigned int occupied = 0, x = node->rect.x, end = node->rect.x + node->rect.width;
    char s[16];
    int n = count_visible();

    (void)userdata;
    scr_fill(scr, node->rect, ' ', norm);
    for (int i = 0; i < ncl; i++) occupied |= cl[i].tags;

    for (int i = 0; i < NTAGS; i++) {
        VuiStyle st = (curview & (1u << i)) ? hi : norm;

        snprintf(s, sizeof(s), " %s%c", tags[i], (occupied & (1u << i)) ? '.' : ' ');
        scr_puts(scr, x, node->rect.y, s, st);
        tagcells[i] = (TagCell){ x, x + vui_strn_width(s, strlen(s)) };
        x += vui_strn_width(s, strlen(s));
    }

    if (PT.arrange == ARRANGE_MONOCLE) snprintf(s, sizeof(s), " [M%d]", count_visible());
    else snprintf(s, sizeof(s), " [%d]", PT.nmaster);
    scr_puts(scr, x + 1, node->rect.y, s, norm);
    x += 1 + vui_strn_width(s, strlen(s)) + 1;

    if (msg[0] != '\0') { // a message takes the right end, over the tabs
        unsigned int w = vui_strn_width(msg, strlen(msg));

        if (end > x + w) scr_puts(scr, end - w, node->rect.y, msg, norm);
        end = end > x + w + 1 ? end - w - 1 : x;
    }

    ntabs = 0;
    for (int k = 0, i; k < n && end > x && (i = nth(k)) >= 0; k++) {
        unsigned int room = (end - x) / (unsigned int)(n - k), want = vui_strn_width(cl[i].title, strlen(cl[i].title)) + 2, w = want < room ? want : room;
        char cell[64];
        VuiStyle st = i == sel ? hi : norm;

        if (w < 4) break;
        scr_fill(scr, (VuiRect){ x, node->rect.y, w, 1 }, ' ', st);
        snprintf(cell, sizeof(cell), " %.*s", (int)(w - 2), cl[i].title); // ASCII names: a byte is a column
        scr_puts(scr, x, node->rect.y, cell, st);
        tabs[ntabs++] = (Tab){ x, x + w, i };
        x += w;
    }
}

static int bar_event(VuiNode *node, VuiCtx *ui, const VuiEvent *e)
{
    (void)node;
    (void)ui;
    if (e->type != VUI_EVENT_MOUSE || e->mouse.action != VUI_MOUSE_PRESS) return 0;

    // dwm's tag clicks: left views the tag, right adds / removes it from the view; with Alt held: move the
    // focused terminal to the tag (left) or add / remove the tag on it (right)
    for (int i = 0; i < NTAGS; i++) {
        if (e->mouse.x >= tagcells[i].x0 && e->mouse.x < tagcells[i].x1 && (e->mouse.button == VUI_MOUSE_LEFT || e->mouse.button == VUI_MOUSE_RIGHT)) {
            Arg a = { .ui = 1u << i };
            int left = e->mouse.button == VUI_MOUSE_LEFT;

            if (e->mouse.modifiers & VUI_MOD_ALT) {
                if (left) tag(&a);
                else toggletag(&a);
            } else if (left) {
                setview(a.ui); // not view(): two quick clicks are not a combo
            } else {
                toggleview(&a);
            }
            return 1;
        }
    }
    if (e->mouse.button != VUI_MOUSE_LEFT) return 0;

    for (int i = 0; i < ntabs; i++) {
        if (e->mouse.x >= tabs[i].x0 && e->mouse.x < tabs[i].x1 && tabs[i].client < ncl) {
            sel = tabs[i].client;
            retile();
            return 1;
        }
    }
    return 0;
}

// ---- the launcher (Alt+p: run in a new terminal, Alt+Shift+p: run in the background) --------------------------------
// A dmenu: type to narrow the programs on $PATH (names starting with the text first), Up/Down choose, Enter runs.
// Text with a space in it is run as typed (sh -c); Escape cancels.
// shortcut: $PATH is scanned each time it opens (a few ms); the filter is prefix then substring, not fuzzy.

static char **cmds;
static unsigned int ncmds, ncmdscap;
static unsigned int *match, nmatch;
static VuiInput *lq;
static VuiList *ll;
static VuiNode *lwin;
static int lbg;
static VuiOnEvent lq_orig_event;
static void (*lwin_orig_destroy)(VuiNode *);

static int cmp_cmd(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void scan_path(void)
{
    const char *path = getenv("PATH");
    char *copy, *dir, *save = NULL;

    for (unsigned int i = 0; i < ncmds; i++) free(cmds[i]);
    ncmds = 0;
    if (path == NULL) return;
    copy = strdup(path);

    for (dir = strtok_r(copy, VTMUX_PATH_SEP, &save); dir != NULL; dir = strtok_r(NULL, VTMUX_PATH_SEP, &save)) {
        DIR *d = opendir(dir);
        struct dirent *e;

        while (d != NULL && (e = readdir(d)) != NULL) {
            char full[1024];
            struct stat st;

            if (e->d_name[0] == '.') continue;
            snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
#ifdef _WIN32
            {
                char *dot = strrchr(e->d_name, '.');

                if (dot == NULL || (_stricmp(dot, ".exe") && _stricmp(dot, ".bat") && _stricmp(dot, ".cmd") && _stricmp(dot, ".com"))) continue;
                if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
                *dot = '\0'; // "notepad", not "notepad.exe"
            }
#else
            if (stat(full, &st) != 0 || !S_ISREG(st.st_mode) || access(full, X_OK) != 0) continue;
#endif
            if (ncmds == ncmdscap) {
                ncmdscap = ncmdscap != 0 ? ncmdscap * 2 : 1024;
                cmds = realloc(cmds, ncmdscap * sizeof(*cmds));
            }
            cmds[ncmds++] = strdup(e->d_name);
        }
        if (d != NULL) closedir(d);
    }
    free(copy);

    qsort(cmds, ncmds, sizeof(*cmds), cmp_cmd);
    {
        unsigned int w = 0;

        for (unsigned int i = 0; i < ncmds; i++) { // the same program in two directories: once
            if (w > 0 && strcmp(cmds[w - 1], cmds[i]) == 0) free(cmds[i]);
            else cmds[w++] = cmds[i];
        }
        ncmds = w;
    }
}

static void filter(VuiNode *input)
{
    char pat[200];
    size_t len;

    (void)input;
    input_get_text(lq, pat, sizeof(pat));
    len = strlen(pat);
    nmatch = 0;

    for (int pass = 0; pass < 2; pass++) { // names starting with the text, then names containing it
        for (unsigned int i = 0; i < ncmds; i++) {
            int starts = strncmp(cmds[i], pat, len) == 0;

            if (pass == 0 ? starts : (!starts && strstr(cmds[i], pat) != NULL)) match[nmatch++] = i;
        }
    }
    list_set_count(ll, nmatch);
    list_set_cursor(ll, 0);
}

static const char *launcher_item(VuiList *l, unsigned int i)
{
    (void)l;
    return cmds[match[i]];
}

static void close_launcher(void)
{
    if (lwin != NULL) vui_overlay_remove(vui_ctx(), lwin);
    lwin = NULL;
}

static void run_line(const char *line, int bg)
{
    static char text[256];
    static const char *argv[5];
    Arg a;

    if (line[0] == '\0') return;

#ifdef _WIN32
    if (bg) {
        vtmux_spawn_detached(line);
        return;
    }
#else
    if (bg) { // detached: its own session, no terminal, no zombie (double fork)
        pid_t pid = fork();

        if (pid == 0) {
            if (fork() == 0) {
                int null = open("/dev/null", O_RDWR);

                setsid();
                if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); }
                execl("/bin/sh", "sh", "-c", line, (char *)NULL);
                _exit(127);
            }
            _exit(0);
        }
        if (pid > 0) waitpid(pid, NULL, 0);
        return;
    }
#endif

    snprintf(text, sizeof(text), "%s", line);
    if (strchr(text, ' ') == NULL) {
        argv[0] = text;
        argv[1] = NULL;
    } else {
#ifdef _WIN32
        argv[0] = "cmd.exe";
        argv[1] = "/c";
#else
        argv[0] = "sh";
        argv[1] = "-c";
#endif
        argv[2] = text;
        argv[3] = NULL;
    }
    a.v = argv;
    spawn(&a);
}

static void launcher_submit(VuiNode *input)
{
    char typed[200], line[200];

    (void)input;
    input_get_text(lq, typed, sizeof(typed));
    snprintf(line, sizeof(line), "%s", strchr(typed, ' ') == NULL && nmatch > 0 ? cmds[match[ll->cursor]] : typed);
    close_launcher();
    run_line(line, lbg);
}

static void launcher_pick(VuiList *l, unsigned int i)
{
    char line[200];

    (void)l;
    snprintf(line, sizeof(line), "%s", cmds[match[i]]);
    close_launcher();
    run_line(line, lbg);
}

// The overlay can also go away by itself (a click outside it): forget it, or the launcher would never open again
static void launcher_destroyed(VuiNode *node)
{
    if (lwin == node) lwin = NULL;
    if (lwin_orig_destroy != NULL) lwin_orig_destroy(node);
}

// Up/Down/PageUp/PageDown move the list while the text field has the focus; Escape closes
static int launcher_input_event(VuiNode *node, VuiCtx *ui, const VuiEvent *e)
{
    if (e->type == VUI_EVENT_KEY && e->key.key == VUI_KEY_ESCAPE && e->key.modifiers == 0) {
        close_launcher();
        return 1;
    }

    if (e->type == VUI_EVENT_KEY && e->key.modifiers == 0 && (e->key.key == VUI_KEY_UP || e->key.key == VUI_KEY_DOWN
        || e->key.key == VUI_KEY_PAGE_UP || e->key.key == VUI_KEY_PAGE_DOWN)) {
        return ll->node.on_event(&ll->node, ui, e);
    }
    return lq_orig_event(node, ui, e);
}

static void launch(const Arg *a)
{
    VuiCtx *ui = vui_ctx();
    VuiBorder *b;
    unsigned int w = vui_width(ui) > 64 ? 60 : vui_width(ui) - 4, h = vui_height(ui) > 16 ? 14 : vui_height(ui) - 2;

    if (lwin != NULL || vui_width(ui) < 20 || vui_height(ui) < 6) return;
    lbg = a->i != 0;
    scan_path();
    match = realloc(match, (ncmds + 1) * sizeof(*match));

    b = border_create(lbg ? "run in background" : "run");
    b->node.direction = VUI_DIR_COLUMN;
    lq = input_create(190);
    lq->node.basis = 1;
    lq->on_change = filter;
    lq->on_submit = launcher_submit;
    lq_orig_event = lq->node.on_event;
    lq->node.on_event = launcher_input_event;
    ll = list_create(0, launcher_item);
    ll->node.basis = 0; // fill what is left: the list's own size is its row count, which overran the border
    ll->node.grow = 1;
    ll->on_select = launcher_pick;
    node_add(&b->node, &lq->node);
    node_add(&b->node, &ll->node);

    lwin = &b->node;
    lwin_orig_destroy = b->node.destroy;
    b->node.destroy = launcher_destroyed;
    if (vui_overlay_add(ui, lwin, (VuiRect){ (vui_width(ui) - w) / 2, 1, w, h }, VUI_OVERLAY_MODAL | VUI_OVERLAY_DISMISS) != 0) {
        node_destroy(lwin);
        lwin = NULL;
        return;
    }
    filter(NULL);
    vui_set_focus(ui, &lq->node);
}

int main(void)
{
    VuiCtx *ui = vui_init();
    Arg first = { .v = termcmd };

    if (ui == NULL) return 1;
    vui_set_mouse(ui, 1); // clicks, drags and wheel: selection, scrolling, programs that use the mouse
    vui_set_key_release(1); // Alt released = the end of a combo (only the Linux and Windows consoles report it)
    root = &div_create(VUI_DIR_COLUMN)->node;

    screen = &div_create(VUI_DIR_ROW)->node;
    screen->grow = 1;
    bar = node_create();
    bar->basis = 1;
    bar->draw = bar_draw;
    bar->on_event = bar_event;
    node_add(root, bar);
    node_add(root, screen);
    load_config(0);
    spawn(&first);

    for (;;) {
        VuiEvent e = vui_next_event(ui, root);

        if (e.type == VUI_EVENT_CLOSE) break;
        reap();
        refresh_names();
        if (e.type == VUI_EVENT_KEY && (e.key.key == VUI_KEY_NONE || e.key.release)) { // bare modifiers and releases: for the combo only
            exact_keys = 1;
            if (e.key.release) combo_armed = 0; // dwm's keyrelease: any key going up ends the combo
            continue;
        }
        if (e.type == VUI_EVENT_KEY) msg[0] = '\0';
        if (e.type == VUI_EVENT_KEY && command(&e.key)) continue;
        vui_dispatch(ui, root, &e);
        if (e.type == VUI_EVENT_MOUSE && e.mouse.action == VUI_MOUSE_PRESS) follow_focus();
    }

    vui_shutdown(ui);
    return 0;
}
#endif
