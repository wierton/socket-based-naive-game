#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>

#include "common.h"

#define LINE_MAX_LEN 50

static int scr_actual_w = 0;
static int scr_actual_h = 0;

static char *user_name = "<unknown>";
static char *user_state = "not login";

static int client_fd = -1;

static struct termio raw_termio;

static char *server_addr = "127.0.0.1";


char *readline();

char *strdup(const char *s);

void bottom_bar_output(int line, const char *format, ...);

void server_say(const char *message);

void tiny_debug(const char *output);

char *accept_input(const char *prompt);

int accept_yesno(const char *prompt);

void resume_and_exit(int status);

void display_user_state();

void main_ui();

void draw_button_in_start_ui();

void flip_screen();

void strlwr(char *s) {
	while(*s) {
		if('A' <= *s && *s <= 'Z')
			*s = *s - 'A' + 'a';
		s ++;
	}
}

int connect_to_server() {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if(sockfd < 0) {
		eprintf("Create Socket Failed!\n");
	}

	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT);
	servaddr.sin_addr.s_addr = inet_addr(server_addr);

	if(connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
		eprintf("Can Not Connect To Server %s!\n", server_addr);
	}

	return sockfd;
}

void wrap_send(client_message_t *pcm) {
	size_t total_len = 0;
	while(total_len < sizeof(client_message_t)) {
		size_t len = send(client_fd, pcm + total_len, sizeof(client_message_t) - total_len, 0);
		if(len < 0) {
			loge("broken pipe\n");
		}

		total_len += len;
	}
}

void wrap_recv(server_message_t *psm) {
	size_t total_len = 0;
	while(total_len < sizeof(server_message_t)) {
		size_t len = recv(client_fd, psm + total_len, sizeof(server_message_t) - total_len, 0);
		if(len < 0) {
			loge("broken pipe\n");
		}

		total_len += len;
	}
}

server_message_t *send_recv(client_message_t *pcm) {
	static server_message_t sm;
	wrap_send(pcm);
	wrap_recv(&sm);
	return &sm;
}

void send_command(int command) {
	client_message_t cm;
	cm.command = command;
	wrap_send(&cm);
}

/* all buttons */
enum {
	buttonLogin = 0,
	buttonQuitGame = 1,
	buttonLaunchBattle = 2,
	buttonInviteUser = 3,
	buttonJoinBattle = 4,
	buttonLogout = 5,
};

int button_login() {
	char *name = accept_input("your name: ");
	bottom_bar_output(0, "register your name '%s' to server...", name);

	client_message_t cm;
	memset(&cm, 0, sizeof(client_message_t));
	cm.command = CLIENT_COMMAND_USER_LOGIN;
	strncpy(cm.user_name, name, USERNAME_SIZE - 1);
	server_message_t *psm = send_recv(&cm);

	if(psm->response == SERVER_RESPONSE_LOGIN_SUCCESS) {
		user_name = name;
		user_state = "login";
		flip_screen();
		server_say("welcome to simple net-based game");
		display_user_state();
		main_ui();
		flip_screen();
		display_user_state();
		draw_button_in_start_ui();
	}else if(psm->response == SERVER_RESPONSE_LOGIN_FAIL_DUP_USERID) {
		user_state = "login fail";
		server_say("your name has been registered!");
		display_user_state();
	}else if(psm->response == SERVER_RESPONSE_LOGIN_FAIL_SERVER_LIMITS) {
		user_state = "login fail";
		server_say("server fail");
		display_user_state();
	}else if(psm->response == SERVER_RESPONSE_YOU_HAVE_LOGINED) {
		server_say("you have logined");
	}

	return 0;
}

int button_quit_game() {
	resume_and_exit(0);
	return 0;
}

int button_launch_battle() {
	if(accept_yesno("invite friend? (yes/no)")) {
		char *name = accept_input("your friend name: ");

		client_message_t cm;
		memset(&cm, 0, sizeof(client_message_t));
		cm.command = CLIENT_COMMAND_LAUNCH_BATTLE;
		strncpy(cm.user_name, name, USERNAME_SIZE - 1);
		wrap_send(&cm);
	}else{
		send_command(CLIENT_COMMAND_LAUNCH_BATTLE);
	}

	return 0;
}

int button_invite_user() {
	return 0;
}

int button_join_battle() {
	return 0;
}

int button_logout() {
	user_name = "<unknown>";
	user_state = "not login";
	send_command(CLIENT_COMMAND_LOGOUT);
	bottom_bar_output(0, "logout");
	return -1;
}

/* button position and handler */
struct button_t {
	pos_t pos;
	const char *s;
	int (*button_func)();
} buttons[] = {
	[buttonLogin]        = {
		{24, 5}, "login", button_login,
	},
	[buttonQuitGame]     = {
		{24, 13},  " quit", button_quit_game,
	},
	[buttonLaunchBattle] = {
		{7, 3},  "launch battle", button_launch_battle,
	},
	[buttonInviteUser] = {
		{7, 7},  " invite user ", button_invite_user,
	},
	[buttonJoinBattle] = {
		{7, 11}, " join battle ", button_join_battle,
	},
	[buttonLogout] = {
		{7, 15}, "    logout   ", button_logout,
	},

	// [buttonQuitBattle]   = {{7, 11},   "quit battle"},
};

void wrap_get_term_attr(struct termio *ptbuf) {
	if(ioctl(0, TCGETA, ptbuf) == -1) {
		eprintf("fail to get terminal information\n");
	}
}

void wrap_set_term_attr(struct termio *ptbuf) {
	if(ioctl(0, TCSETA, ptbuf) == -1) {
		eprintf("fail to set terminal information\n");
	}
}

/* functions to change terminal state */
void disable_buffer() {
	struct termio tbuf;
	wrap_get_term_attr(&tbuf);

	tbuf.c_lflag &= ~ICANON;
	tbuf.c_cc[VMIN] = raw_termio.c_cc[VMIN];

	wrap_set_term_attr(&tbuf);
}

void enable_buffer() {
	struct termio tbuf;
	wrap_get_term_attr(&tbuf);

	tbuf.c_lflag |= ICANON;
	tbuf.c_cc[VMIN] = 60;

	wrap_set_term_attr(&tbuf);
}

void echo_off() {
	struct termio tbuf;
	wrap_get_term_attr(&tbuf);

	tbuf.c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL);

	wrap_set_term_attr(&tbuf);
}

void echo_on() {
	struct termio tbuf;
	wrap_get_term_attr(&tbuf);

	tbuf.c_lflag |= (ECHO|ECHOE|ECHOK|ECHONL);

	wrap_set_term_attr(&tbuf);
}

void set_cursor(uint32_t x, uint32_t y) {
	printf("\033[%d;%df", y, x);
}

void hide_cursor() {
	printf("\033[?25l");
}

void show_cursor() {
	printf("\033[?25h");
}

void init_scr_wh() {
	struct winsize ws;
	ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
	scr_actual_w = ws.ws_col;
	scr_actual_h = ws.ws_row;
}

/* functions to maintain bar */
char *readline() {
	char ch;
	int line_ptr = 0;
	char line[LINE_MAX_LEN];
	memset(line, 0, sizeof(line));
	echo_off();
	disable_buffer();
	while((ch = fgetc(stdin)) != '\n') {
		switch(ch) {
			case '\033':{
				ch = fgetc(stdin);
				ch = fgetc(stdin);
				assert('A' <= ch && ch <= 'D');
				break;
			}
			default: {
				if(line_ptr < sizeof(line) - 1
				&& 0x20 <= ch && ch < 0x80) {
					line[line_ptr ++] = ch;
					fputc(ch, stdout);
					fflush(stdout);
				}
			}
		}
	}
	line[line_ptr] = 0;
	enable_buffer();
	echo_on();
	return strdup(line);
}

char *sformat(const char *format, ...) {
	static char text[100];

	va_list ap;
	va_start(ap, format);
	int len = vsprintf(text, format, ap);
	va_end(ap);

	if(len >= sizeof(text))
		eprintf("buffer overflow\n");

	return text;
}

void bottom_bar_output(int line, const char *format, ...) {
	assert(line <= 0);
	set_cursor(1, SCR_H - 1 + line);
	for(int i = 0; i < scr_actual_w; i++)
		printf(" ");
	set_cursor(1, SCR_H - 1 + line);

	va_list ap;
	va_start(ap, format);
	vfprintf(stdout, format, ap);
	va_end(ap);
}

void display_user_state() {
	bottom_bar_output(-1, "name: %s  HP: %d  state: %s", user_name, 0, user_state);
}

void server_say(const char *message) {
	bottom_bar_output(0, "\033[1;37m[%s]\033[0m %s", "server", message);
}

void tiny_debug(const char *output) {
	bottom_bar_output(0, "\033[1;34m[%s]\033[0m %s", "DEBUG", output);
}

char *accept_input(const char *prompt) {
	show_cursor();
	bottom_bar_output(0, prompt);
	char *line = readline();
	hide_cursor();
	return line;
}

int accept_yesno(const char *prompt) {
	while(1) {
		char *input = accept_input(prompt);
		strlwr(input);
		if(strcmp(input, "y") == 0
		|| strcmp(input, "yes") == 0) {
			return true;
		}else if(strcmp(input, "n") == 0
		|| strcmp(input, "no") == 0) {
			return false;
		}
	}
}

void resume_and_exit(int status) {
	send_command(CLIENT_COMMAND_USER_QUIT);
	wrap_set_term_attr(&raw_termio);
	set_cursor(1, SCR_H + 1);
	show_cursor();
	exit(status);
	close(client_fd);
}

/* command handler */
int cmd_quit(char *args) {
	resume_and_exit(0);
	return 0;
}

int cmd_help(char *args) {
	if(args) {
		if(strcmp(args, "--list") == 0) {
			bottom_bar_output(0, "quit, help");
		}else{
			bottom_bar_output(0, "no help for '%s'", args);
		}
	}else{
		bottom_bar_output(0, "usage: help [--list|command]");
	}
	return 0;
}

static struct {
	const char *cmd;
	int	(*func)(char *args);
} command_handler[] = {
	{"quit", cmd_quit},
	{"help", cmd_help},
};

#define NR_HANDLER (sizeof(command_handler) / sizeof(command_handler[0]))

void read_and_execute_command() {
	char *command = accept_input("command: ");
	strtok(command, " \t");
	char *args = strtok(NULL, " \t");

	for(int i = 0; i < NR_HANDLER; i++) {
		if(strcmp(command, command_handler[i].cmd) == 0) {
			command_handler[i].func(args);
			return;
		}
	}

	if(strlen(command) > 0) {
		bottom_bar_output(0, "invalid command '%s'", command);
	}
}

void flip_screen() {
	set_cursor(0, 0);
	for(int i = 0; i < SCR_H - 2; i++) {
		for(int j = 0; j < SCR_W; j++) {
			printf(" ");
		}
		printf("\n");
	}
}

void draw_button(uint32_t button_id) {
	int x = buttons[button_id].pos.x;
	int y = buttons[button_id].pos.y;
	const char *s = buttons[button_id].s;
	int len = strlen(s);
	set_cursor(x, y);
	printf("┌");
	for(int i = 0; i < len; i++)
		printf("─");
	printf("┐");
	set_cursor(x, y + 1);
	printf("│");
	printf("%s", s);
	printf("│");
	set_cursor(x, y + 2);
	printf("└");
	for(int i = 0; i < len; i++)
		printf("─");
	printf("┘");
}

void draw_selected_button(uint32_t button_id) {
	int x = buttons[button_id].pos.x;
	int y = buttons[button_id].pos.y;
	const char *s = buttons[button_id].s;
	int len = strlen(s);
	set_cursor(x, y);
	printf("\033[1m");
	printf("┏");
	for(int i = 0; i < len; i++)
		printf("━");
	printf("┓");
	set_cursor(x, y + 1);
	printf("┃");
	printf("%s", s);
	printf("┃");
	set_cursor(x, y + 2);
	printf("┗");
	for(int i = 0; i < len; i++)
		printf("━");
	printf("┛");
	printf("\033[0m");
}


int match_char(char ch) {
	char cc;
	echo_off();
	disable_buffer();
	do {
		cc = fgetc(stdin);
	} while(cc != '\n' && cc != ch);
	enable_buffer();
	echo_on();
	return cc;
}

int switch_selected_button_respond_to_key(int st, int ed) {
	int sel = st - 1;
	int old_sel = sel;
	while(1) {
		int ch = match_char('\t');
		if(ch == '\n' && st <= sel && sel < ed) {
			return sel;
		}

		sel ++;

		if(old_sel >= st && old_sel < ed)
			draw_button(old_sel);

		if(sel == ed) {
			read_and_execute_command();
			sel = st;
		}

		draw_selected_button(sel);
		old_sel = sel;
	}
}

void draw_button_in_start_ui() {
	draw_button(buttonQuitGame);
	draw_button(buttonLogin);
}

void start_ui() {
	while(1) {
		draw_button_in_start_ui();
		display_user_state();
		int sel = switch_selected_button_respond_to_key(0, 2);
		buttons[sel].button_func();
	}
}

void draw_button_in_main_ui() {
	draw_button(buttonLaunchBattle);
	draw_button(buttonInviteUser);
	draw_button(buttonJoinBattle);
	draw_button(buttonLogout);
}

void main_ui() {
	while(1) {
		draw_button_in_main_ui();
		int sel = switch_selected_button_respond_to_key(2, 6);
		int ret_code = buttons[sel].button_func();

		if(ret_code < 0) {
			break;
		}
	}
}

int main() {
	client_fd = connect_to_server();

	system("clear");
	init_scr_wh();
	wrap_get_term_attr(&raw_termio);
	hide_cursor();

	flip_screen();
	start_ui();

	set_cursor(1, SCR_H + 1);
	resume_and_exit(0);
	return 0;
}
