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
 *
 *   where {color} is a interger range from 0 to 255, see more
 *   details in 256-colors.py
 *
 *
 * cursor move:
 *   control code: "\033"  (\033 is ascii code of <esc>)
 *
 *   cursor up:             "\033[{count}a"
 *		moves the cursor up by count rows;
 *		the default count is 1.
 *
 *	 cursor down:           "\033[{count}b"
 *		moves the cursor down by count rows;
 *		the default count is 1.
 *
 *	 cursor forward:        "\033[{count}c"
 *		moves the cursor forward by count columns;
 *		the default count is 1.
 *
 *	 cursor backward:       "\033[{count}d"
 *		moves the cursor backward by count columns;
 *		the default count is 1.
 *
 *	 set cursor position:   "\033[{row};{column}f"
 */

/* output style: 16 color mode */
#define VT100_STYLE_NORMAL     "0"
#define VT100_STYLE_BOLD       "1"
#define VT100_STYLE_DARK       "2"
#define VT100_STYLE_BACKGROUND "3"
#define VT100_STYLE_UNDERLINE  "4"

/* output color: 16 color mode */
#define VT100_COLOR_BLACK    "30"
#define VT100_COLOR_RED      "31"
#define VT100_COLOR_GREEN    "32"
#define VT100_COLOR_YELLOW   "33"
#define VT100_COLOR_BLUE     "34"
#define VT100_COLOR_PURPLE   "35"
#define VT100_COLOR_SKYBLUE  "36"
#define VT100_COLOR_WHITE    "37"
#define VT100_COLOR_NORMAL   "38"


/* detects the width and height of local screen firstly by `ioctl`
 *
 * usage of ioctl:
 *	 struct winsize ws;
 *	 ioctl(STDIN_FILENO, TIOCGWINSZ, &size);
 *	 ws.ws_col;
 *	 ws.ws_row;
 */

#define SCR_W 60
#define SCR_H 40

#define USERID_SZ  7
#define USER_CNT   5

#define MAX_ITEM ((USERID_SZ * USER_CNT) / sizeof(pos_t) - 1)

#define CLIENT_COMMAND_USER_LOGIN        0
#define CLIENT_COMMAND_FETCH_ALL_USERS   1
#define CLIENT_COMMAND_LAUNCH_CHALLENGE  2

#define SERVER_RESPONSE_NOT_LOGIN               0
#define SERVER_RESPONSE_LOGIN_FAIL              1 // user id has been registered by other users
#define SERVER_RESPONSE_ALL_USERS_ID            2
#define SERVER_RESPONSE_FRIEND_ACCEPT_CHALLENGE 3
#define SERVER_RESPONSE_FRIEND_REJECT_CHALLENGE 4
#define SERVER_MESSAGE_BATTLE_INFORMATION       5
#define SERVER_MESSAGE_YOU_ARE_DEAD             6

typedef struct pos_t {
	uint8_t x, y;
} pos_t;

typedef struct client_message_t {
	uint8_t command;
	char userid[USERID_SZ]; // last byte must be zero
} client_message_t; // messages format sended from client to server

typedef struct server_message_t {
	uint8_t response;
	union {
		// support at most five users
		char all_users[USER_CNT][USERID_SZ];

		struct {
			pos_t pos[MAX_ITEM];
			int life;
		};
	};
} server_message_t;

#endif
