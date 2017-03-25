#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>

#include "common.h"

#define PORT 50000

void *handler() {
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
	int sockfd = server_start();

	pthread_t thread;

	struct sockaddr_in client_addr;
	socklen_t length = sizeof(client_addr);
	while(1) {
		int conn = accept(sockfd, (struct sockaddr*)&client_addr, &length);
		log("connected by %s:%d\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port);
		if(conn < 0) {
			fprintf(stderr, "fail to accept client.\n");
		}else if(pthread_create(&thread, NULL, handler, &conn) != 0) {
			fprintf(stderr, "fail to create thread.\n");
		}
	}

	return 0;
}
