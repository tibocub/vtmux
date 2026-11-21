// vtmux: vapor terminal multiplexer, dwm-style (tags, master/stack tiling) built on the terminal widget.
//
// Keys: Alt is dwm's Mod. Alt+Shift+<letter> is typed as Alt+<capital>. Everything else goes to the focused terminal.
//   Alt+c        new terminal on the current tag      Alt+C   close the focused terminal
//   Alt+j / k    focus next / previous                Alt+Enter  zoom: swap with the master
//   Alt+1..9     view tag n (Alt+0: all tags)         Alt+F1..F9  same, for any keyboard layout
//   Alt+!..(     move the focused terminal to tag n   Alt+Shift+F1..F9  same (QWERTY: Alt+Shift+1..9)
//   Alt+Tab      view the previous tag
//   Alt+h / l    shrink / grow the master area        Alt+i / d   more / fewer terminals in the master area
//   Alt+Q        quit
//
// Verify:
//   - the bar shows the tags 1..9: the viewed one highlighted, tags holding terminals marked with '.'
//   - Alt+c opens shells that tile (master left, stack right); Alt+h/l, Alt+i/d reshape them live
//   - the focused terminal has the bold frame and the cursor; Alt+j/k move it
//   - Alt+Shift+2 sends the focused terminal to tag 2 (it disappears from view), Alt+2 shows it, still running
//   - `exit` in a shell removes its terminal; Alt+C kills one
//   - resizing the window re-tiles; Alt+Q quits

#include <stdio.h>

#include <vaporui.h>

#ifndef VUI_TERMINAL
int main(void) { puts("mux vtmux needs the vaporUI terminal widget (POSIX build)"); return 1; }
#else

#define NTAGS 9
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
static unsigned int view = 1, prev_view = 1;
static float mfact = 0.55f;
static int nmaster = 1;
static unsigned int serial;
static VuiNode *screen, *bar;

static int visible(int i) { return cl[i].tags & view; }

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
    int n = count_visible(), m = nmaster < n ? nmaster : n;
    VuiDiv *col[2] = { NULL, NULL };

    for (int i = 0; i < ncl; i++) {
        if (cl[i].frame->node.parent != NULL) node_remove(&cl[i].frame->node);
    }
    while (screen->child_count > 0) {  // the old, now empty columns
        VuiNode *old = screen->children[screen->child_count - 1];

        node_remove(old);
        node_destroy(old);
    }

    if (sel >= 0 && !visible(sel)) sel = nth(0);
    if (sel < 0) sel = nth(0);

    for (int k = 0, seen = 0; k < ncl; k++) {
        int c;

        if (!visible(k)) continue;

        c = seen++ >= m;
        if (col[c] == NULL) {
            col[c] = div_create(VUI_DIR_COLUMN);
            col[c]->node.basis = 0;
            col[c]->node.grow = c ? (n > m ? 1.0f - mfact : 0) : (n > m ? mfact : 1.0f);
            node_add(screen, &col[c]->node);
        }

        cl[k].frame->node.basis = 0;
        cl[k].frame->node.grow = 1;
        cl[k].frame->style = th->border;
        if (k == sel) cl[k].frame->style.attr |= VUI_ATTR_BOLD, cl[k].frame->style.fg = VUI_COLOR_BRIGHT_GREEN;
        node_add(&col[c]->node, &cl[k].frame->node);
    }

    if (sel >= 0) vui_set_focus(vui_ctx(), &cl[sel].term->node);
}

static void set_view(unsigned int v)
{
    if (v == view) return;
    prev_view = view;
    view = v;
    retile();
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

static void spawn(void)
{
    VuiTerminal *t;
    VuiBorder *b;

    if (ncl == MAXC) return;

    t = terminal_create(NULL);
    if (t == NULL) return;

    b = border_create(NULL);
    t->on_exit = on_exit_cb;
    b->node.direction = VUI_DIR_COLUMN;
    node_add(&b->node, &t->node);

    cl[ncl] = (Client){ .term = t, .frame = b, .tags = view ? view : 1 };
    snprintf(cl[ncl].title, sizeof(cl[ncl].title), "%u", ++serial);
    b->title = cl[ncl].title;
    sel = ncl++;
    retile();
}

static void focus_step(int dir)
{
    int n = count_visible(), at = 0;

    if (n == 0) return;
    for (int k = 0; k < n; k++) {
        if (nth(k) == sel) at = k;
    }
    sel = nth((at + dir + n) % n);
    retile();
}

static void zoom(void)
{
    int m = nth(0);
    Client c;

    if (sel < 0 || m < 0 || sel == m) return;

    // move the focused client to the front of the tile order, like dwm's zoom (the old master goes second)
    c = cl[sel];
    for (int j = sel; j > 0; j--) cl[j] = cl[j - 1];
    cl[0] = c;
    sel = 0;
    retile();
}

static void tag_client(unsigned int mask)
{
    if (sel < 0 || mask == 0) return;
    cl[sel].tags = mask;
    retile();
}

static void adjust_mfact(float d)
{
    mfact += d;
    if (mfact < 0.1f) mfact = 0.1f;
    if (mfact > 0.9f) mfact = 0.9f;
    retile();
}

static void adjust_nmaster(int d)
{
    nmaster += d;
    if (nmaster < 1) nmaster = 1;
    retile();
}

// Returns 1 if the key was a mux command
static int command(const VuiKeyEvent *k)
{
    static const char shifted[] = "!@#$%^&*("; // Alt+Shift+1..9 on a QWERTY layout
    VuiCodepoint ch = k->character;
    int shift = (k->modifiers & VUI_MOD_SHIFT) != 0;

    if (!(k->modifiers & VUI_MOD_ALT)) return 0;

    if (k->key == VUI_KEY_ENTER) { zoom(); return 1; }
    if (k->key == VUI_KEY_TAB) { set_view(prev_view); return 1; }
    if (k->key >= VUI_KEY_F1 && k->key < VUI_KEY_F1 + NTAGS) {
        unsigned int bit = 1u << (k->key - VUI_KEY_F1);

        if (shift) tag_client(bit); else set_view(bit);
        return 1;
    }
    if (k->key != VUI_KEY_CHAR) return 0;

    if (ch >= '1' && ch <= '9') { set_view(1u << (ch - '1')); return 1; }
    if (ch == '0') { set_view((1u << NTAGS) - 1); return 1; }
    for (int i = 0; i < NTAGS; i++) {
        if (ch == (VuiCodepoint)shifted[i]) { tag_client(1u << i); return 1; }
    }

    switch (ch) {
    case 'c': spawn(); return 1;
    case 'C': if (sel >= 0) { cl[sel].dead = 1; reap(); } return 1;
    case 'j': focus_step(1); return 1;
    case 'k': focus_step(-1); return 1;
    case 'h': adjust_mfact(-0.05f); return 1;
    case 'l': adjust_mfact(0.05f); return 1;
    case 'i': adjust_nmaster(1); return 1;
    case 'd': adjust_nmaster(-1); return 1;
    case 'Q': vui_quit(vui_ctx()); return 1;
    default:  return 0;
    }
}

static void bar_draw(VuiNode *node, VuiScreen *scr, void *userdata)
{
    VuiTheme *th = vui_theme();
    unsigned int occupied = 0, x = node->rect.x;
    char s[8];

    (void)userdata;
    scr_fill(scr, node->rect, ' ', th->bar);
    for (int i = 0; i < ncl; i++) occupied |= cl[i].tags;

    for (int i = 0; i < NTAGS; i++) {
        VuiStyle st = (view & (1u << i)) ? th->focus : th->bar;

        snprintf(s, sizeof(s), " %d%c", i + 1, (occupied & (1u << i)) ? '.' : ' ');
        scr_puts(scr, x, node->rect.y, s, st);
        x += 3;
    }

    snprintf(s, sizeof(s), " [%d]", nmaster);
    scr_puts(scr, x + 1, node->rect.y, s, th->bar);
}

int main(void)
{
    VuiCtx *ui = vui_init();
    VuiNode *root = &div_create(VUI_DIR_COLUMN)->node;

    if (ui == NULL) return 1;

    screen = &div_create(VUI_DIR_ROW)->node;
    screen->grow = 1;
    bar = node_create();
    bar->basis = 1;
    bar->draw = bar_draw;
    node_add(root, screen);
    node_add(root, bar);

    spawn();

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
