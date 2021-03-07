#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include "common.h"

#define REGISTERED_USER_LIST_SIZE 100

#define REGISTERED_USER_FILE "userlists.log"

pthread_mutex_t userlist_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sessions_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t battles_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t items_lock[USER_CNT];

int server_fd = 0, port = 50000, port_range = 100;

int usleep(int usec);

void wrap_recv(int conn, client_message_t *pcm);
void wrap_send(int conn, server_message_t *psm);

void send_to_client(int uid, int message);
void send_to_client_with_username(int uid, int message, char *user_name);
void close_session(int conn, int message);

void terminate_process(int recved_signal);

static int user_list_size = 0;

struct {
    char user_name[USERNAME_SIZE];
    char password[PASSWORD_SIZE];
} registered_user_list[REGISTERED_USER_LIST_SIZE];

struct session_t {
    char user_name[USERNAME_SIZE];
    int conn;
    int state;           // not login, login, battle
    uint32_t bid;
    uint32_t inviter_id;
    client_message_t cm;
} sessions[USER_CNT];

struct battle_t {
    int is_alloced;
    size_t nr_users;
    struct {
        int battle_state;
        int nr_bullets;
        int dir;
        int life;
        pos_t pos;
        pos_t last_pos;
    } users[USER_CNT];

    int num_of_other; // number of other alloced item except for bullet
    int item_count;

    struct {
        int is_used;
        int dir;
        union {
            int owner;
            int times;
        };
        int kind;
        pos_t pos;
    } items[MAX_ITEM];

} battles[USER_CNT];

void load_user_list() {
    FILE *userlist = fopen(REGISTERED_USER_FILE, "r");
    if (userlist == NULL) {
        log("can not find " REGISTERED_USER_FILE "\n");
        return;
    }
    #define LOAD_FAIL \
    log("failed to load users, try to delete " REGISTERED_USER_FILE ".\n"), \
    user_list_size = 0, memset(registered_user_list, 0, sizeof(registered_user_list)),\
    fclose(userlist);
    for (int i = 0; i < REGISTERED_USER_LIST_SIZE; i++) {
        if (fgets(registered_user_list[i].user_name, USERNAME_SIZE, userlist) != NULL) {
            registered_user_list[i].user_name[strlen(registered_user_list[i].user_name) - 1] = 0;
            for (int j = 0; j < i; j++) {
                if (strncmp(registered_user_list[i].user_name, registered_user_list[j].user_name, USERNAME_SIZE - 1) != 0)
                    continue;
                LOAD_FAIL;
                return;
            }
            user_list_size++;
        } else {
            break;
        }
        if (fgets(registered_user_list[i].password, PASSWORD_SIZE, userlist) == NULL) { LOAD_FAIL; return; }
        registered_user_list[i].password[strlen(registered_user_list[i].password) - 1] = 0;
    }
    #undef LOAD_FAIL
    for (int i = 0; i < user_list_size; i++) {
        log("loaded user `%s`\n", registered_user_list[i].user_name);
    }
    log("loaded %d users from " REGISTERED_USER_FILE ".\n", user_list_size);
    fclose(userlist);
}
void save_user_list() {
    FILE *userlist = fopen(REGISTERED_USER_FILE, "w");
    for (int i = 0; i < user_list_size; i++) {
        fprintf(userlist, "%s\n", registered_user_list[i].user_name);
        fprintf(userlist, "%s\n", registered_user_list[i].password);
    }
    log("saved %d users to " REGISTERED_USER_FILE ".\n", user_list_size);
}

void save_user(int i) {
    FILE *userlist = fopen(REGISTERED_USER_FILE, "a");
    fprintf(userlist, "%s\n", registered_user_list[i].user_name);
    fprintf(userlist, "%s\n", registered_user_list[i].password);
    log("saved users `%s` to " REGISTERED_USER_FILE ".\n", registered_user_list[i].user_name);
}

int query_session_built(uint32_t uid) {
    assert(uid < USER_CNT);

    if (sessions[uid].state == USER_STATE_UNUSED
    || sessions[uid].state == USER_STATE_NOT_LOGIN) {
        return false;
    } else {
        return true;
    }
}

void user_quit_battle(uint32_t bid, uint32_t uid) {
    assert(bid < USER_CNT && uid < USER_CNT);

    log("user %s quit from battle %d(%ld users)\n", sessions[uid].user_name, bid, battles[bid].nr_users);
    battles[bid].nr_users --;
    battles[bid].users[uid].battle_state = BATTLE_STATE_UNJOINED;
    sessions[uid].state = USER_STATE_LOGIN;

    if (battles[bid].nr_users == 0) {
        // disband battle
        log("disband battle %d\n", bid);
        battles[bid].is_alloced = false;
    } else {
        server_message_t sm;
        sm.message = SERVER_MESSAGE_USER_QUIT_BATTLE;
        strncpy(sm.friend_name, sessions[uid].user_name, USERNAME_SIZE - 1);

        for (int i = 0; i < USER_CNT; i++) {
            if (battles[bid].users[i].battle_state != BATTLE_STATE_UNJOINED) {

                wrap_send(sessions[i].conn, &sm);
            }
        }
    }
}

void user_join_battle_common_part(uint32_t bid, uint32_t uid, uint32_t joined_state) {
    assert(bid < USER_CNT && uid < USER_CNT);

    log("user %s join in battle %d(%ld users)\n", sessions[uid].user_name, bid, battles[bid].nr_users);

    if (joined_state == USER_STATE_BATTLE) {
        battles[bid].nr_users ++;
        battles[bid].users[uid].battle_state = BATTLE_STATE_LIVE;
    } else if (joined_state == USER_STATE_WAIT_TO_BATTLE) {
        battles[bid].users[uid].battle_state = BATTLE_STATE_UNJOINED;
    } else {
        loge("check here, other joined_state:%d\n", joined_state);
    }

    battles[bid].users[uid].life = INIT_LIFE;
    battles[bid].users[uid].nr_bullets = INIT_BULLETS;

    sessions[uid].state = joined_state;
    sessions[uid].bid = bid;
}

void user_join_battle(uint32_t bid, uint32_t uid) {
    int ux = (rand() & 0x7FFF) % BATTLE_W;
    int uy = (rand() & 0x7FFF) % BATTLE_H;
    battles[bid].users[uid].pos.x = ux;
    battles[bid].users[uid].pos.y = uy;
    log("alloc position (%hhu, %hhu) for launcher #%d@%s\n",
            ux, uy, uid, sessions[uid].user_name);

    sessions[uid].state = USER_STATE_BATTLE;

    if (battles[bid].users[uid].battle_state == BATTLE_STATE_UNJOINED) {
        user_join_battle_common_part(bid, uid, USER_STATE_BATTLE);
    }
}

void user_invited_to_join_battle(uint32_t bid, uint32_t uid) {
    if (sessions[uid].state == USER_STATE_WAIT_TO_BATTLE
    && bid != sessions[uid].bid) {
        log("user %d@%s rejects old battle #%d since he was invited to a new battle\n", uid, sessions[uid].user_name, sessions[uid].bid);

        send_to_client_with_username(sessions[uid].inviter_id, SERVER_MESSAGE_FRIEND_REJECT_BATTLE, sessions[uid].user_name);
    }

    user_join_battle_common_part(bid, uid, USER_STATE_WAIT_TO_BATTLE);
}

int find_uid_by_user_name(const char *user_name) {
    int ret_uid = -1;
    log("find user '%s'\n", user_name);
    for (int i = 0; i < USER_CNT; i++) {
        if (query_session_built(i)) {
            if (strncmp(user_name, sessions[i].user_name, USERNAME_SIZE - 1) == 0) {
                ret_uid = i;
                break;
            }
        }
    }

    if (ret_uid == -1) {
        logi("fail\n");
    } else {
        logi("found: %d@%s\n", ret_uid, sessions[ret_uid].user_name);
    }

    return ret_uid;
}

int get_unalloced_battle() {
    int ret_bid = -1;
    pthread_mutex_lock(&battles_lock);
    for (int i = 0; i < USER_CNT; i++) {
        if (battles[i].is_alloced == false) {
            memset(&battles[i], 0, sizeof(struct battle_t));
            battles[i].is_alloced = true;
            ret_bid = i;
            break;
        }
    }
    pthread_mutex_unlock(&battles_lock);
    if (ret_bid == -1) {
        loge("check here, returned battle id should not be -1\n");
    } else {
        log("alloc unalloced battle id #%d\n", ret_bid);
    }
    return ret_bid;
}

int get_unused_session() {
    int ret_uid = -1;
    pthread_mutex_lock(&sessions_lock);
    for (int i = 0; i < USER_CNT; i++) {
        if (sessions[i].state == USER_STATE_UNUSED) {
            memset(&sessions[i], 0, sizeof(struct session_t));
            sessions[i].conn = -1;
            sessions[i].state = USER_STATE_NOT_LOGIN;
            ret_uid = i;
            break;
        }
    }
    pthread_mutex_unlock(&sessions_lock);
    if (ret_uid == -1) {
        log("fail to alloc session id\n");
    } else {
        log("alloc unused session id #%d\n", ret_uid);
    }
    return ret_uid;
}

void inform_friends(int uid, int message) {
    server_message_t sm;
    char *user_name = sessions[uid].user_name;
    memset(&sm, 0, sizeof(server_message_t));
    sm.message = message;
    for (int i = 0; i < USER_CNT; i++) {
        if (i == uid || !query_session_built(i))
            continue;
        strncpy(sm.friend_name, user_name, USERNAME_SIZE - 1);
        wrap_send(sessions[i].conn, &sm);
    }
}

int get_unused_item(int bid) {
    int ret_item_id = -1;
    pthread_mutex_lock(&items_lock[bid]);
    for (int i = 0; i < MAX_ITEM; i++) {
        if (!(battles[bid].items[i].is_used)) {
            ret_item_id = i;
            battles[bid].items[i].is_used = true;
            break;
        }
    }
    pthread_mutex_unlock(&items_lock[bid]);
    return ret_item_id;
}

void random_generate_items(int bid) {
    int random_kind, item_id;
    if (battles[bid].item_count <= 7) {
        random_kind = 3, item_id = get_unused_item(bid);
        if (item_id == -1) return;
    } else {
        if (rand() % 200 > 2) return;
        if (battles[bid].num_of_other >= MAX_OTHER) return;
        item_id = get_unused_item(bid);
        if (item_id == -1) return;
        random_kind = rand() % (ITEM_END - 1) + 1;
        if (rand() % 3 != 0 && random_kind == 2) random_kind = 1;
        //if (rand() % 2 != 0 && random_kind == 2) random_kind = 1;
    }
    battles[bid].item_count++;
    battles[bid].items[item_id].kind = random_kind;
    battles[bid].items[item_id].pos.x = (rand() & 0x7FFF) % BATTLE_W;
    battles[bid].items[item_id].pos.y = (rand() & 0x7FFF) % BATTLE_H;
    log("new item: #%dk%d(%d,%d)\n", item_id,
            battles[bid].items[item_id].kind,
            battles[bid].items[item_id].pos.x,
            battles[bid].items[item_id].pos.y);
    if (random_kind == ITEM_MAGMA) {
        battles[bid].items[item_id].times = MAGMA_INIT_TIMES;
    }
}

void move_bullets(int bid) {
    for (int i = 0; i < MAX_ITEM; i++) {
        if (battles[bid].items[i].is_used == false
        || battles[bid].items[i].kind != ITEM_BULLET)
            continue;
        uint8_t *px = &(battles[bid].items[i].pos.x);
        uint8_t *py = &(battles[bid].items[i].pos.y);

        // log("try to move bullet %d with dir %d\n", i, battles[bid].items[i].dir);

        switch(battles[bid].items[i].dir) {
            case DIR_UP:    (*py)--;break;
            case DIR_DOWN:    (*py)++;break;
            case DIR_LEFT:    (*px)--;break;
            case DIR_RIGHT:    (*px)++;break;
        }

        if (*px >= BATTLE_W || *py >= BATTLE_H) {
            log("free bullet #%d\n", i);
            battles[bid].items[i].is_used = false;
        }
    }
}

void check_who_get_blood_vial(int bid) {
    for (int i = 0; i < MAX_ITEM; i++) {
        if (battles[bid].items[i].is_used == false
        || battles[bid].items[i].kind != ITEM_BLOOD_VIAL)
            continue;

        int ix = battles[bid].items[i].pos.x;
        int iy = battles[bid].items[i].pos.y;

        for (int j = 0; j < USER_CNT; j++) {
            if (battles[bid].users[j].battle_state != BATTLE_STATE_LIVE)
                continue;
            int ux = battles[bid].users[j].pos.x;
            int uy = battles[bid].users[j].pos.y;

            if (ix == ux && iy == uy) {
                battles[bid].users[j].life += LIFE_PER_VIAL;
                log("user %d@%s got blood vial\n", j, sessions[j].user_name);
                if (battles[bid].users[j].life > MAX_LIFE) {
                    log("user %d@%s life exceeds max value\n", j, sessions[j].user_name);
                    battles[bid].users[j].life = MAX_LIFE;
                }

                battles[bid].items[i].is_used = false;
                battles[bid].num_of_other --;
                send_to_client(j, SERVER_MESSAGE_YOU_GOT_BLOOD_VIAL);
                break;
            }
        }
    }
}

void check_who_traped_in_magma(int bid) {
    for (int i = 0; i < MAX_ITEM; i++) {
        if (battles[bid].items[i].is_used == false
        || battles[bid].items[i].kind != ITEM_MAGMA)
            continue;

        int ix = battles[bid].items[i].pos.x;
        int iy = battles[bid].items[i].pos.y;

        for (int j = 0; j < USER_CNT; j++) {
            if (battles[bid].users[j].battle_state != BATTLE_STATE_LIVE)
                continue;
            int ux = battles[bid].users[j].pos.x;
            int uy = battles[bid].users[j].pos.y;

            if (ix == ux && iy == uy) {
                battles[bid].users[j].life --;
                battles[bid].items[i].times --;
                log("user %d@%s is trapped in magma\n", j, sessions[j].user_name);
                send_to_client(j, SERVER_MESSAGE_YOU_ARE_TRAPPED_IN_MAGMA);
                if (battles[bid].items[i].times <= 0) {
                    log("magma %d is exhausted\n", i);
                    battles[bid].items[i].is_used = false;
                    battles[bid].num_of_other --;
                }
                break;
            }
        }
    }
}

void check_who_got_charger(int bid) {
    for (int i = 0; i < MAX_ITEM; i++) {
        if (battles[bid].items[i].is_used == false
        || battles[bid].items[i].kind != ITEM_MAGAZINE)
            continue;

        int ix = battles[bid].items[i].pos.x;
        int iy = battles[bid].items[i].pos.y;

        for (int j = 0; j < USER_CNT; j++) {
            if (battles[bid].users[j].battle_state != BATTLE_STATE_LIVE)
                continue;
            int ux = battles[bid].users[j].pos.x;
            int uy = battles[bid].users[j].pos.y;

            if (ix == ux && iy == uy) {
                battles[bid].users[j].nr_bullets += BULLETS_PER_MAGAZINE;
                log("user %d@%s is got magazine\n", j, sessions[j].user_name);
                if (battles[bid].users[j].nr_bullets > MAX_BULLETS) {
                    log("user %d@%s's bullets exceeds max value\n", j, sessions[j].user_name);
                    battles[bid].users[j].nr_bullets = MAX_BULLETS;
                }

                send_to_client(j, SERVER_MESSAGE_YOU_GOT_MAGAZINE);
                battles[bid].items[i].is_used = false;
                break;
            }
        }
    }
}

void check_who_is_shooted(int bid) {
    for (int i = 0; i < MAX_ITEM; i++) {
        if (battles[bid].items[i].is_used == false
        || battles[bid].items[i].kind != ITEM_BULLET)
            continue;

        int ix = battles[bid].items[i].pos.x;
        int iy = battles[bid].items[i].pos.y;

        for (int j = 0; j < USER_CNT; j++) {
            if (battles[bid].users[j].battle_state != BATTLE_STATE_LIVE)
                continue;
            int ux = battles[bid].users[j].pos.x;
            int uy = battles[bid].users[j].pos.y;

            if (ix == ux && iy == uy
            && battles[bid].items[i].owner != j) {
                battles[bid].users[j].life --;
                log("user %d@%s is shooted\n", j, sessions[j].user_name);
                send_to_client(j, SERVER_MESSAGE_YOU_ARE_SHOOTED);
                battles[bid].items[i].is_used = false;
                break;
            }
        }
    }
}

void check_who_is_dead(int bid) {
    for (int i = 0; i < USER_CNT; i++) {
        if (battles[bid].users[i].battle_state == BATTLE_STATE_LIVE
        && battles[bid].users[i].life <= 0) {
            log("user %d@%s is dead\n", i, sessions[i].user_name);
            battles[bid].users[i].battle_state = BATTLE_STATE_DEAD;
            log("send dead info to user %d@%s\n", i, sessions[i].user_name);
            send_to_client(i, SERVER_MESSAGE_YOU_ARE_DEAD);
        } else if (battles[bid].users[i].battle_state == BATTLE_STATE_DEAD){
            battles[bid].users[i].battle_state = BATTLE_STATE_WITNESS;
        }
    }
}

void inform_all_user_battle_state(int bid) {
    server_message_t sm;
    sm.message = SERVER_MESSAGE_BATTLE_INFORMATION;
    for (int i = 0; i < USER_CNT; i++) {
        if (battles[bid].users[i].battle_state == BATTLE_STATE_LIVE) {
            sm.user_pos[i].x = battles[bid].users[i].pos.x;
            sm.user_pos[i].y = battles[bid].users[i].pos.y;
        } else {
            sm.user_pos[i].x = -1;
            sm.user_pos[i].y = -1;
        }
    }

    for (int i = 0; i < MAX_ITEM; i++) {
        if (battles[bid].items[i].is_used) {
            sm.item_kind[i] = battles[bid].items[i].kind;
            sm.item_pos[i].x = battles[bid].items[i].pos.x;
            sm.item_pos[i].y = battles[bid].items[i].pos.y;
        } else {
            sm.item_kind[i] = ITEM_NONE;
        }
    }

    for (int i = 0; i < USER_CNT; i++) {
        sm.index = i;
        sm.life = battles[bid].users[i].life;
        sm.bullets_num = battles[bid].users[i].nr_bullets;
        if (battles[bid].users[i].battle_state != BATTLE_STATE_UNJOINED) {
            wrap_send(sessions[i].conn, &sm);
        }
    }
}

void *battle_ruler(void *args) {
    int bid = (int)(uintptr_t)args;
    log("battle ruler for battle #%d\n", bid);
    // FIXME: battle re-alloced before exiting loop 
    while (battles[bid].is_alloced) {
        move_bullets(bid);
        check_who_is_shooted(bid);
        check_who_is_dead(bid);
        usleep(10000);
        move_bullets(bid);
        check_who_is_shooted(bid);
        check_who_is_dead(bid);
        usleep(10000);
        move_bullets(bid);
        check_who_is_shooted(bid);
        check_who_is_dead(bid);
        usleep(10000);
        move_bullets(bid);
        check_who_is_shooted(bid);
        check_who_is_dead(bid);
        usleep(10000);

        move_bullets(bid);
        check_who_get_blood_vial(bid);
        check_who_traped_in_magma(bid);
        check_who_got_charger(bid);
        check_who_is_shooted(bid);
        check_who_is_dead(bid);
        
        random_generate_items(bid);

        inform_all_user_battle_state(bid);
        usleep(10000);
    }
    return NULL;
}

int check_user_registered(char *user_name, char *password) {
    for (int i = 0; i < REGISTERED_USER_LIST_SIZE; i++) {
        if (strncmp(user_name, registered_user_list[i].user_name, USERNAME_SIZE - 1) != 0)
            continue;

        if (strncmp(password, registered_user_list[i].password, PASSWORD_SIZE - 1) != 0) {
            logi("user name '%s' sent error password\n", user_name);
            return SERVER_RESPONSE_LOGIN_FAIL_ERROR_PASSWORD;
        } else {
            return SERVER_RESPONSE_LOGIN_SUCCESS;
        }
    }

    logi("user name '%s' hasn't been registered\n", user_name);
    return SERVER_RESPONSE_LOGIN_FAIL_UNREGISTERED_USERID;
}

void launch_battle(int bid) {
    pthread_t thread;

    log("try to create battle_ruler thread\n");
    if (pthread_create(&thread, NULL, battle_ruler, (void *)(uintptr_t)bid) == -1) {
        eprintf("fail to launch battle\n");
    }
}

int client_command_user_register(int uid) {
    int ul_index = -1;
    char *user_name = sessions[uid].cm.user_name;
    char *password = sessions[uid].cm.password;
    log("user '%s' tries to register with password '%s'\n", user_name, password);

    for (int i = 0; i < REGISTERED_USER_LIST_SIZE; i++) {
        if (strncmp(user_name, registered_user_list[i].user_name, USERNAME_SIZE - 1) != 0)
            continue;

        log("user '%s'&'%s' has been registered\n", user_name, password);
        send_to_client(uid, SERVER_RESPONSE_YOU_HAVE_REGISTERED);
        return 0;
    }

    pthread_mutex_lock(&userlist_lock);
    if (user_list_size < REGISTERED_USER_LIST_SIZE)
        ul_index = user_list_size ++;
    pthread_mutex_unlock(&userlist_lock);

    log("fetch empty user list index #%d\n", ul_index);
    if (ul_index == -1) {
        log("user '%s' registers fail\n", user_name);
        send_to_client(uid, SERVER_RESPONSE_REGISTER_FAIL);
    } else {
        log("user '%s' registers success\n", user_name);
        strncpy(registered_user_list[ul_index].user_name,
                user_name, USERNAME_SIZE - 1);
        strncpy(registered_user_list[ul_index].password,
                password, PASSWORD_SIZE - 1);
        send_to_client(uid, SERVER_RESPONSE_REGISTER_SUCCESS);
        save_user(ul_index);
    }
    return 0;
}

int client_command_user_login(int uid) {
    int is_dup = 0;
    client_message_t *pcm = &sessions[uid].cm;
    char *user_name = pcm->user_name;
    char *password = pcm->password;
    log("user '%s' try to login\n", user_name);
    int message = check_user_registered(user_name, password);

    if (query_session_built(uid)) {
        log("user '%s' has logined\n", sessions[uid].user_name);
        send_to_client(uid, SERVER_RESPONSE_YOU_HAVE_LOGINED);
        return 0;
    }


    for (int i = 0; i < USER_CNT; i++) {
        if (query_session_built(i)) {
            logi("check dup user id: '%s' vs. '%s'\n", user_name, sessions[i].user_name);
            if (strncmp(user_name, sessions[i].user_name, USERNAME_SIZE - 1) == 0) {
                log("user %d@%s duplicate with %dth user '%s'\n", uid, user_name, i, sessions[i].user_name);
                is_dup = 1;
                break;
            }
        }
    }

    // no duplicate user ids found
    if (is_dup) {
        log("send fail dup id message to client.\n");
        send_to_client(uid, SERVER_RESPONSE_LOGIN_FAIL_DUP_USERID);
        sessions[uid].state = USER_STATE_NOT_LOGIN;
    } else if (message == SERVER_RESPONSE_LOGIN_SUCCESS){
        log("user '%s' login success\n", user_name);
        sessions[uid].state = USER_STATE_LOGIN;
        send_to_client(uid, SERVER_RESPONSE_LOGIN_SUCCESS);
        strncpy(sessions[uid].user_name, user_name, USERNAME_SIZE - 1);
        inform_friends(uid, SERVER_MESSAGE_FRIEND_LOGIN);
    } else {
        send_to_client(uid, message);
    }

    return 0;
}

int client_command_user_logout(int uid) {
    if (sessions[uid].state == USER_STATE_BATTLE
    || sessions[uid].state == USER_STATE_WAIT_TO_BATTLE) {
        log("user %d@%s tries to logout was in battle\n", uid, sessions[uid].user_name);
        user_quit_battle(sessions[uid].bid, uid);
    }

    log("user %d@%s logout\n", uid, sessions[uid].user_name);
    sessions[uid].state = USER_STATE_NOT_LOGIN;
    inform_friends(uid, SERVER_MESSAGE_FRIEND_LOGOUT);
    return 0;
}

void list_all_users(server_message_t *psm) {
    for (int i = 0; i < USER_CNT; i++) {
        if (query_session_built(i)) {
            log("%s: found '%s' %s\n", __func__, sessions[i].user_name,
                    sessions[i].state == USER_STATE_BATTLE ? "in battle" : "");
            psm->all_users[i].user_state = sessions[i].state;
            strncpy(psm->all_users[i].user_name, sessions[i].user_name, USERNAME_SIZE - 1);
        }
    }
}

int client_command_fetch_all_users(int uid) {
    char *user_name = sessions[uid].user_name;
    log("user %d@'%s' tries to fetch all users' info\n", uid, user_name);

    if (!query_session_built(uid)) {
        logi("user %d@'%s' who tries to list users hasn't login\n", uid, user_name);
        send_to_client(uid, SERVER_RESPONSE_YOU_HAVE_NOT_LOGIN);
        return 0;
    }

    server_message_t sm;
    memset(&sm, 0, sizeof(server_message_t));
    list_all_users(&sm);
    sm.response = SERVER_RESPONSE_ALL_USERS_INFO;

    wrap_send(sessions[uid].conn, &sm);

    return 0;
}

int client_command_fetch_all_friends(int uid) {
    char *user_name = sessions[uid].user_name;
    log("user %d@'%s' tries to fetch all friends' info\n", uid, user_name);

    if (!query_session_built(uid)) {
        logi("user %d@'%s' who tries to list users hasn't login\n", uid, user_name);
        send_to_client(uid, SERVER_RESPONSE_YOU_HAVE_NOT_LOGIN);
        return 0;
    }

    server_message_t sm;
    memset(&sm, 0, sizeof(server_message_t));
    list_all_users(&sm);
    sm.all_users[uid].user_state = USER_STATE_UNUSED;
    sm.response = SERVER_RESPONSE_ALL_FRIENDS_INFO;

    wrap_send(sessions[uid].conn, &sm);

    return 0;
}


int invite_friend_to_battle(int bid, int uid, char *friend_name) {
    int friend_id = find_uid_by_user_name(friend_name);
    if (friend_id == -1) {
        // fail to find friend
        logi("friend '%s' hasn't login\n", friend_name);
        send_to_client(uid, SERVER_MESSAGE_FRIEND_NOT_LOGIN);
    } else if (friend_id == uid){
        logi("launch battle %d for %s\n", bid, sessions[uid].user_name);
        sessions[uid].inviter_id = uid;
        send_to_client(uid, SERVER_RESPONSE_INVITATION_SENT);
    } else if (sessions[friend_id].state == USER_STATE_BATTLE) {
        // friend already in battle
        logi("friend '%s' already in battle\n", friend_name);
        send_to_client(uid, SERVER_MESSAGE_FRIEND_ALREADY_IN_BATTLE);
    } else {
        // invite friend
        logi("friend %d@'%s' found\n", friend_id, friend_name);

        user_invited_to_join_battle(bid, friend_id);
        // WARNING: can't move this statement
        sessions[friend_id].inviter_id = uid;

        send_to_client_with_username(friend_id, SERVER_MESSAGE_INVITE_TO_BATTLE, sessions[uid].user_name);
    }

    return 0;
}

int client_command_launch_battle(int uid) {
    if (sessions[uid].state == USER_STATE_BATTLE) {
        log("user '%s' who tries to launch battle has been in battle\n", sessions[uid].user_name);
        send_to_client(uid, SERVER_RESPONSE_YOURE_ALREADY_IN_BATTLE);
        return 0;
    } else {
        log("user '%s' tries to launch battle\n", sessions[uid].user_name);
    }

    int bid = get_unalloced_battle();
    client_message_t *pcm = &sessions[uid].cm;

    log("%s launch battle with %s\n", sessions[uid].user_name, pcm->user_name);

    if (bid == -1) {
        loge("fail to create battle for %s and %s\n", sessions[uid].user_name, pcm->user_name);
        send_to_client(uid, SERVER_RESPONSE_LAUNCH_BATTLE_FAIL);
        return 0;
    } else {
        logi("launch battle %d for %s, invite %s\n", bid, sessions[uid].user_name, pcm->user_name);
        user_join_battle(bid, uid);
        invite_friend_to_battle(bid, uid, pcm->user_name);
        launch_battle(bid);
        send_to_client(uid, SERVER_RESPONSE_LAUNCH_BATTLE_SUCCESS);
    }

    return 0;
}

int client_command_quit_battle(int uid) {
    log("user %d@%s tries to quit battle\n", uid, sessions[uid].user_name);
    if (sessions[uid].state != USER_STATE_BATTLE) {
        logi("but he hasn't join battle\n");
        send_to_client(uid, SERVER_RESPONSE_YOURE_NOT_IN_BATTLE);
    } else {
        logi("call user_quit_battle to quit\n");
        user_quit_battle(sessions[uid].bid, uid);
    }
    return 0;
}

int client_command_invite_user(int uid) {
    client_message_t *pcm = &sessions[uid].cm;
    int bid = sessions[uid].bid;
    int friend_id = find_uid_by_user_name(pcm->user_name);
    log("user %d@%s tries to invite friend\n", uid, sessions[uid].user_name);

    if (sessions[uid].state != USER_STATE_BATTLE) {
        log("user %s who invites friend %s wasn't in battle\n", sessions[uid].user_name, pcm->user_name);
        send_to_client(uid, SERVER_RESPONSE_YOURE_NOT_IN_BATTLE);
    } else {
        logi("invite user %s to battle #%d\n", sessions[friend_id].user_name, bid);
        invite_friend_to_battle(bid, uid, pcm->user_name);
    }
    return 0;
}

int client_command_send_message(int uid) 
{
    client_message_t *pcm = &sessions[uid].cm;
    server_message_t sm;
    memset(&sm,0,sizeof(server_message_t));
    sm.message=SERVER_MESSAGE_FRIEND_MESSAGE;
    strncpy(sm.from_user,sessions[uid].user_name,USERNAME_SIZE);
    strncpy(sm.msg,pcm->message,MSG_SIZE);
    if (pcm->user_name[0]=='\0')
    {
        logi("user %d:%s yells at all users: %s\n", uid, sessions[uid].user_name, pcm->message);
        int i;
        for (i=0;i<USER_CNT;i++)
        {
            if (uid==i)continue;
            wrap_send(sessions[i].conn,&sm);
        }
    }
    else
    {
        int friend_id=find_uid_by_user_name(pcm->user_name);
        if (friend_id==-1||friend_id==uid)
        {
            logi("user %d:%s fails to speak to '%s':\"%s\"\n",uid,sessions[uid].user_name,pcm->user_name,pcm->message);
        }
        else
        {
            logi("uiser %d:%s speaks to %d:%s : \"%s\"\n",uid,sessions[uid].user_name,friend_id,pcm->user_name,pcm->message);
            wrap_send(sessions[friend_id].conn,&sm);
        }
    }
    return 0;
}

int client_command_accept_battle(int uid) {
    log("user %s accept battle #%d\n", sessions[uid].user_name, sessions[uid].bid);

    if (sessions[uid].state == USER_STATE_BATTLE) {
        logi("already in battle\n");
        send_to_client(uid, SERVER_RESPONSE_YOURE_ALREADY_IN_BATTLE);
    } else if (sessions[uid].state == USER_STATE_WAIT_TO_BATTLE) {
        int inviter_id = sessions[uid].inviter_id;
        int bid = sessions[uid].bid;

        if (battles[bid].is_alloced) {
            send_to_client_with_username(inviter_id, SERVER_MESSAGE_FRIEND_ACCEPT_BATTLE, sessions[inviter_id].user_name);
            user_join_battle(bid, uid);
            logi("accept success\n");
        } else {
            logi("user %s accept battle which didn't exist\n", sessions[uid].user_name);
            send_to_client(uid, SERVER_RESPONSE_YOURE_ALREADY_IN_BATTLE);
        }

    } else {
        logi("hasn't been invited\n");
        send_to_client(uid, SERVER_RESPONSE_NOBODY_INVITE_YOU);
    }

    return 0;
}

int client_command_reject_battle(int uid) {
    log("user %s reject battle #%d\n", sessions[uid].user_name, sessions[uid].bid);
    if (sessions[uid].state == USER_STATE_BATTLE) {
        logi("user already in battle\n");
        send_to_client(uid, SERVER_RESPONSE_YOURE_ALREADY_IN_BATTLE);
    } else if (sessions[uid].state == USER_STATE_WAIT_TO_BATTLE) {
        logi("reject success\n");
        int bid = sessions[uid].bid;
        send_to_client(sessions[uid].inviter_id, SERVER_MESSAGE_FRIEND_REJECT_BATTLE);
        sessions[uid].state = USER_STATE_LOGIN;
        battles[bid].users[uid].battle_state = BATTLE_STATE_UNJOINED;
    } else {
        logi("hasn't been invited\n");
        send_to_client(uid, SERVER_RESPONSE_NOBODY_INVITE_YOU);
    }
    return 0;
}

int client_command_quit(int uid) {
    int conn = sessions[uid].conn;
    if (sessions[uid].state == USER_STATE_BATTLE
    || sessions[uid].state == USER_STATE_WAIT_TO_BATTLE) {
        log("user %d@%s tries to quit client was in battle\n", uid, sessions[uid].user_name);
        user_quit_battle(sessions[uid].bid, uid);
    }

    sessions[uid].conn = -1;
    log("user %d@%s quit\n", uid, sessions[uid].user_name);
    sessions[uid].state = USER_STATE_UNUSED;
    close(conn);
    return -1;
}

int client_command_move_up(int uid) {
    log("user %s move up\n", sessions[uid].user_name);
    int bid = sessions[uid].bid;
    battles[bid].users[uid].dir = DIR_UP;
    if (battles[bid].users[uid].pos.y > 0) {
        battles[bid].users[uid].pos.y --;
    }
    return 0;
}

int client_command_move_down(int uid) {
    log("user %s move down\n", sessions[uid].user_name);
    int bid = sessions[uid].bid;
    battles[bid].users[uid].dir = DIR_DOWN;
    if (battles[bid].users[uid].pos.y < BATTLE_H - 1) {
        battles[bid].users[uid].pos.y ++;
    }
    return 0;
}

int client_command_move_left(int uid) {
    log("user %s move left\n", sessions[uid].user_name);
    int bid = sessions[uid].bid;
    battles[bid].users[uid].dir = DIR_LEFT;
    if (battles[bid].users[uid].pos.x > 0) {
        battles[bid].users[uid].pos.x --;
    }
    return 0;
}

int client_command_move_right(int uid) {
    log("user %s move right\n", sessions[uid].user_name);
    int bid = sessions[uid].bid;
    battles[bid].users[uid].dir = DIR_RIGHT;
    if (battles[bid].users[uid].pos.x < BATTLE_W - 1) {
        battles[bid].users[uid].pos.x ++;
    }
    return 0;
}

int client_command_fire(int uid) {
    log("user %s fire up\n", sessions[uid].user_name);
    int bid = sessions[uid].bid;
    int item_id = get_unused_item(bid);
    log("alloc item %d for bullet\n", item_id);
    if (item_id == -1) return 0;

    if (battles[bid].users[uid].nr_bullets <= 0) {
        send_to_client(uid, SERVER_MESSAGE_YOUR_MAGAZINE_IS_EMPTY);
        return 0;
    }

    int x = battles[bid].users[uid].pos.x;
    int y = battles[bid].users[uid].pos.y;
    int dir = battles[bid].users[uid].dir;
    log("bullet, %s@(%d, %d), direct to %d\n",
            sessions[uid].user_name, x, y, dir);
    battles[bid].items[item_id].kind = ITEM_BULLET;
    battles[bid].items[item_id].dir = dir;
    battles[bid].items[item_id].owner = uid;
    battles[bid].items[item_id].pos.x = x;
    battles[bid].items[item_id].pos.y = y;

    battles[bid].users[uid].nr_bullets --;

    return 0;
}

int client_command_fire_up(int uid) {
    log("user %s fire up\n", sessions[uid].user_name);
    int bid = sessions[uid].bid;
    int item_id = get_unused_item(bid);
    log("alloc item %d for bullet\n", item_id);
    if (item_id == -1) return 0;

    if (battles[bid].users[uid].nr_bullets <= 0) {
        send_to_client(uid, SERVER_MESSAGE_YOUR_MAGAZINE_IS_EMPTY);
        return 0;
    }

    int x = battles[bid].users[uid].pos.x;
    int y = battles[bid].users[uid].pos.y;
    log("bullet, %s@(%d, %d), direct to %d\n",
            sessions[uid].user_name, x, y, DIR_UP);
    battles[bid].items[item_id].kind = ITEM_BULLET;
    battles[bid].items[item_id].dir = DIR_UP;
    battles[bid].items[item_id].owner = uid;
    battles[bid].items[item_id].pos.x = x;
    battles[bid].items[item_id].pos.y = y;

    battles[bid].users[uid].nr_bullets --;

    return 0;
}
int client_command_fire_down(int uid) {
    log("user %s fire\n", sessions[uid].user_name);
    int bid = sessions[uid].bid;
    int item_id = get_unused_item(bid);
    log("alloc item %d for bullet\n", item_id);
    if (item_id == -1) return 0;

    if (battles[bid].users[uid].nr_bullets <= 0) {
        send_to_client(uid, SERVER_MESSAGE_YOUR_MAGAZINE_IS_EMPTY);
        return 0;
    }

    int x = battles[bid].users[uid].pos.x;
    int y = battles[bid].users[uid].pos.y;
    log("bullet, %s@(%d, %d), direct to %d\n",
            sessions[uid].user_name, x, y, DIR_DOWN);
    battles[bid].items[item_id].kind = ITEM_BULLET;
    battles[bid].items[item_id].dir = DIR_DOWN;
    battles[bid].items[item_id].owner = uid;
    battles[bid].items[item_id].pos.x = x;
    battles[bid].items[item_id].pos.y = y;

    battles[bid].users[uid].nr_bullets --;

    return 0;
}
int client_command_fire_left(int uid) {
    log("user %s fire\n", sessions[uid].user_name);
    int bid = sessions[uid].bid;
    int item_id = get_unused_item(bid);
    log("alloc item %d for bullet\n", item_id);
    if (item_id == -1) return 0;

    if (battles[bid].users[uid].nr_bullets <= 0) {
        send_to_client(uid, SERVER_MESSAGE_YOUR_MAGAZINE_IS_EMPTY);
        return 0;
    }

    int x = battles[bid].users[uid].pos.x;
    int y = battles[bid].users[uid].pos.y;
    log("bullet, %s@(%d, %d), direct to %d\n",
            sessions[uid].user_name, x, y, DIR_LEFT);
    battles[bid].items[item_id].kind = ITEM_BULLET;
    battles[bid].items[item_id].dir = DIR_LEFT;
    battles[bid].items[item_id].owner = uid;
    battles[bid].items[item_id].pos.x = x;
    battles[bid].items[item_id].pos.y = y;

    battles[bid].users[uid].nr_bullets --;

    return 0;
}
int client_command_fire_right(int uid) {
    log("user %s fire\n", sessions[uid].user_name);
    int bid = sessions[uid].bid;
    int item_id = get_unused_item(bid);
    log("alloc item %d for bullet\n", item_id);
    if (item_id == -1) return 0;

    if (battles[bid].users[uid].nr_bullets <= 0) {
        send_to_client(uid, SERVER_MESSAGE_YOUR_MAGAZINE_IS_EMPTY);
        return 0;
    }

    int x = battles[bid].users[uid].pos.x;
    int y = battles[bid].users[uid].pos.y;
    log("bullet, %s@(%d, %d), direct to %d\n",
            sessions[uid].user_name, x, y, DIR_RIGHT);
    battles[bid].items[item_id].kind = ITEM_BULLET;
    battles[bid].items[item_id].dir = DIR_RIGHT;
    battles[bid].items[item_id].owner = uid;
    battles[bid].items[item_id].pos.x = x;
    battles[bid].items[item_id].pos.y = y;

    battles[bid].users[uid].nr_bullets --;

    return 0;
}

int client_message_fatal(int uid) {
    loge("received FATAL from user `%s`: %d \n", sessions[uid].user_name, uid);
    for (int i = 0; i < USER_CNT; i++) {
        if (sessions[i].conn >= 0) {
            send_to_client(i, SERVER_MESSAGE_FATAL);
            log("send FATAL to user `%s`: %d\n", sessions[i].user_name, i);
        }
    }
    terminate_process(2);
    return 0;
}

static int(*handler[])(int) = {
    [CLIENT_COMMAND_USER_QUIT] = client_command_quit,
    [CLIENT_COMMAND_USER_REGISTER] = client_command_user_register,
    [CLIENT_COMMAND_USER_LOGIN] = client_command_user_login,
    [CLIENT_COMMAND_USER_LOGOUT] = client_command_user_logout,
    [CLIENT_COMMAND_FETCH_ALL_USERS] = client_command_fetch_all_users,
    [CLIENT_COMMAND_FETCH_ALL_FRIENDS] = client_command_fetch_all_friends,
    [CLIENT_COMMAND_LAUNCH_BATTLE] = client_command_launch_battle,
    [CLIENT_COMMAND_QUIT_BATTLE] = client_command_quit_battle,
    [CLIENT_COMMAND_ACCEPT_BATTLE] = client_command_accept_battle,
    [CLIENT_COMMAND_REJECT_BATTLE] = client_command_reject_battle,
    [CLIENT_COMMAND_INVITE_USER] = client_command_invite_user,
    [CLIENT_COMMAND_SEND_MESSAGE] = client_command_send_message,
    [CLIENT_COMMAND_MOVE_UP] = client_command_move_up,
    [CLIENT_COMMAND_MOVE_DOWN] = client_command_move_down,
    [CLIENT_COMMAND_MOVE_LEFT] = client_command_move_left,
    [CLIENT_COMMAND_MOVE_RIGHT] = client_command_move_right,
    [CLIENT_COMMAND_FIRE] = client_command_fire,
    [CLIENT_COMMAND_FIRE_UP] = client_command_fire_up,
    [CLIENT_COMMAND_FIRE_DOWN] = client_command_fire_down,
    [CLIENT_COMMAND_FIRE_LEFT] = client_command_fire_left,
    [CLIENT_COMMAND_FIRE_RIGHT] = client_command_fire_right,
    [CLIENT_MESSAGE_FATAL] = client_message_fatal,
};

void wrap_recv(int conn, client_message_t *pcm) {
    size_t total_len = 0;
    while (total_len < sizeof(client_message_t)) {
        size_t len = recv(conn, pcm + total_len, sizeof(client_message_t) - total_len, 0);
        if (len < 0) {
            loge("broken pipe\n");
        }

        total_len += len;
    }
}

void wrap_send(int conn, server_message_t *psm) {
    size_t total_len = 0;
    while (total_len < sizeof(server_message_t)) {
        size_t len = send(conn, psm + total_len, sizeof(server_message_t) - total_len, 0);
        if (len < 0) {
            loge("broken pipe\n");
        }

        total_len += len;
    }
}

void send_to_client(int uid, int message) {
    int conn = sessions[uid].conn;
    server_message_t sm;
    memset(&sm, 0, sizeof(server_message_t));
    sm.response = message;
    wrap_send(conn, &sm);
}

void send_to_client_with_username(int uid, int message, char *user_name) {
    int conn = sessions[uid].conn;
    server_message_t sm;
    memset(&sm, 0, sizeof(server_message_t));
    sm.response = message;
    strncpy(sm.friend_name, user_name, USERNAME_SIZE - 1);
    wrap_send(conn, &sm);
}

void close_session(int conn, int message) {
    send_to_client(conn, message);
    close(conn);
}

void *session_start(void *args) {
    int uid = -1;
    int conn = (int)(uintptr_t)args;
    client_message_t *pcm = NULL;
    if ((uid = get_unused_session()) < 0) {
        close_session(conn, SERVER_RESPONSE_LOGIN_FAIL_SERVER_LIMITS);
        return NULL;
    } else {
        sessions[uid].conn = conn;
        pcm = &sessions[uid].cm;
        memset(pcm, 0, sizeof(client_message_t));
        log("build session #%d\n", uid);
    }

    while (1) {
        wrap_recv(conn, pcm);
        if (pcm->command >= CLIENT_COMMAND_END)
            continue;

        int ret_code = handler[pcm->command](uid);
        log("state of user '%s': %d\n", sessions[uid].user_name, sessions[uid].state);
        if (ret_code < 0) {
            log("close session #%d\n", uid);
            break;
        }
    }
    return NULL;
}

void *run_battle(void *args) {
    // TODO:
    return NULL;
}

int server_start() {
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
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
            loge("Can not bind to port %d!\n", cur_port);
        } else {
            binded = true;
            port = cur_port;
            break;
        }
    }
    if (!binded) {
        eprintf("Can not start server.");
    }

    if (listen(sockfd, USER_CNT) == -1) {
        eprintf("fail to listen on socket.\n");
    } else {
        log("Listen on port %d.\n", port);
    }

    return sockfd;
}

void terminate_process(int recved_signal) {
    for (int i = 0; i < USER_CNT; i++) {
        if (sessions[i].conn >= 0) {
            send_to_client(i, SERVER_MESSAGE_QUIT);
            close(sessions[i].conn);
            log("close conn:%d\n", sessions[i].conn);
        }
    }

    if (server_fd) {
        close(server_fd);
        log("close server fd:%d\n", server_fd);
    }

    pthread_mutex_destroy(&sessions_lock);
    pthread_mutex_destroy(&battles_lock);
    for (int i = 0; i < USER_CNT; i++) {
        pthread_mutex_destroy(&items_lock[i]);
    }

    log("receive terminate signal and exit(%d)\n", recved_signal);
    exit(recved_signal);
}

int main(int argc, char *argv[]) {
    if (argc == 2) {
        port = atoi(argv[1]);
    }
    srand(time(NULL));

    pthread_t thread;

    if (signal(SIGINT, terminate_process) == SIG_ERR) {
        eprintf("An error occurred while setting a signal handler.\n");
    }

    for (int i = 0; i < USER_CNT; i++) {
        pthread_mutex_init(&items_lock[i], NULL);
    }
    log("server " VERSION "\n");

    server_fd = server_start();
    load_user_list();

    for (int i = 0; i < USER_CNT; i++)
        sessions[i].conn = -1;

    struct sockaddr_in client_addr;
    socklen_t length = sizeof(client_addr);
    while (1) {
        int conn = accept(server_fd, (struct sockaddr*)&client_addr, &length);
        log("connected by %s:%d, conn:%d\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port, conn);
        if (conn < 0) {
            loge("fail to accept client.\n");
        } else if (pthread_create(&thread, NULL, session_start, (void *)(uintptr_t)conn) != 0) {
            loge("fail to create thread.\n");
        }
        logi("bind thread #%lu\n", thread);
    }

    return 0;
}
