/* vtmux configuration, dwm style: edit config.h (copied from this file by `make`) and rebuild.
 * Keys are matched exactly: Alt+Shift+q is KEY_CHAR 'q' with MOD|SHIFT (letters are always lowercase + Shift). */

#include KEYMAP_H /* keymaps/<KEYMAP>.h: the tag keys for your keyboard layout */

static const int topbar        = 1;                       /* 0: bar at the bottom */
static const char *tags[]      = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
static const float mfact       = 0.55f;                   /* master area size, 0.1 .. 0.9 */
static const int nmaster       = 1;                       /* terminals in the master area */
static const VuiColor focuscolor = VUI_COLOR_BRIGHT_GREEN; /* frame of the focused terminal */

/* what a new terminal runs: NULL = $SHELL */
static const char *const termcmd[] = { NULL };

#define MODKEY VUI_MOD_ALT
#define SHIFT  VUI_MOD_SHIFT
#define CH(c)  VUI_KEY_CHAR, (c)
#define FN(n)  (VUI_KEY_F1 + (n) - 1), 0

/* view tag N; tag the focused terminal with N. Four rows: the typed character with and without the Shift flag */
#define TAGKEYS(U, S, N) \
	{ MODKEY,         CH(U), view, { .ui = 1u << (N) } }, \
	{ MODKEY | SHIFT, CH(U), tag,  { .ui = 1u << (N) } }, \
	{ MODKEY,         CH(S), tag,  { .ui = 1u << (N) } }, \
	{ MODKEY | SHIFT, CH(S), tag,  { .ui = 1u << (N) } },

/* Function keys work on any layout */
#define FNKEYS(N) \
	{ MODKEY,         FN(N), view, { .ui = 1u << ((N) - 1) } }, \
	{ MODKEY | SHIFT, FN(N), tag,  { .ui = 1u << ((N) - 1) } },

static const Key keys[] = {
	/* modifier           key                  function    argument */
	{ MODKEY,           CH('c'),             spawn,      { .v = termcmd } },
	{ MODKEY | SHIFT,   CH('c'),             killclient, { 0 } },
	{ MODKEY,           CH('j'),             focusstack, { .i = +1 } },
	{ MODKEY,           CH('k'),             focusstack, { .i = -1 } },
	{ MODKEY,           CH('i'),             incnmaster, { .i = +1 } },
	{ MODKEY,           CH('d'),             incnmaster, { .i = -1 } },
	{ MODKEY,           CH('h'),             setmfact,   { .f = -0.05f } },
	{ MODKEY,           CH('l'),             setmfact,   { .f = +0.05f } },
	{ MODKEY,           VUI_KEY_ENTER, 0,    zoom,       { 0 } },
	{ MODKEY,           VUI_KEY_TAB, 0,      view,       { 0 } }, /* the previous tag */
	{ MODKEY,           CH(KEYMAP_ALLTAGS_KEY),     view, { .ui = ~0u } },
	{ MODKEY | SHIFT,   CH(KEYMAP_ALLTAGS_KEY),     tag,  { .ui = ~0u } },
	{ MODKEY,           CH(KEYMAP_ALLTAGS_SHIFTED), tag,  { .ui = ~0u } },
	{ MODKEY | SHIFT,   CH(KEYMAP_ALLTAGS_SHIFTED), tag,  { .ui = ~0u } },
	KEYMAP_TAGS
	FNKEYS(1) FNKEYS(2) FNKEYS(3) FNKEYS(4) FNKEYS(5) FNKEYS(6) FNKEYS(7) FNKEYS(8) FNKEYS(9)
	{ MODKEY | SHIFT,   CH('q'),             quit,       { 0 } },
};
