#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <readline/readline.h>

#include "common.h"

void bottom_bar_output(const char *format, ...);

char *accept_input(const char *prompt);

void resume_and_exit(int status);

static int scr_actual_w = 0;
static int scr_actual_h = 0;

enum {
	buttonLogin = 0,
	buttonLaunchBattle = 1,
	buttonQuitGame = 2,
	buttonQuitBattle,
};

int client_fd = -1;

static struct termio raw_termio;

int button_login() {
	char *name = accept_input("your name: ");
	bottom_bar_output("register your name '%s' to server...", name);
	return 0;
}

int button_launch_battle() {
	return 0;
}

int button_quit_game() {
	resume_and_exit(0);
	return 0;
}

struct button_t {
	pos_t pos;
	const char *s;
	int (*button_func)();
} buttons[] = {
	[buttonLogin]        = {{7, 5}, "    login    ", button_login},
	[buttonLaunchBattle] = {{7, 9},  "launch battle", button_launch_battle},
	[buttonQuitGame]     = {{7, 13},  "  quit game  ", button_quit_game},

	// [buttonQuitBattle]   = {{7, 11},   "quit battle"},
};

static char *server_addr = "127.0.0.1";

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

void flip_screen() {
	set_cursor(0, 0);
	for(int i = 0; i < SCR_H; i++) {
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

void draw_button_in_main_menu() {
	draw_button(buttonLaunchBattle);
	draw_button(buttonQuitGame);
	draw_button(buttonLogin);
}

void command_bar(const char *string) {
	set_cursor(1, SCR_H - 1);
	printf("%s", string);
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

void wrap_gets(char *buf, size_t len) {
	do {
		fgets(buf, len, stdin);
	} while(buf[0] == '\n');

	len = strlen(buf) - 1;

	if(buf[len] == '\n'){
		buf[len] = 0;
	}else{
		while(fgetc(stdin) != '\n');
	}
}

void bottom_bar_output(const char *format, ...) {
	set_cursor(1, SCR_H - 1);
	for(int i = 0; i < scr_actual_w; i++)
		printf(" ");
	set_cursor(1, SCR_H - 1);

	va_list ap;
	va_start(ap, format);
	vfprintf(stdout, format, ap);
	va_end(ap);
}

char *accept_input(const char *prompt) {
	show_cursor();
	bottom_bar_output(prompt);
	char *line = readline(NULL);
	hide_cursor();
	return line;
}

void resume_and_exit(int status) {
	wrap_set_term_attr(&raw_termio);
	set_cursor(1, SCR_H + 1);
	show_cursor();
	exit(status);
}

int cmd_quit(char *args) {
	set_cursor(1, SCR_H + 1);
	resume_and_exit(0);
	return 0;
}

int cmd_help(char *args) {
	bottom_bar_output("your args '%s'", args);
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
			break;
		}
	}

	bottom_bar_output("invalid command '%s'", command);
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

void main_menu() {
	while(1) {
		draw_button_in_main_menu();
		int sel = switch_selected_button_respond_to_key(0, 3);
		buttons[sel].button_func();
	}
}

int main() {
	// client_fd = connect_to_server();
	system("clear");

	init_scr_wh();
	wrap_get_term_attr(&raw_termio);
	hide_cursor();

	flip_screen();
	main_menu();
	close(client_fd);

	set_cursor(1, SCR_H + 1);
	resume_and_exit(0);
	return 0;
}
