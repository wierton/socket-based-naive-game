.PHONY:run-client run-server clean

all:server client

server:server.c common.h
	gcc -Wall -std=c11 server.c -o server -lpthread

client:server.c common.h
	echo compile client

clean:
	rm server client
