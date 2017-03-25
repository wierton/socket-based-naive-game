#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#include "common.h"

#define PORT 50000

pthread_mutex_t sessions_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t battles_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t battles_users_lock = PTHREAD_MUTEX_INITIALIZER;

/* unused  -->  not login  -->  login  <-->  battle
 *
 * unused  -->  not login  -->  unused  // login fail
 * */

void wrap_recv(int conn, client_message_t *pcm);
void wrap_send(int conn, server_message_t *psm);

void send_to_client(int conn, int reason);
void close_session(int conn, int reason);

struct session_t {
	char user_name[USERNAME_SIZE];
	int conn;
	int state;           // not login, login, battle
	uint32_t battle_id;
	client_message_t cm;
} sessions[USER_CNT];

struct battle_t {
	int is_alloced;
	size_t nr_users;
	struct {
		int is_used;
		uint32_t session_id;
		pos_t pos;
		pos_t last_pos;
	} users[USER_CNT];

	struct {
		int is_used;
		pos_t pos;
		pos_t last_pos;
	} bullets[MAX_ITEM];

} battles[USER_CNT / 2];

void user_join_battle(uint32_t battle_id, uint32_t session_id) {
	assert(battle_id < USER_CNT / 2 && session_id < USER_CNT);

	int user_index = -1;

	pthread_mutex_lock(&battles_users_lock);
	for(int i = 0; i < USER_CNT; i++) {
		if(!(battles[battle_id].users[i].is_used)) {
			user_index = i;
		}
	}
	pthread_mutex_unlock(&battles_users_lock);

	if(user_index == -1) {
		loge("check here, user join in battle which has been full\n");
	}else{
		log("user %s join in battle %d(%lu users)\n", sessions[session_id].user_name, battle_id, battles[battle_id].nr_users);
		int user_id = battles[battle_id].users[user_index].session_id;
		battles[battle_id].nr_users ++;
		battles[battle_id].users[user_index].is_used = true;
		battles[battle_id].users[user_index].session_id = session_id;
		sessions[user_id].state = USER_STATE_BATTLE;
	}
}

void user_quit_battle(uint32_t battle_id, uint32_t session_id) {
	assert(battle_id < USER_CNT / 2 && session_id < USER_CNT);

	int user_index = -1;

	for(int i = 0; i < USER_CNT; i++) {
		if(battles[battle_id].users[i].is_used
		&& battles[battle_id].users[i].session_id == session_id) {
			user_index = i;
		}
	}

	if(user_index == -1) {
		loge("check here, user %s quit battle %d which he didn't join in\n", sessions[session_id].user_name, battle_id);
	}else{
		log("user %s quit from battle %d(%lu users)\n", sessions[session_id].user_name, battle_id, battles[battle_id].nr_users);
		int user_id = battles[battle_id].users[user_index].session_id;
		battles[battle_id].nr_users --;
		battles[battle_id].users[user_index].is_used = false;
		sessions[user_id].state = USER_STATE_LOGIN;
	}

	if(battles[battle_id].nr_users <= 1) {
		// disband battle
		for(int i = 0; i < USER_CNT; i++) {
			if(battles[battle_id].users[i].is_used) {
				battles[battle_id].users[i].is_used = false;
				int user_id = battles[battle_id].users[i].session_id;
				log("force quit user %s from battle\n", sessions[user_id].user_name);
				int conn = sessions[user_id].conn;
				send_to_client(conn, SERVER_MESSAGE_BATTLE_DISBANDED);
			}
		}

		battles[battle_id].is_alloced = false;
	}
}

int find_session_id_by_user_name(const char *user_name) {
	int ret_session_id = -1;
	log("find user '%s'\n", user_name);
	for(int i = 0; i < USER_CNT; i++) {
		if(sessions[i].state == USER_STATE_LOGIN
		|| sessions[i].state == USER_STATE_BATTLE) {
			if(strncmp(user_name, sessions[i].user_name, USERNAME_SIZE - 1)) {
				ret_session_id = i;
				break;
			}
		}
	}

	if(ret_session_id == -1) {
		logi("fail\n");
	}else{
		logi("found: %d@%s\n", ret_session_id, sessions[ret_session_id].user_name);
	}

	return ret_session_id;
}

int get_unalloced_battle() {
	int ret_battle_id = -1;
	pthread_mutex_lock(&battles_lock);
	for(int i = 0; i < USER_CNT; i++) {
		if(battles[i].is_alloced == false) {
			memset(&battles[i], 0, sizeof(struct battle_t));
			battles[i].is_alloced = true;
			ret_battle_id = i;
		}
	}
	pthread_mutex_unlock(&battles_lock);
	if(ret_battle_id == -1) {
		loge("check here, returned battle id should not be -1\n");
	}else{
		log("alloc unalloced battle id #%d\n", ret_battle_id);
	}
	return ret_battle_id;
}

int get_unused_session() {
	int ret_session_id = -1;
	pthread_mutex_lock(&sessions_lock);
	for(int i = 0; i < USER_CNT; i++) {
		if(sessions[i].state == USER_STATE_UNUSED) {
			memset(&sessions[i], 0, sizeof(struct session_t));
			sessions[i].state = USER_STATE_NOT_LOGIN;
			ret_session_id = i;
		}
	}
	pthread_mutex_unlock(&sessions_lock);
	if(ret_session_id == -1) {
		log("fail to alloc session id\n");
	}else{
		log("alloc unused session id #%d\n", ret_session_id);
	}
	return ret_session_id;
}

int client_command_user_login(int session_id) {
	int ret_code = 0;
	int conn = sessions[session_id].conn;
	client_message_t *pcm = &sessions[session_id].cm;
	char *user_name = pcm->user_name;
	log("user '%s' login\n", user_name);
	for(int i = 0; i < USER_CNT; i++) {
		if(sessions[i].state == USER_STATE_LOGIN
		|| sessions[i].state == USER_STATE_BATTLE) {
			if(strncmp(user_name, sessions[i].user_name, USERNAME_SIZE) == 0) {
				log("user %d@%s duplicate with %dth user '%s'\n", session_id, user_name, i, sessions[i].user_name);
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
		strncpy(sessions[session_id].user_name, user_name, USERNAME_SIZE - 1);
	}

	return ret_code;
}

int client_command_fetch_all_users(int session_id) {
	int conn = sessions[session_id].conn;
	char *user_name = sessions[session_id].user_name;
	log("user '%s' trys to fetch all users' info\n", user_name);
	server_message_t sm;
	memset(&sm, 0, sizeof(server_message_t));
	sm.response = SERVER_RESPONSE_ALL_USERS_INFO;
	for(int i = 0; i < USER_CNT; i++) {
		if(sessions[i].state == USER_STATE_LOGIN
		|| sessions[i].state == USER_STATE_BATTLE) {
			logi("found '%s' %s\n", sessions[i].user_name,
					sessions[i].state == USER_STATE_BATTLE ? "in battle" : "");
			sm.all_users[i].user_state = sessions[i].state;
			strncpy(sm.all_users[i].user_name, sessions[i].user_name, USERNAME_SIZE - 1);
		}
	}

	wrap_send(conn, &sm);

	return 0;
}

int invite_friend_to_battle(int battle_id, int user_id, int friend_id) {
	int conn = sessions[user_id].conn;
	char *friend_name = sessions[friend_id].user_name;
	if(friend_id == -1) {
		logi("friend '%s' hasn't login\n", friend_name);
		send_to_client(conn, SERVER_RESPONSE_BATTLE_FRIEND_NOT_LOGIN);
		user_quit_battle(battle_id, friend_id);
	}else if(sessions[friend_id].state != USER_STATE_LOGIN) {
		logi("friend '%s' already in battle\n", friend_name);
		send_to_client(conn, SERVER_RESPONSE_BATTLE_FRIEND_ALREADY_IN_BATTLE);
		user_quit_battle(battle_id, friend_id);
	}else{
		logi("friend '%s' found\n", friend_name);

		user_join_battle(battle_id, user_id);
		user_join_battle(battle_id, friend_id);

		server_message_t sm;
		memset(&sm, 0, sizeof(server_message_t));
		sm.response = SERVER_MESSAGE_INVITE_TO_BATTLE;
		strncpy(sm.friend_name, sessions[user_id].user_name, USERNAME_SIZE - 1);
		wrap_send(sessions[friend_id].conn, &sm);
	}

	return 0;
}

int client_command_launch_battle(int session_id) {
	int conn = sessions[session_id].conn;

	if(sessions[session_id].state == USER_STATE_BATTLE) {
		log("user '%s' who try to launch battle has been in battle\n", sessions[session_id].user_name);
		send_to_client(conn, SERVER_RESPONSE_YOURE_ALREADY_IN_BATTLE);
		return 0;
	}

	int battle_id = get_unalloced_battle();
	client_message_t *pcm = &sessions[session_id].cm;
	int friend_id = find_session_id_by_user_name(pcm->user_name);
	log("%s launch battle with %s\n", sessions[session_id].user_name, pcm->user_name);

	if(battle_id == -1) {
		loge("fail to create battle for %s and %s\n", sessions[session_id].user_name, pcm->user_name);
		send_to_client(conn, SERVER_RESPONSE_FAIL_TO_CREATE_BATTLE);
		return 0;
	}else{
		logi("launch battle %d for %s and %s\n", battle_id, sessions[session_id].user_name, pcm->user_name);
		sessions[session_id].battle_id = battle_id;
	}

	invite_friend_to_battle(battle_id, session_id, friend_id);

	return 0;
}

int client_command_quit_battle(int session_id) {
	if(sessions[session_id].state != USER_STATE_BATTLE) {
		send_to_client(sessions[session_id].conn, SERVER_RESPONSE_YOURE_NOT_IN_BATTLE);
	}else{
		user_quit_battle(sessions[session_id].battle_id, session_id);
	}
	return 0;
}

int client_command_invite_user(int session_id) {
	client_message_t *pcm = &sessions[session_id].cm;
	int battle_id = sessions[session_id].battle_id;
	int friend_id = find_session_id_by_user_name(pcm->user_name);

	if(sessions[session_id].state != USER_STATE_BATTLE) {
		log("user %s who invites friend %s wasn't in battle\n", sessions[session_id].user_name, pcm->user_name);
		send_to_client(sessions[session_id].conn, SERVER_RESPONSE_YOURE_NOT_IN_BATTLE);
	}else{
		logi("invite user %s to battle #%d\n", sessions[friend_id].user_name, battle_id);
		sessions[friend_id].battle_id = battle_id;
		invite_friend_to_battle(battle_id, session_id, friend_id);
	}
	return 0;
}

int client_command_accept_battle(int session_id) {
	return 0;
}

int client_command_reject_battle(int session_id) {
	return 0;
}

int client_command_logout(int session_id) {
	int conn = sessions[session_id].conn;
	log("user %d@%s logout\n", session_id, sessions[session_id].user_name);
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

void *run_battle(void *args) {
	// TODO:
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
	srand(time(NULL));

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

	pthread_mutex_destroy(&sessions_lock);
	pthread_mutex_destroy(&battles_lock);
	pthread_mutex_destroy(&battles_users_lock);

	return 0;
}
