#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

/* some special characters:
 *  ▁ ▂ ▃ ▄ ▅ ▆ ▇ █ ▊ ▌ ▎ ▖ ▗ ▘ ▙ ▚ ▛ ▜ ▝ ▞ ▟ ━ ┃
 *
 *  ┏ ┓ ┗ ┛ ┣ ┫ ┳ ┻ ╋ ╸ ╹ ╺
 *
 *  ╻ ╏ ─ ─ │ │ ╴ ╴ ╵ ╵ ╶ ╶ ╵ ╵ ⎢ ⎥ ⎺ ⎻ ⎼ ⎽ ◾ ▪ ╱ ╲ ╳ 
 *
 *  ■ □ ★ ☆ ◆ ◇ ◣ ◢  ◤ ◥ ▲ △ ▼ ▽ ⊿ 
 *
 *  ┌─┬─┐
 *  │ │ │
 *  ├─┼─┤
 *  │ │ │
 *  └─┴─┘
 *
 * */

/* output colored text
 *   for example:
 *     printf("\033[{style};{color}mHello World!\n\033[0m");
 *
 *     where {style} is interger range from 0 to 4, {color} is a
 *     interger range from 30 to 38
 *
 *
 * 256 color support:
 *   control code: "\033[48;5;{color}m" or "\033[38;5;{color}m"
 *   where {color} is a interger range from 0 to 255, see more
 *   details in 256-colors.py
 *
 *
 * cursor move:
 *   control code: "\033"  (\033 is ascii code of <Esc>)
 *
 *   cursor up:             "\033[{COUNT}A"
 *		Moves the cursor up by COUNT rows;
 *		the default count is 1.
 *
 *	 cursor down:           "\033[{COUNT}B"
 *		Moves the cursor down by COUNT rows;
 *		the default count is 1.
 *
 *	 cursor forward:        "\033[{COUNT}C"
 *		Moves the cursor forward by COUNT columns;
 *		the default count is 1.
 *
 *	 cursor backward:       "\033[{COUNT}D"
 *		Moves the cursor backward by COUNT columns;
 *		the default count is 1.
 *
 *	 set cursor position:   "\033[{ROW};{COLUMN}f"
 */

/* output style: 16 color */
#define VT100_STYLE_NORMAL    "0"
#define VT100_STYLE_BOLD      "1"
#define VT100_STYLE_DARK      "2"
#define VT100_STYLE_BACK      "3"
#define VT100_STYLE_UNDERLINE "4"

/* output color: 16 color */
#define VT100_COLOR_BLACK    "30"
#define VT100_COLOR_RED      "31"
#define VT100_COLOR_GREEN    "32"
#define VT100_COLOR_YELLOW   "33"
#define VT100_COLOR_BLUE     "34"
#define VT100_COLOR_PURPLE   "35"
#define VT100_COLOR_SKYBLUE  "36"
#define VT100_COLOR_WHITE    "37"
#define VT100_COLOR_NORMAL   "38"


/* detect the width and height of local screen firstly by `ioctl`
 *
 * usage:
 *	 struct winsize ws;
 *	 ioctl(STDIN_FILENO, TIOCGWINSZ, &size);
 *	 ws.ws_col;
 *	 ws.ws_row;
 */

#define SCR_W 60
#define SCR_H 40

typedef struct pos_t {
	uint8_t x, y;
} pos_t;

typedef struct send_message_t {
	pos_t pos[100];
} send_message_t;


#endif
