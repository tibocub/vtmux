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

#include <stdio.h>
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

#include "config.h"

#define NTAGS ((int)(sizeof(tags) / sizeof(tags[0])))
#define MAXC  16

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
static float curmfact = mfact;
static int curnmaster = nmaster;
static unsigned int serial;
static VuiNode *screen, *bar;

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
    int n = count_visible(), m = curnmaster < n ? curnmaster : n;
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

        if (!visible(k)) continue;

        c = seen++ >= m;
        if (col[c] == NULL) {
            col[c] = div_create(VUI_DIR_COLUMN);
            col[c]->node.basis = 0;
            col[c]->node.grow = c ? 1.0f - curmfact : (n > m ? curmfact : 1.0f);
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
    const char *const *argv = a->v;

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
    curmfact += a->f;
    if (curmfact < 0.1f) curmfact = 0.1f;
    if (curmfact > 0.9f) curmfact = 0.9f;
    retile();
}

static void incnmaster(const Arg *a)
{
    curnmaster += a->i;
    if (curnmaster < 1) curnmaster = 1;
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
    for (unsigned int i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (keys[i].key == k->key && keys[i].mods == k->modifiers && (k->key != VUI_KEY_CHAR || keys[i].ch == (keys[i].physical ? k->base : k->character))) {
            keys[i].func(&keys[i].arg);
            return 1;
        }
    }
    return 0;
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

    snprintf(s, sizeof(s), " [%d]", curnmaster);
    scr_puts(scr, x + 1, node->rect.y, s, th->bar);
}

int main(void)
{
    VuiCtx *ui = vui_init();
    VuiNode *root = &div_create(VUI_DIR_COLUMN)->node;
    Arg first = { .v = termcmd };

    if (ui == NULL) return 1;
    vui_set_layout(layout);

    screen = &div_create(VUI_DIR_ROW)->node;
    screen->grow = 1;
    bar = node_create();
    bar->basis = 1;
    bar->draw = bar_draw;
    node_add(root, topbar ? bar : screen);
    node_add(root, topbar ? screen : bar);

    spawn(&first);

    for (;;) {
        VuiEvent e = vui_next_event(ui, root);

        if (e.type == VUI_EVENT_CLOSE) break;
        reap();
        if (e.type == VUI_EVENT_KEY && command(&e.key)) continue;
        vui_dispatch(ui, root, &e);
    }

    vui_shutdown(ui);
    return 0;
}
#endif
