#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

#define LINE_MAX_LEN 20

#define wlog(fmt, ...) write_log("%s:%d: " fmt, user_name, __LINE__, ##__VA_ARGS__)
#define wlogi(fmt, ...) write_log("%s:%d: ==> " fmt, user_name, __LINE__, ##__VA_ARGS__)

static int port = 50000, port_range = 100;
static int scr_actual_w = 0;
static int scr_actual_h = 0;

static int user_hp = 0;
static int user_bullets = 0;
static int user_state = USER_STATE_NOT_LOGIN;
static char* user_name = "<unknown>";
static char* user_state_s[] = {
    [USER_STATE_NOT_LOGIN] = "not login",
    [USER_STATE_LOGIN] = "login",
    [USER_STATE_BATTLE] = "battle",
};

static int client_fd = -1;

static struct termio raw_termio;

static char* server_addr;

static int global_serv_message = -1;

pthread_mutex_t cursor_lock = PTHREAD_MUTEX_INITIALIZER;

char* readline();

void write_log(const char* format, ...);

char* strdup(const char* s);

void bottom_bar_output(int line, const char* format, ...);

void server_say(const char* message);

void tiny_debug(const char* output);

char* accept_input(const char* prompt);

int accept_yesno(const char* prompt);

void resume_and_exit(int status);

void display_user_state();

void flip_screen();

void show_cursor();

struct catalog_t {
    pos_t pos;
    const char* title;
    char records[USER_CNT][USERNAME_SIZE];
} friend_list = {
    {34, 1},
    "online friends",
};

typedef struct catalog_t catalog_t;

static char* server_message_s[] = {
    [SERVER_SAY_NOTHING] = "SERVER_SAY_NOTHING",
    [SERVER_RESPONSE_LOGIN_SUCCESS] = "SERVER_RESPONSE_LOGIN_SUCCESS",
    [SERVER_RESPONSE_YOU_HAVE_LOGINED] = "SERVER_RESPONSE_YOU_HAVE_LOGINED",
    [SERVER_RESPONSE_YOU_HAVE_NOT_LOGIN] = "SERVER_RESPONSE_YOU_HAVE_NOT_LOGIN",
    [SERVER_RESPONSE_LOGIN_FAIL_UNREGISTERED_USERID] = "SERVER_RESPONSE_LOGIN_FAIL_UNREGISTERED_USERID",
    [SERVER_RESPONSE_LOGIN_FAIL_ERROR_PASSWORD] = "SERVER_RESPONSE_LOGIN_FAIL_ERROR_PASSWORD",
    [SERVER_RESPONSE_LOGIN_FAIL_DUP_USERID] = "SERVER_RESPONSE_LOGIN_FAIL_DUP_USERID",
    [SERVER_RESPONSE_LOGIN_FAIL_SERVER_LIMITS] = "SERVER_RESPONSE_LOGIN_FAIL_SERVER_LIMITS",
    [SERVER_RESPONSE_ALL_USERS_INFO] = "SERVER_RESPONSE_ALL_USERS_INFO",
    [SERVER_RESPONSE_ALL_FRIENDS_INFO] = "SERVER_RESPONSE_ALL_FRIENDS_INFO",
    [SERVER_RESPONSE_LAUNCH_BATTLE_FAIL] = "SERVER_RESPONSE_LAUNCH_BATTLE_FAIL",
    [SERVER_RESPONSE_LAUNCH_BATTLE_SUCCESS] = "SERVER_RESPONSE_LAUNCH_BATTLE_SUCCESS",
    [SERVER_RESPONSE_YOURE_NOT_IN_BATTLE] = "SERVER_RESPONSE_YOURE_NOT_IN_BATTLE",
    [SERVER_RESPONSE_YOURE_ALREADY_IN_BATTLE] = "SERVER_RESPONSE_YOURE_ALREADY_IN_BATTLE",
    [SERVER_RESPONSE_NOBODY_INVITE_YOU] = "SERVER_RESPONSE_NOBODY_INVITE_YOU",
    [SERVER_MESSAGE_DELIM] = "SERVER_MESSAGE_DELIM",
    [SERVER_MESSAGE_FRIEND_LOGIN] = "SERVER_MESSAGE_FRIEND_LOGIN",
    [SERVER_MESSAGE_FRIEND_LOGOUT] = "SERVER_MESSAGE_FRIEND_LOGOUT",
    [SERVER_MESSAGE_FRIEND_ACCEPT_BATTLE] = "SERVER_MESSAGE_FRIEND_ACCEPT_BATTLE",
    [SERVER_MESSAGE_FRIEND_REJECT_BATTLE] = "SERVER_MESSAGE_FRIEND_REJECT_BATTLE",
    [SERVER_MESSAGE_FRIEND_NOT_LOGIN] = "SERVER_MESSAGE_FRIEND_NOT_LOGIN",
    [SERVER_MESSAGE_FRIEND_ALREADY_IN_BATTLE] = "SERVER_MESSAGE_FRIEND_ALREADY_IN_BATTLE",
    [SERVER_MESSAGE_INVITE_TO_BATTLE] = "SERVER_MESSAGE_INVITE_TO_BATTLE",
    [SERVER_MESSAGE_USER_QUIT_BATTLE] = "SERVER_MESSAGE_USER_QUIT_BATTLE",
    [SERVER_MESSAGE_BATTLE_DISBANDED] = "SERVER_MESSAGE_BATTLE_DISBANDED",
    [SERVER_MESSAGE_BATTLE_INFORMATION] = "SERVER_MESSAGE_BATTLE_INFORMATION",
    [SERVER_MESSAGE_YOUR_MAGAZINE_IS_EMPTY] = "SERVER_MESSAGE_YOUR_MAGAZINE_IS_EMPTY",
    [SERVER_MESSAGE_YOU_ARE_DEAD] = "SERVER_MESSAGE_YOU_ARE_DEAD",
    [SERVER_MESSAGE_YOU_ARE_SHOOTED] = "SERVER_MESSAGE_YOU_ARE_SHOOTED",
    [SERVER_MESSAGE_YOU_ARE_TRAPPED_IN_MAGMA] = "SERVER_MESSAGE_YOU_ARE_TRAPPED_IN_MAGMA",
    [SERVER_MESSAGE_YOU_GOT_BLOOD_VIAL] = "SERVER_MESSAGE_YOU_GOT_BLOOD_VIAL",
    [SERVER_MESSAGE_QUIT] = "SERVER_MESSAGE_QUIT",
};

void strlwr(char* s) {
    while (*s) {
        if ('A' <= *s && *s <= 'Z')
            *s = *s - 'A' + 'a';
        s++;
    }
}

int connect_to_server() {
    log("connecting to %s ...\n", server_addr);
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        eprintf("Create Socket Failed!\n");
    }

    struct sockaddr_in servaddr;
    bool binded = false;
    for (int cur_port = port; cur_port <= port + port_range; cur_port++) {
        memset(&servaddr, 0, sizeof(servaddr));

        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(cur_port);
        servaddr.sin_addr.s_addr = inet_addr(server_addr);

        if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == 0) {
            port = cur_port, binded = true;
            break;
        }
    }
    if (!binded) {
        eprintf("Can Not Connet To Server %s.\n", server_addr);
        exit(1);
    }

    return sockfd;
}

void wrap_send(client_message_t* pcm) {
    size_t total_len = 0;
    while (total_len < sizeof(client_message_t)) {
        size_t len = send(client_fd, pcm + total_len, sizeof(client_message_t) - total_len, 0);
        if (len < 0) {
            loge("broken pipe\n");
        }

        total_len += len;
    }
}

void wrap_recv(server_message_t* psm) {
    size_t total_len = 0;
    while (total_len < sizeof(server_message_t)) {
        size_t len = recv(client_fd, psm + total_len, sizeof(server_message_t) - total_len, 0);
        if (len < 0) {
            loge("broken pipe\n");
        }

        total_len += len;
    }
}

void send_command(int command) {
    client_message_t cm;
    cm.command = command;
    wrap_send(&cm);
}

/* all buttons */
enum {
    buttonLogin = 0,
    buttonRegister = 1,
    buttonQuitGame = 2,
    buttonLaunchBattle = 3,
    buttonInviteUser = 4,
    buttonJoinBattle = 5,
    buttonLogout = 6,
};

int button_login() {
    wlog("call button handler %s\n", __func__);
    wlogi("require name\n");
    char* name = accept_input("your name: ");
    wlogi("input name '%s'\n", name);

    char* password = accept_input("password: ");
    wlogi("input password '%s'\n", password);

    bottom_bar_output(0, "try to login with name '%s' ...", name);

    client_message_t cm;
    memset(&cm, 0, sizeof(client_message_t));
    cm.command = CLIENT_COMMAND_USER_LOGIN;
    strncpy(cm.user_name, name, USERNAME_SIZE - 1);
    strncpy(cm.password, password, PASSWORD_SIZE - 1);
    wlogi("send login message to server\n");
    global_serv_message = -1;
    wrap_send(&cm);

    do {
        if (global_serv_message == SERVER_RESPONSE_LOGIN_SUCCESS
            || global_serv_message == SERVER_RESPONSE_YOU_HAVE_LOGINED
            || global_serv_message == SERVER_RESPONSE_LOGIN_FAIL_UNREGISTERED_USERID
            || global_serv_message == SERVER_RESPONSE_LOGIN_FAIL_ERROR_PASSWORD
            || global_serv_message == SERVER_RESPONSE_LOGIN_FAIL_DUP_USERID
            || global_serv_message == SERVER_RESPONSE_LOGIN_FAIL_SERVER_LIMITS)
            break;
    } while (1);
    wlog("wait until message=%s\n", server_message_s[global_serv_message]);

    if (global_serv_message == SERVER_RESPONSE_LOGIN_SUCCESS) {
        send_command(CLIENT_COMMAND_FETCH_ALL_FRIENDS);
        user_name = name;
    }

    wlogi("set user name to '%s'\n", name);

    return 0;
}

int button_register() {
    wlog("call button handler %s\n", __func__);
    wlogi("require name\n");
    char* name = accept_input("your name: ");
    wlogi("input name '%s'\n", name);

    char* password = accept_input("password: ");
    wlogi("input password '%s'\n", password);

    bottom_bar_output(0, "register your name '%s' to server...", name);

    client_message_t cm;
    memset(&cm, 0, sizeof(client_message_t));
    cm.command = CLIENT_COMMAND_USER_REGISTER;
    strncpy(cm.user_name, name, USERNAME_SIZE - 1);
    strncpy(cm.password, password, PASSWORD_SIZE - 1);
    wlogi("send register message to server\n");
    wrap_send(&cm);

    return 0;
}

int button_quit_game() {
    wlog("call button handler %s\n", __func__);
    resume_and_exit(0);
    return 0;
}

int button_launch_battle() {
    wlog("call button handler %s\n", __func__);
    wlogi("send `launch battle` message to server\n");
    global_serv_message = -1;
    send_command(CLIENT_COMMAND_LAUNCH_BATTLE);
    /* wait for server reply */
    do {
        if (global_serv_message == SERVER_RESPONSE_LAUNCH_BATTLE_SUCCESS
            || global_serv_message == SERVER_RESPONSE_LAUNCH_BATTLE_FAIL)
            break;
    } while (1);
    wlog("wait until message=%s\n", server_message_s[global_serv_message]);
    return 0;
}

int button_invite_user() {
    wlog("call button handler %s\n", __func__);
    /* send invitation */
    wlogi("ask friend's name\n");
    char* name = accept_input("invite who to your battle: ");
    wlogi("friend name '%s'\n", name);
    client_message_t cm;
    memset(&cm, 0, sizeof(client_message_t));
    cm.command = CLIENT_COMMAND_LAUNCH_BATTLE;
    strncpy(cm.user_name, name, USERNAME_SIZE - 1);
    wlogi("send `launch battle` and invitation to server\n");
    global_serv_message = -1;
    wrap_send(&cm);
    /* wait for server reply */
    do {
        if (global_serv_message == SERVER_RESPONSE_LAUNCH_BATTLE_SUCCESS
            || global_serv_message == SERVER_RESPONSE_LAUNCH_BATTLE_FAIL)
            break;
    } while (1);
    wlog("wait until message=%s\n", server_message_s[global_serv_message]);
    return 0;
}

int button_join_battle() {
    wlog("call button handler %s\n", __func__);
    send_command(CLIENT_COMMAND_ACCEPT_BATTLE);
    user_state = USER_STATE_BATTLE;
    return 0;
}

int button_logout() {
    wlog("call button handler %s\n", __func__);
    user_name = "<unknown>";
    user_state = USER_STATE_NOT_LOGIN;
    send_command(CLIENT_COMMAND_USER_LOGOUT);
    bottom_bar_output(0, "logout");
    return -1;
}

/* button position and handler */
struct button_t {
    pos_t pos;
    const char* s;
    int (*button_func)();
} buttons[] = {
    [buttonLogin] = {
        {24, 3},
        " login  ",
        button_login,
    },
    [buttonRegister] = {
        {24, 7},
        "register",
        button_register,
    },
    [buttonQuitGame] = {
        {24, 11},
        "  quit  ",
        button_quit_game,
    },
    [buttonLaunchBattle] = {
        {7, 1},
        "launch battle",
        button_launch_battle,
    },
    [buttonInviteUser] = {
        {7, 5},
        " invite user ",
        button_invite_user,
    },
    [buttonJoinBattle] = {
        {7, 9},
        "accept battle",
        button_join_battle,
    },
    [buttonLogout] = {
        {7, 13},
        "    logout   ",
        button_logout,
    },

    // [buttonQuitBattle]   = {{7, 11},   "quit battle"},
};

void wrap_get_term_attr(struct termio* ptbuf) {
    if (ioctl(0, TCGETA, ptbuf) == -1) {
        eprintf("fail to get terminal information\n");
    }
}

void wrap_set_term_attr(struct termio* ptbuf) {
    if (ioctl(0, TCSETA, ptbuf) == -1) {
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

    tbuf.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);

    wrap_set_term_attr(&tbuf);
}

void echo_on() {
    struct termio tbuf;
    wrap_get_term_attr(&tbuf);

    tbuf.c_lflag |= (ECHO | ECHOE | ECHOK | ECHONL);

    wrap_set_term_attr(&tbuf);
}

void set_cursor(uint32_t x, uint32_t y) {
    printf("\033[%d;%df", y + 1, x + 1);
}

void hide_cursor() {
    printf("\033[?25l");
}

void show_cursor() {
    printf("\033[?25h");
    fflush(stdout);
}

int keyboard_detected() {
    return 0;
}

void save_cursor_pos() {
    printf("\033[s");
}

void load_cursor_pos() {
    printf("\033u");
}

void lock_cursor() {
    pthread_mutex_lock(&cursor_lock);
}

void unlock_cursor() {
    pthread_mutex_unlock(&cursor_lock);
}

void init_scr_wh() {
    struct winsize ws;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    scr_actual_w = ws.ws_col;
    scr_actual_h = ws.ws_row;
}

/* functions to maintain bar */
char* readline() {
    char ch;
    int line_ptr = 0;
    char line[LINE_MAX_LEN];
    memset(line, 0, sizeof(line));
    lock_cursor();
    echo_off();
    disable_buffer();
    while ((ch = fgetc(stdin)) != '\n') {
        switch (ch) {
            case '\033': {
                ch = fgetc(stdin);
                ch = fgetc(stdin);
                assert('A' <= ch && ch <= 'D');
                break;
            }
            case 0x7f:  //handle backspace(sends delete)
                if (line_ptr == 0) break;
                line[--line_ptr] = '\0';
                fputc(ch, stdout);
                fflush(stdout);
                break;
            default: {
                if (line_ptr < sizeof(line) - 1
                    && 0x20 <= ch && ch < 0x80) {
                    line[line_ptr++] = ch;
                    fputc(ch, stdout);
                    fflush(stdout);
                }
            }
        }
    }
    line[line_ptr] = 0;
    unlock_cursor();
    return strdup(line);
}

char* sformat(const char* format, ...) {
    static char text[100];

    va_list ap;
    va_start(ap, format);
    int len = vsprintf(text, format, ap);
    va_end(ap);

    if (len >= sizeof(text))
        eprintf("buffer overflow\n");

    return text;
}

void bottom_bar_output(int line, const char* format, ...) {
    assert(line <= 0);
    lock_cursor();
    set_cursor(0, SCR_H - 1 + line);
    for (int i = 0; i < scr_actual_w; i++)
        printf(" ");
    set_cursor(0, SCR_H - 1 + line);

    va_list ap;
    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);

    fflush(stdout);

    unlock_cursor();
}

void write_log(const char* format, ...) {
    FILE* fp = fopen("log.txt", "a+");

    va_list ap;
    va_start(ap, format);
    vfprintf(fp, format, ap);
    va_end(ap);

    fclose(fp);
}

void display_user_state() {
    assert(user_state <= sizeof(user_state_s) / sizeof(user_state_s[0]));
    bottom_bar_output(-1, "name: %s  HP: %d  bullets: %d  state: %s", user_name, user_hp, user_bullets, user_state_s[user_state]);
}

void server_say(const char* message) {
    bottom_bar_output(0, "\033[1;37m[%s]\033[0m %s", "server", message);
}

void tiny_debug(const char* output) {
    bottom_bar_output(0, "\033[1;34m[%s]\033[0m %s", "DEBUG", output);
}

void error(const char* output) {
    bottom_bar_output(0, "\033[1;31m[%s]\033[0m %s", "ERROR", output);
}

char* accept_input(const char* prompt) {
    char* line;
    show_cursor();
    do {
        bottom_bar_output(0, prompt);
        line = readline();
    } while (strncmp(line, "", 1) == 0);
    hide_cursor();
    return line;
}

int accept_yesno(const char* prompt) {
    while (1) {
        char* input = accept_input(prompt);
        strlwr(input);
        if (strcmp(input, "y") == 0
            || strcmp(input, "yes") == 0) {
            return true;
        } else if (strcmp(input, "n") == 0
                   || strcmp(input, "no") == 0) {
            return false;
        }
    }
}

void resume_and_exit(int status) {
    show_cursor();
    send_command(CLIENT_COMMAND_USER_QUIT);
    wrap_set_term_attr(&raw_termio);
    set_cursor(0, SCR_H);
    close(client_fd);
    wlog("====================EXIT====================\n\n\n");
    exit(status);
}

/* command handler */
int cmd_quit(char* args) {
    resume_and_exit(0);
    return 0;
}

int cmd_ulist(char* args) {
    if (user_state == USER_STATE_NOT_LOGIN)
        bottom_bar_output(0, "Please login first!");
    else
        send_command(CLIENT_COMMAND_FETCH_ALL_USERS);
    return 0;
}

int cmd_invite(char* args) {
    if (user_state == USER_STATE_NOT_LOGIN) {
        bottom_bar_output(0, "Please login first!");
        return 0;
    }
    client_message_t cm;
    cm.command = CLIENT_COMMAND_INVITE_USER;
    strncpy(cm.user_name, args, USERNAME_SIZE - 1);
    wrap_send(&cm);
    return 0;
}

int cmd_yell(char* args) {
    if (user_state == USER_STATE_NOT_LOGIN) {
        bottom_bar_output(0, "Please login first!");
        return 0;
    }
    char* msg = accept_input("Yell at all users: ");
    client_message_t cm;
    cm.command = CLIENT_COMMAND_SEND_MESSAGE;
    cm.user_name[0] = '\0';
    strncpy(cm.message, msg, MSG_SIZE - 1);
    wrap_send(&cm);
    return 0;
}

int cmd_tell(char* args) {
    if (user_state == USER_STATE_NOT_LOGIN) {
        bottom_bar_output(0, "Please login first!");
        return 0;
    }
    if (!args) {
        bottom_bar_output(0, "Input a friend, or try \"yell\"");
        return 0;
    }
    char* msg = accept_input(sformat("Speak to %s: ", args));
    client_message_t cm;
    cm.command = CLIENT_COMMAND_SEND_MESSAGE;
    strncpy(cm.user_name, args, USERNAME_SIZE - 1);
    strncpy(cm.message, msg, MSG_SIZE - 1);
    wrap_send(&cm);
    return 0;
}

int cmd_fuck() {
    send_command(CLIENT_MESSAGE_FATAL);
    return 0;
}

int cmd_help(char* args) {
    if (args) {
        if (strcmp(args, "--list") == 0) {
            bottom_bar_output(0, "quit, help, ulist, invite, yell, tell, fuck");
        } else if (strcmp(args, "quit") == 0) {
            bottom_bar_output(0, "quit the game and return terminal");
        } else if (strcmp(args, "ulist") == 0) {
            bottom_bar_output(0, "list all online friends");
        } else if (strcmp(args, "invite") == 0) {
            bottom_bar_output(0, "invite friend to your battle(need args)");
        } else if (strcmp(args, "yell") == 0) {
            bottom_bar_output(0, "send message to all friends");
        } else if (strcmp(args, "tell") == 0) {
            bottom_bar_output(0, "send message to one friend(need args)");
        } else if (strcmp(args, "fuck") == 0) {
            bottom_bar_output(0, "forced stop ALL client and server");
        } else {
            bottom_bar_output(0, "no help for '%s'", args);
        }
    } else {
        bottom_bar_output(0, "usage: help [--list|command]");
    }
    return 0;
}

static struct {
    const char* cmd;
    int (*func)(char* args);
} command_handler[] = {
    {"quit", cmd_quit},
    {"ulist", cmd_ulist},
    {"invite", cmd_invite},
    {"yell", cmd_yell},
    {"tell", cmd_tell},
    {"fuck", cmd_fuck},
    /* ------------------- */
    {"help", cmd_help},
};

#define NR_HANDLER (sizeof(command_handler) / sizeof(command_handler[0]))

void read_and_execute_command() {
    char* command = accept_input("command: ");
    wlog("accept command: '%s'\n", command);
    strtok(command, " \t");
    char* args = strtok(NULL, " \t");

    for (int i = 0; i < NR_HANDLER; i++) {
        if (strcmp(command, command_handler[i].cmd) == 0) {
            command_handler[i].func(args);
            return;
        }
    }

    if (strlen(command) > 0) {
        bottom_bar_output(0, "invalid command '%s'", command);
    }
}

void flip_screen() {
    lock_cursor();
    set_cursor(0, SCR_H);
    printf("\033[2J");
    unlock_cursor();
}

void draw_button(uint32_t button_id) {
    int x = buttons[button_id].pos.x;
    int y = buttons[button_id].pos.y;
    const char* s = buttons[button_id].s;
    int len = strlen(s);
    lock_cursor();
    set_cursor(x, y);
    printf("┌");
    for (int i = 0; i < len; i++)
        printf("─");
    printf("┐");
    set_cursor(x, y + 1);
    printf("│");
    printf("%s", s);
    printf("│");
    set_cursor(x, y + 2);
    printf("└");
    for (int i = 0; i < len; i++)
        printf("─");
    printf("┘");
    unlock_cursor();
}

void draw_selected_button(uint32_t button_id) {
    int x = buttons[button_id].pos.x;
    int y = buttons[button_id].pos.y;
    const char* s = buttons[button_id].s;
    int len = strlen(s);
    lock_cursor();
    set_cursor(x, y);
    printf("\033[1m");
    printf("┏");
    for (int i = 0; i < len; i++)
        printf("━");
    printf("┓");
    set_cursor(x, y + 1);
    printf("┃");
    printf("%s", s);
    printf("┃");
    set_cursor(x, y + 2);
    printf("┗");
    for (int i = 0; i < len; i++)
        printf("━");
    printf("┛");
    printf("\033[0m");
    unlock_cursor();
}

void draw_catalog(catalog_t* pcl) {
    int x = pcl->pos.x;
    int y = pcl->pos.y;
    int len = strlen(pcl->title);
    int w = len;
    if (len < USERNAME_SIZE) w = USERNAME_SIZE;

    lock_cursor();
    set_cursor(x, y);
    printf("┌");
    for (int i = 0; i < len; i++)
        printf("─");
    printf("┐");
    set_cursor(x, y + 1);
    printf("│");
    printf("%s", pcl->title);
    for (int i = w - len; i > 0; i--)
        printf(" ");
    printf("│");

    set_cursor(x, y + 2);
    printf("├");
    for (int i = 0; i < w; i++)
        printf("─");
    printf("┤");

    for (int i = 0; i < USER_CNT; i++) {
        int j = 0;
        int ulen = strlen(pcl->records[i]);
        set_cursor(x, y + i + 3);
        printf("│");
        for (; j < (w - ulen) / 2; j++)
            printf(" ");
        if (ulen > 0) {
            printf("%s", pcl->records[i]);
            j += ulen;
        } else {
            printf("\b*");
        }
        for (; j < w; j++)
            printf(" ");
        printf("│");
    }

    set_cursor(x, y + USER_CNT + 3);
    printf("└");
    for (int i = 0; i < w; i++)
        printf("─");
    printf("┘");

    fflush(stdout);

    unlock_cursor();
}

void run_battle() {
    wlog("run battle\n");
    flip_screen();
    bottom_bar_output(0, "type <TAB> to enter command mode and invite more friends\n");
    echo_off();
    disable_buffer();
    while (user_state == USER_STATE_BATTLE) {
        int ch = fgetc(stdin);
        if (ch == 'q') {
            wlog("type q and quit battle\n");
            user_state = USER_STATE_LOGIN;
            send_command(CLIENT_COMMAND_QUIT_BATTLE);
            send_command(CLIENT_COMMAND_FETCH_ALL_FRIENDS);
            break;
        } else if (ch == '\t') {
            wlog("type <TAB> and enter command mode\n");
            read_and_execute_command();
        }

        switch (ch) {
            case 'w': send_command(CLIENT_COMMAND_MOVE_UP); break;
            case 's': send_command(CLIENT_COMMAND_MOVE_DOWN); break;
            case 'a': send_command(CLIENT_COMMAND_MOVE_LEFT); break;
            case 'd': send_command(CLIENT_COMMAND_MOVE_RIGHT); break;
            case ' ': send_command(CLIENT_COMMAND_FIRE); break;
            case 'h': send_command(CLIENT_COMMAND_FIRE_LEFT); break;
            case 'j': send_command(CLIENT_COMMAND_FIRE_DOWN); break;
            case 'k': send_command(CLIENT_COMMAND_FIRE_UP); break;
            case 'l': send_command(CLIENT_COMMAND_FIRE_RIGHT); break;
        }
    }

    flip_screen();
    wlog("exit run_battle\n");
}

int switch_selected_button_respond_to_key(int st, int ed) {
    int sel = st - 1;
    int old_sel = sel;

    if (st >= ed) return st;

    echo_off();
    disable_buffer();
    wlog("enter select button, [%d, %d)\n", st, ed);
    while (1) {
        int ch = fgetc(stdin);
        wlog("capture key '%c' in button ui\n", ch);
        switch (ch) {
            case 'a':
            case 'w':
                sel--;
                if (sel < st) sel = ed - 1;
                break;
            case 's':
            case 'd':
                sel++;
                if (sel >= ed) sel = st;
                break;
            case '\t':
                wlog("sel_menu enter command mode\n");
                read_and_execute_command();
                sel = st;
                break;
        }

        if (ch == '\n') {
            if (st <= sel && sel < ed) {
                break;
            } else {
                sel = st;
            }
        }

        if (st <= old_sel && old_sel < ed) {
            draw_button(old_sel);
        }

        if (st <= sel && sel < ed)
            draw_selected_button(sel);
        old_sel = sel;
    }

    echo_on();
    wlogi("return sel: %d\n", sel);
    return sel;
}

void draw_button_in_main_ui() {
    draw_button(buttonLaunchBattle);
    draw_button(buttonInviteUser);
    draw_button(buttonJoinBattle);
    draw_button(buttonLogout);
    draw_catalog(&friend_list);
}

void main_ui() {
    flip_screen();
    while (1) {
        draw_button_in_main_ui();
        display_user_state();
        int sel = switch_selected_button_respond_to_key(3, 7);
        wlog("user select %d\n", sel);

        int ret_code = buttons[sel].button_func();
        wlog("button handler return %d\n", ret_code);

        if (ret_code < 0 || user_state == USER_STATE_NOT_LOGIN) {
            flip_screen();
            wlog("quit main ui with user_state:%d\n", user_state);
            break;
        }

        if (user_state == USER_STATE_BATTLE) {
            wlog("enter battle mode\n");
            run_battle();
        }
    }
}

void draw_button_in_start_ui() {
    draw_button(buttonLogin);
    draw_button(buttonRegister);
    draw_button(buttonQuitGame);
}

void start_ui() {
    wlog("enter start ui\n");
    bottom_bar_output(0, "type w s a d to switch button, type <TAB> to enter command");
    while (1) {
        draw_button_in_start_ui();
        display_user_state();
        int sel = switch_selected_button_respond_to_key(0, 3);
        wlog("user select %d@%s\n", sel, buttons[sel].s);
        buttons[sel].button_func();

        if (user_state == USER_STATE_LOGIN) {
            wlog("user state changed into LOGIN\n");
            wlog("enter main ui\n");
            main_ui();
        }
    }
}

int serv_quit(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    system("clear");
    puts("forced terminated by server.\033[?25h");
    resume_and_exit(1);
    return 0;
}

int serv_fatal(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    system("clear");
    puts("forced terminated by a user.\033[?25h");
    resume_and_exit(3);
    return 0;
}

int serv_response_you_have_not_login(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("you haven't logined");
    return 0;
}

int serv_response_register_success(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("register success");
    return 0;
}

int serv_response_register_fail(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("register fail");
    return 0;
}

int serv_response_you_have_registered(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("your name has been registered!");
    return 0;
}

int serv_response_login_success(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    wlog("==> try server_say\n");
    server_say("welcome to simple net-based game");
    wlog("==> change user_state from %s into %s\n", user_state_s[user_state], user_state_s[USER_STATE_LOGIN]);
    user_state = USER_STATE_LOGIN;
    wlog("==> update user state to screen\n");
    display_user_state();
    return 0;
}

int serv_response_login_fail_unregistered_userid(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("your name hasn't been registered!");
    display_user_state();
    return 0;
}

int serv_response_login_fail_error_password(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("error password!");
    display_user_state();
    return 0;
}

int serv_response_login_fail_dup_userid(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("you have logined in another place!");
    display_user_state();
    return 0;
}

int serv_response_login_fail_server_limits(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("server fail");
    display_user_state();
    return 0;
}

int serv_response_you_have_logined(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("you have logined");
    return 0;
}

int serv_response_all_users_info(server_message_t* psm) {
    int len = 0;
    static char users[USERNAME_SIZE * USER_CNT];
    wlog("call message handler %s\n", __func__);
    for (int i = 0; i < USER_CNT; i++) {
        int state = psm->all_users[i].user_state;
        if (state != USER_STATE_UNUSED
            && state != USER_STATE_NOT_LOGIN) {
            len += sprintf(users + len, "%s, ", psm->all_users[i].user_name);
        }
    }
    if (len > sizeof(users) - 1)
        eprintf("buffer overflow\n");

    users[len - 2] = 0;
    wlog("server response user list: %s\n", users);
    bottom_bar_output(0, "online: %s", users);
    return 0;
}

int serv_response_all_friends_info(server_message_t* psm) {
    int j = 0;
    wlog("call message handler %s\n", __func__);
    for (int i = 0; i < USER_CNT; i++) {
        int state = psm->all_users[i].user_state;
        if (state != USER_STATE_UNUSED
            && state != USER_STATE_NOT_LOGIN) {
            strncpy(friend_list.records[j++],
                    psm->all_users[i].user_name,
                    USERNAME_SIZE - 1);
        }
    }
    wlog("draw catalogs of friends\n");
    draw_catalog(&friend_list);
    return 0;
}

int serv_response_launch_battle_fail(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("launch battle failed");
    return 0;
}

int serv_response_launch_battle_success(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("launch battle success");
    user_state = USER_STATE_BATTLE;
    return 0;
}

int serv_response_nobody_invite_you(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("no body invite you");
    user_state = USER_STATE_LOGIN;
    return 0;
}

int serv_response_invitation_sent(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("invitation has been sent");
    return 0;
}

int serv_msg_friend_login(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    for (int i = 0; i < USER_CNT; i++) {
        if (friend_list.records[i][0] == 0) {
            strncpy(friend_list.records[i], psm->friend_name, USERNAME_SIZE - 1);
            break;
        }
    }
    if (user_state == USER_STATE_LOGIN)
        draw_catalog(&friend_list);
    server_say(sformat("friend %s login\n", psm->friend_name));
    return 0;
}

int serv_msg_friend_logout(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    for (int i = 0; i < USER_CNT; i++) {
        if (strncmp(friend_list.records[i], psm->friend_name, USERNAME_SIZE - 1) == 0) {
            friend_list.records[i][0] = 0;
            break;
        }
    }
    if (user_state == USER_STATE_LOGIN)
        draw_catalog(&friend_list);
    server_say(sformat("friend %s logout\n", psm->friend_name));
    return 0;
}

int serv_msg_accept_battle(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say(sformat("friend %s accept your invitation", psm->friend_name));
    return 0;
}

int serv_msg_reject_battle(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    wlog("friend '%s' reject battle\n", psm->friend_name);
    server_say(sformat("friend %s reject your invitation", psm->friend_name));
    return 0;
}

int serv_msg_friend_not_login(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say(sformat("friend %s hasn't login", psm->friend_name));
    return 0;
}

int serv_msg_friend_already_in_battle(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say(sformat("friend %s has join another battle", psm->friend_name));
    return 0;
}

int serv_msg_you_are_invited(server_message_t* psm) {
    // FIXME:
    wlog("call message handler %s\n", __func__);
    server_say(sformat("friend %s invite you to his battle [press button to join]", psm->friend_name));
    return 0;
}

int serv_msg_friend_msg(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say(sformat("%s: %s", psm->from_user, psm->msg));
    return 0;
}

int serv_msg_friend_quit_battle(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say(sformat("friend %s quit battle", psm->friend_name));
    return 0;
}

int serv_msg_battle_disbanded(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("battle is disbanded");
    return 0;
}

void flip_old_items(server_message_t* psm) {
    static server_message_t sm = {0};
    wlog("call flip old items\n");
    lock_cursor();
    for (int i = 0; i < USER_CNT; i++) {
        if (sm.user_pos[i].x >= BATTLE_W
            || sm.user_pos[i].y >= BATTLE_H)
            continue;

        wlog("clear user %d@(%d, %d)\n", i, sm.user_pos[i].x, sm.user_pos[i].y);
        set_cursor(sm.user_pos[i].x, sm.user_pos[i].y);
        printf(" ");
    }

    for (int i = 0; i < MAX_ITEM; i++) {
        if (sm.item_kind[i] != ITEM_BULLET)
            continue;

        set_cursor(sm.item_pos[i].x, sm.item_pos[i].y);
        printf(" ");
    }

    fflush(stdout);

    unlock_cursor();
    memcpy(&sm, psm, sizeof(server_message_t));
}

void draw_users(server_message_t* psm) {
    lock_cursor();
    for (int i = 0; i < USER_CNT; i++) {
        if (psm->user_pos[i].x >= BATTLE_W
            || psm->user_pos[i].y >= BATTLE_H)
            continue;

        set_cursor(psm->user_pos[i].x, psm->user_pos[i].y);
        if (i == psm->index)
            printf(GREEN "Y" NONE);
        else
            printf(RED "A" NONE);
    }

    fflush(stdout);
    unlock_cursor();
}

void draw_items(server_message_t* psm) {
    lock_cursor();
    for (int i = 0; i < MAX_ITEM; i++) {
        if (psm->item_kind[i] == ITEM_NONE)
            continue;

        set_cursor(psm->item_pos[i].x, psm->item_pos[i].y);
        switch (psm->item_kind[i]) {
            case ITEM_MAGAZINE: printf("+"); break;
            case ITEM_MAGMA: printf("X"); break;
            case ITEM_GRASS: printf("█"); break;
            case ITEM_BLOOD_VIAL: printf("*"); break;
            case ITEM_BULLET: printf(".");
        }
    }

    fflush(stdout);

    unlock_cursor();
}

void log_psm_info(server_message_t* psm) {
    int len = 0;
    static char s[1024 * 1024];
    char* p = s;
    len += sprintf(p + len, "message: %d, life:%d, user_index:%d\n", psm->message, psm->life, psm->index);
    len += sprintf(p + len, "user_pos:");
    for (int i = 0; i < USER_CNT; i++) {
        len += sprintf(p + len, "(%d,%d), ", psm->user_pos[i].x, psm->user_pos[i].y);
    }

    len += sprintf(p + len, "\n");

    len += sprintf(p + len, "items:");
    for (int i = 0; i < MAX_ITEM; i++) {
        len += sprintf(p + len, "(%d:%d,%d), ", psm->item_kind[i], psm->item_pos[i].x, psm->item_pos[i].y);
    }

    len += sprintf(p + len, "\n");

    if (len > sizeof(s) - 1)
        eprintf("buffer overflow\n");

    wlog("battle info:\n%s\n", s);
}

int serv_msg_battle_info(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    if (user_state == USER_STATE_BATTLE) {
        log_psm_info(psm);
        user_bullets = psm->bullets_num;
        user_hp = psm->life;
        flip_old_items(psm);
        draw_users(psm);
        draw_items(psm);
        display_user_state();
    }
    return 0;
}

int serv_msg_you_are_dead(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("you're dead");
    return 0;
}

int serv_msg_you_are_shooted(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("you're shooted");
    return 0;
}

int serv_msg_you_are_trapped_in_magma(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("you're burned by magma");
    return 0;
}

int serv_msg_you_got_blood_vial(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("you got blood vial");
    return 0;
}

int server_message_you_got_magazine(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("you got charger");
    return 0;
}

int server_message_your_magazine_is_empty(server_message_t* psm) {
    wlog("call message handler %s\n", __func__);
    server_say("your charger is empty");
    return 0;
}

static int (*recv_msg_func[])(server_message_t*) = {
    [SERVER_RESPONSE_REGISTER_SUCCESS] = serv_response_register_success,
    [SERVER_RESPONSE_REGISTER_FAIL] = serv_response_register_fail,
    [SERVER_RESPONSE_YOU_HAVE_REGISTERED] = serv_response_you_have_registered,
    [SERVER_RESPONSE_LOGIN_SUCCESS] = serv_response_login_success,
    [SERVER_RESPONSE_YOU_HAVE_NOT_LOGIN] = serv_response_you_have_not_login,
    [SERVER_RESPONSE_LOGIN_FAIL_UNREGISTERED_USERID] = serv_response_login_fail_unregistered_userid,
    [SERVER_RESPONSE_LOGIN_FAIL_ERROR_PASSWORD] = serv_response_login_fail_error_password,
    [SERVER_RESPONSE_LOGIN_FAIL_DUP_USERID] = serv_response_login_fail_dup_userid,
    [SERVER_RESPONSE_LOGIN_FAIL_SERVER_LIMITS] = serv_response_login_fail_server_limits,
    [SERVER_RESPONSE_YOU_HAVE_LOGINED] = serv_response_you_have_logined,
    [SERVER_RESPONSE_ALL_USERS_INFO] = serv_response_all_users_info,
    [SERVER_RESPONSE_ALL_FRIENDS_INFO] = serv_response_all_friends_info,
    [SERVER_RESPONSE_LAUNCH_BATTLE_FAIL] = serv_response_launch_battle_fail,
    [SERVER_RESPONSE_LAUNCH_BATTLE_SUCCESS] = serv_response_launch_battle_success,
    [SERVER_RESPONSE_NOBODY_INVITE_YOU] = serv_response_nobody_invite_you,
    [SERVER_RESPONSE_INVITATION_SENT] = serv_response_invitation_sent,
    [SERVER_MESSAGE_FRIEND_LOGIN] = serv_msg_friend_login,
    [SERVER_MESSAGE_FRIEND_LOGOUT] = serv_msg_friend_logout,
    [SERVER_MESSAGE_FRIEND_ACCEPT_BATTLE] = serv_msg_accept_battle,
    [SERVER_MESSAGE_FRIEND_REJECT_BATTLE] = serv_msg_reject_battle,
    [SERVER_MESSAGE_FRIEND_NOT_LOGIN] = serv_msg_friend_not_login,
    [SERVER_MESSAGE_FRIEND_ALREADY_IN_BATTLE] = serv_msg_friend_already_in_battle,
    [SERVER_MESSAGE_INVITE_TO_BATTLE] = serv_msg_you_are_invited,
    [SERVER_MESSAGE_FRIEND_MESSAGE] = serv_msg_friend_msg,
    [SERVER_MESSAGE_USER_QUIT_BATTLE] = serv_msg_friend_quit_battle,
    [SERVER_MESSAGE_BATTLE_DISBANDED] = serv_msg_battle_disbanded,
    [SERVER_MESSAGE_BATTLE_INFORMATION] = serv_msg_battle_info,
    [SERVER_MESSAGE_YOU_ARE_DEAD] = serv_msg_you_are_dead,
    [SERVER_MESSAGE_YOU_ARE_SHOOTED] = serv_msg_you_are_shooted,
    [SERVER_MESSAGE_YOU_ARE_TRAPPED_IN_MAGMA] = serv_msg_you_are_trapped_in_magma,
    [SERVER_MESSAGE_YOU_GOT_BLOOD_VIAL] = serv_msg_you_got_blood_vial,
    [SERVER_MESSAGE_YOU_GOT_MAGAZINE] = server_message_you_got_magazine,
    [SERVER_MESSAGE_YOUR_MAGAZINE_IS_EMPTY] = server_message_your_magazine_is_empty,
    [SERVER_MESSAGE_QUIT] = serv_quit,
    [SERVER_MESSAGE_FATAL] = serv_fatal,
};

void* message_monitor(void* args) {
    server_message_t sm;
    wlog("monitor thread starts\n");
    while (1) {
        wrap_recv(&sm);
        wlog("receive server message: %s\n", server_message_s[sm.message]);
        if (recv_msg_func[sm.message]) {
            wlog("==> call message handler\n");
            recv_msg_func[sm.message](&sm);
            wlog("==> quit message handler\n");
        }

        // delay assignment
        if (sm.message == SERVER_RESPONSE_LOGIN_SUCCESS
            || sm.message == SERVER_RESPONSE_YOU_HAVE_LOGINED
            || sm.message == SERVER_RESPONSE_LOGIN_FAIL_UNREGISTERED_USERID
            || sm.message == SERVER_RESPONSE_LOGIN_FAIL_ERROR_PASSWORD
            || sm.message == SERVER_RESPONSE_LOGIN_FAIL_DUP_USERID
            || sm.message == SERVER_RESPONSE_LOGIN_FAIL_SERVER_LIMITS
            || sm.message == SERVER_RESPONSE_LAUNCH_BATTLE_SUCCESS
            || sm.message == SERVER_RESPONSE_LAUNCH_BATTLE_FAIL)
            global_serv_message = sm.message;
    }
    return NULL;
}

void start_message_monitor() {
    pthread_t thread;
    if (pthread_create(&thread, NULL, message_monitor, NULL) != 0) {
        eprintf("fail to start message monitor.\n");
    }
}

void terminate(int signum) {
    system("clear");
    puts("forced quit.\033[?25h");
    resume_and_exit(signum);
}

int main(int argc, char* argv[]) {
    system("rm ./log.txt");
    if (argc >= 2) {
        server_addr = (char*)malloc(256);
        strcpy(server_addr, argv[1]);
        if (argc >= 3) {
            port = atoi(argv[2]);
        }
    } else {
        server_addr = (char*)malloc(256);
        strcpy(server_addr, "127.0.0.1");
    }
    wlog("====================START====================\n");
    log("client " VERSION "\n");
    client_fd = connect_to_server();
    if (signal(SIGINT, terminate) == SIG_ERR) {
        log("failed to set signal");
    }

    system("clear");
    init_scr_wh();
    wrap_get_term_attr(&raw_termio);
    hide_cursor();

    flip_screen();

    start_message_monitor();
    start_ui();

    resume_and_exit(0);
    return 0;
}
