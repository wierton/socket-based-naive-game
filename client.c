#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

enum {
	buttonLaunchBattle,
	buttonQuitBattle,
	buttonQuitGame,
};

int client_fd = -1;

struct button_t {
	pos_t pos;
	const char *s;
} buttons[] = {
	[buttonLaunchBattle] = {{7, 3},  "launch battle"},
	[buttonQuitGame]     = {{7, 7}, "  quit game  "},

	[buttonQuitBattle]   = {{7, 11},   "quit battle"},
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

void button_selected(uint32_t button_id) {
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

void accept_command() {
}

int main() {
	// client_fd = connect_to_server();
	flip_screen();
	draw_button(buttonLaunchBattle);
	draw_button(buttonQuitGame);
	button_selected(buttonQuitBattle);

	set_cursor(1, SCR_H + 1);
	return 0;
}
