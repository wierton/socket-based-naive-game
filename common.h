#ifndef COMMON_H
#define COMMON_H

#define VERSION "v1.3.4"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

#ifndef bool
#define bool _Bool
#endif

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
 *   cursor up:             "\033[{count}A"
 *		moves the cursor up by count rows;
 *		the default count is 1.
 *
 *	 cursor down:           "\033[{count}B"
 *		moves the cursor down by count rows;
 *		the default count is 1.
 *
 *	 cursor forward:        "\033[{count}C"
 *		moves the cursor forward by count columns;
 *		the default count is 1.
 *
 *	 cursor backward:       "\033[{count}D"
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

// inner log
#define logi(fmt, ...) \
	fprintf(stderr, "\033[" VT100_STYLE_NORMAL ";" VT100_COLOR_BLUE "m[LOG] \033[0m" "%s:%d: ==> " fmt, __func__, __LINE__, ## __VA_ARGS__)

#define loge(fmt, ...) \
	fprintf(stderr, "\033[" VT100_STYLE_NORMAL ";" VT100_COLOR_RED "m[ERROR] \033[0m" "%s:%d: " fmt, __func__, __LINE__, ## __VA_ARGS__)

#define eprintf(...) do { \
	loge(__VA_ARGS__); \
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
#define SCR_H 18

#define BATTLE_W (SCR_W)
#define BATTLE_H (SCR_H - 2)

#define USERNAME_SIZE  12
#define MSG_SIZE 40
#define USER_CNT   10

#define PASSWORD_SIZE USERNAME_SIZE

#define INIT_BULLETS 12
#define MAX_BULLETS 24
#define BULLETS_PER_MAGAZINE 5

#define INIT_LIFE 5
#define MAX_LIFE 20
#define LIFE_PER_VIAL 3

#define MAGMA_INIT_TIMES 8
#define MAX_OTHER 15

#define MAX_ITEM (USER_CNT * (MAX_BULLETS) + MAX_OTHER)

enum {
	CLIENT_COMMAND_USER_QUIT,
	CLIENT_COMMAND_USER_REGISTER,
	CLIENT_COMMAND_USER_LOGIN,
	CLIENT_COMMAND_USER_LOGOUT,
	CLIENT_COMMAND_FETCH_ALL_USERS,
	CLIENT_COMMAND_FETCH_ALL_FRIENDS,
	CLIENT_COMMAND_LAUNCH_BATTLE,
	CLIENT_COMMAND_QUIT_BATTLE,
	CLIENT_COMMAND_ACCEPT_BATTLE,
	CLIENT_COMMAND_REJECT_BATTLE,
	CLIENT_COMMAND_INVITE_USER,
	CLIENT_COMMAND_SEND_MESSAGE,
	CLIENT_COMMAND_MOVE_UP,
	CLIENT_COMMAND_MOVE_DOWN,
	CLIENT_COMMAND_MOVE_LEFT,
	CLIENT_COMMAND_MOVE_RIGHT,
	CLIENT_COMMAND_FIRE,
	CLIENT_COMMAND_FIRE_UP,
	CLIENT_COMMAND_FIRE_DOWN,
	CLIENT_COMMAND_FIRE_LEFT,
	CLIENT_COMMAND_FIRE_RIGHT,
	CLIENT_MESSAGE_FATAL,
	CLIENT_COMMAND_END,
};

enum {
	SERVER_SAY_NOTHING,
	SERVER_RESPONSE_REGISTER_SUCCESS,
	SERVER_RESPONSE_REGISTER_FAIL,
	SERVER_RESPONSE_YOU_HAVE_REGISTERED,
	SERVER_RESPONSE_LOGIN_SUCCESS,
	SERVER_RESPONSE_YOU_HAVE_LOGINED,
	SERVER_RESPONSE_YOU_HAVE_NOT_LOGIN,
	SERVER_RESPONSE_LOGIN_FAIL_UNREGISTERED_USERID,
	SERVER_RESPONSE_LOGIN_FAIL_ERROR_PASSWORD,
	SERVER_RESPONSE_LOGIN_FAIL_DUP_USERID,    // user id has been registered by other users
	SERVER_RESPONSE_LOGIN_FAIL_SERVER_LIMITS, // server unable to handle more users
	SERVER_RESPONSE_ALL_USERS_INFO,
	SERVER_RESPONSE_ALL_FRIENDS_INFO,
	SERVER_RESPONSE_LAUNCH_BATTLE_FAIL,
	SERVER_RESPONSE_LAUNCH_BATTLE_SUCCESS,
	SERVER_RESPONSE_YOURE_NOT_IN_BATTLE,
	SERVER_RESPONSE_YOURE_ALREADY_IN_BATTLE,
	SERVER_RESPONSE_INVITATION_SENT,
	SERVER_RESPONSE_NOBODY_INVITE_YOU,
	/* ----------------------------------------------- */
	SERVER_MESSAGE_DELIM,
	SERVER_MESSAGE_FRIEND_LOGIN,
	SERVER_MESSAGE_FRIEND_LOGOUT,
	SERVER_MESSAGE_FRIEND_ACCEPT_BATTLE,
	SERVER_MESSAGE_FRIEND_REJECT_BATTLE,
	SERVER_MESSAGE_FRIEND_NOT_LOGIN,
	SERVER_MESSAGE_FRIEND_ALREADY_IN_BATTLE,
	SERVER_MESSAGE_INVITE_TO_BATTLE,
	SERVER_MESSAGE_FRIEND_MESSAGE,
	SERVER_MESSAGE_USER_QUIT_BATTLE,
	SERVER_MESSAGE_BATTLE_DISBANDED,          // since no other users in this battle
	SERVER_MESSAGE_BATTLE_INFORMATION,
	SERVER_MESSAGE_YOU_ARE_DEAD,
	SERVER_MESSAGE_YOU_ARE_SHOOTED,
	SERVER_MESSAGE_YOU_ARE_TRAPPED_IN_MAGMA,
	SERVER_MESSAGE_YOU_GOT_BLOOD_VIAL,
	SERVER_MESSAGE_YOU_GOT_MAGAZINE,
	SERVER_MESSAGE_YOUR_MAGAZINE_IS_EMPTY,

    SERVER_MESSAGE_QUIT,
    SERVER_MESSAGE_FATAL,
};

enum {
	ITEM_NONE,
	ITEM_MAGAZINE,
	ITEM_MAGMA,
	ITEM_GRASS,
	ITEM_BLOOD_VIAL,
	ITEM_END,
	ITEM_BULLET,
};

enum {
	DIR_UP,
	DIR_DOWN,
	DIR_LEFT,
	DIR_RIGHT,
};

enum {
	BATTLE_STATE_UNJOINED,
	BATTLE_STATE_LIVE,
	BATTLE_STATE_WITNESS,
	BATTLE_STATE_DEAD,
};

/* unused  -->  not login  -->  login  <-...->  battle
 *
 * unused  -->  not login  -->  unused  // login fail
 *
 * login  <-->  invited to battle  <-->  battle
 * */
#define USER_STATE_UNUSED          0
#define USER_STATE_NOT_LOGIN       1
#define USER_STATE_LOGIN           2
#define USER_STATE_BATTLE          3
#define USER_STATE_WAIT_TO_BATTLE  4

typedef struct pos_t {
	uint8_t x;
	uint8_t y; 
} pos_t;

// format of messages sended from client to server
typedef struct client_message_t {
	uint8_t command;
	char user_name[USERNAME_SIZE]; // last byte must be zero
	union
	{
		char message[MSG_SIZE];
		char password[PASSWORD_SIZE];
	};
} client_message_t;

// format of messages sended from server to client
typedef struct server_message_t {
	union {
		uint8_t response;
		uint8_t message;
	};

	union {
		// support at most five users
		char friend_name[USERNAME_SIZE];

		struct {
			char user_name[USERNAME_SIZE];
			uint8_t user_state;
		} all_users[USER_CNT];

		struct {
			uint8_t life, index, bullets_num;
			pos_t user_pos[USER_CNT];
			uint8_t item_kind[MAX_ITEM];
			pos_t item_pos[MAX_ITEM];
		};

		struct {
			char from_user[USERNAME_SIZE];
			char msg[MSG_SIZE];
		}; // for message
	};
} server_message_t;

#define BLACK                "\e[0;30m"
#define L_BLACK              "\e[1;30m"
#define RED                  "\e[0;31m"
#define L_RED                "\e[1;31m"
#define GREEN                "\e[0;32m"
#define L_GREEN              "\e[1;32m"
#define BROWN                "\e[0;33m"
#define YELLOW               "\e[1;33m"
#define BLUE                 "\e[0;34m"
#define L_BLUE               "\e[1;34m"
#define PURPLE               "\e[0;35m"
#define L_PURPLE             "\e[1;35m"
#define CYAN                 "\e[0;36m"
#define L_CYAN               "\e[1;36m"
#define GRAY                 "\e[0;37m"
#define WHITE                "\e[1;37m"
#define BOLD                 "\e[1m"
#define UNDERLINE            "\e[4m"
#define BLINK                "\e[5m"
#define REVERSE              "\e[7m"
#define HIDE                 "\e[8m"
#define CLEAR                "\e[2J"
#define CLRLINE              "\r\e[K"
#define NONE                 "\e[0m"


#endif
