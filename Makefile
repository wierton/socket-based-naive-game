.PHONY:run-client run-server clean

all:server client

server:server.c common.h
	gcc -Wall -std=c11 server.c -o server -lpthread

client:client.c common.h
	gcc -Wall -std=c11 client.c -o client

clean:
	rm server client
