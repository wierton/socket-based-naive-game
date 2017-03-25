#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

#ifndef false
#define false 0
#endif

#ifndef true
#define true 1
#endif

/* some special characters(terminal graph):
 *
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


#define log(fmt, ...) \
	fprintf(stderr, "\033[" VT100_STYLE_NORMAL ";" VT100_COLOR_BLUE "m[LOG] \033[0m" "%s:%d: " fmt, __func__, __LINE__, ## __VA_ARGS__)

#define loge(fmt, ...) \
	fprintf(stderr, "\033[" VT100_STYLE_NORMAL ";" VT100_COLOR_RED "m[ERROR] \033[0m" "%s:%d: " fmt, __func__, __LINE__, ## __VA_ARGS__)

#define eprintf(fmt, ...) do { \
	loge("\033[" VT100_STYLE_NORMAL ";" VT100_COLOR_RED "m[ERROR] \033[0m" "%s:%d: " fmt, __func__, __LINE__, ## __VA_ARGS__); \
	exit(0); \
} while(0)

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

#define MAX_ITEM 30

enum {
	CLIENT_COMMAND_USER_LOGIN,
	CLIENT_COMMAND_FETCH_ALL_USERS,
	CLIENT_COMMAND_LAUNCH_BATTLE,
	CLIENT_COMMAND_QUIT_BATTLE,
	CLIENT_COMMAND_ACCEPT_BATTLE,
	CLIENT_COMMAND_REJECT_BATTLE,
	CLIENT_COMMAND_INVITE_USER,
	CLIENT_COMMAND_LOGOUT,
	CLIENT_COMMAND_MOVE_UP,
	CLIENT_COMMAND_MOVE_DOWN,
	CLIENT_COMMAND_MOVE_LEFT,
	CLIENT_COMMAND_MOVE_RIGHT,
	CLIENT_COMMAND_FIRE,
	CLIENT_COMMAND_END,
};

enum {
	SERVER_RESPONSE_LOGIN_SUCCESS,
	SERVER_RESPONSE_NOT_LOGIN,
	SERVER_RESPONSE_LOGIN_FAIL_DUP_USERID,    // user id has been registered by other users
	SERVER_RESPONSE_LOGIN_FAIL_SERVER_LIMITS, // server unable to handle more users
	SERVER_RESPONSE_ALL_USERS_INFO,
	SERVER_RESPONSE_FRIEND_ACCEPT_BATTLE,
	SERVER_RESPONSE_FRIEND_REJECT_BATTLE,
	SERVER_RESPONSE_BATTLE_FRIEND_NOT_LOGIN,
	SERVER_RESPONSE_BATTLE_ALREADY_IN_BATTLE,
	SERVER_RESPONSE_FAIL_TO_CREATE_BATTLE,
	/* ----------------------------------------------- */
	SERVER_MESSAGE_INVITE_TO_BATTLE,
	SERVER_MESSAGE_BATTLE_INFORMATION,
	SERVER_MESSAGE_YOU_ARE_DEAD,
};

#define USER_STATE_UNUSED    0
#define USER_STATE_NOT_LOGIN 1
#define USER_STATE_LOGIN     2
#define USER_STATE_BATTLE    3

typedef struct pos_t {
	uint8_t x;
	uint8_t y; 
} pos_t;

// format of messages sended from client to server
typedef struct client_message_t {
	uint8_t command;
	char user_id[USERID_SZ]; // last byte must be zero
} client_message_t;

// format of messages sended from server to client
typedef struct server_message_t {
	uint8_t response;
	union {
		// support at most five users
		struct {
			char user_id[USERID_SZ];
			uint8_t user_state;
		} all_users[USER_CNT];

		struct {
			uint8_t life, nr_users;
			pos_t user_pos[USER_CNT];
			pos_t bullet_pos[MAX_ITEM];
		};
	};
} server_message_t;

#endif
