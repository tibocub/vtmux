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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static void reload(const Arg *a);

#include "config.h"

#define NTAGS ((int)(sizeof(tags) / sizeof(tags[0])))
#define MAXC  16
_Static_assert(NTAGS <= 9, "vtmux supports at most 9 tags");

typedef struct {
    VuiTerminal *term;
    VuiBorder *frame;
    char title[8];
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

// Rebuild the visible tree from the clients: master column | stack column. Hidden terminals keep running.
static void retile(void)
{
    VuiTheme *th = vui_theme();
    int n = count_visible(), m = PT.nmaster < n ? PT.nmaster : n, mono = PT.arrange == ARRANGE_MONOCLE;
    VuiDiv *col[2] = { NULL, NULL };

    for (int i = 0; i < ncl; i++) {
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
    b->node.direction = VUI_DIR_COLUMN;
    node_add(&b->node, &t->node);

    cl[ncl] = (Client){ .term = t, .frame = b, .tags = curview };
    snprintf(cl[ncl].title, sizeof(cl[ncl].title), "%u", ++serial);
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

static void view(const Arg *a)
{
    unsigned int v = a->ui & ((1u << NTAGS) - 1); // 0: back to the previous tag

    if (v == 0) v = prevview;
    if (v == curview) return;
    prevview = curview;
    curview = v;
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
    for (int i = 0; i < nbinds; i++) {
        const Key *b = &binds[i].k;

        if (b->key == k->key && b->mods == k->modifiers && (k->key != VUI_KEY_CHAR || b->ch == (b->physical ? k->base : k->character))) {
            b->func(&b->arg);
            return 1;
        }
    }
    return 0;
}

// ---- ~/.config/vtmux.conf ----------------------------------------------------------------------------------------
//   # comment
//   set mfact 0.6            set nmaster 2        set topbar 0        set layout fr        set term /bin/zsh
//   bind Alt+Shift+c kill    bind Alt+x spawn htop    bind Alt+9 view 9    unbind Alt+Enter
// Chords are Mod+key, Mod = Alt, Shift, Ctrl in any order, key = a character, Enter Tab Escape Space Backspace Up Down
// Left Right Home End PageUp PageDown Insert Delete F1..F12. A letter is the key that types it on your layout; a digit
// is the key in that position of the number row (so Alt+1 is the same key on AZERTY). Actions: spawn [cmd...], kill,
// focus +1|-1, nmaster +1|-1, mfact +0.05|-0.05, zoom, view N|prev|all, tag N|all, reload, quit.
// shortcut: spawn's command is split on spaces (no quoting); at most 8 words.

static const struct { const char *name; void (*func)(const Arg *); } actions[] = {
    { "spawn", spawn }, { "kill", killclient }, { "focus", focusstack }, { "nmaster", incnmaster },
    { "mfact", setmfact }, { "zoom", zoom }, { "view", view }, { "tag", tag }, { "reload", reload }, { "quit", quit }, { "arrange", arrange },
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
        } else if (b.k.func == focusstack || b.k.func == incnmaster) {
            if (n < 4) return "needs +1 or -1";
            b.k.arg.i = atoi(word[3]);
        } else if (b.k.func == setmfact) {
            if (n < 4) return "needs +0.05 or -0.05";
            b.k.arg.f = (float)atof(word[3]);
        } else if (b.k.func == arrange) {
            if (n < 4) return "needs tile, monocle or toggle";
            b.k.arg.i = same_word(word[3], "tile") ? ARRANGE_TILE : same_word(word[3], "monocle") ? ARRANGE_MONOCLE
                      : same_word(word[3], "toggle") ? -1 : -2;
            if (b.k.arg.i == -2) return "needs tile, monocle or toggle";
        } else if (b.k.func == view || b.k.func == tag) {
            if (n < 4 || tag_arg(word[3], &mask) != 0) return "needs a tag 1-9, all or prev";
            if (b.k.func == tag && mask == 0) return "tag needs a tag 1-9 or all";
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
    const char *conf = getenv("VTMUX_CONF"), *h = getenv("HOME");
    char path[512], line[512], first[96] = "";
    FILE *f;
    int lineno = 0, bad = 0;

    nbinds = 0;
    for (unsigned int i = 0; i < sizeof(keys) / sizeof(keys[0]) && nbinds < MAXBIND; i++) binds[nbinds++].k = keys[i];
    for (int i = 0; i <= NTAGS; i++) pertag[i] = (Pertag){ ARRANGE_TILE, mfact, nmaster };
    top = topbar;
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

static void bar_draw(VuiNode *node, VuiScreen *scr, void *userdata)
{
    VuiTheme *th = vui_theme();
    unsigned int occupied = 0, x = node->rect.x;
    char s[16];

    (void)userdata;
    scr_fill(scr, node->rect, ' ', th->bar);
    for (int i = 0; i < ncl; i++) occupied |= cl[i].tags;

    for (int i = 0; i < NTAGS; i++) {
        VuiStyle st = (curview & (1u << i)) ? th->focus : th->bar;

        snprintf(s, sizeof(s), " %s%c", tags[i], (occupied & (1u << i)) ? '.' : ' ');
        scr_puts(scr, x, node->rect.y, s, st);
        x += vui_strn_width(s, strlen(s));
    }

    if (PT.arrange == ARRANGE_MONOCLE) snprintf(s, sizeof(s), " [M%d]", count_visible());
    else snprintf(s, sizeof(s), " [%d]", PT.nmaster);
    scr_puts(scr, x + 1, node->rect.y, s, th->bar);
    if (msg[0] != '\0') scr_puts(scr, x + 1 + vui_strn_width(s, strlen(s)) + 1, node->rect.y, msg, th->bar);
}

int main(void)
{
    VuiCtx *ui = vui_init();
    Arg first = { .v = termcmd };

    if (ui == NULL) return 1;
    root = &div_create(VUI_DIR_COLUMN)->node;

    screen = &div_create(VUI_DIR_ROW)->node;
    screen->grow = 1;
    bar = node_create();
    bar->basis = 1;
    bar->draw = bar_draw;
    node_add(root, bar);
    node_add(root, screen);
    load_config(0);
    spawn(&first);

    for (;;) {
        VuiEvent e = vui_next_event(ui, root);

        if (e.type == VUI_EVENT_CLOSE) break;
        reap();
        if (e.type == VUI_EVENT_KEY) msg[0] = '\0';
        if (e.type == VUI_EVENT_KEY && command(&e.key)) continue;
        vui_dispatch(ui, root, &e);
    }

    vui_shutdown(ui);
    return 0;
}
#endif
