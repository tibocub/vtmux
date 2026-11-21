/* QWERTY (default). TAGKEYS(unshifted, shifted, tag): what the key types without and with Shift.
 * A terminal sends the *typed* character, so a layout must list both. Contributions for other layouts
 * (de, be, es, ...) are welcome: copy this file to keymaps/<code>.h and build with `make KEYMAP=<code>`. */
#define KEYMAP_TAGS \
	TAGKEYS('1', '!', 0) \
	TAGKEYS('2', '@', 1) \
	TAGKEYS('3', '#', 2) \
	TAGKEYS('4', '$', 3) \
	TAGKEYS('5', '%', 4) \
	TAGKEYS('6', '^', 5) \
	TAGKEYS('7', '&', 6) \
	TAGKEYS('8', '*', 7) \
	TAGKEYS('9', '(', 8)

/* the key that views / tags every tag: 0 on QWERTY */
#define KEYMAP_ALLTAGS_KEY     '0'
#define KEYMAP_ALLTAGS_SHIFTED ')'
