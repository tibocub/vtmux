/* French AZERTY: the number row types & é " ' ( - è _ ç, and the digits with Shift. */
#define KEYMAP_TAGS \
	TAGKEYS('&',    '1', 0) \
	TAGKEYS(0xE9,   '2', 1) /* é */ \
	TAGKEYS('"',    '3', 2) \
	TAGKEYS('\'',   '4', 3) \
	TAGKEYS('(',    '5', 4) \
	TAGKEYS('-',    '6', 5) \
	TAGKEYS(0xE8,   '7', 6) /* è */ \
	TAGKEYS('_',    '8', 7) \
	TAGKEYS(0xE7,   '9', 8) /* ç */

/* à / 0, as in dwm */
#define KEYMAP_ALLTAGS_KEY     0xE0
#define KEYMAP_ALLTAGS_SHIFTED '0'
