#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>

#include "common.h"

#define PORT 50000

pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER;

/* unused  -->  not login  -->  login  <-->  battle
 *
 * unused  -->  not login  -->  unused  // login fail
 * */

void wrap_recv(int conn, client_message_t *pcm);
void wrap_send(int conn, server_message_t *psm);

void send_to_client(int conn, int reason);
void close_session(int conn, int reason);

struct session_t {
	char user_id[USERID_SZ];
	int conn;
	int state;           // not login, login, battle
	client_message_t cm;
	pos_t last_pos;
	pos_t user_pos;      // make sense in battle
} sessions[USER_CNT];

int get_unused_session() {
	int ret_session_id = -1;
	pthread_mutex_lock(&mlock);
	for(int i = 0; i < USER_CNT; i++) {
		if(sessions[i].state == USER_STATE_UNUSED) {
			memset(&sessions[i], 0, sizeof(struct session_t));
			sessions[i].state = USER_STATE_NOT_LOGIN;
			ret_session_id = i;
		}
	}
	pthread_mutex_unlock(&mlock);
	return ret_session_id;
}

int client_command_user_login(int session_id) {
	int ret_code = 0;
	int conn = sessions[session_id].conn;
	client_message_t *pcm = &sessions[session_id].cm;
	char *user_id = pcm->user_id;
	log("user '%s' login\n", user_id);
	for(int i = 0; i < USER_CNT; i++) {
		if(sessions[i].state == USER_STATE_LOGIN
		|| sessions[i].state == USER_STATE_BATTLE) {
			if(strncmp(user_id, sessions[i].user_id, USERID_SZ) == 0) {
				log("user %d@%s duplicate with %dth user '%s'\n", session_id, user_id, i, sessions[i].user_id);
				ret_code = -1;
				break;
			}
		}
	}

	// no duplicate user ids found
	if(ret_code == -1) {
		close_session(conn, SERVER_RESPONSE_LOGIN_FAIL_DUP_USERID);
	}else{
		send_to_client(conn, SERVER_RESPONSE_LOGIN_SUCCESS);
		strncpy(sessions[session_id].user_id, user_id, USERID_SZ - 1);
	}

	return ret_code;
}

int client_command_fetch_all_users(int session_id) {
	int conn = sessions[session_id].conn;
	char *user_id = sessions[session_id].user_id;
	log("user '%s' trys to fetch all users' info\n", user_id);
	server_message_t sm;
	memset(&sm, 0, sizeof(server_message_t));
	sm.response = SERVER_RESPONSE_ALL_USERS_INFO;
	for(int i = 0; i < USER_CNT; i++) {
		if(sessions[i].state == USER_STATE_LOGIN
		|| sessions[i].state == USER_STATE_BATTLE) {
			log("\t==> found '%s' %s\n", sessions[i].user_id,
					sessions[i].state == USER_STATE_BATTLE ? "in battle" : "");
			sm.all_users[i].user_state = sessions[i].state;
			strncpy(sm.all_users[i].user_id, sessions[i].user_id, USERID_SZ - 1);
		}
	}

	wrap_send(conn, &sm);

	return 0;
}

int client_command_launch_battle(int session_id) {
	return 0;
}

int client_command_quit_battle(int session_id) {
	return 0;
}

int client_command_invite_user(int session_id) {
	return 0;
}

int client_command_logout(int session_id) {
	int conn = sessions[session_id].conn;
	log("user %d@%s logout\n", session_id, sessions[session_id].user_id);
	sessions[session_id].state = USER_STATE_UNUSED;
	close(conn);
	return -1;
}

int client_command_move_up(int session_id) {
	return 0;
}

int client_command_move_down(int session_id) {
	return 0;
}

int client_command_move_left(int session_id) {
	return 0;
}

int client_command_move_right(int session_id) {
	return 0;
}

int client_command_fire(int session_id) {
	return 0;
}

static int(*handler[])(int) = {
	[CLIENT_COMMAND_USER_LOGIN] = client_command_user_login,
	[CLIENT_COMMAND_FETCH_ALL_USERS] = client_command_fetch_all_users,
	[CLIENT_COMMAND_LAUNCH_BATTLE] = client_command_launch_battle,
	[CLIENT_COMMAND_QUIT_BATTLE] = client_command_quit_battle,
	[CLIENT_COMMAND_INVITE_USER] = client_command_invite_user,
	[CLIENT_COMMAND_LOGOUT] = client_command_logout,
	[CLIENT_COMMAND_MOVE_UP] = client_command_move_up,
	[CLIENT_COMMAND_MOVE_DOWN] = client_command_move_down,
	[CLIENT_COMMAND_MOVE_LEFT] = client_command_move_left,
	[CLIENT_COMMAND_MOVE_RIGHT] = client_command_move_right,
	[CLIENT_COMMAND_FIRE] = client_command_fire,
};

void wrap_recv(int conn, client_message_t *pcm) {
	size_t total_len = 0;
	while(total_len < sizeof(client_message_t)) {
		size_t len = recv(conn, pcm, sizeof(client_message_t) - total_len, 0);
		if(len < 0) {
			loge("broken pipe\n");
		}

		total_len += len;
	}
}

void wrap_send(int conn, server_message_t *psm) {
	size_t total_len = 0;
	while(total_len < sizeof(server_message_t)) {
		size_t len = send(conn, psm, sizeof(server_message_t) - total_len, 0);
		if(len < 0) {
			loge("broken pipe\n");
		}

		total_len += len;
	}
}

void send_to_client(int conn, int reason) {
	server_message_t sm;
	memset(&sm, 0, sizeof(server_message_t));
	sm.response = reason;
	wrap_send(conn, &sm);
}

void close_session(int conn, int reason) {
	send_to_client(conn, reason);
	close(conn);
}

void *session_start(void *args) {
	int session_id = -1;
	int conn = (int)(uintptr_t)args;
	client_message_t *pcm = NULL;
	if((session_id = get_unused_session()) < 0) {
		close_session(conn, SERVER_RESPONSE_LOGIN_FAIL_SERVER_LIMITS);
		return NULL;
	}else{
		sessions[session_id].conn = conn;
		pcm = &sessions[session_id].cm;
		memset(pcm, 0, sizeof(client_message_t));
	}

	while(1) {
		wrap_recv(conn, pcm);
		if(pcm->command >= CLIENT_COMMAND_END)
			continue;

		int ret_code = handler[pcm->command](session_id);
		if(ret_code < 0)
			break;
	}
	return NULL;
}

int server_start() {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if(sockfd < 0) {
		eprintf("Create Socket Failed!\n");
	}

	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
		eprintf("Can not bind to port %d!\n", PORT);
	}

	if(listen(sockfd, USER_CNT) == -1) {
		eprintf("fail to listen on socket.\n");
	}

	return sockfd;
}

int main() {
	pthread_t thread;

	int sockfd = server_start();

	struct sockaddr_in client_addr;
	socklen_t length = sizeof(client_addr);
	while(1) {
		int conn = accept(sockfd, (struct sockaddr*)&client_addr, &length);
		log("connected by %s:%d\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port);
		if(conn < 0) {
			loge("fail to accept client.\n");
		}else if(pthread_create(&thread, NULL, session_start, (void *)(uintptr_t)conn) != 0) {
			loge("fail to create thread.\n");
		}
	}

	pthread_mutex_destroy(&mlock);

	return 0;
}
