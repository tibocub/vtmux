/* vtmux configuration, dwm style: edit config.h (copied from this file by `make`) and rebuild.
 * Keys are matched exactly: Alt+Shift+q is MOD|SHIFT and the key 'q' (letters are always lowercase + Shift).
 * CH(c) binds the key that *types* c on your layout (dwm's XK_q). PK(c) binds the key in the position of c on a
 * US keyboard (the number row: the key typing & on AZERTY is PK('1')), so it works on every layout. */

/* Terminals other than kitty, foot, WezTerm, Ghostty, Alacritty can't say which physical key was pressed:
 * name your layout ("fr", ...) or set $VUI_LAYOUT. NULL: $VUI_LAYOUT, then "us". */
static const char *layout      = NULL;

static const int topbar        = 1;                       /* 0: bar at the bottom */
static const char *tags[]      = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
static const float mfact       = 0.55f;                   /* master area size, 0.1 .. 0.9 */
static const int nmaster       = 1;                       /* terminals in the master area */
static const VuiColor focuscolor = VUI_COLOR_BRIGHT_GREEN; /* frame of the focused terminal */

/* what a new terminal runs: NULL = $SHELL */
static const char *const termcmd[] = { NULL };

#define MODKEY VUI_MOD_ALT
#define SHIFT  VUI_MOD_SHIFT
#define CH(c)  VUI_KEY_CHAR, (c), 0
#define PK(c)  VUI_KEY_CHAR, (c), 1
#define FN(n)  (VUI_KEY_F1 + (n) - 1), 0, 0

/* view tag N; tag the focused terminal with N */
#define TAGKEYS(K, N) \
	{ MODKEY,         PK(K), view, { .ui = 1u << (N) } }, \
	{ MODKEY | SHIFT, PK(K), tag,  { .ui = 1u << (N) } },

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
	{ MODKEY,           VUI_KEY_ENTER, 0, 0, zoom,       { 0 } },
	{ MODKEY,           VUI_KEY_TAB, 0, 0,   view,       { 0 } }, /* the previous tag */
	{ MODKEY,           PK('0'),             view,       { .ui = ~0u } }, /* every tag */
	{ MODKEY | SHIFT,   PK('0'),             tag,        { .ui = ~0u } },
	TAGKEYS('1', 0) TAGKEYS('2', 1) TAGKEYS('3', 2) TAGKEYS('4', 3) TAGKEYS('5', 4)
	TAGKEYS('6', 5) TAGKEYS('7', 6) TAGKEYS('8', 7) TAGKEYS('9', 8)
	FNKEYS(1) FNKEYS(2) FNKEYS(3) FNKEYS(4) FNKEYS(5) FNKEYS(6) FNKEYS(7) FNKEYS(8) FNKEYS(9)
	{ MODKEY | SHIFT,   CH('q'),             quit,       { 0 } },
};
